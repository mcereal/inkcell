#define _POSIX_C_SOURCE 200809L

/*
 * The pointer: a click and a scroll, answered against the boxes a frame registered.
 *
 * The first half is the model with no panel - targets the d-pad cannot reach, what is on top,
 * a press and a release that have to agree. The second half draws the real action bar and asks
 * the map what a click on it would press, because the claim that makes a window clickable with
 * no application code is that the hints say which key they are, and only drawing one tests it.
 */

#include "framework/inkcell_test.h"

#include "inkcell/ui/actions.h"
#include "inkcell/ui/fb.h"
#include "inkcell/ui/fb_capture.h"
#include "inkcell/ui/focus.h"
#include "inkcell/ui/pointer.h"
#include "inkcell/ui/widgets.h"

enum {
    P_ID_ROW = 1,
    P_ID_ROW_BELOW,
    P_ID_DIALOG,
};

#define POINTER_STORAGE 16U

/* ---- the model -------------------------------------------------------------------------- */

INKCELL_TEST_CASE(pointer_targets_are_pressed_and_never_walked_onto, unit) {
    struct inkcell_focus_item storage[POINTER_STORAGE];
    struct inkcell_focus_map map;
    inkcell_focus_begin(&map, storage, POINTER_STORAGE);

    /* A list row, and under it the footer's hint - the layout that made targets necessary. */
    (void)inkcell_focus_add(&map, P_ID_ROW, 0, 0, 200, 40);
    INKCELL_TEST_FAIL_IF(
        !inkcell_focus_add_target(&map, INKCELL_FOCUS_KEY(INKCELL_KEY_A), 10, 100, 60, 20, 0),
        "a target should register like any box");

    INKCELL_TEST_FAIL_IF(inkcell_focus_find(&map, P_ID_ROW, INKCELL_FOCUS_DOWN) !=
                             INKCELL_FOCUS_NONE,
                         "down from the last row must not land on the footer's hint");
    INKCELL_TEST_FAIL_IF(inkcell_focus_find_wrapping(&map, P_ID_ROW, INKCELL_FOCUS_UP) !=
                             INKCELL_FOCUS_NONE,
                         "...nor wrap round onto it");
    INKCELL_TEST_FAIL_IF(
        inkcell_focus_nearest(&map, (struct inkcell_focus_rect){10, 100, 60, 20}) != P_ID_ROW,
        "a cursor recovering its place must not settle on a target");

    struct inkcell_focus_map only_targets;
    inkcell_focus_begin(&only_targets, storage, POINTER_STORAGE);
    (void)inkcell_focus_add_target(&only_targets, INKCELL_FOCUS_KEY(INKCELL_KEY_B), 0, 0, 10, 10,
                                   0);
    INKCELL_TEST_FAIL_IF(inkcell_focus_first(&only_targets) != INKCELL_FOCUS_NONE,
                         "a screen of nothing but hints has nowhere for a cursor to start");
    INKCELL_TEST_FAIL_IF(inkcell_focus_hit(&only_targets, 5, 5) != INKCELL_FOCUS_KEY(INKCELL_KEY_B),
                         "...and is still clickable");
    record_success(test_name);
}

INKCELL_TEST_CASE(pointer_hit_takes_what_was_drawn_last, unit) {
    struct inkcell_focus_item storage[POINTER_STORAGE];
    struct inkcell_focus_map map;
    inkcell_focus_begin(&map, storage, POINTER_STORAGE);
    (void)inkcell_focus_add(&map, P_ID_ROW, 0, 0, 200, 40);
    (void)inkcell_focus_add(&map, P_ID_ROW_BELOW, 0, 40, 200, 40);
    /* A dialog's answer, drawn over both rows. */
    (void)inkcell_focus_add(&map, P_ID_DIALOG, 50, 20, 100, 40);

    INKCELL_TEST_FAIL_IF(inkcell_focus_hit(&map, 10, 10) != P_ID_ROW, "a row is under its own box");
    INKCELL_TEST_FAIL_IF(inkcell_focus_hit(&map, 10, 40) != P_ID_ROW_BELOW,
                         "half-open: the first pixel of the next row is the next row");
    INKCELL_TEST_FAIL_IF(inkcell_focus_hit(&map, 100, 30) != P_ID_DIALOG,
                         "the box on top is the one drawn last");
    INKCELL_TEST_FAIL_IF(inkcell_focus_hit(&map, 300, 10) != INKCELL_FOCUS_NONE,
                         "nothing is under empty panel");
    INKCELL_TEST_FAIL_IF(inkcell_focus_hit(NULL, 0, 0) != INKCELL_FOCUS_NONE,
                         "no map, nothing under anything");
    record_success(test_name);
}

