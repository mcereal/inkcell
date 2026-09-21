#define _POSIX_C_SOURCE 200809L

/*
 * The directional focus finder: rectangles in, "what lies that way" out.
 *
 * Every case here is a *layout*, written in pixels the way a screen would have come out, and
 * then a press. That is deliberate: the bug this file exists to catch is never "the function
 * returned the wrong number", it is "the cursor left the row it was travelling along", and the
 * only way to see that is to lay out something with a row in it and a trap underneath.
 *
 * The traps are the ones a real panel sets - a chip strip over a card, a tall box beside two
 * short ones, a grid whose columns do not line up - because a finder that only ever meets a
 * tidy grid is a finder that has not met a screen.
 */

#include "framework/inkcell_test.h"

#include "inkcell/ui/focus.h"
#include "inkcell/ui/key.h"

#include <limits.h>

/* Ids are the screen's own vocabulary; these stand in for a screen's enum. */
enum {
    ID_A = 1,
    ID_B,
    ID_C,
    ID_D,
    ID_E,
    ID_F,
    ID_G,
    ID_H,
    ID_I,
};

#define FOCUS_STORAGE 16U

INKCELL_TEST_CASE(focus_grid_walks_by_geometry, unit) {
    struct inkcell_focus_item storage[FOCUS_STORAGE];
    struct inkcell_focus_map map;

    /* Three columns of 100 across, three rows of 60 down, with a 10 px gap either way: the
       keypad shape nothing in this toolkit could express before. */
    inkcell_focus_begin(&map, storage, FOCUS_STORAGE);
    const uint32_t cells[3][3] = {{ID_A, ID_B, ID_C}, {ID_D, ID_E, ID_F}, {ID_G, ID_H, ID_I}};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            (void)inkcell_focus_add(&map, cells[row][col], col * 110, row * 70, 100, 60);
        }
    }
    INKCELL_TEST_FAIL_IF(map.count != 9U || map.dropped != 0U, "nine cells should have gone in");

    INKCELL_TEST_FAIL_IF(inkcell_focus_find(&map, ID_A, INKCELL_FOCUS_RIGHT) != ID_B,
                         "right from the top left should be the cell beside it");
    INKCELL_TEST_FAIL_IF(inkcell_focus_find(&map, ID_A, INKCELL_FOCUS_DOWN) != ID_D,
                         "down from the top left should be the cell under it");
    INKCELL_TEST_FAIL_IF(inkcell_focus_find(&map, ID_E, INKCELL_FOCUS_LEFT) != ID_D,
                         "left from the middle should be the cell beside it");
    INKCELL_TEST_FAIL_IF(inkcell_focus_find(&map, ID_E, INKCELL_FOCUS_UP) != ID_B,
                         "up from the middle should be the cell above it");

    /* The edges are not an error and not a wrap: they are the edge of the screen, and the press
       is going spare for whatever the screen wants to do with it. */
    INKCELL_TEST_FAIL_IF(inkcell_focus_find(&map, ID_C, INKCELL_FOCUS_RIGHT) != INKCELL_FOCUS_NONE,
                         "right from the last column should find nothing");
    INKCELL_TEST_FAIL_IF(inkcell_focus_find(&map, ID_A, INKCELL_FOCUS_UP) != INKCELL_FOCUS_NONE,
                         "up from the first row should find nothing");
    record_success(test_name);
}

/*
 * The rule that makes a row a row.
 *
 * ID_B is straight ahead and 110 px away; ID_D is off the row and its nearest corner is 30 px
 * away. A finder measuring plain distance hands the cursor to ID_D and the reader watches it
 * fall out of the strip they were walking along. The beam is what stops that, and this case is
 * the reason the weight exists at all.
 */
INKCELL_TEST_CASE(focus_keeps_to_the_row_it_is_on, unit) {
    struct inkcell_focus_item storage[FOCUS_STORAGE];
    struct inkcell_focus_map map;

    inkcell_focus_begin(&map, storage, FOCUS_STORAGE);
    (void)inkcell_focus_add(&map, ID_A, 0, 100, 100, 40);   /* the chip the cursor is on */
    (void)inkcell_focus_add(&map, ID_B, 110, 100, 100, 40); /* the next chip along */
    (void)inkcell_focus_add(&map, ID_D, 30, 170, 300, 40);  /* a card just below the strip */

    INKCELL_TEST_FAIL_IF(inkcell_focus_find(&map, ID_A, INKCELL_FOCUS_RIGHT) != ID_B,
                         "right should stay in the strip rather than dive at the nearest box");
    /* And the way *out* of the strip is the press that means out. */
    INKCELL_TEST_FAIL_IF(inkcell_focus_find(&map, ID_A, INKCELL_FOCUS_DOWN) != ID_D,
                         "down should leave the strip for the card under it");
    record_success(test_name);
}

