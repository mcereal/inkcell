#define _POSIX_C_SOURCE 200809L

/*
 * A touchscreen: the evdev reports a digitizer sends, decoded into the one contact that counts,
 * and that contact as a tap or a drag over the frame's boxes.
 *
 * The reports are played through inkcell_input_handle_device_event() on a pretend descriptor,
 * in the order a protocol B driver sends them - so the decoder is tested on the stream rather
 * than on the calls it makes.
 */

#include "framework/inkcell_test.h"

#include "inkcell/ui/focus.h"
#include "inkcell/ui/input.h"
#include "inkcell/ui/input_codes.h"
#include "inkcell/ui/touch.h"

#include <string.h>

#define TOUCH_FD 42
#define PAD_FD 7
#define TOUCH_LOG 16U

struct touch_log {
    enum inkcell_touch_phase phase[TOUCH_LOG];
    uint32_t x[TOUCH_LOG];
    uint32_t y[TOUCH_LOG];
    size_t count;
    int keys;
};

static void touch_log_on_touch(void *userdata, enum inkcell_touch_phase phase, uint32_t nx,
                               uint32_t ny) {
    struct touch_log *log = userdata;
    if (log->count < TOUCH_LOG) {
        log->phase[log->count] = phase;
        log->x[log->count] = nx;
        log->y[log->count] = ny;
    }
    log->count++;
}

static void touch_log_on_key(void *userdata, enum inkcell_key key) {
    (void)key;
    ((struct touch_log *)userdata)->keys++;
}

/* A digitizer 0..1079 by 0..2399, as a phone's is. */
static void touch_attach(struct inkcell_input *input, struct touch_log *log,
                         enum inkcell_input_touch_kind kind) {
    memset(input, 0, sizeof *input);
    memset(log, 0, sizeof *log);
    input->repeat_timer_fd = -1;
    input->repeat_source_fd = -1;
    const struct inkcell_input_touch_axes axes = {
        .kind = kind, .min_x = 0, .max_x = 1079, .min_y = 0, .max_y = 2399};
    (void)inkcell_input_attach_touch(input, TOUCH_FD, &axes);
    inkcell_input_set_touch_handler(input, touch_log_on_touch, log);
    inkcell_input_set_handler(input, touch_log_on_key, log);
}

static void ev(struct inkcell_input *input, uint16_t type, uint16_t code, int32_t value) {
    inkcell_input_handle_device_event(input, TOUCH_FD, type, code, value);
}

static void syn(struct inkcell_input *input) {
    ev(input, EV_SYN, SYN_REPORT, 0);
}

/* ---- the frame ------------------------------------------------------------------------- */

INKCELL_TEST_CASE(touch_to_frame_turns_with_the_panel, unit) {
    int x = -1;
    int y = -1;
    const uint32_t far = INKCELL_TOUCH_UNIT - 1U;
    INKCELL_TEST_FAIL_IF(!inkcell_touch_to_frame(0U, 0U, 1080U, 2400U, 0U, &x, &y) || x != 0 ||
                             y != 0,
                         "unturned, the digitizer's origin is the frame's");
    INKCELL_TEST_FAIL_IF(!inkcell_touch_to_frame(far, far, 1080U, 2400U, 0U, &x, &y) || x != 1079 ||
                             y != 2399,
                         "...and its far corner the last pixel, never one past it");
    INKCELL_TEST_FAIL_IF(!inkcell_touch_to_frame(INKCELL_TOUCH_UNIT / 2U, INKCELL_TOUCH_UNIT / 4U,
                                                 1080U, 2400U, 0U, &x, &y) ||
                             x != 540 || y != 600,
                         "a fraction is that share of the frame");
    INKCELL_TEST_FAIL_IF(!inkcell_touch_to_frame(0U, 0U, 1080U, 2400U, 180U, &x, &y) || x != 1079 ||
                             y != 2399,
                         "upside down, the origin is the far corner");
    /* Turned a quarter: a landscape frame over a portrait digitizer. */
    INKCELL_TEST_FAIL_IF(!inkcell_touch_to_frame(0U, 0U, 2400U, 1080U, 90U, &x, &y) || x != 2399 ||
                             y != 0,
                         "at 90 the digitizer's origin is the frame's top right");
    INKCELL_TEST_FAIL_IF(!inkcell_touch_to_frame(0U, 0U, 2400U, 1080U, 270U, &x, &y) || x != 0 ||
                             y != 1079,
                         "at 270 it is the bottom left");
    INKCELL_TEST_FAIL_IF(inkcell_touch_to_frame(0U, 0U, 1080U, 2400U, 45U, &x, &y),
                         "a rotation that is not a right angle is refused");
    INKCELL_TEST_FAIL_IF(inkcell_touch_to_frame(0U, 0U, 0U, 2400U, 0U, &x, &y),
                         "so is an empty frame");
    record_success(test_name);
}

