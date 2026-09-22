#define _POSIX_C_SOURCE 200809L

/*
 * The stack, and the width class.
 *
 * Almost none of this is about where a particular box lands. What a stack is *for* is that the
 * screens stop doing the arithmetic themselves, so what is worth holding is the handful of
 * properties a hand-written version got wrong - the ones that look fine on the panel the author
 * had in front of them and come apart on the next one:
 *
 *   - what is placed adds up to the extent exactly, at every extent rather than at the round
 *     ones (`stack_grow_fills_the_extent_exactly` sweeps a range on purpose);
 *   - neighbours are adjacent and never overlap, which is the half-open rule the focus map is
 *     written to as well;
 *   - a run that does not fit gives up whole items from its tail instead of drawing past the
 *     room it was given;
 *   - the indices a caller built the run with survive that, because a call site indexes `out`
 *     by the order it added things in;
 *   - a row and a column are one piece of arithmetic with the dimensions swapped, and not two
 *     that can drift.
 *
 * The last two cases are the width class, which is one function and two thresholds - but the
 * *reason* it is measured in columns rather than pixels is testable, and that is what
 * `width_class_follows_the_text_size` is.
 */

#include "framework/inkcell_test.h"

#include "inkcell/ui/fb_capture.h"
#include "inkcell/ui/fb_draw.h"
#include "inkcell/ui/stack.h"
#include "inkcell/ui/theme.h"
#include "inkcell/ui/widgets/chrome.h"

#include <stdlib.h>

/* A body-shaped box, so the numbers in here read like the ones a screen is handed. */
static struct inkcell_box body_box(void) {
    const struct inkcell_box box = {.x = 8, .y = 40, .w = 1008, .h = 600};
    return box;
}

/* ---- the run ---------------------------------------------------------------------------------
 */

INKCELL_TEST_CASE(stack_places_a_column_in_order, unit) {
    struct inkcell_stack stack;
    inkcell_stack_begin(&stack, body_box(), INKCELL_AXIS_Y, 10);
    inkcell_stack_add_fixed(&stack, 100);
    inkcell_stack_add_fixed(&stack, 50);
    inkcell_stack_add_fixed(&stack, 25);

    struct inkcell_box out[3];
    const uint32_t placed = inkcell_stack_resolve(&stack, out, 3U);
    INKCELL_TEST_FAIL_IF(placed != 3U, "three items that fit should all be placed");
    INKCELL_TEST_FAIL_IF(out[0].y != 40 || out[0].h != 100, "the first sits at the box's top");
    INKCELL_TEST_FAIL_IF(out[1].y != 150, "the second follows the first plus the gap");
    INKCELL_TEST_FAIL_IF(out[2].y != 210, "and the third follows the second");
    /* Stretch is what a bare stack does, so every box is the full cross extent. */
    for (uint32_t i = 0U; i < 3U; ++i) {
        INKCELL_TEST_FAIL_IF(out[i].x != 8 || out[i].w != 1008,
                             "a column's items stretch across it by default");
    }
    record_success(test_name);
}

INKCELL_TEST_CASE(stack_leaves_no_gap_between_neighbours, unit) {
    /* Half-open, like the focus map's rectangles: a box ends exactly where the gap starts and
       the next one starts exactly where the gap ends. A stack that was a pixel out here would
       leave a hairline of ground between two cards on every screen at once. */
    struct inkcell_stack stack;
    inkcell_stack_begin(&stack, body_box(), INKCELL_AXIS_X, 7);
    for (uint32_t i = 0U; i < 5U; ++i) {
        inkcell_stack_add_grow(&stack, 0, 1U);
    }

    struct inkcell_box out[5];
    INKCELL_TEST_FAIL_IF(inkcell_stack_resolve(&stack, out, 5U) != 5U, "five equal shares fit");
    for (uint32_t i = 1U; i < 5U; ++i) {
        const int previous_end = out[i - 1U].x + out[i - 1U].w;
        INKCELL_TEST_FAIL_IF(out[i].x != previous_end + 7,
                             "neighbours are one gap apart, never overlapping and never short");
    }
    record_success(test_name);
}