/*
 * The ragged case: a tall card beside two stacked buttons.
 *
 * Neither button is level with the card's centre and the card is not level with either of them,
 * so a finder that asked "is all of it past my edge, and are our centres in line" would answer
 * that there is nothing to the right of either button. There plainly is.
 */
INKCELL_TEST_CASE(focus_reaches_a_box_that_overlaps_two_rows, unit) {
    struct inkcell_focus_item storage[FOCUS_STORAGE];
    struct inkcell_focus_map map;

    inkcell_focus_begin(&map, storage, FOCUS_STORAGE);
    (void)inkcell_focus_add(&map, ID_A, 0, 0, 120, 50);    /* upper button */
    (void)inkcell_focus_add(&map, ID_B, 0, 60, 120, 50);   /* lower button */
    (void)inkcell_focus_add(&map, ID_C, 140, 0, 200, 110); /* the card beside them both */

    INKCELL_TEST_FAIL_IF(inkcell_focus_find(&map, ID_A, INKCELL_FOCUS_RIGHT) != ID_C,
                         "the upper button should reach the card beside it");
    INKCELL_TEST_FAIL_IF(inkcell_focus_find(&map, ID_B, INKCELL_FOCUS_RIGHT) != ID_C,
                         "the lower button should reach the same card");
    /* Coming back the other way is a choice between two, and the beam makes it: the card's own
       row overlaps both buttons, so the nearer centre decides. */
    INKCELL_TEST_FAIL_IF(inkcell_focus_find(&map, ID_C, INKCELL_FOCUS_LEFT) != ID_A,
                         "left from the card should land on the button nearest its centre line");
    record_success(test_name);
}

/*
 * A card with two verbs on its heading line, over its own rows: the shape card.h describes and
 * the shape no screen here could navigate. The verbs are a row of their own inside a column,
 * which is two directions at once and exactly what a cursor index cannot hold.
 */
INKCELL_TEST_CASE(focus_moves_between_a_cards_actions_and_out_of_it, unit) {
    struct inkcell_focus_item storage[FOCUS_STORAGE];
    struct inkcell_focus_map map;

    inkcell_focus_begin(&map, storage, FOCUS_STORAGE);
    (void)inkcell_focus_add(&map, ID_A, 16, 40, 120, 56);  /* the row above the card */
    (void)inkcell_focus_add(&map, ID_B, 380, 110, 90, 32); /* the card's first verb */
    (void)inkcell_focus_add(&map, ID_C, 480, 110, 90, 32); /* its second */
    (void)inkcell_focus_add(&map, ID_D, 16, 190, 554, 56); /* the row below the card */

    INKCELL_TEST_FAIL_IF(inkcell_focus_find(&map, ID_B, INKCELL_FOCUS_RIGHT) != ID_C,
                         "the verbs should be reachable from each other");
    INKCELL_TEST_FAIL_IF(inkcell_focus_find(&map, ID_C, INKCELL_FOCUS_LEFT) != ID_B,
                         "and back again");
    INKCELL_TEST_FAIL_IF(inkcell_focus_find(&map, ID_C, INKCELL_FOCUS_DOWN) != ID_D,
                         "down from a verb should leave the card");
    INKCELL_TEST_FAIL_IF(inkcell_focus_find(&map, ID_B, INKCELL_FOCUS_UP) != ID_A,
                         "up from a verb should reach the row above the card");
    record_success(test_name);
}

/*
 * When nothing is in line, the press still leaves: the beam decides *between* candidates, it
 * does not veto the ones that are off it.
 *
 * This is the press that makes a form with a chip row in it navigable. There is nothing to the
 * right of the field, and what the reader means by pressing right is "the thing over there" -
 * so a finder that answered "nothing is level with you" would leave half a screen unreachable
 * and hand the screen back its own arithmetic to write.
 */
