/*
 * Which held keys repeat: the four directions always, and whatever else the application's
 * repeat policy says yes to at the press.
 *
 * No timerfd: a zeroed struct has none, and a hold is stepped by hand with
 * inkcell_input_repeat_tick(), which is what the timer calls.
 */

#include "framework/inkcell_test.h"

#include "inkcell/ui/input.h"
#include "inkcell/ui/input_codes.h"

#include <stdbool.h>
#include <string.h>

struct repeat_capture {
    enum inkcell_key keys[16];
    unsigned count;
};

static void repeat_capture_key(void *userdata, enum inkcell_key key) {
    struct repeat_capture *capture = (struct repeat_capture *)userdata;
    if (capture->count < sizeof capture->keys / sizeof capture->keys[0]) {
        capture->keys[capture->count] = key;
    }
    capture->count++;
}

/* What an on-screen keyboard wants while it is up: the backspace and the caret. */
static bool repeat_editing(void *userdata, enum inkcell_key key) {
    const bool *typing = (const bool *)userdata;
    return *typing && (key == INKCELL_KEY_X || key == INKCELL_KEY_L2 || key == INKCELL_KEY_R2 ||
                       key == INKCELL_KEY_B);
}

INKCELL_TEST_CASE(input_repeat_policy_chooses_the_keys_that_repeat, unit) {
    inkcell_input_reload_key_repeat();
    struct repeat_capture capture;
    memset(&capture, 0, sizeof capture);
    struct inkcell_input input;
    memset(&input, 0, sizeof input);
    inkcell_input_set_handler(&input, repeat_capture_key, &capture);

    /* Unasked, a face button is one press however long it is held. */
    inkcell_input_handle_event(&input, EV_KEY, KEY_X, 1);
    INKCELL_TEST_FAIL_IF(inkcell_input_repeat_key(&input) != INKCELL_KEY_NONE,
                         "with no policy X should not repeat");
    inkcell_input_handle_event(&input, EV_KEY, KEY_X, 0);

    bool typing = false;
    inkcell_input_set_repeat_policy(&input, repeat_editing, &typing);
    inkcell_input_handle_event(&input, EV_KEY, KEY_X, 1);
    INKCELL_TEST_FAIL_IF(inkcell_input_repeat_key(&input) != INKCELL_KEY_NONE,
                         "a policy saying no should leave X a single press");
    inkcell_input_handle_event(&input, EV_KEY, KEY_X, 0);

    /* Typing: X repeats on the directions' timer, and the kernel's autorepeat is dropped. */
    typing = true;
    capture.count = 0U;
    inkcell_input_handle_event(&input, EV_KEY, KEY_X, 1);
    inkcell_input_handle_event(&input, EV_KEY, KEY_X, 2);
    inkcell_input_repeat_tick(&input);
    inkcell_input_repeat_tick(&input);
    INKCELL_TEST_FAIL_IF(capture.count != 3U || capture.keys[2] != INKCELL_KEY_X,
                         "a held X the policy chose should repeat, once per tick");
    inkcell_input_handle_event(&input, EV_KEY, KEY_X, 0);
    inkcell_input_repeat_tick(&input);
    INKCELL_TEST_FAIL_IF(capture.count != 3U ||
                             inkcell_input_repeat_key(&input) != INKCELL_KEY_NONE,
                         "releasing X should end its repeat");

    /* A trigger is an axis: back under the threshold is its release. */
    capture.count = 0U;
    inkcell_input_handle_event(&input, EV_ABS, ABS_Z, 255);
    inkcell_input_repeat_tick(&input);
    INKCELL_TEST_FAIL_IF(capture.count != 2U || capture.keys[1] != INKCELL_KEY_L2,
                         "a held trigger the policy chose should repeat");
    inkcell_input_handle_event(&input, EV_ABS, ABS_Z, 0);
    INKCELL_TEST_FAIL_IF(inkcell_input_repeat_key(&input) != INKCELL_KEY_NONE,
                         "the trigger coming back up should end its repeat");

    /* B is never the policy's to give: its hold is B_HELD, not forty Bs. */
    capture.count = 0U;
    inkcell_input_handle_event(&input, EV_KEY, KEY_BACKSPACE, 1);
    inkcell_input_repeat_tick(&input);
    inkcell_input_repeat_tick(&input);
    INKCELL_TEST_FAIL_IF(capture.count > 2U || capture.keys[0] != INKCELL_KEY_B,
                         "B should hold, never repeat, whatever the policy says");
    inkcell_input_handle_event(&input, EV_KEY, KEY_BACKSPACE, 0);
    record_success(test_name);
}
