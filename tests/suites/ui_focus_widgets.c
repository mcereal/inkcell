#define _POSIX_C_SOURCE 200809L

/*
 * What the widgets put in the focus map: the other half of tests/suites/ui_focus.c.
 *
 * That suite is the finder, in pixels a test made up. This one is the claim the finder rests
 * on - **what was drawn is what can be reached** - and it can only be tested by drawing. Every
 * case here opens a capture, hands the state a map, renders a real component into it and then
 * asks the map what is there. Nothing looks at a pixel: the question is not what the card looks
 * like, it is whether the card told the truth about where it put things.
 *
 * The cases that matter are the ones where a widget drew *less than it was asked for*. A card
 * too narrow for its third verb, a strip too narrow for its last chip, a list showing ten rows
 * of four hundred: each of those is a place a screen keeping its own cursor would put the
 * reader on something that is not on the panel, and each of them is a line in this file.
 */

#include "framework/inkcell_test.h"

#include "inkcell/ui/fb_capture.h"
#include "inkcell/ui/focus.h"
#include "inkcell/ui/widgets.h"

#define FOCUS_W_STORAGE 64U

/* Ids a screen would have in an enum of its own. The list's is a base with an item index folded
   in, which is the one place in the toolkit that works that way - see inkcell_fb_list_focus(). */
enum {
    W_ID_CARD = 100,
    W_ID_CHIP = 200,
    W_ID_ROW = 1000,
    W_ID_DIALOG = 300,
    W_ID_BUTTON = 400,
};

struct focus_harness {
    struct inkcell_capture *capture;
    struct inkcell_backend_fb_state *state;
    struct inkcell_focus_item storage[FOCUS_W_STORAGE];
    struct inkcell_focus_map map;
};

/*
 * A panel with a map on it. `width` is the whole point of the parameter: the narrow panels are
 * how a component is made to run out of room without a test having to invent a long word for it.
 */
static bool focus_harness_open(struct focus_harness *h, uint32_t width, uint32_t height) {
    h->capture = NULL;
    if (inkcell_capture_open(&h->capture, width, height, 2) < 0) {
        return false;
    }
    h->state = inkcell_capture_state(h->capture);
    inkcell_focus_begin(&h->map, h->storage, FOCUS_W_STORAGE);
    inkcell_fb_set_focus_map(h->state, &h->map);
    return true;
}

static void focus_harness_close(struct focus_harness *h) {
    inkcell_capture_close(h->capture);
}

/* ---- the card --------------------------------------------------------------------------- */

static void focus_card_with_three_verbs(struct inkcell_fb_card *card) {
    inkcell_fb_card_begin(card, INKCELL_FB_CARD_FILLED, INKCELL_ICON_NONE,
                          INKCELL_STR_TREND_SPAN_ALL, INKCELL_TONE_NORMAL);
    card->action_focus_id = W_ID_CARD;
    inkcell_fb_card_row_text(card, INKCELL_TONE_NORMAL, INKCELL_STR_TREND_SPAN_1H, "1");
    inkcell_fb_card_action(card, INKCELL_STR_KEY_DELETE, false);
    inkcell_fb_card_action(card, INKCELL_STR_KEY_CANCEL, true);
    inkcell_fb_card_action(card, INKCELL_STR_KEY_SPACE, false);
}

INKCELL_TEST_CASE(focus_widgets_card_registers_its_verbs, unit) {
    struct focus_harness h;
    INKCELL_TEST_FAIL_IF(!focus_harness_open(&h, INKCELL_CAPTURE_WIDTH, INKCELL_CAPTURE_HEIGHT),
                         "the capture should open");

    struct inkcell_fb_card card;
    focus_card_with_three_verbs(&card);
    struct inkcell_fb_layout layout = inkcell_fb_layout_begin(h.state, false, false);
    int y = layout.body_y;
    INKCELL_TEST_FAIL_IF_CLEANUP(!inkcell_fb_draw_card(h.state, &layout, &y, &card),
                                 focus_harness_close(&h), "a card should fit a full panel");

    for (uint32_t i = 0U; i < 3U; ++i) {
        INKCELL_TEST_FAIL_IF_CLEANUP(!inkcell_focus_has(&h.map, W_ID_CARD + i),
                                     focus_harness_close(&h),
                                     "every verb a wide card drew should be reachable");
    }
    /* Declared left to right, so the ids run left to right too: the card lays its verbs out
       from its trailing edge and a strip that registered them in draw order would have the
       cursor walking them backwards. */
    struct inkcell_focus_rect first = {0, 0, 0, 0};
    struct inkcell_focus_rect last = {0, 0, 0, 0};
    (void)inkcell_focus_rect_of(&h.map, W_ID_CARD, &first);
    (void)inkcell_focus_rect_of(&h.map, W_ID_CARD + 2U, &last);
    INKCELL_TEST_FAIL_IF_CLEANUP(first.x >= last.x, focus_harness_close(&h),
                                 "the first verb declared should be the leftmost");
    INKCELL_TEST_FAIL_IF_CLEANUP(
        inkcell_focus_find(&h.map, W_ID_CARD, INKCELL_FOCUS_RIGHT) != W_ID_CARD + 1U,
        focus_harness_close(&h), "right from a verb should be the verb beside it");
    focus_harness_close(&h);
    record_success(test_name);
}