INKCELL_TEST_CASE(focus_leaves_a_row_when_nothing_is_level_with_it, unit) {
    struct inkcell_focus_item storage[FOCUS_STORAGE];
    struct inkcell_focus_map map;

    inkcell_focus_begin(&map, storage, FOCUS_STORAGE);
    (void)inkcell_focus_add(&map, ID_A, 16, 120, 300, 56); /* the field */
    (void)inkcell_focus_add(&map, ID_B, 360, 40, 80, 32);  /* a verb, up and to the right */
    (void)inkcell_focus_add(&map, ID_C, 360, 240, 80, 32); /* another, down and to the right */

    INKCELL_TEST_FAIL_IF(inkcell_focus_find(&map, ID_A, INKCELL_FOCUS_RIGHT) != ID_B,
                         "right should reach the nearer of the two rather than nothing at all");
    record_success(test_name);
}

/*
 * The beam is absolute sideways and released by distance vertically, which is Android's
 * asymmetry and is stated in the contract rather than merely happening.
 *
 * Sideways there is no argument: a press along a row is travelling along that row, and the
 * nearer thing off it can wait for the press that means "off it". Vertically the release is the
 * one that keeps a cursor from leaping the height of a sparse panel to stay in a column - the
 * in-line cell here is two hundred pixels down and the diagonal one is ten.
 */
INKCELL_TEST_CASE(focus_beam_is_absolute_sideways_and_not_vertically, unit) {
    struct inkcell_focus_item storage[FOCUS_STORAGE];
    struct inkcell_focus_map map;

    inkcell_focus_begin(&map, storage, FOCUS_STORAGE);
    (void)inkcell_focus_add(&map, ID_A, 0, 0, 20, 20);
    (void)inkcell_focus_add(&map, ID_B, 200, 0, 20, 20); /* in line, far */
    (void)inkcell_focus_add(&map, ID_C, 30, 30, 20, 20); /* off the row, near */
    INKCELL_TEST_FAIL_IF(inkcell_focus_find(&map, ID_A, INKCELL_FOCUS_RIGHT) != ID_B,
                         "a sideways press should stay on its row however far along the next "
                         "thing on it is");

    inkcell_focus_begin(&map, storage, FOCUS_STORAGE);
    (void)inkcell_focus_add(&map, ID_A, 0, 0, 20, 20);
    (void)inkcell_focus_add(&map, ID_B, 0, 200, 20, 20); /* in line, far */
    (void)inkcell_focus_add(&map, ID_C, 30, 30, 20, 20); /* off the column, near */
    INKCELL_TEST_FAIL_IF(inkcell_focus_find(&map, ID_A, INKCELL_FOCUS_DOWN) != ID_C,
                         "a vertical press should take the near diagonal over a column two "
                         "hundred pixels long");
    record_success(test_name);
}

/*
 * Wrapping, and the property it is defined by: it lands where holding the other direction would
 * have. The second strip below is the part worth asserting - a wrap that walked the whole map
 * would come back round onto another row, which is a cursor teleporting rather than wrapping.
 */
INKCELL_TEST_CASE(focus_wraps_along_the_row_and_no_further, unit) {
    struct inkcell_focus_item storage[FOCUS_STORAGE];
    struct inkcell_focus_map map;

    inkcell_focus_begin(&map, storage, FOCUS_STORAGE);
    (void)inkcell_focus_add(&map, ID_A, 0, 0, 90, 40);
    (void)inkcell_focus_add(&map, ID_B, 100, 0, 90, 40);
    (void)inkcell_focus_add(&map, ID_C, 200, 0, 90, 40);
    /* A second strip under the first, and the same width, so that the end of either row really
       is the end of the screen rather than a step diagonally onto the other one. */
    (void)inkcell_focus_add(&map, ID_D, 0, 60, 90, 40);
    (void)inkcell_focus_add(&map, ID_E, 100, 60, 90, 40);
    (void)inkcell_focus_add(&map, ID_F, 200, 60, 90, 40);

    INKCELL_TEST_FAIL_IF(inkcell_focus_find_wrapping(&map, ID_C, INKCELL_FOCUS_RIGHT) != ID_A,
                         "right at the end of a strip should come back to its start");
    INKCELL_TEST_FAIL_IF(inkcell_focus_find_wrapping(&map, ID_A, INKCELL_FOCUS_LEFT) != ID_C,
                         "and left at the start should reach its end");
    INKCELL_TEST_FAIL_IF(inkcell_focus_find_wrapping(&map, ID_F, INKCELL_FOCUS_RIGHT) != ID_D,
                         "a wrap should stay in the row it wrapped from");
    /* An unwrapped press is untouched by any of this: wrapping is what happens at the edge, not
       a different way of moving. */
    INKCELL_TEST_FAIL_IF(inkcell_focus_find_wrapping(&map, ID_A, INKCELL_FOCUS_RIGHT) != ID_B,
                         "a press with somewhere to go should go there");
    /* And a column wraps the same way a row does, which is the one thing a screen holding a
       cursor index got for free and would have been sorry to lose. */
    INKCELL_TEST_FAIL_IF(inkcell_focus_find_wrapping(&map, ID_D, INKCELL_FOCUS_DOWN) != ID_A,
                         "down at the bottom of a column should come back to its top");
    record_success(test_name);
}

