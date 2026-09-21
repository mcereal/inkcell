#define _POSIX_C_SOURCE 200809L

/*
 * The focus ring's journey: adopting, travelling, turning round, and forgetting.
 *
 * None of this is a picture. The ring is a few pixels of outline and what matters about it is
 * *where it is at a given millisecond*, so every case here steps a named clock and reads the
 * position back with inkcell_fb_focus_ring_rect(). A golden picture of a ring halfway through a
 * move would pin the easing curve to a digest, which is the wrong thing to make hard to change.
 *
 * The clock is named rather than read for the reason the capture's is: a test whose answers
 * depend on how fast the host got round to the next frame is not a test.
 */

#include "framework/inkcell_test.h"

#include "inkcell/ui/fb_capture.h"
#include "inkcell/ui/focus.h"
#include "inkcell/ui/widgets.h"

#define RING_STORAGE 8U
#define RING_T0 1000U

enum {
    RING_ID_LEFT = 1,
    RING_ID_RIGHT,
    RING_ID_BELOW,
};

struct ring_harness {
    struct inkcell_capture *capture;
    struct inkcell_backend_fb_state *state;
    struct inkcell_focus_item storage[RING_STORAGE];
    struct inkcell_focus_map map;
};

/*
 * Three boxes and a clock. The map is filled directly rather than by drawing components,
 * because what is under test is the ring and a component in the way would only decide the
 * coordinates for us.
 */
static bool ring_open(struct ring_harness *h) {
    h->capture = NULL;
    if (inkcell_capture_open(&h->capture, INKCELL_CAPTURE_WIDTH, INKCELL_CAPTURE_HEIGHT,
                             INKCELL_SCALE(4)) < 0) {
        return false;
    }
    h->state = inkcell_capture_state(h->capture);
    inkcell_fb_state_set_now(h->state, RING_T0);
    inkcell_focus_begin(&h->map, h->storage, RING_STORAGE);
    inkcell_fb_set_focus_map(h->state, &h->map);
    (void)inkcell_focus_add_round(&h->map, RING_ID_LEFT, 100, 100, 80, 40, 8);
    (void)inkcell_focus_add_round(&h->map, RING_ID_RIGHT, 500, 100, 200, 40, 20);
    (void)inkcell_focus_add_round(&h->map, RING_ID_BELOW, 100, 300, 80, 40, 8);
    return true;
}

static void ring_close(struct ring_harness *h) {
    inkcell_capture_close(h->capture);
}

/* How long a move takes on this state's theme. Asked rather than assumed, because the duration
   is a theme token and a case that hard-coded it would fail on a theme that moved faster. */
static uint32_t ring_motion(const struct ring_harness *h) {
    return inkcell_fb_motion(h->state, INKCELL_FB_FOCUS_RING_MOTION);
}

INKCELL_TEST_CASE(focus_ring_adopts_the_first_box_it_is_shown, unit) {
    struct ring_harness h;
    INKCELL_TEST_FAIL_IF(!ring_open(&h), "the capture should open");

    struct inkcell_focus_rect at = {0, 0, 0, 0};
    int radius = 0;
    INKCELL_TEST_FAIL_IF_CLEANUP(inkcell_fb_focus_ring_rect(h.state, &at, &radius), ring_close(&h),
                                 "a frame that has drawn no ring has none");

    inkcell_fb_draw_focus_ring(h.state, &h.map, RING_ID_LEFT);
    INKCELL_TEST_FAIL_IF_CLEANUP(!inkcell_fb_focus_ring_rect(h.state, &at, &radius), ring_close(&h),
                                 "the ring should be up");
    /* On the box outright, not on the way to it: a screen opening is not a press. */
    INKCELL_TEST_FAIL_IF_CLEANUP(at.x != 100 || at.y != 100 || at.w != 80 || at.h != 40,
                                 ring_close(&h),
                                 "the first box a ring is shown should be adopted, not flown to");
    INKCELL_TEST_FAIL_IF_CLEANUP(radius != 8, ring_close(&h),
                                 "and it should take that box's own corner");
    INKCELL_TEST_FAIL_IF_CLEANUP(inkcell_fb_state_animating(h.state), ring_close(&h),
                                 "adopting owes no second frame");
    ring_close(&h);
    record_success(test_name);
}

/*
 * The whole point, in one case: a move is a journey with a middle.
 *
 * The box halfway is checked for being *between* rather than at a computed coordinate - the
 * easing curve is INKCELL_EASE_OUT and pinning its exact output here would make this a test of
 * the curve, which tests/suites/ui_anim.c already is.
 */