INKCELL_TEST_CASE(stack_grow_fills_the_extent_exactly, unit) {
    /*
     * The one that matters. A share rounded down per item leaves a stray pixel at the trailing
     * edge on most extents and none of the round ones, which is exactly the bug that survives
     * being looked at: the author's panel is 1024 wide and three items divide it evenly.
     *
     * So the sweep is over widths that mostly do not divide, with weights that mostly do not
     * either, and the assertion is that the last box ends on the last pixel every time.
     */
    for (int width = 37; width < 400; ++width) {
        const struct inkcell_box box = {.x = 3, .y = 0, .w = width, .h = 20};
        struct inkcell_stack stack;
        inkcell_stack_begin(&stack, box, INKCELL_AXIS_X, 5);
        inkcell_stack_add_grow(&stack, 0, 1U);
        inkcell_stack_add_grow(&stack, 0, 2U);
        inkcell_stack_add_grow(&stack, 0, 3U);

        struct inkcell_box out[3];
        if (inkcell_stack_resolve(&stack, out, 3U) != 3U) {
            record_failure(test_name, "three weightless items always fit");
            return;
        }
        if (out[2].x + out[2].w != box.x + box.w) {
            record_failure(test_name, "the run should end on the box's last pixel at every width");
            return;
        }
    }
    record_success(test_name);
}

INKCELL_TEST_CASE(stack_grow_shares_are_proportional, unit) {
    /* 300 pixels, no gaps, weights 1:2:3 - a sixth, a third and a half. */
    const struct inkcell_box box = {.x = 0, .y = 0, .w = 300, .h = 10};
    struct inkcell_stack stack;
    inkcell_stack_begin(&stack, box, INKCELL_AXIS_X, 0);
    inkcell_stack_add_grow(&stack, 0, 1U);
    inkcell_stack_add_grow(&stack, 0, 2U);
    inkcell_stack_add_grow(&stack, 0, 3U);

    struct inkcell_box out[3];
    INKCELL_TEST_FAIL_IF(inkcell_stack_resolve(&stack, out, 3U) != 3U, "all three fit");
    INKCELL_TEST_FAIL_IF(out[0].w != 50 || out[1].w != 100 || out[2].w != 150,
                         "the leftover should divide by weight");
    record_success(test_name);
}

INKCELL_TEST_CASE(stack_grow_takes_what_a_basis_left, unit) {
    /* Growth is over and above what an item asked for, not instead of it: a label with a fixed
       icon beside it is the common row and the icon must keep its width. */
    const struct inkcell_box box = {.x = 0, .y = 0, .w = 200, .h = 10};
    struct inkcell_stack stack;
    inkcell_stack_begin(&stack, box, INKCELL_AXIS_X, 0);
    inkcell_stack_add_fixed(&stack, 30);
    inkcell_stack_add_grow(&stack, 20, 1U);

    struct inkcell_box out[2];
    INKCELL_TEST_FAIL_IF(inkcell_stack_resolve(&stack, out, 2U) != 2U, "both fit");
    INKCELL_TEST_FAIL_IF(out[0].w != 30, "a rigid item keeps the width it asked for");
    INKCELL_TEST_FAIL_IF(out[1].w != 170, "the growing one takes its basis plus everything left");
    record_success(test_name);
}