/*
 * The wrap is in line only, and this is the layout that says so: the row below starts further
 * left than the row being wrapped.
 *
 * A wrap implemented as "hold the other direction until the screen runs out" lands on ID_C
 * here, because the last of those presses has nothing in line ahead of it and is entitled to
 * leave the row - which is the right answer for a press and the wrong one for a wrap. Coming
 * round to a row the reader was not on has not brought them back to the start, it has lost
 * their place.
 */
INKCELL_TEST_CASE(focus_wrapping_ignores_a_row_that_reaches_further_back, unit) {
    struct inkcell_focus_item storage[FOCUS_STORAGE];
    struct inkcell_focus_map map;

    inkcell_focus_begin(&map, storage, FOCUS_STORAGE);
    (void)inkcell_focus_add(&map, ID_A, 0, 0, 90, 40);
    (void)inkcell_focus_add(&map, ID_B, 100, 0, 90, 40);
    (void)inkcell_focus_add(&map, ID_C, -10, 60, 90, 40);

    INKCELL_TEST_FAIL_IF(inkcell_focus_find_wrapping(&map, ID_B, INKCELL_FOCUS_RIGHT) != ID_A,
                         "a wrap should come round to the start of its own row");
    record_success(test_name);
}

/* One thing on a row has nowhere to wrap to, and says so rather than answering with itself: a
   caller that assigned the answer unconditionally would otherwise never know the press was
   spare. */
INKCELL_TEST_CASE(focus_wrapping_alone_on_a_row_finds_nothing, unit) {
    struct inkcell_focus_item storage[FOCUS_STORAGE];
    struct inkcell_focus_map map;

    inkcell_focus_begin(&map, storage, FOCUS_STORAGE);
    (void)inkcell_focus_add(&map, ID_A, 0, 0, 90, 40);
    (void)inkcell_focus_add(&map, ID_B, 0, 60, 90, 40);

    INKCELL_TEST_FAIL_IF(inkcell_focus_find_wrapping(&map, ID_A, INKCELL_FOCUS_RIGHT) !=
                             INKCELL_FOCUS_NONE,
                         "a lone chip has nothing to wrap onto");
    INKCELL_TEST_FAIL_IF(inkcell_focus_find_wrapping(&map, ID_A, INKCELL_FOCUS_DOWN) != ID_B,
                         "a direction with somewhere to go is unaffected");
    record_success(test_name);
}

/*
 * What the map refuses, and why each one is a mistake rather than a state.
 *
 * The full case is the one with a number attached: a screen that outgrew its array draws a
 * button nobody can select, which reads as a dead control rather than as a bug, so the count is
 * there for a test to assert on.
 */
INKCELL_TEST_CASE(focus_refuses_what_cannot_be_reached, unit) {
    struct inkcell_focus_item storage[2];
    struct inkcell_focus_map map;

    inkcell_focus_begin(&map, storage, 2U);
    INKCELL_TEST_FAIL_IF(inkcell_focus_add(&map, INKCELL_FOCUS_NONE, 0, 0, 10, 10),
                         "the id meaning nothing should not name something");
    INKCELL_TEST_FAIL_IF(inkcell_focus_add(&map, ID_A, 0, 0, 0, 10),
                         "a box with no width cannot be found by an eye or a cursor");
    INKCELL_TEST_FAIL_IF(!inkcell_focus_add(&map, ID_A, 0, 0, 10, 10), "a real box should go in");
    INKCELL_TEST_FAIL_IF(inkcell_focus_add(&map, ID_A, 40, 0, 10, 10),
                         "one id should not name two boxes");
    INKCELL_TEST_FAIL_IF(map.dropped != 0U, "none of those were for want of room");

    INKCELL_TEST_FAIL_IF(!inkcell_focus_add(&map, ID_B, 20, 0, 10, 10), "the array holds two");
    INKCELL_TEST_FAIL_IF(inkcell_focus_add(&map, ID_C, 40, 0, 10, 10), "and refuses the third");
    INKCELL_TEST_FAIL_IF(map.dropped != 1U, "the refusal for want of room should be counted");
    INKCELL_TEST_FAIL_IF(map.count != 2U, "a refused add should not have been stored");
    record_success(test_name);
}