INKCELL_TEST_CASE(focus_ring_travels_between_two_boxes, unit) {
    struct ring_harness h;
    INKCELL_TEST_FAIL_IF(!ring_open(&h), "the capture should open");

    inkcell_fb_draw_focus_ring(h.state, &h.map, RING_ID_LEFT);
    inkcell_fb_draw_focus_ring(h.state, &h.map, RING_ID_RIGHT);

    struct inkcell_focus_rect at = {0, 0, 0, 0};
    int radius = 0;
    (void)inkcell_fb_focus_ring_rect(h.state, &at, &radius);
    INKCELL_TEST_FAIL_IF_CLEANUP(at.x != 100, ring_close(&h),
                                 "the journey should start at the box it left");
    INKCELL_TEST_FAIL_IF_CLEANUP(!inkcell_fb_state_animating(h.state), ring_close(&h),
                                 "a ring on the move owes another frame");

    inkcell_fb_state_set_now(h.state, RING_T0 + ring_motion(&h) / 2U);
    inkcell_fb_draw_focus_ring(h.state, &h.map, RING_ID_RIGHT);
    (void)inkcell_fb_focus_ring_rect(h.state, &at, &radius);
    INKCELL_TEST_FAIL_IF_CLEANUP(at.x <= 100 || at.x >= 500, ring_close(&h),
                                 "halfway through, the ring is between the two boxes");
    /* The box it is on is a different width and a different roundness, and both travel: a ring
       that moved but did not grow would arrive as the wrong shape on the wrong box. */
    INKCELL_TEST_FAIL_IF_CLEANUP(at.w <= 80 || at.w >= 200, ring_close(&h),
                                 "the ring should be growing into the wider box");
    INKCELL_TEST_FAIL_IF_CLEANUP(radius <= 8 || radius >= 20, ring_close(&h),
                                 "and rounding into the rounder one");

    inkcell_fb_state_set_now(h.state, RING_T0 + ring_motion(&h));
    inkcell_fb_draw_focus_ring(h.state, &h.map, RING_ID_RIGHT);
    (void)inkcell_fb_focus_ring_rect(h.state, &at, &radius);
    INKCELL_TEST_FAIL_IF_CLEANUP(at.x != 500 || at.w != 200 || radius != 20, ring_close(&h),
                                 "a finished journey lands exactly on its box");
    INKCELL_TEST_FAIL_IF_CLEANUP(inkcell_fb_state_animating(h.state), ring_close(&h),
                                 "and stops asking for frames");
    ring_close(&h);
    record_success(test_name);
}

/*
 * A second press before the first has arrived turns round from where the ring *is*, not from
 * where it set out. The same property inkcell_anim_to() gives a switch flicked twice, and the
 * reason the journey stores a rectangle rather than a pair of ids.
 */
INKCELL_TEST_CASE(focus_ring_turns_round_from_where_it_is, unit) {
    struct ring_harness h;
    INKCELL_TEST_FAIL_IF(!ring_open(&h), "the capture should open");

    inkcell_fb_draw_focus_ring(h.state, &h.map, RING_ID_LEFT);
    inkcell_fb_draw_focus_ring(h.state, &h.map, RING_ID_RIGHT);
    inkcell_fb_state_set_now(h.state, RING_T0 + ring_motion(&h) / 2U);
    inkcell_fb_draw_focus_ring(h.state, &h.map, RING_ID_RIGHT);

    struct inkcell_focus_rect midway = {0, 0, 0, 0};
    (void)inkcell_fb_focus_ring_rect(h.state, &midway, NULL);

    /* Back to where it came from, from half a journey out. */
    inkcell_fb_draw_focus_ring(h.state, &h.map, RING_ID_LEFT);
    struct inkcell_focus_rect at = {0, 0, 0, 0};
    (void)inkcell_fb_focus_ring_rect(h.state, &at, NULL);
    INKCELL_TEST_FAIL_IF_CLEANUP(at.x != midway.x, ring_close(&h),
                                 "turning round should start from where the ring had got to");

    inkcell_fb_state_set_now(h.state, RING_T0 + ring_motion(&h) / 2U + ring_motion(&h));
    inkcell_fb_draw_focus_ring(h.state, &h.map, RING_ID_LEFT);
    (void)inkcell_fb_focus_ring_rect(h.state, &at, NULL);
    INKCELL_TEST_FAIL_IF_CLEANUP(at.x != 100 || at.w != 80, ring_close(&h),
                                 "and still land on the box it turned back to");
    ring_close(&h);
    record_success(test_name);
}

