#define _POSIX_C_SOURCE 200809L

/*
 * A position in a piece of content, in pixels: the arithmetic, with no panel anywhere near it.
 *
 * This is the suite the model was shaped to have. Everything a scroll does is two numbers and
 * a curve - where the content is going, where it came from, and how the ends give - and every
 * one of those has an answer a test can state exactly, because nothing here accumulates: the
 * position is derived from the clock, so a case can put the scroll anywhere in its travel and
 * read the number out.
 *
 * The cases in order: the extent and its clamp, the travel, the band at both ends, the spring,
 * and the reveal. The band gets the most attention of the five - it is the whole of what the
 * ends *feel* like, and it is the only part of this that a reader would notice being wrong
 * without being able to say why.
 */

#include "framework/inkcell_test.h"

#include "inkcell/ui/anim.h"
#include "inkcell/ui/scroll.h"

/* A window 300 pixels tall onto 1000 pixels of content: 700 of scroll, and a give of 100 at
   each end (a third of the window). Round numbers, so an expectation in a case is a number a
   reader can check rather than one they have to trust. */
#define SCROLL_VIEWPORT 300
#define SCROLL_CONTENT 1000
#define SCROLL_MAX (SCROLL_CONTENT - SCROLL_VIEWPORT)
#define SCROLL_GIVE (SCROLL_VIEWPORT / 3)

/* Long enough for any travel in this module to have finished. The durations are stated in
   src/scroll.c rather than taken from a theme, and a second is an order of magnitude past the
   longest of them - so this stays right if one of them is retuned. */
#define SCROLL_SETTLED_MS 1000U

static struct inkcell_scroll scroll_open(void) {
    struct inkcell_scroll s = {0};
    inkcell_scroll_extent(&s, SCROLL_CONTENT, SCROLL_VIEWPORT);
    return s;
}

/* ---- the extent ------------------------------------------------------------------------- */

/* Content that fits has nowhere to go, and a scroll over it is not a scroll. */
INKCELL_TEST_CASE(scroll_content_that_fits_does_not_scroll, unit) {
    struct inkcell_scroll s = {0};
    inkcell_scroll_extent(&s, 100, SCROLL_VIEWPORT);

    INKCELL_TEST_FAIL_IF(inkcell_scroll_max(&s) != 0, "there should be nothing to scroll");
    inkcell_scroll_to(&s, 500, 0U);
    INKCELL_TEST_FAIL_IF(inkcell_scroll_offset(&s, SCROLL_SETTLED_MS) != 0,
                         "and nowhere for a jump to land");
    record_success(test_name);
}

/*
 * Content that shrank pulls the position back with it.
 *
 * Not an edge case - a filter emptying is exactly this, on a list the reader is part way down
 * - and the failure it prevents is a window onto rows that are not there any more.
 */
INKCELL_TEST_CASE(scroll_shrinking_content_pulls_the_position_back, unit) {
    struct inkcell_scroll s = scroll_open();
    inkcell_scroll_place(&s, SCROLL_MAX);

    inkcell_scroll_extent(&s, 400, SCROLL_VIEWPORT);
    INKCELL_TEST_FAIL_IF(inkcell_scroll_offset(&s, SCROLL_SETTLED_MS) != 100,
                         "the position should follow the content's new end");
    record_success(test_name);
}

/*
 * Restating the same extent does not take an overscroll back.
 *
 * The bug this is here to stop was invisible in the arithmetic and obvious in a picture: a
 * viewport sets the extent every frame, so a clamp applied unconditionally in
 * inkcell_scroll_extent() undid every overscroll before the frame that raised it could draw
 * one. Nothing about the band was wrong; the ends simply did not give.
 */
INKCELL_TEST_CASE(scroll_restating_the_extent_keeps_an_overscroll, unit) {
    struct inkcell_scroll s = scroll_open();
    inkcell_scroll_place(&s, SCROLL_MAX);
    inkcell_scroll_by(&s, 200, 0U);
    const int32_t held = inkcell_scroll_overscroll(&s, SCROLL_SETTLED_MS);
    INKCELL_TEST_FAIL_IF(held <= 0, "it should be past the end to begin with");

    /* What a viewport does on the next frame, and the one after that. */
    inkcell_scroll_extent(&s, SCROLL_CONTENT, SCROLL_VIEWPORT);
    inkcell_scroll_extent(&s, SCROLL_CONTENT, SCROLL_VIEWPORT);
    INKCELL_TEST_FAIL_IF(inkcell_scroll_overscroll(&s, SCROLL_SETTLED_MS) != held,
                         "an unchanged extent should leave it exactly where it was");

    /* And a *changed* one still pulls it back, which is the behaviour the check must not have
       cost: an end that has moved is not the end the reader was pushing at. */
    inkcell_scroll_extent(&s, SCROLL_CONTENT + 200, SCROLL_VIEWPORT);
    INKCELL_TEST_FAIL_IF(inkcell_scroll_overscroll(&s, SCROLL_SETTLED_MS) != 0,
                         "a changed extent should cancel it");
    record_success(test_name);
}