/*
 * The screen changed under the cursor. This is the pair of calls a screen makes after laying
 * out, and the reason `inkcell_focus_rect_of()` is worth keeping beside the focused id.
 */
INKCELL_TEST_CASE(focus_survives_the_thing_it_was_on_disappearing, unit) {
    struct inkcell_focus_item storage[FOCUS_STORAGE];
    struct inkcell_focus_map map;
    struct inkcell_focus_rect remembered = {0, 0, 0, 0};

    inkcell_focus_begin(&map, storage, FOCUS_STORAGE);
    (void)inkcell_focus_add(&map, ID_A, 0, 0, 200, 50);
    (void)inkcell_focus_add(&map, ID_B, 0, 60, 200, 50);
    (void)inkcell_focus_add(&map, ID_C, 0, 120, 200, 50);
    INKCELL_TEST_FAIL_IF(!inkcell_focus_rect_of(&map, ID_B, &remembered),
                         "the cursor's rectangle should be readable while it is drawn");

    /* The next frame: the middle row is gone, and the rows below it have moved up into it. */
    inkcell_focus_begin(&map, storage, FOCUS_STORAGE);
    (void)inkcell_focus_add(&map, ID_A, 0, 0, 200, 50);
    (void)inkcell_focus_add(&map, ID_C, 0, 60, 200, 50);

    INKCELL_TEST_FAIL_IF(inkcell_focus_has(&map, ID_B), "the row is gone");
    INKCELL_TEST_FAIL_IF(inkcell_focus_find(&map, ID_B, INKCELL_FOCUS_DOWN) != INKCELL_FOCUS_NONE,
                         "a cursor on nothing should not be moved somewhere plausible");
    INKCELL_TEST_FAIL_IF(inkcell_focus_nearest(&map, remembered) != ID_C,
                         "the cursor should land on whatever took the place it was looking at");
    record_success(test_name);
}

/* A box that overlaps where the cursor was is where the cursor was, however much larger it is
   than the box it replaced - measured between the boxes and not between their centres. */
INKCELL_TEST_CASE(focus_nearest_prefers_the_box_it_is_standing_in, unit) {
    struct inkcell_focus_item storage[FOCUS_STORAGE];
    struct inkcell_focus_map map;
    const struct inkcell_focus_rect was = {0, 60, 200, 50};

    inkcell_focus_begin(&map, storage, FOCUS_STORAGE);
    /* The row grew a supporting line and is now twice as tall; its centre moved 25 px down,
       which is further from `was` than the neighbour above it. */
    (void)inkcell_focus_add(&map, ID_A, 0, 0, 200, 50);
    (void)inkcell_focus_add(&map, ID_B, 0, 60, 200, 100);

    INKCELL_TEST_FAIL_IF(inkcell_focus_nearest(&map, was) != ID_B,
                         "a row that grew is still the row the cursor was on");
    record_success(test_name);
}

/* Where a screen with no cursor starts. Registration order is draw order, and draw order is not
   reading order: chrome is drawn before the body it sits above. */
