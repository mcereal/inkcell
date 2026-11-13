/*
 * The pager: which page a finger's drag or flick ends on, where the pages are while it holds
 * them and while they settle, and the gestures it must leave alone.
 *
 * Pages 1000 px wide, a 10 px slop, a flick at 1000 px a second and a 200 ms settle, so every
 * number below reads as a fraction of a page.
 */

#include "framework/inkcell_test.h"

#include "inkcell/ui/pager.h"

#define PAGE 1000
#define SETTLE 200U

static void four_pages(struct inkcell_pager *p) {
    inkcell_pager_init(p, 10, 1000, SETTLE);
    inkcell_pager_extent(p, PAGE, 4U);
}

/* A drag from `x0` to `x1`, `steps` moves `ms` apart, starting at `t`; answers when it ended. */
static uint64_t drag(struct inkcell_pager *p, int32_t x0, int32_t x1, int steps, uint32_t ms,
                     uint64_t t) {
    inkcell_pager_down(p, x0, 500, t);
    for (int i = 1; i <= steps; ++i) {
        t += ms;
        (void)inkcell_pager_move(p, x0 + (x1 - x0) * i / steps, 500, t);
    }
    return t;
}

INKCELL_TEST_CASE(ui_pager_a_tap_is_not_a_page_turn, unit) {
    struct inkcell_pager p;
    four_pages(&p);
    inkcell_pager_down(&p, 500, 500, 0U);
    INKCELL_TEST_FAIL_IF(inkcell_pager_move(&p, 504, 503, 10U), "a wobble claimed the gesture");
    INKCELL_TEST_FAIL_IF(inkcell_pager_up(&p, 20U, false), "a tap was the pager's");
    INKCELL_TEST_FAIL_IF(p.page != 0U || inkcell_pager_offset(&p, 20U) != 0, "a tap moved pages");
    record_success(test_name);
}

INKCELL_TEST_CASE(ui_pager_a_slow_drag_turns_past_half_way, unit) {
    struct inkcell_pager p;
    four_pages(&p);
    /* 600 px left over two seconds: slow, and past half a page. */
    uint64_t t = drag(&p, 900, 300, 40, 50U, 0U);
    INKCELL_TEST_FAIL_IF(!p.claimed, "a drag across was not the pager's");
    const int32_t held = inkcell_pager_offset(&p, t);
    INKCELL_TEST_FAIL_IF(held != 590, "the pages do not follow the finger, less the slop");
    INKCELL_TEST_FAIL_IF(!inkcell_pager_up(&p, t, false) || p.page != 1U,
                         "past half way did not turn the page");
    INKCELL_TEST_FAIL_IF(inkcell_pager_offset(&p, t) != held, "the settle did not start in place");
    const int32_t mid = inkcell_pager_offset(&p, t + SETTLE / 2U);
    INKCELL_TEST_FAIL_IF(mid <= held || mid >= PAGE, "the settle does not travel");
    INKCELL_TEST_FAIL_IF(inkcell_pager_offset(&p, t + SETTLE) != PAGE ||
                             inkcell_pager_active(&p, t + SETTLE),
                         "the settle does not end on the page");

    /* Short of half way, slowly: back to where it was. */
    t = drag(&p, 300, 650, 35, 50U, t + 1000U);
    INKCELL_TEST_FAIL_IF(!inkcell_pager_up(&p, t, false) || p.page != 1U,
                         "short of half way turned the page");
    record_success(test_name);
}

INKCELL_TEST_CASE(ui_pager_a_flick_turns_a_page_from_a_short_drag, unit) {
    struct inkcell_pager p;
    four_pages(&p);
    /* 80 px left in 40 ms: 2000 px a second. */
    const uint64_t t = drag(&p, 500, 420, 4, 10U, 0U);
    INKCELL_TEST_FAIL_IF(!inkcell_pager_up(&p, t, false) || p.page != 1U,
                         "a flick did not turn the page");
    /* And back: a flick right from page 1. */
    const uint64_t u = drag(&p, 400, 480, 4, 10U, t + SETTLE);
    INKCELL_TEST_FAIL_IF(!inkcell_pager_up(&p, u, false) || p.page != 0U,
                         "a flick right did not turn back");
    /* A drag that stopped before it was let go is not a flick, however fast it began. */
    uint64_t v = drag(&p, 500, 420, 4, 10U, u + SETTLE);
    v += 300U;
    (void)inkcell_pager_move(&p, 420, 500, v);
    INKCELL_TEST_FAIL_IF(!inkcell_pager_up(&p, v, false) || p.page != 0U,
                         "a finger that stopped still flicked");
    record_success(test_name);
}