/* ---- which nodes are screens ---------------------------------------------------------- */

static void set_bit(unsigned long *bits, unsigned code) {
    bits[code / INKCELL_INPUT_BITS_PER_LONG] |= 1UL << (code % INKCELL_INPUT_BITS_PER_LONG);
}

INKCELL_TEST_CASE(touch_kind_tells_a_screen_from_a_pad_and_a_touchpad, unit) {
    unsigned long keys[INKCELL_INPUT_BIT_WORDS(KEY_MAX + 1U)];
    unsigned long axes[INKCELL_INPUT_BIT_WORDS(ABS_MAX + 1U)];
    unsigned long props[INKCELL_INPUT_BIT_WORDS(INPUT_PROP_MAX + 1U)];
    const size_t kw = sizeof keys / sizeof keys[0];
    const size_t aw = sizeof axes / sizeof axes[0];
    const size_t pw = sizeof props / sizeof props[0];

    memset(keys, 0, sizeof keys);
    memset(axes, 0, sizeof axes);
    memset(props, 0, sizeof props);
    set_bit(axes, ABS_X);
    set_bit(axes, ABS_Y);
    INKCELL_TEST_FAIL_IF(inkcell_input_touch_kind(keys, kw, axes, aw, props, pw) !=
                             INKCELL_INPUT_TOUCH_NO,
                         "a stick has ABS_X and ABS_Y and is not a screen");
    set_bit(keys, BTN_LEFT);
    INKCELL_TEST_FAIL_IF(inkcell_input_touch_kind(keys, kw, axes, aw, props, pw) !=
                             INKCELL_INPUT_TOUCH_SINGLE,
                         "an absolute pointer with a left button is a VM's tablet: a screen");
    set_bit(props, INPUT_PROP_POINTER);
    INKCELL_TEST_FAIL_IF(inkcell_input_touch_kind(keys, kw, axes, aw, props, pw) !=
                             INKCELL_INPUT_TOUCH_NO,
                         "unless it says it moves a cursor");
    memset(keys, 0, sizeof keys);
    memset(props, 0, sizeof props);
    set_bit(keys, BTN_TOUCH);
    INKCELL_TEST_FAIL_IF(inkcell_input_touch_kind(keys, kw, axes, aw, props, pw) !=
                             INKCELL_INPUT_TOUCH_SINGLE,
                         "with BTN_TOUCH it is a single-contact screen");
    set_bit(axes, ABS_MT_SLOT);
    set_bit(axes, ABS_MT_TRACKING_ID);
    set_bit(axes, ABS_MT_POSITION_X);
    set_bit(axes, ABS_MT_POSITION_Y);
    INKCELL_TEST_FAIL_IF(inkcell_input_touch_kind(keys, kw, axes, aw, props, pw) !=
                             INKCELL_INPUT_TOUCH_MULTI,
                         "slots make it multitouch, read through them");
    INKCELL_TEST_FAIL_IF(inkcell_input_touch_kind(keys, kw, axes, aw, NULL, 0U) !=
                             INKCELL_INPUT_TOUCH_MULTI,
                         "a node that cannot give its properties is taken at its axes");
    set_bit(props, INPUT_PROP_POINTER);
    INKCELL_TEST_FAIL_IF(inkcell_input_touch_kind(keys, kw, axes, aw, props, pw) !=
                             INKCELL_INPUT_TOUCH_NO,
                         "a touchpad moves a cursor and has no point on the frame");
    record_success(test_name);
}

/* ---- the reports ---------------------------------------------------------------------- */