INKCELL_TEST_CASE(stack_is_rigid_until_asked_to_shrink, unit) {
    /*
     * CSS shrinks everything by default and that is the wrong default here - see the note on
     * `shrink`. A row of three 100s in 250 pixels gives up the third rather than squeezing all
     * three by seventeen, because a row that is subtly wrong everywhere is harder to notice
     * than a row that is obviously missing something.
     */
    const struct inkcell_box box = {.x = 0, .y = 0, .w = 250, .h = 10};
    struct inkcell_stack stack;
    inkcell_stack_begin(&stack, box, INKCELL_AXIS_X, 0);
    inkcell_stack_add_fixed(&stack, 100);
    inkcell_stack_add_fixed(&stack, 100);
    inkcell_stack_add_fixed(&stack, 100);

    struct inkcell_box out[3];
    INKCELL_TEST_FAIL_IF(inkcell_stack_resolve(&stack, out, 3U) != 2U,
                         "three rigid hundreds in two hundred and fifty is two of them");
    INKCELL_TEST_FAIL_IF(out[0].w != 100 || out[1].w != 100,
                         "the two that were placed keep the width they asked for");
    INKCELL_TEST_FAIL_IF(!inkcell_box_is_empty(out[2]),
                         "the one that was dropped comes back empty");
    record_success(test_name);
}

INKCELL_TEST_CASE(stack_drop_does_not_move_the_indices, unit) {
    /* The whole reason `out` is parallel to the add order. A run that renumbered what it placed
       would have every call site testing a count instead of a box, and the first screen to get
       that wrong would draw its value where its label goes. */
    const struct inkcell_box box = {.x = 0, .y = 0, .w = 120, .h = 10};
    struct inkcell_stack stack;
    inkcell_stack_begin(&stack, box, INKCELL_AXIS_X, 0);
    inkcell_stack_add_fixed(&stack, 40);
    inkcell_stack_add_fixed(&stack, 40);
    inkcell_stack_add_fixed(&stack, 40);
    inkcell_stack_add_fixed(&stack, 40);

    struct inkcell_box out[4];
    INKCELL_TEST_FAIL_IF(inkcell_stack_resolve(&stack, out, 4U) != 3U, "three of the four fit");
    INKCELL_TEST_FAIL_IF(out[0].x != 0 || out[1].x != 40 || out[2].x != 80,
                         "the ones that fit are where they would have been anyway");
    INKCELL_TEST_FAIL_IF(!inkcell_box_is_empty(out[3]), "and the last is empty, not renumbered");
    record_success(test_name);
}

INKCELL_TEST_CASE(stack_shrink_stops_at_the_floor, unit) {
    /* A label gives room to a value without disappearing. */
    const struct inkcell_box box = {.x = 0, .y = 0, .w = 100, .h = 10};
    struct inkcell_stack stack;
    inkcell_stack_begin(&stack, box, INKCELL_AXIS_X, 0);
    const struct inkcell_stack_item label = {.basis = 80, .min = 30, .shrink = 1U};
    inkcell_stack_add(&stack, label);
    inkcell_stack_add_fixed(&stack, 60);

    struct inkcell_box out[2];
    INKCELL_TEST_FAIL_IF(inkcell_stack_resolve(&stack, out, 2U) != 2U,
                         "both fit once the label gives way");
    INKCELL_TEST_FAIL_IF(out[0].w != 40, "the label absorbs the whole overflow, down to no more");
    INKCELL_TEST_FAIL_IF(out[1].w != 60, "a rigid neighbour is untouched");
    INKCELL_TEST_FAIL_IF(out[0].w + out[1].w != 100, "and the row still fills the box exactly");
    record_success(test_name);
}

INKCELL_TEST_CASE(stack_shrink_spares_the_narrow, unit) {
    /*
     * Weighted by what an item has as well as by what it offered, so a wide item gives up more.
     * Weighing by the offer alone takes the same number of pixels off each, which is what
     * squeezes a short value to nothing while the long label beside it is barely touched.
     */
    const struct inkcell_box box = {.x = 0, .y = 0, .w = 240, .h = 10};
    struct inkcell_stack stack;
    inkcell_stack_begin(&stack, box, INKCELL_AXIS_X, 0);
    const struct inkcell_stack_item wide = {.basis = 240, .shrink = 1U};
    const struct inkcell_stack_item narrow = {.basis = 60, .shrink = 1U};
    inkcell_stack_add(&stack, wide);
    inkcell_stack_add(&stack, narrow);

    struct inkcell_box out[2];
    INKCELL_TEST_FAIL_IF(inkcell_stack_resolve(&stack, out, 2U) != 2U, "both are placed");
    INKCELL_TEST_FAIL_IF(out[0].w + out[1].w != 240, "the row fills the box exactly");
    INKCELL_TEST_FAIL_IF(out[1].w <= 30, "the narrow one should keep more than half of itself");
    INKCELL_TEST_FAIL_IF(out[0].w >= 240 - 30, "and the wide one should have given up the rest");
    record_success(test_name);
}

