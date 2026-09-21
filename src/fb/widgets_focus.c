#define _POSIX_C_SOURCE 200809L

/*
 * The focus ring: one outline, travelling between the boxes the frame registered.
 *
 * All of the difficulty is in one question - what is it travelling *from* - and the answer is
 * why this is not a slot in the animation table. That table remembers a scalar per id, and the
 * box the ring is leaving belongs to an id the cursor has already left; by the time the move
 * is known, the thing that would key it is gone. So the journey is kept on the frame, beside
 * the screen transition, which is the other thing here that is one per frame rather than one
 * per widget.
 *
 * What it draws is decided entirely by the map: the rectangle, and the radius the component
 * recorded when it drew itself. Nothing in this file knows what a chip is.
 */

#include "inkcell/ui/widgets/focus.h"

#include "inkcell/ui/theme.h"

/* How far outside the box the ring sits, and how thick it is: the hairline everything else
   outlines with, and twice it - the same pair the card's focus edge takes, because a ring that
   was thinner than the card's would read as a different kind of thing. */
static int focus_ring_inset(const struct inkcell_backend_fb_state *state) {
    return inkcell_fb_edge(state);
}

static int focus_ring_thickness(const struct inkcell_backend_fb_state *state) {
    return 2 * inkcell_fb_edge(state);
}

/* One coordinate, `progress` of the way from `from` to `to`. In 64 bits because a panel
   coordinate times INKCELL_ANIM_ONE leaves an int with very little room, and a ring that
   overflowed would land somewhere no arithmetic explains. */
static int focus_ring_between(int from, int to, int32_t progress) {
    const int64_t span = (int64_t)to - (int64_t)from;
    return (int)((int64_t)from + (span * progress) / INKCELL_ANIM_ONE);
}

static struct inkcell_focus_rect focus_ring_rect_between(struct inkcell_focus_rect from,
                                                         struct inkcell_focus_rect to,
                                                         int32_t progress) {
    return (struct inkcell_focus_rect){
        .x = focus_ring_between(from.x, to.x, progress),
        .y = focus_ring_between(from.y, to.y, progress),
        .w = focus_ring_between(from.w, to.w, progress),
        .h = focus_ring_between(from.h, to.h, progress),
    };
}

