#define _POSIX_C_SOURCE 200809L

/*
 * Pixels, glyphs and the page geometry.
 *
 * Everything above this file measures in cells rather than bytes: a name is four *characters*
 * wide whether it is "Andy" or one emoji, so inkcell_fb_cols()/inkcell_fb_fit()/inkcell_fb_width()
 * are the only legitimate way to ask how much fits. A strlen() or a "%-12s" up in a screen is a
 * bug.
 */

#include "inkcell/ui/fb_draw.h"

#include "inkcell/i18n/strings.h"
#include "inkcell/ui/emoji.h"
#include "inkcell/ui/icon.h"
#include "inkcell/utils/text.h"
#include "inkcell/utils/time.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Coverage is independent of the palette. A bounded, four-way cache holds the common
   glyph/scale pairs without retaining framebuffers or growing with incoming text. */
#define INKCELL_FB_GLYPH_CACHE_SETS 64U
#define INKCELL_FB_GLYPH_CACHE_WAYS 4U
#define INKCELL_FB_GLYPH_CACHE_PIXELS 2048U
struct inkcell_fb_cached_glyph {
    const struct inkcell_font *font;
    uint32_t codepoint;
    int scale;
    uint64_t used;
    uint8_t steps[INKCELL_FB_GLYPH_CACHE_PIXELS];
};
struct inkcell_fb_glyph_cache {
    uint64_t clock;
    struct inkcell_fb_cached_glyph entries[INKCELL_FB_GLYPH_CACHE_SETS]
                                          [INKCELL_FB_GLYPH_CACHE_WAYS];
};

void inkcell_fb_glyph_cache_free(struct inkcell_backend_fb_state *state) {
    free(state->glyph_cache);
    state->glyph_cache = NULL;
}

/* ---- the theme on the state --------------------------------------------------------------- */

/*
 * Every colour, margin and glyph size a frame uses comes through these four.
 *
 * They are trivial on purpose: the point is that there is exactly one path from "what does
 * this mean" to "which pixels", so a theme switch cannot leave a corner of the UI behind.
 */
void inkcell_fb_state_set_theme(struct inkcell_backend_fb_state *state,
                                const struct inkcell_theme *theme, int scale) {
    if (state == NULL) {
        return;
    }
    if (state->glyph_cache == NULL) {
        state->glyph_cache = calloc(1U, sizeof *state->glyph_cache);
    }
    state->theme = theme != NULL ? theme : inkcell_theme_default();
    state->scale = inkcell_theme_clamp_scale(state->theme, scale);
    /* Every position remembered in there is in pixels, measured against metrics this call has
       just replaced. Keeping them would slide a knob from where it sat under the old scale. */
    inkcell_anim_table_reset(&state->anim);
    /* And the frame's own transition, for the same reason and one more: a theme switch is not a
       move between screens, so a screen that slid in because the palette changed would be
       animating an event that did not happen. */
    memset(&state->slide, 0, sizeof state->slide);
    state->slide_dir = 0;
}

void inkcell_fb_state_set_now(struct inkcell_backend_fb_state *state, uint64_t now_ms) {
    if (state != NULL && now_ms > state->now_ms) {
        state->now_ms = now_ms;
    }
}

bool inkcell_fb_state_animating(const struct inkcell_backend_fb_state *state) {
    if (state == NULL) {
        return false;
    }
    /* The transition is asked about separately from the table because it is kept separately -
       see `slide` on the state. A frame owes another one while either has somewhere to be.
       An app still filling owes one for a reason that is not an animation at all: the next
       piece is read on the next frame, so without this the fill would stop wherever the last
       press left it. */
    return inkcell_anim_active(&state->slide, state->now_ms) ||
           inkcell_anim_table_active(&state->anim, state->now_ms) || inkcell_fb_app_pending(state);
}

/*
 * How far a screen travels on its way in, as a fraction of the panel: a quarter of it.
 *
 * Not the whole width, and this is the one measurement in the transition that had to be looked
 * at rather than reasoned about. Only one screen is drawn (see inkcell_fb_transition_offset()), so
 * a screen that started a full panel out left the body *empty* on the frame the press landed - one
 * blank frame, every time, before anything arrived. A blink is a worse artefact than no animation
 * at all.
 *
 * A quarter is also what Material's shared-axis transition does, and for the same reason
 * arrived at from the other end: there the displacement is small because the cross-fade is what
 * carries the change of identity, and the slide only says which way. Here there is no fade to
 * carry it - so the slide says which way *and* the content under the cursor is legible for the
 * whole of the move, which is what a blank frame was spending.
 */
#define INKCELL_FB_TRANSITION_TRAVEL_NUM 1
#define INKCELL_FB_TRANSITION_TRAVEL_DEN 4

/*
 * The move this frame is part of, as the distance the arriving screen still has to travel.
 *
 * Positive is a screen coming in from the right - which is what going a level deeper looks like
 * on every handheld - negative one coming in from the left, and 0 a frame that is not moving,
 * which is very nearly all of them.
 *
 * Where the "was" comes from is the whole design, and it is not in the snapshot: inkcell/ui/route.h
 * reads the nav and says which *place* it is showing, this remembers the last one, and the
 * difference between two places is the direction. Nothing in the store or the nav records how it
 * got here, so no call site that opens a level has to remember to say so.
 *
 * Only one screen is ever rendered. There is no alpha on this panel and nothing can read back
 * what is already on it, so a cross-fade is out and so is carrying the outgoing screen along
 * beside the incoming one - which would want a second page of pixels held for the length of the
 * move. What is left is a displacement, and INKCELL_FB_TRANSITION_TRAVEL_NUM is how much of one.
 *
 * A theme that asks for no motion gets none: inkcell_anim_to() with a zero duration puts the
 * value on its target, which lands the screen in place on the frame it arrives.
 */
void inkcell_fb_transition_begin(struct inkcell_backend_fb_state *state,
                                 enum inkcell_transition move) {
    if (state == NULL || move == INKCELL_TRANSITION_NONE) {
        return;
    }
    state->slide_dir = (move == INKCELL_TRANSITION_FORWARD) ? 1 : -1;
    /* From the far end every time, including when a move interrupts one already running:
       a second press is a second screen arriving, not the first one changing its mind about
       where it was going. */
    inkcell_anim_set(&state->slide, 0);
    inkcell_anim_to(&state->slide, state->now_ms, INKCELL_ANIM_ONE,
                    /* MEDIUM rather than the SHORT the audit named. SHORT is what a control
                       acknowledging a press takes, and it is also what anything *leaving*
                       takes - and this is the one animation here with nothing leaving in it.
                       The token whose stated meaning is "something arriving that was not
                       there" is the one a screen arriving should be spending. */
                    inkcell_fb_motion(state, INKCELL_MOTION_MEDIUM), INKCELL_EASE_OUT);
}

int inkcell_fb_transition_offset(struct inkcell_backend_fb_state *state) {
    if (state == NULL || state->slide_dir == 0) {
        return 0;
    }
    const int32_t remaining = INKCELL_ANIM_ONE - inkcell_anim_value(&state->slide, state->now_ms);
    if (remaining <= 0) {
        state->slide_dir = 0;
        return 0;
    }
    const int travel =
        (int)state->var.xres * INKCELL_FB_TRANSITION_TRAVEL_NUM / INKCELL_FB_TRANSITION_TRAVEL_DEN;
    return state->slide_dir * (int)(((int64_t)remaining * travel) / INKCELL_ANIM_ONE);
}

void inkcell_fb_shift_begin(struct inkcell_backend_fb_state *state, int dx, int top, int bottom) {
    if (state == NULL || bottom <= top) {
        return;
    }
    state->shift_x = dx;
    state->shift_top = top;
    state->shift_bottom = bottom;
    state->shift_active = true;
}

void inkcell_fb_shift_end(struct inkcell_backend_fb_state *state) {
    if (state != NULL) {
        state->shift_active = false;
        state->shift_x = 0;
    }
}

bool inkcell_fb_state_set_theme_by_id(struct inkcell_backend_fb_state *state, const char *id) {
    if (state == NULL || id == NULL || id[0] == '\0') {
        /* Nothing named one - the capture harness has no app behind it - so keep drawing with
           whatever this state was opened with. */
        return false;
    }
    const struct inkcell_theme *theme = inkcell_theme_by_id(id);
    if (theme == NULL || theme == state->theme) {
        return false;
    }
    /* The new theme brings its own glyph scale unless the environment pinned one. */
    inkcell_fb_state_set_theme(state, theme, state->scale_pinned ? state->scale : 0);
    return true;
}

/* ---- the application behind the frame ---------------------------------------------------- */

void inkcell_fb_set_app(struct inkcell_backend_fb_state *state, const struct inkcell_fb_app *app) {
    if (state == NULL) {
        return;
    }
    if (state->app.close != NULL) {
        state->app.close(state, state->app.ctx);
    }
    if (app == NULL) {
        state->app = (struct inkcell_fb_app){0};
        return;
    }
    state->app = *app;
}