INKCELL_TEST_CASE(pointer_key_ids_round_trip, unit) {
    INKCELL_TEST_FAIL_IF(inkcell_focus_key_of(INKCELL_FOCUS_KEY(INKCELL_KEY_R1)) != INKCELL_KEY_R1,
                         "a key's id should come back as the key");
    INKCELL_TEST_FAIL_IF(inkcell_focus_key_of(P_ID_ROW) != INKCELL_KEY_NONE,
                         "an application's own id is not a key");
    INKCELL_TEST_FAIL_IF(inkcell_focus_key_of(INKCELL_FOCUS_KEY_BASE) != INKCELL_KEY_NONE,
                         "the block's base is KEY_NONE, which is no key");
    INKCELL_TEST_FAIL_IF(inkcell_focus_key_of(0xFFFFFFFFU) != INKCELL_KEY_NONE,
                         "past the last key is not a key");

    enum inkcell_key keys[2];
    INKCELL_TEST_FAIL_IF(inkcell_button_keys(INKCELL_BUTTON_A, keys) != 1U ||
                             keys[0] != INKCELL_KEY_A,
                         "the A cap is the A key");
    INKCELL_TEST_FAIL_IF(inkcell_button_keys(INKCELL_BUTTON_SHOULDERS, keys) != 2U ||
                             keys[0] != INKCELL_KEY_L1 || keys[1] != INKCELL_KEY_R1,
                         "L/R is L1 then R1, the order it is printed in");
    INKCELL_TEST_FAIL_IF(inkcell_button_keys(INKCELL_BUTTON_QUIT, keys) != 0U,
                         "quitting is not a press the application hears");
    record_success(test_name);
}

INKCELL_TEST_CASE(pointer_click_needs_press_and_release_on_one_thing, unit) {
    struct inkcell_focus_item storage[POINTER_STORAGE];
    struct inkcell_focus_map map;
    inkcell_focus_begin(&map, storage, POINTER_STORAGE);
    (void)inkcell_focus_add(&map, P_ID_ROW, 0, 0, 200, 40);
    (void)inkcell_focus_add_target(&map, INKCELL_FOCUS_KEY(INKCELL_KEY_X), 0, 100, 50, 20, 0);

    struct inkcell_pointer pointer;
    inkcell_pointer_reset(&pointer);

    inkcell_pointer_down(&pointer, &map, 10, 10);
    struct inkcell_pointer_result r = inkcell_pointer_up(&pointer, &map, 150, 30);
    INKCELL_TEST_FAIL_IF(r.kind != INKCELL_POINTER_CLICK || r.target != P_ID_ROW || r.x != 150,
                         "a release anywhere on the pressed row is a click on it");

    inkcell_pointer_down(&pointer, &map, 10, 110);
    r = inkcell_pointer_up(&pointer, &map, 20, 110);
    INKCELL_TEST_FAIL_IF(r.kind != INKCELL_POINTER_KEY || r.key != INKCELL_KEY_X,
                         "a click on a key target is that key");

    inkcell_pointer_down(&pointer, &map, 10, 10);
    r = inkcell_pointer_up(&pointer, &map, 10, 110);
    INKCELL_TEST_FAIL_IF(r.kind != INKCELL_POINTER_NONE,
                         "pressing one thing and letting go over another is nothing");

    inkcell_pointer_down(&pointer, &map, 300, 300);
    r = inkcell_pointer_up(&pointer, &map, 300, 300);
    INKCELL_TEST_FAIL_IF(r.kind != INKCELL_POINTER_NONE, "a click on empty panel is nothing");

    r = inkcell_pointer_up(&pointer, &map, 10, 10);
    INKCELL_TEST_FAIL_IF(
        r.kind != INKCELL_POINTER_NONE,
        "a release with no press - the click that focused the window - is nothing");

    INKCELL_TEST_FAIL_IF(!inkcell_pointer_over_target(&map, 10, 110, false),
                         "a key target is worth a hand with nowhere to send clicks");
    INKCELL_TEST_FAIL_IF(inkcell_pointer_over_target(&map, 10, 10, false),
                         "...and a row is not, when nothing would hear the click");
    INKCELL_TEST_FAIL_IF(!inkcell_pointer_over_target(&map, 10, 10, true),
                         "...and is, when something would");
    record_success(test_name);
}

