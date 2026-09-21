#define _POSIX_C_SOURCE 200809L

/*
 * The grid: the window over rows of tiles, and the press that steps through it.
 *
 * Two halves, and they are the two halves a grid adds to a list. The window is
 * `inkcell_grid_begin()` - a `struct inkcell_list` over the grid's rows, so what is tested here
 * is not the window arithmetic again (tests/suites/ui_layout.c has that) but the translation:
 * which *items* the rows it settled on hold, and what happens at the short last row every grid
 * whose count is not a multiple of its width ends on.
 *
 * The other half is the press. `struct inkcell_focus_run` grew a stride so that a screen can
 * hold one id and step it in two directions, and the cases below are the presses a cursor
 * index gets wrong: down is a whole row on rather than the next number, down from a tile with
 * nothing under it is the short last row rather than nothing at all, and sideways is the tile
 * beside this one rather than whatever the numbering puts next to it.
 *
 * Almost no pixels. What a tile *looks* like is the golden sheet's, and what it registers is
 * tests/suites/ui_focus_widgets.c - the one exception is the last case, which is here because
 * the sheet turned out not to cover it: a tile only shrinks its art when it is too cramped to
 * hold it, and none of the gallery's grids is that cramped, so the floor that shrinking stops
 * at had no picture watching it.
 */

#include "framework/inkcell_test.h"

#include "inkcell/ui/fb_capture.h"
#include "inkcell/ui/fb_draw.h"
#include "inkcell/ui/focus.h"
#include "inkcell/ui/layout.h"
#include "inkcell/ui/widgets/chrome.h"
#include "inkcell/ui/widgets/grid.h"

#include <stdlib.h>
#include <string.h>

/* The base a screen would give its tiles, and the shape of the grid most cases below use: eleven
   items four across, which is two full rows and a row of three. */
#define GRID_BASE 100U
#define GRID_COUNT 11U
#define GRID_COLS 4U

/* ---- the window ------------------------------------------------------------------------------ */

INKCELL_TEST_CASE(grid_window_holds_the_cursors_row, unit) {
    /* Two rows fit of the three the count occupies, and the cursor is in the last of them. */
    struct inkcell_grid grid = inkcell_grid_begin(GRID_COUNT, 9U, GRID_COLS, 2U);
    INKCELL_TEST_FAIL_IF(grid.rows.count != 3U, "eleven items four across should be three rows");
    INKCELL_TEST_FAIL_IF(grid.rows.first != 1U, "the window should have scrolled off row zero");

    uint32_t index = 0U;
    uint32_t seen = 0U;
    uint32_t first = 0U;
    uint32_t last = 0U;
    while (inkcell_grid_next(&grid, &index)) {
        if (seen == 0U) {
            first = index;
        }
        last = index;
        seen += 1U;
    }
    INKCELL_TEST_FAIL_IF(first != 4U, "the walk should start at the first item of the first row");
    INKCELL_TEST_FAIL_IF(last != 10U, "the walk should stop at the last item, not the last cell");
    INKCELL_TEST_FAIL_IF(seen != 7U, "two rows of four ending three short is seven tiles");
    record_success(test_name);
}

INKCELL_TEST_CASE(grid_sideways_costs_no_scroll, unit) {
    /* Every tile in one row must put the window in the same place, or a press across a row
       would scroll the panel under the reader on the way. */
    uint32_t first = 0U;
    for (uint32_t cursor = 4U; cursor < 8U; ++cursor) {
        const struct inkcell_grid grid = inkcell_grid_begin(GRID_COUNT, cursor, GRID_COLS, 2U);
        if (cursor == 4U) {
            first = grid.rows.first;
        }
        INKCELL_TEST_FAIL_IF(grid.rows.first != first,
                             "a cursor moving along a row should not move the window");
        INKCELL_TEST_FAIL_IF(inkcell_grid_row_of(&grid, cursor) != 1U,
                             "items four through seven are all in row one");
    }
    record_success(test_name);
}