INKCELL_TEST_CASE(touch_multi_reports_one_contact_down_moving_and_up, unit) {
    static struct inkcell_input input;
    static struct touch_log log;
    touch_attach(&input, &log, INKCELL_INPUT_TOUCH_MULTI);

    /* What a driver with pointer emulation sends: the slot, then the legacy echo. */
    ev(&input, EV_ABS, ABS_MT_SLOT, 0);
    ev(&input, EV_ABS, ABS_MT_TRACKING_ID, 17);
    ev(&input, EV_ABS, ABS_MT_POSITION_X, 540);
    ev(&input, EV_ABS, ABS_MT_POSITION_Y, 1200);
    ev(&input, EV_KEY, BTN_TOUCH, 1);
    ev(&input, EV_ABS, ABS_X, 540);
    ev(&input, EV_ABS, ABS_Y, 1200);
    INKCELL_TEST_FAIL_IF(log.count != 0U, "nothing is said before the report ends");
    syn(&input);
    INKCELL_TEST_FAIL_IF(log.count != 1U || log.phase[0] != INKCELL_TOUCH_DOWN,
                         "the report's end is the contact going down");
    INKCELL_TEST_FAIL_IF(log.x[0] != 32768U || log.y[0] != 32768U,
                         "the middle of the digitizer is half of each axis");

    syn(&input);
    INKCELL_TEST_FAIL_IF(log.count != 1U, "a report that moves nothing says nothing");
    ev(&input, EV_ABS, ABS_MT_POSITION_Y, 1260);
    syn(&input);
    INKCELL_TEST_FAIL_IF(log.count != 2U || log.phase[1] != INKCELL_TOUCH_MOVE ||
                             log.y[1] <= log.y[0],
                         "a moved contact is a move, further down");

    ev(&input, EV_ABS, ABS_MT_TRACKING_ID, -1);
    ev(&input, EV_KEY, BTN_TOUCH, 0);
    syn(&input);
    INKCELL_TEST_FAIL_IF(log.count != 3U || log.phase[2] != INKCELL_TOUCH_UP ||
                             log.y[2] != log.y[1],
                         "a lifted contact is an up where it last was");
    INKCELL_TEST_FAIL_IF(log.keys != 0, "BTN_TOUCH on the screen is not a button");
    record_success(test_name);
}

INKCELL_TEST_CASE(touch_multi_follows_the_first_finger_only, unit) {
    static struct inkcell_input input;
    static struct touch_log log;
    touch_attach(&input, &log, INKCELL_INPUT_TOUCH_MULTI);

    ev(&input, EV_ABS, ABS_MT_TRACKING_ID, 1);
    ev(&input, EV_ABS, ABS_MT_POSITION_X, 100);
    ev(&input, EV_ABS, ABS_MT_POSITION_Y, 100);
    syn(&input);
    /* A second finger, in slot 1, moving about. */
    ev(&input, EV_ABS, ABS_MT_SLOT, 1);
    ev(&input, EV_ABS, ABS_MT_TRACKING_ID, 2);
    ev(&input, EV_ABS, ABS_MT_POSITION_X, 900);
    ev(&input, EV_ABS, ABS_MT_POSITION_Y, 900);
    syn(&input);
    ev(&input, EV_ABS, ABS_MT_POSITION_Y, 1500);
    syn(&input);
    INKCELL_TEST_FAIL_IF(log.count != 1U, "a second finger is not the contact that counts");

    /* The first lifts; the second is still down and must not become a tap of its own. */
    ev(&input, EV_ABS, ABS_MT_SLOT, 0);
    ev(&input, EV_ABS, ABS_MT_TRACKING_ID, -1);
    syn(&input);
    ev(&input, EV_ABS, ABS_MT_SLOT, 1);
    ev(&input, EV_ABS, ABS_MT_POSITION_Y, 1600);
    syn(&input);
    ev(&input, EV_ABS, ABS_MT_TRACKING_ID, -1);
    syn(&input);
    INKCELL_TEST_FAIL_IF(log.count != 2U || log.phase[1] != INKCELL_TOUCH_UP,
                         "the first finger's lift is the up, and the second is not promoted");

    /* A fresh finger after all have lifted counts again, in whatever slot it lands. */
    ev(&input, EV_ABS, ABS_MT_SLOT, 3);
    ev(&input, EV_ABS, ABS_MT_TRACKING_ID, 3);
    ev(&input, EV_ABS, ABS_MT_POSITION_X, 10);
    ev(&input, EV_ABS, ABS_MT_POSITION_Y, 10);
    syn(&input);
    INKCELL_TEST_FAIL_IF(log.count != 3U || log.phase[2] != INKCELL_TOUCH_DOWN,
                         "the next finger down counts");
    record_success(test_name);
}