INKCELL_TEST_CASE(ui_pager_leaves_a_vertical_gesture_alone, unit) {
    struct inkcell_pager p;
    four_pages(&p);
    inkcell_pager_down(&p, 500, 500, 0U);
    INKCELL_TEST_FAIL_IF(inkcell_pager_move(&p, 508, 530, 10U), "a scroll down was taken");
    INKCELL_TEST_FAIL_IF(inkcell_pager_move(&p, 300, 540, 20U),
                         "a gesture decided as vertical was taken later");
    INKCELL_TEST_FAIL_IF(inkcell_pager_up(&p, 30U, false) || inkcell_pager_offset(&p, 30U) != 0,
                         "a vertical gesture moved pages");
    record_success(test_name);
}

INKCELL_TEST_CASE(ui_pager_ends_give_and_hold, unit) {
    struct inkcell_pager p;
    four_pages(&p);
    /* Right from the first page: there is nothing before it. */
    uint64_t t = drag(&p, 100, 600, 25, 40U, 0U);
    const int32_t give = inkcell_pager_offset(&p, t);
    INKCELL_TEST_FAIL_IF(give >= 0 || give <= -490, "the first page's edge is a wall or nothing");
    INKCELL_TEST_FAIL_IF(!inkcell_pager_up(&p, t, false) || p.page != 0U,
                         "a drag before the first page turned it");
    INKCELL_TEST_FAIL_IF(inkcell_pager_offset(&p, t + SETTLE) != 0, "the band did not spring back");
    /* A flick left from the last page stays there. */
    inkcell_pager_to(&p, 3U, t + SETTLE);
    t = drag(&p, 500, 420, 4, 10U, t + 2U * SETTLE);
    INKCELL_TEST_FAIL_IF(!inkcell_pager_up(&p, t, false) || p.page != 3U,
                         "a flick past the last page went somewhere");
    record_success(test_name);
}

INKCELL_TEST_CASE(ui_pager_a_finger_catches_a_settle, unit) {
    struct inkcell_pager p;
    four_pages(&p);
    inkcell_pager_to(&p, 2U, 0U);
    const int32_t moving = inkcell_pager_offset(&p, SETTLE / 2U);
    INKCELL_TEST_FAIL_IF(!inkcell_pager_active(&p, SETTLE / 2U), "pager_to does not ease");
    inkcell_pager_down(&p, 500, 500, SETTLE / 2U);
    INKCELL_TEST_FAIL_IF(inkcell_pager_offset(&p, SETTLE) != moving,
                         "a finger did not stop the pages where they were");
    /* Let go without dragging: they finish going where they were going. */
    (void)inkcell_pager_up(&p, SETTLE, false);
    INKCELL_TEST_FAIL_IF(inkcell_pager_offset(&p, 3U * SETTLE) != 2 * PAGE,
                         "the pages did not finish their settle");
    /* Fewer pages than the page it is on: brought back to the last. */
    inkcell_pager_extent(&p, PAGE, 2U);
    INKCELL_TEST_FAIL_IF(p.page != 1U, "a page past the end survived the extent");
    record_success(test_name);
}

INKCELL_TEST_CASE(ui_pager_a_cancel_goes_back, unit) {
    struct inkcell_pager p;
    four_pages(&p);
    const uint64_t t = drag(&p, 900, 200, 10, 50U, 0U);
    INKCELL_TEST_FAIL_IF(!inkcell_pager_up(&p, t, true) || p.page != 0U,
                         "a cancelled drag turned the page");
    INKCELL_TEST_FAIL_IF(inkcell_pager_offset(&p, t + SETTLE) != 0, "a cancel did not go back");
    record_success(test_name);
}