/*
 * The case this whole mechanism exists for.
 *
 * A card drops verbs it has no room for, from the end. A screen that had reserved a cursor
 * position per *declared* verb would walk onto one that was never drawn - the failure
 * widgets/card.h has to ask screens to remember not to cause. Here the card is asked the
 * question on a panel a third of the width, and the answer is in the map rather than in a rule
 * somebody has to keep.
 */
INKCELL_TEST_CASE(focus_widgets_card_drops_the_verb_it_could_not_draw, unit) {
    struct focus_harness h;
    /* Narrow enough that the third verb has nowhere to go - found by asking the card rather
       than by picking a number: at 240 px it still fits all three. */
    INKCELL_TEST_FAIL_IF(!focus_harness_open(&h, 160U, 240U), "the capture should open");

    struct inkcell_fb_card card;
    focus_card_with_three_verbs(&card);
    struct inkcell_fb_layout layout = inkcell_fb_layout_begin(h.state, false, false);
    int y = layout.body_y;
    (void)inkcell_fb_draw_card(h.state, &layout, &y, &card);

    INKCELL_TEST_FAIL_IF_CLEANUP(h.map.count >= 3U, focus_harness_close(&h),
                                 "a narrow card cannot have drawn all three verbs");
    /* Whatever survived is a prefix: the verbs are dropped from the end, so a gap in the middle
       would mean the ids and the layout disagree about which verb is which. */
    for (uint32_t i = 1U; i < 3U; ++i) {
        INKCELL_TEST_FAIL_IF_CLEANUP(inkcell_focus_has(&h.map, W_ID_CARD + i) &&
                                         !inkcell_focus_has(&h.map, W_ID_CARD + i - 1U),
                                     focus_harness_close(&h),
                                     "a registered verb implies the one before it was drawn");
    }
    INKCELL_TEST_FAIL_IF_CLEANUP(h.map.dropped != 0U, focus_harness_close(&h),
                                 "nothing should have been refused for want of storage");
    focus_harness_close(&h);
    record_success(test_name);
}

/* ---- the chip strip ----------------------------------------------------------------------- */

INKCELL_TEST_CASE(focus_widgets_chip_strip_registers_the_pills_that_fit, unit) {
    struct focus_harness h;
    INKCELL_TEST_FAIL_IF(!focus_harness_open(&h, INKCELL_CAPTURE_WIDTH, INKCELL_CAPTURE_HEIGHT),
                         "the capture should open");

    const struct inkcell_fb_chip chips[] = {
        {.icon = INKCELL_ICON_MESSAGES, .label = "One", .focus_id = W_ID_CHIP},
        {.icon = INKCELL_ICON_NODES, .label = "Two", .focus_id = W_ID_CHIP + 1U},
        {.icon = INKCELL_ICON_DISPLAY, .label = "Three", .focus_id = W_ID_CHIP + 2U},
    };
    const size_t count = sizeof chips / sizeof chips[0];
    const int room =
        inkcell_fb_chip_strip_width(h.state, chips, count, 0U, INKCELL_FB_CHIP_LABELS_ALL, 2);
    (void)inkcell_fb_draw_chip_strip(h.state, 0, 100, chips, count, 0U, room, INKCELL_COLOR_BG, 2);

    INKCELL_TEST_FAIL_IF_CLEANUP(h.map.count != count, focus_harness_close(&h),
                                 "a strip with room for its chips should register all of them");
    INKCELL_TEST_FAIL_IF_CLEANUP(
        inkcell_focus_find(&h.map, W_ID_CHIP, INKCELL_FOCUS_RIGHT) != W_ID_CHIP + 1U,
        focus_harness_close(&h), "the chips should be reachable along the strip");

    /* The pill and not the gap after it: two chips a press apart must not be two boxes that
       touch, or the beam rule has nothing to separate them by. */
    struct inkcell_focus_rect one = {0, 0, 0, 0};
    struct inkcell_focus_rect two = {0, 0, 0, 0};
    (void)inkcell_focus_rect_of(&h.map, W_ID_CHIP, &one);
    (void)inkcell_focus_rect_of(&h.map, W_ID_CHIP + 1U, &two);
    INKCELL_TEST_FAIL_IF_CLEANUP(one.x + one.w >= two.x, focus_harness_close(&h),
                                 "a chip's box should stop short of the next chip's");
    focus_harness_close(&h);
    record_success(test_name);
}

