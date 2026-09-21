#define _POSIX_C_SOURCE 200809L

/*
 * The layer: the box, the travel, the scrim and the stack.
 *
 * Nothing here knows what is in an overlay. That is the whole shape of the thing - see
 * include/inkcell/ui/overlay.h - and it is why this file is in src/fb/ beside the components
 * without being one: it is the machinery four widgets each had a private copy of, and the
 * reason the fifth and sixth could not be written.
 */

#include "inkcell/ui/overlay.h"

#include "inkcell/ui/anim.h"
#include "inkcell/ui/theme.h"

#include <string.h>

/*
 * How long a layer takes to arrive, and how long to leave.
 *
 * Not the same token, and the asymmetry is the point: arriving is the part that has to be
 * seen, leaving is the part that has to be out of the way. It is the shape of every platform's
 * overlay motion, and it is the pair the snackbar already used - lifted up here so that every
 * layer takes it rather than each one deciding again.
 */
#define INKCELL_OVERLAY_IN_MOTION INKCELL_MOTION_MEDIUM
#define INKCELL_OVERLAY_OUT_MOTION INKCELL_MOTION_SHORT

/*
 * How far a INKCELL_OVERLAY_TRAVEL_NEAR layer moves: a sixteenth of the panel's height.
 *
 * A fraction of the panel rather than of the layer, so a tall sheet and a short menu travel
 * the same distance and read as one system - a distance proportional to the thing moving would
 * make the big overlay the slow one, which is backwards. A sixteenth is a little over forty
 * pixels on the Brick's panel: far enough that the eye catches the arrival, near enough that
 * nothing reads as sliding past.
 */
#define INKCELL_OVERLAY_NEAR_NUM 1
#define INKCELL_OVERLAY_NEAR_DEN 16

/* The gap between an anchored layer and the thing it hangs off: the half-margin every panel
   stands off an edge by, for inkcell_fb_gutter()'s reason - what it measures is clearance
   between two surfaces, not room around text. */
static int inkcell_overlay_anchor_gap(const struct inkcell_draw_state *state) {
    return inkcell_fb_gutter(state);
}

/* The region a layer belongs to: what it said, or the whole panel when it said nothing. */
static struct inkcell_fb_rect inkcell_overlay_bounds(const struct inkcell_draw_state *state,
                                                     const struct inkcell_overlay *overlay) {
    if (overlay->bounds.w > 0 && overlay->bounds.h > 0) {
        return overlay->bounds;
    }
    return (struct inkcell_fb_rect){0, 0, inkcell_fb_panel_width(state),
                                    inkcell_fb_panel_height(state)};
}

/* The slot holding `id`, or NULL. */
static struct inkcell_fb_overlay_slot *inkcell_overlay_find(struct inkcell_draw_state *state,
                                                            uint32_t id) {
    for (uint32_t i = 0U; i < INKCELL_OVERLAY_SLOTS; ++i) {
        if (state->overlays[i].id == id) {
            return &state->overlays[i];
        }
    }
    return NULL;
}

/*
 * The slot holding `id`, taking a free one on first sight.
 *
 * No eviction, unlike the animation table one header over, and the difference is what the two
 * are for. A table entry is where a *widget* left a scalar, and the correct failure there is
 * that the control off screen longest forgets a slide nobody is watching. A layer is a thing
 * the reader is looking at and pressing buttons against; quietly taking its slot away would
 * leave a panel on screen with no way to dismiss it. So a fifth layer is refused, and the
 * screen that wanted one finds out by its overlay not appearing rather than by another one
 * vanishing.
 */
static struct inkcell_fb_overlay_slot *inkcell_overlay_slot(struct inkcell_draw_state *state,
                                                            uint32_t id) {
    struct inkcell_fb_overlay_slot *slot = inkcell_overlay_find(state, id);
    if (slot != NULL) {
        return slot;
    }
    for (uint32_t i = 0U; i < INKCELL_OVERLAY_SLOTS; ++i) {
        if (state->overlays[i].id == INKCELL_OVERLAY_NONE) {
            memset(&state->overlays[i], 0, sizeof state->overlays[i]);
            state->overlays[i].id = id;
            return &state->overlays[i];
        }
    }
    return NULL;
}

