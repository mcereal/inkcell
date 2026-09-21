#define _POSIX_C_SOURCE 200809L

/*
 * The floating action button: where it lands, what it does when it will not fit, and the
 * journey the golden sheet can only photograph the ends of.
 *
 * It gets a suite of its own because it is the one piece of chrome whose *width is a function
 * of time*. Everything else here is either arithmetic over a geometry and a theme - which is
 * tests/suites/ui_frame.c - or a picture, which is the gallery's digest. A container easing
 * between two widths while its label fades is neither: the sheet takes two frames and a
 * transition starts on the first of them, so what a picture can show is an end, and what
 * actually has to be pinned is that the thing in between is *between*.
 *
 * Nothing here looks at a pixel. inkcell_fb_draw_fab() hands back the box it came out as, so
 * every question below is asked of that box - which is the same box the focus map is given, and
 * therefore the same claim tests/suites/ui_focus_widgets.c rests on: what was drawn is what can
 * be reached.
 */

#include "framework/inkcell_test.h"

#include "inkcell/ui/fb_capture.h"
#include "inkcell/ui/widgets/chrome.h"

#include <string.h>

/* An identity for the collapse. Any non-zero value keys the animation table; 0 is the opt-out
   and is what the last case below is about. */
#define FAB_ANIM_ID 4242U

/* A panel too narrow to hold a large FAB and a verb beside it. Narrowing the frame rather than
   lengthening the label is how the widget suites reach a component's elision - it is a fact
   about the room rather than about how wide a theme happens to set a particular word. */
#define FAB_NARROW_WIDTH 240U

static struct inkcell_capture *fab_open(struct inkcell_backend_fb_state **state, uint32_t width,
                                        uint32_t height) {
    struct inkcell_capture *capture = NULL;
    if (inkcell_capture_open(&capture, width, height, 4) < 0) {
        return NULL;
    }
    *state = inkcell_capture_state(capture);
    return capture;
}

INKCELL_TEST_CASE(fab_anchors_to_the_trailing_corner, unit) {
    struct inkcell_backend_fb_state *state = NULL;
    struct inkcell_capture *capture =
        fab_open(&state, INKCELL_CAPTURE_WIDTH, INKCELL_CAPTURE_HEIGHT);
    INKCELL_TEST_FAIL_IF(capture == NULL, "the capture should open");

    const struct inkcell_fb_layout layout = inkcell_fb_layout_begin(state, true, false);
    const struct inkcell_fb_fab fab = {.icon = INKCELL_ICON_COMPOSE};
    const struct inkcell_fb_rect box = inkcell_fb_fab_box(state, &layout, &fab);
    const int margin = inkcell_fb_margin(state);

    INKCELL_TEST_FAIL_IF_CLEANUP(box.w <= 0, inkcell_capture_close(capture),
                                 "a full panel should have room for one");
    INKCELL_TEST_FAIL_IF_CLEANUP(box.x + box.w != (int)INKCELL_CAPTURE_WIDTH - margin,
                                 inkcell_capture_close(capture),
                                 "it stands a margin in from the trailing edge");
    /* A full margin clear of the footer, not the half a card stops at: it floats over the frame
       rather than sitting in the body, so it keeps the panel edge's distance. */
    INKCELL_TEST_FAIL_IF_CLEANUP(box.y + box.h != layout.footer_y - margin,
                                 inkcell_capture_close(capture),
                                 "and a margin clear of the action bar");
    INKCELL_TEST_FAIL_IF_CLEANUP(box.w != box.h, inkcell_capture_close(capture),
                                 "a FAB with no label is square, and therefore a circle");
    /* The clearance is the other half of "it consumes no body rows": what a scrolling body adds
       to its content so the last row can be pulled out from under it. */
    INKCELL_TEST_FAIL_IF_CLEANUP(inkcell_fb_fab_clearance(state, &layout, &fab) !=
                                     layout.footer_y - box.y,
                                 inkcell_capture_close(capture),
                                 "the clearance reaches from the disc to the foot of the body");

    inkcell_capture_close(capture);
    record_success(test_name);
}