INKCELL_TEST_CASE(pointer_wheel_adds_up_fractions_and_forgets_them_on_reversal, unit) {
    struct inkcell_pointer pointer;
    inkcell_pointer_reset(&pointer);

    INKCELL_TEST_FAIL_IF(inkcell_pointer_wheel(&pointer, 1.0f) != 1, "a notch is a row");
    INKCELL_TEST_FAIL_IF(inkcell_pointer_wheel(&pointer, -2.0f) != -2, "two down is two rows down");

    /* A trackpad: a quarter of a notch at a time. */
    int total = 0;
    for (int i = 0; i < 8; ++i) {
        total += inkcell_pointer_wheel(&pointer, 0.25f);
    }
    INKCELL_TEST_FAIL_IF(total != 2, "eight quarters are two rows");

    (void)inkcell_pointer_wheel(&pointer, 0.75f);
    INKCELL_TEST_FAIL_IF(inkcell_pointer_wheel(&pointer, -0.5f) != 0,
                         "a reversal starts from nothing rather than paying off the carry");
    INKCELL_TEST_FAIL_IF(inkcell_pointer_wheel(&pointer, -0.5f) != -1,
                         "...and then counts the new direction");

    INKCELL_TEST_FAIL_IF(inkcell_pointer_wheel(&pointer, 500.0f) != INKCELL_POINTER_WHEEL_MAX,
                         "a fling is clamped");
    record_success(test_name);
}

/* ---- the action bar --------------------------------------------------------------------- */

INKCELL_TEST_CASE(pointer_action_bar_hints_are_their_keys, unit) {
    struct inkcell_capture *capture = NULL;
    INKCELL_TEST_FAIL_IF(inkcell_capture_open(&capture, INKCELL_CAPTURE_WIDTH,
                                              INKCELL_CAPTURE_HEIGHT, INKCELL_SCALE(2)) < 0,
                         "the capture should open");
    struct inkcell_draw_state *const state = inkcell_capture_state(capture);
    struct inkcell_focus_item storage[POINTER_STORAGE];
    struct inkcell_focus_map map;
    inkcell_focus_begin(&map, storage, POINTER_STORAGE);
    inkcell_fb_set_focus_map(state, &map);

    const struct inkcell_button_action items[] = {
        {.button = INKCELL_BUTTON_A, .label = INKCELL_STR_KEY_DELETE},
        {.button = INKCELL_BUTTON_SHOULDERS, .label = INKCELL_STR_KEY_SPACE},
        {.button = INKCELL_BUTTON_QUIT, .label = INKCELL_STR_KEY_CANCEL},
    };
    const struct inkcell_fb_action_bar bar = {.items = items, .count = 3U, .status = NULL};
    const struct inkcell_fb_layout layout = inkcell_fb_layout_begin(state, true, false);
    inkcell_fb_draw_action_bar(state, &layout, &bar);

    struct inkcell_focus_rect a;
    struct inkcell_focus_rect l1;
    struct inkcell_focus_rect r1;
    const bool drawn = inkcell_focus_rect_of(&map, INKCELL_FOCUS_KEY(INKCELL_KEY_A), &a) &&
                       inkcell_focus_rect_of(&map, INKCELL_FOCUS_KEY(INKCELL_KEY_L1), &l1) &&
                       inkcell_focus_rect_of(&map, INKCELL_FOCUS_KEY(INKCELL_KEY_R1), &r1);
    INKCELL_TEST_FAIL_IF_CLEANUP(!drawn, inkcell_capture_close(capture),
                                 "every hint should register the key it names");
    INKCELL_TEST_FAIL_IF_CLEANUP(
        map.count != 3U, inkcell_capture_close(capture),
        "A, L1 and R1, and nothing for quit - the close box is the way out");
    INKCELL_TEST_FAIL_IF_CLEANUP(a.y < layout.footer_y, inkcell_capture_close(capture),
                                 "the hints are in the footer");
    INKCELL_TEST_FAIL_IF_CLEANUP(l1.x + l1.w != r1.x || l1.y != r1.y,
                                 inkcell_capture_close(capture),
                                 "L/R is one hint split down the middle");
    INKCELL_TEST_FAIL_IF_CLEANUP(a.x + a.w > l1.x, inkcell_capture_close(capture),
                                 "the hints are in the order they were offered");
    INKCELL_TEST_FAIL_IF_CLEANUP(inkcell_focus_first(&map) != INKCELL_FOCUS_NONE,
                                 inkcell_capture_close(capture),
                                 "a d-pad must not be able to start on a hint");

    struct inkcell_pointer pointer;
    inkcell_pointer_reset(&pointer);
    inkcell_pointer_down(&pointer, &map, a.x + a.w - 1, a.y + a.h / 2);
    const struct inkcell_pointer_result r =
        inkcell_pointer_up(&pointer, &map, a.x + a.w - 1, a.y + a.h / 2);
    INKCELL_TEST_FAIL_IF_CLEANUP(r.kind != INKCELL_POINTER_KEY || r.key != INKCELL_KEY_A,
                                 inkcell_capture_close(capture),
                                 "clicking the verb beside the cap presses the cap");

    inkcell_capture_close(capture);
    record_success(test_name);
}