bool inkcell_fb_app_pending(const struct inkcell_backend_fb_state *state) {
    return state != NULL && state->app.pending != NULL && state->app.pending(state->app.ctx);
}

void inkcell_fb_app_frame_begin(struct inkcell_backend_fb_state *state) {
    if (state != NULL && state->app.frame_begin != NULL) {
        state->app.frame_begin(state->app.ctx);
    }
}

/* Asks the app to drop whatever it has memoised on this state - what a geometry or mode change
   invalidates. A no-op when nothing installed an app, or when it keeps no caches. */
void inkcell_fb_app_drop_caches(struct inkcell_backend_fb_state *state) {
    if (state != NULL && state->app.drop_caches != NULL) {
        state->app.drop_caches(state, state->app.ctx);
    }
}

void inkcell_fb_render(struct inkcell_backend_fb_state *state, const void *snapshot) {
    if (state == NULL || state->app.render == NULL) {
        return;
    }
    inkcell_fb_app_frame_begin(state);
    state->app.render(state, snapshot, state->app.ctx);
}

struct inkcell_rgb inkcell_fb_color(const struct inkcell_backend_fb_state *state,
                                    enum inkcell_color role) {
    return inkcell_theme_color(state != NULL ? state->theme : NULL, role);
}

struct inkcell_rgb inkcell_fb_tone_color(const struct inkcell_backend_fb_state *state,
                                         enum inkcell_tone tone) {
    return inkcell_theme_tone(state != NULL ? state->theme : NULL, tone);
}

struct inkcell_paint inkcell_fb_paint(const struct inkcell_backend_fb_state *state,
                                      enum inkcell_family family, enum inkcell_slot slot,
                                      enum inkcell_state ui_state) {
    return inkcell_theme_paint(state != NULL ? state->theme : NULL, family, slot, ui_state);
}

struct inkcell_rgb inkcell_fb_state_layer(const struct inkcell_backend_fb_state *state,
                                          enum inkcell_color fill, enum inkcell_color ink,
                                          enum inkcell_state ui_state) {
    return inkcell_theme_state_layer(inkcell_fb_color(state, fill), inkcell_fb_color(state, ink),
                                     ui_state);
}

const struct inkcell_metrics *inkcell_fb_metrics(const struct inkcell_backend_fb_state *state) {
    return inkcell_theme_metrics(state != NULL ? state->theme : NULL);
}

const struct inkcell_font *inkcell_fb_font(const struct inkcell_backend_fb_state *state) {
    return inkcell_theme_font(state != NULL ? state->theme : NULL);
}

int inkcell_fb_radius(const struct inkcell_backend_fb_state *state, enum inkcell_shape shape) {
    return inkcell_theme_radius(state->theme, shape, state->scale);
}

int inkcell_fb_space(const struct inkcell_backend_fb_state *state, enum inkcell_space space) {
    return inkcell_theme_space(state->theme, space, state->scale);
}

int inkcell_fb_space_at(const struct inkcell_backend_fb_state *state, enum inkcell_space space,
                        int scale) {
    return inkcell_theme_space(state->theme, space, scale);
}

int inkcell_fb_type_scale(const struct inkcell_backend_fb_state *state, enum inkcell_type type) {
    return inkcell_theme_type_scale(state->theme, type, state->scale);
}

enum inkcell_weight inkcell_fb_type_weight(const struct inkcell_backend_fb_state *state,
                                           enum inkcell_type type) {
    return inkcell_theme_type_weight(state->theme, type);
}

int inkcell_fb_gutter(const struct inkcell_backend_fb_state *state) {
    const int margin = inkcell_fb_margin(state);
    return margin > 1 ? margin / 2 : margin;
}

uint32_t inkcell_fb_motion(const struct inkcell_backend_fb_state *state,
                           enum inkcell_motion motion) {
    return inkcell_theme_motion(state->theme, motion);
}

int inkcell_fb_edge(const struct inkcell_backend_fb_state *state) {
    const int edge = inkcell_fb_space(state, INKCELL_SPACE_XS);
    return edge > 0 ? edge : 1;
}

int inkcell_fb_margin(const struct inkcell_backend_fb_state *state) {
    return (int)inkcell_fb_metrics(state)->margin;
}

int inkcell_fb_rail_gutter(const struct inkcell_backend_fb_state *state) {
    return inkcell_fb_gutter(state);
}

struct inkcell_fb_row_box inkcell_fb_row_box(const struct inkcell_backend_fb_state *state) {
    const int gutter = inkcell_fb_gutter(state);
    /*
     * The row's own padding: how far its words sit inside its fill. Derived rather than stated,
     * because the leading edge is what every row has always been drawn at - the fill starts at
     * the gutter and the text at the margin - and the trailing edge owes the same, or a value
     * column against the fill's right edge would be padded on one side only.
     */
    const int pad = inkcell_fb_margin(state) - gutter;
    struct inkcell_fb_row_box box;
    box.x = gutter;
    box.w = (int)state->var.xres - 2 * gutter - inkcell_fb_rail_gutter(state);
    /* A panel too narrow to hold a padded row still has to hand back a box the fills and the
       measurements agree about: one pixel wide, with the text span collapsed onto it. Every
       caller that divides by a column width already guards its own division. */
    if (box.w < 1) {
        box.w = 1;
    }
    box.text_x = box.x + pad;
    box.text_right = box.x + box.w - pad;
    if (box.text_right < box.text_x) {
        box.text_x = box.x;
        box.text_right = box.x + box.w;
    }
    return box;
}

/* Scale an 8-bit channel into a framebuffer bitfield and shift it into place. */
static inline uint32_t inkcell_fb_pack_channel(uint8_t value, const struct fb_bitfield *field) {
    if (field->length == 0U) {
        return 0U;
    }
    uint32_t scaled = field->length >= 8U ? (uint32_t)value << (field->length - 8U)
                                          : (uint32_t)value >> (8U - field->length);
    return scaled << field->offset;
}

/*
 * The Brick's display engine composites fb0 with per-pixel alpha (the layer dump in
 * /sys/class/disp/disp/attr/sys says `a[pixel 255]`), so a 32-bit pixel with a zero top byte is
 * fully transparent and shows the black background no matter what RGB it carries. That was the
 * black screen. Alpha is therefore always written as opaque, whether or not the driver reports a
 * transp bitfield: for 32 bpp every bit outside the colour channels is set.
 */