INKCELL_TEST_CASE(touch_dropped_events_cancel_rather_than_tap, unit) {
    static struct inkcell_input input;
    static struct touch_log log;
    touch_attach(&input, &log, INKCELL_INPUT_TOUCH_MULTI);
    ev(&input, EV_ABS, ABS_MT_TRACKING_ID, 1);
    ev(&input, EV_ABS, ABS_MT_POSITION_X, 100);
    ev(&input, EV_ABS, ABS_MT_POSITION_Y, 100);
    syn(&input);
    ev(&input, EV_SYN, SYN_DROPPED, 0);
    ev(&input, EV_ABS, ABS_MT_TRACKING_ID, -1);
    syn(&input);
    INKCELL_TEST_FAIL_IF(log.count != 2U || log.phase[1] != INKCELL_TOUCH_CANCEL,
                         "an overrun cancels the contact, and the rest of that report is ignored");

    ev(&input, EV_ABS, ABS_MT_TRACKING_ID, 4);
    syn(&input);
    inkcell_input_device_lost(&input, TOUCH_FD);
    INKCELL_TEST_FAIL_IF(log.count != 4U || log.phase[3] != INKCELL_TOUCH_CANCEL,
                         "a screen that goes away cancels what is on it");
    record_success(test_name);
}

INKCELL_TEST_CASE(touch_single_reads_btn_touch, unit) {
    static struct inkcell_input input;
    static struct touch_log log;
    touch_attach(&input, &log, INKCELL_INPUT_TOUCH_SINGLE);
    ev(&input, EV_ABS, ABS_X, 1079);
    ev(&input, EV_ABS, ABS_Y, 0);
    ev(&input, EV_KEY, BTN_TOUCH, 1);
    syn(&input);
    ev(&input, EV_ABS, ABS_X, 1000);
    syn(&input);
    ev(&input, EV_KEY, BTN_TOUCH, 0);
    syn(&input);
    INKCELL_TEST_FAIL_IF(log.count != 3U || log.phase[0] != INKCELL_TOUCH_DOWN ||
                             log.phase[1] != INKCELL_TOUCH_MOVE || log.phase[2] != INKCELL_TOUCH_UP,
                         "down, move, up");
    INKCELL_TEST_FAIL_IF(log.x[0] != INKCELL_TOUCH_UNIT - 1U || log.y[0] != 0U,
                         "the far edge of an axis is the last unit");
    record_success(test_name);
}

INKCELL_TEST_CASE(touch_single_reads_a_tablets_btn_left, unit) {
    static struct inkcell_input input;
    static struct touch_log log;
    touch_attach(&input, &log, INKCELL_INPUT_TOUCH_SINGLE);
    ev(&input, EV_ABS, ABS_X, 500);
    ev(&input, EV_ABS, ABS_Y, 500);
    syn(&input);
    INKCELL_TEST_FAIL_IF(log.count != 0U, "a pointer moving with no button down is no contact");
    ev(&input, EV_KEY, BTN_LEFT, 1);
    syn(&input);
    ev(&input, EV_ABS, ABS_Y, 900);
    syn(&input);
    ev(&input, EV_KEY, BTN_LEFT, 0);
    syn(&input);
    INKCELL_TEST_FAIL_IF(log.count != 3U || log.phase[0] != INKCELL_TOUCH_DOWN ||
                             log.phase[1] != INKCELL_TOUCH_MOVE || log.phase[2] != INKCELL_TOUCH_UP,
                         "the button is the finger: down, drag, up");
    record_success(test_name);
}