INKCELL_TEST_CASE(stack_shrink_gives_up_when_the_floors_do_not_fit, unit) {
    /* Shrinking is tried before anything is dropped, and an item is only given up when the
       floors genuinely cannot be made to fit. */
    const struct inkcell_box box = {.x = 0, .y = 0, .w = 100, .h = 10};
    struct inkcell_stack stack;
    inkcell_stack_begin(&stack, box, INKCELL_AXIS_X, 0);
    const struct inkcell_stack_item item = {.basis = 60, .min = 40, .shrink = 1U};
    inkcell_stack_add(&stack, item);
    inkcell_stack_add(&stack, item);
    inkcell_stack_add(&stack, item);

    struct inkcell_box out[3];
    INKCELL_TEST_FAIL_IF(inkcell_stack_resolve(&stack, out, 3U) != 2U,
                         "three floors of forty do not fit in a hundred, two do");
    INKCELL_TEST_FAIL_IF(out[0].w + out[1].w != 100, "the two that stayed fill the box exactly");
    record_success(test_name);
}

INKCELL_TEST_CASE(stack_justifies_only_what_nothing_asked_for, unit) {
    const struct inkcell_box box = {.x = 0, .y = 0, .w = 100, .h = 10};

    struct inkcell_stack stack;
    struct inkcell_box out[2];

    inkcell_stack_begin(&stack, box, INKCELL_AXIS_X, 0);
    stack.justify = INKCELL_JUSTIFY_END;
    inkcell_stack_add_fixed(&stack, 20);
    inkcell_stack_add_fixed(&stack, 20);
    INKCELL_TEST_FAIL_IF(inkcell_stack_resolve(&stack, out, 2U) != 2U, "both fit");
    INKCELL_TEST_FAIL_IF(out[0].x != 60 || out[1].x != 80,
                         "packed against the trailing edge, in order");

    inkcell_stack_begin(&stack, box, INKCELL_AXIS_X, 0);
    stack.justify = INKCELL_JUSTIFY_CENTER;
    inkcell_stack_add_fixed(&stack, 20);
    inkcell_stack_add_fixed(&stack, 20);
    INKCELL_TEST_FAIL_IF(inkcell_stack_resolve(&stack, out, 2U) != 2U, "both fit");
    INKCELL_TEST_FAIL_IF(out[0].x != 30, "and centred leaves the same room either side");

    /* A growing item takes the leftover, so there is nothing to justify - the two settings do
       not fight, the growth simply wins. */
    inkcell_stack_begin(&stack, box, INKCELL_AXIS_X, 0);
    stack.justify = INKCELL_JUSTIFY_END;
    inkcell_stack_add_grow(&stack, 20, 1U);
    inkcell_stack_add_fixed(&stack, 20);
    INKCELL_TEST_FAIL_IF(inkcell_stack_resolve(&stack, out, 2U) != 2U, "both fit");
    INKCELL_TEST_FAIL_IF(out[0].x != 0 || out[0].w != 80,
                         "a run with room for nothing spare has nothing to justify");
    record_success(test_name);
}

