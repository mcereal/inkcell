#define _POSIX_C_SOURCE 200809L

/*
 * The device UI: /dev/fb0, the page flip, and the backend vtable.
 *
 * Copy changed spans into page 0 and its page 1 mirror, then request FBIOPAN_DISPLAY. The Brick's
 * display engine composites fb0 with per-pixel alpha, so every pixel is written opaque; see
 * compose_color() in inkcell_fb_draw.c.
 */

#include "inkcell/ui/fb_draw.h"

#include "inkcell/ui/fb.h"
#include "inkcell/ui/latency.h"

#include "inkcell/utils/env.h"
#include "inkcell/utils/log.h"
#include "inkcell/utils/time.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

/*
 * The look this run is drawn with.
 *
 * <PREFIX>_THEME names it (see src/ui/theme/theme.c for the list) and <PREFIX>_FB_SCALE
 * overrides the glyph multiplier the theme asks for - an environment variable rather than a flag
 * because on the Brick the app is started by launch.sh, not by anyone with a shell.
 */
static void inkcell_fb_apply_theme_from_env(struct inkcell_backend_fb_state *state) {
    const struct inkcell_theme *theme = inkcell_theme_from_env();
    /* A scale named in the environment outlives a theme switch: it is an explicit choice about
       this panel, where a theme's own scale is only that theme's default. */
    state->scale_pinned = inkcell_env_get("INKCELL_FB_SCALE") != NULL;
    const int scale = (int)inkcell_env_int("INKCELL_FB_SCALE", INKCELL_SCALE_MIN, INKCELL_SCALE_MAX,
                                           inkcell_theme_scale(theme));
    inkcell_fb_state_set_theme(state, theme, scale);
}

static int inkcell_backend_fb_init(void **state_out, void *userdata) {
    struct inkcell_backend_fb_context *context = (struct inkcell_backend_fb_context *)userdata;
    if (context == NULL) {
        return -EINVAL;
    }

    static struct inkcell_backend_fb_state state_storage;
    struct inkcell_backend_fb_state *state = &state_storage;
    memset(state, 0, sizeof *state);

    state->inkcell_fb_fd = open("/dev/fb0", O_RDWR);
    if (state->inkcell_fb_fd < 0) {
        inkcell_log_warn("ui", "Failed to open /dev/fb0: %s", strerror(errno));
        return -errno;
    }

    if (ioctl(state->inkcell_fb_fd, FBIOGET_FSCREENINFO, &state->fix) < 0) {
        inkcell_log_warn("ui", "FBIOGET_FSCREENINFO failed: %s", strerror(errno));
        close(state->inkcell_fb_fd);
        state->inkcell_fb_fd = -1;
        return -errno;
    }

    if (ioctl(state->inkcell_fb_fd, FBIOGET_VSCREENINFO, &state->var) < 0) {
        inkcell_log_warn("ui", "FBIOGET_VSCREENINFO failed: %s", strerror(errno));
        close(state->inkcell_fb_fd);
        state->inkcell_fb_fd = -1;
        return -errno;
    }

    state->bytes_per_pixel = state->var.bits_per_pixel / 8;
    state->line_bytes = state->fix.line_length;
    /* Widened before the multiply, not after: both operands are 32-bit, so the product is a
       32-bit one whatever it is assigned to. The kernel's own numbers put an overflow out of
       reach today, and a wrapped inkcell_fb_size would be a short mapping that every later bounds
       check believes - so it takes the cast page_bytes below already takes. */
    state->inkcell_fb_size = (size_t)state->line_bytes * (size_t)state->var.yres_virtual;
    state->inkcell_fb_ptr = mmap(NULL, state->inkcell_fb_size, PROT_READ | PROT_WRITE, MAP_SHARED,
                                 state->inkcell_fb_fd, 0);
    if (state->inkcell_fb_ptr == MAP_FAILED) {
        inkcell_log_warn("ui", "mmap on framebuffer failed: %s", strerror(errno));
        close(state->inkcell_fb_fd);
        state->inkcell_fb_fd = -1;
        return -errno;
    }

    const size_t page_bytes = (size_t)state->line_bytes * state->var.yres;
    if (page_bytes <= state->inkcell_fb_size) {
        state->draw_buffer = calloc(1U, page_bytes);
        state->previous_frame = calloc(1U, page_bytes);
        if (state->draw_buffer == NULL || state->previous_frame == NULL) {
            free(state->draw_buffer);
            free(state->previous_frame);
            state->draw_buffer = NULL;
            state->previous_frame = NULL;
            inkcell_log_warn("ui", "Frame buffers unavailable; drawing directly");
        }
    }
    inkcell_fb_apply_theme_from_env(state);
    inkcell_fb_set_app(state, context->app);

    inkcell_log_info("ui",
                     "Framebuffer UI backend active (%ux%u %u bpp, virtual %ux%u, offset %u,%u, "
                     "theme %s at scale %d)",
                     state->var.xres, state->var.yres, state->var.bits_per_pixel,
                     state->var.xres_virtual, state->var.yres_virtual, state->var.xoffset,
                     state->var.yoffset, state->theme->id, state->scale);

    if (state_out != NULL) {
        *state_out = state;
    }
    return 0;
}