INKCELL_TEST_CASE(touch_a_tap_inside_one_report_is_still_a_tap, unit) {
    static struct inkcell_input input;
    static struct touch_log log;
    touch_attach(&input, &log, INKCELL_INPUT_TOUCH_SINGLE);
    ev(&input, EV_ABS, ABS_X, 100);
    ev(&input, EV_KEY, BTN_TOUCH, 1);
    ev(&input, EV_KEY, BTN_TOUCH, 0);
    syn(&input);
    INKCELL_TEST_FAIL_IF(log.count != 2U || log.phase[0] != INKCELL_TOUCH_DOWN ||
                             log.phase[1] != INKCELL_TOUCH_UP,
                         "a single-touch down and up in one report is a down and an up");

    static struct inkcell_input multi;
    static struct touch_log mlog;
    touch_attach(&multi, &mlog, INKCELL_INPUT_TOUCH_MULTI);
    ev(&multi, EV_ABS, ABS_MT_TRACKING_ID, 1);
    ev(&multi, EV_ABS, ABS_MT_POSITION_X, 100);
    ev(&multi, EV_ABS, ABS_MT_TRACKING_ID, -1);
    syn(&multi);
    INKCELL_TEST_FAIL_IF(mlog.count != 2U || mlog.phase[0] != INKCELL_TOUCH_DOWN ||
                             mlog.phase[1] != INKCELL_TOUCH_UP,
                         "a slot down and lifted in one report is a down and an up");

    /* Lifted and down again in the same slot: two contacts, not one that jumped. */
    ev(&multi, EV_ABS, ABS_MT_TRACKING_ID, 2);
    syn(&multi);
    ev(&multi, EV_ABS, ABS_MT_TRACKING_ID, -1);
    ev(&multi, EV_ABS, ABS_MT_TRACKING_ID, 3);
    ev(&multi, EV_ABS, ABS_MT_POSITION_X, 900);
    syn(&multi);
    INKCELL_TEST_FAIL_IF(mlog.count != 5U || mlog.phase[3] != INKCELL_TOUCH_UP ||
                             mlog.phase[4] != INKCELL_TOUCH_DOWN,
                         "a slot lifted and reused in one report is an up and a down");
    record_success(test_name);
}

INKCELL_TEST_CASE(touch_screen_events_stay_off_the_pad, unit) {
    static struct inkcell_input input;
    static struct touch_log log;
    touch_attach(&input, &log, INKCELL_INPUT_TOUCH_MULTI);
    inkcell_input_handle_device_event(&input, PAD_FD, EV_KEY, BTN_SOUTH, 1);
    INKCELL_TEST_FAIL_IF(log.keys != 1, "a pad beside the screen still presses keys");
    inkcell_input_handle_device_event(&input, PAD_FD, EV_ABS, ABS_MT_TRACKING_ID, 9);
    inkcell_input_handle_device_event(&input, PAD_FD, EV_SYN, SYN_REPORT, 0);
    INKCELL_TEST_FAIL_IF(log.count != 0U, "only the screen's own node is read as a screen");
    record_success(test_name);
}

/* ---- taps and drags ------------------------------------------------------------------- */

enum { T_ID_ROW = 1, T_ID_ROW_BELOW };

#define TOUCH_BOXES 8U

static void touch_map(struct inkcell_focus_map *map, struct inkcell_focus_item *storage) {
    inkcell_focus_begin(map, storage, TOUCH_BOXES);
    (void)inkcell_focus_add(map, T_ID_ROW, 0, 0, 1080, 200);
    (void)inkcell_focus_add(map, T_ID_ROW_BELOW, 0, 200, 1080, 200);
    (void)inkcell_focus_add_target(map, INKCELL_FOCUS_KEY(INKCELL_KEY_B), 0, 2300, 200, 100, 0);
}