INKCELL_TEST_CASE(stack_spreads_to_both_edges, unit) {
    /* BETWEEN puts the first at one edge and the last at the other, which is what a footer of
       keycaps wants and is a thing the largest-remainder split has to get exactly right: a
       last item a pixel short of the edge is visible against the frame. */
    const struct inkcell_box box = {.x = 0, .y = 0, .w = 101, .h = 10};
    struct inkcell_stack stack;
    inkcell_stack_begin(&stack, box, INKCELL_AXIS_X, 0);
    stack.justify = INKCELL_JUSTIFY_BETWEEN;
    inkcell_stack_add_fixed(&stack, 10);
    inkcell_stack_add_fixed(&stack, 10);
    inkcell_stack_add_fixed(&stack, 10);

    struct inkcell_box out[3];
    INKCELL_TEST_FAIL_IF(inkcell_stack_resolve(&stack, out, 3U) != 3U, "all three fit");
    INKCELL_TEST_FAIL_IF(out[0].x != 0, "the first sits at the leading edge");
    INKCELL_TEST_FAIL_IF(out[2].x + out[2].w != 101,
                         "and the last on the trailing one, odd leftover and all");
    INKCELL_TEST_FAIL_IF(out[1].x <= out[0].x + out[0].w || out[1].x >= out[2].x,
                         "with the middle between them");
    record_success(test_name);
}

INKCELL_TEST_CASE(stack_alignment_is_the_stacks_until_an_item_says_otherwise, unit) {
    /*
     * Why INKCELL_ALIGN_DEFAULT exists. With stretch as the zero value, every item that had
     * simply not filled the field in would overrule a stack that asked for centring - a default
     * that works until somebody uses it.
     */
    const struct inkcell_box box = {.x = 0, .y = 0, .w = 100, .h = 40};
    struct inkcell_stack stack;
    inkcell_stack_begin(&stack, box, INKCELL_AXIS_X, 0);
    stack.align = INKCELL_ALIGN_CENTER;
    const struct inkcell_stack_item quiet = {.basis = 20, .cross = 10};
    const struct inkcell_stack_item loud = {.basis = 20, .cross = 10, .align = INKCELL_ALIGN_END};
    const struct inkcell_stack_item tall = {
        .basis = 20, .cross = 10, .align = INKCELL_ALIGN_STRETCH};
    inkcell_stack_add(&stack, quiet);
    inkcell_stack_add(&stack, loud);
    inkcell_stack_add(&stack, tall);

    struct inkcell_box out[3];
    INKCELL_TEST_FAIL_IF(inkcell_stack_resolve(&stack, out, 3U) != 3U, "all three fit");
    INKCELL_TEST_FAIL_IF(out[0].y != 15 || out[0].h != 10,
                         "an item that said nothing takes the stack's alignment");
    INKCELL_TEST_FAIL_IF(out[1].y != 30 || out[1].h != 10, "one that said something takes its own");
    INKCELL_TEST_FAIL_IF(out[2].y != 0 || out[2].h != 40,
                         "and stretch ignores the cross size rather than arguing with it");
    record_success(test_name);
}

INKCELL_TEST_CASE(stack_row_and_column_are_one_arithmetic, unit) {
    /* The two axes are the same code with the dimensions swapped, and this is what says so: the
       version that branches inside the placement loop is the version where the row case grows a
       fix the column case never gets. */
    const struct inkcell_box across = {.x = 5, .y = 9, .w = 200, .h = 60};
    const struct inkcell_box down = {.x = 9, .y = 5, .w = 60, .h = 200};

    struct inkcell_stack row;
    struct inkcell_stack column;
    inkcell_stack_begin(&row, across, INKCELL_AXIS_X, 7);
    inkcell_stack_begin(&column, down, INKCELL_AXIS_Y, 7);
    for (uint32_t i = 0U; i < 4U; ++i) {
        inkcell_stack_add_grow(&row, 11, (uint8_t)(i + 1U));
        inkcell_stack_add_grow(&column, 11, (uint8_t)(i + 1U));
    }

    struct inkcell_box across_out[4];
    struct inkcell_box down_out[4];
    INKCELL_TEST_FAIL_IF(inkcell_stack_resolve(&row, across_out, 4U) != 4U, "the row places four");
    INKCELL_TEST_FAIL_IF(inkcell_stack_resolve(&column, down_out, 4U) != 4U,
                         "and so does the column");
    for (uint32_t i = 0U; i < 4U; ++i) {
        INKCELL_TEST_FAIL_IF(across_out[i].x != down_out[i].y || across_out[i].w != down_out[i].h,
                             "a row is a column with the two dimensions swapped");
        INKCELL_TEST_FAIL_IF(across_out[i].y != down_out[i].x || across_out[i].h != down_out[i].w,
                             "and so is its cross axis");
    }
    record_success(test_name);
}