static inline uint32_t compose_color(const struct inkcell_backend_fb_state *state, uint8_t r,
                                     uint8_t g, uint8_t b) {
    const struct fb_var_screeninfo *var = &state->var;
    const bool has_fields =
        var->red.length != 0U || var->green.length != 0U || var->blue.length != 0U;

    uint32_t color;
    uint32_t color_mask;
    if (has_fields) {
        color = inkcell_fb_pack_channel(r, &var->red) | inkcell_fb_pack_channel(g, &var->green) |
                inkcell_fb_pack_channel(b, &var->blue);
        color_mask = inkcell_fb_pack_channel(0xFFU, &var->red) |
                     inkcell_fb_pack_channel(0xFFU, &var->green) |
                     inkcell_fb_pack_channel(0xFFU, &var->blue);
    } else {
        switch (var->bits_per_pixel) {
        case 32:
        case 24:
            color = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            color_mask = 0x00FFFFFFU;
            break;
        case 16:
            color = (uint32_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            color_mask = 0xFFFFU;
            break;
        default:
            return 0U;
        }
    }

    if (var->transp.length != 0U) {
        color |= inkcell_fb_pack_channel(0xFFU, &var->transp);
    } else if (var->bits_per_pixel == 32U) {
        color |= ~color_mask; /* opaque in whatever byte the colour channels leave free */
    }
    return color;
}

/*
 * Where a rectangle really lands: the frame's transform, the panel's edges and whatever clip is
 * in force, applied once - and how many pixels came off its leading edges on the way.
 *
 * Two callers, and the second is why `dx` and `dy` are answered at all. A fill is one colour, so
 * it does not care which part of itself survived; a blit is a picture, and the part of the box
 * that was trimmed is exactly the part of the source that must be skipped. Working it out here
 * rather than in the blit is what keeps one copy of the rules - a second copy is how a clip
 * comes to be honoured by the fills and not by the pictures.
 */
struct inkcell_fb_clipped_box {
    int x, y, w, h;
    int dx, dy;
};

static bool inkcell_fb_clip_box(const struct inkcell_backend_fb_state *state, int x, int y, int w,
                                int h, struct inkcell_fb_clipped_box *out) {
    int dx = 0;
    int dy = 0;
    /*
     * The frame's transform, before anything is measured against the panel: a screen arriving
     * from off the right-hand edge is drawn at coordinates that are not on the panel at all,
     * and the clamp below is what turns that into the part of it that has arrived. The band is
     * applied here rather than left to the caller for the same reason - a row whose glyphs
     * overhang the top of the body must be cut off at the body, not drawn over the navigation
     * bar it is sliding underneath. See inkcell_fb_shift_begin().
     */
    if (state->shift_active) {
        x += state->shift_x;
        if (y < state->shift_top) {
            const int trimmed = state->shift_top - y;
            h -= trimmed;
            dy += trimmed;
            y = state->shift_top;
        }
        if (y + h > state->shift_bottom) {
            h = state->shift_bottom - y;
        }
        if (h <= 0) {
            return false;
        }
    }
    if (x < 0) {
        w += x;
        dx -= x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        dy -= y;
        y = 0;
    }
    if (x + w > (int)state->var.xres) {
        w = (int)state->var.xres - x;
    }
    if (y + h > (int)state->var.yres) {
        h = (int)state->var.yres - y;
    }
    if (w <= 0 || h <= 0) {
        return false;
    }

    if (state->clip_active) {
        const int right = x + w < state->clip.right ? x + w : state->clip.right;
        const int bottom = y + h < state->clip.bottom ? y + h : state->clip.bottom;
        if (x < state->clip.x) {
            dx += state->clip.x - x;
            x = state->clip.x;
        }
        if (y < state->clip.y) {
            dy += state->clip.y - y;
            y = state->clip.y;
        }
        w = right - x;
        h = bottom - y;
        if (w <= 0 || h <= 0) {
            return false;
        }
    }

    out->x = x;
    out->y = y;
    out->w = w;
    out->h = h;
    out->dx = dx;
    out->dy = dy;
    return true;
}

/* One row of a span, in the mapping's own pixel format. The format is fixed for the life of the
   mapping, so the switch belongs out here rather than inside the column loop it used to sit
   in.

   The stores go through memcpy rather than a cast to uint32_t*: the compiler emits the same
   single instruction, but a row pointer is only as aligned as fix.line_length makes it, and
   casting one to a wider type is undefined where the hardware is strict about it. */
static inline void inkcell_fb_store_span(uint8_t *row, int w, uint32_t packed, size_t bpp) {
    switch (bpp) {
    case 4: {
        uint8_t *px = row;
        for (int col = 0; col < w; ++col, px += 4) {
            memcpy(px, &packed, 4U);
        }
        break;
    }
    case 3: {
        uint8_t *px = row;
        for (int col = 0; col < w; ++col) {
            px[0] = (uint8_t)(packed & 0xFFU);
            px[1] = (uint8_t)((packed >> 8) & 0xFFU);
            px[2] = (uint8_t)((packed >> 16) & 0xFFU);
            px += 3;
        }
        break;
    }
    case 2: {
        const uint16_t narrow = (uint16_t)packed;
        uint8_t *px = row;
        for (int col = 0; col < w; ++col, px += 2) {
            memcpy(px, &narrow, 2U);
        }
        break;
    }
    default:
        memset(row, (int)(packed & 0xFFU), (size_t)w);
        break;
    }
}

/*
 * Fill a clipped, axis-aligned span with an already-packed pixel value.
 *
 * This is the one place that touches the mapping. Everything above it packs its colour once
 * and then describes rectangles, because compose_color() is far too much arithmetic to run per
 * pixel: a full screen of text is ~200k scaled sub-pixels, and packing each one separately was
 * about a third of the frame.
 */
static void inkcell_fb_fill_packed(const struct inkcell_backend_fb_state *state, int x, int y,
                                   int w, int h, uint32_t packed) {
    if (w <= 0 || h <= 0) {
        return;
    }
    struct inkcell_fb_clipped_box box;
    if (!inkcell_fb_clip_box(state, x, y, w, h, &box)) {
        return;
    }

    const size_t bpp = state->bytes_per_pixel;
    const size_t stride = state->fix.line_length;
    uint8_t *row = state->inkcell_fb_ptr + (size_t)box.y * stride + (size_t)box.x * bpp;

    for (int r = 0; r < box.h; ++r, row += stride) {
        if ((size_t)(row - state->inkcell_fb_ptr) + (size_t)box.w * bpp > state->inkcell_fb_size) {
            return;
        }
        inkcell_fb_store_span(row, box.w, packed, bpp);
    }
}

/*
 * Whether this mapping holds exactly the word a decoded tile is already made of.
 *
 * The decoder hands back BGRA (mesh/map/tile_image.h), which read four bytes at a time on a
 * little-endian machine is 0xAARRGGBB - and that is what compose_color() answers for a 32 bpp
 * panel whose channels sit where every 32 bpp panel puts them. Where the two agree a tile's
 * pixels are the panel's pixels and the blit is a copy with the alpha forced opaque; where they
 * do not - a 16 bpp panel, or channels in an order nobody expects - every pixel has to be
 * packed, and the blit takes the slow path.
 *
 * Asked rather than assumed, and asked with two probes rather than one: a single value can
 * agree by accident with a format that has a channel in the wrong place, and the Brick's own
 * fb0 is read out of the kernel at runtime like anybody else's.
 */
static bool inkcell_fb_blit_is_direct(const struct inkcell_backend_fb_state *state) {
    return state->bytes_per_pixel == 4U &&
           compose_color(state, 0x12U, 0x34U, 0x56U) == 0xFF123456U &&
           compose_color(state, 0xA0U, 0xB0U, 0xC0U) == 0xFFA0B0C0U;
}

/*
 * Draw a rectangle of BGRA pixels - a decoded image: a map tile, a sprite, a photo.
 *
 * It goes through inkcell_fb_clip_box() rather than clamping for itself, which is what makes the
 * map's own clip cover the picture as well as the ink: every fill, glyph and icon in this backend
 * is clipped by that one function, and a blit that did its own arithmetic would be the one thing on
 * the frame that could paint over the app bar. The part of the box that was trimmed is the part of
 * the source that is skipped, which is what `dx` and `dy` come back for.
 *
 * `stride` is the source's own row length in bytes, so a caller can hand over a sub-rectangle
 * of a larger image without copying it out first.
 *
 * The slow path coalesces runs of one colour before packing it, for the reason
 * inkcell_fb_fill_packed() exists: compose_color() per pixel is about a third of a frame, and a map
 * tile quantised to a palette - which is what a tile pack produces - is long runs of one colour
 * with detail drawn through it. A photograph degenerates to a pack per pixel, so a caller with one
 * of those wants a direct-blit surface instead.
 */
void inkcell_fb_blit_bgra(const struct inkcell_backend_fb_state *state, int x, int y, int w, int h,
                          const uint8_t *pixels, size_t stride) {
    if (state == NULL || pixels == NULL || w <= 0 || h <= 0) {
        return;
    }
    struct inkcell_fb_clipped_box box;
    if (!inkcell_fb_clip_box(state, x, y, w, h, &box)) {
        return;
    }

    const size_t bpp = state->bytes_per_pixel;
    const size_t dst_stride = state->fix.line_length;
    const bool direct = inkcell_fb_blit_is_direct(state);
    uint8_t *dst = state->inkcell_fb_ptr + (size_t)box.y * dst_stride + (size_t)box.x * bpp;
    const uint8_t *src = pixels + (size_t)box.dy * stride + (size_t)box.dx * INKCELL_FB_BGRA_BYTES;

    for (int row = 0; row < box.h; ++row, dst += dst_stride, src += stride) {
        if ((size_t)(dst - state->inkcell_fb_ptr) + (size_t)box.w * bpp > state->inkcell_fb_size) {
            return;
        }
        if (direct) {
            /* The alpha is forced rather than copied: the Brick's display engine composites fb0
               per pixel, so a tile carrying anything but 255 there would be a hole in the map.
               Every other pixel this backend writes is opaque for the same reason. */
            uint8_t *out = dst;
            const uint8_t *in = src;
            for (int col = 0; col < box.w; ++col, out += 4, in += 4) {
                uint32_t word;
                memcpy(&word, in, 4U);
                word |= 0xFF000000U;
                memcpy(out, &word, 4U);
            }
            continue;
        }
        int col = 0;
        while (col < box.w) {
            uint32_t word;
            memcpy(&word, src + (size_t)col * INKCELL_FB_BGRA_BYTES, 4U);
            int end = col + 1;
            while (end < box.w) {
                uint32_t next;
                memcpy(&next, src + (size_t)end * INKCELL_FB_BGRA_BYTES, 4U);
                if (next != word) {
                    break;
                }
                ++end;
            }
            const uint32_t packed =
                compose_color(state, (uint8_t)((word >> 16) & 0xFFU),
                              (uint8_t)((word >> 8) & 0xFFU), (uint8_t)(word & 0xFFU));
            inkcell_fb_store_span(dst + (size_t)col * bpp, end - col, packed, bpp);
            col = end;
        }
    }
}

/*
 * The coverage ramp a tinted sprite - a glyph or an icon - is drawn in.
 *
 * 32 steps is below what the eye separates at this size, and packing a colour per pixel was
 * the thing inkcell_fb_fill_packed() exists to avoid: quantising first makes the blend one multiply
 * per channel per *step* rather than per pixel, and lets equal steps coalesce into spans.
 * Text and icons share it because they are the same operation - coverage, tinted with the
 * ink, over a ground the caller has just filled.
 */
#define INKCELL_FB_BLEND_STEPS 32

static void inkcell_fb_blend_table(const struct inkcell_backend_fb_state *state,
                                   struct inkcell_rgb ink, struct inkcell_rgb ground,
                                   uint32_t out[INKCELL_FB_BLEND_STEPS]) {
    for (int step = 0; step < INKCELL_FB_BLEND_STEPS; ++step) {
        const int32_t a = (int32_t)step * 255 / (INKCELL_FB_BLEND_STEPS - 1);
        const uint8_t r = (uint8_t)(((int32_t)ink.r * a + (int32_t)ground.r * (255 - a)) / 255);
        const uint8_t g = (uint8_t)(((int32_t)ink.g * a + (int32_t)ground.g * (255 - a)) / 255);
        const uint8_t b = (uint8_t)(((int32_t)ink.b * a + (int32_t)ground.b * (255 - a)) / 255);
        out[step] = compose_color(state, r, g, b);
    }
}

/* Glyph metrics for a given multiplier. The gaps are the font's, not this file's: a taller
   font with a different line gap changes every measurement above without touching one. */
int inkcell_fb_char_adv(const struct inkcell_backend_fb_state *state, int scale) {
    return inkcell_font_advance(inkcell_fb_font(state), scale);
}

int inkcell_fb_cell_adv(const struct inkcell_backend_fb_state *state, uint32_t codepoint,
                        int scale) {
    return inkcell_font_advance_cp(inkcell_fb_font(state), codepoint, scale);
}

/*
 * How wide `text` draws, in pixels.
 *
 * The counterpart to inkcell_text_cells(), and the one to reach for when the answer becomes a
 * coordinate: a cell count times the nominal advance was the same number while every face was
 * monospace, and is an estimate now. Anything that centres a label, right-aligns a value or
 * sizes a box around a string has to measure it, because being wrong by the difference between
 * 'W' and 'i' is being wrong by half the string.
 *
 * It walks the same cells inkcell_fb_draw_text() walks and adds the same advances, so a line is
 * exactly as wide as it draws - including an emoji, which steps its own square box rather than
 * the face's advance, and a newline, which starts the width over.
 */
int inkcell_fb_text_width(const struct inkcell_backend_fb_state *state, const char *text,
                          int scale) {
    return inkcell_fb_text_width_weight(state, text, scale, INKCELL_WEIGHT_REGULAR);
}

int inkcell_fb_text_width_weight(const struct inkcell_backend_fb_state *state, const char *text,
                                 int scale, enum inkcell_weight weight) {
    if (text == NULL) {
        return 0;
    }
    const struct inkcell_font *font = inkcell_font_at_weight(inkcell_fb_font(state), weight);
    int width = 0;
    int widest = 0;
    size_t offset = 0;
    for (;;) {
        const struct inkcell_text_cell cell = inkcell_text_cell_next(&text[offset]);
        if (cell.bytes == 0U) {
            break;
        }
        offset += cell.bytes;
        if (cell.codepoint == (uint32_t)'\n' && !cell.is_emoji) {
            width = 0;
            continue;
        }
        width += cell.is_emoji ? inkcell_font_advance(font, scale)
                               : inkcell_font_advance_cp(font, cell.codepoint, scale);
        if (width > widest) {
            widest = width;
        }
    }
    return widest;
}
/*
 * How many nominal cells `text` needs - its measured width, rounded up to the grid.
 *
 * The bridge for the places that still reserve space in columns: a trailing slot negotiating
 * against the row's width, a bubble sizing itself against the panel. Those are grids, and a
 * grid can keep working on a proportional face as long as what it reserves covers what will be
 * drawn - so the width is measured properly and then rounded *up* to whole cells, rather than
 * the string being counted and hoped for.
 *
 * Rounding up rather than to nearest, deliberately: reserving a cell too many costs a column
 * of blank, and reserving one too few clips a word.
 */
size_t inkcell_fb_text_cols(const struct inkcell_backend_fb_state *state, const char *text,
                            int scale) {
    const int adv = inkcell_fb_char_adv(state, scale);
    if (text == NULL || adv <= 0) {
        return 0U;
    }
    const int width = inkcell_fb_text_width(state, text, scale);
    return width > 0 ? (size_t)((width + adv - 1) / adv) : 0U;
}

/*
 * The wrap metric for this state's font: one cell's advance, in pixels.
 *
 * What it is for is stated on struct inkcell_wrap_metric. What it costs is worth saying here:
 * an emoji steps its own square box and a glyph steps the face's advance, which is exactly the
 * pair inkcell_fb_draw_text() steps - so a wrap budget in pixels cuts the line where the drawn
 * line would actually stop, and the measure pass and the draw pass still agree.
 */
static int inkcell_fb_wrap_cell(const struct inkcell_text_cell *cell, void *ctx) {
    const struct inkcell_fb_wrap_ctx *wrap = (const struct inkcell_fb_wrap_ctx *)ctx;
    if (cell == NULL || wrap == NULL) {
        return 0;
    }
    return cell->is_emoji ? inkcell_fb_char_adv(wrap->state, wrap->scale)
                          : inkcell_fb_cell_adv(wrap->state, cell->codepoint, wrap->scale);
}

struct inkcell_wrap_metric inkcell_fb_wrap_metric(struct inkcell_fb_wrap_ctx *ctx,
                                                  const struct inkcell_backend_fb_state *state,
                                                  int scale) {
    struct inkcell_wrap_metric metric = {NULL, NULL};
    if (ctx == NULL) {
        return metric;
    }
    ctx->state = state;
    ctx->scale = scale;
    metric.cell = inkcell_fb_wrap_cell;
    metric.ctx = ctx;
    return metric;
}

int inkcell_fb_line_adv(const struct inkcell_backend_fb_state *state, int scale) {
    return inkcell_font_line(inkcell_fb_font(state), scale);
}

/* The widest cell inkcell_fb_draw_glyph() will resample into: the largest cell a font may declare,
   at the largest scale the type scale can clamp to. */
/*
 * Sized off the *master* rather than the cell, because that is what the box is now measured
 * from - a proportional face stores every glyph in a master as wide as its widest advance, and
 * that master is wider than the nominal cell by design.
 *
 * The divisor is the master resolution the rasterised faces store at: four units to the pixel.
 * A face that stored its master at the cell (5x7, at one unit to the pixel) has a master only
 * as wide as `INKCELL_GLYPH_MAX_WIDTH`, so it stays well inside this. inkcell_fb_draw_glyph_ramp()
 * bails on a box that exceeds it rather than overrunning the scratch row.
 */
#define INKCELL_FB_GLYPH_BOX_MAX (INKCELL_GLYPH_MASTER_MAX_WIDTH * INKCELL_SCALE_MAX / 2)

/*
 * One axis of the resample: the two master samples a destination pixel sits between, and how
 * far between them it is.
 *
 * A pixel-art font sets `lo == hi` and `frac == 0`, which collapses the interpolation below
 * into a plain lookup - so one formula serves both samplings with no per-pixel branch, and
 * 5x7 comes out of it as the same hard-edged blocks it has always drawn.
 */
struct inkcell_fb_glyph_tap {
    int16_t lo;
    int16_t hi;
    int16_t frac; /* 0..255, the position between `lo` and `hi` */
};

static struct inkcell_fb_glyph_tap inkcell_fb_glyph_tap(int index, int box, int master,
                                                        enum inkcell_font_sampling sampling) {
    struct inkcell_fb_glyph_tap tap = {0, 0, 0};
    if (master <= 0 || box <= 0) {
        return tap;
    }
    /* Half-pixel offsets at both ends: sampling from the pixel's centre is what keeps a
       symmetric glyph symmetric after the scale. */
    int32_t pos = (((int32_t)index * 2 + 1) * master * 128) / box - 128;
    if (pos < 0) {
        pos = 0;
    }
    if (sampling == INKCELL_FONT_PIXEL) {
        /* Nearest, which on the integer ratio a pixel font is drawn at is exactly the source
           block - rounding rather than truncating is what keeps the block boundaries where
           they were. */
        int32_t at = (pos + 128) >> 8;
        if (at > master - 1) {
            at = master - 1;
        }
        tap.lo = (int16_t)at;
        tap.hi = (int16_t)at;
        return tap;
    }
    int32_t lo = pos >> 8;
    if (lo > master - 1) {
        lo = master - 1;
    }
    tap.lo = (int16_t)lo;
    tap.hi = (int16_t)(lo + 1 < master ? lo + 1 : lo);
    tap.frac = (int16_t)(pos & 0xFF);
    return tap;
}

/* Coverage at one destination pixel, quantised into the blend table's index. The rounding
   lives here rather than at the call site for the reason inkcell_fb_icon_step()'s does: measuring a
   span and drawing it must round identically or the span boundaries move. */
static int inkcell_fb_glyph_step(const uint8_t *row_lo, const uint8_t *row_hi,
                                 const struct inkcell_fb_glyph_tap *tap, int32_t fy) {
    const int32_t fx = tap->frac;
    const int32_t upper = row_lo[tap->lo] * (256 - fx) + row_lo[tap->hi] * fx;
    const int32_t lower = row_hi[tap->lo] * (256 - fx) + row_hi[tap->hi] * fx;
    const int32_t alpha = upper * (256 - fy) + lower * fy; /* 0 .. MAX_ALPHA << 16 */
    return (int)((alpha * (INKCELL_FB_BLEND_STEPS - 1)) / (INKCELL_GLYPH_MAX_ALPHA * 65536));
}

/*
 * Draw one glyph's coverage into its cell, in a ramp the caller has already built.
 *
 * The master is resampled into `width * scale` by `height * scale` and emitted as spans of
 * equal coverage, the way inkcell_fb_draw_icon() emits a symbol. Taking the ramp rather than a
 * colour pair is what keeps a line of text to one table build instead of one per character.
 *
 * The master's overhang rows are drawn too, above `y` - they are the same resample continued
 * upward, not a second pass, which is why the whole glyph is one box placed by its baseline
 * rather than a cell plus an accent stuck on top of it.
 */
static void inkcell_fb_draw_glyph_ramp(const struct inkcell_backend_fb_state *state, int x, int y,
                                       uint32_t codepoint, int scale,
                                       const struct inkcell_font *font,
                                       const uint32_t blend[INKCELL_FB_BLEND_STEPS]) {
    const int cell_rows = (int)font->master_h - (int)font->master_top;
    /*
     * The box the master resamples into, taken from the master rather than from the cell.
     *
     * For a monospace face the two are the same thing and this is the `width * scale` it has
     * always been. For a proportional one they are not: every glyph is stored in the same
     * master box - as wide as the widest advance the face has - and positioned inside it by its
     * own sidebearing, so what varies between characters is where the pen goes next, not how
     * big the box is. Keeping the box uniform is what lets the glyph cache stay a fixed-stride
     * array, and drawing it at the pen is safe because a zero-coverage span is skipped below:
     * two neighbouring masters overlap and only their ink is painted.
     */
    const int master_px = font->master_scale > 0U ? (int)font->master_scale : 1;
    const int box_w = (int)font->master_w * scale / master_px;
    const int box_h = (int)font->height * scale;
    if (scale <= 0 || box_w <= 0 || box_h <= 0 || box_w > INKCELL_FB_GLYPH_BOX_MAX ||
        font->master_w == 0U || cell_rows <= 0) {
        return;
    }
    /* The cell is `cell_rows` of the master, so the whole of it - overhang included - is that
       much taller, and starts that much higher. Both from the one division, so the overhang
       lands exactly on the row the cell starts at. */
    const int full_h = box_h * (int)font->master_h / cell_rows;
    const int top_off = box_h * (int)font->master_top / cell_rows;
    if (full_h <= 0) {
        return;
    }

    struct inkcell_fb_cached_glyph *cached = NULL;
    bool hit = false;
    if (state->glyph_cache != NULL &&
        (size_t)box_w * (size_t)full_h <= INKCELL_FB_GLYPH_CACHE_PIXELS) {
        struct inkcell_fb_glyph_cache *cache = state->glyph_cache;
        const size_t set = (codepoint * 31U + (uint32_t)scale * 17U) % INKCELL_FB_GLYPH_CACHE_SETS;
        cached = &cache->entries[set][0];
        for (size_t i = 0; i < INKCELL_FB_GLYPH_CACHE_WAYS; ++i) {
            struct inkcell_fb_cached_glyph *entry = &cache->entries[set][i];
            if (entry->font == font && entry->codepoint == codepoint && entry->scale == scale) {
                cached = entry;
                hit = true;
                break;
            }
            if (entry->used < cached->used) {
                cached = entry;
            }
        }
        cached->used = ++cache->clock;
    }

    struct inkcell_glyph glyph;
    struct inkcell_fb_glyph_tap taps[INKCELL_FB_GLYPH_BOX_MAX];
    if (!hit) {
        (void)inkcell_font_glyph(font, codepoint, &glyph);
        for (int dx = 0; dx < box_w; ++dx) {
            taps[dx] = inkcell_fb_glyph_tap(dx, box_w, (int)font->master_w, font->sampling);
        }
        if (cached != NULL) {
            cached->font = font;
            cached->codepoint = codepoint;
            cached->scale = scale;
        }
    }

    /* The pen sits `master_left` columns into the master, so the box is drawn that much before
       it - the horizontal mirror of the overhang above. */
    const int left = x - (int)font->master_left * scale / master_px;
    const int top = y - top_off;
    for (int dy = 0; dy < full_h; ++dy) {
        uint8_t scratch[INKCELL_FB_GLYPH_BOX_MAX];
        uint8_t *steps = cached != NULL ? &cached->steps[(size_t)dy * (size_t)box_w] : scratch;
        if (!hit) {
            const struct inkcell_fb_glyph_tap row =
                inkcell_fb_glyph_tap(dy, full_h, (int)font->master_h, font->sampling);
            const uint8_t *row_lo = &glyph.alpha[(size_t)row.lo * font->master_w];
            const uint8_t *row_hi = &glyph.alpha[(size_t)row.hi * font->master_w];
            for (int dx = 0; dx < box_w; ++dx) {
                steps[dx] = (uint8_t)inkcell_fb_glyph_step(row_lo, row_hi, &taps[dx], row.frac);
            }
        }
        int dx = 0;
        while (dx < box_w) {
            const int step = steps[dx];
            int end = dx + 1;
            while (end < box_w && steps[end] == step) {
                ++end;
            }
            if (step > 0) {
                inkcell_fb_fill_packed(state, left + dx, top + dy, end - dx, 1, blend[step]);
            }
            dx = end;
        }
    }
}

void inkcell_fb_draw_glyph(const struct inkcell_backend_fb_state *state, int x, int y,
                           uint32_t codepoint, int scale, struct inkcell_rgb ink,
                           struct inkcell_rgb ground) {
    uint32_t blend[INKCELL_FB_BLEND_STEPS];
    inkcell_fb_blend_table(state, ink, ground, blend);
    inkcell_fb_draw_glyph_ramp(state, x, y, codepoint, scale, inkcell_fb_font(state), blend);
}

/*
 * The emoji palette, packed into framebuffer pixels once.
 *
 * compose_color() depends only on the mapping's pixel format, which never changes while fb0 is
 * open, so the 255 palette entries are packed on first use and reused. The signature guards the
 * case of a second open with a different format - the tests do exactly that.
 */
static uint64_t inkcell_fb_format_signature(const struct inkcell_backend_fb_state *state) {
    const struct fb_var_screeninfo *var = &state->var;
    uint64_t sig = var->bits_per_pixel;
    const struct fb_bitfield *fields[4] = {&var->red, &var->green, &var->blue, &var->transp};
    for (size_t i = 0; i < 4U; ++i) {
        sig = sig * 131U + fields[i]->offset;
        sig = sig * 131U + fields[i]->length;
    }
    return sig;
}

#define INKCELL_FB_EMOJI_PALETTE_SLOTS 256

static const uint32_t *inkcell_fb_emoji_palette(const struct inkcell_backend_fb_state *state,
                                                const bool **opaque_out) {
    static uint32_t packed[INKCELL_FB_EMOJI_PALETTE_SLOTS];
    static bool opaque[INKCELL_FB_EMOJI_PALETTE_SLOTS];
    static uint64_t signature;
    static bool valid;

    const uint64_t sig = inkcell_fb_format_signature(state);
    if (!valid || sig != signature) {
        for (size_t i = 0; i < INKCELL_FB_EMOJI_PALETTE_SLOTS; ++i) {
            uint8_t rgb[3];
            opaque[i] = inkcell_emoji_color((uint8_t)i, rgb);
            packed[i] = opaque[i] ? compose_color(state, rgb[0], rgb[1], rgb[2]) : 0U;
        }
        signature = sig;
        valid = true;
    }
    *opaque_out = opaque;
    return packed;
}

/* Decoding a sprite is a run-length expansion into 256 bytes. A row of identical reactions or a
   repeated node emoji redraws the same sprite many times per frame, so keep the last one. */
static const uint8_t *inkcell_fb_emoji_pixels(uint16_t sprite) {
    static uint8_t pixels[INKCELL_EMOJI_SIZE * INKCELL_EMOJI_SIZE];
    static uint16_t cached_sprite;
    static bool valid;

    if (!valid || cached_sprite != sprite) {
        inkcell_emoji_decode(sprite, pixels);
        cached_sprite = sprite;
        valid = true;
    }
    return pixels;
}

/*
 * The box to actually draw a sprite in, given the room for one.
 *
 * Nearest neighbour divides evenly or it does not: at 81 px a 16 px sprite lands as a mix of
 * five- and six-pixel blocks, and on a face that is one eye a pixel wider than the other. At 80
 * every source pixel is the same 5x5 square, which reads as pixel art rather than as a
 * distortion - so a sprite with room to spare is drawn at the whole multiple that fits and the
 * leftover goes to the margin, where nothing is drawn anyway.
 *
 * Only once there is a multiple worth snapping to. Inside a line of text the box is a text cell
 * - 20 px at the body scale - and rounding that down to 16 would spend a fifth of the glyph to
 * tidy up an unevenness nothing can see at that size.
 */
int inkcell_fb_emoji_box_fit(int box) {
    if (box < 2 * INKCELL_EMOJI_SIZE) {
        return box;
    }
    return box - box % INKCELL_EMOJI_SIZE;
}

/*
 * Draw one emoji sprite into a square `box` pixels on a side, top-left at (x, y).
 *
 * Sampling is nearest-neighbour from the stored 16x16, and anything smoother would need to
 * blend against a background this function cannot see (rows under the cursor are filled a
 * different colour). In a text cell that is a small upscale of pixel art - 15 px at the tab
 * scale, 20 px at the body scale - and the staircase is invisible on fills this flat.
 *
 * Emoji carry no ink colour and take none. Their own is the point of having them: the red of a
 * flag and the yellow of a lightning bolt are most of what makes one recognisable at 20 px.
 */
void inkcell_fb_draw_emoji_box(const struct inkcell_backend_fb_state *state, int x, int top,
                               int box, uint16_t sprite) {
    if (box <= 0) {
        return;
    }

    const uint8_t *pixels = inkcell_fb_emoji_pixels(sprite);
    const bool *opaque = NULL;
    const uint32_t *palette = inkcell_fb_emoji_palette(state, &opaque);

    /*
     * A whole multiple - what inkcell_fb_emoji_box_fit() hands back, so every keycap - is walked in
     * *source* pixels rather than destination ones: each one is a square block, so a run of
     * equal neighbours is one rectangle however large the block is.
     *
     * Same pixels as the general path below, which is why this is a fast path rather than a
     * second renderer: at a keycap's size that one would compare a palette index per
     * destination pixel, and a page of forty sprites at five times each is a quarter of a
     * million of them for a grid that redraws on every press.
     *
     * It is also where a box too wide for the general path's column map lands, snapped to the
     * whole multiple inside it and centred in what was asked for. The alternative was the bug
     * this replaced: the bound sat above this path although this path has no use for the map,
     * so on a panel with room for a key over INKCELL_FB_EMOJI_BOX_MAX across - which the capture
     * tool will render - every emoji keycap drew nothing at all and the selected one drew a bare
     * fill. A cap costing a shift of at most fifteen pixels at that size is one nobody sees.
     */
    const int drawn = box > INKCELL_FB_EMOJI_BOX_MAX ? inkcell_fb_emoji_box_fit(box) : box;
    if (drawn % INKCELL_EMOJI_SIZE == 0) {
        const int block = drawn / INKCELL_EMOJI_SIZE;
        const int inset = (box - drawn) / 2;
        for (int sy = 0; sy < INKCELL_EMOJI_SIZE; ++sy) {
            const uint8_t *src_row = &pixels[sy * INKCELL_EMOJI_SIZE];
            int sx = 0;
            while (sx < INKCELL_EMOJI_SIZE) {
                const uint8_t index = src_row[sx];
                int end = sx + 1;
                while (end < INKCELL_EMOJI_SIZE && src_row[end] == index) {
                    ++end;
                }
                if (opaque[index]) {
                    inkcell_fb_fill_packed(state, x + inset + sx * block, top + inset + sy * block,
                                           (end - sx) * block, block, palette[index]);
                }
                sx = end;
            }
        }
        return;
    }

    /* Nearest-neighbour source column per destination column. Identical for every row, so the
       division runs once per column instead of once per pixel. Anything wider than the map has
       already been drawn above, so this is only ever a text cell's worth. */
    int sx_map[INKCELL_FB_EMOJI_BOX_MAX];
    if (box > (int)(sizeof sx_map / sizeof sx_map[0])) {
        return;
    }
    for (int dx = 0; dx < box; ++dx) {
        sx_map[dx] = dx * INKCELL_EMOJI_SIZE / box;
    }

    /* Emoji are mostly flat fills, so coalescing equal-index neighbours into one span turns
       most rows into a handful of writes. */
    for (int dy = 0; dy < box; ++dy) {
        const uint8_t *src_row = &pixels[(dy * INKCELL_EMOJI_SIZE / box) * INKCELL_EMOJI_SIZE];
        int dx = 0;
        while (dx < box) {
            const uint8_t index = src_row[sx_map[dx]];
            int end = dx + 1;
            while (end < box && src_row[sx_map[end]] == index) {
                ++end;
            }
            if (opaque[index]) {
                inkcell_fb_fill_packed(state, x + dx, top + dy, end - dx, 1, palette[index]);
            }
            dx = end;
        }
    }
}

/*
 * The same sprite as one cell of a line of text.
 *
 * The box is the full character advance rather than the glyph's five columns: at the advance
 * an emoji stands as tall as the capitals beside it, and the sprites carry their own
 * transparent margin, so neighbours still separate. Centred in the line's height puts it on
 * the same optical line as those capitals - one font row of padding above and below.
 */
static void inkcell_fb_draw_emoji(const struct inkcell_backend_fb_state *state, int x, int y,
                                  uint16_t sprite, int scale) {
    const int box = inkcell_fb_char_adv(state, scale);
    inkcell_fb_draw_emoji_box(state, x, y + ((int)inkcell_fb_font(state)->height * scale - box) / 2,
                              box, sprite);
}

/*
 * The room an icon takes in a line: a text cell, or the symbol itself when that is wider.
 *
 * It was the cell alone, which held while the cell was as wide as a capital is tall - a
 * monospace advance very nearly is. A proportional advance is not: it is the width of an
 * average lowercase letter, comfortably narrower than the cap height an icon is drawn to
 * match (see inkcell_fb_icon_drawn), and a box measured that way is a box the symbol hangs a
 * fifth of itself out of on each side. At a dialog's icon scale that overhang is twenty
 * pixels and lands outside the panel.
 *
 * So the box is whichever is larger. A row that reserves one still gets at least its cell, and
 * a symbol is never asked to fit in less than it is drawn at.
 */
int inkcell_fb_icon_box(const struct inkcell_backend_fb_state *state, int scale) {
    const int cell = inkcell_fb_char_adv(state, scale);
    const int drawn = inkcell_fb_icon_drawn(state, scale);
    return drawn > cell ? drawn : cell;
}

/*
 * What it is actually drawn at, which is a little wider than the cell it occupies.
 *
 * A symbol has to stand as tall as the capitals beside it to read as their equal, and the cell
 * advance is narrower than the glyph body is tall - so an icon drawn at the advance comes out
 * visibly smaller than the text it is labelling, which is the one thing a Material icon is
 * never allowed to be. It is drawn at the body's height instead and centred on its cell, so the
 * overhang is a couple of pixels into the gaps either side and the column arithmetic above is
 * untouched.
 *
 * The height being matched is the *symbol's*, not the sprite's: a sprite is a window a little
 * wider than Material's grid (INKCELL_ICON_WINDOW), and the shape lives in the central body of
 * it (INKCELL_ICON_BODY) with air around the outside. Scaling by the ratio is what puts the
 * shape on the capitals' height rather than the air - drawing the window at the body's height
 * instead would sit every symbol a fifth short of the text it labels.
 */
int inkcell_fb_icon_drawn(const struct inkcell_backend_fb_state *state, int scale) {
    const int body = inkcell_font_cap(inkcell_fb_font(state), scale);
    return (body * INKCELL_ICON_WINDOW + INKCELL_ICON_BODY / 2) / INKCELL_ICON_BODY;
}

/* The largest box inkcell_fb_draw_icon() can be asked for: the empty state's symbol at the largest
   glyph scale, plus the air its window carries around it. Rounded the way inkcell_fb_icon_drawn()
   rounds, not merely scaled the same way - a bound a pixel under what it is bounding fails the
   check below, and an icon that fails that check is not drawn at all. */
#define INKCELL_FB_ICON_DRAWN_MAX                                                                  \
    ((INKCELL_GLYPH_MAX_HEIGHT * INKCELL_FB_ICON_SCALE_MAX * INKCELL_ICON_WINDOW +                 \
      INKCELL_ICON_BODY / 2) /                                                                     \
     INKCELL_ICON_BODY)

/*
 * Coverage at one destination pixel, as a blend step.
 *
 * Bilinear between the four sprite pixels around it: `sample_x` is an 8.8 position along the
 * sprite's row, `fy` the fraction between the two rows the caller has already picked out. The
 * result is the index into the packed-colour table below, which is why the quantisation lives
 * here rather than at the call site - measuring a span and drawing it must round identically or
 * the span boundaries move.
 */
static int inkcell_fb_icon_step(const uint8_t *row0, const uint8_t *row1, int32_t sample_x,
                                int32_t fy) {
    const int32_t x0 = sample_x >> 8;
    const int32_t x1 = (x0 + 1 < INKCELL_ICON_SIZE) ? x0 + 1 : x0;
    const int32_t fx = sample_x & 0xFF;
    const int32_t upper = row0[x0] * (256 - fx) + row0[x1] * fx;
    const int32_t lower = row1[x0] * (256 - fx) + row1[x1] * fx;
    const int32_t alpha = upper * (256 - fy) + lower * fy; /* 0 .. INKCELL_ICON_MAX_ALPHA << 16 */
    return (int)((alpha * (INKCELL_FB_BLEND_STEPS - 1)) / (INKCELL_ICON_MAX_ALPHA * 65536));
}

/*
 * Draw one icon sprite into the cell, in `ink` over `ground`.
 *
 * The two colours are the whole difference from inkcell_fb_draw_emoji(). An emoji carries its own
 * palette; an icon carries coverage only and is *tinted*, so a chevron on a selected row is the
 * selected row's ink and a warning is the bad tone - it is themed like the text it stands
 * beside, because it is doing that text's job.
 *
 * `ground` is what it is blended against, and it has to be passed in for the same reason
 * inkcell_fb_draw_emoji() does not blend at all: what is already on the panel is not readable from
 * here - an icon sits on the ground on one row and on the cursor fill on the next - and the
 * display engine composites fb0 against its own layer rather than against what we have drawn,
 * so there is no alpha to leave the job to. A caller that has just filled a row knows the
 * colour it filled it with; nothing else does.
 *
 * Sampling is bilinear, unlike the emoji path's nearest neighbour, and that is not a
 * preference: the sprite is 32 px and the cell it lands in is 28 at the body scale and 21 in
 * the chrome, so nearest neighbour would drop every fourth source row - and on the empty
 * screen's symbol, several times that size, would duplicate them instead. On a flat-filled
 * emoji either is invisible; on a 2 px chevron stroke it is the difference between a smooth
 * diagonal and a staircase. The blend is one multiply per channel per step rather than per
 * pixel, because coverage is quantised into INKCELL_FB_BLEND_STEPS packed colours first.
 */
void inkcell_fb_draw_icon(const struct inkcell_backend_fb_state *state, int x, int y,
                          enum inkcell_icon icon, int scale, struct inkcell_rgb ink,
                          struct inkcell_rgb ground) {
    if (!inkcell_icon_is_valid(icon) || scale <= 0 || scale > INKCELL_FB_ICON_SCALE_MAX) {
        return;
    }
    const int box = inkcell_fb_icon_drawn(state, scale);
    /* Centred on the cell in both directions, so it sits on the same optical line as the
       capitals beside it and in the same column the layout above counted. */
    const int left = x - (box - inkcell_fb_icon_box(state, scale)) / 2;
    const int top = y + ((int)inkcell_fb_font(state)->height * scale - box) / 2;

    /* Source column per destination column, as a 8.8 fixed-point position: identical for every
       row, so the division runs once per column instead of once per pixel. Sized for the
       largest icon anything asks for, which is the empty state's - the rest are one text cell. */
    int32_t sx[INKCELL_FB_ICON_DRAWN_MAX];
    if (box <= 0 || box > (int)(sizeof sx / sizeof sx[0])) {
        return;
    }
    for (int dx = 0; dx < box; ++dx) {
        /* Half-pixel offsets at both ends: sampling from the pixel's centre is what keeps a
           symmetric symbol symmetric after the scale. */
        const int32_t pos = (((int32_t)dx * 2 + 1) * INKCELL_ICON_SIZE * 128) / box - 128;
        sx[dx] = pos < 0 ? 0 : pos;
    }

    uint8_t pixels[INKCELL_ICON_SIZE * INKCELL_ICON_SIZE];
    inkcell_icon_alpha(icon, pixels);

    uint32_t blend[INKCELL_FB_BLEND_STEPS];
    inkcell_fb_blend_table(state, ink, ground, blend);

    for (int dy = 0; dy < box; ++dy) {
        const int32_t pos_y = (((int32_t)dy * 2 + 1) * INKCELL_ICON_SIZE * 128) / box - 128;
        const int32_t py = pos_y < 0 ? 0 : pos_y;
        const int32_t y0 = py >> 8;
        const int32_t y1 = (y0 + 1 < INKCELL_ICON_SIZE) ? y0 + 1 : y0;
        const int32_t fy = py & 0xFF;
        const uint8_t *row0 = &pixels[y0 * INKCELL_ICON_SIZE];
        const uint8_t *row1 = &pixels[y1 * INKCELL_ICON_SIZE];

        int dx = 0;
        while (dx < box) {
            const int step = inkcell_fb_icon_step(row0, row1, sx[dx], fy);
            /* Runs of equal coverage - which most of a filled symbol is - become one span, the
               way the emoji path coalesces equal palette indices. */
            int end = dx + 1;
            while (end < box && inkcell_fb_icon_step(row0, row1, sx[end], fy) == step) {
                ++end;
            }
            if (step > 0) {
                inkcell_fb_fill_packed(state, left + dx, top + dy, end - dx, 1, blend[step]);
            }
            dx = end;
        }
    }
}

/*
 * Draw `text` as UTF-8, one cell per character - or per emoji, which may be several
 * codepoints.
 *
 * Walking cells rather than bytes is the whole point: a node named with a single emoji used to
 * draw as four question marks, because every byte of the sequence fell through the font's
 * ASCII range separately. Everything below measures with the same walker, so a line is always
 * as wide as it draws.
 */
void inkcell_fb_draw_text(const struct inkcell_backend_fb_state *state, int x, int y,
                          const char *text, int scale, struct inkcell_rgb ink,
                          struct inkcell_rgb ground) {
    inkcell_fb_draw_text_weight(state, x, y, text, scale, INKCELL_WEIGHT_REGULAR, ink, ground);
}

void inkcell_fb_draw_text_weight(const struct inkcell_backend_fb_state *state, int x, int y,
                                 const char *text, int scale, enum inkcell_weight weight,
                                 struct inkcell_rgb ink, struct inkcell_rgb ground) {
    const struct inkcell_font *font = inkcell_font_at_weight(inkcell_fb_font(state), weight);
    /* One ramp for the whole run: every character in it is the same ink over the same ground,
       and building the table per glyph would cost more than drawing one. */
    uint32_t blend[INKCELL_FB_BLEND_STEPS];
    inkcell_fb_blend_table(state, ink, ground, blend);

    int cursor = x;
    size_t offset = 0;
    for (;;) {
        const struct inkcell_text_cell cell = inkcell_text_cell_next(&text[offset]);
        if (cell.bytes == 0U) {
            break;
        }
        offset += cell.bytes;

        if (cell.is_emoji) {
            inkcell_fb_draw_emoji(state, cursor, y, cell.sprite, scale);
            /* An emoji is a square sprite, not a letter: it steps its own box whatever the
               face beside it does, which is the nominal advance. */
            cursor += inkcell_fb_char_adv(state, scale);
            continue;
        }
        if (cell.codepoint == (uint32_t)'\n') {
            y += inkcell_fb_line_adv(state, scale);
            cursor = x;
            continue;
        }
        inkcell_fb_draw_glyph_ramp(state, cursor, y, cell.codepoint, scale, font, blend);
        cursor += inkcell_font_advance_cp(font, cell.codepoint, scale);
    }
}

void inkcell_fb_fill_rect(const struct inkcell_backend_fb_state *state, int x, int y, int w, int h,
                          struct inkcell_rgb color) {
    inkcell_fb_fill_packed(state, x, y, w, h, compose_color(state, color.r, color.g, color.b));
}

void inkcell_fb_fill_round_rect_ends(const struct inkcell_backend_fb_state *state, int x, int y,
                                     int w, int h, int radius, struct inkcell_rgb color,
                                     bool round_top, bool round_bottom) {
    if (w <= 0 || h <= 0) {
        return;
    }
    if (!round_top && !round_bottom) {
        inkcell_fb_fill_rect(state, x, y, w, h, color);
        return;
    }
    const int limit = (w < h ? w : h) / 2;
    if (radius > limit) {
        radius = limit;
    }
    if (radius <= 0) {
        inkcell_fb_fill_rect(state, x, y, w, h, color);
        return;
    }

    const uint32_t packed = compose_color(state, color.r, color.g, color.b);
    /* The straight middle, then a span per row of each corner band. A square end takes its own
       band back into the middle, so the two are one fill rather than a fill and a patch - which
       is what keeps a squared corner from showing a seam where the two would have met. */
    const int top_band = round_top ? radius : 0;
    const int bottom_band = round_bottom ? radius : 0;
    inkcell_fb_fill_packed(state, x, y + top_band, w, h - top_band - bottom_band, packed);

    /*
     * How far in the fill starts on each of the rounded rows.
     *
     * Everything is doubled so the test lands on pixel *centres* without leaving integers:
     * the row's centre is half a pixel below its top edge, and a circle drawn from pixel
     * corners is visibly lopsided at this size. `2*d - 2*r + 1` is twice the offset of the
     * pixel centre from the arc's centre, and the comparison is that against twice the radius.
     */
    const int diameter_sq = 4 * radius * radius;
    for (int dy = 0; dy < radius; ++dy) {
        const int oy = 2 * dy - 2 * radius + 1;
        int dx = 0;
        while (dx < radius) {
            const int ox = 2 * dx - 2 * radius + 1;
            if (ox * ox + oy * oy <= diameter_sq) {
                break;
            }
            ++dx;
        }
        const int span = w - 2 * dx;
        if (round_top) {
            inkcell_fb_fill_packed(state, x + dx, y + dy, span, 1, packed);
        }
        if (round_bottom) {
            inkcell_fb_fill_packed(state, x + dx, y + h - 1 - dy, span, 1, packed);
        }
    }
}

void inkcell_fb_fill_round_rect(const struct inkcell_backend_fb_state *state, int x, int y, int w,
                                int h, int radius, struct inkcell_rgb color) {
    inkcell_fb_fill_round_rect_ends(state, x, y, w, h, radius, color, true, true);
}

void inkcell_fb_clear(const struct inkcell_backend_fb_state *state, struct inkcell_rgb color) {
    inkcell_fb_fill_rect(state, 0, 0, (int)state->var.xres, (int)state->var.yres, color);
}

/* Columns of text that fit between the margins at this scale. */
size_t inkcell_fb_cols(const struct inkcell_backend_fb_state *state, int scale) {
    const int usable = (int)state->var.xres - 2 * inkcell_fb_margin(state);
    if (usable <= 0) {
        return 1U;
    }
    return (size_t)(usable / inkcell_fb_char_adv(state, scale));
}

size_t inkcell_fb_row_cols(const struct inkcell_backend_fb_state *state, int scale) {
    const struct inkcell_fb_row_box box = inkcell_fb_row_box(state);
    const int usable = box.text_right - box.text_x;
    const int adv = inkcell_fb_char_adv(state, scale);
    if (usable <= 0 || adv <= 0 || usable < adv) {
        return 1U;
    }
    return (size_t)(usable / adv);
}

/* Clip a line to `cols` columns. Counted in drawn cells, not bytes, so a character is never
   cut in half - half a sequence would draw as the replacement box and, on the paths that also
   log or serialise the line, would be malformed UTF-8 - and a flag or a ZWJ sequence is never
   split into the pieces it is spelled with. */
void inkcell_fb_fit(char *line, size_t cols) {
    inkcell_text_cell_truncate(line, cols);
}

/* Columns a line occupies once drawn. */
size_t inkcell_fb_width(const char *line) {
    return inkcell_text_cells(line);
}

/*
 * The cursor's highlight, and the ground everything on the row is drawn against.
 *
 * It is a rounded, inset shape rather than a full-bleed bar. Both halves of that matter and for
 * the same reason: a bar running edge to edge reads as a *band across the screen*, while a
 * shape with ends reads as one row picked out of a column of them - which is what a cursor is.
 * The corners come from the theme's shape scale, so a theme that wants the old bar back asks
 * for INKCELL_SHAPE_SM of zero rather than for a different renderer.
 *
 * Its own function because a list mixes row shapes: a plain row and a section heading in the
 * same list highlighting to two slightly different rectangles is a cursor that changes shape as
 * it walks, and two copies of `y - scale` is exactly how that happens.
 */
struct inkcell_rgb inkcell_fb_draw_row_fill_on(const struct inkcell_backend_fb_state *state, int y,
                                               uint32_t rows, bool selected,
                                               enum inkcell_color ground) {
    if (!selected) {
        /* Nothing is painted: the row is already standing on whatever laid that colour down -
           the panel, or the card surface a grouped list drew before any row was placed. What is
           returned is what the text will be blended against, which is the only thing a caller
           wanted from an unselected row. */
        return inkcell_fb_color(state, ground);
    }
    const struct inkcell_rgb fill = inkcell_fb_color(state, INKCELL_COLOR_SURFACE_SEL);
    const int line = inkcell_fb_line_adv(state, state->scale);
    const struct inkcell_fb_row_box box = inkcell_fb_row_box(state);
    inkcell_fb_fill_round_rect(state, box.x, y - state->scale, box.w,
                               (int)(rows > 0U ? rows : 1U) * line,
                               inkcell_fb_radius(state, INKCELL_SHAPE_SM), fill);
    return fill;
}

struct inkcell_rgb inkcell_fb_draw_row_fill(const struct inkcell_backend_fb_state *state, int y,
                                            uint32_t rows, bool selected) {
    return inkcell_fb_draw_row_fill_on(state, y, rows, selected, INKCELL_COLOR_BG);
}

/* One list row of text, highlighted when it is the cursor: the fill above, then the words. */
void inkcell_fb_draw_row(const struct inkcell_backend_fb_state *state, int y, const char *text,
                         struct inkcell_rgb color, bool selected) {
    const struct inkcell_rgb ground = inkcell_fb_draw_row_fill(state, y, 1U, selected);
    if (selected) {
        color = inkcell_fb_color(state, INKCELL_COLOR_TEXT_ON_SEL);
    }
    inkcell_fb_draw_text(state, inkcell_fb_row_box(state).text_x, y, text, state->scale, color,
                         ground);
}

/* "3m", "2h", "5d" since a radio-reported epoch; "?" when either clock is unusable. */
void inkcell_fb_format_age(uint32_t last_heard, char *out, size_t out_len) {
    if (last_heard == 0U) {
        snprintf(out, out_len, "%s", inkcell_str(INKCELL_STR_COMMON_UNKNOWN_SHORT));
        return;
    }
    const uint32_t now = inkcell_time_wall_s();
    if (now == 0U || now < last_heard) {
        snprintf(out, out_len, "%s", inkcell_str(INKCELL_STR_TIME_NOW));
        return;
    }
    const uint32_t delta = now - last_heard;
    if (delta < 60U) {
        inkcell_str_format(out, out_len, INKCELL_STR_TIME_SECONDS_SHORT, delta);
    } else if (delta < 3600U) {
        inkcell_str_format(out, out_len, INKCELL_STR_TIME_MINUTES_SHORT, delta / 60U);
    } else if (delta < 86400U) {
        inkcell_str_format(out, out_len, INKCELL_STR_TIME_HOURS_SHORT, delta / 3600U);
    } else {
        inkcell_str_format(out, out_len, INKCELL_STR_TIME_DAYS_SHORT, delta / 86400U);
    }
}

void inkcell_fb_format_clock(uint32_t rx_time, char *out, size_t out_len) {
    if (rx_time == 0U) {
        out[0] = '\0';
        return;
    }
    time_t t = (time_t)rx_time;
    struct tm tm_buf;
    if (localtime_r(&t, &tm_buf) == NULL) {
        out[0] = '\0';
        return;
    }
    strftime(out, out_len, "%H:%M", &tm_buf);
}

/* Wraps `text` into at most `max_lines` lines of `cols` columns, drawing each from `x`.

   The x is a parameter because a dialog's paragraph is inset from its panel's edge rather than
   from the body's - the body margin was the only answer while the only wrapped text on screen
   was a screen's own. */
int inkcell_fb_draw_wrapped_at(const struct inkcell_backend_fb_state *state, int x, int y,
                               const char *text, size_t width, int max_lines,
                               struct inkcell_rgb color, struct inkcell_rgb ground) {
    struct inkcell_fb_wrap_ctx wctx;
    const struct inkcell_wrap_metric metric = inkcell_fb_wrap_metric(&wctx, state, state->scale);
    struct inkcell_wrap wrap;
    inkcell_wrap_begin_measured(&wrap, text, width, &metric);

    int lines = 0;
    while (lines < max_lines && inkcell_wrap_next(&wrap)) {
        inkcell_fb_draw_text(state, x, y, wrap.line, state->scale, color, ground);
        y += inkcell_fb_line_adv(state, state->scale);
        ++lines;
    }
    return lines;
}

/* The same, from the body's left margin - which is where a screen's own wrapped text starts. */
int inkcell_fb_draw_wrapped(const struct inkcell_backend_fb_state *state, int y, const char *text,
                            size_t width, int max_lines, struct inkcell_rgb color,
                            struct inkcell_rgb ground) {
    return inkcell_fb_draw_wrapped_at(state, inkcell_fb_margin(state), y, text, width, max_lines,
                                      color, ground);
}

uint32_t inkcell_fb_wrapped_lines(const struct inkcell_backend_fb_state *state, const char *text,
                                  size_t width, int scale) {
    struct inkcell_fb_wrap_ctx wctx;
    const struct inkcell_wrap_metric metric = inkcell_fb_wrap_metric(&wctx, state, scale);
    return inkcell_wrap_lines_measured(text, width, &metric);
}