INKCELL_TEST_CASE(grid_degenerate_shapes_draw_nothing_rather_than_dividing, unit) {
    /* A grid zero columns wide is a division, so it is read as one column - and a grid with
       nothing in it, or no room to put it, walks zero times rather than once. */
    struct inkcell_grid narrow = inkcell_grid_begin(3U, 2U, 0U, 4U);
    INKCELL_TEST_FAIL_IF(narrow.cols != 1U, "no columns should be read as one");
    INKCELL_TEST_FAIL_IF(narrow.rows.count != 3U, "one column makes a row per item");

    uint32_t index = 0U;
    struct inkcell_grid empty = inkcell_grid_begin(0U, 4U, GRID_COLS, 2U);
    INKCELL_TEST_FAIL_IF(empty.cursor != 0U, "a cursor past the end should clamp to zero");
    INKCELL_TEST_FAIL_IF(inkcell_grid_next(&empty, &index), "an empty grid should draw nothing");
    INKCELL_TEST_FAIL_IF(inkcell_grid_is_cursor(&empty, 0U),
                         "nothing is the cursor when there is nothing");

    struct inkcell_grid airless = inkcell_grid_begin(GRID_COUNT, 0U, GRID_COLS, 0U);
    INKCELL_TEST_FAIL_IF(inkcell_grid_next(&airless, &index),
                         "a window with no rows in it should draw nothing");
    record_success(test_name);
}

INKCELL_TEST_CASE(grid_cursor_is_the_item_not_the_row, unit) {
    struct inkcell_grid grid = inkcell_grid_begin(GRID_COUNT, 6U, GRID_COLS, 3U);
    INKCELL_TEST_FAIL_IF(!inkcell_grid_is_cursor(&grid, 6U), "the cursor is the item it names");
    INKCELL_TEST_FAIL_IF(inkcell_grid_is_cursor(&grid, 4U),
                         "the rest of the cursor's row is not the cursor");
    INKCELL_TEST_FAIL_IF(inkcell_grid_rows(&grid) != 3U,
                         "the short last row is a row like any other");
    record_success(test_name);
}

/* ---- the press --------------------------------------------------------------------------------
 *
 * Every case here passes a NULL map, which is the point: a run answers for the part of a screen
 * that is not drawn, and the cases below are all presses onto a tile the window may well have
 * left off the panel. INKCELL_FOCUS_NONE therefore means "the run had nothing that way and the
 * geometry would have decided" - which on a grid's edge is what leaving it means.
 */

static const struct inkcell_focus_run k_grid_run = {
    .base = GRID_BASE,
    .count = GRID_COUNT,
    .stride = GRID_COLS,
};

static uint32_t grid_press(uint32_t id, enum inkcell_focus_dir dir) {
    return inkcell_focus_step(NULL, id, dir, &k_grid_run, 1U);
}

INKCELL_TEST_CASE(grid_press_steps_a_row_at_a_time_down_and_one_across, unit) {
    INKCELL_TEST_FAIL_IF(grid_press(GRID_BASE + 1U, INKCELL_FOCUS_RIGHT) != GRID_BASE + 2U,
                         "right is the next item");
    INKCELL_TEST_FAIL_IF(grid_press(GRID_BASE + 1U, INKCELL_FOCUS_LEFT) != GRID_BASE,
                         "left is the one before");
    INKCELL_TEST_FAIL_IF(grid_press(GRID_BASE + 1U, INKCELL_FOCUS_DOWN) != GRID_BASE + 5U,
                         "down is a whole row on");
    INKCELL_TEST_FAIL_IF(grid_press(GRID_BASE + 5U, INKCELL_FOCUS_UP) != GRID_BASE + 1U,
                         "up is a whole row back");
    record_success(test_name);
}

