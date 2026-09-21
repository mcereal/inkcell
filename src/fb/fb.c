#define _POSIX_C_SOURCE 200809L

/*
 * The device UI: /dev/fb0, the page flip, and the backend vtable.
 *
 * Copy changed spans into page 0 and its page 1 mirror, then request FBIOPAN_DISPLAY. The Brick's
 * display engine composites fb0 with per-pixel alpha, so every pixel is written opaque; see
 * compose_color() in src/fb/fb_draw.c.
 */

#include "inkcell/ui/fb_draw.h"

#include "inkcell/ui/fb.h"
#include "inkcell/ui/latency.h"

#include "inkwell/base/env.h"
#include "inkwell/base/log.h"
#include "inkwell/base/time.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

/*
 * The panel behind the drawing state: the descriptor, the pages, the pan and the copy of the
 * last frame a damage comparison is made against.
 *
 * None of it is a renderer's business, which is why it is here rather than on `struct
 * inkcell_draw_state`. A frame needs to know where its pixels go and how one is spelled -
 * that is the `surface` on the state - and nothing at all about how they later reach a display.
 * A backend presenting through a window keeps a texture and a swapchain here instead, and every
 * screen above carries on unchanged.
 *
 * `state` is first so the two are one allocation and the vtable can hand out either.
 */
struct inkcell_fb_panel {
    struct inkcell_draw_state state;
    int fd;
    struct fb_fix_screeninfo fix;
    struct fb_var_screeninfo var;
    /* The frame is drawn in ordinary RAM and copied from there: the blend reads back the pixel
       it is about to write over, and reading the display mapping to do it is expensive on the
       device. `previous_frame` is what the copy compares against. */
    uint8_t *draw_buffer;
    uint8_t *previous_frame;
    bool frame_valid;
    bool pan_failed_logged;
};

static struct inkcell_fb_panel *inkcell_fb_panel_of(void *state_ptr) {
    return (struct inkcell_fb_panel *)state_ptr;
}

/*
 * The look this run is drawn with.
 *
 * <PREFIX>_THEME names it (see src/ui/theme/theme.c for the list) and <PREFIX>_FB_SCALE
 * overrides the glyph multiplier the theme asks for - an environment variable rather than a flag
 * because on the Brick the app is started by launch.sh, not by anyone with a shell.
 */
static void inkcell_fb_apply_theme_from_env(struct inkcell_draw_state *state) {
    const struct inkcell_theme *theme = inkcell_theme_from_env();
    /* A scale named in the environment outlives a theme switch: it is an explicit choice about
       this panel, where a theme's own scale is only that theme's default. */
    state->scale_pinned = inkwell_env_get("INKCELL_FB_SCALE") != NULL;
    /*
     * The knob stays in *whole* steps, where the scale it sets is in units.
     *
     * Everything inside counts quarters now, but this is the number somebody types on a device
     * over ssh, and <PREFIX>_FB_SCALE=4 has meant "the body size" for as long as there has been
     * one. Reading it in units would quietly halve every existing invocation, so the conversion
     * happens here - the one place the outside world states a scale.
     */
    const int steps =
        (int)inkwell_env_int("INKCELL_FB_SCALE", INKCELL_SCALE_MIN / INKCELL_SCALE_UNIT,
                             INKCELL_SCALE_MAX / INKCELL_SCALE_UNIT, 0);
    const int scale = steps > 0 ? INKCELL_SCALE(steps) : inkcell_theme_scale(theme);
    inkcell_fb_state_set_theme(state, theme, scale);
}

/*
 * The kernel's description of a pixel, in inkcell's terms.
 *
 * The whole of the conversion, and the only place in the library that reads a bitfield off an
 * FBIOGET_VSCREENINFO. Everything above draws against `struct inkcell_pixel_format`, which is
 * why this is four assignments in a backend rather than a type in a public header.
 *
 * `msb_right` is not carried across: it is zero on every driver this has ever run on, and a
 * format that needed it would need it in the packing too - which would be a change to
 * compose_color(), not a field quietly ignored here.
 */