INKCELL_TEST_CASE(focus_first_is_reading_order_not_draw_order, unit) {
    struct inkcell_focus_item storage[FOCUS_STORAGE];
    struct inkcell_focus_map map;

    inkcell_focus_begin(&map, storage, FOCUS_STORAGE);
    (void)inkcell_focus_add(&map, ID_C, 300, 200, 80, 40); /* drawn first, lowest on the panel */
    (void)inkcell_focus_add(&map, ID_B, 120, 20, 80, 40);
    (void)inkcell_focus_add(&map, ID_A, 20, 20, 80, 40); /* level with ID_B, and before it */

    INKCELL_TEST_FAIL_IF(inkcell_focus_first(&map) != ID_A,
                         "the cursor should start at the top, and at the start of that line");

    struct inkcell_focus_map empty;
    inkcell_focus_begin(&empty, NULL, 0U);
    INKCELL_TEST_FAIL_IF(inkcell_focus_first(&empty) != INKCELL_FOCUS_NONE,
                         "a screen with nothing on it has nothing to focus");
    INKCELL_TEST_FAIL_IF(inkcell_focus_add(&empty, ID_A, 0, 0, 10, 10) || empty.dropped != 1U,
                         "a map with no storage should refuse and say so");
    record_success(test_name);
}

INKCELL_TEST_CASE(focus_reads_the_d_pad_and_nothing_else, unit) {
    enum inkcell_focus_dir dir = INKCELL_FOCUS_LEFT;

    INKCELL_TEST_FAIL_IF(!inkcell_focus_dir_for_key(INKCELL_KEY_RIGHT, &dir) ||
                             dir != INKCELL_FOCUS_RIGHT,
                         "right should be right");
    INKCELL_TEST_FAIL_IF(!inkcell_focus_dir_for_key(INKCELL_KEY_UP, &dir) ||
                             dir != INKCELL_FOCUS_UP,
                         "up should be up");
    INKCELL_TEST_FAIL_IF(inkcell_focus_dir_for_key(INKCELL_KEY_A, &dir),
                         "a face button is not a direction");
    INKCELL_TEST_FAIL_IF(dir != INKCELL_FOCUS_UP, "a refused key should not have written one");
    record_success(test_name);
}

/*
 * The arithmetic holds at the ends of the number line.
 *
 * A gap between two ints does not fit in an int and its square does not fit in what the score
 * is computed in, so a box out at INT_MAX is how a finder stops being merely wrong and starts
 * being undefined. CI runs this suite under UBSan, which is what makes this case worth its
 * lines: it is not asserting an answer so much as asserting that there is one.
 */
INKCELL_TEST_CASE(focus_does_not_overflow_on_an_absurd_layout, unit) {
    struct inkcell_focus_item storage[FOCUS_STORAGE];
    struct inkcell_focus_map map;

    inkcell_focus_begin(&map, storage, FOCUS_STORAGE);
    (void)inkcell_focus_add(&map, ID_A, INT_MIN, 0, INT_MAX, 100);
    (void)inkcell_focus_add(&map, ID_B, 0, 0, INT_MAX, 100);
    (void)inkcell_focus_add(&map, ID_C, INT_MIN / 2, INT_MAX - 100, 1000, 100);

    INKCELL_TEST_FAIL_IF(inkcell_focus_find(&map, ID_A, INKCELL_FOCUS_RIGHT) != ID_B,
                         "the box starting further along is still the box to the right");
    INKCELL_TEST_FAIL_IF(inkcell_focus_find(&map, ID_A, INKCELL_FOCUS_DOWN) != ID_C,
                         "and the one at the bottom of the world is still below");
    INKCELL_TEST_FAIL_IF(inkcell_focus_find(&map, ID_C, INKCELL_FOCUS_DOWN) != INKCELL_FOCUS_NONE,
                         "with nothing under it");
    record_success(test_name);
}

/* ---- the half of a screen that is not on the panel ------------------------------------------ */

/*
 * A screen with a list on it, as the press sees it: ten rows drawn out of four hundred, a chip
 * above them and a verb below.
 *
 * Every case below uses this one layout because the interesting question is always the same -
 * which of the two halves answers - and the trap is always the same too: the verb under the
 * list is drawn, so the geometry always has *an* answer at the bottom of the window, and it is
 * the wrong one until the list has run out.
 */
#define STEP_ROWS 1000U
#define STEP_ITEMS 400U
#define STEP_VISIBLE 10U

static void focus_step_screen(struct inkcell_focus_map *map, struct inkcell_focus_item *storage,
                              uint32_t capacity, uint32_t first) {
    inkcell_focus_begin(map, storage, capacity);
    (void)inkcell_focus_add(map, ID_A, 16, 0, 90, 40); /* a chip over the list */
    for (uint32_t i = 0U; i < STEP_VISIBLE; ++i) {
        (void)inkcell_focus_add(map, STEP_ROWS + first + i, 16, 60 + (int)i * 44, 600, 44);
    }
    (void)inkcell_focus_add(map, ID_B, 16, 60 + (int)STEP_VISIBLE * 44, 120, 40); /* and a verb */
}