/* One coordinate, `progress` of the way from `from` to `to`. In 64 bits for the focus ring's
   reason: a panel coordinate times INKCELL_ANIM_ONE leaves an int very little room. */
static int inkcell_overlay_between(int from, int to, int32_t progress) {
    const int64_t span = (int64_t)to - (int64_t)from;
    return (int)((int64_t)from + (span * progress) / INKCELL_ANIM_ONE);
}

/*
 * Where the layer rests, given its placement and the room it has.
 *
 * Fitted to the bounds rather than clipped by them: a panel cut off at the edge of the screen
 * looks broken, and one that came out a little smaller than asked for looks like a panel. The
 * same reasoning the dialog already applies to its own buttons, applied to the whole box.
 */
static struct inkcell_fb_rect inkcell_overlay_rest(const struct inkcell_draw_state *state,
                                                   const struct inkcell_overlay *overlay,
                                                   struct inkcell_fb_rect bounds) {
    const int gutter = inkcell_fb_gutter(state);
    /*
     * The size is the caller's and only the region is a limit.
     *
     * The obvious alternative - inset everything by a gutter so no layer ever touches an edge
     * - is wrong for the one shape that most wants an edge. A bottom sheet is full bleed and
     * square across the bottom *because* it came up off the edge of the screen; inset by a
     * gutter it is a floating card that happens to be low down, which is a different object.
     *
     * So a caller that wants a floating panel asks for a size smaller than its region - the
     * dialog asks for `xres - 2 * gutter` - and a caller that wants a full-bleed one asks for
     * the region. The layer places what it is given; it does not have a second opinion about
     * how big things should be.
     */
    int w = overlay->w > 0 ? overlay->w : bounds.w;
    int h = overlay->h > 0 ? overlay->h : bounds.h;
    if (w > bounds.w) {
        w = bounds.w;
    }
    if (h > bounds.h) {
        h = bounds.h;
    }
    if (w < 1) {
        w = 1;
    }
    if (h < 1) {
        h = 1;
    }

    struct inkcell_fb_rect rest = {bounds.x + (bounds.w - w) / 2, 0, w, h};
    switch (overlay->placement) {
    /* Flush against the edge of the region, for the reason above. A layer that should float
       clear of it says so by stating a region that stops short - which is what the snackbar
       does, and why its bounds end a margin above the footer rather than at it. */
    case INKCELL_OVERLAY_BOTTOM:
        rest.y = bounds.y + bounds.h - h;
        break;
    case INKCELL_OVERLAY_TOP:
        rest.y = bounds.y;
        break;
    case INKCELL_OVERLAY_ANCHOR: {
        /*
         * Under the anchor when there is room under it, over it when there is not - which is
         * what every menu on every platform does, and is decided here rather than by the
         * caller because the caller would have to measure the panel to answer it.
         */
        const int gap = inkcell_overlay_anchor_gap(state);
        const int below = overlay->anchor.y + overlay->anchor.h + gap;
        const int above = overlay->anchor.y - gap - h;
        rest.y = (below + h <= bounds.y + bounds.h || above < bounds.y) ? below : above;
        /*
         * Aligned to the anchor's leading edge rather than centred on it, and held inside the
         * bounds. A menu is *about* the thing it hangs off, so its edge lining up with that
         * thing's edge is what says so; centring would leave a wide menu under a narrow button
         * pointing at nothing in particular.
         */
        rest.x = overlay->anchor.x;
        if (rest.x + w > bounds.x + bounds.w - gutter) {
            rest.x = bounds.x + bounds.w - gutter - w;
        }
        if (rest.x < bounds.x + gutter) {
            rest.x = bounds.x + gutter;
        }
        break;
    }
    case INKCELL_OVERLAY_CENTER:
    default:
        rest.y = bounds.y + (bounds.h - h) / 2;
        break;
    }
    if (rest.y < bounds.y) {
        rest.y = bounds.y;
    }
    return rest;
}

/*
 * Where the layer comes *from*: the resting box, displaced.
 *
 * The direction is the placement's and the distance is the travel's, which is the split the
 * two enums exist to make. An anchored layer travels away from whichever edge it came off, so
 * a menu that opened upward rises and one that opened downward drops - and neither is a second
 * thing the caller had to say, because the placement already decided which it was.
 */