/* A strip too narrow keeps drawing rather than dropping a tab, so the chip that ran past its
   room is on the panel's edge and is nobody's to stand on. */
INKCELL_TEST_CASE(focus_widgets_chip_strip_leaves_out_what_ran_past_its_room, unit) {
    struct focus_harness h;
    INKCELL_TEST_FAIL_IF(!focus_harness_open(&h, INKCELL_CAPTURE_WIDTH, INKCELL_CAPTURE_HEIGHT),
                         "the capture should open");

    const struct inkcell_fb_chip chips[] = {
        {.icon = INKCELL_ICON_MESSAGES, .label = "One", .focus_id = W_ID_CHIP},
        {.icon = INKCELL_ICON_NODES, .label = "Two", .focus_id = W_ID_CHIP + 1U},
        {.icon = INKCELL_ICON_DISPLAY, .label = "Three", .focus_id = W_ID_CHIP + 2U},
    };
    const size_t count = sizeof chips / sizeof chips[0];
    /* Room for the first pill and not much else, at the setting a strip falls back to. */
    const int room = inkcell_fb_chip_width(h.state, chips[0].icon, "", 2);
    (void)inkcell_fb_draw_chip_strip(h.state, 0, 100, chips, count, 0U, room, INKCELL_COLOR_BG, 2);

    INKCELL_TEST_FAIL_IF_CLEANUP(h.map.count >= count, focus_harness_close(&h),
                                 "a strip that overran its room should not register every chip");
    INKCELL_TEST_FAIL_IF_CLEANUP(!inkcell_focus_has(&h.map, W_ID_CHIP), focus_harness_close(&h),
                                 "the chip that did fit should still be there");
    focus_harness_close(&h);
    record_success(test_name);
}

/* ---- the dialog --------------------------------------------------------------------------- */

/*
 * The dialog is two buttons a screen could toggle between blind, and it is the best argument in
 * the file for not doing that.
 *
 * The pair sits side by side when the words fit and *stacks* when they do not, which is decided
 * down here from the labels and the panel's width. So left-right is the press that moves between
 * them on one panel and up-down on another, and the only thing that knows which is the thing
 * that laid them out. A screen resolving the press against the boxes is right on both without
 * being told, which is what these two cases are.
 */
static void focus_dialog_draw(struct focus_harness *h) {
    const struct inkcell_fb_layout layout = inkcell_fb_layout_begin(h->state, false, false);
    const struct inkcell_fb_dialog dialog = {
        .icon = INKCELL_ICON_WARNING,
        .headline = "Discard?",
        .text = "",
        .accept = "Discard",
        .cancel = "Keep",
        .cursor = 0U,
        .action_focus_id = W_ID_DIALOG,
    };
    inkcell_fb_draw_dialog(h->state, &layout, &dialog);
}

INKCELL_TEST_CASE(focus_widgets_dialog_answers_are_reachable_either_way_round, unit) {
    struct focus_harness h;
    INKCELL_TEST_FAIL_IF(!focus_harness_open(&h, INKCELL_CAPTURE_WIDTH, INKCELL_CAPTURE_HEIGHT),
                         "the capture should open");
    focus_dialog_draw(&h);

    INKCELL_TEST_FAIL_IF_CLEANUP(!inkcell_focus_has(&h.map, W_ID_DIALOG) ||
                                     !inkcell_focus_has(&h.map, W_ID_DIALOG + 1U),
                                 focus_harness_close(&h), "both answers should be registered");
    /* Wide: the cancel sits before the accept on one line, so the press between them is
       sideways. */
    INKCELL_TEST_FAIL_IF_CLEANUP(
        inkcell_focus_find(&h.map, W_ID_DIALOG, INKCELL_FOCUS_LEFT) != W_ID_DIALOG + 1U,
        focus_harness_close(&h), "left from the accept should be the cancel");
    focus_harness_close(&h);

    /* Narrow: the same two answers, stacked, and the same question answered by the geometry
       rather than by the screen remembering which layout it got. */
    INKCELL_TEST_FAIL_IF(!focus_harness_open(&h, 160U, 400U), "the narrow capture should open");
    focus_dialog_draw(&h);
    INKCELL_TEST_FAIL_IF_CLEANUP(
        inkcell_focus_find(&h.map, W_ID_DIALOG, INKCELL_FOCUS_DOWN) != W_ID_DIALOG + 1U,
        focus_harness_close(&h), "down from the accept should be the cancel it is stacked over");
    focus_harness_close(&h);
    record_success(test_name);
}