INKCELL_TEST_CASE(focus_step_scrolls_rather_than_leaving_the_list, unit) {
    struct inkcell_focus_item storage[FOCUS_STORAGE];
    struct inkcell_focus_map map;
    const struct inkcell_focus_run runs[] = {{STEP_ROWS, STEP_ITEMS}};
    focus_step_screen(&map, storage, FOCUS_STORAGE, 0U);

    /* Inside the window the two halves agree, and the run answers because it is asked first. */
    INKCELL_TEST_FAIL_IF(inkcell_focus_step(&map, STEP_ROWS, INKCELL_FOCUS_DOWN, runs, 1U) !=
                             STEP_ROWS + 1U,
                         "inside the window, down is the next item");

    /*
     * The case the whole thing is for. The last visible row has a verb drawn under it, so
     * inkcell_focus_find() has an answer - and taking it would walk the cursor out of a
     * four-hundred-item list at item nine.
     */
    const uint32_t last_visible = STEP_ROWS + STEP_VISIBLE - 1U;
    INKCELL_TEST_FAIL_IF(inkcell_focus_find(&map, last_visible, INKCELL_FOCUS_DOWN) != ID_B,
                         "the geometry alone leaves the list at the bottom of the window");
    INKCELL_TEST_FAIL_IF(inkcell_focus_step(&map, last_visible, INKCELL_FOCUS_DOWN, runs, 1U) !=
                             STEP_ROWS + STEP_VISIBLE,
                         "the press at the bottom of the window is a scroll, not an exit");
    record_success(test_name);
}

/* And at the true end of the list, leaving is exactly what the press means. The window here is
   the last ten items, so the run has nowhere to go and the geometry answers. */
INKCELL_TEST_CASE(focus_step_leaves_the_list_at_its_ends, unit) {
    struct inkcell_focus_item storage[FOCUS_STORAGE];
    struct inkcell_focus_map map;
    const struct inkcell_focus_run runs[] = {{STEP_ROWS, STEP_ITEMS}};
    focus_step_screen(&map, storage, FOCUS_STORAGE, STEP_ITEMS - STEP_VISIBLE);

    const uint32_t last = STEP_ROWS + STEP_ITEMS - 1U;
    INKCELL_TEST_FAIL_IF(inkcell_focus_step(&map, last, INKCELL_FOCUS_DOWN, runs, 1U) != ID_B,
                         "the last item of the list is where the verb below it is reachable");

    /* The top end is the same statement upwards, on a window showing the first items. */
    focus_step_screen(&map, storage, FOCUS_STORAGE, 0U);
    INKCELL_TEST_FAIL_IF(inkcell_focus_step(&map, STEP_ROWS, INKCELL_FOCUS_UP, runs, 1U) != ID_A,
                         "and item 0 is where the chip above is reachable");
    record_success(test_name);
}

/* A run runs down a column and says nothing about across it: left and right from a row are the
   geometry's, which is what makes a row with a control beside it navigable. */
INKCELL_TEST_CASE(focus_step_leaves_sideways_presses_to_the_geometry, unit) {
    struct inkcell_focus_item storage[FOCUS_STORAGE];
    struct inkcell_focus_map map;
    const struct inkcell_focus_run runs[] = {{STEP_ROWS, STEP_ITEMS}};

    inkcell_focus_begin(&map, storage, FOCUS_STORAGE);
    (void)inkcell_focus_add(&map, STEP_ROWS, 16, 60, 400, 44);
    (void)inkcell_focus_add(&map, ID_A, 460, 60, 80, 44); /* something beside the row */

    INKCELL_TEST_FAIL_IF(inkcell_focus_step(&map, STEP_ROWS, INKCELL_FOCUS_RIGHT, runs, 1U) != ID_A,
                         "right from a row is a question about what is drawn beside it");
    INKCELL_TEST_FAIL_IF(inkcell_focus_step(&map, STEP_ROWS, INKCELL_FOCUS_DOWN, runs, 1U) !=
                             STEP_ROWS + 1U,
                         "and down is still the run's");
    record_success(test_name);
}