INKCELL_TEST_CASE(fab_sizes_differ_in_the_room_around_the_symbol, unit) {
    struct inkcell_backend_fb_state *state = NULL;
    struct inkcell_capture *capture =
        fab_open(&state, INKCELL_CAPTURE_WIDTH, INKCELL_CAPTURE_HEIGHT);
    INKCELL_TEST_FAIL_IF(capture == NULL, "the capture should open");

    const struct inkcell_fb_layout layout = inkcell_fb_layout_begin(state, true, false);
    /* Smallest first, which is not the enum's own order: the zero value is the ordinary size
       rather than the smallest, so a FAB nobody said anything about is the one they meant. */
    static const enum inkcell_fb_fab_size k_sizes[] = {INKCELL_FB_FAB_SM, INKCELL_FB_FAB_MD,
                                                       INKCELL_FB_FAB_LG};
    int last = 0;
    for (size_t i = 0U; i < sizeof k_sizes / sizeof k_sizes[0]; ++i) {
        const struct inkcell_fb_fab fab = {.icon = INKCELL_ICON_COMPOSE, .size = k_sizes[i]};
        const struct inkcell_fb_rect box = inkcell_fb_fab_box(state, &layout, &fab);
        INKCELL_TEST_FAIL_IF_CLEANUP(box.w <= last, inkcell_capture_close(capture),
                                     "each size is bigger than the one under it");
        INKCELL_TEST_FAIL_IF_CLEANUP(box.w != box.h, inkcell_capture_close(capture),
                                     "and every one of them is a circle");
        last = box.w;
    }

    inkcell_capture_close(capture);
    record_success(test_name);
}

INKCELL_TEST_CASE(fab_extends_to_hold_its_verb, unit) {
    struct inkcell_backend_fb_state *state = NULL;
    struct inkcell_capture *capture =
        fab_open(&state, INKCELL_CAPTURE_WIDTH, INKCELL_CAPTURE_HEIGHT);
    INKCELL_TEST_FAIL_IF(capture == NULL, "the capture should open");

    const struct inkcell_fb_layout layout = inkcell_fb_layout_begin(state, true, false);
    const struct inkcell_fb_fab collapsed = {.icon = INKCELL_ICON_COMPOSE, .label = "Compose"};
    struct inkcell_fb_fab extended = collapsed;
    extended.extended = true;

    const struct inkcell_fb_rect disc = inkcell_fb_fab_box(state, &layout, &collapsed);
    const struct inkcell_fb_rect pill = inkcell_fb_fab_box(state, &layout, &extended);

    /* A label the FAB is not showing costs it nothing: `extended` is what decides the width, so
       a screen can hand the verb in on every frame and let the widget answer. */
    INKCELL_TEST_FAIL_IF_CLEANUP(disc.w != disc.h, inkcell_capture_close(capture),
                                 "a label that is not out leaves a disc");
    INKCELL_TEST_FAIL_IF_CLEANUP(pill.w <= pill.h, inkcell_capture_close(capture),
                                 "and one that is out makes a capsule");
    /* It grows leftwards. The trailing edge is the anchor, so a FAB extending does not walk off
       the panel and a screen's other chrome does not move under it. */
    INKCELL_TEST_FAIL_IF_CLEANUP(pill.x + pill.w != disc.x + disc.w, inkcell_capture_close(capture),
                                 "the trailing edge is where it is anchored");
    INKCELL_TEST_FAIL_IF_CLEANUP(pill.y != disc.y || pill.h != disc.h,
                                 inkcell_capture_close(capture),
                                 "extending changes the width and nothing else");

    inkcell_capture_close(capture);
    record_success(test_name);
}

INKCELL_TEST_CASE(fab_drops_a_verb_it_cannot_hold, unit) {
    struct inkcell_backend_fb_state *state = NULL;
    struct inkcell_capture *capture = fab_open(&state, FAB_NARROW_WIDTH, INKCELL_CAPTURE_HEIGHT);
    INKCELL_TEST_FAIL_IF(capture == NULL, "the capture should open");

    const struct inkcell_fb_layout layout = inkcell_fb_layout_begin(state, true, false);
    const struct inkcell_fb_fab fab = {.icon = INKCELL_ICON_COMPOSE,
                                       .label = "Start a new message",
                                       .extended = true,
                                       .size = INKCELL_FB_FAB_LG};
    const struct inkcell_fb_fab bare = {.icon = INKCELL_ICON_COMPOSE, .size = INKCELL_FB_FAB_LG};

    const struct inkcell_fb_rect box = inkcell_fb_fab_box(state, &layout, &fab);
    const struct inkcell_fb_rect disc = inkcell_fb_fab_box(state, &layout, &bare);

    /* The chip strip's elision, with one label instead of five: a verb that does not fit is a
       FAB without a verb, never a FAB hanging off the panel. */
    INKCELL_TEST_FAIL_IF_CLEANUP(box.w != disc.w, inkcell_capture_close(capture),
                                 "a verb too wide for the frame leaves the disc it started as");
    INKCELL_TEST_FAIL_IF_CLEANUP(box.x < inkcell_fb_margin(state), inkcell_capture_close(capture),
                                 "and it stays inside the margin either way");

    inkcell_capture_close(capture);
    record_success(test_name);
}