/* ---- the list --------------------------------------------------------------------------- */

INKCELL_TEST_CASE(focus_widgets_list_registers_the_window_and_not_the_list, unit) {
    struct focus_harness h;
    INKCELL_TEST_FAIL_IF(!focus_harness_open(&h, INKCELL_CAPTURE_WIDTH, INKCELL_CAPTURE_HEIGHT),
                         "the capture should open");

    const uint32_t count = 400U;
    const uint32_t cursor = 0U;
    struct inkcell_fb_layout layout = inkcell_fb_layout_begin(h.state, false, false);
    struct inkcell_fb_list list = inkcell_fb_list_begin(&layout, count, cursor);
    inkcell_fb_list_focus(&list, W_ID_ROW);
    uint32_t index = 0U;
    uint32_t drawn = 0U;
    while (inkcell_fb_list_next(&list, &index)) {
        inkcell_fb_list_row(h.state, &list, index, "row", INKCELL_TONE_NORMAL);
        drawn += 1U;
    }

    INKCELL_TEST_FAIL_IF_CLEANUP(drawn == 0U || drawn >= count, focus_harness_close(&h),
                                 "the window should be some of the list and not all of it");
    INKCELL_TEST_FAIL_IF_CLEANUP(h.map.count != drawn, focus_harness_close(&h),
                                 "a row drawn is a row registered, and no others");
    INKCELL_TEST_FAIL_IF_CLEANUP(!inkcell_focus_has(&h.map, W_ID_ROW + cursor),
                                 focus_harness_close(&h), "the cursor's own row should be there");
    INKCELL_TEST_FAIL_IF_CLEANUP(inkcell_focus_has(&h.map, W_ID_ROW + count - 1U),
                                 focus_harness_close(&h),
                                 "an item four hundred rows down the list is not on the panel");

    /* The seam inkcell_fb_list_focus() documents: down at the last row of the *window* finds
       nothing, because nothing is drawn down there. That press is a scroll, and the list's own
       cursor is what answers it. */
    INKCELL_TEST_FAIL_IF_CLEANUP(
        inkcell_focus_find(&h.map, W_ID_ROW + drawn - 1U, INKCELL_FOCUS_DOWN) != INKCELL_FOCUS_NONE,
        focus_harness_close(&h), "the bottom of the window should be the end of the map");
    INKCELL_TEST_FAIL_IF_CLEANUP(
        inkcell_focus_find(&h.map, W_ID_ROW, INKCELL_FOCUS_DOWN) != W_ID_ROW + 1U,
        focus_harness_close(&h), "inside the window, down is the next row");
    focus_harness_close(&h);
    record_success(test_name);
}

/* ---- the opt-out ------------------------------------------------------------------------ */

/*
 * A frame with no map is every frame written before this existed, and it has to keep working:
 * the widgets are handed ids by a screen that knows about focus and by nothing else, and a
 * toolkit that needed a map installed to draw a card would have made this a migration rather
 * than a feature.
 */