/*
 * The cursor was moved by the layout rather than by a press: a row was deleted, a filter
 * emptied the list, and inkcell_focus_nearest() answered somewhere new. A ring that flew there
 * would be reporting a press nobody made.
 */
INKCELL_TEST_CASE(focus_ring_placed_does_not_travel, unit) {
    struct ring_harness h;
    INKCELL_TEST_FAIL_IF(!ring_open(&h), "the capture should open");

    inkcell_fb_draw_focus_ring(h.state, &h.map, RING_ID_LEFT);
    inkcell_fb_focus_ring_place(h.state, &h.map, RING_ID_BELOW);
    inkcell_fb_draw_focus_ring(h.state, &h.map, RING_ID_BELOW);

    struct inkcell_focus_rect at = {0, 0, 0, 0};
    (void)inkcell_fb_focus_ring_rect(h.state, &at, NULL);
    INKCELL_TEST_FAIL_IF_CLEANUP(at.y != 300, ring_close(&h),
                                 "a placed ring is already where it was put");
    INKCELL_TEST_FAIL_IF_CLEANUP(inkcell_fb_state_animating(h.state), ring_close(&h),
                                 "and owes no frame for a journey it did not make");
    ring_close(&h);
    record_success(test_name);
}

/*
 * The box went away.
 *
 * A row that scrolled off, a verb a narrower card dropped: the id is no longer in the map, so
 * there is nothing to draw a ring round and - the part that matters - nothing honest to travel
 * *from* next time. Keeping the last box would fly the ring in from a rectangle that is not on
 * the panel any more.
 */
INKCELL_TEST_CASE(focus_ring_forgets_a_box_that_left_the_frame, unit) {
    struct ring_harness h;
    INKCELL_TEST_FAIL_IF(!ring_open(&h), "the capture should open");

    inkcell_fb_draw_focus_ring(h.state, &h.map, RING_ID_LEFT);

    /* The next frame draws a screen without that box on it at all. */
    inkcell_focus_begin(&h.map, h.storage, RING_STORAGE);
    (void)inkcell_focus_add_round(&h.map, RING_ID_RIGHT, 500, 100, 200, 40, 20);
    inkcell_fb_draw_focus_ring(h.state, &h.map, RING_ID_LEFT);
    INKCELL_TEST_FAIL_IF_CLEANUP(inkcell_fb_focus_ring_rect(h.state, NULL, NULL), ring_close(&h),
                                 "a ring has nothing to be on when its box is gone");

    /* And the box it lands on next is adopted rather than travelled to. */
    inkcell_fb_draw_focus_ring(h.state, &h.map, RING_ID_RIGHT);
    struct inkcell_focus_rect at = {0, 0, 0, 0};
    (void)inkcell_fb_focus_ring_rect(h.state, &at, NULL);
    INKCELL_TEST_FAIL_IF_CLEANUP(at.x != 500, ring_close(&h),
                                 "with nowhere to come from, the next box is adopted");
    INKCELL_TEST_FAIL_IF_CLEANUP(inkcell_fb_state_animating(h.state), ring_close(&h),
                                 "so nothing is in flight");

    /* INKCELL_FOCUS_NONE is the same answer asked a different way. */
    inkcell_fb_draw_focus_ring(h.state, &h.map, INKCELL_FOCUS_NONE);
    INKCELL_TEST_FAIL_IF_CLEANUP(inkcell_fb_focus_ring_rect(h.state, NULL, NULL), ring_close(&h),
                                 "a screen with nothing focused has no ring");
    ring_close(&h);
    record_success(test_name);
}

/*
 * The ring goes exactly where its box went.
 *
 * A box moving under the ring is not the cursor moving - a list gliding between windows shifts
 * its rows a few pixels per frame - so following is what keeps the ring on its own row. A
 * journey there would trail it by an easing curve for the length of the scroll.
 */