INKCELL_TEST_CASE(touch_tap_lands_on_the_box_it_went_down_on, unit) {
    struct inkcell_focus_item storage[TOUCH_BOXES];
    struct inkcell_focus_map map;
    touch_map(&map, storage);
    struct inkcell_touch touch;
    inkcell_touch_init(&touch);

    (void)inkcell_touch_handle(&touch, &map, INKCELL_TOUCH_DOWN, 500, 190, 1080U, 2400U);
    /* A wobble that crosses into the row below without leaving the slop. */
    (void)inkcell_touch_handle(&touch, &map, INKCELL_TOUCH_MOVE, 505, 205, 1080U, 2400U);
    struct inkcell_touch_result r =
        inkcell_touch_handle(&touch, &map, INKCELL_TOUCH_UP, 505, 205, 1080U, 2400U);
    INKCELL_TEST_FAIL_IF(r.kind != INKCELL_TOUCH_TAP || r.press.kind != INKCELL_POINTER_CLICK ||
                             r.press.target != T_ID_ROW,
                         "a tap that wobbled over an edge is the box it went down on");

    (void)inkcell_touch_handle(&touch, &map, INKCELL_TOUCH_DOWN, 50, 2350, 1080U, 2400U);
    r = inkcell_touch_handle(&touch, &map, INKCELL_TOUCH_UP, 50, 2350, 1080U, 2400U);
    INKCELL_TEST_FAIL_IF(r.press.kind != INKCELL_POINTER_KEY || r.press.key != INKCELL_KEY_B,
                         "a key's box is that key, as it is for a click");

    (void)inkcell_touch_handle(&touch, &map, INKCELL_TOUCH_DOWN, 500, 1000, 1080U, 2400U);
    r = inkcell_touch_handle(&touch, &map, INKCELL_TOUCH_UP, 500, 1000, 1080U, 2400U);
    INKCELL_TEST_FAIL_IF(r.kind != INKCELL_TOUCH_TAP || r.press.kind != INKCELL_POINTER_NONE,
                         "a tap on nothing is a tap, and presses nothing");

    (void)inkcell_touch_handle(&touch, &map, INKCELL_TOUCH_DOWN, 500, 100, 1080U, 2400U);
    r = inkcell_touch_handle(&touch, &map, INKCELL_TOUCH_CANCEL, 500, 100, 1080U, 2400U);
    INKCELL_TEST_FAIL_IF(r.kind != INKCELL_TOUCH_NONE, "a cancel is not a tap");
    r = inkcell_touch_handle(&touch, &map, INKCELL_TOUCH_UP, 500, 100, 1080U, 2400U);
    INKCELL_TEST_FAIL_IF(r.kind != INKCELL_TOUCH_NONE, "...and the up after it is nothing");
    record_success(test_name);
}

INKCELL_TEST_CASE(touch_drag_scrolls_with_the_finger_and_never_taps, unit) {
    struct inkcell_focus_item storage[TOUCH_BOXES];
    struct inkcell_focus_map map;
    touch_map(&map, storage);
    struct inkcell_touch touch;
    inkcell_touch_init(&touch);
    touch.slop_px = 20;
    touch.step_px = 100;

    (void)inkcell_touch_handle(&touch, &map, INKCELL_TOUCH_DOWN, 500, 100, 1080U, 2400U);
    struct inkcell_touch_result r =
        inkcell_touch_handle(&touch, &map, INKCELL_TOUCH_MOVE, 500, 130, 1080U, 2400U);
    INKCELL_TEST_FAIL_IF(r.kind != INKCELL_TOUCH_NONE,
                         "crossing the slop starts a drag and is not yet a row");
    r = inkcell_touch_handle(&touch, &map, INKCELL_TOUCH_MOVE, 500, 260, 1080U, 2400U);
    INKCELL_TEST_FAIL_IF(r.kind != INKCELL_TOUCH_SCROLL || r.steps != 1,
                         "a step of travel down is one row, and down brings earlier rows: Up");
    r = inkcell_touch_handle(&touch, &map, INKCELL_TOUCH_MOVE, 500, 300, 1080U, 2400U);
    INKCELL_TEST_FAIL_IF(r.kind != INKCELL_TOUCH_NONE, "a part of a step is carried");
    r = inkcell_touch_handle(&touch, &map, INKCELL_TOUCH_MOVE, 500, 100, 1080U, 2400U);
    INKCELL_TEST_FAIL_IF(r.kind != INKCELL_TOUCH_SCROLL || r.steps != -2,
                         "back up is the other way, and the carry does not pay for the reversal");
    r = inkcell_touch_handle(&touch, &map, INKCELL_TOUCH_UP, 500, 100, 1080U, 2400U);
    INKCELL_TEST_FAIL_IF(r.kind != INKCELL_TOUCH_NONE,
                         "a drag that ends over where it began is still not a tap");
    record_success(test_name);
}