static struct inkcell_pixel_format inkcell_fb_format_from_var(const struct fb_var_screeninfo *var) {
    return (struct inkcell_pixel_format){
        .r = {(uint8_t)var->red.offset, (uint8_t)var->red.length},
        .g = {(uint8_t)var->green.offset, (uint8_t)var->green.length},
        .b = {(uint8_t)var->blue.offset, (uint8_t)var->blue.length},
        .a = {(uint8_t)var->transp.offset, (uint8_t)var->transp.length},
        .bits_per_pixel = (uint8_t)var->bits_per_pixel,
    };
}

static int inkcell_backend_fb_init(void **state_out, void *userdata) {
    struct inkcell_backend_fb_context *context = (struct inkcell_backend_fb_context *)userdata;
    if (context == NULL) {
        return -EINVAL;
    }

    static struct inkcell_fb_panel panel_storage;
    struct inkcell_fb_panel *panel = &panel_storage;
    memset(panel, 0, sizeof *panel);
    struct inkcell_draw_state *state = &panel->state;

    panel->fd = open("/dev/fb0", O_RDWR);
    if (panel->fd < 0) {
        inkwell_log_warn("ui", "Failed to open /dev/fb0: %s", strerror(errno));
        return -errno;
    }

    if (ioctl(panel->fd, FBIOGET_FSCREENINFO, &panel->fix) < 0) {
        inkwell_log_warn("ui", "FBIOGET_FSCREENINFO failed: %s", strerror(errno));
        close(panel->fd);
        panel->fd = -1;
        return -errno;
    }

    if (ioctl(panel->fd, FBIOGET_VSCREENINFO, &panel->var) < 0) {
        inkwell_log_warn("ui", "FBIOGET_VSCREENINFO failed: %s", strerror(errno));
        close(panel->fd);
        panel->fd = -1;
        return -errno;
    }

    /* Widened before the multiply, not after: both operands are 32-bit, so the product is a
       32-bit one whatever it is assigned to. The kernel's own numbers put an overflow out of
       reach today, and a wrapped size would be a short mapping that every later bounds check
       believes - so it takes the cast page_bytes below already takes. */
    const size_t mapping_bytes = (size_t)panel->fix.line_length * (size_t)panel->var.yres_virtual;
    uint8_t *mapping = mmap(NULL, mapping_bytes, PROT_READ | PROT_WRITE, MAP_SHARED, panel->fd, 0);
    if (mapping == MAP_FAILED) {
        inkwell_log_warn("ui", "mmap on framebuffer failed: %s", strerror(errno));
        close(panel->fd);
        panel->fd = -1;
        return -errno;
    }

    state->surface = (struct inkcell_surface){
        .pixels = mapping,
        .size = mapping_bytes,
        .width = panel->var.xres,
        .height = panel->var.yres,
        .stride = panel->fix.line_length,
        .bytes_per_pixel = panel->var.bits_per_pixel / 8U,
        .format = inkcell_fb_format_from_var(&panel->var),
    };

    const size_t page_bytes = (size_t)state->surface.stride * state->surface.height;
    if (page_bytes <= state->surface.size) {
        panel->draw_buffer = calloc(1U, page_bytes);
        panel->previous_frame = calloc(1U, page_bytes);
        if (panel->draw_buffer == NULL || panel->previous_frame == NULL) {
            free(panel->draw_buffer);
            free(panel->previous_frame);
            panel->draw_buffer = NULL;
            panel->previous_frame = NULL;
            inkwell_log_warn("ui", "Frame buffers unavailable; drawing directly");
        }
    }
    inkcell_fb_apply_theme_from_env(state);
    inkcell_fb_set_app(state, context->app);

    inkwell_log_info("ui",
                     "Framebuffer UI backend active (%ux%u %u bpp, virtual %ux%u, offset %u,%u, "
                     "theme %s at scale %d)",
                     panel->var.xres, panel->var.yres, panel->var.bits_per_pixel,
                     panel->var.xres_virtual, panel->var.yres_virtual, panel->var.xoffset,
                     panel->var.yoffset, state->theme->id, state->scale);

    if (state_out != NULL) {
        *state_out = panel;
    }
    return 0;
}