INKCELL_TEST_CASE(stack_refuses_what_it_cannot_answer, unit) {
    struct inkcell_stack stack;
    inkcell_stack_begin(&stack, body_box(), INKCELL_AXIS_Y, 0);
    for (uint32_t i = 0U; i < INKCELL_STACK_MAX; ++i) {
        INKCELL_TEST_FAIL_IF(!inkcell_stack_add_fixed(&stack, 1), "the run should take its fill");
    }
    INKCELL_TEST_FAIL_IF(inkcell_stack_add_fixed(&stack, 1),
                         "and refuse the one past the cap rather than overwrite the last");
    INKCELL_TEST_FAIL_IF(stack.count != INKCELL_STACK_MAX, "a refused item is not counted");

    /* A cap it cannot fill is refused outright: a partial answer is a run laid out against a
       total the caller cannot see. */
    struct inkcell_box out[INKCELL_STACK_MAX];
    out[0].w = 1234;
    INKCELL_TEST_FAIL_IF(inkcell_stack_resolve(&stack, out, 4U) != 0U,
                         "a short output should be refused");
    INKCELL_TEST_FAIL_IF(out[0].w != 1234, "and nothing written into it");

    /* Neither is a box with no room in it something to lay out in. */
    const struct inkcell_box nothing = {.x = 0, .y = 0, .w = 0, .h = 10};
    inkcell_stack_begin(&stack, nothing, INKCELL_AXIS_X, 0);
    inkcell_stack_add_fixed(&stack, 5);
    INKCELL_TEST_FAIL_IF(inkcell_stack_resolve(&stack, out, INKCELL_STACK_MAX) != 0U,
                         "an empty box places nothing");
    INKCELL_TEST_FAIL_IF(!inkcell_box_is_empty(out[0]), "and says so in the box it hands back");
    record_success(test_name);
}

/* ---- boxes ------------------------------------------------------------------------------------
 */

INKCELL_TEST_CASE(box_inset_never_turns_inside_out, unit) {
    const struct inkcell_box box = {.x = 10, .y = 20, .w = 30, .h = 40};
    const struct inkcell_box in = inkcell_box_inset(box, 5, 5);
    INKCELL_TEST_FAIL_IF(in.x != 15 || in.y != 25 || in.w != 20 || in.h != 30,
                         "an inset takes the same room off both edges");

    const struct inkcell_box out = inkcell_box_inset(box, -2, -2);
    INKCELL_TEST_FAIL_IF(out.x != 8 || out.w != 34, "a negative inset is an outset, and allowed");

    const struct inkcell_box gone = inkcell_box_inset(box, 40, 40);
    INKCELL_TEST_FAIL_IF(gone.w != 0 || gone.h != 0,
                         "an inset deeper than the box leaves nothing, not a negative");
    INKCELL_TEST_FAIL_IF(!inkcell_box_is_empty(gone), "which is what empty means");
    record_success(test_name);
}