static struct inkcell_fb_rect inkcell_overlay_from(const struct inkcell_draw_state *state,
                                                   const struct inkcell_overlay *overlay,
                                                   struct inkcell_fb_rect bounds,
                                                   struct inkcell_fb_rect rest) {
    if (overlay->travel == INKCELL_OVERLAY_TRAVEL_NONE) {
        return rest;
    }
    const int near =
        inkcell_fb_panel_height(state) * INKCELL_OVERLAY_NEAR_NUM / INKCELL_OVERLAY_NEAR_DEN;
    /* Which way is "back where it came from": down for anything sitting at or rising towards
       the bottom, up for anything hanging from the top. */
    int sign = 1;
    switch (overlay->placement) {
    case INKCELL_OVERLAY_TOP:
        sign = -1;
        break;
    case INKCELL_OVERLAY_ANCHOR:
        /* Above the anchor means it opened upward, so it came from below - and the other way
           round. Compared against the anchor rather than remembered, because the placement
           already worked it out and a second copy of that decision is a second chance to
           disagree with it. */
        sign = (rest.y < overlay->anchor.y) ? 1 : -1;
        break;
    case INKCELL_OVERLAY_CENTER:
    case INKCELL_OVERLAY_BOTTOM:
    default:
        sign = 1;
        break;
    }

    struct inkcell_fb_rect from = rest;
    if (overlay->travel == INKCELL_OVERLAY_TRAVEL_OFF_PANEL) {
        /* Entirely outside the region it belongs to, so nothing of it is on the panel on the
           frame it starts. */
        from.y = (sign > 0) ? (bounds.y + bounds.h) : (bounds.y - rest.h);
        return from;
    }
    /*
     * A near travel stays *inside* the region, and only INKCELL_OVERLAY_TRAVEL_OFF_PANEL
     * leaves it. Two things follow and both are wanted.
     *
     * The first is that nothing is cut off on the way in. A layer half outside its bounds is
     * a layer with half its content clipped, and because the focus map goes through the same
     * clip, a dialog rising into place would have its two answers unreachable for the length
     * of the animation - the reader would press A on the frame it arrived and nothing would
     * happen.
     *
     * The second is that a layer *sized to its region has nowhere to travel*, and comes up
     * where it belongs. That is the right answer for a dialog filling the body, and it is not
     * a loss: what says a modal arrived is its scrim easing in under it, which is the one
     * entrance this panel can draw honestly. A panel cannot be faded or scaled here - there is
     * no alpha to fade through and glyphs come in whole multiples - so a centred modal that
     * insisted on travelling would be a panel sliding, which is a sheet's motion and says the
     * wrong thing.
     */
    from.y = rest.y + sign * near;
    if (from.y + rest.h > bounds.y + bounds.h) {
        from.y = bounds.y + bounds.h - rest.h;
    }
    if (from.y < bounds.y) {
        from.y = bounds.y;
    }
    return from;
}

/* The union of two boxes, as a damage rect: where a layer was and where it now is, which
   together are what the next frame has to be allowed to repaint. */
static struct inkcell_fb_damage_rect inkcell_overlay_span(struct inkcell_fb_rect a,
                                                          struct inkcell_fb_damage_rect b) {
    struct inkcell_fb_damage_rect out = {
        .x = a.x, .y = a.y, .right = a.x + a.w, .bottom = a.y + a.h, .valid = true};
    if (b.valid) {
        out.x = b.x < out.x ? b.x : out.x;
        out.y = b.y < out.y ? b.y : out.y;
        out.right = b.right > out.right ? b.right : out.right;
        out.bottom = b.bottom > out.bottom ? b.bottom : out.bottom;
    }
    return out;
}