INKCELL_TEST_CASE(pointer_action_bar_is_verbs_and_leaves_out_what_the_pointer_has, unit) {
    struct inkcell_capture *capture = NULL;
    INKCELL_TEST_FAIL_IF(inkcell_capture_open(&capture, INKCELL_CAPTURE_WIDTH,
                                              INKCELL_CAPTURE_HEIGHT, INKCELL_SCALE(2)) < 0,
                         "the capture should open");
    struct inkcell_draw_state *const state = inkcell_capture_state(capture);
    state->pointer = true;
    struct inkcell_focus_item storage[POINTER_STORAGE];
    struct inkcell_focus_map map;
    inkcell_focus_begin(&map, storage, POINTER_STORAGE);
    inkcell_fb_set_focus_map(state, &map);

    const struct inkcell_button_action items[] = {
        {.button = INKCELL_BUTTON_A, .label = INKCELL_STR_KEY_DELETE},
        {.button = INKCELL_BUTTON_B, .label = INKCELL_STR_KEY_CANCEL},
        {.button = INKCELL_BUTTON_UP_DOWN, .label = INKCELL_STR_KEY_SPACE},
        {.button = INKCELL_BUTTON_LEFT_RIGHT, .label = INKCELL_STR_KEY_SPACE},
        {.button = INKCELL_BUTTON_QUIT, .label = INKCELL_STR_KEY_CANCEL},
    };
    const struct inkcell_fb_action_bar bar = {.items = items, .count = 5U, .status = NULL};
    struct inkcell_fb_layout layout = inkcell_fb_layout_begin(state, true, true);
    const struct inkcell_fb_app_bar heading = {.title = "Title"};
    inkcell_fb_draw_app_bar(state, &layout, &heading);
    inkcell_fb_draw_action_bar(state, &layout, &bar);

    struct inkcell_focus_rect a;
    struct inkcell_focus_rect back;
    struct inkcell_focus_rect left;
    struct inkcell_focus_rect up;
    INKCELL_TEST_FAIL_IF_CLEANUP(!inkcell_focus_rect_of(&map, INKCELL_FOCUS_KEY(INKCELL_KEY_A), &a),
                                 inkcell_capture_close(capture), "a face button's verb is A");
    INKCELL_TEST_FAIL_IF_CLEANUP(
        !inkcell_focus_rect_of(&map, INKCELL_FOCUS_KEY(INKCELL_KEY_LEFT), &left),
        inkcell_capture_close(capture), "a pair the pointer has no other way to press stays");
    INKCELL_TEST_FAIL_IF_CLEANUP(
        inkcell_focus_rect_of(&map, INKCELL_FOCUS_KEY(INKCELL_KEY_UP), &up),
        inkcell_capture_close(capture), "the arrows are the wheel, and leave the bar");
    INKCELL_TEST_FAIL_IF_CLEANUP(
        !inkcell_focus_rect_of(&map, INKCELL_FOCUS_KEY(INKCELL_KEY_B), &back),
        inkcell_capture_close(capture), "the app bar's arrow is B");
    INKCELL_TEST_FAIL_IF_CLEANUP(back.y >= layout.footer_y, inkcell_capture_close(capture),
                                 "...and it is the arrow, not a B left in the footer");
    INKCELL_TEST_FAIL_IF_CLEANUP(map.count != 4U, inkcell_capture_close(capture),
                                 "the arrow, A, and the pair's two halves - nothing else");
    INKCELL_TEST_FAIL_IF_CLEANUP(a.x + a.w > left.x, inkcell_capture_close(capture),
                                 "what is left out gives its room to what follows");

    inkcell_capture_close(capture);
    record_success(test_name);
}

