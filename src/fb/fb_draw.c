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
#include "inkwell/base/env.h"
#include "inkwell/base/text.h"
#include "inkwell/base/time.h"

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

void inkcell_fb_glyph_cache_free(struct inkcell_draw_state *state) {
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
void inkcell_fb_state_set_theme(struct inkcell_draw_state *state, const struct inkcell_theme *theme,
                                int scale) {
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
    /* And the layers, for the same reason: the boxes they are travelling between describe a
       geometry this state no longer has. See inkcell_fb_overlay_reset(). */
    inkcell_fb_overlay_reset(state);
    /* And the frame's own transition, for the same reason and one more: a theme switch is not a
       move between screens, so a screen that slid in because the palette changed would be
       animating an event that did not happen. */
    memset(&state->slide, 0, sizeof state->slide);
    state->slide_dir = 0;
}

void inkcell_fb_state_set_now(struct inkcell_draw_state *state, uint64_t now_ms) {
    if (state != NULL && now_ms > state->now_ms) {
        state->now_ms = now_ms;
    }
}

bool inkcell_fb_state_animating(const struct inkcell_draw_state *state) {
    if (state == NULL) {
        return false;
    }
    /* The transition, the focus ring and a gliding list are asked about separately from the
       table because they are kept separately - see `slide`, `focus_ring` and `list_glide` on
       the state. A frame owes another one
       while either has somewhere to be. An app still filling owes one for a reason that is not an
       animation at all: the next piece is read on the next frame, so without this the fill would
       stop wherever the last press left it. */
    for (uint32_t i = 0U; i < INKCELL_OVERLAY_SLOTS; ++i) {
        /* A layer arriving or leaving is owed the next frame, and a layer that has *finished*
           leaving is owed one more than that: its slot is released by the draw, so without a
           frame in which that draw happens the panel keeps the last position it was in. */
        if (state->overlays[i].id != INKCELL_OVERLAY_NONE &&
            (inkcell_anim_active(&state->overlays[i].travel, state->now_ms) ||
             !state->overlays[i].up)) {
            return true;
        }
    }
    return inkcell_anim_active(&state->slide, state->now_ms) ||
           inkcell_anim_active(&state->focus_ring.travel, state->now_ms) ||
           inkcell_anim_active(&state->list_glide.travel, state->now_ms) ||
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
void inkcell_fb_transition_begin(struct inkcell_draw_state *state, enum inkcell_transition move) {
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

int inkcell_fb_transition_offset(struct inkcell_draw_state *state) {
    if (state == NULL || state->slide_dir == 0) {
        return 0;
    }
    const int32_t remaining = INKCELL_ANIM_ONE - inkcell_anim_value(&state->slide, state->now_ms);
    if (remaining <= 0) {
        state->slide_dir = 0;
        return 0;
    }
    const int travel = inkcell_fb_region(state).w * INKCELL_FB_TRANSITION_TRAVEL_NUM /
                       INKCELL_FB_TRANSITION_TRAVEL_DEN;
    return state->slide_dir * (int)(((int64_t)remaining * travel) / INKCELL_ANIM_ONE);
}

void inkcell_fb_shift_begin(struct inkcell_draw_state *state, int dx, int top, int bottom) {
    if (state == NULL || bottom <= top) {
        return;
    }
    state->shift_x = dx;
    state->shift_top = top;
    state->shift_bottom = bottom;
    /* Across, the band is the region: with a rail beside the body, a screen sliding in must
       pass under the rail's edge rather than over the rail. Unnarrowed it is the surface, which
       is the band this always was. */
    const struct inkcell_box region = inkcell_fb_region(state);
    state->shift_left = region.x;
    state->shift_right = region.x + region.w;
    state->shift_active = true;
}

void inkcell_fb_shift_end(struct inkcell_draw_state *state) {
    if (state != NULL) {
        state->shift_active = false;
        state->shift_x = 0;
    }
}

/* ---- the view stack --------------------------------------------------------------------------
 *
 * See include/inkcell/ui/fb_draw.h. Everything here is composition: a push adds its offset to
 * the one already in force and intersects its box with the box already in force, so the clip
 * that every pixel goes through - inkcell_fb_clip_box() - reads one entry and does no walking.
 */

/* The whole panel, as an entry: what the bottom of the stack is measured against. */
static struct inkcell_fb_view inkcell_fb_view_panel(const struct inkcell_draw_state *state) {
    return (struct inkcell_fb_view){
        .dx = 0,
        .dy = 0,
        .x = 0,
        .y = 0,
        .right = inkcell_fb_panel_width(state),
        .bottom = inkcell_fb_panel_height(state),
    };
}

/* The entry in force, or the panel when nothing is pushed. */
static struct inkcell_fb_view inkcell_fb_view_top(const struct inkcell_draw_state *state) {
    if (state->views == 0U) {
        return inkcell_fb_view_panel(state);
    }
    return state->views_stack[state->views - 1U];
}

bool inkcell_fb_view_push(struct inkcell_draw_state *state, struct inkcell_fb_rect box, int dx,
                          int dy) {
    if (state == NULL || state->views >= INKCELL_FB_VIEW_DEPTH) {
        return false;
    }
    const struct inkcell_fb_view under = inkcell_fb_view_top(state);
    /* The box arrives in the *enclosing* view's coordinates, which is what a caller has in
       hand: a sheet's content asks for a region of the sheet, not a region of the panel. So it
       is translated by what is already in force before it is intersected with it. */
    const int left = box.x + under.dx;
    const int top = box.y + under.dy;
    struct inkcell_fb_view view = {
        .dx = under.dx + dx,
        .dy = under.dy + dy,
        .x = left > under.x ? left : under.x,
        .y = top > under.y ? top : under.y,
        .right = (left + box.w) < under.right ? (left + box.w) : under.right,
        .bottom = (top + box.h) < under.bottom ? (top + box.h) : under.bottom,
    };
    if (view.right <= view.x || view.bottom <= view.y) {
        /* Nothing of it is on the panel. Refused rather than pushed empty, so a caller cannot
           spend a frame drawing into a window that is not there - and so the `if` around the
           push is the whole of the test. */
        return false;
    }
    state->views_stack[state->views++] = view;
    return true;
}

void inkcell_fb_view_pop(struct inkcell_draw_state *state) {
    if (state != NULL && state->views > 0U) {
        state->views--;
    }
}

struct inkcell_fb_rect inkcell_fb_view_box(const struct inkcell_draw_state *state) {
    if (state == NULL) {
        return (struct inkcell_fb_rect){0, 0, 0, 0};
    }
    const struct inkcell_fb_view view = inkcell_fb_view_top(state);
    return (struct inkcell_fb_rect){
        .x = view.x, .y = view.y, .w = view.right - view.x, .h = view.bottom - view.y};
}

struct inkcell_fb_rect inkcell_fb_view_content_box(const struct inkcell_draw_state *state) {
    if (state == NULL) {
        return (struct inkcell_fb_rect){0, 0, 0, 0};
    }
    const struct inkcell_fb_view view = inkcell_fb_view_top(state);
    return (struct inkcell_fb_rect){.x = view.x - view.dx,
                                    .y = view.y - view.dy,
                                    .w = view.right - view.x,
                                    .h = view.bottom - view.y};
}

bool inkcell_fb_state_set_theme_by_id(struct inkcell_draw_state *state, const char *id) {
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

void inkcell_fb_set_app(struct inkcell_draw_state *state, const struct inkcell_fb_app *app) {
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

void inkcell_fb_set_focus_map(struct inkcell_draw_state *state, struct inkcell_focus_map *map) {
    if (state == NULL) {
        return;
    }
    state->focus = map;
}

/*
 * The one place a widget's geometry becomes something a d-pad can reach.
 *
 * Written through a const state deliberately: the map is the screen's, not the frame's, so
 * registering into it does not make the frame mutable and the dozens of draw functions that
 * take the state immutably stay that way. What is const here is the panel, and the panel is
 * exactly what this does not touch.
 */
/*
 * The one place a component's geometry becomes something a d-pad can reach.
 *
 * Written through a const state deliberately: the map is the screen's, not the frame's, so
 * registering into it does not make the frame mutable and the dozens of draw functions that
 * take the state immutably stay that way. What is const here is the panel, and the panel is
 * exactly what this does not touch.
 */
static void inkcell_fb_focus_put(const struct inkcell_draw_state *state, uint32_t id,
                                 const struct inkcell_fb_rect *rect, int radius,
                                 bool pointer_only) {
    if (state == NULL || state->focus == NULL || id == INKCELL_FOCUS_NONE || rect == NULL) {
        return;
    }
    /*
     * A box with no part of it on the panel is not a place to stand, whatever was asked for.
     * The components only lay out what they are drawing, so this catches the screen that
     * measured something wrong rather than anything inkcell does - and it is the one clip worth
     * applying here.
     *
     * The two it deliberately does not apply are the ones the pixels go through, and they
     * would each make this answer a different question:
     *
     *   - **The damage band** (`clip_active`). That is what limits a *partial redraw* to the
     *     rows that changed, so intersecting with it would make the map hold whatever happened
     *     to be repainted this frame. A reader who moved a switch would find the other half of
     *     the screen unreachable until something else redrew it.
     *   - **The transition shift** (`shift_active`). A screen sliding in is every box moved by
     *     one dx, and a uniform translation changes no answer this map is asked for - what is
     *     to my right is to my right at every point of the slide. Registering the travelling
     *     position instead would clip away whatever had not arrived yet, so the screen would be
     *     navigable only in the parts that had landed. The boxes here are where the layout put
     *     things, which is where they will be when the slide ends and where a press is resolved
     *     against in the meantime.
     *
     * The *view* stack is the one transform that does apply, and the difference is the whole
     * reason it is a different mechanism. A view can move part of the frame out of sight: a row
     * scrolled past the top of its viewport, a menu item below the bottom of the menu. Those
     * are not places to stand, and a map that held them would let the cursor walk onto
     * something nobody can see and then press it. So a box is registered where the view put it,
     * and one the view cut away entirely is not registered at all - which is the rule the list
     * has always followed by registering only the rows it drew, stated once for everything.
     *
     * And a box the view cut *in half* is registered as the half that is on the panel. That is
     * the same rule rather than a second one, and both of the things it fixes are real. The
     * focus ring is drawn after the view has been popped, from the rectangle in this map - so
     * a row half above its viewport would have its ring painted up across the chrome above it,
     * which is the exact lie the ring exists to prevent. And a direction is answered by
     * comparing boxes, so a box that reaches somewhere the reader cannot see is a box that
     * wins presses it should lose.
     *
     * The radius is kept as it was, and that is deliberate: it is the curve the *component*
     * drew, and a ring that squared its corners at a cut would be reporting a shape nothing on
     * the panel has. A ring around a half-row follows the half-row.
     */
    const struct inkcell_fb_view view = inkcell_fb_view_top(state);
    int x = rect->x + view.dx;
    int y = rect->y + view.dy;
    int w = rect->w;
    int h = rect->h;
    if (x < view.x) {
        w -= view.x - x;
        x = view.x;
    }
    if (y < view.y) {
        h -= view.y - y;
        y = view.y;
    }
    if (x + w > view.right) {
        w = view.right - x;
    }
    if (y + h > view.bottom) {
        h = view.bottom - y;
    }
    if (w <= 0 || h <= 0) {
        return;
    }
    if (pointer_only) {
        (void)inkcell_focus_add_target(state->focus, id, x, y, w, h, radius);
    } else {
        (void)inkcell_focus_add_round(state->focus, id, x, y, w, h, radius);
    }
}

void inkcell_fb_focus_register(const struct inkcell_draw_state *state, uint32_t id,
                               const struct inkcell_fb_rect *rect) {
    inkcell_fb_focus_put(state, id, rect, 0, false);
}

void inkcell_fb_focus_register_shaped(const struct inkcell_draw_state *state, uint32_t id,
                                      const struct inkcell_fb_rect *rect,
                                      enum inkcell_shape shape) {
    if (state == NULL || rect == NULL) {
        return;
    }
    /* INKCELL_SHAPE_FULL asks for more radius than any box has and the fill clamps it to half
       the shorter side; inkcell_focus_add_round() clamps the same way, so what lands in the map
       is the curve that was drawn. */
    inkcell_fb_focus_put(state, id, rect, inkcell_fb_radius(state, shape), false);
}

void inkcell_fb_focus_mark(const struct inkcell_draw_state *state, uint32_t id) {
    if (state == NULL || !inkcell_focus_has(state->focus, id)) {
        return;
    }
    inkcell_focus_mark(state->focus, id);
}

void inkcell_fb_target_register(const struct inkcell_draw_state *state, uint32_t id,
                                const struct inkcell_fb_rect *rect) {
    inkcell_fb_focus_put(state, id, rect, 0, true);
}

bool inkcell_fb_app_pending(const struct inkcell_draw_state *state) {
    return state != NULL && state->app.pending != NULL && state->app.pending(state->app.ctx);
}

void inkcell_fb_animation_damage(struct inkcell_draw_state *state, int x, int y, int w, int h) {
    if (state == NULL || w <= 0 || h <= 0) {
        return;
    }
    /*
     * Where something moved this frame, unioned with wherever else did.
     *
     * This exists for the one case a row-by-row compare gets wrong: a frame drawn under a clip
     * band - a list repainting its own rows and nothing else - skips every row outside the
     * band when it is copied to the panel, and a switch sliding two rows above the band is
     * exactly such a row. The widget that slid is the only thing that knows it did, so it says
     * so here and inkcell_fb_copy_damage() lets those rows through.
     *
     * A union rather than a list: the rows are what matter and a second rectangle beside the
     * first costs at most the rows between them, which on a panel this size is cheaper than
     * keeping and walking a list of them would be.
     */
    int right = x + w;
    int bottom = y + h;
    if (x < 0) {
        x = 0;
    }
    if (y < 0) {
        y = 0;
    }
    if (right > inkcell_fb_panel_width(state)) {
        right = inkcell_fb_panel_width(state);
    }
    if (bottom > inkcell_fb_panel_height(state)) {
        bottom = inkcell_fb_panel_height(state);
    }
    if (right <= x || bottom <= y) {
        return;
    }

    struct inkcell_fb_damage_rect *damage = &state->animation_damage;
    if (!damage->valid) {
        damage->x = x;
        damage->y = y;
        damage->right = right;
        damage->bottom = bottom;
        damage->valid = true;
        return;
    }
    if (x < damage->x) {
        damage->x = x;
    }
    if (y < damage->y) {
        damage->y = y;
    }
    if (right > damage->right) {
        damage->right = right;
    }
    if (bottom > damage->bottom) {
        damage->bottom = bottom;
    }
}

void inkcell_fb_app_frame_begin(struct inkcell_draw_state *state) {
    if (state == NULL) {
        return;
    }
    /* Last frame's moving parts are not this one's. Cleared here rather than after the copy so
       that a caller which renders without presenting - the capture harness - does not carry a
       rectangle from one page into the next. */
    state->animation_damage = (struct inkcell_fb_damage_rect){0};
    /*
     * With one exception, and it is the one thing a frame inherits from the last: wherever the
     * focus ring painted.
     *
     * A ring is erased by whatever is under it being drawn again, and that happens before the
     * ring is asked where it is going - so a ring that declared its old position at the point
     * it moved would be declaring it a whole frame too late, after everything that could have
     * repainted those rows had already been told they had not changed. Said here it is in
     * place before the first fill. It is also the only way a ring that has *gone* is cleared
     * up at all: there is no draw call left to say anything on that frame.
     */
    const struct inkcell_fb_damage_rect painted = state->focus_ring.drawn;
    state->focus_ring.drawn = (struct inkcell_fb_damage_rect){0};
    if (painted.valid) {
        inkcell_fb_animation_damage(state, painted.x, painted.y, painted.right - painted.x,
                                    painted.bottom - painted.y);
    }
    /*
     * And every layer's, for the ring's reason exactly - which is worth spelling out, because
     * the layers first declared their own damage from inkcell_fb_overlay_begin() and that is
     * a frame too late in precisely the way the paragraph above describes.
     *
     * A layer is drawn over the body, so the body under it is drawn *first*. By the time
     * begin() runs, everything that could have repainted the rows the layer is vacating has
     * already been told those rows did not change - so under a clip band the old position is
     * never repainted and the layer leaves a trail behind it.
     *
     * The scrim makes the same mistake worse rather than merely visible. It is a
     * read-modify-write over what is already on the panel, so a region that was dimmed last
     * frame and not repainted this one gets dimmed *again*: the body behind a modal would
     * darken a step per frame for as long as the modal was up. That is why the span a layer
     * records covers its scrim's whole region and not just its own box.
     */
    for (uint32_t i = 0U; i < INKCELL_OVERLAY_SLOTS; ++i) {
        const struct inkcell_fb_damage_rect was = state->overlays[i].drawn;
        state->overlays[i].drawn = (struct inkcell_fb_damage_rect){0};
        if (was.valid) {
            inkcell_fb_animation_damage(state, was.x, was.y, was.right - was.x, was.bottom - was.y);
        }
    }
    /*
     * And the layer order, which is a fact about one frame's draw sequence rather than
     * something carried between frames. The layers themselves are not cleared - a layer
     * outlives the app's interest in it, which is the whole of include/inkcell/ui/overlay.h -
     * but which of them was drawn last is answered afresh every frame.
     */
    state->overlay_order = 0U;
    if (state->app.frame_begin != NULL) {
        state->app.frame_begin(state->app.ctx);
    }
}

/* Asks the app to drop whatever it has memoised on this state - what a geometry or mode change
   invalidates. A no-op when nothing installed an app, or when it keeps no caches. */
void inkcell_fb_app_drop_caches(struct inkcell_draw_state *state) {
    if (state != NULL && state->app.drop_caches != NULL) {
        state->app.drop_caches(state, state->app.ctx);
    }
}

void inkcell_fb_render(struct inkcell_draw_state *state, const void *snapshot) {
    if (state == NULL || state->app.render == NULL) {
        return;
    }
    inkcell_fb_app_frame_begin(state);
    state->app.render(state, snapshot, state->app.ctx);
}

struct inkcell_rgb inkcell_fb_color(const struct inkcell_draw_state *state,
                                    enum inkcell_color role) {
    return inkcell_theme_color(state != NULL ? state->theme : NULL, role);
}

struct inkcell_rgb inkcell_fb_tone_color(const struct inkcell_draw_state *state,
                                         enum inkcell_tone tone) {
    return inkcell_theme_tone(state != NULL ? state->theme : NULL, tone);
}

struct inkcell_paint inkcell_fb_paint(const struct inkcell_draw_state *state,
                                      enum inkcell_family family, enum inkcell_slot slot,
                                      enum inkcell_state ui_state) {
    return inkcell_theme_paint(state != NULL ? state->theme : NULL, family, slot, ui_state);
}

struct inkcell_rgb inkcell_fb_state_layer(const struct inkcell_draw_state *state,
                                          enum inkcell_color fill, enum inkcell_color ink,
                                          enum inkcell_state ui_state) {
    return inkcell_theme_state_layer_for(state != NULL ? state->theme : NULL,
                                         inkcell_fb_color(state, fill),
                                         inkcell_fb_color(state, ink), ui_state);
}

/* One channel of the mix. Integer, and towards the ground rather than by an alpha, for the
   reason the whole of this file is: the same arithmetic on every host that draws it. */
static uint8_t inkcell_fb_fade_channel(uint8_t from, uint8_t to, int32_t progress) {
    const int32_t span = (int32_t)to - (int32_t)from;
    return (uint8_t)((int32_t)from + (span * progress) / INKCELL_ANIM_ONE);
}

struct inkcell_rgb inkcell_fb_fade(struct inkcell_rgb ink, struct inkcell_rgb ground,
                                   int32_t progress) {
    if (progress <= 0) {
        return ink;
    }
    if (progress >= INKCELL_ANIM_ONE) {
        return ground;
    }
    return (struct inkcell_rgb){
        .r = inkcell_fb_fade_channel(ink.r, ground.r, progress),
        .g = inkcell_fb_fade_channel(ink.g, ground.g, progress),
        .b = inkcell_fb_fade_channel(ink.b, ground.b, progress),
    };
}

const struct inkcell_metrics *inkcell_fb_metrics(const struct inkcell_draw_state *state) {
    return inkcell_theme_metrics(state != NULL ? state->theme : NULL);
}

const struct inkcell_font *inkcell_fb_font(const struct inkcell_draw_state *state) {
    return inkcell_theme_font(state != NULL ? state->theme : NULL);
}

int inkcell_fb_radius(const struct inkcell_draw_state *state, enum inkcell_shape shape) {
    return inkcell_theme_radius(state->theme, shape, state->scale);
}

int inkcell_fb_space(const struct inkcell_draw_state *state, enum inkcell_space space) {
    return inkcell_theme_space(state->theme, space, state->scale);
}

int inkcell_fb_space_at(const struct inkcell_draw_state *state, enum inkcell_space space,
                        int scale) {
    return inkcell_theme_space(state->theme, space, scale);
}

int inkcell_fb_type_scale(const struct inkcell_draw_state *state, enum inkcell_type type) {
    return inkcell_theme_type_scale(state->theme, type, state->scale);
}

enum inkcell_weight inkcell_fb_type_weight(const struct inkcell_draw_state *state,
                                           enum inkcell_type type) {
    return inkcell_theme_type_weight(state->theme, type);
}

struct inkcell_type_style inkcell_fb_type_style(const struct inkcell_draw_state *state,
                                                enum inkcell_type type) {
    return inkcell_theme_type_style(state != NULL ? state->theme : NULL, type,
                                    state != NULL ? state->scale : 0);
}

int inkcell_fb_gutter(const struct inkcell_draw_state *state) {
    const int margin = inkcell_fb_margin(state);
    return margin > 1 ? margin / 2 : margin;
}

uint32_t inkcell_fb_motion(const struct inkcell_draw_state *state, enum inkcell_motion motion) {
    return inkcell_theme_motion(state->theme, motion);
}

int inkcell_fb_edge(const struct inkcell_draw_state *state) {
    const int edge = inkcell_fb_space(state, INKCELL_SPACE_XS);
    return edge > 0 ? edge : 1;
}

int inkcell_fb_margin(const struct inkcell_draw_state *state) {
    return (int)inkcell_fb_metrics(state)->margin;
}

int inkcell_fb_scrim_depth(const struct inkcell_draw_state *state) {
    return inkcell_theme_opacity(state != NULL ? state->theme : NULL, INKCELL_OPACITY_SCRIM);
}

int inkcell_fb_rail_gutter(const struct inkcell_draw_state *state) {
    return inkcell_fb_gutter(state);
}

struct inkcell_box inkcell_fb_region(const struct inkcell_draw_state *state) {
    const struct inkcell_box surface = {
        .x = 0,
        .y = 0,
        .w = inkcell_fb_panel_width(state),
        .h = inkcell_fb_panel_height(state),
    };
    if (state == NULL || inkcell_box_is_empty(state->region)) {
        return surface;
    }
    /* Clamped to the surface, so a region set against a panel that has since shrunk - a window
       dragged smaller between two frames - is a smaller region rather than one off the edge. */
    struct inkcell_box box = state->region;
    const int right = box.x + box.w < surface.w ? box.x + box.w : surface.w;
    const int bottom = box.y + box.h < surface.h ? box.y + box.h : surface.h;
    box.x = box.x > 0 ? box.x : 0;
    box.y = box.y > 0 ? box.y : 0;
    box.w = right - box.x;
    box.h = bottom - box.y;
    return inkcell_box_is_empty(box) ? surface : box;
}

struct inkcell_box inkcell_fb_set_region(struct inkcell_draw_state *state, struct inkcell_box box) {
    if (state == NULL) {
        return (struct inkcell_box){0};
    }
    const struct inkcell_box was = state->region;
    state->region = inkcell_box_is_empty(box) ? (struct inkcell_box){0} : box;
    return was;
}

struct inkcell_box inkcell_fb_content_column(const struct inkcell_draw_state *state) {
    const struct inkcell_box region = inkcell_fb_region(state);
    struct inkcell_box column = {
        .x = region.x + inkcell_fb_margin(state),
        .y = region.y,
        .w = region.w - 2 * inkcell_fb_margin(state),
        .h = region.h,
    };
    if (column.w < 1) {
        column.w = 1;
        return column;
    }
    /*
     * The cap only engages above the compact class, and that is the whole of the rule.
     *
     * A compact surface *is* one column - that is what the class means - so capping it at the
     * measure would be measuring a thing against itself, and on a panel that sits a hair over
     * the measure it would shave a few pixels off every screen for nothing. Above compact
     * there is genuinely room to spare, and spending it on a wider column rather than on
     * margins is the thing that makes a maximised window unreadable.
     */
    if (inkcell_fb_width_class(state) == INKCELL_WIDTH_COMPACT) {
        return column;
    }
    return inkcell_box_measure(column, inkcell_fb_measure_width(state, INKCELL_WIDTH_MEASURE_COLS));
}

int inkcell_fb_content_x(const struct inkcell_draw_state *state) {
    return inkcell_fb_content_column(state).x;
}

int inkcell_fb_content_w(const struct inkcell_draw_state *state) {
    return inkcell_fb_content_column(state).w;
}

struct inkcell_fb_row_box inkcell_fb_row_box(const struct inkcell_draw_state *state) {
    const struct inkcell_box column = inkcell_fb_content_column(state);
    const int gutter = inkcell_fb_gutter(state);
    /*
     * The row's own padding: how far its words sit inside its fill. Derived rather than stated,
     * because the leading edge is what every row has always been drawn at - the fill starts at
     * the gutter and the text at the margin - and the trailing edge owes the same, or a value
     * column against the fill's right edge would be padded on one side only.
     */
    const int pad = inkcell_fb_margin(state) - gutter;
    struct inkcell_fb_row_box box;
    /*
     * Hung off the content column rather than off the panel, which is what lets a row stop
     * being as wide as the surface happens to be. A row's fill still stands a gutter outside
     * the column's text - that is what a fill is - so on a compact surface, where the column is
     * the margin-inset panel, this is the arithmetic it always was.
     */
    box.x = column.x - pad;
    box.w = column.w + 2 * pad - inkcell_fb_rail_gutter(state);
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

/* Scale an 8-bit channel into the surface's own field width and shift it into place. */
static inline uint32_t inkcell_fb_pack_channel(uint8_t value, const struct inkcell_channel *field) {
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
static inline uint32_t compose_color(const struct inkcell_draw_state *state, uint8_t r, uint8_t g,
                                     uint8_t b) {
    const struct inkcell_pixel_format *fmt = &state->surface.format;
    const bool has_fields = fmt->r.length != 0U || fmt->g.length != 0U || fmt->b.length != 0U;

    uint32_t color;
    uint32_t color_mask;
    if (has_fields) {
        color = inkcell_fb_pack_channel(r, &fmt->r) | inkcell_fb_pack_channel(g, &fmt->g) |
                inkcell_fb_pack_channel(b, &fmt->b);
        color_mask = inkcell_fb_pack_channel(0xFFU, &fmt->r) |
                     inkcell_fb_pack_channel(0xFFU, &fmt->g) |
                     inkcell_fb_pack_channel(0xFFU, &fmt->b);
    } else {
        switch (fmt->bits_per_pixel) {
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

    if (fmt->a.length != 0U) {
        color |= inkcell_fb_pack_channel(0xFFU, &fmt->a);
    } else if (fmt->bits_per_pixel == 32U) {
        color |= ~color_mask; /* opaque in whatever byte the colour channels leave free */
    }
    return color;
}

/*
 * The look this run is drawn with.
 *
 * <PREFIX>_THEME names it (see src/ui/theme/theme.c for the list) and <PREFIX>_FB_SCALE
 * overrides the glyph multiplier the theme asks for - an environment variable rather than a flag
 * because on the Brick the app is started by launch.sh, not by anyone with a shell.
 *
 * Every backend that opens a panel of its own wants this same answer, which is why it is here
 * rather than in whichever one was written first. The capture harness is the exception and
 * deliberately so: a scene script names its theme and its scale, and a screenshot that changed
 * because of the environment it ran in would be a test that agrees with its own host.
 */
void inkcell_fb_state_apply_theme_from_env(struct inkcell_draw_state *state) {
    const struct inkcell_theme *theme = inkcell_theme_from_env();
    /* A scale named in the environment outlives a theme switch: it is an explicit choice about
       this panel, where a theme's own scale is only that theme's default. */
    state->scale_pinned = inkwell_env_get("FB_SCALE") != NULL;
    /*
     * The knob stays in *whole* steps, where the scale it sets is in units.
     *
     * Everything inside counts quarters now, but this is the number somebody types on a device
     * over ssh, and <PREFIX>_FB_SCALE=4 has meant "the body size" for as long as there has been
     * one. Reading it in units would quietly halve every existing invocation, so the conversion
     * happens here - the one place the outside world states a scale.
     */
    const int steps = (int)inkwell_env_int("FB_SCALE", INKCELL_SCALE_MIN / INKCELL_SCALE_UNIT,
                                           INKCELL_SCALE_MAX / INKCELL_SCALE_UNIT, 0);
    const int scale = steps > 0 ? INKCELL_SCALE(steps) : inkcell_theme_scale(theme);
    inkcell_fb_state_set_theme(state, theme, scale);
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

/* Whether a box lies entirely inside the region a widget said it was animating in this frame.
   False when nothing has been declared, which is every frame that has no motion on it. */
static bool inkcell_fb_within_animation(const struct inkcell_draw_state *state, int x, int y, int w,
                                        int h) {
    const struct inkcell_fb_damage_rect *damage = &state->animation_damage;
    return damage->valid && x >= damage->x && y >= damage->y && x + w <= damage->right &&
           y + h <= damage->bottom;
}

static bool inkcell_fb_clip_box(const struct inkcell_draw_state *state, int x, int y, int w, int h,
                                struct inkcell_fb_clipped_box *out) {
    int dx = 0;
    int dy = 0;
    /*
     * The view stack first, because it is the *innermost* transform: a box arrives here in
     * whatever coordinates the region it was drawn in uses, and the entry in force is what
     * turns those into the frame's. The offset moves it and the box cuts it, and both are
     * already composed down to one of each - see inkcell_fb_view_push().
     *
     * `dx`/`dy` are how far into the source the clipped box starts, which is what a glyph or a
     * sprite has to skip to line up with where it landed. A row half above the top of a
     * scrolled viewport is exactly that case, and it is why the trim is accumulated rather than
     * the coordinate simply clamped.
     */
    if (state->views > 0U) {
        const struct inkcell_fb_view *const view = &state->views_stack[state->views - 1U];
        x += view->dx;
        y += view->dy;
        if (x < view->x) {
            const int trimmed = view->x - x;
            w -= trimmed;
            dx += trimmed;
            x = view->x;
        }
        if (y < view->y) {
            const int trimmed = view->y - y;
            h -= trimmed;
            dy += trimmed;
            y = view->y;
        }
        if (x + w > view->right) {
            w = view->right - x;
        }
        if (y + h > view->bottom) {
            h = view->bottom - y;
        }
        if (w <= 0 || h <= 0) {
            return false;
        }
    }
    /*
     * Then the frame's transform, which is the outermost one: a screen arriving from off the
     * right-hand edge is drawn at coordinates that are not on the panel at all, and the clamp
     * below is what turns that into the part of it that has arrived. After the view rather than
     * before it, so a region of a screen that is sliding slides with the screen - the window
     * and what is in it travel together, which is what makes a view inside a transition a
     * region of the frame rather than a hole cut in the panel.
     *
     * The band is applied here rather than left to the caller for the same reason - a row whose
     * glyphs overhang the top of the body must be cut off at the body, not drawn over the
     * navigation bar it is sliding underneath. See inkcell_fb_shift_begin().
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
        if (x < state->shift_left) {
            const int trimmed = state->shift_left - x;
            w -= trimmed;
            dx += trimmed;
            x = state->shift_left;
        }
        if (x + w > state->shift_right) {
            w = state->shift_right - x;
        }
        if (h <= 0 || w <= 0) {
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
    if (x + w > inkcell_fb_panel_width(state)) {
        w = inkcell_fb_panel_width(state) - x;
    }
    if (y + h > inkcell_fb_panel_height(state)) {
        h = inkcell_fb_panel_height(state) - y;
    }
    if (w <= 0 || h <= 0) {
        return false;
    }

    /*
     * The band, unless this box is a widget redrawing something that moved.
     *
     * A clipped frame repaints one band and leaves the rest of the panel alone, which is
     * exactly wrong for a control animating outside it: the switch two rows above the band
     * has somewhere new to be, and a clip that rejected its fills would leave the old knob
     * on the panel with no way to remove it. So a widget says where it is moving before it
     * draws - inkcell_fb_animation_damage() - and what it says is exempt here, and let
     * through again by inkcell_fb_copy_damage() when the frame is presented. Both halves, or
     * neither works: exempting only the copy copies pixels nothing redrew, and exempting only
     * the draw redraws pixels nothing copies.
     *
     * Whole containment rather than an intersection, because this function answers with one
     * box and a union of two is not one. A widget declares its damage padded around itself
     * and then draws inside it, so its own boxes are contained; anything straddling the
     * damage and the band is not that widget's and is clipped by the band as it always was.
     */
    if (state->clip_active && inkcell_fb_within_animation(state, x, y, w, h)) {
        out->x = x;
        out->y = y;
        out->w = w;
        out->h = h;
        out->dx = dx;
        out->dy = dy;
        return true;
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
static void inkcell_fb_fill_packed(const struct inkcell_draw_state *state, int x, int y, int w,
                                   int h, uint32_t packed) {
    if (w <= 0 || h <= 0) {
        return;
    }
    struct inkcell_fb_clipped_box box;
    if (!inkcell_fb_clip_box(state, x, y, w, h, &box)) {
        return;
    }

    const size_t bpp = state->surface.bytes_per_pixel;
    const size_t stride = state->surface.stride;
    uint8_t *row = state->surface.pixels + (size_t)box.y * stride + (size_t)box.x * bpp;

    for (int r = 0; r < box.h; ++r, row += stride) {
        if ((size_t)(row - state->surface.pixels) + (size_t)box.w * bpp > state->surface.size) {
            return;
        }
        inkcell_fb_store_span(row, box.w, packed, bpp);
    }
}

/* ---- anti-aliased shapes ---------------------------------------------------------------------
 *
 * A curve drawn by asking "is this pixel inside?" has a stepped edge, and at the radii Material
 * and iOS actually use - a pill, an avatar disc, a switch knob - the steps are long enough to
 * read as a staircase rather than as a curve. The answer is the one the glyph renderer has
 * always used here: coverage rather than a yes or no, blended into what is already there.
 *
 * What made that hard was the ground. inkcell_fb_blend_table() needs to know what a glyph is
 * being drawn *over*, and a caller could tell it; a rounded rectangle crosses a row fill
 * halfway down and could not. The way out is that this backend does not draw onto the panel at
 * all - inkcell_backend_fb_present() points the surface at a draw buffer in ordinary RAM, for
 * the whole render and copies the result afterwards. So the ground is readable: it is the pixel
 * about to be written over, and decompose_color() is what reads it back.
 *
 * Only edge pixels are read and blended. A shape's interior is still one bulk fill, so what
 * this costs is the boundary - a few hundred pixels on a card, a few thousand on a full-panel
 * disc - rather than the area.
 *
 * Integer arithmetic throughout, and that is not a preference. The golden sheet compares
 * digests of the rendered page across compilers and architectures, so a coverage value that
 * depended on a float's rounding would be a test that fails on one machine and passes on
 * another. Sixteen sub-samples a pixel, counted exactly.
 */

#define INKCELL_FB_AA_SUB 4 /* sub-samples per axis */
#define INKCELL_FB_AA_STEPS (INKCELL_FB_AA_SUB * INKCELL_FB_AA_SUB)
/* Sub-sample centres land on odd halves of a sub-pixel, so everything is doubled to stay whole:
   a sample's offset from a shape's centre is 2*SUB*(pixel - centre) + 2*sub + 1. */
#define INKCELL_FB_AA_FIXED (2 * INKCELL_FB_AA_SUB)

/* An `length`-bit channel widened back to eight bits by replication, so a full-scale value
   comes back full scale - five bits of 0x1F is 0xFF, not 0xF8. Exact in the direction that
   matters: inkcell_fb_pack_channel() truncates, so packing this again returns the original. */
static inline uint8_t inkcell_fb_widen_channel(uint32_t value, uint32_t length) {
    if (length == 0U) {
        return 0U;
    }
    if (length >= 8U) {
        return (uint8_t)(value >> (length - 8U));
    }
    uint32_t out = value;
    uint32_t have = length;
    while (have < 8U) {
        out = (out << length) | value;
        have += length;
    }
    return (uint8_t)(out >> (have - 8U));
}

static inline uint8_t inkcell_fb_unpack_channel(uint32_t packed,
                                                const struct inkcell_channel *field) {
    if (field->length == 0U) {
        return 0U;
    }
    const uint32_t mask = (field->length >= 32U) ? 0xFFFFFFFFU : ((1U << field->length) - 1U);
    return inkcell_fb_widen_channel((packed >> field->offset) & mask, field->length);
}

/* compose_color() read backwards. Every branch mirrors one up there; a format that function
   refuses comes back black, which is what it writes. */
static inline struct inkcell_rgb decompose_color(const struct inkcell_draw_state *state,
                                                 uint32_t packed) {
    const struct inkcell_pixel_format *fmt = &state->surface.format;
    const bool has_fields = fmt->r.length != 0U || fmt->g.length != 0U || fmt->b.length != 0U;
    struct inkcell_rgb rgb = {0U, 0U, 0U};
    if (has_fields) {
        rgb.r = inkcell_fb_unpack_channel(packed, &fmt->r);
        rgb.g = inkcell_fb_unpack_channel(packed, &fmt->g);
        rgb.b = inkcell_fb_unpack_channel(packed, &fmt->b);
        return rgb;
    }
    switch (fmt->bits_per_pixel) {
    case 32:
    case 24:
        rgb.r = (uint8_t)((packed >> 16) & 0xFFU);
        rgb.g = (uint8_t)((packed >> 8) & 0xFFU);
        rgb.b = (uint8_t)(packed & 0xFFU);
        break;
    case 16:
        rgb.r = inkcell_fb_widen_channel((packed >> 11) & 0x1FU, 5U);
        rgb.g = inkcell_fb_widen_channel((packed >> 5) & 0x3FU, 6U);
        rgb.b = inkcell_fb_widen_channel(packed & 0x1FU, 5U);
        break;
    default:
        break;
    }
    return rgb;
}

/* inkcell_fb_store_span() backwards, for one pixel. */
static inline uint32_t inkcell_fb_load_pixel(const uint8_t *px, size_t bpp) {
    switch (bpp) {
    case 4: {
        uint32_t packed = 0U;
        memcpy(&packed, px, 4U);
        return packed;
    }
    case 3:
        return (uint32_t)px[0] | ((uint32_t)px[1] << 8) | ((uint32_t)px[2] << 16);
    case 2: {
        uint16_t narrow = 0U;
        memcpy(&narrow, px, 2U);
        return narrow;
    }
    default:
        return px[0];
    }
}

static inline uint8_t inkcell_fb_mix_channel(uint8_t ground, uint8_t ink, int coverage) {
    /* Rounded rather than truncated, so a half-covered pixel between two colours lands in the
       middle rather than a step towards the ground. */
    const int mixed = (int)ground * (INKCELL_FB_AA_STEPS - coverage) + (int)ink * coverage +
                      INKCELL_FB_AA_STEPS / 2;
    return (uint8_t)(mixed / INKCELL_FB_AA_STEPS);
}

/*
 * One pixel at (`x`, `y`), blended `coverage` of the way towards `color`.
 *
 * A pixel at a time rather than a run of them, and that is a deliberate trade. A run would
 * clip once and blend many, which is faster - but it needs the coverages in a buffer first,
 * and every shape below wanted a different length for it: the corner's radius, the ring's
 * band, the arc's diameter. Sizing one buffer for all three is how a thick enough stroke on a
 * big enough shape came to write past the end of it. Clipping per pixel costs about a fifth
 * again on the edge pixels of a shape, which are the only pixels that reach here at all, and
 * it cannot be got wrong.
 */
static void inkcell_fb_blend_pixel(const struct inkcell_draw_state *state, int x, int y,
                                   struct inkcell_rgb color, int coverage) {
    if (coverage <= 0) {
        return;
    }
    struct inkcell_fb_clipped_box box;
    if (!inkcell_fb_clip_box(state, x, y, 1, 1, &box)) {
        return;
    }

    const size_t bpp = state->surface.bytes_per_pixel;
    const size_t stride = state->surface.stride;
    uint8_t *px = state->surface.pixels + (size_t)box.y * stride + (size_t)box.x * bpp;
    if ((size_t)(px - state->surface.pixels) + bpp > state->surface.size) {
        return;
    }

    if (coverage >= INKCELL_FB_AA_STEPS) {
        inkcell_fb_store_span(px, 1, compose_color(state, color.r, color.g, color.b), bpp);
        return;
    }
    const struct inkcell_rgb ground = decompose_color(state, inkcell_fb_load_pixel(px, bpp));
    const uint32_t blended =
        compose_color(state, inkcell_fb_mix_channel(ground.r, color.r, coverage),
                      inkcell_fb_mix_channel(ground.g, color.g, coverage),
                      inkcell_fb_mix_channel(ground.b, color.b, coverage));
    inkcell_fb_store_span(px, 1, blended, bpp);
}

/*
 * How much of the pixel at (px, py) the rounded rectangle covers, 0..INKCELL_FB_AA_STEPS.
 *
 * The straight parts are answered without sampling - a pixel away from the corners is in or out
 * and nothing in between, because the edges are axis-aligned and land on pixel boundaries. Only
 * a pixel inside one of the four corner squares is sampled, and even then only when the whole
 * pixel is not plainly on one side of the arc.
 */
static int inkcell_fb_rrect_coverage(int px, int py, int x, int y, int w, int h, int radius) {
    if (px < x || py < y || px >= x + w || py >= y + h) {
        return 0;
    }
    if (radius <= 0) {
        return INKCELL_FB_AA_STEPS;
    }

    /* Which corner square, if any, and where in it - measured from the outer corner so that all
       four reduce to the same arithmetic. */
    int i;
    if (py < y + radius) {
        i = py - y;
    } else if (py >= y + h - radius) {
        i = (y + h - 1) - py;
    } else {
        return INKCELL_FB_AA_STEPS; /* between the corner bands: the straight sides */
    }
    int j;
    if (px < x + radius) {
        j = px - x;
    } else if (px >= x + w - radius) {
        j = (x + w - 1) - px;
    } else {
        return INKCELL_FB_AA_STEPS; /* between the corners: the straight top or bottom */
    }

    /* The circle's centre sits at (radius, radius) in the corner square. The pixel's farthest
       point from it is the corner nearest the square's origin, and its nearest point the one
       diagonally opposite. */
    /*
     * Squared in 64 bits, and that is not caution for its own sake: the sample offsets are in
     * eighths of a pixel, so the square of a radius past about 5,800 does not fit in 32 - and
     * the clipping that would have thrown such a shape away happens later, at the blend. A
     * caller handing over a radius larger than the panel is making a legal call with almost
     * nothing to draw, not asking for undefined behaviour.
     */
    const int64_t far_x = radius - j;
    const int64_t far_y = radius - i;
    const int64_t near_x = far_x - 1;
    const int64_t near_y = far_y - 1;
    const int64_t rr = (int64_t)radius * radius;
    if (far_x * far_x + far_y * far_y <= rr) {
        return INKCELL_FB_AA_STEPS;
    }
    if (near_x * near_x + near_y * near_y >= rr) {
        return 0;
    }

    const int64_t centre = (int64_t)INKCELL_FB_AA_FIXED * radius;
    const int64_t limit = centre * centre;
    int covered = 0;
    for (int sy = 0; sy < INKCELL_FB_AA_SUB; ++sy) {
        const int64_t oy = (int64_t)INKCELL_FB_AA_FIXED * i + 2 * sy + 1 - centre;
        for (int sx = 0; sx < INKCELL_FB_AA_SUB; ++sx) {
            const int64_t ox = (int64_t)INKCELL_FB_AA_FIXED * j + 2 * sx + 1 - centre;
            if (ox * ox + oy * oy <= limit) {
                ++covered;
            }
        }
    }
    return covered;
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
static bool inkcell_fb_blit_is_direct(const struct inkcell_draw_state *state) {
    return state->surface.bytes_per_pixel == 4U &&
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
void inkcell_fb_blit_bgra(const struct inkcell_draw_state *state, int x, int y, int w, int h,
                          const uint8_t *pixels, size_t stride) {
    if (state == NULL || pixels == NULL || w <= 0 || h <= 0) {
        return;
    }
    struct inkcell_fb_clipped_box box;
    if (!inkcell_fb_clip_box(state, x, y, w, h, &box)) {
        return;
    }

    const size_t bpp = state->surface.bytes_per_pixel;
    const size_t dst_stride = state->surface.stride;
    const bool direct = inkcell_fb_blit_is_direct(state);
    uint8_t *dst = state->surface.pixels + (size_t)box.y * dst_stride + (size_t)box.x * bpp;
    const uint8_t *src = pixels + (size_t)box.dy * stride + (size_t)box.dx * INKCELL_FB_BGRA_BYTES;

    for (int row = 0; row < box.h; ++row, dst += dst_stride, src += stride) {
        if ((size_t)(dst - state->surface.pixels) + (size_t)box.w * bpp > state->surface.size) {
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

static void inkcell_fb_blend_table(const struct inkcell_draw_state *state, struct inkcell_rgb ink,
                                   struct inkcell_rgb ground,
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
int inkcell_fb_char_adv(const struct inkcell_draw_state *state, int scale) {
    return inkcell_font_advance(inkcell_fb_font(state), scale);
}

int inkcell_fb_cell_adv(const struct inkcell_draw_state *state, uint32_t codepoint, int scale) {
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
int inkcell_fb_text_width(const struct inkcell_draw_state *state, const char *text, int scale) {
    return inkcell_fb_text_width_weight(state, text, scale, INKCELL_WEIGHT_REGULAR);
}

int inkcell_fb_text_width_weight(const struct inkcell_draw_state *state, const char *text,
                                 int scale, enum inkcell_weight weight) {
    const struct inkcell_type_style style = inkcell_type_style_plain(scale, weight);
    return inkcell_fb_text_width_styled(state, text, &style);
}

/*
 * A style resolved against this state's font, once, for the length of one run.
 *
 * Everything a walk over a string needs and nothing it would have to work out per character:
 * which cut of the face to ask, the tracking in pixels, and - where the style asked for tabular
 * figures - the advance every digit is to step. The digit advance is ten table lookups, so
 * resolving it here is the difference between paying for it once a string and once a character.
 *
 * `digit` of 0 means proportional figures, which is the plain style and every role that has not
 * said otherwise.
 */
struct fb_text_run {
    const struct inkcell_font *font;
    int scale;
    int tracking;
    int digit;
};

static struct fb_text_run fb_text_run(const struct inkcell_draw_state *state,
                                      const struct inkcell_type_style *style) {
    struct fb_text_run run = {NULL, 0, 0, 0};
    const struct inkcell_type_style plain = inkcell_type_style_plain(
        state != NULL ? state->scale : INKCELL_SCALE_MIN, INKCELL_WEIGHT_REGULAR);
    if (style == NULL) {
        style = &plain;
    }
    run.font = inkcell_font_at_weight(inkcell_fb_font(state), style->weight);
    run.scale = style->scale;
    run.tracking = inkcell_type_tracking_px(style);
    run.digit = style->tabular ? inkcell_font_digit_advance(run.font, style->scale) : 0;
    return run;
}

/* Whether this cell is one of the ten the tabular advance applies to. A cell rather than a
   codepoint because an emoji sequence may begin with a digit - the keycap emoji are exactly
   that - and those step a sprite's box, not a figure's. */
static bool fb_text_run_is_digit(const struct fb_text_run *run,
                                 const struct inkcell_text_cell *cell) {
    return run->digit > 0 && !cell->is_emoji && cell->codepoint >= (uint32_t)'0' &&
           cell->codepoint <= (uint32_t)'9';
}

/*
 * How far `cell` steps in this run, tracking included.
 *
 * The one place the three cases live: an emoji steps its own square box, a digit in a tabular
 * run steps the common advance, and everything else steps what the face drew it at. Both the
 * measuring walk and the drawing walk call it, which is what keeps a line exactly as wide as it
 * draws - the property every layout in this toolkit is built on.
 */
static int fb_text_run_advance(const struct inkcell_draw_state *state,
                               const struct fb_text_run *run,
                               const struct inkcell_text_cell *cell) {
    int advance;
    if (cell->is_emoji) {
        advance = inkcell_fb_char_adv(state, run->scale);
    } else if (fb_text_run_is_digit(run, cell)) {
        advance = run->digit;
    } else {
        advance = inkcell_font_advance_cp(run->font, cell->codepoint, run->scale);
    }
    advance += run->tracking;
    /* Tracking is allowed to tighten a run and not to close it: a cell that steps nothing is a
       pile of glyphs on one another, and a theme with an absurd tracking should get an ugly
       line rather than an unreadable one. */
    return advance > 0 ? advance : 1;
}

/* Where inside a tabular cell the glyph sits: centred, so a column of readings lines up on its
   digits rather than on its left edge. Zero for everything else, which is drawn at the pen. */
static int fb_text_run_bearing(const struct fb_text_run *run,
                               const struct inkcell_text_cell *cell) {
    if (!fb_text_run_is_digit(run, cell)) {
        return 0;
    }
    const int drawn = inkcell_font_advance_cp(run->font, cell->codepoint, run->scale);
    const int slack = run->digit - drawn;
    return slack > 0 ? slack / 2 : 0;
}

int inkcell_fb_text_width_styled(const struct inkcell_draw_state *state, const char *text,
                                 const struct inkcell_type_style *style) {
    if (text == NULL) {
        return 0;
    }
    const struct fb_text_run run = fb_text_run(state, style);
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
        width += fb_text_run_advance(state, &run, &cell);
        if (width > widest) {
            widest = width;
        }
    }
    /*
     * The last cell's tracking is not part of the line, in either direction. Air after the
     * final letter would put a gap inside the right edge of every box measured this way and
     * pull every centred line a pixel to the left; and where the run is *tight*, the pen has
     * been drawn back past ink that is still on the panel, so the right edge is further out
     * than the pen is. Both are the same correction.
     */
    if (widest > 0) {
        widest -= run.tracking;
    }
    return widest > 0 ? widest : 0;
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
size_t inkcell_fb_text_cols(const struct inkcell_draw_state *state, const char *text, int scale) {
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
    /* Rebuilt from what the metric already resolved rather than from the style, so that a walk
       over a paragraph does not re-answer the same two questions per character. */
    const struct fb_text_run run = {
        .font = inkcell_font_at_weight(inkcell_fb_font(wrap->state), wrap->style.weight),
        .scale = wrap->style.scale,
        .tracking = wrap->tracking_px,
        .digit = wrap->digit_adv,
    };
    return fb_text_run_advance(wrap->state, &run, cell);
}

struct inkcell_wrap_metric inkcell_fb_wrap_metric(struct inkcell_fb_wrap_ctx *ctx,
                                                  const struct inkcell_draw_state *state,
                                                  int scale) {
    const struct inkcell_type_style style = inkcell_type_style_plain(scale, INKCELL_WEIGHT_REGULAR);
    return inkcell_fb_wrap_metric_styled(ctx, state, &style);
}

struct inkcell_wrap_metric inkcell_fb_wrap_metric_styled(struct inkcell_fb_wrap_ctx *ctx,
                                                         const struct inkcell_draw_state *state,
                                                         const struct inkcell_type_style *style) {
    struct inkcell_wrap_metric metric = {NULL, NULL};
    if (ctx == NULL) {
        return metric;
    }
    ctx->state = state;
    ctx->style = style != NULL ? *style
                               : inkcell_type_style_plain(state != NULL ? state->scale : 0,
                                                          INKCELL_WEIGHT_REGULAR);
    ctx->scale = ctx->style.scale;
    const struct fb_text_run run = fb_text_run(state, &ctx->style);
    ctx->tracking_px = run.tracking;
    ctx->digit_adv = run.digit;
    metric.cell = inkcell_fb_wrap_cell;
    metric.ctx = ctx;
    return metric;
}

size_t inkcell_fb_wrap_budget(const struct inkcell_fb_wrap_ctx *ctx, int width) {
    const int tracking = ctx != NULL ? ctx->tracking_px : 0;
    const int budget = width + tracking;
    /* Never nothing: inkcell_wrap_begin_measured() reads a budget of 0 as one unit anyway, and
       a column narrower than a cell is a column that still has to emit something. */
    return budget > 0 ? (size_t)budget : 1U;
}

int inkcell_fb_line_adv(const struct inkcell_draw_state *state, int scale) {
    return inkcell_font_line(inkcell_fb_font(state), scale);
}

int inkcell_fb_line_adv_styled(const struct inkcell_draw_state *state,
                               const struct inkcell_type_style *style) {
    const int scale = style != NULL ? style->scale : (state != NULL ? state->scale : 0);
    /* The font's own line, at the role's line height - which is a percentage rather than a
       pixel count precisely so that this one call is where the two meet. */
    return inkcell_type_line_px(inkcell_font_line(inkcell_fb_font(state), scale), style);
}

int inkcell_fb_cell_adv_styled(const struct inkcell_draw_state *state, uint32_t codepoint,
                               const struct inkcell_type_style *style) {
    const struct fb_text_run run = fb_text_run(state, style);
    const struct inkcell_text_cell cell = {
        .codepoint = codepoint, .bytes = 1U, .is_emoji = false, .sprite = 0U};
    return fb_text_run_advance(state, &run, &cell);
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
#define INKCELL_FB_GLYPH_BOX_MAX                                                                   \
    (INKCELL_GLYPH_MASTER_MAX_WIDTH * INKCELL_SCALE_MAX / (2 * INKCELL_SCALE_UNIT))

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
static void inkcell_fb_draw_glyph_ramp(const struct inkcell_draw_state *state, int x, int y,
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
    const int box_w = (int)font->master_w * scale / (master_px * INKCELL_SCALE_UNIT);
    const int box_h = inkcell_scale_px((int)font->height, scale);
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
    const int left = x - (int)font->master_left * scale / (master_px * INKCELL_SCALE_UNIT);
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

void inkcell_fb_draw_glyph(const struct inkcell_draw_state *state, int x, int y, uint32_t codepoint,
                           int scale, struct inkcell_rgb ink, struct inkcell_rgb ground) {
    uint32_t blend[INKCELL_FB_BLEND_STEPS];
    inkcell_fb_blend_table(state, ink, ground, blend);
    inkcell_fb_draw_glyph_ramp(state, x, y, codepoint, scale, inkcell_fb_font(state), blend);
}

/*
 * The emoji palette, packed into the surface's pixels once.
 *
 * compose_color() depends only on the surface's pixel format, which never changes while a
 * surface is open, so the 255 palette entries are packed on first use and reused. The signature
 * guards the case of a second open with a different format - the tests do exactly that.
 */
static uint64_t inkcell_fb_format_signature(const struct inkcell_draw_state *state) {
    const struct inkcell_pixel_format *fmt = &state->surface.format;
    uint64_t sig = fmt->bits_per_pixel;
    const struct inkcell_channel *fields[4] = {&fmt->r, &fmt->g, &fmt->b, &fmt->a};
    for (size_t i = 0; i < 4U; ++i) {
        sig = sig * 131U + fields[i]->offset;
        sig = sig * 131U + fields[i]->length;
    }
    return sig;
}

#define INKCELL_FB_EMOJI_PALETTE_SLOTS 256

static const uint32_t *inkcell_fb_emoji_palette(const struct inkcell_draw_state *state,
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
void inkcell_fb_draw_emoji_box(const struct inkcell_draw_state *state, int x, int top, int box,
                               uint16_t sprite) {
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
     * tool will render - every emoji keycap drew nothing at all and the focused one drew a bare
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
static void inkcell_fb_draw_emoji(const struct inkcell_draw_state *state, int x, int y,
                                  uint16_t sprite, int scale) {
    const int box = inkcell_fb_char_adv(state, scale);
    inkcell_fb_draw_emoji_box(
        state, x, y + (inkcell_scale_px((int)inkcell_fb_font(state)->height, scale) - box) / 2, box,
        sprite);
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
int inkcell_fb_icon_box(const struct inkcell_draw_state *state, int scale) {
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
int inkcell_fb_icon_drawn(const struct inkcell_draw_state *state, int scale) {
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
 * palette; an icon carries coverage only and is *tinted*, so a chevron on a focused row is the
 * focused row's ink and a warning is the bad tone - it is themed like the text it stands
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
void inkcell_fb_draw_icon(const struct inkcell_draw_state *state, int x, int y,
                          enum inkcell_icon icon, int scale, struct inkcell_rgb ink,
                          struct inkcell_rgb ground) {
    if (!inkcell_icon_is_valid(icon) || scale <= 0 || scale > INKCELL_FB_ICON_SCALE_MAX) {
        return;
    }
    const int box = inkcell_fb_icon_drawn(state, scale);
    /* Centred on the cell in both directions, so it sits on the same optical line as the
       capitals beside it and in the same column the layout above counted. */
    const int left = x - (box - inkcell_fb_icon_box(state, scale)) / 2;
    const int top = y + (inkcell_scale_px((int)inkcell_fb_font(state)->height, scale) - box) / 2;

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
void inkcell_fb_draw_text(const struct inkcell_draw_state *state, int x, int y, const char *text,
                          int scale, struct inkcell_rgb ink, struct inkcell_rgb ground) {
    inkcell_fb_draw_text_weight(state, x, y, text, scale, INKCELL_WEIGHT_REGULAR, ink, ground);
}

void inkcell_fb_draw_text_weight(const struct inkcell_draw_state *state, int x, int y,
                                 const char *text, int scale, enum inkcell_weight weight,
                                 struct inkcell_rgb ink, struct inkcell_rgb ground) {
    const struct inkcell_type_style style = inkcell_type_style_plain(scale, weight);
    inkcell_fb_draw_text_styled(state, x, y, text, &style, ink, ground);
}

void inkcell_fb_draw_text_styled(const struct inkcell_draw_state *state, int x, int y,
                                 const char *text, const struct inkcell_type_style *style,
                                 struct inkcell_rgb ink, struct inkcell_rgb ground) {
    const struct fb_text_run run = fb_text_run(state, style);
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
            inkcell_fb_draw_emoji(state, cursor, y, cell.sprite, run.scale);
            cursor += fb_text_run_advance(state, &run, &cell);
            continue;
        }
        if (cell.codepoint == (uint32_t)'\n') {
            y += inkcell_fb_line_adv_styled(state, style);
            cursor = x;
            continue;
        }
        /* The bearing is zero for everything but a digit in a tabular run, where it is what
           centres the narrower figures in the common cell. */
        inkcell_fb_draw_glyph_ramp(state, cursor + fb_text_run_bearing(&run, &cell), y,
                                   cell.codepoint, run.scale, run.font, blend);
        cursor += fb_text_run_advance(state, &run, &cell);
    }
}

void inkcell_fb_fill_rect(const struct inkcell_draw_state *state, int x, int y, int w, int h,
                          struct inkcell_rgb color) {
    inkcell_fb_fill_packed(state, x, y, w, h, compose_color(state, color.r, color.g, color.b));
}

void inkcell_fb_fill_round_rect_ends(const struct inkcell_draw_state *state, int x, int y, int w,
                                     int h, int radius, struct inkcell_rgb color, bool round_top,
                                     bool round_bottom) {
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
    /* The straight middle, then a row at a time through each corner band. A square end takes its
       own band back into the middle, so the two are one fill rather than a fill and a patch -
       which is what keeps a squared corner from showing a seam where they would have met. */
    const int top_band = round_top ? radius : 0;
    const int bottom_band = round_bottom ? radius : 0;
    inkcell_fb_fill_packed(state, x, y + top_band, w, h - top_band - bottom_band, packed);

    /*
     * The corner bands, skipping rows and columns with no pixel on the panel.
     *
     * The blend clips each pixel anyway, so this changes nothing that is drawn. What it changes
     * is what is *walked*: a shape far larger than the panel has a corner band as big as its
     * own radius, and deciding against every pixel of it one sub-sample at a time is work with
     * no picture at the end. Two comparisons per row and column is what that costs instead.
     */
    const int panel_w = inkcell_fb_panel_width(state);
    const int panel_h = inkcell_fb_panel_height(state);
    const int middle = w - 2 * radius;
    for (int i = 0; i < radius; ++i) {
        const int top_row = y + i;
        const int bottom_row = y + h - 1 - i;
        const bool want_top = round_top && top_row >= 0 && top_row < panel_h;
        const bool want_bottom = round_bottom && bottom_row >= 0 && bottom_row < panel_h;
        if (!want_top && !want_bottom) {
            continue;
        }
        /* Between the two corners the row is solid, and on any real shape it is most of it. */
        if (want_top && middle > 0) {
            inkcell_fb_fill_packed(state, x + radius, top_row, middle, 1, packed);
        }
        if (want_bottom && middle > 0) {
            inkcell_fb_fill_packed(state, x + radius, bottom_row, middle, 1, packed);
        }
        for (int j = 0; j < radius; ++j) {
            const int left_col = x + j;
            const int right_col = x + w - 1 - j;
            const bool want_left = left_col >= 0 && left_col < panel_w;
            const bool want_right = right_col >= 0 && right_col < panel_w;
            if (!want_left && !want_right) {
                continue;
            }
            const int cov = inkcell_fb_rrect_coverage(j, i, 0, 0, 2 * radius, 2 * radius, radius);
            if (cov <= 0) {
                continue;
            }
            /* The other three corners are this one reflected. Reflecting rather than sampling
               them again is not only cheaper: it is what guarantees the two ends of a pill are
               the same shape, rather than the same shape to within a rounding decision taken
               four times. */
            if (want_top) {
                if (want_left) {
                    inkcell_fb_blend_pixel(state, left_col, top_row, color, cov);
                }
                if (want_right) {
                    inkcell_fb_blend_pixel(state, right_col, top_row, color, cov);
                }
            }
            if (want_bottom) {
                if (want_left) {
                    inkcell_fb_blend_pixel(state, left_col, bottom_row, color, cov);
                }
                if (want_right) {
                    inkcell_fb_blend_pixel(state, right_col, bottom_row, color, cov);
                }
            }
        }
    }
}

void inkcell_fb_fill_round_rect(const struct inkcell_draw_state *state, int x, int y, int w, int h,
                                int radius, struct inkcell_rgb color) {
    inkcell_fb_fill_round_rect_ends(state, x, y, w, h, radius, color, true, true);
}

void inkcell_fb_stroke_round_rect(const struct inkcell_draw_state *state, int x, int y, int w,
                                  int h, int radius, int thickness, struct inkcell_rgb color) {
    if (w <= 0 || h <= 0 || thickness <= 0) {
        return;
    }
    const int limit = (w < h ? w : h) / 2;
    if (radius > limit) {
        radius = limit;
    }
    if (radius < 0) {
        radius = 0;
    }
    if (thickness > limit) {
        thickness = limit;
    }

    /*
     * The ring between two rounded rectangles, the inner one inset by the thickness.
     *
     * Subtracting the inner coverage from the outer is exact rather than an approximation,
     * because the inner shape lies wholly inside the outer one: every sub-sample the inner
     * claims is one the outer claims too, so the difference is the count of samples that fall
     * in the ring. That is what makes a hairline a hairline at any radius, instead of the
     * two-fills-one-over-the-other an outline used to be - which needed to know what was behind
     * it, stepped on both edges, and repainted the hole it was meant to leave alone.
     */
    const int inner_x = x + thickness;
    const int inner_y = y + thickness;
    const int inner_w = w - 2 * thickness;
    const int inner_h = h - 2 * thickness;
    const int inner_r = radius > thickness ? radius - thickness : 0;
    const bool hollow = inner_w > 0 && inner_h > 0;

    const uint32_t packed = compose_color(state, color.r, color.g, color.b);
    /* How far in from either side a row can hold anything: the corner square where the arc is,
       or the stroke itself where it is straight. When the two bands meet or overlap - a circle,
       or a stroke as thick as the shape is wide - there is no solid middle and they are one run
       across the row. */
    const int band = radius > thickness ? radius : thickness;
    const bool split = 2 * band < w;
    const int run = split ? band : w;
    const int sides = split ? 2 : 1;

    /* Bounded to the panel for the reason the fill's corner bands are: the blend clips, but a
       ring larger than the panel would still be walked in full. */
    const int panel_w = inkcell_fb_panel_width(state);
    const int panel_h = inkcell_fb_panel_height(state);
    const int row_from = y > 0 ? y : 0;
    const int row_to = (y + h) < panel_h ? (y + h) : panel_h;
    for (int py = row_from; py < row_to; ++py) {
        const bool cross_row = (py < y + thickness) || (py >= y + h - thickness);
        for (int side = 0; side < sides; ++side) {
            const int start = side == 0 ? x : x + w - run;
            /* In 64 bits so that a caller's coordinate near the ends of an int cannot
               overflow on the way to being clamped away. */
            const int64_t first = (int64_t)0 - start;
            const int64_t last = (int64_t)panel_w - start;
            const int col_from = (int)(first > 0 ? first : 0);
            const int col_to = (int)(last < run ? last : run);
            for (int k = col_from; k < col_to; ++k) {
                const int px = start + k;
                const int outer = inkcell_fb_rrect_coverage(px, py, x, y, w, h, radius);
                if (outer <= 0) {
                    continue;
                }
                const int inner = hollow ? inkcell_fb_rrect_coverage(px, py, inner_x, inner_y,
                                                                     inner_w, inner_h, inner_r)
                                         : 0;
                inkcell_fb_blend_pixel(state, px, py, color, outer - inner);
            }
        }
        /* The straight top and bottom, between the bands, where the ring is solid. There is no
           such middle when the bands are one run - the run already covered the row. */
        const int middle = split ? w - 2 * run : 0;
        if (cross_row && middle > 0) {
            inkcell_fb_fill_packed(state, x + run, py, middle, 1, packed);
        }
    }
}

/* ---- arcs ------------------------------------------------------------------------------------
 *
 * sin(i * pi/128) * 1024 for i in 0..64 - a quarter wave, mirrored into the other three. The
 * trailing entry repeats the last so that interpolating at the very end reads a real value
 * rather than off the end.
 *
 * Committed rather than computed at startup, for the reason everything else here is an integer:
 * a table built with the host's libm would put the golden sheet at the mercy of which libm the
 * host has. Linear interpolation between entries is within 0.05% of the true sine, which is a
 * twentieth of a sub-sample at any radius this panel can hold.
 */
static const int16_t k_sine_quarter[66] = {
    0,   25,  50,  75,  100, 125,  150,  175,  200,  224,  249,  273,  297,  321,  345,  369, 392,
    415, 438, 460, 483, 505, 526,  548,  569,  590,  610,  630,  650,  669,  688,  706,  724, 742,
    759, 775, 792, 807, 822, 837,  851,  865,  878,  891,  903,  915,  926,  936,  946,  955, 964,
    972, 980, 987, 993, 999, 1004, 1009, 1013, 1016, 1019, 1021, 1023, 1024, 1024, 1024,
};

/* sin(p * pi/512) * 1024, p in 0..256. */
static int32_t inkcell_fb_quarter_sin(int32_t p) {
    const int32_t index = p >> 2;
    const int32_t frac = p & 3;
    const int32_t a = k_sine_quarter[index];
    const int32_t b = k_sine_quarter[index + 1];
    return a + ((b - a) * frac) / 4;
}

/*
 * A turn reduced to 0..999.
 *
 * Called before anything is *added* to a caller's turn, which is the order that matters: a
 * quarter turn for the cosine and a sweep for the far end of an arc are both added to it, and
 * a turn is an int32_t a caller may legitimately fill - a spinner driven straight off the
 * monotonic clock reaches the top of the range in about twenty-five days of uptime. Normalising
 * afterwards is normalising a number that has already overflowed.
 */
static int32_t inkcell_fb_wrap_turn(int32_t turn) {
    int32_t wrapped = turn % 1000;
    if (wrapped < 0) {
        wrapped += 1000;
    }
    return wrapped;
}

/* sin of `turn`, stated in permille of a whole circle, scaled by 1024. */
static int32_t inkcell_fb_sin_turn(int32_t turn) {
    const int32_t wrapped = inkcell_fb_wrap_turn(turn);
    const int32_t angle = ((wrapped * 1024 + 500) / 1000) & 1023;
    const int32_t p = angle & 255;
    switch (angle >> 8) {
    case 0:
        return inkcell_fb_quarter_sin(p);
    case 1:
        return inkcell_fb_quarter_sin(256 - p);
    case 2:
        return -inkcell_fb_quarter_sin(p);
    default:
        return -inkcell_fb_quarter_sin(256 - p);
    }
}

/*
 * Where a turn points, on the panel's axes.
 *
 * Zero is twelve o'clock and a turn runs clockwise, because that is what every progress ring
 * and every clock face does. `y` grows downwards here, so "up" is negative and the cosine is
 * negated rather than the sine - which also makes a positive cross product mean "clockwise
 * from", the test the sweep below is built on.
 */
static void inkcell_fb_turn_vector(int32_t turn, int32_t *vx, int32_t *vy) {
    const int32_t wrapped = inkcell_fb_wrap_turn(turn);
    *vx = inkcell_fb_sin_turn(wrapped);
    *vy = -inkcell_fb_sin_turn(wrapped + 250);
}

void inkcell_fb_stroke_arc(const struct inkcell_draw_state *state, int cx, int cy, int radius,
                           int thickness, int32_t start, int32_t sweep, struct inkcell_rgb color) {
    if (radius <= 0 || thickness <= 0 || sweep <= 0) {
        return;
    }
    if (thickness > radius) {
        thickness = radius;
    }
    if (sweep > 1000) {
        sweep = 1000;
    }

    const int inner = radius - thickness;
    /* In 64 bits, for the reason inkcell_fb_rrect_coverage() is: eight sub-pixel units squared
       leaves a 32-bit int at a radius of about 5,800, and a radius past the panel is a legal
       call whose pixels the blend clips, not an invalid one. */
    const int64_t outer_sq = (int64_t)INKCELL_FB_AA_FIXED * radius;
    const int64_t inner_sq = (int64_t)INKCELL_FB_AA_FIXED * inner;
    const int64_t outer_limit = outer_sq * outer_sq;
    const int64_t inner_limit = inner_sq * inner_sq;
    const int64_t radius_sq = (int64_t)radius * radius;
    const int64_t inner_radius_sq = (int64_t)inner * inner;

    const bool whole = (sweep >= 1000);
    const bool major = (sweep > 500);
    int32_t sx = 0;
    int32_t sy = 0;
    int32_t ex = 0;
    int32_t ey = 0;
    /* Normalised before the sweep is added to it, or a turn near the top of the range overflows
       on the way to the far end of the arc. */
    const int32_t from = inkcell_fb_wrap_turn(start);
    inkcell_fb_turn_vector(from, &sx, &sy);
    inkcell_fb_turn_vector(from + sweep, &ex, &ey);

    /*
     * Only the part of the ring that is on the panel.
     *
     * The blend clips every pixel anyway, so this changes nothing that is drawn - but a ring
     * larger than the panel would otherwise be *walked* in full, and a radius of sixty thousand
     * is fourteen billion pixels to decide against. Bounded here, a clipped arc costs what is
     * visible of it.
     */
    const int64_t left = (int64_t)cx - radius;
    const int64_t right = (int64_t)cx + radius;
    const int64_t top = (int64_t)cy - radius;
    const int64_t bottom = (int64_t)cy + radius;
    const int x0 = (int)(left > 0 ? left : 0);
    const int x1 = (int)(right < (int64_t)inkcell_fb_panel_width(state)
                             ? right
                             : (int64_t)inkcell_fb_panel_width(state));
    const int y0 = (int)(top > 0 ? top : 0);
    const int y1 = (int)(bottom < (int64_t)inkcell_fb_panel_height(state)
                             ? bottom
                             : (int64_t)inkcell_fb_panel_height(state));

    for (int py = y0; py < y1; ++py) {
        for (int px = x0; px < x1; ++px) {
            /*
             * The cheap rejects first, against the pixel's nearest and farthest corners: most
             * of this box is the hole in the middle, and a ring only ever has about its own
             * circumference worth of edge pixels to sample.
             */
            const int64_t dx_near = px >= cx ? px - cx : (cx - 1 - px);
            const int64_t dy_near = py >= cy ? py - cy : (cy - 1 - py);
            const int64_t dx_far = dx_near + 1;
            const int64_t dy_far = dy_near + 1;
            if (dx_near * dx_near + dy_near * dy_near >= radius_sq ||
                dx_far * dx_far + dy_far * dy_far <= inner_radius_sq) {
                continue;
            }

            int covered = 0;
            for (int ssy = 0; ssy < INKCELL_FB_AA_SUB; ++ssy) {
                const int64_t oy = (int64_t)INKCELL_FB_AA_FIXED * (py - cy) + 2 * ssy + 1;
                for (int ssx = 0; ssx < INKCELL_FB_AA_SUB; ++ssx) {
                    const int64_t ox = (int64_t)INKCELL_FB_AA_FIXED * (px - cx) + 2 * ssx + 1;
                    const int64_t d = ox * ox + oy * oy;
                    if (d > outer_limit || d < inner_limit) {
                        continue;
                    }
                    if (!whole) {
                        /* Inside the wedge: clockwise of the start ray and anticlockwise of the
                           end one. A sweep past the half turn is the same test on the wedge it
                           leaves behind, negated - the two rays cannot bound the larger side
                           directly. */
                        const int64_t from_start = (int64_t)sx * oy - (int64_t)sy * ox;
                        const int64_t to_end = ox * (int64_t)ey - oy * (int64_t)ex;
                        const bool within = major ? !(((int64_t)ex * oy - (int64_t)ey * ox) > 0 &&
                                                      (ox * (int64_t)sy - oy * (int64_t)sx) > 0)
                                                  : (from_start >= 0 && to_end >= 0);
                        if (!within) {
                            continue;
                        }
                    }
                    ++covered;
                }
            }
            inkcell_fb_blend_pixel(state, px, py, color, covered);
        }
    }
}

void inkcell_fb_clear(const struct inkcell_draw_state *state, struct inkcell_rgb color) {
    inkcell_fb_fill_rect(state, 0, 0, inkcell_fb_panel_width(state), inkcell_fb_panel_height(state),
                         color);
}

/*
 * ---- the scrim --------------------------------------------------------------------------------
 *
 * What is already on the panel, moved a fraction of the way towards one colour. See
 * inkcell_fb_scrim_rect() in the header for what it is for.
 *
 * Two things about the implementation are worth stating, because both are the difference
 * between a scrim that is free and one that costs a frame.
 *
 * **It clips itself, once, rather than per pixel.** inkcell_fb_blend_pixel() above deliberately
 * clips each pixel because the shapes that reach it touch a few dozen; this touches every pixel
 * of a region the size of the body, and clipping each of those separately is the whole of the
 * cost. So the box goes through inkcell_fb_clip_box() once and the loop runs inside the answer.
 *
 * **It remembers the last pixel it converted.** A round trip through decompose_color() and
 * compose_color() is a dozen shifts and multiplies, and the region under a dialog is mostly
 * flat: a body is a ground with rows of the same fill on it, so the same packed value arrives
 * thousands of times in a row. One slot of memo turns almost all of them into a compare. It is
 * one slot rather than a table because the runs are *contiguous* - what makes a body cheap is
 * not that it has few colours but that it changes colour rarely - and a table would be a cache
 * that has to be sized and invalidated for no further gain.
 */
void inkcell_fb_scrim_rect(const struct inkcell_draw_state *state, struct inkcell_fb_rect box,
                           struct inkcell_rgb color, int percent) {
    if (state == NULL || percent <= 0 || box.w <= 0 || box.h <= 0) {
        return;
    }
    if (percent > 100) {
        percent = 100;
    }
    struct inkcell_fb_clipped_box clipped;
    if (!inkcell_fb_clip_box(state, box.x, box.y, box.w, box.h, &clipped)) {
        return;
    }

    const size_t bpp = state->surface.bytes_per_pixel;
    const size_t stride = state->surface.stride;
    /* The mix is stated in AA steps rather than in percent so it runs through the same
       inkcell_fb_mix_channel() every anti-aliased edge does - one rounding rule for the whole
       backend, rather than a second one that disagrees at the halves. */
    const int coverage = (percent * INKCELL_FB_AA_STEPS + 50) / 100;
    bool memo_valid = false;
    uint32_t memo_in = 0U;
    uint32_t memo_out = 0U;

    uint8_t *row = state->surface.pixels + (size_t)clipped.y * stride + (size_t)clipped.x * bpp;
    for (int r = 0; r < clipped.h; ++r, row += stride) {
        if ((size_t)(row - state->surface.pixels) + (size_t)clipped.w * bpp > state->surface.size) {
            return;
        }
        uint8_t *px = row;
        for (int c = 0; c < clipped.w; ++c, px += bpp) {
            const uint32_t packed = inkcell_fb_load_pixel(px, bpp);
            if (memo_valid && packed == memo_in) {
                inkcell_fb_store_span(px, 1, memo_out, bpp);
                continue;
            }
            const struct inkcell_rgb ground = decompose_color(state, packed);
            const uint32_t mixed =
                compose_color(state, inkcell_fb_mix_channel(ground.r, color.r, coverage),
                              inkcell_fb_mix_channel(ground.g, color.g, coverage),
                              inkcell_fb_mix_channel(ground.b, color.b, coverage));
            inkcell_fb_store_span(px, 1, mixed, bpp);
            memo_in = packed;
            memo_out = mixed;
            memo_valid = true;
        }
    }
}

/*
 * ---- the shadow -------------------------------------------------------------------------------
 *
 * The scrim's read-modify-write with a shape to it: what is on the panel around a box, moved
 * towards INKCELL_COLOR_SHADOW by an amount that falls off with distance from the box's edge.
 * See inkcell_fb_draw_shadow() in the header for what it is for and the contract it holds its
 * callers to.
 *
 * The falloff is a smoothstep across `blur`, centred on the edge of the box *after* it has been
 * moved down by `offset`. Centred rather than starting at the edge, because that is what a soft
 * shadow is: half in and half out of the silhouette that casts it. A falloff that began at the
 * edge at full strength would put a hard dark band `offset` pixels tall under every box, which
 * reads as a border that slipped rather than as depth. Smoothstep rather than linear because a
 * linear ramp has a visible corner where it meets the ground and a smoothstep does not - it
 * arrives flat at both ends, which is the one property of a gaussian worth paying for.
 *
 * Everything is integer, in sixteenths of a pixel, for the reason the anti-aliasing is: a golden
 * digest that depended on a float's rounding would pass on one machine and fail on another.
 */

/* Floor of the square root, for the one place a distance needs one: the rounded corners. */
static int64_t inkcell_fb_isqrt(int64_t v) {
    if (v <= 0) {
        return 0;
    }
    int64_t root = 0;
    int64_t bit = (int64_t)1 << 62;
    while (bit > v) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (v >= root + bit) {
            v -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return root;
}

/* Sixteenths of a pixel per pixel: the resolution of the falloff. */
#define INKCELL_FB_SHADOW_Q 16
/* The falloff's own resolution: 0 is none of the shadow, this is all of it. Finer than the
   anti-aliasing's sixteen steps on purpose - an edge is a pixel wide and sixteen levels across
   one pixel are invisible, but a shadow is a ramp sixteen pixels long and sixteen levels across
   that is a staircase. */
#define INKCELL_FB_SHADOW_ONE 256

/*
 * Signed distance, in sixteenths, from the centre of pixel (px, py) to the rounded rectangle
 * whose half-extents are (hx, hy) about (cx, cy) - all in sixteenths too. Negative inside.
 *
 * The usual formulation: fold the point into one quadrant, measure it against the corner circle
 * if it is past both straight edges, and against the nearer straight edge otherwise. Only the
 * first case needs a root, and it is the minority of pixels a shadow touches.
 */
static int inkcell_fb_round_rect_distance(int px, int py, int cx, int cy, int hx, int hy,
                                          int radius) {
    const int dx = px * INKCELL_FB_SHADOW_Q + INKCELL_FB_SHADOW_Q / 2 - cx;
    const int dy = py * INKCELL_FB_SHADOW_Q + INKCELL_FB_SHADOW_Q / 2 - cy;
    const int qx = (dx < 0 ? -dx : dx) - (hx - radius);
    const int qy = (dy < 0 ? -dy : dy) - (hy - radius);
    if (qx > 0 && qy > 0) {
        return (int)inkcell_fb_isqrt((int64_t)qx * qx + (int64_t)qy * qy) - radius;
    }
    return (qx > qy ? qx : qy) - radius;
}

/* One channel moved `amount` of INKCELL_FB_SHADOW_ONE towards `to`, rounded to nearest. */
static inline uint8_t inkcell_fb_shadow_channel(uint8_t from, uint8_t to, int amount) {
    const int span = (int)to - (int)from;
    const int moved = span * amount;
    return (uint8_t)((int)from + (moved + (moved >= 0 ? INKCELL_FB_SHADOW_ONE / 2
                                                      : -(INKCELL_FB_SHADOW_ONE / 2))) /
                                     INKCELL_FB_SHADOW_ONE);
}

struct inkcell_fb_rect inkcell_fb_shadow_bounds(const struct inkcell_draw_state *state,
                                                struct inkcell_fb_rect box,
                                                enum inkcell_elevation elevation) {
    const struct inkcell_shadow_px shadow = inkcell_theme_shadow(
        state != NULL ? state->theme : NULL, elevation, state != NULL ? state->scale : 0);
    if (shadow.depth <= 0 || box.w <= 0 || box.h <= 0) {
        return box;
    }
    /* Half the blur either side of the moved edge, rounded up so the last faint pixel is
       inside what a caller declares as damage. */
    const int half = (shadow.blur + 1) / 2;
    const int top = box.y + shadow.offset - half;
    const int bottom = box.y + box.h + shadow.offset + half;
    struct inkcell_fb_rect out = {
        .x = box.x - half,
        .y = top < box.y ? top : box.y,
        .w = box.w + 2 * half,
        .h = 0,
    };
    out.h = (bottom > box.y + box.h ? bottom : box.y + box.h) - out.y;
    return out;
}

void inkcell_fb_draw_shadow(const struct inkcell_draw_state *state, struct inkcell_fb_rect box,
                            int radius, enum inkcell_elevation elevation, int32_t progress) {
    if (state == NULL || box.w <= 0 || box.h <= 0 || progress <= 0) {
        return;
    }
    const struct inkcell_shadow_px shadow =
        inkcell_theme_shadow(state->theme, elevation, state->scale);
    if (shadow.depth <= 0) {
        return;
    }
    if (progress > INKCELL_ANIM_ONE) {
        progress = INKCELL_ANIM_ONE;
    }
    /* The whole shadow's strength at its darkest, in falloff units, eased in with the thing
       casting it: a dialog half arrived casts half a shadow, so the two are one arrival. */
    const int peak = (int)(((int64_t)shadow.depth * INKCELL_FB_SHADOW_ONE * progress) /
                           ((int64_t)100 * INKCELL_ANIM_ONE));
    if (peak <= 0) {
        return;
    }

    const int short_side = box.w < box.h ? box.w : box.h;
    if (radius < 0) {
        radius = 0;
    }
    if (radius > short_side / 2) {
        radius = short_side / 2;
    }
    const struct inkcell_fb_rect bounds = inkcell_fb_shadow_bounds(state, box, elevation);

    struct inkcell_fb_clipped_box clipped;
    if (!inkcell_fb_clip_box(state, bounds.x, bounds.y, bounds.w, bounds.h, &clipped)) {
        return;
    }
    /*
     * The band, always - including where something declared this region as animating.
     *
     * Every other primitive is let through the band inside declared damage, because what it
     * puts down is the same colour however many times it is put down. This one is not: it
     * darkens what is there, so a pixel it touches twice without a repaint between is a pixel
     * a step darker, and a shadow over rows nothing repainted would deepen every frame. Inside
     * the band the ground has been drawn again before this runs; outside it, what is on the
     * panel is last frame's shadow already, and leaving it alone is correct.
     *
     * The overlay layer is the caller this costs anything, and it costs a frame: the span a
     * layer records is carried into the next frame's band (see inkcell_fb_app_frame_begin()),
     * so the edge of a shadow that has travelled past it arrives one frame late.
     */
    if (state->clip_active) {
        const int right =
            clipped.x + clipped.w < state->clip.right ? clipped.x + clipped.w : state->clip.right;
        const int bottom =
            clipped.y + clipped.h < state->clip.bottom ? clipped.y + clipped.h : state->clip.bottom;
        if (clipped.x < state->clip.x) {
            clipped.dx += state->clip.x - clipped.x;
            clipped.x = state->clip.x;
        }
        if (clipped.y < state->clip.y) {
            clipped.dy += state->clip.y - clipped.y;
            clipped.y = state->clip.y;
        }
        clipped.w = right - clipped.x;
        clipped.h = bottom - clipped.y;
        if (clipped.w <= 0 || clipped.h <= 0) {
            return;
        }
    }

    /* The casting shape in sixteenths, moved down by the offset. */
    const int hx = box.w * INKCELL_FB_SHADOW_Q / 2;
    const int hy = box.h * INKCELL_FB_SHADOW_Q / 2;
    const int cx = box.x * INKCELL_FB_SHADOW_Q + hx;
    const int cy = (box.y + shadow.offset) * INKCELL_FB_SHADOW_Q + hy;
    const int r_q = radius * INKCELL_FB_SHADOW_Q;
    const int span = shadow.blur * INKCELL_FB_SHADOW_Q;
    const struct inkcell_rgb color = inkcell_fb_color(state, INKCELL_COLOR_SHADOW);

    const size_t bpp = state->surface.bytes_per_pixel;
    const size_t stride = state->surface.stride;
    uint8_t *row = state->surface.pixels + (size_t)clipped.y * stride + (size_t)clipped.x * bpp;
    for (int r = 0; r < clipped.h; ++r, row += stride) {
        if ((size_t)(row - state->surface.pixels) + (size_t)clipped.w * bpp > state->surface.size) {
            return;
        }
        /* Where this row is in the caller's coordinates, which is what the shape is stated in. */
        const int y = bounds.y + clipped.dy + r;
        /*
         * The part of the row the box itself is about to cover, skipped. Only on the rows clear
         * of its corners, where "inside the box" is a plain span; a corner row is measured all
         * the way across, because the curve leaves pixels at its ends that the fill will not.
         * This is most of the region on anything the size of a dialog, and it is why a shadow
         * costs its perimeter rather than its area.
         */
        const bool plain = y >= box.y + radius && y < box.y + box.h - radius;
        uint8_t *px = row;
        for (int c = 0; c < clipped.w; ++c, px += bpp) {
            const int x = bounds.x + clipped.dx + c;
            if (plain && x >= box.x && x < box.x + box.w) {
                const int skip = box.x + box.w - x;
                c += skip - 1;
                px += (size_t)(skip - 1) * bpp;
                continue;
            }
            const int d = inkcell_fb_round_rect_distance(x, y, cx, cy, hx, hy, r_q);
            /* 0 at half the blur inside the edge, `span` at half the blur outside it. */
            const int u = d + span / 2;
            if (u >= span) {
                continue;
            }
            int falloff = INKCELL_FB_SHADOW_ONE;
            if (u > 0) {
                const int t = (u * INKCELL_FB_SHADOW_ONE) / span;
                const int smooth = (t * t * (3 * INKCELL_FB_SHADOW_ONE - 2 * t)) /
                                   (INKCELL_FB_SHADOW_ONE * INKCELL_FB_SHADOW_ONE);
                falloff = INKCELL_FB_SHADOW_ONE - smooth;
            }
            const int amount = (peak * falloff) / INKCELL_FB_SHADOW_ONE;
            if (amount <= 0) {
                continue;
            }
            const struct inkcell_rgb ground =
                decompose_color(state, inkcell_fb_load_pixel(px, bpp));
            inkcell_fb_store_span(
                px, 1,
                compose_color(state, inkcell_fb_shadow_channel(ground.r, color.r, amount),
                              inkcell_fb_shadow_channel(ground.g, color.g, amount),
                              inkcell_fb_shadow_channel(ground.b, color.b, amount)),
                bpp);
        }
    }
}

/* Columns of text that fit between the margins at this scale. */
size_t inkcell_fb_cols(const struct inkcell_draw_state *state, int scale) {
    /* The region inset by its margin, deliberately, and not the content column: this is what a
       width class is *taken from* (see inkcell_fb_width_class), and a column that is itself
       capped by the class would be a circle. What a screen laying text out wants is
       inkcell_fb_row_cols(), which is measured inside the column. */
    const int usable = inkcell_fb_region(state).w - 2 * inkcell_fb_margin(state);
    if (usable <= 0) {
        return 1U;
    }
    return (size_t)(usable / inkcell_fb_char_adv(state, scale));
}

enum inkcell_width_class inkcell_fb_width_class(const struct inkcell_draw_state *state) {
    return inkcell_width_class_of(inkcell_fb_cols(state, state == NULL ? 1 : state->scale));
}

int inkcell_fb_measure_width(const struct inkcell_draw_state *state, size_t cols) {
    if (state == NULL || cols == 0U) {
        return 0;
    }
    return inkcell_fb_char_adv(state, state->scale) * (int)cols;
}

size_t inkcell_fb_row_cols(const struct inkcell_draw_state *state, int scale) {
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

static bool inkcell_fb_theme_focus_fill(const struct inkcell_draw_state *state) {
    const struct inkcell_theme *theme =
        (state != NULL && state->theme != NULL) ? state->theme : inkcell_theme_default();
    return theme != NULL && theme->focus_fill;
}

struct inkcell_rgb inkcell_fb_focus_fill(const struct inkcell_draw_state *state,
                                         enum inkcell_color ground) {
    if (inkcell_fb_theme_focus_fill(state)) {
        return inkcell_fb_color(state, INKCELL_COLOR_SURFACE_SEL);
    }
    return inkcell_fb_state_layer(state, ground, INKCELL_COLOR_TEXT, INKCELL_STATE_FOCUSED);
}

struct inkcell_rgb inkcell_fb_focus_ink(const struct inkcell_draw_state *state,
                                        enum inkcell_tone tone, bool quiet) {
    if (!inkcell_fb_theme_focus_fill(state)) {
        return inkcell_fb_tone_color(state, tone);
    }
    return inkcell_fb_color(state,
                            quiet ? INKCELL_COLOR_TEXT_ON_SEL_DIM : INKCELL_COLOR_TEXT_ON_SEL);
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
struct inkcell_rgb inkcell_fb_draw_row_fill_on(const struct inkcell_draw_state *state, int y,
                                               uint32_t rows, bool focused,
                                               enum inkcell_color ground) {
    if (!focused) {
        /* Nothing is painted: the row is already standing on whatever laid that colour down -
           the panel, or the card surface a grouped list drew before any row was placed. What is
           returned is what the text will be blended against, which is the only thing a caller
           wanted from a row the cursor is not on. */
        return inkcell_fb_color(state, ground);
    }
    const struct inkcell_rgb fill = inkcell_fb_focus_fill(state, ground);
    const int line = inkcell_fb_line_adv(state, state->scale);
    const struct inkcell_fb_row_box box = inkcell_fb_row_box(state);
    inkcell_fb_fill_round_rect(state, box.x, y - inkcell_step_px(state->scale), box.w,
                               (int)(rows > 0U ? rows : 1U) * line,
                               inkcell_fb_radius(state, INKCELL_SHAPE_SM), fill);
    return fill;
}

struct inkcell_rgb inkcell_fb_draw_row_fill(const struct inkcell_draw_state *state, int y,
                                            uint32_t rows, bool focused) {
    return inkcell_fb_draw_row_fill_on(state, y, rows, focused, INKCELL_COLOR_BG);
}

/* One list row of text, lifted when it is the cursor: the fill above, then the words. */
void inkcell_fb_draw_row(const struct inkcell_draw_state *state, int y, const char *text,
                         struct inkcell_rgb color, bool focused) {
    const struct inkcell_rgb ground = inkcell_fb_draw_row_fill(state, y, 1U, focused);
    if (focused && inkcell_fb_theme_focus_fill(state)) {
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
    const uint32_t now = inkwell_time_wall_s();
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
int inkcell_fb_draw_wrapped_at(const struct inkcell_draw_state *state, int x, int y,
                               const char *text, size_t width, int max_lines,
                               struct inkcell_rgb color, struct inkcell_rgb ground) {
    const struct inkcell_type_style style =
        inkcell_type_style_plain(state->scale, INKCELL_WEIGHT_REGULAR);
    return inkcell_fb_draw_wrapped_styled(state, x, y, text, width, max_lines, &style, color,
                                          ground);
}

int inkcell_fb_draw_wrapped_styled(const struct inkcell_draw_state *state, int x, int y,
                                   const char *text, size_t width, int max_lines,
                                   const struct inkcell_type_style *style, struct inkcell_rgb color,
                                   struct inkcell_rgb ground) {
    struct inkcell_fb_wrap_ctx wctx;
    const struct inkcell_wrap_metric metric = inkcell_fb_wrap_metric_styled(&wctx, state, style);
    struct inkcell_wrap wrap;
    inkcell_wrap_begin_measured(&wrap, text, inkcell_fb_wrap_budget(&wctx, (int)width), &metric);

    /* The role's line, not the font's: a paragraph is the one place a line height is visible at
       all, and a supporting body that loosened its leading did so for exactly this call. */
    const int line = inkcell_fb_line_adv_styled(state, style);
    int lines = 0;
    while (lines < max_lines && inkcell_wrap_next(&wrap)) {
        inkcell_fb_draw_text_styled(state, x, y, wrap.line, style, color, ground);
        y += line;
        ++lines;
    }
    return lines;
}

/* The same, from the body's left margin - which is where a screen's own wrapped text starts. */
int inkcell_fb_draw_wrapped_centered(const struct inkcell_draw_state *state, int y,
                                     const char *text, size_t width, int max_lines,
                                     struct inkcell_rgb color, struct inkcell_rgb ground) {
    struct inkcell_fb_wrap_ctx wctx;
    const struct inkcell_wrap_metric metric = inkcell_fb_wrap_metric(&wctx, state, state->scale);
    struct inkcell_wrap wrap;
    inkcell_wrap_begin_measured(&wrap, text, width, &metric);

    /* The band the lines are centred in, rather than the panel: a caller that wrapped to a
       narrower width meant that width, and centring on the panel would hang the text off the
       side of the column it was measured for. */
    const int left = inkcell_fb_content_x(state);
    int lines = 0;
    while (lines < max_lines && inkcell_wrap_next(&wrap)) {
        /*
         * Each line on its own measurement, not the block on the widest.
         *
         * A ragged paragraph centred as a block is a block with one edge straight, which is
         * the thing that reads as a mistake; and measuring is the only way to know how wide a
         * line is on a proportional face, where two lines of the same character count are two
         * different widths.
         */
        const int line_w = inkcell_fb_text_width(state, wrap.line, state->scale);
        const int x = line_w < (int)width ? left + ((int)width - line_w) / 2 : left;
        inkcell_fb_draw_text(state, x, y, wrap.line, state->scale, color, ground);
        y += inkcell_fb_line_adv(state, state->scale);
        ++lines;
    }
    return lines;
}

int inkcell_fb_draw_wrapped(const struct inkcell_draw_state *state, int y, const char *text,
                            size_t width, int max_lines, struct inkcell_rgb color,
                            struct inkcell_rgb ground) {
    return inkcell_fb_draw_wrapped_at(state, inkcell_fb_margin(state), y, text, width, max_lines,
                                      color, ground);
}

uint32_t inkcell_fb_wrapped_lines(const struct inkcell_draw_state *state, const char *text,
                                  size_t width, int scale) {
    const struct inkcell_type_style style = inkcell_type_style_plain(scale, INKCELL_WEIGHT_REGULAR);
    return inkcell_fb_wrapped_lines_styled(state, text, width, &style);
}

uint32_t inkcell_fb_wrapped_lines_styled(const struct inkcell_draw_state *state, const char *text,
                                         size_t width, const struct inkcell_type_style *style) {
    struct inkcell_fb_wrap_ctx wctx;
    const struct inkcell_wrap_metric metric = inkcell_fb_wrap_metric_styled(&wctx, state, style);
    return inkcell_wrap_lines_measured(text, inkcell_fb_wrap_budget(&wctx, (int)width), &metric);
}