INKCELL_TEST_CASE(focus_widgets_register_nothing_without_a_map, unit) {
    struct focus_harness h;
    INKCELL_TEST_FAIL_IF(!focus_harness_open(&h, INKCELL_CAPTURE_WIDTH, INKCELL_CAPTURE_HEIGHT),
                         "the capture should open");
    inkcell_fb_set_focus_map(h.state, NULL);

    struct inkcell_fb_card card;
    focus_card_with_three_verbs(&card);
    struct inkcell_fb_layout layout = inkcell_fb_layout_begin(h.state, false, false);
    int y = layout.body_y;
    (void)inkcell_fb_draw_card(h.state, &layout, &y, &card);

    INKCELL_TEST_FAIL_IF_CLEANUP(h.map.count != 0U, focus_harness_close(&h),
                                 "a frame with no map should have collected nothing");

    /* And a widget given a map but no id is the same answer from the other side. */
    inkcell_fb_set_focus_map(h.state, &h.map);
    const struct inkcell_fb_button anonymous = {.rect = {10, 10, 40, 20}, .label = "x", .scale = 2};
    inkcell_fb_draw_button(h.state, &anonymous);
    INKCELL_TEST_FAIL_IF_CLEANUP(h.map.count != 0U, focus_harness_close(&h),
                                 "a button with no id is not a place to stand");

    const struct inkcell_fb_button named = {
        .rect = {10, 10, 40, 20}, .label = "x", .scale = 2, .focus_id = W_ID_BUTTON};
    inkcell_fb_draw_button(h.state, &named);
    struct inkcell_focus_rect rect = {0, 0, 0, 0};
    INKCELL_TEST_FAIL_IF_CLEANUP(!inkcell_focus_rect_of(&h.map, W_ID_BUTTON, &rect),
                                 focus_harness_close(&h), "a named button should be registered");
    INKCELL_TEST_FAIL_IF_CLEANUP(rect.x != 10 || rect.y != 10 || rect.w != 40 || rect.h != 20,
                                 focus_harness_close(&h),
                                 "the box registered should be the box it was drawn in");
    focus_harness_close(&h);
    record_success(test_name);
}

/* ---- the whole screen -------------------------------------------------------------------- */

/*
 * The point of all of it: three components that know nothing about each other, and a d-pad that
 * walks between them without the screen owning a map of itself.
 */
INKCELL_TEST_CASE(focus_widgets_compose_into_one_screen, unit) {
    struct focus_harness h;
    INKCELL_TEST_FAIL_IF(!focus_harness_open(&h, INKCELL_CAPTURE_WIDTH, INKCELL_CAPTURE_HEIGHT),
                         "the capture should open");

    struct inkcell_fb_layout layout = inkcell_fb_layout_begin(h.state, false, false);
    const struct inkcell_fb_chip chips[] = {
        {.icon = INKCELL_ICON_MESSAGES, .label = "One", .focus_id = W_ID_CHIP},
        {.icon = INKCELL_ICON_NODES, .label = "Two", .focus_id = W_ID_CHIP + 1U},
    };
    (void)inkcell_fb_draw_chip_strip(h.state, inkcell_fb_row_box(h.state).text_x, layout.body_y,
                                     chips, 2U, 0U, layout.body_w, INKCELL_COLOR_BG, 2);

    struct inkcell_fb_list list = inkcell_fb_list_begin(&layout, 4U, 0U);
    list.y = layout.body_y + 4 * layout.line;
    inkcell_fb_list_focus(&list, W_ID_ROW);
    uint32_t index = 0U;
    while (inkcell_fb_list_next(&list, &index)) {
        inkcell_fb_list_row(h.state, &list, index, "row", INKCELL_TONE_NORMAL);
    }

    /* Down from a chip reaches the rows under it. */
    INKCELL_TEST_FAIL_IF_CLEANUP(
        inkcell_focus_find(&h.map, W_ID_CHIP, INKCELL_FOCUS_DOWN) != W_ID_ROW,
        focus_harness_close(&h), "down from the filter row should reach the list");
    /*
     * And up from the first row reaches the strip - *a* chip, deliberately not a named one. A
     * row runs the width of the panel and a chip does not, so which chip is a question about
     * which one is nearest the row's centre, and a case that pinned the answer would be pinning
     * an arithmetic rather than a behaviour. A screen that wants the reader returned to the chip
     * they left from is what inkcell_focus_nearest() and a remembered rectangle are for; a
     * screen that does not, wants exactly this.
     */
    const uint32_t up = inkcell_focus_find(&h.map, W_ID_ROW, INKCELL_FOCUS_UP);
    INKCELL_TEST_FAIL_IF_CLEANUP(up != W_ID_CHIP && up != W_ID_CHIP + 1U, focus_harness_close(&h),
                                 "up from the first row should come back to the filter row");
    INKCELL_TEST_FAIL_IF_CLEANUP(inkcell_focus_first(&h.map) != W_ID_CHIP, focus_harness_close(&h),
                                 "a screen with no cursor yet should start at the top");
    focus_harness_close(&h);
    record_success(test_name);
}