bool inkcell_fb_overlay_begin(struct inkcell_draw_state *state,
                              const struct inkcell_overlay *overlay,
                              struct inkcell_overlay_frame *frame) {
    if (frame != NULL) {
        memset(frame, 0, sizeof *frame);
    }
    if (state == NULL || overlay == NULL || frame == NULL || overlay->id == INKCELL_OVERLAY_NONE) {
        return false;
    }
    struct inkcell_fb_overlay_slot *slot = inkcell_overlay_slot(state, overlay->id);
    if (slot == NULL) {
        /* Every slot is taken by a layer that is still on the panel. Refused rather than
           evicting one - see inkcell_overlay_slot(). */
        return false;
    }

    const bool up = overlay->up;
    if (up && !slot->up && !inkcell_anim_active(&slot->travel, state->now_ms) &&
        inkcell_anim_value(&slot->travel, state->now_ms) == 0) {
        /*
         * A layer arriving from nothing.
         *
         * Put back to zero with no duration first, then aimed at the far end below. That is
         * the same step around inkcell_anim's two deliberate no-ops that the snackbar takes -
         * re-aiming at a target already held does nothing, and a slot seen for the first time
         * adopts rather than animates - and here as there it has to be taken, because an
         * overlay appearing is exactly the thing worth showing.
         */
        inkcell_anim_set(&slot->travel, 0);
    }
    slot->up = up;
    slot->touched_ms = state->now_ms;
    slot->modal = overlay->modal;
    slot->order = ++state->overlay_order;

    /*
     * Aimed, then read. Not through the animation table: that table keys a scalar by an id so
     * that a widget with nowhere to keep one can, and a layer has somewhere - its own slot -
     * because it has more than a scalar to remember. `inkcell_anim_to()` is a no-op when it is
     * already heading where it is told to, so this may be called every frame with the state it
     * sees and only an actual change starts anything.
     */
    inkcell_anim_to(
        &slot->travel, state->now_ms, up ? INKCELL_ANIM_ONE : 0,
        inkcell_fb_motion(state, up ? INKCELL_OVERLAY_IN_MOTION : INKCELL_OVERLAY_OUT_MOTION),
        INKCELL_EASE_OUT);
    const int32_t progress = inkcell_anim_value(&slot->travel, state->now_ms);
    if (!up && progress == 0) {
        /*
         * All the way out. The slot is released here rather than when the app let go of it,
         * which is the whole of the lifetime rule: a layer is on the panel until its travel
         * says otherwise, and the app's `if` runs for exactly that long.
         */
        if (slot->drawn.valid) {
            /* One last declaration of where it was, so the frame that no longer draws it is
               still allowed to repaint the rows it left behind. */
            inkcell_fb_animation_damage(state, slot->drawn.x, slot->drawn.y,
                                        slot->drawn.right - slot->drawn.x,
                                        slot->drawn.bottom - slot->drawn.y);
        }
        memset(slot, 0, sizeof *slot);
        return false;
    }

    const struct inkcell_fb_rect bounds = inkcell_overlay_bounds(state, overlay);
    const struct inkcell_fb_rect rest = inkcell_overlay_rest(state, overlay, bounds);
    const struct inkcell_fb_rect from = inkcell_overlay_from(state, overlay, bounds, rest);
    const struct inkcell_fb_rect box = {
        .x = inkcell_overlay_between(from.x, rest.x, progress),
        .y = inkcell_overlay_between(from.y, rest.y, progress),
        .w = rest.w,
        .h = rest.h,
    };

    /*
     * The clip, before anything is drawn - including the scrim.
     *
     * It is the other half of what the bounds are for. A layer arriving is half outside its
     * resting place by construction, and content drawn past the region it belongs to would
     * paint over chrome the frame is not redrawing.
     *
     * A push that is refused - the view stack is full - takes the layer off this frame
     * entirely, rather than drawing it without its clip. Unclipped is not a degraded version
     * of clipped here: a sheet arriving would paint its way up across the app bar and the tab
     * strip, and the promise this call makes to its caller is that what it draws stays inside
     * its region. Not drawing is the only other honest answer, and it is why the push comes
     * *before* the scrim - a dimmed screen with no panel on it would be worse than either.
     *
     * No offset: an overlay's content is placed against the box it was handed, in panel
     * coordinates, so there is nothing to translate. A sheet whose *content* scrolls pushes a
     * second view inside this one, which is what the stack nests for.
     */
    if (!inkcell_fb_view_push(state, bounds, 0, 0)) {
        return false;
    }
    frame->clipped = true;

    /*
     * Where it is this frame, recorded for the next one to repaint.
     *
     * Declared here and *also* at inkcell_fb_app_frame_begin(), which is the half that took a
     * round of review to get right. This call runs after the body under the layer has been
     * drawn, so by now everything that could have repainted the rows the layer is vacating has
     * already been told they did not change - the old position has to be declared before the
     * frame starts, and that is what the loop in frame_begin() does with this record. See the
     * comment there.
     *
     * The span takes in the scrim's whole region rather than just the box, because a scrim is
     * a read-modify-write: a region dimmed last frame and not repainted this one would be
     * dimmed a second time, and the body behind a modal would darken a step per frame for as
     * long as the modal was up.
     */
    struct inkcell_fb_damage_rect span = inkcell_overlay_span(box, slot->drawn);
    if (overlay->scrim) {
        span = inkcell_overlay_span(bounds, span);
    }
    inkcell_fb_animation_damage(state, span.x, span.y, span.right - span.x, span.bottom - span.y);
    slot->drawn = span;

    /*
     * The scrim, before the panel and after everything under it.
     *
     * Eased with the layer rather than switched on with it, so a modal never appears over a
     * body that dimmed one frame earlier - the two are one arrival. It is drawn inside the
     * bounds, which is what lets a dialog dim the body and leave the navigation bar alone: the
     * app has not been replaced, one screen of it is asking a question.
     */
    if (overlay->scrim) {
        const int depth = inkcell_fb_scrim_depth(state);
        inkcell_fb_scrim_rect(state, bounds, inkcell_fb_color(state, INKCELL_COLOR_SCRIM),
                              (int)(((int64_t)depth * progress) / INKCELL_ANIM_ONE));
    }

    frame->id = overlay->id;
    frame->box = box;
    frame->rest = rest;
    frame->progress = progress;
    frame->arriving = up;
    return true;
}

