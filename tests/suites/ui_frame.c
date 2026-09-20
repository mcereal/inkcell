#define _POSIX_C_SOURCE 200809L

/*
 * The frame: where the chrome ends and the body begins, and the rows something moved into.
 *
 * Both of these are arithmetic over a geometry and a theme, with nothing drawn, so they are
 * testable here exactly as the easing curves are - and both were, until this change, declared
 * in a public header and defined nowhere at all. The golden sheet is what found that (a static
 * library never links, so nothing before it had to), and these are the cases that keep the
 * answers pinned now that there are answers.
 */

#include "framework/inkcell_test.h"

#include "inkcell/ui/fb_capture.h"
#include "inkcell/ui/widgets/chrome.h"

/* A state with a panel and a theme on it, and no device behind either. The capture harness is
   the supported way to get one; opening it here keeps these cases honest about using the same
   surface an application does. */
static struct inkcell_capture *frame_open(struct inkcell_backend_fb_state **state) {
    struct inkcell_capture *capture = NULL;
    if (inkcell_capture_open(&capture, INKCELL_CAPTURE_WIDTH, INKCELL_CAPTURE_HEIGHT, 4) < 0) {
        return NULL;
    }
    *state = inkcell_capture_state(capture);
    return capture;
}

INKCELL_TEST_CASE(layout_begin_leaves_room_for_the_footer, unit) {
    struct inkcell_backend_fb_state *state = NULL;
    struct inkcell_capture *capture = frame_open(&state);
    INKCELL_TEST_FAIL_IF(capture == NULL, "the capture should open");

    const struct inkcell_fb_layout bare = inkcell_fb_layout_begin(state, false, false);
    const struct inkcell_fb_layout footed = inkcell_fb_layout_begin(state, true, false);

    INKCELL_TEST_FAIL_IF(bare.footer_y != (int)INKCELL_CAPTURE_HEIGHT,
                         "with no action bar the body runs to the bottom of the panel");
    INKCELL_TEST_FAIL_IF(footed.footer_y >= bare.footer_y,
                         "an action bar should take its room out of the body");
    /* The room has to come out of the row count too, or a list fills rows the keycaps are
       about to be painted over. */
    INKCELL_TEST_FAIL_IF(footed.rows >= bare.rows, "the rows should drop by what the footer took");
    INKCELL_TEST_FAIL_IF(footed.body_y != bare.body_y,
                         "reserving the footer should not move the top of the body");

    inkcell_capture_close(capture);
    record_success(test_name);
}

INKCELL_TEST_CASE(layout_begin_opens_an_empty_frame, unit) {
    struct inkcell_backend_fb_state *state = NULL;
    struct inkcell_capture *capture = frame_open(&state);
    INKCELL_TEST_FAIL_IF(capture == NULL, "the capture should open");

    const struct inkcell_fb_layout layout = inkcell_fb_layout_begin(state, true, true);

    /* Nothing has been drawn above the body, so the first body row and the first row below the
       navigation bar are the same row. The nav bar moves both when it draws. */
    INKCELL_TEST_FAIL_IF(layout.nav_y != layout.body_y,
                         "an empty frame has nothing between nav_y and body_y");
    INKCELL_TEST_FAIL_IF(layout.line <= 0, "the body needs a line advance to count rows in");
    INKCELL_TEST_FAIL_IF(layout.rows == 0U, "a panel this size holds body rows");
    INKCELL_TEST_FAIL_IF(layout.cols == 0U, "the body has columns at the nominal advance");
    INKCELL_TEST_FAIL_IF(layout.body_w <= 0, "the body has a width to wrap against");
    INKCELL_TEST_FAIL_IF(!layout.back, "`back` is carried through as it was asked for");

    /* `rows` is the one arithmetic, and inkcell_fb_layout_rows() is where it lives: the two
       must not be able to disagree, because chrome recomputes with the second after moving the
       body and a list measures against the first. */
    INKCELL_TEST_FAIL_IF(inkcell_fb_layout_rows(state, &layout) != layout.rows,
                         "layout_begin should count rows the way layout_rows does");

    inkcell_capture_close(capture);
    record_success(test_name);
}

INKCELL_TEST_CASE(layout_rows_shrink_as_the_body_top_moves, unit) {
    struct inkcell_backend_fb_state *state = NULL;
    struct inkcell_capture *capture = frame_open(&state);
    INKCELL_TEST_FAIL_IF(capture == NULL, "the capture should open");

    struct inkcell_fb_layout layout = inkcell_fb_layout_begin(state, true, false);
    const uint32_t before = layout.rows;

    /* What a piece of chrome does when it takes its room: move the top, then recount. */
    layout.body_y += 4 * layout.line;
    INKCELL_TEST_FAIL_IF(inkcell_fb_layout_rows(state, &layout) != before - 4U,
                         "four body rows of chrome should cost four body rows");

    /* And the degenerate end: a body with no room left reports none rather than a negative
       count wrapped into an unsigned one. */
    layout.body_y = layout.footer_y + 1000;
    INKCELL_TEST_FAIL_IF(inkcell_fb_layout_rows(state, &layout) != 0U,
                         "a body past its own bottom holds no rows");

    inkcell_capture_close(capture);
    record_success(test_name);
}

