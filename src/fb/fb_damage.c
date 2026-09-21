#define _POSIX_C_SOURCE 200809L

/*
 * What changed since the last frame, in rows.
 *
 * Drawing into ordinary RAM and copying from there is not an optimisation: it is what lets the
 * blend read back the pixel it is about to write over, which is how the anti-aliasing works at
 * all - see compose_color() in fb_draw.c. The price is that every frame then has to be handed
 * over somehow, and handing a whole page over when four rows moved is the difference between a
 * list that scrolls and one that stutters.
 *
 * This is that comparison, and it lives here rather than in fb.c because none of it is the
 * framebuffer's. A presenter uploading a texture asks the same question and wants the same
 * answer, and the file it gets that answer from should not be the one that includes
 * <linux/fb.h>. inkcell_fb_copy_damage() below is /dev/fb0's consumer of it; something with a
 * texture walks the same rows through inkcell_fb_damage_row() and uploads where this copies.
 */

#include "inkcell/ui/fb_draw.h"

#include <string.h>

/*
 * Whether row `y` changed, and the byte span [*first_out, *end_out) within it that did.
 *
 * The inside of both loops below. It reads and never writes: what to do with a span - copy it,
 * or note where it is - is the caller's, and each of them finishes a row it acted on by
 * bringing `previous` up to date over that span.
 */
static bool inkcell_fb_damage_row(const struct inkcell_draw_state *state, const uint8_t *frame,
                                  const uint8_t *previous, uint32_t y, bool force,
                                  size_t *first_out, size_t *end_out) {
    if (state == NULL || frame == NULL || previous == NULL || first_out == NULL ||
        end_out == NULL) {
        return false;
    }
    const size_t stride = state->surface.stride;
    const size_t bpp = state->surface.bytes_per_pixel;
    if (bpp == 0U || stride == 0U || y >= state->surface.height) {
        return false;
    }
    /* The clip band, and the rows something animated into this frame from outside it - see
       inkcell_fb_animation_damage(). A widget that slid above or below the band is a row the
       band would otherwise skip, and skipping it leaves the last position of a moving thing on
       the panel. */
    if (!force && state->clip_active && ((int)y < state->clip.y || (int)y >= state->clip.bottom) &&
        !(state->animation_damage.valid && (int)y >= state->animation_damage.y &&
          (int)y < state->animation_damage.bottom)) {
        return false;
    }

    const size_t offset = (size_t)y * stride;
    const uint8_t *const src = frame + offset;
    const uint8_t *const old = previous + offset;
    size_t first = 0U;
    size_t end = stride;
    if (!force) {
        if (memcmp(src, old, stride) == 0) {
            return false;
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
    *first_out = first;
    *end_out = end;
    return true;
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
    if (state->surface.bytes_per_pixel == 0U || page_bytes > state->surface.size) {
        return 0U;
    }
    /* A caller may ask for the mirror and still not get it: whether there is a second page to
       write is the surface's answer, not the caller's, and a mirror that did not check would
       write `page_bytes` past the end of a single-page mapping. */
    mirror = mirror && page_bytes <= state->surface.size / 2U;
    size_t written = 0U;
    for (uint32_t y = 0U; y < state->surface.height; ++y) {
        size_t first;
        size_t end;
        if (!inkcell_fb_damage_row(state, frame, previous, y, force, &first, &end)) {
            continue;
        }
        const size_t offset = (size_t)y * stride;
        const size_t bytes = end - first;
        memcpy(state->surface.pixels + offset + first, frame + offset + first, bytes);
        if (mirror) {
            memcpy(state->surface.pixels + page_bytes + offset + first, frame + offset + first,
                   bytes);
        }
        memcpy(previous + offset + first, frame + offset + first, bytes);
        written += bytes * (mirror ? 2U : 1U);
    }
    return written;
}

/*
 * The same rows, as rectangles.
 *
 * What a texture upload needs where the copy above needs spans: SDL_UpdateTexture() and every
 * call like it take a rectangle and a pitch, not a list of byte ranges. A run of consecutive
 * damaged rows becomes one rectangle as wide as the widest row in the run, because a run is
 * what a widget leaves behind - a list row, an app bar, a snackbar are each a band - and one
 * upload of a band beats one per row by more than the untouched pixels at the ends of the
 * narrower rows cost. Those pixels are re-sent carrying the values they already had, so the
 * widening is bandwidth and never correctness.
 *
 * `max` caps the rectangles, and the cap is not a limit on how much damage may be reported:
 * once it is reached the last rectangle grows to swallow every further run, so what comes back
 * always covers everything that changed. That is the property a caller depends on - a
 * rectangle short of the damage is a stale row left on the panel - and it is why the overflow
 * grows rather than drops.
 *
 * Returns how many rectangles were written, updating `previous` exactly as the copy does.
 * `max` of 0 is refused without consuming anything, since damage that cannot be reported must
 * not be forgotten.
 */
size_t inkcell_fb_damage_rects(struct inkcell_draw_state *state, const uint8_t *frame,
                               uint8_t *previous, bool force, struct inkcell_fb_damage_rect *out,
                               size_t max) {
    if (state == NULL || frame == NULL || previous == NULL || out == NULL || max == 0U) {
        return 0U;
    }
    const size_t stride = state->surface.stride;
    const size_t bpp = state->surface.bytes_per_pixel;
    if (bpp == 0U || stride * state->surface.height > state->surface.size) {
        return 0U;
    }

    size_t count = 0U;
    /* Whether the row before this one was damaged, which is what makes a run a run. */
    bool in_run = false;
    for (uint32_t y = 0U; y < state->surface.height; ++y) {
        size_t first;
        size_t end;
        if (!inkcell_fb_damage_row(state, frame, previous, y, force, &first, &end)) {
            in_run = false;
            continue;
        }
        const size_t offset = (size_t)y * stride;
        memcpy(previous + offset + first, frame + offset + first, end - first);

        const int x = (int)(first / bpp);
        const int right = (int)((end + bpp - 1U) / bpp);
        if (!in_run && count < max) {
            out[count++] = (struct inkcell_fb_damage_rect){
                .x = x, .y = (int)y, .right = right, .bottom = (int)y + 1, .valid = true};
        } else {
            /* Either this row continues the run above, or the rectangles are spent and the
               last one takes it. Both are the same widening. */
            struct inkcell_fb_damage_rect *const run = &out[count - 1U];
            if (x < run->x) {
                run->x = x;
            }
            if (right > run->right) {
                run->right = right;
            }
            run->bottom = (int)y + 1;
        }
        in_run = true;
    }
    return count;
}
