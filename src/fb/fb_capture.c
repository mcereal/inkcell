#define _POSIX_C_SOURCE 200809L

/*
 * Off-screen framebuffer rendering.
 *
 * This is the fb backend with the device taken out of it: same inkcell_fb_render(), same
 * palette, same cell measurement, drawing into a malloc'd page instead of an mmap of
 * /dev/fb0. It belongs in this directory rather than in a tool because it is the other half of
 * the fb backend: it shares fb.c's state, its palette and its page geometry, and a tool that
 * rebuilt those would be a second renderer to keep in step with this one.
 *
 * The theme comes from <PREFIX>_THEME like the device backend's does, and
 * inkcell_capture_set_theme() overrides it - which is how one scene script renders the same
 * frames in four looks.
 *
 * The surface's pixel format leaves every channel zero. compose_color() then takes its 32 bpp
 * path and packs 0xFFRRGGBB, which is what the Brick's fb0 actually holds - so a page from here
 * is interchangeable with a page dd'd off the device.
 */

#include "inkcell/ui/fb_draw.h"

#include "inkcell/ui/fb_capture.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct inkcell_capture {
    struct inkcell_draw_state state;
    uint32_t width;
    uint32_t height;
};

int inkcell_capture_open(struct inkcell_capture **out, uint32_t width, uint32_t height, int scale) {
    if (out == NULL || width == 0U || height == 0U) {
        return -EINVAL;
    }
    /* 4 bytes a pixel, and the guard is against width * height overflowing size_t on a 32-bit
       host rather than against anything a caller is likely to ask for. */
    if (width > UINT32_MAX / 4U || height > UINT32_MAX / (width * 4U)) {
        return -EINVAL;
    }

    struct inkcell_capture *capture = calloc(1U, sizeof *capture);
    if (capture == NULL) {
        return -ENOMEM;
    }

    const size_t stride = (size_t)width * 4U;
    const size_t page_bytes = stride * (size_t)height;
    uint8_t *pixels = calloc(1U, page_bytes);
    if (pixels == NULL) {
        free(capture);
        return -ENOMEM;
    }

    capture->width = width;
    capture->height = height;

    struct inkcell_draw_state *state = &capture->state;
    state->surface = (struct inkcell_surface){
        .pixels = pixels,
        .size = page_bytes,
        .width = width,
        .height = height,
        .stride = (uint32_t)stride,
        .bytes_per_pixel = 4U,
        .format = {.bits_per_pixel = 32U},
    };
    inkcell_fb_state_set_theme(state, inkcell_theme_from_env(), scale);
    /* A scale the caller named is theirs to keep: a theme arriving later in a snapshot must
       not quietly swap it for that theme's default. 0 meant "the theme's own", which is not a
       choice to preserve. */
    state->scale_pinned = (scale > 0);

    *out = capture;
    return 0;
}

struct inkcell_draw_state *inkcell_capture_state(struct inkcell_capture *capture) {
    return capture == NULL ? NULL : &capture->state;
}

void inkcell_capture_close(struct inkcell_capture *capture) {
    if (capture == NULL) {
        return;
    }
    inkcell_fb_set_app(&capture->state, NULL);
    inkcell_fb_glyph_cache_free(&capture->state);
    free(capture->state.surface.pixels);
    free(capture);
}

void inkcell_capture_set_reference(struct inkcell_capture *capture, bool reference) {
    if (capture != NULL) {
        capture->state.partial_disabled = reference;
        capture->state.thread_cache_disabled = reference;
        /* A reference render must not read a memo taken under the other mode, and what is
           memoised is the app's - so it is asked to drop it rather than told how. */
        inkcell_fb_app_drop_caches(&capture->state);
    }
}

void inkcell_capture_set_scale(struct inkcell_capture *capture, int scale) {
    if (capture == NULL) {
        return;
    }
    capture->state.scale = inkcell_theme_clamp_scale(capture->state.theme, scale);
    capture->state.scale_pinned = (scale > 0);
}

void inkcell_capture_set_theme(struct inkcell_capture *capture, const struct inkcell_theme *theme) {
    if (capture == NULL) {
        return;
    }
    /* The scale rides along: a theme carries one, and a capture that kept the previous theme's
       would render the new one at a size it never asks for. A caller that wants both says so by
       calling set_scale() afterwards, which is what the scene script's `scale` line does - and
       which is also what re-pins it. Naming a theme outright is an instruction; a theme
       arriving in a snapshot is not, and that one leaves a pinned scale alone. */
    inkcell_fb_state_set_theme(&capture->state, theme, 0);
    capture->state.scale_pinned = false;
}

const struct inkcell_theme *inkcell_capture_theme(const struct inkcell_capture *capture) {
    return capture != NULL ? capture->state.theme : NULL;
}

void inkcell_capture_advance(struct inkcell_capture *capture, uint32_t ms) {
    if (capture != NULL) {
        inkcell_fb_state_set_now(&capture->state, capture->state.now_ms + ms);
    }
}

uint64_t inkcell_capture_now(const struct inkcell_capture *capture) {
    return capture != NULL ? capture->state.now_ms : 0U;
}

bool inkcell_capture_animating(const struct inkcell_capture *capture) {
    return capture != NULL && inkcell_fb_state_animating(&capture->state);
}