void inkcell_fb_overlay_end(struct inkcell_draw_state *state, struct inkcell_overlay_frame *frame) {
    if (state == NULL || frame == NULL) {
        return;
    }
    if (frame->clipped) {
        inkcell_fb_view_pop(state);
        frame->clipped = false;
    }
}

uint32_t inkcell_fb_overlay_modal(const struct inkcell_draw_state *state) {
    if (state == NULL) {
        return INKCELL_OVERLAY_NONE;
    }
    uint32_t best = INKCELL_OVERLAY_NONE;
    uint32_t best_order = 0U;
    for (uint32_t i = 0U; i < INKCELL_OVERLAY_SLOTS; ++i) {
        const struct inkcell_fb_overlay_slot *slot = &state->overlays[i];
        /* `up` is the half that matters: a layer on its way out never owns the press, because
           a reader pressing B as a menu closes means the screen behind it. */
        if (slot->id == INKCELL_OVERLAY_NONE || !slot->modal || !slot->up) {
            continue;
        }
        if (slot->order >= best_order) {
            best_order = slot->order;
            best = slot->id;
        }
    }
    return best;
}

bool inkcell_fb_overlay_showing(const struct inkcell_draw_state *state, uint32_t id) {
    if (state == NULL || id == INKCELL_OVERLAY_NONE) {
        return false;
    }
    for (uint32_t i = 0U; i < INKCELL_OVERLAY_SLOTS; ++i) {
        if (state->overlays[i].id == id) {
            return true;
        }
    }
    return false;
}

void inkcell_fb_overlay_drop(struct inkcell_draw_state *state, uint32_t id) {
    if (state == NULL || id == INKCELL_OVERLAY_NONE) {
        return;
    }
    struct inkcell_fb_overlay_slot *slot = inkcell_overlay_find(state, id);
    if (slot == NULL) {
        return;
    }
    /* The box it painted is kept, and only that: the frame after this one has to be allowed to
       repaint the rows the old layer was on, and the slot being zeroed is exactly what would
       otherwise lose them. */
    const struct inkcell_fb_damage_rect drawn = slot->drawn;
    memset(slot, 0, sizeof *slot);
    if (drawn.valid) {
        inkcell_fb_animation_damage(state, drawn.x, drawn.y, drawn.right - drawn.x,
                                    drawn.bottom - drawn.y);
    }
}

void inkcell_fb_overlay_reset(struct inkcell_draw_state *state) {
    if (state == NULL) {
        return;
    }
    memset(state->overlays, 0, sizeof state->overlays);
    state->overlay_order = 0U;
}