/* ---- the travel ------------------------------------------------------------------------- */

/* A press moves the content, and it eases there rather than arriving. */
INKCELL_TEST_CASE(scroll_eases_to_where_a_press_sent_it, unit) {
    struct inkcell_scroll s = scroll_open();
    inkcell_scroll_by(&s, 120, 0U);

    INKCELL_TEST_FAIL_IF(inkcell_scroll_offset(&s, 0U) != 0,
                         "it should start where it was");
    const int32_t midway = inkcell_scroll_offset(&s, 40U);
    INKCELL_TEST_FAIL_IF(midway <= 0 || midway >= 120, "and be on its way part way through");
    INKCELL_TEST_FAIL_IF(!inkcell_scroll_active(&s, 40U), "and owe another frame while it is");
    INKCELL_TEST_FAIL_IF(inkcell_scroll_offset(&s, SCROLL_SETTLED_MS) != 120,
                         "and land exactly where it was sent");
    INKCELL_TEST_FAIL_IF(inkcell_scroll_active(&s, SCROLL_SETTLED_MS),
                         "and stop owing frames once it has");
    record_success(test_name);
}

/*
 * A second press mid-travel carries on from where the content actually is.
 *
 * The case `inkcell_anim_to()` documents one header over, in the one place it matters most: a
 * reader holding a direction is pressing several times a second, and a scroll that re-based
 * each press on where the last one *started* would crawl.
 */
INKCELL_TEST_CASE(scroll_a_second_press_adds_to_the_first, unit) {
    struct inkcell_scroll s = scroll_open();
    inkcell_scroll_by(&s, 100, 0U);
    inkcell_scroll_by(&s, 100, 40U);

    INKCELL_TEST_FAIL_IF(inkcell_scroll_offset(&s, SCROLL_SETTLED_MS) != 200,
                         "two presses should be two presses");
    const int32_t mid = inkcell_scroll_offset(&s, 40U);
    INKCELL_TEST_FAIL_IF(mid <= 0, "and the second should start from where the first got to");
    record_success(test_name);
}

/* A place is a position with nothing in flight: what a screen opening does. */
INKCELL_TEST_CASE(scroll_place_does_not_animate, unit) {
    struct inkcell_scroll s = scroll_open();
    inkcell_scroll_place(&s, 250);

    INKCELL_TEST_FAIL_IF(inkcell_scroll_offset(&s, 0U) != 250, "it should be there at once");
    INKCELL_TEST_FAIL_IF(inkcell_scroll_active(&s, 0U), "with nothing owed");
    record_success(test_name);
}

/* ---- the band --------------------------------------------------------------------------- */

/*
 * The curve, on its own.
 *
 * Three properties, and they are the three things that make an overscroll feel like one. It
 * is very nearly linear at the start, so the first pixel of push moves the content and the
 * gesture is not dead. It gives less and less. And it never reaches the bound, however hard
 * it is pushed - which is what turns "the list has ended" from a wall into resistance.
 */
INKCELL_TEST_CASE(scroll_rubber_gives_less_the_harder_it_is_pushed, unit) {
    const int32_t dim = 100;

    INKCELL_TEST_FAIL_IF(inkcell_scroll_rubber(0, dim) != 0, "no push is no give");
    const int32_t small = inkcell_scroll_rubber(10, dim);
    const int32_t more = inkcell_scroll_rubber(20, dim);
    const int32_t lots = inkcell_scroll_rubber(1000, dim);
    const int32_t absurd = inkcell_scroll_rubber(100000, dim);

    INKCELL_TEST_FAIL_IF(small <= 0 || small > 10, "a small push should move very nearly itself");
    INKCELL_TEST_FAIL_IF(more <= small, "a bigger push should still move it further");
    INKCELL_TEST_FAIL_IF(more >= 2 * small, "but less than twice as far");
    INKCELL_TEST_FAIL_IF(lots >= dim || absurd >= dim, "and never as far as the bound");
    INKCELL_TEST_FAIL_IF(absurd <= lots, "while never going backwards either");
    /* One function for both ends rather than two: the sign carries the direction. */
    INKCELL_TEST_FAIL_IF(inkcell_scroll_rubber(-20, dim) != -more, "the two ends are symmetric");
    /* No room to give is no give, which is what a viewport of nothing asks for. */
    INKCELL_TEST_FAIL_IF(inkcell_scroll_rubber(50, 0) != 0, "no room is no give");
    record_success(test_name);
}