static bool focus_ring_same(struct inkcell_focus_rect a, struct inkcell_focus_rect b) {
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

/* Where the ring is now, travel included. The one reader of `travel`. */
static struct inkcell_focus_rect focus_ring_now(const struct inkcell_fb_focus_ring *ring,
                                                uint64_t now_ms, int *radius) {
    const int32_t at = inkcell_anim_value(&ring->travel, now_ms);
    if (radius != NULL) {
        *radius = focus_ring_between(ring->from_radius, ring->to_radius, at);
    }
    return focus_ring_rect_between(ring->from, ring->to, at);
}

/* The target, or false when there is nothing to put a ring on. */
static bool focus_ring_target(const struct inkcell_focus_map *map, uint32_t id,
                              struct inkcell_focus_rect *rect, int *radius) {
    if (id == INKCELL_FOCUS_NONE || !inkcell_focus_rect_of(map, id, rect)) {
        return false;
    }
    *radius = inkcell_focus_radius_of(map, id);
    return true;
}

/*
 * Aims the ring at a box, from wherever it currently is.
 *
 * `travelled` is the whole of the difference between a press and a placement: false adopts the
 * target outright, which is what a screen opening wants and what a cursor moved by a reflow
 * rather than by a press wants.
 *
 * Taking the *current* position as the new start is what makes a second press mid-flight turn
 * round from where the ring actually is rather than from the box it set out from - the same
 * property inkcell_anim_to() exists to give a switch flicked twice.
 */
static void focus_ring_aim(struct inkcell_backend_fb_state *state, uint32_t id,
                           struct inkcell_focus_rect target, int radius, bool travelled) {
    struct inkcell_fb_focus_ring *ring = &state->focus_ring;
    const bool arriving = ring->id == INKCELL_FOCUS_NONE;

    if (!travelled || arriving) {
        ring->id = id;
        ring->from = target;
        ring->to = target;
        ring->from_radius = radius;
        ring->to_radius = radius;
        inkcell_anim_set(&ring->travel, INKCELL_ANIM_ONE);
        return;
    }
    if (ring->id == id && focus_ring_same(ring->to, target) && ring->to_radius == radius) {
        return; /* already there, or already on the way */
    }

    int from_radius = 0;
    const struct inkcell_focus_rect from = focus_ring_now(ring, state->now_ms, &from_radius);
    /*
     * A journey with no distance in it is not a journey.
     *
     * The case is a list that scrolled: the press moved the cursor to the next item, the window
     * moved with it, and the row under the cursor came out in exactly the place the last one
     * was. The id changed and the box did not, so there is nothing to travel - and a ring that
     * started a transition here would spend a motion's worth of frames interpolating between
     * two identical rectangles and asking to be redrawn for every one of them.
     */
    if (focus_ring_same(from, target) && from_radius == radius) {
        ring->id = id;
        ring->from = target;
        ring->to = target;
        ring->from_radius = radius;
        ring->to_radius = radius;
        inkcell_anim_set(&ring->travel, INKCELL_ANIM_ONE);
        return;
    }
    ring->id = id;
    ring->from = from;
    ring->from_radius = from_radius;
    ring->to = target;
    ring->to_radius = radius;
    /* Set to 0 before aiming: inkcell_anim_to() ignores a re-aim at the target it is already
       heading for, and every journey here heads for ONE. */
    inkcell_anim_set(&ring->travel, 0);
    inkcell_anim_to(&ring->travel, state->now_ms, INKCELL_ANIM_ONE,
                    inkcell_fb_motion(state, INKCELL_FB_FOCUS_RING_MOTION), INKCELL_EASE_OUT);
}

/*
 * The rows the stroke about to be drawn lands on, declared so that a frame drawn under a
 * partial-redraw band paints it and presents it.
 *
 * Only where the ring is *going*. Where it has been is not this frame's to say: those rows are
 * erased by whatever is under them being drawn again, which happened before this call - so the
 * box just painted is carried to the next frame instead and declared at
 * inkcell_fb_app_frame_begin(), where it is early enough to matter.
 */
static void focus_ring_painted(struct inkcell_backend_fb_state *state,
                               struct inkcell_focus_rect box) {
    const int pad = focus_ring_inset(state) + focus_ring_thickness(state);
    const int x = box.x - pad;
    const int y = box.y - pad;
    const int w = box.w + 2 * pad;
    const int h = box.h + 2 * pad;

    inkcell_fb_animation_damage(state, x, y, w, h);
    state->focus_ring.drawn = (struct inkcell_fb_damage_rect){
        .x = x, .y = y, .right = x + w, .bottom = y + h, .valid = true};
}

void inkcell_fb_focus_ring_place(struct inkcell_backend_fb_state *state,
                                 const struct inkcell_focus_map *map, uint32_t id) {
    if (state == NULL) {
        return;
    }
    struct inkcell_focus_rect target = {0, 0, 0, 0};
    int radius = 0;
    if (!focus_ring_target(map, id, &target, &radius)) {
        const struct inkcell_fb_damage_rect painted = state->focus_ring.drawn;
        state->focus_ring = (struct inkcell_fb_focus_ring){0};
        state->focus_ring.drawn = painted;
        return;
    }
    focus_ring_aim(state, id, target, radius, false);
}

void inkcell_fb_draw_focus_ring(struct inkcell_backend_fb_state *state,
                                const struct inkcell_focus_map *map, uint32_t id) {
    if (state == NULL) {
        return;
    }
    struct inkcell_focus_rect target = {0, 0, 0, 0};
    int radius = 0;
    if (!focus_ring_target(map, id, &target, &radius)) {
        /* Nothing to be on. Forgetting the journey rather than keeping the last box is what
           stops the next id being travelled to from a rectangle that is no longer anywhere -
           but what was *painted* survives the clearing, because those pixels are still on the
           panel and the next frame is where they get cleaned up. */
        const struct inkcell_fb_damage_rect painted = state->focus_ring.drawn;
        state->focus_ring = (struct inkcell_fb_focus_ring){0};
        state->focus_ring.drawn = painted;
        return;
    }

    focus_ring_aim(state, id, target, radius, true);

    int at_radius = 0;
    const struct inkcell_focus_rect at =
        focus_ring_now(&state->focus_ring, state->now_ms, &at_radius);
    focus_ring_painted(state, at);
    const int inset = focus_ring_inset(state);
    /* Outside the box rather than over it: the ring says where the cursor is, and a ring drawn
       on top of a label would be saying it at the label's expense. The radius grows with the
       inset for the same reason a card's outer edge does - two curves a hairline apart are
       concentric only if the outer one is the wider. */
    inkcell_fb_stroke_round_rect(state, at.x - inset, at.y - inset, at.w + 2 * inset,
                                 at.h + 2 * inset, at_radius + inset, focus_ring_thickness(state),
                                 inkcell_fb_tone_color(state, INKCELL_TONE_PRIMARY));
}

bool inkcell_fb_focus_ring_rect(const struct inkcell_backend_fb_state *state,
                                struct inkcell_focus_rect *rect, int *radius) {
    if (state == NULL || state->focus_ring.id == INKCELL_FOCUS_NONE) {
        return false;
    }
    int at_radius = 0;
    const struct inkcell_focus_rect at =
        focus_ring_now(&state->focus_ring, state->now_ms, &at_radius);
    if (rect != NULL) {
        *rect = at;
    }
    if (radius != NULL) {
        *radius = at_radius;
    }
    return true;
}
