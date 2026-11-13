#define _POSIX_C_SOURCE 200809L

/*
 * A phone's buttons: power and volume, to a host that asked for them, with their phase - and to a
 * host that did not, nothing at all, as before.
 */

#include "framework/inkcell_test.h"

#include "inkcell/ui/input.h"
#include "inkcell/ui/input_codes.h"

#include <string.h>

struct buttons_seen {
    int count;
    enum inkcell_side_button button;
    bool down;
    int keys;
};

static void buttons_capture(void *userdata, enum inkcell_side_button button, bool down) {
    struct buttons_seen *seen = userdata;
    seen->count++;
    seen->button = button;
    seen->down = down;
}

static void buttons_key(void *userdata, enum inkcell_key key) {
    (void)key;
    ((struct buttons_seen *)userdata)->keys++;
}

INKCELL_TEST_CASE(input_buttons_map_power_and_volume, unit) {
    INKCELL_TEST_FAIL_IF(
        inkcell_input_map_side_button(KEY_POWER) != INKCELL_SIDE_POWER ||
            inkcell_input_map_side_button(KEY_VOLUMEUP) != INKCELL_SIDE_VOLUME_UP ||
            inkcell_input_map_side_button(KEY_VOLUMEDOWN) != INKCELL_SIDE_VOLUME_DOWN,
        "power, volume up and volume down");
    INKCELL_TEST_FAIL_IF(inkcell_input_map_side_button(KEY_ESC) != INKCELL_SIDE_NONE,
                         "anything else is no button");
}

INKCELL_TEST_CASE(input_buttons_come_with_their_phase_to_a_host_that_asked, unit) {
    struct inkcell_input input;
    memset(&input, 0, sizeof input);
    input.repeat_timer_fd = -1;
    input.repeat_source_fd = -1;
    struct buttons_seen seen;
    memset(&seen, 0, sizeof seen);
    inkcell_input_set_handler(&input, buttons_key, &seen);

    inkcell_input_handle_device_event(&input, -1, EV_KEY, KEY_VOLUMEUP, 1);
    INKCELL_TEST_FAIL_IF(seen.count != 0, "a host that never asked hears no button");

    INKCELL_TEST_FAIL_IF(inkcell_input_watch_side_buttons(&input, buttons_capture, &seen) < 0,
                         "asking for them");
    inkcell_input_handle_device_event(&input, -1, EV_KEY, KEY_POWER, 1);
    INKCELL_TEST_FAIL_IF(seen.count != 1 || seen.button != INKCELL_SIDE_POWER || !seen.down,
                         "power, down");
    inkcell_input_handle_device_event(&input, -1, EV_KEY, KEY_POWER, 2);
    INKCELL_TEST_FAIL_IF(seen.count != 1, "the kernel's repeat of a held button is not passed on");
    inkcell_input_handle_device_event(&input, -1, EV_KEY, KEY_POWER, 0);
    INKCELL_TEST_FAIL_IF(seen.count != 2 || seen.down, "and up");
    inkcell_input_handle_device_event(&input, -1, EV_KEY, KEY_VOLUMEDOWN, 1);
    INKCELL_TEST_FAIL_IF(seen.button != INKCELL_SIDE_VOLUME_DOWN || !seen.down, "volume down");
    INKCELL_TEST_FAIL_IF(seen.keys != 0 || input.stopping,
                         "a button is never a key, nor a quit key");
}