/* Pushed past the bottom, the content goes past it - by less than it was pushed, and never by
   more than the give. */
INKCELL_TEST_CASE(scroll_overscrolls_at_the_bottom, unit) {
    struct inkcell_scroll s = scroll_open();
    inkcell_scroll_place(&s, SCROLL_MAX);
    inkcell_scroll_by(&s, 200, 0U);

    const int32_t offset = inkcell_scroll_offset(&s, SCROLL_SETTLED_MS);
    const int32_t over = inkcell_scroll_overscroll(&s, SCROLL_SETTLED_MS);
    INKCELL_TEST_FAIL_IF(offset <= SCROLL_MAX, "it should be past the end");
    INKCELL_TEST_FAIL_IF(offset >= SCROLL_MAX + 200, "by less than it was pushed");
    INKCELL_TEST_FAIL_IF(over != offset - SCROLL_MAX, "and should report how far");
    INKCELL_TEST_FAIL_IF(over >= SCROLL_GIVE, "and never past the give");
    record_success(test_name);
}

/* And the same at the top, which is the same arithmetic with the sign the other way round. */
INKCELL_TEST_CASE(scroll_overscrolls_at_the_top, unit) {
    struct inkcell_scroll s = scroll_open();
    inkcell_scroll_by(&s, -200, 0U);

    const int32_t offset = inkcell_scroll_offset(&s, SCROLL_SETTLED_MS);
    INKCELL_TEST_FAIL_IF(offset >= 0, "it should be above the top");
    INKCELL_TEST_FAIL_IF(offset <= -SCROLL_GIVE, "and never past the give");
    INKCELL_TEST_FAIL_IF(inkcell_scroll_overscroll(&s, SCROLL_SETTLED_MS) != offset,
                         "and should report how far above");
    record_success(test_name);
}

/*
 * Holding a direction at the end does not build up a debt.
 *
 * The bug this is here to stop is a felt one rather than a visible one: a reader who holds
 * down for a second at the bottom of a list and then presses up, and watches nothing happen
 * while a target a thousand pixels past the end unwinds. The band is asymptotic, so those
 * thousand pixels looked identical to a hundred on the panel and cost a second to take back.
 */
INKCELL_TEST_CASE(scroll_a_held_press_at_the_end_does_not_build_a_debt, unit) {
    struct inkcell_scroll s = scroll_open();
    inkcell_scroll_place(&s, SCROLL_MAX);
    for (int i = 0; i < 40; ++i) {
        inkcell_scroll_by(&s, 30, SCROLL_SETTLED_MS);
    }
    const int32_t held = inkcell_scroll_offset(&s, SCROLL_SETTLED_MS * 2U);
    INKCELL_TEST_FAIL_IF(held - SCROLL_MAX >= SCROLL_GIVE, "the give is still the give");

    /* And one press back the other way is one press back the other way. */
    inkcell_scroll_by(&s, -30, SCROLL_SETTLED_MS * 2U);
    const int32_t after = inkcell_scroll_offset(&s, SCROLL_SETTLED_MS * 3U);
    INKCELL_TEST_FAIL_IF(after >= held, "a press against the overscroll should move it back");
    record_success(test_name);
}

/* ---- the spring ------------------------------------------------------------------------- */

/* Let go, and anything past an end comes back to it. */
INKCELL_TEST_CASE(scroll_springs_back_when_it_is_let_go, unit) {
    struct inkcell_scroll s = scroll_open();
    inkcell_scroll_place(&s, SCROLL_MAX);
    inkcell_scroll_by(&s, 200, 0U);
    INKCELL_TEST_FAIL_IF(inkcell_scroll_overscroll(&s, SCROLL_SETTLED_MS) <= 0,
                         "it should be held past the end");

    inkcell_scroll_release(&s, SCROLL_SETTLED_MS);
    INKCELL_TEST_FAIL_IF(!inkcell_scroll_active(&s, SCROLL_SETTLED_MS),
                         "and owe the frames it takes to come back");
    INKCELL_TEST_FAIL_IF(inkcell_scroll_offset(&s, SCROLL_SETTLED_MS * 2U) != SCROLL_MAX,
                         "and land exactly on the end");
    INKCELL_TEST_FAIL_IF(inkcell_scroll_overscroll(&s, SCROLL_SETTLED_MS * 2U) != 0,
                         "with nothing left over");
    record_success(test_name);
}