static void inkcell_backend_fb_shutdown(void *state_ptr, void *userdata) {
    struct inkcell_fb_panel *panel = inkcell_fb_panel_of(state_ptr);
    if (panel != NULL) {
        struct inkcell_draw_state *state = &panel->state;
        inkcell_fb_set_app(state, NULL);
        inkcell_fb_glyph_cache_free(state);
        free(panel->draw_buffer);
        free(panel->previous_frame);
        panel->draw_buffer = NULL;
        panel->previous_frame = NULL;
        if (state->surface.pixels != NULL && state->surface.pixels != MAP_FAILED) {
            munmap(state->surface.pixels, state->surface.size);
            state->surface.pixels = NULL;
        }
        if (panel->fd >= 0) {
            close(panel->fd);
            panel->fd = -1;
        }
    }
    (void)userdata;
}

/*
 * The Brick's fb0 is 1024x16384: a stack of 768-row pages that NextUI's SDL flips between, and
 * the Allwinner display engine keeps showing whichever page SDL last presented (observed:
 * rows 768..1535, i.e. page 1) after the launcher hands over. Drawing at row 0 is then
 * invisible. Pan the display back to page 0 after each frame and, in case the driver ignores
 * the pan, mirror changed spans into page 1 as well. Compare in ordinary RAM: reading
 * the display mapping to find differences would itself be expensive on the device.
 */
size_t inkcell_fb_copy_damage(struct inkcell_draw_state *state, const uint8_t *frame,
                              uint8_t *previous, bool force, bool mirror) {
    if (state == NULL || frame == NULL || previous == NULL) {
        return 0U;
    }
    const size_t stride = state->surface.stride;
    const size_t page_bytes = stride * state->surface.height;
    const size_t bpp = state->surface.bytes_per_pixel;
    if (bpp == 0U || page_bytes > state->surface.size) {
        return 0U;
    }
    /* A caller may ask for the mirror and still not get it: whether there is a second page to
       write is the surface's answer, not the caller's, and a mirror that did not check would
       write `page_bytes` past the end of a single-page mapping. */
    mirror = mirror && page_bytes <= state->surface.size / 2U;
    size_t written = 0U;
    for (uint32_t y = 0U; y < state->surface.height; ++y) {
        /* The clip band, and the rows something animated into this frame from outside it -
           see inkcell_fb_animation_damage(). A widget that slid above or below the band is a
           row the band would otherwise skip, and skipping it leaves the last position of a
           moving thing on the panel. */
        if (!force && state->clip_active &&
            ((int)y < state->clip.y || (int)y >= state->clip.bottom) &&
            !(state->animation_damage.valid && (int)y >= state->animation_damage.y &&
              (int)y < state->animation_damage.bottom)) {
            continue;
        }
        const size_t offset = (size_t)y * stride;
        const uint8_t *src = frame + offset;
        uint8_t *old = previous + offset;
        size_t first = 0U;
        size_t end = stride;
        if (!force) {
            if (memcmp(src, old, stride) == 0) {
                continue;
            }
            while (first < end && src[first] == old[first]) {
                ++first;
            }
            while (end > first && src[end - 1U] == old[end - 1U]) {
                --end;
            }
            /* Whole pixels, including when the stride itself has padding. */
            first -= first % bpp;
            end = ((end + bpp - 1U) / bpp) * bpp;
            if (end > stride) {
                end = stride;
            }
        }
        const size_t bytes = end - first;
        memcpy(state->surface.pixels + offset + first, src + first, bytes);
        if (mirror) {
            memcpy(state->surface.pixels + page_bytes + offset + first, src + first, bytes);
        }
        memcpy(old + first, src + first, bytes);
        written += bytes * (mirror ? 2U : 1U);
    }
    return written;
}

static void inkcell_fb_show_page0(struct inkcell_fb_panel *panel) {
    struct fb_var_screeninfo var = panel->var;
    var.xoffset = 0U;
    var.yoffset = 0U;
    if (ioctl(panel->fd, FBIOPAN_DISPLAY, &var) < 0) {
        if (!panel->pan_failed_logged) {
            inkwell_log_warn("ui", "FBIOPAN_DISPLAY failed: %s; relying on the mirrored page",
                             strerror(errno));
            panel->pan_failed_logged = true;
        }
    }
}

