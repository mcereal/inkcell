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
#include "inkcell/ui/widgets/keyboard.h"

/* A state with a panel and a theme on it, and no device behind either. The capture harness is
   the supported way to get one; opening it here keeps these cases honest about using the same
   surface an application does. */
static struct inkcell_capture *frame_open(struct inkcell_draw_state **state) {
    struct inkcell_capture *capture = NULL;
    if (inkcell_capture_open(&capture, INKCELL_CAPTURE_WIDTH, INKCELL_CAPTURE_HEIGHT,
                             INKCELL_SCALE(4)) < 0) {
        return NULL;
    }
    *state = inkcell_capture_state(capture);
    return capture;
}

INKCELL_TEST_CASE(layout_begin_leaves_room_for_the_footer, unit) {
    struct inkcell_draw_state *state = NULL;
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
    struct inkcell_draw_state *state = NULL;
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
    struct inkcell_draw_state *state = NULL;
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

/*
 * The keyboard grid's two ends: a body with room to spare, and one with none.
 *
 * Both are about where the five rows *land*, which is the one thing the golden sheet cannot
 * say - it renders two geometries, and the answer here changes with every panel and every
 * glyph scale. The widget draws nothing a digest would catch either: a grid that stopped short
 * of the footer looks like a grid.
 */
INKCELL_TEST_CASE(keyboard_grid_sits_on_the_footer, unit) {
    struct inkcell_draw_state *state = NULL;
    struct inkcell_capture *capture = frame_open(&state);
    INKCELL_TEST_FAIL_IF(capture == NULL, "the capture should open");

    const struct inkcell_fb_layout layout = inkcell_fb_layout_begin(state, true, true);
    const struct inkcell_keyboard keyboard = {0};
    const struct inkcell_fb_keyboard grid = {.keyboard = &keyboard};

    /* Room to spare: the keys take their cap and the slack goes above them, so the last row
       ends exactly on the footer. A keyboard sits at the bottom of what it is given. */
    int y = layout.body_y;
    inkcell_fb_draw_keyboard(state, &layout, &y, &grid);
    INKCELL_TEST_FAIL_IF(y != layout.footer_y,
                         "a grid with slack above it should end on the footer");

    /*
     * No room at all: two lines of body for five rows of keys. The floor wins and the grid
     * overflows the footer rather than being crushed - the rows are aimed at with a d-pad, and
     * a keycap thinner than the text on it is a key that cannot be hit. The last row going
     * under the keycap bar is the honest failure; a 16 px key is not.
     */
    const int floor_h = layout.line + inkcell_fb_space(state, INKCELL_SPACE_MD);
    int cramped = layout.footer_y - 2 * layout.line;
    const int before = cramped;
    inkcell_fb_draw_keyboard(state, &layout, &cramped, &grid);
    INKCELL_TEST_FAIL_IF(cramped - before != (int)INKCELL_KB_ROWS * floor_h,
                         "a grid with no room should hold its floor rather than compress");

    /* And nothing to draw is nothing drawn: a screen that has not opened a keyboard yet hands
       this a NULL cursor, and the row it was laying out must survive the call. */
    int untouched = layout.body_y;
    const struct inkcell_fb_keyboard empty = {0};
    inkcell_fb_draw_keyboard(state, &layout, &untouched, &empty);
    INKCELL_TEST_FAIL_IF(untouched != layout.body_y, "a keyboard with no cursor should draw none");

    inkcell_capture_close(capture);
    record_success(test_name);
}

INKCELL_TEST_CASE(animation_damage_unions_and_clamps, unit) {
    struct inkcell_draw_state *state = NULL;
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

/*
 * The four-way cap: printed, since it names a real press, and standing for no key a click could
 * make - so a pointer's bar leaves it out, as it does the arrow pair it widens.
 */
INKCELL_TEST_CASE(dpad_cap_is_printed_and_never_clicked, unit) {
    INKCELL_TEST_FAIL_IF(inkcell_button_cap(INKCELL_BUTTON_DPAD)[0] == '\0',
                         "the four-way cap is printed beside the verb it names");
    enum inkcell_key keys[2] = {INKCELL_KEY_NONE, INKCELL_KEY_NONE};
    INKCELL_TEST_FAIL_IF(inkcell_button_keys(INKCELL_BUTTON_DPAD, keys) != 0U,
                         "no one click can stand for four directions");
    record_success(test_name);
}

/*
 * The clipped frame, which is the case inkcell_fb_animation_damage() exists for and the one
 * nothing else in this tree exercises: no code here sets `clip_active`, so the band is a
 * contract with whatever application does.
 *
 * Both halves are checked together on purpose. Exempting only the copy copies pixels nothing
 * redrew; exempting only the draw redraws pixels nothing copies. Either alone is a control
 * frozen on the panel, and either alone passes a test that only looks at the other.
 */
INKCELL_TEST_CASE(animation_damage_draws_outside_the_clip_band, unit) {
    struct inkcell_draw_state *state = NULL;
    struct inkcell_capture *capture = frame_open(&state);
    INKCELL_TEST_FAIL_IF(capture == NULL, "the capture should open");

    const struct inkcell_rgb black = {0U, 0U, 0U};
    const struct inkcell_rgb white = {255U, 255U, 255U};
    inkcell_fb_clear(state, black);

    /* A band a list would repaint, and two rows well outside it. */
    state->clip_active = true;
    state->clip = (struct inkcell_fb_damage_rect){
        .x = 0, .y = 400, .right = (int)INKCELL_CAPTURE_WIDTH, .bottom = 500, .valid = true};

    uint32_t width = 0U;
    uint32_t height = 0U;
    size_t stride = 0U;
    const uint8_t *pixels = inkcell_capture_pixels(capture, &width, &height, &stride);
    INKCELL_TEST_FAIL_IF(pixels == NULL, "the page should have pixels");

    /* Outside the band and undeclared: rejected, as a clipped frame should reject it. */
    inkcell_fb_fill_rect(state, 10, 100, 40, 20, white);
    INKCELL_TEST_FAIL_IF(pixels[(size_t)110 * stride + 20 * 4] != 0U,
                         "an undeclared fill outside the band should be clipped away");

    /* Declared, then drawn - the order every widget uses. This one has to land, or the knob
       that moved has no way to be repainted where it moved to. */
    inkcell_fb_animation_damage(state, 0, 200, 100, 60);
    inkcell_fb_fill_rect(state, 10, 210, 40, 20, white);
    INKCELL_TEST_FAIL_IF(pixels[(size_t)220 * stride + 20 * 4] == 0U,
                         "a fill inside declared animation damage should reach the panel");

    /* And the exemption is not a hole in the band: a box that is not wholly inside the damage
       is still that band's to clip, or one widget's declaration would let every later draw in
       the frame through. */
    inkcell_fb_fill_rect(state, 10, 190, 40, 100, white);
    INKCELL_TEST_FAIL_IF(pixels[(size_t)195 * stride + 20 * 4] != 0U,
                         "a fill straddling the damage and the band should still be clipped");

    inkcell_capture_close(capture);
    record_success(test_name);
}
