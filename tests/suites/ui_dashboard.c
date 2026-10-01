#define _POSIX_C_SOURCE 200809L

/*
 * The two pieces a board is drawn with: a card filling a box, and a stat tile.
 *
 * What is worth holding is the contract each makes to the board around it rather than any
 * particular pixel - the golden sheet holds the pixels. A boxed card hands back the room under
 * its rows and never room outside its box, so a chart drawn there cannot overrun the tile; a
 * card that has more rows than its box holds keeps to the box; and a stat tile's figure steps
 * down a size rather than reporting a height for a figure that will not fit.
 */

#include "framework/inkcell_test.h"

#include "inkcell/ui/fb_capture.h"
#include "inkcell/ui/widgets.h"

static struct inkcell_capture *dash_open(struct inkcell_draw_state **state) {
    struct inkcell_capture *capture = NULL;
    if (inkcell_capture_open(&capture, 1600U, 1000U, INKCELL_SCALE(4)) < 0) {
        return NULL;
    }
    *state = inkcell_capture_state(capture);
    return capture;
}

static bool box_inside(struct inkcell_box inner, struct inkcell_box outer) {
    return inner.x >= outer.x && inner.y >= outer.y && inner.x + inner.w <= outer.x + outer.w &&
           inner.y + inner.h <= outer.y + outer.h;
}

INKCELL_TEST_CASE(card_in_hands_back_the_room_under_its_rows, unit) {
    struct inkcell_draw_state *state = NULL;
    struct inkcell_capture *capture = dash_open(&state);
    INKCELL_TEST_FAIL_IF(capture == NULL, "the capture should open");
    const struct inkcell_fb_layout layout = inkcell_fb_layout_begin(state, true, false);

    const struct inkcell_box box = {.x = 40, .y = 100, .w = 600, .h = 500};
    struct inkcell_fb_card card;
    inkcell_fb_card_begin(&card, INKCELL_FB_CARD_FILLED, INKCELL_ICON_NONE,
                          INKCELL_STR_COMMON_UNKNOWN_SHORT, INKCELL_TONE_NORMAL);
    const struct inkcell_box bare = inkcell_fb_draw_card_in(state, &layout, box, &card);
    INKCELL_TEST_FAIL_IF(inkcell_box_is_empty(bare),
                         "a heading with no rows is a frame in a box, with room in it");
    INKCELL_TEST_FAIL_IF(!box_inside(bare, box), "the room is inside the box");

    inkcell_fb_card_row_text(&card, INKCELL_TONE_NORMAL, INKCELL_STR_COMMON_UNKNOWN_SHORT, "1");
    inkcell_fb_card_row_text(&card, INKCELL_TONE_NORMAL, INKCELL_STR_COMMON_UNKNOWN_SHORT, "2");
    const struct inkcell_box rest = inkcell_fb_draw_card_in(state, &layout, box, &card);
    INKCELL_TEST_FAIL_IF(!box_inside(rest, box), "the room under rows is inside the box");
    INKCELL_TEST_FAIL_IF(rest.y != bare.y + 2 * layout.line,
                         "two rows take two lines off the top of the room");
    INKCELL_TEST_FAIL_IF(rest.x != bare.x || rest.w != bare.w,
                         "and the room is as wide as the rows either way");

    struct inkcell_fb_card none;
    inkcell_fb_card_begin(&none, INKCELL_FB_CARD_FILLED, INKCELL_ICON_NONE, INKCELL_STR_NONE,
                          INKCELL_TONE_NORMAL);
    INKCELL_TEST_FAIL_IF(!inkcell_box_is_empty(inkcell_fb_draw_card_in(state, &layout, box, &none)),
                         "a card with no heading and no rows is nothing");

    inkcell_capture_close(capture);
    record_success(test_name);
}

INKCELL_TEST_CASE(card_in_keeps_a_long_card_to_its_box, unit) {
    struct inkcell_draw_state *state = NULL;
    struct inkcell_capture *capture = dash_open(&state);
    INKCELL_TEST_FAIL_IF(capture == NULL, "the capture should open");
    const struct inkcell_fb_layout layout = inkcell_fb_layout_begin(state, true, false);

    struct inkcell_fb_card card;
    inkcell_fb_card_begin(&card, INKCELL_FB_CARD_FILLED, INKCELL_ICON_NONE,
                          INKCELL_STR_COMMON_UNKNOWN_SHORT, INKCELL_TONE_NORMAL);
    for (uint32_t i = 0U; i < INKCELL_FB_CARD_ROWS_MAX; ++i) {
        inkcell_fb_card_row_text(&card, INKCELL_TONE_NORMAL, INKCELL_STR_COMMON_UNKNOWN_SHORT, "x");
    }
    const struct inkcell_box box = {.x = 0, .y = 0, .w = 500, .h = 3 * layout.line};
    const struct inkcell_box rest = inkcell_fb_draw_card_in(state, &layout, box, &card);
    INKCELL_TEST_FAIL_IF(rest.y > box.y + box.h, "rows past the box are dropped, not drawn");
    INKCELL_TEST_FAIL_IF(rest.h != 0 && !box_inside(rest, box),
                         "and no room outside it is offered");

    inkcell_capture_close(capture);
    record_success(test_name);
}

INKCELL_TEST_CASE(stat_figure_steps_down_rather_than_overrunning, unit) {
    struct inkcell_draw_state *state = NULL;
    struct inkcell_capture *capture = dash_open(&state);
    INKCELL_TEST_FAIL_IF(capture == NULL, "the capture should open");
    const struct inkcell_fb_layout layout = inkcell_fb_layout_begin(state, true, false);

    struct inkcell_fb_stat stat = {
        .rect = {0, 0, 600, 200}, .label = INKCELL_STR_COMMON_UNKNOWN_SHORT, .value = "1234567"};
    const int wide = inkcell_fb_stat_height(state, &layout, &stat);
    stat.rect.w = 120;
    const int narrow = inkcell_fb_stat_height(state, &layout, &stat);
    INKCELL_TEST_FAIL_IF(wide <= 0, "a tile has a height");
    INKCELL_TEST_FAIL_IF(
        narrow >= wide, "a figure too wide for a narrow tile is set a size down, so it is shorter");

    stat.rect.w = 600;
    stat.picture = INKCELL_FB_STAT_METER;
    INKCELL_TEST_FAIL_IF(inkcell_fb_stat_height(state, &layout, &stat) <= wide,
                         "a meter under the figure costs room");
    stat.picture = INKCELL_FB_STAT_TREND;
    INKCELL_TEST_FAIL_IF(inkcell_fb_stat_height(state, &layout, &stat) != wide,
                         "a trend with no points draws nothing and costs nothing");

    inkcell_capture_close(capture);
    record_success(test_name);
}