static void inkcell_backend_fb_present(void *state_ptr, const void *snapshot, void *userdata) {
    struct inkcell_fb_panel *panel = inkcell_fb_panel_of(state_ptr);
    (void)userdata;
    if (panel == NULL || snapshot == NULL || panel->state.surface.pixels == NULL) {
        return;
    }
    struct inkcell_draw_state *state = &panel->state;

    /* One clock reading per frame, taken here rather than inside the drawing code: a widget
       that read the clock for itself would draw two halves of one frame at two different
       times, and a capture could not pin either of them. */
    inkcell_fb_state_set_now(state, inkwell_time_monotonic_ms());
    inkcell_latency_frame_begin();
    const size_t page_bytes = (size_t)state->surface.stride * state->surface.height;
    size_t written;
    if (panel->draw_buffer != NULL) {
        /* Drawn into ordinary RAM and copied from there, which is what makes the blend able to
           read back the pixel it is about to write over - see the anti-aliasing note in
           fb_draw.c. Only the destination changes, so the surface is pointed at the draw buffer
           for the render and put back afterwards. */
        uint8_t *const mapping = state->surface.pixels;
        const size_t mapping_size = state->surface.size;
        state->surface.pixels = panel->draw_buffer;
        state->surface.size = page_bytes;
        inkcell_fb_render(state, snapshot);
        state->surface.pixels = mapping;
        state->surface.size = mapping_size;
        written = inkcell_fb_copy_damage(state, panel->draw_buffer, panel->previous_frame,
                                         !panel->frame_valid,
                                         panel->var.yres_virtual >= 2U * state->surface.height);
        panel->frame_valid = true;
    } else {
        state->partial_disabled = true;
        inkcell_fb_render(state, snapshot);
        written = page_bytes;
        if (panel->var.yres_virtual >= 2U * state->surface.height &&
            page_bytes <= state->surface.size / 2U) {
            memcpy(state->surface.pixels + page_bytes, state->surface.pixels, page_bytes);
            written *= 2U;
        }
    }
    inkcell_latency_frame_drawn(written);
    if (written > 0U) {
        inkcell_fb_show_page0(panel);
        const size_t pages = panel->var.yres_virtual >= 2U * state->surface.height &&
                                     page_bytes <= state->surface.size / 2U
                                 ? 2U
                                 : 1U;
        msync(state->surface.pixels, page_bytes * pages, MS_ASYNC);
    }
    /* After the pan, because the pan is what puts the frame in front of the reader - a
       measurement that stopped at the end of the draw would be timing this function rather
       than answering how long the press took. */
    inkcell_latency_frame_end(written);
}

static bool inkcell_backend_fb_animating(void *state_ptr, void *userdata) {
    (void)userdata;
    struct inkcell_fb_panel *panel = inkcell_fb_panel_of(state_ptr);
    return panel != NULL && inkcell_fb_state_animating(&panel->state);
}

static uint32_t inkcell_backend_fb_page_rows(void *state_ptr, void *userdata) {
    (void)userdata;
    struct inkcell_fb_panel *panel = inkcell_fb_panel_of(state_ptr);
    return panel != NULL ? panel->state.page_rows : 0U;
}

/*
 * What the last frame registered, which is whatever the application pushed in and the
 * components filled. Held rather than copied: it is rebuilt every frame and read before the
 * next one, so the pointer on the state is the answer.
 */
static const struct inkcell_focus_map *inkcell_backend_fb_focus_map(void *state_ptr,
                                                                    void *userdata) {
    (void)userdata;
    struct inkcell_fb_panel *panel = inkcell_fb_panel_of(state_ptr);
    return panel != NULL ? panel->state.focus : NULL;
}

static const struct inkcell_backend k_fb_backend = {
    .name = "fb",
    .init = inkcell_backend_fb_init,
    .shutdown = inkcell_backend_fb_shutdown,
    .present = inkcell_backend_fb_present,
    .animating = inkcell_backend_fb_animating,
    .page_rows = inkcell_backend_fb_page_rows,
    .focus_map = inkcell_backend_fb_focus_map,
};

bool inkcell_backend_fb_is_available(void) {
    return access("/dev/fb0", R_OK | W_OK) == 0;
}

const struct inkcell_backend *inkcell_backend_fb(void) {
    return &k_fb_backend;
}