INKCELL_TEST_CASE(focus_ring_follows_a_box_that_moved, unit) {
    struct ring_harness h;
    INKCELL_TEST_FAIL_IF(!ring_open(&h), "the capture should open");

    inkcell_fb_draw_focus_ring(h.state, &h.map, RING_ID_LEFT);

    /* The same id, one row further up: what a list looks like after it scrolled. The second
       box stays registered throughout, because the journey below starts from this frame. */
    inkcell_focus_begin(&h.map, h.storage, RING_STORAGE);
    (void)inkcell_focus_add_round(&h.map, RING_ID_LEFT, 100, 60, 80, 40, 8);
    (void)inkcell_focus_add_round(&h.map, RING_ID_BELOW, 100, 300, 80, 40, 8);
    inkcell_fb_draw_focus_ring(h.state, &h.map, RING_ID_LEFT);
    INKCELL_TEST_FAIL_IF_CLEANUP(inkcell_fb_state_animating(h.state), ring_close(&h),
                                 "following a box is not a journey and owes no frames of its own");

    struct inkcell_focus_rect at = {0, 0, 0, 0};
    (void)inkcell_fb_focus_ring_rect(h.state, &at, NULL);
    INKCELL_TEST_FAIL_IF_CLEANUP(at.y != 60, ring_close(&h),
                                 "it should be where the box went, on the same frame");

    /* A move that is still in flight is not broken off by its destination moving: the journey
       keeps its start and its clock and lands on the corrected box. */
    inkcell_fb_draw_focus_ring(h.state, &h.map, RING_ID_BELOW);
    inkcell_fb_state_set_now(h.state, RING_T0 + ring_motion(&h) / 2U);
    inkcell_focus_begin(&h.map, h.storage, RING_STORAGE);
    (void)inkcell_focus_add_round(&h.map, RING_ID_BELOW, 100, 320, 80, 40, 8);
    inkcell_fb_draw_focus_ring(h.state, &h.map, RING_ID_BELOW);
    INKCELL_TEST_FAIL_IF_CLEANUP(!inkcell_fb_state_animating(h.state), ring_close(&h),
                                 "a journey under way should still be under way");
    inkcell_fb_state_set_now(h.state, RING_T0 + 2U * ring_motion(&h));
    inkcell_fb_draw_focus_ring(h.state, &h.map, RING_ID_BELOW);
    (void)inkcell_fb_focus_ring_rect(h.state, &at, NULL);
    INKCELL_TEST_FAIL_IF_CLEANUP(at.y != 320, ring_close(&h),
                                 "and land where the box had moved to");
    ring_close(&h);
    record_success(test_name);
}

/* A component's own shape reaches the ring: a pill registers as a pill, and the ring that
   lands on it is one too rather than a rectangle with the corners guessed at. */
INKCELL_TEST_CASE(focus_ring_takes_the_shape_the_component_drew, unit) {
    struct ring_harness h;
    INKCELL_TEST_FAIL_IF(!ring_open(&h), "the capture should open");
    inkcell_focus_begin(&h.map, h.storage, RING_STORAGE);

    const struct inkcell_fb_button pill = {.rect = {40, 40, 200, 40},
                                           .label = "chip",
                                           .shape = INKCELL_SHAPE_FULL,
                                           .scale = INKCELL_SCALE(2),
                                           .focus_id = RING_ID_LEFT};
    inkcell_fb_draw_button(h.state, &pill);
    /* Half the shorter side: what a fill resolves "as round as it goes" to, so what the map
       holds is the curve on the panel rather than the INT16_MAX that asked for it. */
    INKCELL_TEST_FAIL_IF_CLEANUP(inkcell_focus_radius_of(&h.map, RING_ID_LEFT) != 20,
                                 ring_close(&h), "a pill should register as a pill");

    inkcell_fb_draw_focus_ring(h.state, &h.map, RING_ID_LEFT);
    int radius = 0;
    (void)inkcell_fb_focus_ring_rect(h.state, NULL, &radius);
    INKCELL_TEST_FAIL_IF_CLEANUP(radius != 20, ring_close(&h), "and the ring on it should be one");
    ring_close(&h);
    record_success(test_name);
}

/*
 * Erasing a ring is somebody else's drawing, and that drawing has already happened by the time
 * the ring is asked to move - so the box it painted is carried into the next frame and declared
 * there, before the first fill. Without it a screen that repaints part of a frame keeps a trail
 * of outline behind a travelling ring, and keeps a whole ring after the cursor has gone.
 *
 * The two cases are one test because they are one mechanism seen from either end: a ring that
 * moved, and a ring that is no longer there at all.
 */