INKCELL_TEST_CASE(box_measure_caps_and_centres, unit) {
    const struct inkcell_box wide = {.x = 0, .y = 0, .w = 1000, .h = 100};
    const struct inkcell_box capped = inkcell_box_measure(wide, 600);
    INKCELL_TEST_FAIL_IF(capped.w != 600, "a box wider than the measure is cut down to it");
    INKCELL_TEST_FAIL_IF(capped.x != 200, "and what is left over is split either side");
    INKCELL_TEST_FAIL_IF(capped.y != 0 || capped.h != 100, "the cross axis is not its business");

    const struct inkcell_box narrow = {.x = 4, .y = 0, .w = 300, .h = 100};
    const struct inkcell_box same = inkcell_box_measure(narrow, 600);
    INKCELL_TEST_FAIL_IF(same.x != 4 || same.w != 300,
                         "a panel that never reaches the cap is untouched by it");
    INKCELL_TEST_FAIL_IF(inkcell_box_measure(wide, 0).w != 1000, "and no cap is no change");
    record_success(test_name);
}

/* ---- width classes ----------------------------------------------------------------------------
 */

INKCELL_TEST_CASE(width_class_divides_where_it_says, unit) {
    INKCELL_TEST_FAIL_IF(inkcell_width_class_of(0U) != INKCELL_WIDTH_COMPACT,
                         "a surface with nothing on it is compact, which is the safe way round");
    INKCELL_TEST_FAIL_IF(inkcell_width_class_of(INKCELL_WIDTH_MEASURE_COLS) !=
                             INKCELL_WIDTH_COMPACT,
                         "one measure and no more is the compact case by construction");
    INKCELL_TEST_FAIL_IF(inkcell_width_class_of(INKCELL_WIDTH_MEDIUM_COLS - 1U) !=
                             INKCELL_WIDTH_COMPACT,
                         "a column short of the threshold is still below it");
    INKCELL_TEST_FAIL_IF(inkcell_width_class_of(INKCELL_WIDTH_MEDIUM_COLS) != INKCELL_WIDTH_MEDIUM,
                         "and the threshold itself is the class it names");
    INKCELL_TEST_FAIL_IF(inkcell_width_class_of(INKCELL_WIDTH_EXPANDED_COLS - 1U) !=
                             INKCELL_WIDTH_MEDIUM,
                         "medium runs right up to expanded");
    INKCELL_TEST_FAIL_IF(inkcell_width_class_of(INKCELL_WIDTH_EXPANDED_COLS) !=
                             INKCELL_WIDTH_EXPANDED,
                         "which starts where two readable columns fit");
    INKCELL_TEST_FAIL_IF((int)INKCELL_WIDTH_COMPACT != 0,
                         "compact is the zero value, so an unmeasured surface is the narrow one");
    record_success(test_name);
}

INKCELL_TEST_CASE(width_class_follows_the_text_size, unit) {
    /*
     * The reason the class is counted in columns rather than pixels, held as a test because it
     * is the one property a pixel threshold would get wrong: the same surface, with the text
     * turned up, is a narrower surface as far as the reader is concerned. iOS does exactly this
     * when Dynamic Type is turned up, and for this reason - at that size the second pane was
     * never going to be readable.
     */
    struct inkcell_capture *small = NULL;
    struct inkcell_capture *large = NULL;
    if (inkcell_capture_open(&small, 1600U, 900U, INKCELL_SCALE(3)) < 0) {
        record_failure(test_name, "the capture should open");
        return;
    }
    if (inkcell_capture_open(&large, 1600U, 900U, INKCELL_SCALE(6)) < 0) {
        inkcell_capture_close(small);
        record_failure(test_name, "the second capture should open");
        return;
    }

    const enum inkcell_width_class at_small = inkcell_fb_width_class(inkcell_capture_state(small));
    const enum inkcell_width_class at_large = inkcell_fb_width_class(inkcell_capture_state(large));
    inkcell_capture_close(small);
    inkcell_capture_close(large);

    INKCELL_TEST_FAIL_IF(at_small <= at_large,
                         "the same panel set in larger text should fall to a narrower class");
    record_success(test_name);
}