INKCELL_TEST_CASE(grid_press_sideways_stays_in_its_row, unit) {
    /* The press that a run of one column gets wrong in the other direction: item 3 is the end of
       row zero and item 4 is the start of row one, and they are next to each other only in the
       numbering. */
    INKCELL_TEST_FAIL_IF(grid_press(GRID_BASE + 3U, INKCELL_FOCUS_RIGHT) != INKCELL_FOCUS_NONE,
                         "right at the end of a row is the edge of the grid");
    INKCELL_TEST_FAIL_IF(grid_press(GRID_BASE + 4U, INKCELL_FOCUS_LEFT) != INKCELL_FOCUS_NONE,
                         "left at the start of a row is the edge of the grid");
    record_success(test_name);
}

INKCELL_TEST_CASE(grid_press_down_lands_on_a_short_last_row, unit) {
    /* Eleven items four across leaves row two holding 8, 9 and 10. Nothing is under item 7, and
       "nothing" is the wrong answer: there is more grid down there. */
    INKCELL_TEST_FAIL_IF(grid_press(GRID_BASE + 7U, INKCELL_FOCUS_DOWN) != GRID_BASE + 10U,
                         "down from over the gap should land on the last tile of the row");
    INKCELL_TEST_FAIL_IF(grid_press(GRID_BASE + 6U, INKCELL_FOCUS_DOWN) != GRID_BASE + 10U,
                         "so should down from the tile beside it");
    INKCELL_TEST_FAIL_IF(grid_press(GRID_BASE + 4U, INKCELL_FOCUS_DOWN) != GRID_BASE + 8U,
                         "a tile with something under it still steps straight down");
    INKCELL_TEST_FAIL_IF(grid_press(GRID_BASE + 9U, INKCELL_FOCUS_DOWN) != INKCELL_FOCUS_NONE,
                         "down from the last row is the bottom of the grid");
    INKCELL_TEST_FAIL_IF(grid_press(GRID_BASE + 2U, INKCELL_FOCUS_UP) != INKCELL_FOCUS_NONE,
                         "up from the first row is the top of it");
    record_success(test_name);
}

INKCELL_TEST_CASE(grid_press_walks_past_the_tiles_it_cannot_stand_on, unit) {
    /* A grid with gaps in it says so the way a list with headings in it does. Item 5 is not a
       place to stand, so a press right from 4 is 6 and a press down from 1 is 9. */
    static const uint8_t k_stands[GRID_COUNT] = {1U, 1U, 1U, 1U, 1U, 0U, 1U, 1U, 1U, 1U, 1U};
    const struct inkcell_focus_run run = {
        .base = GRID_BASE, .count = GRID_COUNT, .stride = GRID_COLS, .focusable = k_stands};
    INKCELL_TEST_FAIL_IF(inkcell_focus_step(NULL, GRID_BASE + 4U, INKCELL_FOCUS_RIGHT, &run, 1U) !=
                             GRID_BASE + 6U,
                         "a press should step past a tile that is not a place to stand");
    INKCELL_TEST_FAIL_IF(inkcell_focus_step(NULL, GRID_BASE + 1U, INKCELL_FOCUS_DOWN, &run, 1U) !=
                             GRID_BASE + 9U,
                         "and should keep going down the column rather than stopping in the gap");
    record_success(test_name);
}