/* A release with nothing held is a no-op, which is what lets a screen call it every frame -
   and a screen that only called it when it thought it mattered is one that will eventually
   leave a list stretched. */
INKCELL_TEST_CASE(scroll_release_in_the_middle_changes_nothing, unit) {
    struct inkcell_scroll s = scroll_open();
    inkcell_scroll_place(&s, 200);
    inkcell_scroll_release(&s, 0U);

    INKCELL_TEST_FAIL_IF(inkcell_scroll_active(&s, 0U), "nothing should have started");
    INKCELL_TEST_FAIL_IF(inkcell_scroll_offset(&s, 0U) != 200, "and nothing should have moved");
    record_success(test_name);
}

/* ---- the reveal ------------------------------------------------------------------------- */

/*
 * A box already in the window moves nothing, and one below it is brought up by the least it
 * can be.
 *
 * The least, because a reveal that centred what it was revealing would move everything else
 * too - and a reader pressing down a list expects the list to come up by a row rather than to
 * jump to put their row in the middle.
 */
INKCELL_TEST_CASE(scroll_reveal_moves_the_least_it_can, unit) {
    struct inkcell_scroll s = scroll_open();

    INKCELL_TEST_FAIL_IF(inkcell_scroll_reveal(&s, 100, 40, 0, 0U),
                         "a box already in the window should move nothing");

    /* A row whose bottom is 20 past the window: the window comes down by exactly 20. */
    INKCELL_TEST_FAIL_IF(!inkcell_scroll_reveal(&s, 280, 40, 0, 0U),
                         "a box below the window should move it");
    INKCELL_TEST_FAIL_IF(inkcell_scroll_offset(&s, SCROLL_SETTLED_MS) != 20,
                         "by exactly as far as it had to");
    record_success(test_name);
}

/* The pad is clear space beyond the box: a row revealed flush against the bottom edge looks
   like the last row of the list whether or not it is. */
INKCELL_TEST_CASE(scroll_reveal_leaves_room_past_the_box, unit) {
    struct inkcell_scroll s = scroll_open();
    (void)inkcell_scroll_reveal(&s, 280, 40, 30, 0U);

    INKCELL_TEST_FAIL_IF(inkcell_scroll_offset(&s, SCROLL_SETTLED_MS) != 50,
                         "the pad should come up with the box");
    record_success(test_name);
}

/*
 * A reveal is measured against where the scroll is *going*, not where it is.
 *
 * A cursor pressed twice quickly is two reveals, and the second asked against a position the
 * first is still travelling through would decide the row it wants is already in view - it
 * will be, in a hundred milliseconds. Reading the target makes a repeated press add up the way
 * a repeated press should.
 */
INKCELL_TEST_CASE(scroll_reveal_reads_where_it_is_going, unit) {
    struct inkcell_scroll s = scroll_open();
    (void)inkcell_scroll_reveal(&s, 280, 40, 0, 0U);
    /* One millisecond later the content has barely moved, and the next row is asked for. */
    INKCELL_TEST_FAIL_IF(!inkcell_scroll_reveal(&s, 320, 40, 0, 1U),
                         "the next row down should still be a move");
    INKCELL_TEST_FAIL_IF(inkcell_scroll_offset(&s, SCROLL_SETTLED_MS) != 60,
                         "and the two should add up");
    record_success(test_name);
}

/* A reveal cannot overscroll: a jump to a position is not a gesture and has nothing to
   rubber-band against. */
INKCELL_TEST_CASE(scroll_reveal_stops_at_the_end, unit) {
    struct inkcell_scroll s = scroll_open();
    (void)inkcell_scroll_reveal(&s, SCROLL_CONTENT - 10, 40, 40, 0U);

    INKCELL_TEST_FAIL_IF(inkcell_scroll_offset(&s, SCROLL_SETTLED_MS) != SCROLL_MAX,
                         "a reveal past the end should stop at the end");
    INKCELL_TEST_FAIL_IF(inkcell_scroll_overscroll(&s, SCROLL_SETTLED_MS) != 0,
                         "and never past it");
    record_success(test_name);
}