INKCELL_TEST_CASE(focus_ring_leaves_the_next_frame_the_rows_it_painted, unit) {
    struct ring_harness h;
    INKCELL_TEST_FAIL_IF(!ring_open(&h), "the capture should open");

    inkcell_fb_draw_focus_ring(h.state, &h.map, RING_ID_LEFT);
    INKCELL_TEST_FAIL_IF_CLEANUP(!h.state->focus_ring.drawn.valid, ring_close(&h),
                                 "a ring that painted should have said where");

    /* The next frame begins: what the ring painted is this frame's to repaint, and it is said
       before anything draws rather than when the ring is reached at the end. */
    inkcell_fb_app_frame_begin(h.state);
    INKCELL_TEST_FAIL_IF_CLEANUP(!h.state->animation_damage.valid, ring_close(&h),
                                 "the frame should open owing the ring's old rows");
    INKCELL_TEST_FAIL_IF_CLEANUP(h.state->animation_damage.x > 100 ||
                                     h.state->animation_damage.right < 180,
                                 ring_close(&h), "and the rows owed should be the ones it painted");
    INKCELL_TEST_FAIL_IF_CLEANUP(h.state->focus_ring.drawn.valid, ring_close(&h),
                                 "once said, it is not owed twice");

    /*
     * And the cursor goes away entirely, which is the case with no draw call left to speak.
     *
     * The frame that opens owing those rows is the same frame on which the ring turns out not
     * to be there - which is what makes this work without a draw call: the rows were declared
     * before anything painted, so whatever is under them is redrawn over the ring during this
     * frame rather than after it. Nothing is owed afterwards, and that is the assertion: a box
     * still outstanding here would be one nobody is ever going to paint.
     */
    inkcell_fb_draw_focus_ring(h.state, &h.map, RING_ID_LEFT);
    inkcell_fb_app_frame_begin(h.state);
    INKCELL_TEST_FAIL_IF_CLEANUP(!h.state->animation_damage.valid, ring_close(&h),
                                 "the frame the ring vanishes on is the one that repaints it");
    inkcell_fb_draw_focus_ring(h.state, &h.map, INKCELL_FOCUS_NONE);
    INKCELL_TEST_FAIL_IF_CLEANUP(inkcell_fb_focus_ring_rect(h.state, NULL, NULL), ring_close(&h),
                                 "the ring is gone");
    INKCELL_TEST_FAIL_IF_CLEANUP(h.state->focus_ring.drawn.valid, ring_close(&h),
                                 "and it leaves nothing behind for a frame that will not come");
    ring_close(&h);
    record_success(test_name);
}

/*
 * A list that scrolled is a new id in the old box, and that is not a journey.
 *
 * Pressing down at the bottom of a window moves the cursor to the next item and the window with
 * it, so the row under the cursor comes out exactly where the last one was. The id changed and
 * the rectangle did not. A ring that started a transition here would interpolate between two
 * identical boxes for a motion's worth of frames and ask to be redrawn for every one of them,
 * which is a scroll pretending to be a move.
 */
INKCELL_TEST_CASE(focus_ring_does_not_travel_where_there_is_no_distance, unit) {
    struct ring_harness h;
    INKCELL_TEST_FAIL_IF(!ring_open(&h), "the capture should open");

    inkcell_fb_draw_focus_ring(h.state, &h.map, RING_ID_LEFT);

    /* The next frame: a different item, drawn in the box the last one had. */
    inkcell_focus_begin(&h.map, h.storage, RING_STORAGE);
    (void)inkcell_focus_add_round(&h.map, RING_ID_RIGHT, 100, 100, 80, 40, 8);
    inkcell_fb_draw_focus_ring(h.state, &h.map, RING_ID_RIGHT);

    INKCELL_TEST_FAIL_IF_CLEANUP(inkcell_fb_state_animating(h.state), ring_close(&h),
                                 "a ring with nowhere to go should not claim to be moving");
    struct inkcell_focus_rect at = {0, 0, 0, 0};
    (void)inkcell_fb_focus_ring_rect(h.state, &at, NULL);
    INKCELL_TEST_FAIL_IF_CLEANUP(at.x != 100 || at.y != 100, ring_close(&h),
                                 "and it should be where both boxes are");

    /* A real move from there still travels: the rule is about distance, not about the id. */
    inkcell_focus_begin(&h.map, h.storage, RING_STORAGE);
    (void)inkcell_focus_add_round(&h.map, RING_ID_BELOW, 100, 300, 80, 40, 8);
    inkcell_fb_draw_focus_ring(h.state, &h.map, RING_ID_BELOW);
    INKCELL_TEST_FAIL_IF_CLEANUP(!inkcell_fb_state_animating(h.state), ring_close(&h),
                                 "a box somewhere else is still a journey");
    ring_close(&h);
    record_success(test_name);
}