INKCELL_TEST_CASE(grid_press_down_walks_back_along_a_short_row, unit) {
    /* The clamp into a short last row has to land on a tile the cursor can *stand* on, not
       merely on the last one. Item 10 is the end of the row and is not a place to stand, so a
       press down from item 7 means item 9 - and giving up there would leave the grid while the
       row it was aiming at still had something in it. */
    static const uint8_t k_stands[GRID_COUNT] = {1U, 1U, 1U, 1U, 1U, 1U, 1U, 1U, 1U, 1U, 0U};
    const struct inkcell_focus_run run = {
        .base = GRID_BASE, .count = GRID_COUNT, .stride = GRID_COLS, .focusable = k_stands};
    INKCELL_TEST_FAIL_IF(inkcell_focus_step(NULL, GRID_BASE + 7U, INKCELL_FOCUS_DOWN, &run, 1U) !=
                             GRID_BASE + 9U,
                         "the clamp should walk back to the last tile that can be stood on");

    /* And when nothing in that row can be: the press is the bottom of the grid after all, and
       the walk must stop at the row's own first column rather than climbing into the row
       above. */
    static const uint8_t k_empty_row[GRID_COUNT] = {1U, 1U, 1U, 1U, 1U, 1U, 1U, 1U, 0U, 0U, 0U};
    const struct inkcell_focus_run bare = {
        .base = GRID_BASE, .count = GRID_COUNT, .stride = GRID_COLS, .focusable = k_empty_row};
    INKCELL_TEST_FAIL_IF(inkcell_focus_step(NULL, GRID_BASE + 7U, INKCELL_FOCUS_DOWN, &bare, 1U) !=
                             INKCELL_FOCUS_NONE,
                         "a short row with nothing to stand on is the bottom of the grid");
    record_success(test_name);
}

INKCELL_TEST_CASE(grid_run_without_a_stride_is_still_a_column, unit) {
    /* Every list in the tree declares a run with no stride at all, and those runs must answer
       exactly as they did: down and up by one, and nothing to say about sideways. */
    const struct inkcell_focus_run list = {.base = GRID_BASE, .count = GRID_COUNT};
    INKCELL_TEST_FAIL_IF(inkcell_focus_step(NULL, GRID_BASE + 4U, INKCELL_FOCUS_DOWN, &list, 1U) !=
                             GRID_BASE + 5U,
                         "a run with no stride steps one item down");
    INKCELL_TEST_FAIL_IF(inkcell_focus_step(NULL, GRID_BASE + 4U, INKCELL_FOCUS_RIGHT, &list, 1U) !=
                             INKCELL_FOCUS_NONE,
                         "and leaves sideways to the geometry");

    const struct inkcell_focus_run single = {.base = GRID_BASE, .count = GRID_COUNT, .stride = 1U};
    INKCELL_TEST_FAIL_IF(
        inkcell_focus_step(NULL, GRID_BASE + 4U, INKCELL_FOCUS_DOWN, &single, 1U) != GRID_BASE + 5U,
        "a stride of one is a column too");
    INKCELL_TEST_FAIL_IF(inkcell_focus_step(NULL, GRID_BASE + 4U, INKCELL_FOCUS_RIGHT, &single,
                                            1U) != INKCELL_FOCUS_NONE,
                         "and says nothing about sideways either");
    record_success(test_name);
}

INKCELL_TEST_CASE(grid_press_outside_the_run_is_the_geometrys, unit) {
    /* An id that is not in the run at all - a chip above the grid, a button below it - must fall
       through to the finder rather than being stepped as if it were a tile. */
    INKCELL_TEST_FAIL_IF(grid_press(GRID_BASE - 1U, INKCELL_FOCUS_DOWN) != INKCELL_FOCUS_NONE,
                         "an id below the base is not in this run");
    INKCELL_TEST_FAIL_IF(grid_press(GRID_BASE + GRID_COUNT, INKCELL_FOCUS_UP) != INKCELL_FOCUS_NONE,
                         "and neither is one past its end");
    record_success(test_name);
}

/* ---- what a cramped tile will not do -------------------------------------------------------- */

/*
 * A tile too small for its initials shrinks them, and stops at one whole step.
 *
 * Both kinds of art step *down* until they fit, and the floor is the point: below a whole step
 * is a size the type scale cannot name, and initials drawn there are not a smaller label but an
 * unreadable one. The two loops are separate - an icon is measured by its drawn box and
 * initials by their text width - so the floor is stated twice and can be got wrong once, which
 * is exactly what happened when the scale learned to count quarters and one of the two kept
 * walking raw units down to 1.
 *
 * Measured as a difference rather than as an extent: a tile draws a container under its art, so
 * the ink of the tile as a whole says nothing about the size of the letters on it. The same
 * grid is drawn twice - once with no art, once with initials - and what the second put on the
 * panel that the first did not is the initials and nothing else.
 */