uint32_t inkcell_capture_page_rows(const struct inkcell_capture *capture) {
    return capture != NULL ? capture->state.page_rows : 0U;
}

void inkcell_capture_render(struct inkcell_capture *capture, const void *snapshot) {
    if (capture == NULL || snapshot == NULL) {
        return;
    }
    inkcell_fb_render(&capture->state, snapshot);
}

const uint8_t *inkcell_capture_pixels(const struct inkcell_capture *capture, uint32_t *width,
                                      uint32_t *height, size_t *stride) {
    if (capture == NULL) {
        return NULL;
    }
    if (width != NULL) {
        *width = capture->width;
    }
    if (height != NULL) {
        *height = capture->height;
    }
    if (stride != NULL) {
        *stride = capture->state.surface.stride;
    }
    return capture->state.surface.pixels;
}

/* One channel of a packed pixel, brought back to eight bits. A narrow channel is widened with its
   high bits repeated into the low ones, so a 5-bit channel at full reads 255 rather than 248; a
   wide one (10 bits of XRGB2101010) keeps its top eight, which is where the renderer put them. */
static uint8_t inkcell_surface_channel(uint32_t word, struct inkcell_channel channel) {
    if (channel.length == 0U || channel.length > 16U || channel.offset >= 32U) {
        return 0U;
    }
    const uint32_t value = (word >> channel.offset) & ((1U << channel.length) - 1U);
    if (channel.length >= 8U) {
        return (uint8_t)(value >> (channel.length - 8U));
    }
    uint32_t wide = value << (8U - channel.length);
    for (uint32_t filled = channel.length; filled < 8U; filled += channel.length) {
        wide |= (uint32_t)(wide >> filled);
    }
    return (uint8_t)wide;
}

/*
 * A pixel as R, G, B, read the way compose_color() in fb_draw.c wrote it: through the format's
 * channels when it describes them, and by bit depth when it does not.
 */
static void inkcell_surface_rgb(const struct inkcell_surface *surface, const uint8_t *src,
                                uint8_t rgb[3]) {
    uint32_t word = 0U;
    for (uint32_t i = 0U; i < surface->bytes_per_pixel && i < 4U; ++i) {
        word |= (uint32_t)src[i] << (8U * i);
    }
    const struct inkcell_pixel_format *fmt = &surface->format;
    if (fmt->r.length != 0U || fmt->g.length != 0U || fmt->b.length != 0U) {
        rgb[0] = inkcell_surface_channel(word, fmt->r);
        rgb[1] = inkcell_surface_channel(word, fmt->g);
        rgb[2] = inkcell_surface_channel(word, fmt->b);
    } else if (surface->bytes_per_pixel == 2U) {
        rgb[0] = inkcell_surface_channel(word, (struct inkcell_channel){11U, 5U});
        rgb[1] = inkcell_surface_channel(word, (struct inkcell_channel){5U, 6U});
        rgb[2] = inkcell_surface_channel(word, (struct inkcell_channel){0U, 5U});
    } else {
        /* Little-endian 0xFFRRGGBB: B,G,R,X in memory. */
        rgb[0] = (uint8_t)(word >> 16);
        rgb[1] = (uint8_t)(word >> 8);
        rgb[2] = (uint8_t)word;
    }
}

int inkcell_surface_write_ppm(const struct inkcell_surface *surface, const char *path) {
    if (surface == NULL || path == NULL || surface->pixels == NULL || surface->width == 0U ||
        surface->height == 0U || surface->bytes_per_pixel == 0U || surface->bytes_per_pixel > 4U) {
        return -EINVAL;
    }
    /* Every byte the rows below read has to be inside `size`. A surface is allowed to be shorter
       than its geometry - the renderer clips to what is there - and a writer that trusted width,
       height and stride alone would read past the end of it. */
    const size_t row_bytes = (size_t)surface->width * surface->bytes_per_pixel;
    if (surface->stride < row_bytes ||
        (size_t)(surface->height - 1U) > (SIZE_MAX - row_bytes) / surface->stride ||
        (size_t)(surface->height - 1U) * surface->stride + row_bytes > surface->size) {
        return -EINVAL;
    }

    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return -errno;
    }

    int status = 0;
    if (fprintf(file, "P6\n%u %u\n255\n", surface->width, surface->height) < 0) {
        status = -EIO;
    }

    uint8_t *row = status == 0 ? malloc((size_t)surface->width * 3U) : NULL;
    if (status == 0 && row == NULL) {
        status = -ENOMEM;
    }

    for (uint32_t y = 0U; status == 0 && y < surface->height; ++y) {
        const uint8_t *src = surface->pixels + (size_t)y * surface->stride;
        for (uint32_t x = 0U; x < surface->width; ++x) {
            inkcell_surface_rgb(surface, src + (size_t)x * surface->bytes_per_pixel, &row[x * 3U]);
        }
        if (fwrite(row, 3U, surface->width, file) != surface->width) {
            status = -EIO;
        }
    }

    free(row);
    if (fclose(file) != 0 && status == 0) {
        status = -EIO;
    }
    return status;
}

int inkcell_capture_write_ppm(const struct inkcell_capture *capture, const char *path) {
    if (capture == NULL || path == NULL) {
        return -EINVAL;
    }
    return inkcell_surface_write_ppm(&capture->state.surface, path);
}