INKCELL_TEST_CASE(fab_never_leaves_the_panel_on_a_verb_that_grew, unit) {
    struct inkcell_backend_fb_state *state = NULL;
    struct inkcell_capture *capture = fab_open(&state, FAB_NARROW_WIDTH, INKCELL_CAPTURE_HEIGHT);
    INKCELL_TEST_FAIL_IF(capture == NULL, "the capture should open");

    const struct inkcell_fb_layout layout = inkcell_fb_layout_begin(state, true, false);
    struct inkcell_fb_fab fab = {
        .icon = INKCELL_ICON_COMPOSE, .label = "New", .extended = true, .id = FAB_ANIM_ID};

    const struct inkcell_fb_rect settled = inkcell_fb_draw_fab(state, &layout, &fab);
    INKCELL_TEST_FAIL_IF_CLEANUP(settled.w <= settled.h, inkcell_capture_close(capture),
                                 "a verb this frame can hold should be out");

    /*
     * The same FAB handed a verb the frame cannot hold. It stops fitting on the very frame its
     * collapse begins, and a collapse begins at the width it was already at - so this is the
     * one frame on which the eased width is wider than the room, and the FAB must still be on
     * the panel rather than off it until the next one.
     */
    fab.label = "Start a new message to somebody far away";
    const struct inkcell_fb_rect caught = inkcell_fb_draw_fab(state, &layout, &fab);
    INKCELL_TEST_FAIL_IF_CLEANUP(caught.w <= 0, inkcell_capture_close(capture),
                                 "it should not blink off the panel to lose a label");
    INKCELL_TEST_FAIL_IF_CLEANUP(caught.x < inkcell_fb_margin(state),
                                 inkcell_capture_close(capture),
                                 "and it should not reach past the margin to keep one");

    inkcell_capture_advance(capture, 4000U);
    const struct inkcell_fb_rect arrived = inkcell_fb_draw_fab(state, &layout, &fab);
    INKCELL_TEST_FAIL_IF_CLEANUP(arrived.w != arrived.h, inkcell_capture_close(capture),
                                 "and it finishes as the disc the verb no longer fits beside");

    inkcell_capture_close(capture);
    record_success(test_name);
}

INKCELL_TEST_CASE(fab_collapses_through_the_widths_between, unit) {
    struct inkcell_backend_fb_state *state = NULL;
    struct inkcell_capture *capture =
        fab_open(&state, INKCELL_CAPTURE_WIDTH, INKCELL_CAPTURE_HEIGHT);
    INKCELL_TEST_FAIL_IF(capture == NULL, "the capture should open");

    const struct inkcell_fb_layout layout = inkcell_fb_layout_begin(state, true, false);
    struct inkcell_fb_fab fab = {
        .icon = INKCELL_ICON_COMPOSE, .label = "Compose", .extended = true, .id = FAB_ANIM_ID};

    /* First sight of an id adopts its target rather than animating to it, so nothing slides on
       the frame a screen opens - the extended FAB is extended immediately. */
    const struct inkcell_fb_rect opened = inkcell_fb_draw_fab(state, &layout, &fab);
    INKCELL_TEST_FAIL_IF_CLEANUP(opened.w != inkcell_fb_fab_box(state, &layout, &fab).w,
                                 inkcell_capture_close(capture),
                                 "a first paint lands on its target rather than travelling to it");

    fab.extended = false;
    const struct inkcell_fb_rect resting = inkcell_fb_fab_box(state, &layout, &fab);

    /* The frame that observes the change starts the transition, so the container is still where
       it was - which is what makes the collapse a journey rather than a jump. */
    const struct inkcell_fb_rect began = inkcell_fb_draw_fab(state, &layout, &fab);
    INKCELL_TEST_FAIL_IF_CLEANUP(began.w != opened.w, inkcell_capture_close(capture),
                                 "the frame that asks for the collapse has not moved yet");

    inkcell_capture_advance(capture, 40U);
    const struct inkcell_fb_rect midway = inkcell_fb_draw_fab(state, &layout, &fab);
    INKCELL_TEST_FAIL_IF_CLEANUP(midway.w >= opened.w || midway.w <= resting.w,
                                 inkcell_capture_close(capture),
                                 "part way through it is between the two widths");
    INKCELL_TEST_FAIL_IF_CLEANUP(midway.x + midway.w != resting.x + resting.w,
                                 inkcell_capture_close(capture),
                                 "and it shrinks towards the edge it is anchored to");

    /* Well past every duration a theme can name for a short transition. */
    inkcell_capture_advance(capture, 4000U);
    const struct inkcell_fb_rect arrived = inkcell_fb_draw_fab(state, &layout, &fab);
    INKCELL_TEST_FAIL_IF_CLEANUP(arrived.w != resting.w, inkcell_capture_close(capture),
                                 "and it finishes on the disc rather than a rounding away from it");

    inkcell_capture_close(capture);
    record_success(test_name);
}