INKCELL_TEST_CASE(grid_cramped_initials_stop_at_a_whole_step, unit) {
    struct inkcell_capture *capture = NULL;
    if (inkcell_capture_open(&capture, 1024U, 768U, INKCELL_SCALE(4)) < 0) {
        record_failure(test_name, "the capture should open");
        return;
    }
    struct inkcell_backend_fb_state *state = inkcell_capture_state(capture);

    uint32_t width = 0U, height = 0U;
    size_t stride = 0U;
    const uint8_t *pixels = inkcell_capture_pixels(capture, &width, &height, &stride);
    uint8_t *without = NULL;
    const char *failure = NULL;
    struct inkcell_fb_rect box = {0, 0, 0, 0};

    /* Twelve across and twelve down on a panel this size is a tile of about two cells square:
       far too small for two letters at the body scale, and small enough that the loop walks all
       the way to its floor rather than stopping part way. Smaller than this and the tile has no
       room for the letters even at the floor, which is a tile that draws nothing and would make
       the measurement below vacuous. */
    const struct inkcell_fb_grid_style style = {.cols = 12U, .rows = 12U};

    for (int pass = 0; pass < 2 && failure == NULL; ++pass) {
        inkcell_fb_clear(state, inkcell_fb_color(state, INKCELL_COLOR_BG));
        struct inkcell_fb_layout layout = inkcell_fb_layout_begin(state, false, false);
        struct inkcell_fb_grid grid = inkcell_fb_grid_begin(state, &layout, 144U, 0U, &style);

        uint32_t index = 0U;
        while (inkcell_fb_grid_next(&grid, &index)) {
            if (index == 0U) {
                box = inkcell_fb_grid_tile_box(&grid);
            }
            const struct inkcell_fb_tile tile = {
                .art = pass == 0 ? INKCELL_FB_TILE_ART_NONE : INKCELL_FB_TILE_ART_INITIALS,
                .text = "WW",
                .tone = INKCELL_TONE_PRIMARY,
            };
            inkcell_fb_grid_tile(state, &grid, index, &tile);
        }

        if (pass == 0) {
            without = malloc((size_t)height * stride);
            if (without == NULL) {
                failure = "frame allocation failed";
            } else {
                memcpy(without, pixels, (size_t)height * stride);
            }
        }
    }

    if (failure == NULL && (box.w <= 0 || box.h <= 0)) {
        failure = "the grid drew no tile to measure";
    }

    if (failure == NULL) {
        /* The rows of the first tile that the initials changed. */
        int top = -1;
        int bottom = -1;
        /* Clamped to the panel at both ends: a grid's origin sits a hairline above the body, so
           the first row's box starts a couple of pixels off the top of the frame. */
        const int y0 = box.y > 0 ? box.y : 0;
        const int x0 = box.x > 0 ? box.x : 0;
        for (int y = y0; y < box.y + box.h && y < (int)height; ++y) {
            const size_t row = (size_t)y * stride;
            for (int x = x0; x < box.x + box.w && x < (int)width; ++x) {
                const size_t at = row + (size_t)x * 4U;
                if (memcmp(without + at, pixels + at, 3U) != 0) {
                    if (top < 0) {
                        top = y;
                    }
                    bottom = y;
                    break;
                }
            }
        }
        if (top < 0) {
            failure = "a cramped tile drew no initials at all";
        } else {
            /* The floor, in pixels: what a capital stands at one whole step. Against the cap
               rather than the cell because that is the ink a letter actually puts down. */
            const int floor_px = inkcell_font_cap(inkcell_fb_font(state), INKCELL_SCALE(1));
            if (bottom - top + 1 < floor_px) {
                failure = "a cramped tile shrank its initials below one whole step";
            }
        }
    }

    free(without);
    inkcell_capture_close(capture);
    INKCELL_TEST_FAIL_IF(failure != NULL, failure);
    record_success(test_name);
}