static void inkcell_backend_fb_shutdown(void *state_ptr, void *userdata) {
    struct inkcell_backend_fb_state *state = (struct inkcell_backend_fb_state *)state_ptr;
    if (state != NULL) {
        inkcell_fb_set_app(state, NULL);
        inkcell_fb_glyph_cache_free(state);
        free(state->draw_buffer);
        free(state->previous_frame);
        state->draw_buffer = NULL;
        state->previous_frame = NULL;
        if (state->inkcell_fb_ptr != NULL && state->inkcell_fb_ptr != MAP_FAILED) {
            munmap(state->inkcell_fb_ptr, state->inkcell_fb_size);
            state->inkcell_fb_ptr = NULL;
        }
        if (state->inkcell_fb_fd >= 0) {
            close(state->inkcell_fb_fd);
            state->inkcell_fb_fd = -1;
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
size_t inkcell_fb_copy_damage(struct inkcell_backend_fb_state *state, const uint8_t *frame,
                              uint8_t *previous, bool force) {
    const size_t stride = state->line_bytes;
    const size_t page_bytes = stride * state->var.yres;
    const size_t bpp = state->bytes_per_pixel;
    if (bpp == 0U || page_bytes > state->inkcell_fb_size) {
        return 0U;
    }
    const bool mirror = state->var.yres_virtual >= 2U * state->var.yres &&
                        page_bytes <= state->inkcell_fb_size / 2U;
    size_t written = 0U;
    for (uint32_t y = 0U; y < state->var.yres; ++y) {
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
        memcpy(state->inkcell_fb_ptr + offset + first, src + first, bytes);
        if (mirror) {
            memcpy(state->inkcell_fb_ptr + page_bytes + offset + first, src + first, bytes);
        }
        memcpy(old + first, src + first, bytes);
        written += bytes * (mirror ? 2U : 1U);
    }
    return written;
}

static void inkcell_fb_show_page0(struct inkcell_backend_fb_state *state) {
    struct fb_var_screeninfo var = state->var;
    var.xoffset = 0U;
    var.yoffset = 0U;
    if (ioctl(state->inkcell_fb_fd, FBIOPAN_DISPLAY, &var) < 0) {
        if (!state->pan_failed_logged) {
            inkcell_log_warn("ui", "FBIOPAN_DISPLAY failed: %s; relying on the mirrored page",
                             strerror(errno));
            state->pan_failed_logged = true;
        }
    }
}

static void inkcell_backend_fb_present(void *state_ptr, const void *snapshot, void *userdata) {
    struct inkcell_backend_fb_state *state = (struct inkcell_backend_fb_state *)state_ptr;
    (void)userdata;
    if (state == NULL || snapshot == NULL || state->inkcell_fb_ptr == NULL) {
        return;
    }

    /* One clock reading per frame, taken here rather than inside the drawing code: a widget
       that read the clock for itself would draw two halves of one frame at two different
       times, and a capture could not pin either of them. */
    inkcell_fb_state_set_now(state, inkcell_time_monotonic_ms());
    inkcell_latency_frame_begin();
    const size_t page_bytes = (size_t)state->line_bytes * state->var.yres;
    size_t written;
    if (state->draw_buffer != NULL) {
        uint8_t *mapping = state->inkcell_fb_ptr;
        const size_t mapping_size = state->inkcell_fb_size;
        state->inkcell_fb_ptr = state->draw_buffer;
        state->inkcell_fb_size = page_bytes;
        inkcell_fb_render(state, snapshot);
        state->inkcell_fb_ptr = mapping;
        state->inkcell_fb_size = mapping_size;
        written = inkcell_fb_copy_damage(state, state->draw_buffer, state->previous_frame,
                                         !state->frame_valid);
        state->frame_valid = true;
    } else {
        state->partial_disabled = true;
        inkcell_fb_render(state, snapshot);
        written = page_bytes;
        if (state->var.yres_virtual >= 2U * state->var.yres &&
            page_bytes <= state->inkcell_fb_size / 2U) {
            memcpy(state->inkcell_fb_ptr + page_bytes, state->inkcell_fb_ptr, page_bytes);
            written *= 2U;
        }
    }
    inkcell_latency_frame_drawn(written);
    if (written > 0U) {
        inkcell_fb_show_page0(state);
        const size_t pages = state->var.yres_virtual >= 2U * state->var.yres &&
                                     page_bytes <= state->inkcell_fb_size / 2U
                                 ? 2U
                                 : 1U;
        msync(state->inkcell_fb_ptr, page_bytes * pages, MS_ASYNC);
    }
    /* After the pan, because the pan is what puts the frame in front of the reader - a
       measurement that stopped at the end of the draw would be timing this function rather
       than answering how long the press took. */
    inkcell_latency_frame_end(written);
}

static bool inkcell_backend_fb_animating(void *state_ptr, void *userdata) {
    (void)userdata;
    return inkcell_fb_state_animating((const struct inkcell_backend_fb_state *)state_ptr);
}

static uint32_t inkcell_backend_fb_page_rows(void *state_ptr, void *userdata) {
    (void)userdata;
    return state_ptr != NULL ? ((const struct inkcell_backend_fb_state *)state_ptr)->page_rows : 0U;
}

/*
 * What the last frame registered, which is whatever the application pushed in and the
 * components filled. Held rather than copied: it is rebuilt every frame and read before the
 * next one, so the pointer on the state is the answer.
 */
static const struct inkcell_focus_map *inkcell_backend_fb_focus_map(void *state_ptr,
                                                                    void *userdata) {
    (void)userdata;
    return state_ptr != NULL ? ((const struct inkcell_backend_fb_state *)state_ptr)->focus : NULL;
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