INKCELL_TEST_CASE(animation_damage_unions_and_clamps, unit) {
    struct inkcell_backend_fb_state *state = NULL;
    struct inkcell_capture *capture = frame_open(&state);
    INKCELL_TEST_FAIL_IF(capture == NULL, "the capture should open");

    INKCELL_TEST_FAIL_IF(state->animation_damage.valid, "a fresh state has nothing moving on it");

    inkcell_fb_animation_damage(state, 100, 200, 50, 60);
    INKCELL_TEST_FAIL_IF(!state->animation_damage.valid, "the first rectangle should take");
    INKCELL_TEST_FAIL_IF(state->animation_damage.y != 200 || state->animation_damage.bottom != 260,
                         "the first rectangle is the damage");

    /* A second, further up and further down: the union has to cover both, because the rows are
       what the copy reads and a row between two moving things is a row neither claimed. */
    inkcell_fb_animation_damage(state, 10, 50, 20, 30);
    INKCELL_TEST_FAIL_IF(state->animation_damage.x != 10 || state->animation_damage.y != 50,
                         "the union should reach the higher, more leading corner");
    INKCELL_TEST_FAIL_IF(state->animation_damage.right != 150 ||
                             state->animation_damage.bottom != 260,
                         "the union should keep the further corner");

    /* Reaching onto the panel from off it. A widget sliding in from off-screen states where it
       is coming from, which is a coordinate the panel does not have - so the part that is on
       the panel is what gets kept. */
    inkcell_fb_animation_damage(state, -500, -500, 600, 600);
    INKCELL_TEST_FAIL_IF(state->animation_damage.x != 0 || state->animation_damage.y != 0,
                         "a rectangle starting off the panel should clamp to it");
    inkcell_fb_animation_damage(state, 0, 0, 100000, 100000);
    INKCELL_TEST_FAIL_IF(state->animation_damage.right != (int)INKCELL_CAPTURE_WIDTH ||
                             state->animation_damage.bottom != (int)INKCELL_CAPTURE_HEIGHT,
                         "a rectangle running off the panel should clamp to it");

    /* Entirely off it is nothing at all, rather than an inverted rectangle every row matches. */
    struct inkcell_fb_damage_rect before = state->animation_damage;
    inkcell_fb_animation_damage(state, -100, -100, 10, 10);
    INKCELL_TEST_FAIL_IF(state->animation_damage.x != before.x ||
                             state->animation_damage.right != before.right,
                         "a rectangle wholly off the panel should change nothing");
    inkcell_fb_animation_damage(state, 0, 0, 0, 0);
    INKCELL_TEST_FAIL_IF(state->animation_damage.right != before.right,
                         "an empty rectangle should change nothing");

    /* And a frame is the unit it lives for: last frame's moving parts are not this one's. */
    inkcell_fb_app_frame_begin(state);
    INKCELL_TEST_FAIL_IF(state->animation_damage.valid,
                         "a new frame should start with nothing moving on it");

    inkcell_capture_close(capture);
    record_success(test_name);
}

INKCELL_TEST_CASE(button_cap_never_returns_null, unit) {
    /* A keycap is drawn straight into a bar. A NULL here is a crash in the one place a wrong
       answer is merely cosmetic, which is why the contract is "" rather than NULL - including
       for the quit key, which no profile names, and for anything outside the enum. */
    for (int button = 0; button < (int)INKCELL_BUTTON_COUNT; ++button) {
        INKCELL_TEST_FAIL_IF(inkcell_button_cap((enum inkcell_button)button) == NULL,
                             "every button in the enum should name a cap or an empty string");
    }
    INKCELL_TEST_FAIL_IF(inkcell_button_cap((enum inkcell_button)INKCELL_BUTTON_COUNT) == NULL,
                         "a button outside the enum should still answer");
    INKCELL_TEST_FAIL_IF(inkcell_button_cap((enum inkcell_button) - 1) == NULL,
                         "a negative button should still answer");

    /* The face buttons are the four a profile is required to name, so these are never empty. */
    INKCELL_TEST_FAIL_IF(inkcell_button_cap(INKCELL_BUTTON_A)[0] == '\0',
                         "the A key is printed on every pad this runs on");
    record_success(test_name);
}