INKCELL_TEST_CASE(pointer_off_keeps_the_keycaps, unit) {
    struct inkcell_capture *capture = NULL;
    INKCELL_TEST_FAIL_IF(inkcell_capture_open(&capture, INKCELL_CAPTURE_WIDTH,
                                              INKCELL_CAPTURE_HEIGHT, INKCELL_SCALE(2)) < 0,
                         "the capture should open");
    struct inkcell_draw_state *const state = inkcell_capture_state(capture);
    struct inkcell_focus_item storage[POINTER_STORAGE];
    struct inkcell_focus_map map;
    inkcell_focus_begin(&map, storage, POINTER_STORAGE);
    inkcell_fb_set_focus_map(state, &map);

    const struct inkcell_button_action items[] = {
        {.button = INKCELL_BUTTON_B, .label = INKCELL_STR_KEY_CANCEL},
        {.button = INKCELL_BUTTON_UP_DOWN, .label = INKCELL_STR_KEY_SPACE},
    };
    const struct inkcell_fb_action_bar bar = {.items = items, .count = 2U, .status = NULL};
    const struct inkcell_fb_layout layout = inkcell_fb_layout_begin(state, true, true);
    inkcell_fb_draw_action_bar(state, &layout, &bar);

    struct inkcell_focus_rect b;
    INKCELL_TEST_FAIL_IF_CLEANUP(
        !inkcell_focus_rect_of(&map, INKCELL_FOCUS_KEY(INKCELL_KEY_B), &b) || b.y < layout.footer_y,
        inkcell_capture_close(capture), "on a panel, B stays in the footer beside its arrow");
    INKCELL_TEST_FAIL_IF_CLEANUP(map.count != 3U, inkcell_capture_close(capture),
                                 "B, and the arrows' two halves");
    inkcell_capture_close(capture);
    record_success(test_name);
}

INKCELL_TEST_CASE(pointer_keeps_back_in_the_bar_when_no_arrow_was_drawn, unit) {
    struct inkcell_capture *capture = NULL;
    INKCELL_TEST_FAIL_IF(inkcell_capture_open(&capture, INKCELL_CAPTURE_WIDTH,
                                              INKCELL_CAPTURE_HEIGHT, INKCELL_SCALE(2)) < 0,
                         "the capture should open");
    struct inkcell_draw_state *const state = inkcell_capture_state(capture);
    state->pointer = true;
    struct inkcell_focus_item storage[POINTER_STORAGE];
    struct inkcell_focus_map map;
    inkcell_focus_begin(&map, storage, POINTER_STORAGE);
    inkcell_fb_set_focus_map(state, &map);

    /* A screen with no heading - the map - whose way back is B all the same. */
    const struct inkcell_button_action items[] = {
        {.button = INKCELL_BUTTON_B, .label = INKCELL_STR_KEY_CANCEL},
    };
    const struct inkcell_fb_action_bar bar = {.items = items, .count = 1U, .status = NULL};
    const struct inkcell_fb_layout layout = inkcell_fb_layout_begin(state, true, true);
    inkcell_fb_draw_action_bar(state, &layout, &bar);

    struct inkcell_focus_rect b;
    INKCELL_TEST_FAIL_IF_CLEANUP(
        !inkcell_focus_rect_of(&map, INKCELL_FOCUS_KEY(INKCELL_KEY_B), &b) || b.y < layout.footer_y,
        inkcell_capture_close(capture), "with no arrow on the frame, the bar is the way back");
    inkcell_capture_close(capture);
    record_success(test_name);
}