INKCELL_TEST_CASE(width_class_of_the_handheld_panel_is_compact, unit) {
    /* The panel this toolkit was written for, measured the way everything else measures it. A
       breakpoint that put the Brick in anything but the compact class would reshape every
       screen on the device. */
    struct inkcell_capture *capture = NULL;
    if (inkcell_capture_open(&capture, 1024U, 768U, INKCELL_SCALE(4)) < 0) {
        record_failure(test_name, "the capture should open");
        return;
    }
    const enum inkcell_width_class width = inkcell_fb_width_class(inkcell_capture_state(capture));
    inkcell_capture_close(capture);
    INKCELL_TEST_FAIL_IF(width != INKCELL_WIDTH_COMPACT, "1024 at the body scale is compact");
    record_success(test_name);
}

/* ---- the body box -----------------------------------------------------------------------------
 */

INKCELL_TEST_CASE(body_box_is_the_room_the_layout_says_it_is, unit) {
    struct inkcell_capture *capture = NULL;
    if (inkcell_capture_open(&capture, 1024U, 768U, INKCELL_SCALE(4)) < 0) {
        record_failure(test_name, "the capture should open");
        return;
    }
    struct inkcell_draw_state *state = inkcell_capture_state(capture);
    const struct inkcell_fb_layout layout = inkcell_fb_layout_begin(state, true, false);
    const struct inkcell_box body = inkcell_fb_body_box(state, &layout);

    const bool agrees = body.y == layout.body_y && body.w == layout.body_w &&
                        body.h == layout.footer_y - layout.body_y;
    const bool classed = layout.width == inkcell_fb_width_class(state);
    /* The measure never reaches into the body it came out of, and on this panel never cuts it
       either: the whole point is that the handheld case is unchanged. */
    const struct inkcell_box measure = inkcell_fb_measure_box(state, &layout);
    const bool inside = measure.x >= body.x && measure.x + measure.w <= body.x + body.w &&
                        measure.y == body.y && measure.h == body.h;
    /* The panel is one measure wide, near enough - see INKCELL_WIDTH_MEASURE_COLS - so what the
       cap takes off it is under a column, which is the rounding of a centred integer and not a
       narrower screen. A device whose body the measure visibly cut would be a cap set wrong. */
    const bool whole = body.w - measure.w <= inkcell_fb_char_adv(state, state->scale);
    inkcell_capture_close(capture);

    INKCELL_TEST_FAIL_IF(!agrees, "the body box is the layout's own numbers, assembled once");
    INKCELL_TEST_FAIL_IF(!classed, "and the layout carries the frame's width class");
    INKCELL_TEST_FAIL_IF(!inside, "the measure stays inside the body");
    INKCELL_TEST_FAIL_IF(!whole, "and leaves the handheld panel as wide as it already was");
    record_success(test_name);
}

INKCELL_TEST_CASE(body_box_measure_narrows_a_wide_window, unit) {
    /* The other half: the same call on a window dragged wide is a column rather than a
       paragraph set across the whole of it. */
    struct inkcell_capture *capture = NULL;
    if (inkcell_capture_open(&capture, 2400U, 900U, INKCELL_SCALE(4)) < 0) {
        record_failure(test_name, "the capture should open");
        return;
    }
    struct inkcell_draw_state *state = inkcell_capture_state(capture);
    const struct inkcell_fb_layout layout = inkcell_fb_layout_begin(state, true, false);
    const struct inkcell_box body = inkcell_fb_body_box(state, &layout);
    const struct inkcell_box measure = inkcell_fb_measure_box(state, &layout);
    const int left = measure.x - body.x;
    const int right = (body.x + body.w) - (measure.x + measure.w);
    const enum inkcell_width_class width = layout.width;
    inkcell_capture_close(capture);

    INKCELL_TEST_FAIL_IF(width != INKCELL_WIDTH_EXPANDED,
                         "a 2400px window at the body scale is expanded");
    INKCELL_TEST_FAIL_IF(measure.w >= body.w, "the measure should have capped it");
    INKCELL_TEST_FAIL_IF(left - right > 1 || right - left > 1,
                         "and centred what was left, to the pixel");
    record_success(test_name);
}