/*
 * The cursor's own row has scrolled out of the window - a list the reader left and came back
 * to, or one that moved under them. The finder has nothing to say about an id it never saw;
 * the run does, and it is the same press as any other.
 */
INKCELL_TEST_CASE(focus_step_moves_a_cursor_that_is_not_on_the_panel, unit) {
    struct inkcell_focus_item storage[FOCUS_STORAGE];
    struct inkcell_focus_map map;
    const struct inkcell_focus_run runs[] = {{STEP_ROWS, STEP_ITEMS}};
    focus_step_screen(&map, storage, FOCUS_STORAGE, 0U);

    const uint32_t far_down = STEP_ROWS + 200U;
    INKCELL_TEST_FAIL_IF(inkcell_focus_has(&map, far_down), "item 200 is not on this frame");
    INKCELL_TEST_FAIL_IF(inkcell_focus_find(&map, far_down, INKCELL_FOCUS_DOWN) !=
                             INKCELL_FOCUS_NONE,
                         "and the finder rightly has nothing to say about it");
    INKCELL_TEST_FAIL_IF(inkcell_focus_step(&map, far_down, INKCELL_FOCUS_DOWN, runs, 1U) !=
                             far_down + 1U,
                         "a cursor off the panel is still a cursor in the list");
    record_success(test_name);
}

/* Ids outside every run, and a call with no runs at all, are the finder with more words. */
INKCELL_TEST_CASE(focus_step_without_a_run_is_the_finder, unit) {
    struct inkcell_focus_item storage[FOCUS_STORAGE];
    struct inkcell_focus_map map;
    const struct inkcell_focus_run runs[] = {{STEP_ROWS, STEP_ITEMS}};
    focus_step_screen(&map, storage, FOCUS_STORAGE, 0U);

    /* ID_A is the chip above the list and belongs to no run: down is geometry, and geometry
       says the first row. */
    INKCELL_TEST_FAIL_IF(inkcell_focus_step(&map, ID_A, INKCELL_FOCUS_DOWN, runs, 1U) != STEP_ROWS,
                         "an id outside every run is answered by what is drawn");
    INKCELL_TEST_FAIL_IF(inkcell_focus_step(&map, STEP_ROWS, INKCELL_FOCUS_DOWN, NULL, 0U) !=
                             STEP_ROWS + 1U,
                         "no runs at all is the finder, which agrees inside a window");

    /* A run with nothing in it holds no id, so it cannot answer for one. */
    const struct inkcell_focus_run empty[] = {{STEP_ROWS, 0U}};
    const uint32_t last_visible = STEP_ROWS + STEP_VISIBLE - 1U;
    INKCELL_TEST_FAIL_IF(inkcell_focus_step(&map, last_visible, INKCELL_FOCUS_DOWN, empty, 1U) !=
                             ID_B,
                         "an empty run is not a list the cursor is in");
    record_success(test_name);
}

/* Two lists on one screen, which is a screen that exists - a column of groups beside a column
   of readings - and each answers only for its own ids. */
INKCELL_TEST_CASE(focus_step_reads_the_run_the_cursor_is_in, unit) {
    struct inkcell_focus_item storage[FOCUS_STORAGE];
    struct inkcell_focus_map map;
    const struct inkcell_focus_run runs[] = {{STEP_ROWS, 3U}, {STEP_ROWS + 100U, STEP_ITEMS}};

    inkcell_focus_begin(&map, storage, FOCUS_STORAGE);
    (void)inkcell_focus_add(&map, STEP_ROWS + 2U, 16, 60, 200, 44);
    (void)inkcell_focus_add(&map, STEP_ROWS + 100U, 300, 60, 200, 44);

    /* The first run has three items and the cursor is on its last: down leaves it, and nothing
       is drawn below, so the press goes spare. */
    INKCELL_TEST_FAIL_IF(inkcell_focus_step(&map, STEP_ROWS + 2U, INKCELL_FOCUS_DOWN, runs, 2U) !=
                             INKCELL_FOCUS_NONE,
                         "the short run is over and nothing is drawn under it");
    /* The second run is four hundred long and the cursor is on its first. */
    INKCELL_TEST_FAIL_IF(inkcell_focus_step(&map, STEP_ROWS + 100U, INKCELL_FOCUS_DOWN, runs, 2U) !=
                             STEP_ROWS + 101U,
                         "the long run keeps going");
    record_success(test_name);
}