INKCELL_TEST_CASE(fab_without_an_identity_never_travels, unit) {
    struct inkcell_backend_fb_state *state = NULL;
    struct inkcell_capture *capture =
        fab_open(&state, INKCELL_CAPTURE_WIDTH, INKCELL_CAPTURE_HEIGHT);
    INKCELL_TEST_FAIL_IF(capture == NULL, "the capture should open");

    const struct inkcell_fb_layout layout = inkcell_fb_layout_begin(state, true, false);
    struct inkcell_fb_fab fab = {
        .icon = INKCELL_ICON_COMPOSE, .label = "Compose", .extended = true};

    const struct inkcell_fb_rect pill = inkcell_fb_draw_fab(state, &layout, &fab);
    fab.extended = false;
    const struct inkcell_fb_rect disc = inkcell_fb_draw_fab(state, &layout, &fab);

    /* An id of 0 is not a key. The FAB draws correctly at whichever width it was asked for and
       simply steps between them, which is the switch's contract and the animation table's. */
    INKCELL_TEST_FAIL_IF_CLEANUP(pill.w <= disc.w, inkcell_capture_close(capture),
                                 "it still answers to `extended`");
    INKCELL_TEST_FAIL_IF_CLEANUP(disc.w != disc.h, inkcell_capture_close(capture),
                                 "and lands on the disc on the very frame it is asked to");

    inkcell_capture_close(capture);
    record_success(test_name);
}

INKCELL_TEST_CASE(fab_draws_nothing_it_has_no_room_for, unit) {
    struct inkcell_backend_fb_state *state = NULL;
    /* A panel with chrome at both ends and almost nothing between them. */
    struct inkcell_capture *capture = fab_open(&state, INKCELL_CAPTURE_WIDTH, 200U);
    INKCELL_TEST_FAIL_IF(capture == NULL, "the capture should open");

    struct inkcell_fb_layout layout = inkcell_fb_layout_begin(state, true, false);
    layout.body_y = layout.footer_y - 1; /* a body one pixel tall: nowhere to float */

    const struct inkcell_fb_fab fab = {.icon = INKCELL_ICON_COMPOSE, .size = INKCELL_FB_FAB_LG};
    const struct inkcell_fb_rect box = inkcell_fb_draw_fab(state, &layout, &fab);

    INKCELL_TEST_FAIL_IF_CLEANUP(box.w != 0 || box.h != 0, inkcell_capture_close(capture),
                                 "a frame with nowhere to put one draws nothing");
    INKCELL_TEST_FAIL_IF_CLEANUP(inkcell_fb_fab_clearance(state, &layout, &fab) != 0,
                                 inkcell_capture_close(capture),
                                 "and charges a scrolling body nothing for it");

    /* A FAB is a picture first: one with nothing to put in its disc is nothing at all, rather
       than an empty circle in the corner of every screen that forgot to name a symbol. */
    const struct inkcell_fb_layout roomy = inkcell_fb_layout_begin(state, true, false);
    const struct inkcell_fb_fab blank = {.label = "Compose", .extended = true};
    INKCELL_TEST_FAIL_IF_CLEANUP(inkcell_fb_draw_fab(state, &roomy, &blank).w != 0,
                                 inkcell_capture_close(capture),
                                 "a FAB with no symbol is not a FAB");

    inkcell_capture_close(capture);
    record_success(test_name);
}
