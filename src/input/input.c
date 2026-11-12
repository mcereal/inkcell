#define _POSIX_C_SOURCE 200809L

#include "inkcell/ui/input.h"

#include "inkcell/i18n/strings.h"
#include "inkcell/ui/input_codes.h"
#include "inkcell/ui/input_profile.h"
#include "inkcell/ui/latency.h"
#include "inkwell/base/array.h"
#include "inkwell/base/env.h"
#include "inkwell/base/fd.h"
#include "inkwell/base/log.h"
#include "inkwell/base/text.h"
#include "inkwell/runtime/loop.h"
#include "inkwell/runtime/timer.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* evdev - the devices themselves - is Linux's. Everything else in this file is the mapping from
   a code to a key, the repeat and the quit keys, which a window backend on any host drives with
   the same codes; see inkcell/ui/input_codes.h. */
#if defined(__linux__)
#include "inkwell/base/ioctl.h"

#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

/* Standard evdev codes. The Brick's gamepad device ("TRIMUI Player1") reports the face and
   system buttons through the usual BTN_ space and the d-pad as ABS_HAT0X/Y. Which BTN_ code is
   which *printed* button is the one thing that is not standard, and it is a fact about the case
   rather than about evdev - see src/ui/input/input_profile.c. SELECT and START are not quit keys:
   they sit next to the d-pad and are too easy to hit while navigating.

   KEY_POWER (116) is not one either, and that is a correction rather than an omission. It was
   here as a host-keyboard convenience, but the Brick's PMIC really does emit it - on its own
   device, axp2202-pek - so a tap of the power button, which on this hardware is the gesture
   for putting the console to sleep, tore the client down instead of suspending it. Sleep is
   the launcher's business and nothing we should answer at all. */
#define INKCELL_INPUT_MAX_QUIT_KEYS 16U

/* How far up /dev/input/eventN to look. Higher than the number of nodes any target has,
   because the numbers are handed out in plug order rather than packed: a host that has had a
   few devices come and go can have its pad well above the count of what is attached now. The
   cost of looking is one open() that fails. */
#define INKCELL_INPUT_SCAN_NODES 64U

static const uint16_t k_default_quit_keys[] = {
    KEY_ESC,  /* 1   - USB keyboard, and what most emulators map "back" to */
    KEY_MENU, /* 139 - the Brick's MENU button, the NextUI convention for leaving a pak */
    BTN_MODE, /* 316 - the same MENU button as the gamepad device reports it */
};

/* Parsed once from <PREFIX>_QUIT_KEYS so the mapping can be corrected on-device without a
   rebuild: <PREFIX>_QUIT_KEYS="139,316" meshclient ... */
static uint16_t s_quit_keys[INKCELL_INPUT_MAX_QUIT_KEYS];
static size_t s_quit_key_count;
static bool s_quit_keys_loaded;
/* The hint's text is rebuilt per call (see inkcell_input_quit_hint); only which of the two
   forms it takes is settled when the keys are loaded. */
static char s_quit_hint[64];
/* The keycap form of the same fact - "MENU", or "K139" when somebody has rebound it. Separate
   from the hint because it is not prose: a cap is what is printed on the plastic, and a key
   code stands in when there is no plastic to read it off. */
static char s_quit_cap[8];
static bool s_quit_hint_is_key_code;

static void inkcell_input_touch_cancel(struct inkcell_input *input);

static void inkcell_input_load_quit_keys(void) {
    if (s_quit_keys_loaded) {
        return;
    }
    s_quit_keys_loaded = true;

    const char *override = inkwell_env_get("QUIT_KEYS");
    if (override != NULL && override[0] != '\0') {
        const char *cursor = override;
        while (*cursor != '\0' && s_quit_key_count < INKCELL_INPUT_MAX_QUIT_KEYS) {
            char *end = NULL;
            const long value = strtol(cursor, &end, 10);
            if (end == cursor) {
                break;
            }
            if (value > 0 && value <= UINT16_MAX) {
                s_quit_keys[s_quit_key_count++] = (uint16_t)value;
            }
            cursor = end;
            while (*cursor == ',' || *cursor == ' ') {
                ++cursor;
            }
        }

        if (s_quit_key_count > 0U) {
            s_quit_hint_is_key_code = true;
            inkwell_log_info("input", "Quit keys overridden by <PREFIX>_QUIT_KEYS (%zu codes)",
                             s_quit_key_count);
            return;
        }
        inkwell_log_warn("input", "<PREFIX>_QUIT_KEYS='%s' parsed to nothing; using defaults",
                         override);
    }

    for (size_t i = 0; i < INKWELL_ARRAY_LEN(k_default_quit_keys); ++i) {
        s_quit_keys[s_quit_key_count++] = k_default_quit_keys[i];
    }
    s_quit_hint_is_key_code = false;
}

/*
 * Software key repeat.
 *
 * The kernel's own autorepeat is an EV_KEY/EV_REP feature, and the Brick reports its d-pad as
 * the absolute axes ABS_HAT0X/Y. An absolute axis never repeats however long it is held: the
 * driver sends one event on the way out of centre and one on the way back, so a 60-node roster
 * cost 60 separate presses. The repeat is therefore generated here, from a timerfd on the same
 * event loop, and it covers the arrow keys of a USB keyboard too - where the kernel would
 * repeat, ours takes over, so both devices scroll at the same speed.
 *
 * The four directions repeat, and so does any other key the application's repeat policy
 * says yes to at the press - see inkcell_input_set_repeat_policy(). Unasked, A, B, X and Y
 * confirm or go back, and a held confirm that fired forty times would be a trap rather than a
 * convenience.
 */
#define INKCELL_INPUT_REPEAT_DELAY_MS 350U /* held this long before the first repeat */
#define INKCELL_INPUT_REPEAT_MS 90U        /* then a row this often */
#define INKCELL_INPUT_REPEAT_RAMP 8U       /* rows before the interval halves */
#define INKCELL_INPUT_REPEAT_MIN_MS 25U    /* never faster than this, whatever the knobs say */
/*
 * And B, which does not repeat but does *hold*: kept down this long, it sends
 * INKCELL_KEY_B_HELD once. Longer than the repeat's delay, because the press has already gone
 * back a step and the hold is a second, larger ask - a thumb resting on B while reading must
 * not unwind a whole stack. The same timer as the repeat, since a hold and a repeat are never
 * both in progress: every press ends whatever was held.
 */
#define INKCELL_INPUT_HOLD_MS 600U

static unsigned int s_repeat_delay_ms;
static unsigned int s_repeat_interval_ms;
static bool s_repeat_loaded;
static unsigned int s_hold_ms;
static bool s_hold_loaded;

static void inkcell_input_load_key_repeat(void) {
    if (s_repeat_loaded) {
        return;
    }
    s_repeat_loaded = true;
    /* Tunable on-device from launch.sh, the same escape hatch <PREFIX>_QUIT_KEYS is; a delay
       of 0 turns hold-to-scroll off and restores one row per press. */
    s_repeat_delay_ms = (unsigned int)inkwell_env_int("KEY_REPEAT_DELAY_MS", 0, 5000,
                                                      INKCELL_INPUT_REPEAT_DELAY_MS);
    s_repeat_interval_ms =
        (unsigned int)inkwell_env_int("KEY_REPEAT_MS", 10, 2000, INKCELL_INPUT_REPEAT_MS);
}

void inkcell_input_reload_key_repeat(void) {
    s_repeat_loaded = false;
    s_repeat_delay_ms = 0U;
    s_repeat_interval_ms = 0U;
    s_hold_loaded = false;
    inkcell_input_load_key_repeat();
}

unsigned int inkcell_input_repeat_delay_ms(unsigned int repeats) {
    inkcell_input_load_key_repeat();
    if (s_repeat_delay_ms == 0U) {
        return 0U;
    }
    if (repeats == 0U) {
        return s_repeat_delay_ms;
    }
    if (repeats < INKCELL_INPUT_REPEAT_RAMP) {
        return s_repeat_interval_ms;
    }

    /* Past the ramp the finger is clearly travelling, not nudging, so the list speeds up. The
       clamps keep a hand-set interval from being made slower by the acceleration. */
    unsigned int fast = s_repeat_interval_ms / 2U;
    if (fast < INKCELL_INPUT_REPEAT_MIN_MS) {
        fast = INKCELL_INPUT_REPEAT_MIN_MS;
    }
    if (fast > s_repeat_interval_ms) {
        fast = s_repeat_interval_ms;
    }
    return fast;
}

/* <PREFIX>_KEY_HOLD_MS, beside the repeat's knobs; 0 turns the hold off. */
static unsigned int inkcell_input_hold_ms(void) {
    if (!s_hold_loaded) {
        s_hold_loaded = true;
        s_hold_ms = (unsigned int)inkwell_env_int("KEY_HOLD_MS", 0, 5000, INKCELL_INPUT_HOLD_MS);
    }
    return s_hold_ms;
}

/* The one face button a hold means something on. */
static bool inkcell_input_key_holds(enum inkcell_key key) {
    return key == INKCELL_KEY_B;
}

static bool inkcell_input_key_is_direction(enum inkcell_key key) {
    return key == INKCELL_KEY_UP || key == INKCELL_KEY_DOWN || key == INKCELL_KEY_LEFT ||
           key == INKCELL_KEY_RIGHT;
}

/* B is never the policy's: a held B is the hold, not a repeat. */
static bool inkcell_input_key_repeats(const struct inkcell_input *input, enum inkcell_key key) {
    if (inkcell_input_key_is_direction(key)) {
        return true;
    }
    return key != INKCELL_KEY_NONE && !inkcell_input_key_holds(key) && input->repeats != NULL &&
           input->repeats(input->repeats_userdata, key);
}

/* True when the hold in progress was started by this exact evdev event, which is what makes a
   release end it. A zeroed struct reports INKCELL_KEY_NONE and so never matches. */
static bool inkcell_input_repeat_owns(const struct inkcell_input *input, uint16_t type,
                                      uint16_t code) {
    return input->repeat_key != INKCELL_KEY_NONE && input->repeat_type == type &&
           input->repeat_code == code;
}

/* One-shot each time rather than an interval timer, because the delay changes as the hold
   ramps up. A zero delay disarms, which is exactly what a released key wants. */
static void inkcell_input_repeat_schedule(struct inkcell_input *input) {
    if (input->repeat_timer_fd <= 0) {
        return;
    }

    unsigned int ms = 0U;
    if (inkcell_input_key_holds(input->repeat_key)) {
        ms = input->repeat_count == 0U ? inkcell_input_hold_ms() : 0U;
    } else if (input->repeat_key != INKCELL_KEY_NONE) {
        ms = inkcell_input_repeat_delay_ms(input->repeat_count);
    }

    const int armed = inkwell_timer_arm_once(input->repeat_timer_fd, ms);
    if (armed < 0) {
        inkwell_log_warn("input", "arming the key repeat timer failed: %s", strerror(-armed));
    }
}

static void inkcell_input_repeat_cancel(struct inkcell_input *input) {
    if (input->repeat_key == INKCELL_KEY_NONE) {
        return;
    }
    input->repeat_key = INKCELL_KEY_NONE;
    input->repeat_type = 0U;
    input->repeat_code = 0U;
    input->repeat_source_fd = -1;
    input->repeat_count = 0U;
    inkcell_input_repeat_schedule(input);
}

/* Every press goes through here, so anything that is not a repeatable direction - a face
   button, an unmapped code - also ends whatever was being held. That keeps a missed release
   from scrolling forever, and makes "press A" a definite stop rather than a maybe. */
static void inkcell_input_repeat_start(struct inkcell_input *input, enum inkcell_key key,
                                       uint16_t type, uint16_t code, int source_fd) {
    if (inkcell_input_key_holds(key) && inkcell_input_hold_ms() > 0U) {
        /* A keyboard's own autorepeat of the same key is the same hold, not a new one:
           restarting the clock on each would mean a held Escape never reached it. */
        if (inkcell_input_repeat_owns(input, type, code)) {
            return;
        }
        input->repeat_key = key;
        input->repeat_type = type;
        input->repeat_code = code;
        input->repeat_source_fd = source_fd;
        input->repeat_count = 0U;
        inkcell_input_repeat_schedule(input);
        return;
    }
    if (!inkcell_input_key_repeats(input, key) || inkcell_input_repeat_delay_ms(0U) == 0U) {
        inkcell_input_repeat_cancel(input);
        return;
    }

    input->repeat_key = key;
    input->repeat_type = type;
    input->repeat_code = code;
    input->repeat_source_fd = source_fd;
    input->repeat_count = 0U;
    inkcell_input_repeat_schedule(input);
}

/* The only release that never arrives is the one from a device that is no longer there: a
   keyboard unplugged mid-hold hangs up its fd instead of sending the key up, and without this
   the timer would happily scroll the list until some other button was pressed. */
void inkcell_input_device_lost(struct inkcell_input *input, int source_fd) {
    if (input != NULL && input->touch_attached && source_fd == input->touch_fd) {
        inkcell_input_touch_cancel(input);
    }
    if (input == NULL || source_fd < 0 || input->repeat_source_fd != source_fd) {
        return;
    }
    inkcell_input_repeat_cancel(input);
}

void inkcell_input_repeat_tick(struct inkcell_input *input) {
    if (input == NULL || input->repeat_key == INKCELL_KEY_NONE) {
        return;
    }

    if (inkcell_input_key_holds(input->repeat_key)) {
        /* Once per hold: the hold is over as far as the timer is concerned, and the release
           that follows ends nothing. Cancelled before the handler runs, for the reason below. */
        inkcell_input_repeat_cancel(input);
        if (input->on_key != NULL) {
            input->on_key(input->key_userdata, INKCELL_KEY_B_HELD);
        }
        return;
    }
    const enum inkcell_key key = input->repeat_key;
    if (input->repeat_count < UINT_MAX) {
        input->repeat_count++;
    }
    /* Scheduled before the handler runs: the handler owns the UI and may tear this input down,
       and by then the timer must already be set (or not) for the next row. */
    inkcell_input_repeat_schedule(input);
    if (input->on_key != NULL) {
        input->on_key(input->key_userdata, key);
    }
}

enum inkcell_key inkcell_input_repeat_key(const struct inkcell_input *input) {
    return input == NULL ? INKCELL_KEY_NONE : input->repeat_key;
}

static int inkcell_input_repeat_callback(int fd, uint32_t events, void *userdata) {
    struct inkcell_input *input = (struct inkcell_input *)userdata;
    if (input == NULL || (events & INKWELL_LOOP_IN) == 0U) {
        return 0;
    }

    (void)inkwell_timer_read(fd);
    inkcell_input_repeat_tick(input);
    return 0;
}

void inkcell_input_reload_quit_keys(void) {
    s_quit_keys_loaded = false;
    s_quit_key_count = 0U;
    memset(s_quit_keys, 0, sizeof s_quit_keys);
    memset(s_quit_hint, 0, sizeof s_quit_hint);
    memset(s_quit_cap, 0, sizeof s_quit_cap);
    s_quit_hint_is_key_code = false;
    inkcell_input_load_quit_keys();
}

bool inkcell_input_is_quit_key(uint16_t code) {
    inkcell_input_load_quit_keys();
    for (size_t i = 0; i < s_quit_key_count; ++i) {
        if (s_quit_keys[i] == code) {
            return true;
        }
    }
    return false;
}

/*
 * Built on every call rather than cached beside the key codes.
 *
 * The codes are settled once at startup and never change; the language is not the same kind of
 * fact, and a hint formatted in English when the input layer came up would outlive a switch to
 * another one. The buffer is still file-scope, so the pointer's lifetime is what it always was.
 */
const char *inkcell_input_quit_hint(void) {
    inkcell_input_load_quit_keys();
    if (s_quit_hint_is_key_code) {
        inkcell_str_format(s_quit_hint, sizeof s_quit_hint, INKCELL_STR_HINT_QUIT_KEY_CODE,
                           s_quit_keys[0]);
    } else {
        inkwell_str_copy(s_quit_hint, sizeof s_quit_hint, inkcell_str(INKCELL_STR_HINT_QUIT_MENU));
    }
    return s_quit_hint;
}

const char *inkcell_input_quit_cap(void) {
    inkcell_input_load_quit_keys();
    if (!s_quit_hint_is_key_code) {
        return "MENU";
    }
    /* A code rather than a name, prefixed so it is read as a key and not as a quantity. The
       buffer is file-scope for the same reason the hint's is: the pointer outlives the call. */
    snprintf(s_quit_cap, sizeof s_quit_cap, "K%u", (unsigned)s_quit_keys[0]);
    return s_quit_cap;
}

/*
 * What a convention decides, as opposed to what a case decides.
 *
 * The four face buttons are not here: which evdev code is printed A is a fact about one piece
 * of plastic, and it is stated - beside the cap that goes with it - in
 * src/ui/input/input_profile.c. Everything below is the same on every device that speaks evdev, so
 * a port that grew a profile does not restate it: the arrow and Enter keys of a USB keyboard, the
 * shoulders, START and SELECT, and the d-pads that some drivers report as buttons rather than as a
 * hat.
 *
 * The profile is asked first, so a device that really does report one of these oddly is still a
 * table row over there rather than an exception carved into this switch.
 */
static enum inkcell_key inkcell_input_map_convention(uint16_t code) {
    switch (code) {
    case KEY_ENTER:
        return INKCELL_KEY_A;
    case KEY_BACKSPACE:
        return INKCELL_KEY_B;
    case KEY_SPACE:
        return INKCELL_KEY_Y;
    case BTN_TL: /* 310 */
    case KEY_PAGEUP:
        return INKCELL_KEY_L1;
    case BTN_TR: /* 311 */
    case KEY_PAGEDOWN:
    case KEY_TAB:
        return INKCELL_KEY_R1;
    /*
     * The triggers, which a keyboard had no route to: Home and End sit beside Page Up and Page
     * Down on every keyboard that has either, so the second pair of shoulders is the second pair
     * of paging keys. A pad's own trigger codes are not added beside them - a pad reports its
     * triggers as axes (see inkcell_input_map_trigger()), and one that also reported them as keys
     * would press L2 twice.
     */
    case KEY_HOME:
        return INKCELL_KEY_L2;
    case KEY_END:
        return INKCELL_KEY_R2;
    case BTN_SELECT: /* 314 */
        return INKCELL_KEY_SELECT;
    case BTN_START: /* 315 */
        return INKCELL_KEY_START;
    /*
     * The three caps a keyboard had no route to at all.
     *
     * A, B and Y have had one since there was a convention - Enter, Backspace, Space - and X,
     * START and SELECT were reachable only from a pad profile, so a keyboard could walk this
     * client's screens and not open the help sheet on any of them. X takes its own letter
     * because that is what is printed on the cap; the other two take function keys because
     * that is where a keyboard keeps the presses that are about the application rather than
     * about what is on screen.
     */
    case KEY_X:
        return INKCELL_KEY_X;
    case KEY_F1:
        return INKCELL_KEY_START;
    case KEY_F2:
        return INKCELL_KEY_SELECT;
    /* Keyboards, and d-pads that some drivers report as keys rather than a hat. */
    case KEY_UP:
    case BTN_DPAD_UP:
        return INKCELL_KEY_UP;
    case KEY_DOWN:
    case BTN_DPAD_DOWN:
        return INKCELL_KEY_DOWN;
    case KEY_LEFT:
    case BTN_DPAD_LEFT:
        return INKCELL_KEY_LEFT;
    case KEY_RIGHT:
    case BTN_DPAD_RIGHT:
        return INKCELL_KEY_RIGHT;
    default:
        return INKCELL_KEY_NONE;
    }
}

enum inkcell_key inkcell_input_map_key(uint16_t code) {
    const enum inkcell_key from_profile =
        inkcell_input_profile_key(inkcell_input_profile_from_env(), code);
    return from_profile != INKCELL_KEY_NONE ? from_profile : inkcell_input_map_convention(code);
}

/*
 * The two shoulder triggers, which on the Brick are not buttons at all.
 *
 * Its pad impersonates an Xbox 360 controller, and a 360's triggers are analogue: there is no
 * BTN_TL2 anywhere in the key bitmap, so the only way to read L2 and R2 is as ABS_Z and ABS_RZ.
 * They are digital in the hand - 255 down, 0 up, nothing between - which is why they can be
 * logical keys rather than something the UI has to think of as an axis.
 *
 * The threshold is low and deliberately so: it is a press detector for a switch, not a curve
 * for a racing game, and a pad that really does report the whole way up should fire at the
 * top of the travel rather than at the bottom of it.
 */
bool inkcell_input_axis_is_trigger(uint16_t code) {
    return code == ABS_Z || code == ABS_RZ;
}

uint8_t inkcell_input_trigger_bit(uint16_t code) {
    return (uint8_t)(code == ABS_Z ? 1U : code == ABS_RZ ? 2U : 0U);
}

enum inkcell_key inkcell_input_map_trigger(uint16_t code, int32_t value) {
    if (value < INKCELL_INPUT_TRIGGER_PRESS) {
        return INKCELL_KEY_NONE;
    }
    if (code == ABS_Z) {
        return INKCELL_KEY_L2;
    }
    if (code == ABS_RZ) {
        return INKCELL_KEY_R2;
    }
    return INKCELL_KEY_NONE;
}

enum inkcell_key inkcell_input_map_hat(uint16_t code, int32_t value) {
    /* 0 is the release back to centre; only the edge into a direction counts as a press. */
    if (value == 0) {
        return INKCELL_KEY_NONE;
    }
    if (code == ABS_HAT0X) {
        return value < 0 ? INKCELL_KEY_LEFT : INKCELL_KEY_RIGHT;
    }
    if (code == ABS_HAT0Y) {
        return value < 0 ? INKCELL_KEY_UP : INKCELL_KEY_DOWN;
    }
    return INKCELL_KEY_NONE;
}

void inkcell_input_set_handler(struct inkcell_input *input, inkcell_key_handler handler,
                               void *userdata) {
    if (input == NULL) {
        return;
    }
    input->on_key = handler;
    input->key_userdata = userdata;
}

void inkcell_input_set_repeat_policy(struct inkcell_input *input, inkcell_key_repeats_fn repeats,
                                     void *userdata) {
    if (input == NULL) {
        return;
    }
    input->repeats = repeats;
    input->repeats_userdata = userdata;
}

/* ---- the touchscreen --------------------------------------------------------------------- */

void inkcell_input_set_touch_handler(struct inkcell_input *input, inkcell_touch_handler handler,
                                     void *userdata) {
    if (input == NULL) {
        return;
    }
    input->on_touch = handler;
    input->touch_userdata = userdata;
}

/* One axis value as a fraction of the digitizer's range, clamped: a panel reports a contact on
   its bezel as just past the edge, which is the edge. */
static uint32_t inkcell_input_touch_unit(int32_t value, int32_t min, int32_t max) {
    if (value <= min) {
        return 0U;
    }
    if (value >= max) {
        return INKCELL_TOUCH_UNIT - 1U;
    }
    const uint64_t span = (uint64_t)((int64_t)max - (int64_t)min) + 1U;
    return (uint32_t)(((uint64_t)((int64_t)value - (int64_t)min) * INKCELL_TOUCH_UNIT) / span);
}

static void inkcell_input_touch_send(struct inkcell_input *input, enum inkcell_touch_phase phase,
                                     int32_t x, int32_t y) {
    input->touch_sent_x = x;
    input->touch_sent_y = y;
    if (input->on_touch == NULL) {
        return;
    }
    const struct inkcell_input_touch_axes *axes = &input->touch_axes;
    input->on_touch(input->touch_userdata, phase,
                    inkcell_input_touch_unit(x, axes->min_x, axes->max_x),
                    inkcell_input_touch_unit(y, axes->min_y, axes->max_y));
}

/* The contact that counts is gone without lifting: its device went away, or the kernel dropped
   events and nothing about it can be trusted. Not a tap. */
static void inkcell_input_touch_cancel(struct inkcell_input *input) {
    /* Held as far as the handler knows: a single-touch contact counts once its down was sent. */
    const bool held = input->touch_primary != 0U || input->touch_sent_down;
    for (size_t i = 0; i < INKCELL_INPUT_TOUCH_SLOTS; ++i) {
        input->touch_slots[i].live = false;
        input->touch_slots[i].fresh = false;
        input->touch_slots[i].lifted = false;
    }
    input->touch_primary = 0U;
    input->touch_single_down = false;
    input->touch_sent_down = false;
    input->touch_single_pressed = false;
    input->touch_single_released = false;
    if (held) {
        inkcell_input_touch_send(input, INKCELL_TOUCH_CANCEL, input->touch_sent_x,
                                 input->touch_sent_y);
    }
}

/* The end of a multitouch report: the contact that counts went down, moved or lifted. */
static void inkcell_input_touch_report_multi(struct inkcell_input *input) {
    if (input->touch_primary != 0U) {
        const struct inkcell_input_touch_slot *slot =
            &input->touch_slots[input->touch_primary - 1U];
        if (!slot->live) {
            input->touch_primary = 0U;
            inkcell_input_touch_send(input, INKCELL_TOUCH_UP, slot->x, slot->y);
        } else if (slot->lifted) {
            /* Lifted and down again in one report: the old contact ends where it was last
               heard, and the new one is picked up below like any other fresh finger. */
            input->touch_primary = 0U;
            inkcell_input_touch_send(input, INKCELL_TOUCH_UP, input->touch_sent_x,
                                     input->touch_sent_y);
        } else if (slot->x != input->touch_sent_x || slot->y != input->touch_sent_y) {
            inkcell_input_touch_send(input, INKCELL_TOUCH_MOVE, slot->x, slot->y);
        }
    }
    /*
     * A new contact counts only when none does and it went down in this report. A finger left
     * on the glass after the one that counted has lifted is not promoted: it went down as a
     * second finger, and turning it into a first one halfway through would be a tap nobody made.
     */
    if (input->touch_primary == 0U) {
        for (size_t i = 0; i < INKCELL_INPUT_TOUCH_SLOTS; ++i) {
            const struct inkcell_input_touch_slot *slot = &input->touch_slots[i];
            if (slot->fresh) {
                input->touch_primary = (unsigned)i + 1U;
                inkcell_input_touch_send(input, INKCELL_TOUCH_DOWN, slot->x, slot->y);
                if (!slot->live) {
                    /* Down and lifted inside one report: a tap faster than the scan. */
                    input->touch_primary = 0U;
                    inkcell_input_touch_send(input, INKCELL_TOUCH_UP, slot->x, slot->y);
                }
                break;
            }
        }
    }
    for (size_t i = 0; i < INKCELL_INPUT_TOUCH_SLOTS; ++i) {
        input->touch_slots[i].fresh = false;
        input->touch_slots[i].lifted = false;
    }
}

/* The end of a single-touch report. The edges BTN_TOUCH made inside it are latched as well as
   its level, so a down and an up inside one report - a tap faster than the scan - still goes
   out as a down and an up, and an up and a down as the one contact ending and the next starting. */
static void inkcell_input_touch_report_single(struct inkcell_input *input) {
    const int32_t x = input->touch_single_x;
    const int32_t y = input->touch_single_y;
    bool down = input->touch_sent_down;
    if (down && input->touch_single_released) {
        inkcell_input_touch_send(input, INKCELL_TOUCH_UP, x, y);
        down = false;
    }
    if (!down && (input->touch_single_down || input->touch_single_pressed)) {
        inkcell_input_touch_send(input, INKCELL_TOUCH_DOWN, x, y);
        down = true;
    }
    if (down && !input->touch_single_down) {
        inkcell_input_touch_send(input, INKCELL_TOUCH_UP, x, y);
        down = false;
    } else if (down && (x != input->touch_sent_x || y != input->touch_sent_y)) {
        inkcell_input_touch_send(input, INKCELL_TOUCH_MOVE, x, y);
    }
    input->touch_sent_down = down;
    input->touch_single_pressed = false;
    input->touch_single_released = false;
}

static void inkcell_input_touch_event(struct inkcell_input *input, uint16_t type, uint16_t code,
                                      int32_t value) {
    const bool multi = input->touch_axes.kind == INKCELL_INPUT_TOUCH_MULTI;
    if (type == EV_SYN) {
        if (code == SYN_DROPPED) {
            input->touch_dropped = true;
            inkcell_input_touch_cancel(input);
            return;
        }
        if (code != SYN_REPORT) {
            return;
        }
        if (input->touch_dropped) {
            /* The report that was overrun ends here; the next one is whole. */
            input->touch_dropped = false;
            return;
        }
        if (multi) {
            inkcell_input_touch_report_multi(input);
        } else {
            inkcell_input_touch_report_single(input);
        }
        return;
    }
    if (input->touch_dropped) {
        return;
    }
    if (multi) {
        if (type != EV_ABS) {
            return; /* BTN_TOUCH and the legacy ABS_X/Y repeat what the slots say */
        }
        if (code == ABS_MT_SLOT) {
            input->touch_slot =
                value >= 0 && (uint32_t)value < INKCELL_INPUT_TOUCH_SLOTS ? (int)value : -1;
            return;
        }
        if (input->touch_slot < 0) {
            return;
        }
        struct inkcell_input_touch_slot *slot = &input->touch_slots[input->touch_slot];
        if (code == ABS_MT_TRACKING_ID) {
            if (value >= 0) {
                slot->fresh = slot->fresh || !slot->live;
            } else if (slot->live) {
                slot->lifted = true;
            }
            slot->live = value >= 0;
        } else if (code == ABS_MT_POSITION_X) {
            slot->x = value;
        } else if (code == ABS_MT_POSITION_Y) {
            slot->y = value;
        }
        return;
    }
    /* One code is the contact, so a node that reports both cannot end it with the other. */
    const uint16_t contact =
        input->touch_axes.kind == INKCELL_INPUT_TOUCH_POINTER ? BTN_LEFT : BTN_TOUCH;
    if (type == EV_KEY && code == contact) {
        input->touch_single_down = value != 0;
        if (value != 0) {
            input->touch_single_pressed = true;
        } else {
            input->touch_single_released = true;
        }
    } else if (type == EV_ABS && code == ABS_X) {
        input->touch_single_x = value;
    } else if (type == EV_ABS && code == ABS_Y) {
        input->touch_single_y = value;
    }
}

bool inkcell_input_attach_touch(struct inkcell_input *input, int fd,
                                const struct inkcell_input_touch_axes *axes) {
    if (input == NULL || axes == NULL || axes->kind == INKCELL_INPUT_TOUCH_NO ||
        axes->max_x <= axes->min_x || axes->max_y <= axes->min_y) {
        return false;
    }
    input->touch_attached = true;
    input->touch_fd = fd;
    input->touch_axes = *axes;
    memset(input->touch_slots, 0, sizeof input->touch_slots);
    input->touch_slot = 0; /* the kernel's current slot until it names another */
    input->touch_primary = 0U;
    input->touch_sent_x = 0;
    input->touch_sent_y = 0;
    input->touch_sent_down = false;
    input->touch_single_down = false;
    input->touch_single_pressed = false;
    input->touch_single_released = false;
    input->touch_dropped = false;
    return true;
}

void inkcell_input_handle_event(struct inkcell_input *input, uint16_t type, uint16_t code,
                                int32_t value) {
    inkcell_input_handle_device_event(input, -1, type, code, value);
}

void inkcell_input_handle_device_event(struct inkcell_input *input, int source_fd, uint16_t type,
                                       uint16_t code, int32_t value) {
    if (input == NULL) {
        return;
    }
    if (input->touch_attached && source_fd == input->touch_fd) {
        inkcell_input_touch_event(input, type, code, value);
        return;
    }

    enum inkcell_key key = INKCELL_KEY_NONE;
    if (type == EV_KEY) {
        /* value 1 is a press, 2 is the kernel's autorepeat, 0 is a release. */
        if (value == 0) {
            if (inkcell_input_repeat_owns(input, type, code)) {
                inkcell_input_repeat_cancel(input);
            }
            return;
        }
        if (value == 1) {
            if (inkcell_input_is_quit_key(code)) {
                inkwell_log_info("input", "Quit key %u pressed; stopping", (unsigned)code);
                input->stopping = true;
                if (input->host.request_stop != NULL) {
                    input->host.request_stop(input->host.ctx);
                }
                return;
            }
            /* Logged at debug so a device run reveals the real button codes in
               MeshClient.txt, which is how <PREFIX>_QUIT_KEYS gets tuned. */
            inkwell_log_debug("input", "key code %u pressed", (unsigned)code);
        } else if (value != 2) {
            return;
        }
        key = inkcell_input_map_key(code);
        /* A direction is repeated by our timer or by nothing at all, never by the kernel: a
           keyboard whose autorepeat we also honoured would take two rows per step, and with
           <PREFIX>_KEY_REPEAT_DELAY_MS=0 it would still scroll on hold after the knob
           promised it would not. The same for a key the policy made ours when it went down.
           Other face buttons keep whatever the kernel does with them. */
        if (value == 2 &&
            (inkcell_input_key_is_direction(key) ||
             (inkcell_input_repeat_owns(input, type, code) && !inkcell_input_key_holds(key)))) {
            return;
        }
    } else if (type == EV_ABS) {
        /*
         * The triggers before the hat, because they are the one axis whose release is not the
         * hat's release: L2 and R2 come back as ABS_Z/ABS_RZ rather than as buttons, and a
         * trigger at rest reports the same 0 the d-pad reports at centre.
         *
         * A latch rather than the value alone. On this pad a trigger is digital - 255 down, 0
         * up - but the axis is an axis, and a pad that reports the way up as 4, 31, 96, 200,
         * 255 would be five presses of shift for one squeeze. The edge is the press.
         */
        if (inkcell_input_axis_is_trigger(code)) {
            const enum inkcell_key pressed = inkcell_input_map_trigger(code, value);
            const uint8_t bit = inkcell_input_trigger_bit(code);
            const bool was_down = (input->triggers_down & bit) != 0U;
            if (pressed == INKCELL_KEY_NONE) {
                input->triggers_down = (uint8_t)(input->triggers_down & (uint8_t)~bit);
                /* The trigger's release, which ends a repeat the policy gave it. */
                if (was_down && inkcell_input_repeat_owns(input, type, code)) {
                    inkcell_input_repeat_cancel(input);
                }
                return;
            }
            if (was_down) {
                return; /* still held; the axis is only reporting how hard */
            }
            input->triggers_down = (uint8_t)(input->triggers_down | bit);
            key = pressed;
        } else {
            /* The hat back at centre: the release of whichever direction was held. */
            if (value == 0) {
                if (inkcell_input_repeat_owns(input, type, code)) {
                    inkcell_input_repeat_cancel(input);
                }
                return;
            }
            key = inkcell_input_map_hat(code, value);
        }
    }

    if (key == INKCELL_KEY_NONE) {
        return;
    }

    inkcell_input_repeat_start(input, key, type, code, source_fd);
    /*
     * The press is offered to the probe before the store sees it, so what gets measured ends at
     * the panel rather than at the top of this function - and only a button going *down* is
     * offered. A kernel autorepeat (value 2) reaches here for a face button on purpose, because
     * the client acts on it, but a held button is one press: counted, its repeats would weigh
     * whatever screen that button drives by how long somebody leant on it. Our own timer's
     * repeat is refused by the probe instead, having no kernel stamp behind it at all.
     *
     * Whether this press cost anything is not knowable here; the application's key handler
     * has the store's answer and confirms it.
     */
    if (type != EV_KEY || value == 1) {
        inkcell_latency_press();
    }
    if (input->on_key != NULL) {
        input->on_key(input->key_userdata, key);
    }
}

#if defined(__linux__)
static int inkcell_input_event_callback(int fd, uint32_t events, void *userdata) {
    struct inkcell_input *input = (struct inkcell_input *)userdata;
    if (input == NULL) {
        return 0;
    }

    /* Hang-up is how an unplugged device says goodbye, and it comes with nothing to read. */
    if ((events & (INKWELL_LOOP_HUP | INKWELL_LOOP_ERR)) != 0U) {
        inkcell_input_device_lost(input, fd);
    }
    if ((events & INKWELL_LOOP_IN) == 0U) {
        return 0;
    }

    struct input_event batch[16];
    for (;;) {
        const ssize_t bytes = read(fd, batch, sizeof batch);
        if (bytes < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            inkwell_log_warn("input", "read from input fd %d failed: %s", fd, strerror(errno));
            inkcell_input_device_lost(input, fd);
            break;
        }
        if (bytes == 0) {
            inkcell_input_device_lost(input, fd);
            break;
        }

        const size_t count = (size_t)bytes / sizeof(struct input_event);
        for (size_t i = 0; i < count; ++i) {
            /* The kernel's own timestamp for this event, on CLOCK_MONOTONIC because
               inkcell_input_use_monotonic_stamps() asked for it when the device was opened.
               It is handed over before the mapping runs, so a probe that is measuring gets the
               moment the button moved rather than the moment this loop got round to it - which
               on a busy loop are the two ends of the interesting part. */
            inkcell_latency_event((uint64_t)batch[i].input_event_sec * 1000000U +
                                  (uint64_t)batch[i].input_event_usec);
            inkcell_input_handle_device_event(input, fd, batch[i].type, batch[i].code,
                                              batch[i].value);
            if (input->stopping) {
                return 0;
            }
        }

        if ((size_t)bytes < sizeof batch) {
            break;
        }
    }

    return 0;
}

#endif

/* One bit of an EVIOCGBIT bitmap, bounds-checked so a short map answers false rather than
   reading past itself. */
static bool inkcell_input_bit_set(const unsigned long *bits, size_t words, unsigned int code) {
    const size_t word = code / INKCELL_INPUT_BITS_PER_LONG;
    if (bits == NULL || word >= words) {
        return false;
    }
    return ((bits[word] >> (code % INKCELL_INPUT_BITS_PER_LONG)) & 1UL) != 0UL;
}

/*
 * Put this device's event timestamps on CLOCK_MONOTONIC.
 *
 * evdev stamps every event with the wall clock unless it is asked otherwise, and the Brick has
 * no RTC battery: with no network it boots into the epoch and the wall clock jumps once NTP
 * lands, so a stamp taken from it cannot be subtracted from anything. Nothing on the reading
 * path uses the stamp - it is what the latency probe measures from - so a kernel too old to
 * know the request simply leaves the client as it was, and the probe declines to count a press
 * whose stamp is not on its own clock.
 */
#if defined(__linux__)
static void inkcell_input_use_monotonic_stamps(int fd, const char *path) {
    const int clockid = CLOCK_MONOTONIC;
    if (ioctl(fd, inkwell_ioctl_request_of(EVIOCSCLOCKID), &clockid) < 0) {
        inkwell_log_debug("input", "%s keeps wall-clock event stamps: %s", path, strerror(errno));
    }
}
#endif

bool inkcell_input_reads_code(uint16_t code) {
    return inkcell_input_map_key(code) != INKCELL_KEY_NONE || inkcell_input_is_quit_key(code);
}

bool inkcell_input_reads_axis(uint16_t code) {
    /* 1 rather than 0, because 0 on the hat is the release rather than a direction and the
       question here is whether the axis means anything at all - not which way it is pushed. */
    return inkcell_input_map_hat(code, 1) != INKCELL_KEY_NONE ||
           inkcell_input_axis_is_trigger(code);
}

/*
 * Which /dev/input nodes are worth watching.
 *
 * The Brick has three or four of them and only one is a pad, so taking the first few that open
 * worked by accident: the others - the PMIC that emits KEY_POWER, a virtual node with no keys
 * at all - contributed nothing and cost nothing. A host with a keyboard, a mouse, two
 * trackpads, an accelerometer and a pad is the case that breaks it, because the pad is not
 * necessarily among the first few, and a client watching eight nodes that cannot spell A is a
 * client with no buttons. Plugging a USB keyboard into a Brick is the same problem in miniature.
 *
 * So a node is opened, asked what it can report, and dropped when the answer is nothing this
 * client maps. The question is asked through the profile, so it is the *selected* pad's codes
 * that decide, and it includes the quit keys, which <PREFIX>_QUIT_KEYS may have moved onto a
 * node nothing else here wants (KEY_POWER on the Brick's PMIC being the obvious one).
 *
 * A node that cannot answer is watched rather than dropped. Being unable to tell is not
 * evidence of a useless device, and the failure directions are not symmetric: a spare fd on
 * something silent costs nothing, where a dropped pad is a client that cannot be driven at all.
 */
bool inkcell_input_device_wanted(const unsigned long *key_bits, size_t key_words,
                                 const unsigned long *abs_bits, size_t abs_words) {
    const bool keys_known = key_bits != NULL;
    const bool axes_known = abs_bits != NULL;

    if (keys_known) {
        for (unsigned int code = 0U; code <= (unsigned int)KEY_MAX; ++code) {
            if (inkcell_input_bit_set(key_bits, key_words, code) &&
                inkcell_input_reads_code((uint16_t)code)) {
                return true;
            }
        }
    }

    /*
     * The axes, walked the same way and for the same reason the codes above are: two of this
     * client's controls are not buttons. The d-pad is a pair of absolute axes rather than four
     * keys, so a pad whose only useful control is its d-pad reports nothing above - and the
     * triggers are two more, with no BTN_ code anywhere in the key bitmap.
     *
     * Asked through inkcell_input_reads_axis() rather than by naming ABS_HAT0X/Y here, so the
     * filter and the mapping cannot come to disagree: they did for one commit, when the
     * triggers became readable and this list did not hear about it. A node exposing only the
     * triggers - a split device, or a driver that puts them on a node of their own - was opened,
     * asked what it reported, and closed, with the mapping for them sitting unreachable one
     * function away.
     */
    if (axes_known) {
        for (unsigned int code = 0U; code <= (unsigned int)ABS_MAX; ++code) {
            if (inkcell_input_bit_set(abs_bits, abs_words, code) &&
                inkcell_input_reads_axis((uint16_t)code)) {
                return true;
            }
        }
    }

    return !keys_known && !axes_known;
}

/*
 * A touchscreen, rather than a touchpad, a tablet or a stick.
 *
 * The slots and their tracking ids are what protocol B is, and a driver that has them is read
 * through them; one without - protocol A, or a single-contact panel - is read through the
 * ABS_X/ABS_Y and BTN_TOUCH that the kernel's pointer emulation, or the driver itself, reports
 * alongside. A stick has ABS_X too, but never BTN_TOUCH.
 *
 * An absolute pointer with BTN_LEFT is read the same way, BTN_LEFT standing for the contact: the
 * tablet a virtual machine gives its guest (QEMU's virtio and USB tablets), where the host's mouse
 * is the only finger an emulated phone has. Its coordinates are the screen's, which is what makes
 * it a screen and not a mouse - a mouse is relative, and has no ABS_X. It is a kind of its own,
 * INKCELL_INPUT_TOUCH_POINTER, ranked below a real screen: an absolute mouse may have nothing to
 * do with the panel, and a host with both should read the panel. BTN_LEFT alone does not
 * rule out a pad, though: hid-input numbers the buttons of a device that is neither a joystick
 * nor a gamepad up from BTN_MISC, and the seventeenth lands on BTN_LEFT. So the pointer reading
 * is refused to any node that also carries something the pad profile reads
 * (inkcell_input_device_wanted()) - taken as a screen, a pad's buttons would never reach it.
 *
 * INPUT_PROP_POINTER is the property that rules a node out: it marks a surface that moves a
 * cursor - a laptop's touchpad - rather than one that is the screen, and a tap on a touchpad
 * has no point on the frame to land on. A node that cannot report its properties is taken at
 * its axes, as the filter above takes a node at its word.
 */
enum inkcell_input_touch_kind
inkcell_input_touch_kind(const unsigned long *key_bits, size_t key_words,
                         const unsigned long *abs_bits, size_t abs_words,
                         const unsigned long *prop_bits, size_t prop_words) {
    if (abs_bits == NULL || inkcell_input_bit_set(prop_bits, prop_words, INPUT_PROP_POINTER)) {
        return INKCELL_INPUT_TOUCH_NO;
    }
    if (inkcell_input_bit_set(abs_bits, abs_words, ABS_MT_SLOT) &&
        inkcell_input_bit_set(abs_bits, abs_words, ABS_MT_TRACKING_ID) &&
        inkcell_input_bit_set(abs_bits, abs_words, ABS_MT_POSITION_X) &&
        inkcell_input_bit_set(abs_bits, abs_words, ABS_MT_POSITION_Y)) {
        return INKCELL_INPUT_TOUCH_MULTI;
    }
    if (!inkcell_input_bit_set(abs_bits, abs_words, ABS_X) ||
        !inkcell_input_bit_set(abs_bits, abs_words, ABS_Y)) {
        return INKCELL_INPUT_TOUCH_NO;
    }
    if (inkcell_input_bit_set(key_bits, key_words, BTN_TOUCH)) {
        return INKCELL_INPUT_TOUCH_SINGLE;
    }
    if (inkcell_input_bit_set(key_bits, key_words, BTN_LEFT) &&
        !inkcell_input_device_wanted(key_bits, key_words, abs_bits, abs_words)) {
        return INKCELL_INPUT_TOUCH_POINTER;
    }
    return INKCELL_INPUT_TOUCH_NO;
}

#if defined(__linux__)
/* A touchscreen's kind and range, read off the node; false when it is not one or will not say
   how big it is. */
static bool inkcell_input_probe_touch(int fd, struct inkcell_input_touch_axes *axes) {
    unsigned long keys[INKCELL_INPUT_BIT_WORDS(KEY_MAX + 1U)];
    unsigned long abs_bits[INKCELL_INPUT_BIT_WORDS(ABS_MAX + 1U)];
    unsigned long props[INKCELL_INPUT_BIT_WORDS(INPUT_PROP_MAX + 1U)];
    memset(keys, 0, sizeof keys);
    memset(abs_bits, 0, sizeof abs_bits);
    memset(props, 0, sizeof props);
    const bool keys_known =
        ioctl(fd, inkwell_ioctl_request_of(EVIOCGBIT(EV_KEY, sizeof keys)), keys) >= 0;
    const bool abs_known =
        ioctl(fd, inkwell_ioctl_request_of(EVIOCGBIT(EV_ABS, sizeof abs_bits)), abs_bits) >= 0;
    const bool props_known =
        ioctl(fd, inkwell_ioctl_request_of(EVIOCGPROP(sizeof props)), props) >= 0;
    const enum inkcell_input_touch_kind kind = inkcell_input_touch_kind(
        keys_known ? keys : NULL, INKWELL_ARRAY_LEN(keys), abs_known ? abs_bits : NULL,
        INKWELL_ARRAY_LEN(abs_bits), props_known ? props : NULL, INKWELL_ARRAY_LEN(props));
    if (kind == INKCELL_INPUT_TOUCH_NO) {
        return false;
    }
    const bool multi = kind == INKCELL_INPUT_TOUCH_MULTI;
    struct input_absinfo x;
    struct input_absinfo y;
    const unsigned axis_x = multi ? (unsigned)ABS_MT_POSITION_X : (unsigned)ABS_X;
    const unsigned axis_y = multi ? (unsigned)ABS_MT_POSITION_Y : (unsigned)ABS_Y;
    if (ioctl(fd, inkwell_ioctl_request_of(EVIOCGABS(axis_x)), &x) < 0 ||
        ioctl(fd, inkwell_ioctl_request_of(EVIOCGABS(axis_y)), &y) < 0) {
        return false;
    }
    *axes = (struct inkcell_input_touch_axes){.kind = kind,
                                              .min_x = x.minimum,
                                              .max_x = x.maximum,
                                              .min_y = y.minimum,
                                              .max_y = y.maximum};
    return true;
}
#endif

#if defined(__linux__)
static bool inkcell_input_device_is_useful(int fd, const char *path) {
    unsigned long keys[INKCELL_INPUT_BIT_WORDS(KEY_MAX + 1U)];
    unsigned long axes[INKCELL_INPUT_BIT_WORDS(ABS_MAX + 1U)];
    memset(keys, 0, sizeof keys);
    memset(axes, 0, sizeof axes);

    const bool keys_known =
        ioctl(fd, inkwell_ioctl_request_of(EVIOCGBIT(EV_KEY, sizeof keys)), keys) >= 0;
    const bool axes_known =
        ioctl(fd, inkwell_ioctl_request_of(EVIOCGBIT(EV_ABS, sizeof axes)), axes) >= 0;
    if (!keys_known && !axes_known) {
        inkwell_log_debug("input", "%s cannot say what it reports; watching it anyway", path);
    }

    return inkcell_input_device_wanted(keys_known ? keys : NULL, INKWELL_ARRAY_LEN(keys),
                                       axes_known ? axes : NULL, INKWELL_ARRAY_LEN(axes));
}

/*
 * For the log, so a device that is being skipped can be identified without a bit dump.
 *
 * A node that will not give one is left empty rather than filled with a word for it: this file
 * is one scripts/check-strings.py watches, the log line reads perfectly well as "event7 ()",
 * and a stand-in name is the kind of string that gets translated by somebody later.
 */
static void inkcell_input_device_name(int fd, char *out, size_t out_len) {
    if (out == NULL || out_len == 0U) {
        return;
    }
    out[0] = '\0';
    if (ioctl(fd, inkwell_ioctl_request_of(EVIOCGNAME(out_len)), out) < 0) {
        out[0] = '\0';
    }
    out[out_len - 1U] = '\0';
}

/* For the log: the code a touch node's contact is read from, so a VM tablet is not mistaken
   for a panel. Protocol words, not prose: scripts/check-strings.py reads this file. */
static const char *inkcell_input_touch_kind_log(enum inkcell_input_touch_kind kind) {
    switch (kind) {
    case INKCELL_INPUT_TOUCH_MULTI:
        return "ABS_MT_SLOT";
    case INKCELL_INPUT_TOUCH_SINGLE:
        return "BTN_TOUCH";
    case INKCELL_INPUT_TOUCH_POINTER:
        return "BTN_LEFT";
    case INKCELL_INPUT_TOUCH_NO:
        break;
    }
    return "";
}

/*
 * A higher-ranked touch node turned up after one was attached: the old one stops being the
 * screen. It stays watched only if it reports something the pad profile reads; otherwise it was
 * opened for touch alone and is closed. Only called from the scan, before any event is read, so
 * there is no contact under way to cancel.
 */
static void inkcell_input_replace_touch(struct inkcell_input *input, const char *by) {
    const int old = input->touch_fd;
    input->touch_attached = false;
    input->touch_fd = -1;
    char label[32];
    snprintf(label, sizeof label, "fd %d", old);
    inkwell_log_info("input", "Touchscreen %s outranks the %s node before it", by,
                     inkcell_input_touch_kind_log(input->touch_axes.kind));
    if (inkcell_input_device_is_useful(old, label)) {
        return;
    }
    for (size_t i = 0; i < input->count; ++i) {
        if (input->fds[i] == old) {
            if (input->host.remove_fd != NULL) {
                input->host.remove_fd(input->host.ctx, old);
            }
            (void)inkwell_fd_close(old);
            input->fds[i] = input->fds[--input->count];
            return;
        }
    }
}
#endif

/* Repeat is a convenience, so a host without a spare fd loses hold-to-scroll and keeps every
   press working, rather than failing the client's startup. */
static void inkcell_input_setup_repeat_timer(struct inkcell_input *input,
                                             const struct inkcell_input_host *host) {
    const int fd = inkwell_timer_open();
    if (fd < 0) {
        inkwell_log_warn("input", "creating the key repeat timer failed: %s", strerror(-fd));
        return;
    }

    const int add_result = host->add_fd(host->ctx, fd, inkcell_input_repeat_callback, input);
    if (add_result < 0) {
        inkwell_log_warn("input", "Failed to watch the key repeat timer: %d", add_result);
        inkwell_timer_close(fd);
        return;
    }

    input->repeat_timer_fd = fd;
}

int inkcell_input_init(struct inkcell_input *input, const struct inkcell_input_host *host) {
    if (input == NULL || host == NULL || host->add_fd == NULL) {
        return -EINVAL;
    }

    memset(input, 0, sizeof *input);
    input->host = *host;
    input->repeat_timer_fd = -1;
    input->repeat_source_fd = -1;
    inkcell_input_load_quit_keys();
    inkcell_input_load_key_repeat();
    inkcell_input_setup_repeat_timer(input, host);

#if !defined(__linux__)
    /* No evdev here. A window backend on this host delivers its keys through
       inkcell_input_handle_event(), and every one of them is still a press. */
    inkwell_log_debug("input", "No evdev on this host; keys arrive from the window, if any");
    return 0;
#else
    size_t skipped = 0U;
    for (unsigned int index = 0; index < INKCELL_INPUT_SCAN_NODES; ++index) {
        if (input->count >= INKCELL_INPUT_MAX_DEVICES) {
            inkwell_log_warn("input", "Watching %u devices already; not looking past event%u",
                             (unsigned)INKCELL_INPUT_MAX_DEVICES, index);
            break;
        }

        char path[32];
        snprintf(path, sizeof path, "/dev/input/event%u", index);

        const int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            continue;
        }

        char name[64];
        inkcell_input_device_name(fd, name, sizeof name);
        struct inkcell_input_touch_axes axes;
        const bool touch = inkcell_input_probe_touch(fd, &axes) &&
                           (!input->touch_attached || axes.kind > input->touch_axes.kind);
        if (!touch && !inkcell_input_device_is_useful(fd, path)) {
            inkwell_log_debug("input", "Skipping %s (%s): nothing this client reads", path, name);
            close(fd);
            ++skipped;
            continue;
        }

        const int add_result = host->add_fd(host->ctx, fd, inkcell_input_event_callback, input);
        if (add_result < 0) {
            inkwell_log_warn("input", "Failed to watch %s: %d", path, add_result);
            close(fd);
            continue;
        }

        inkcell_input_use_monotonic_stamps(fd, path);
        if (touch) {
            if (input->touch_attached) {
                inkcell_input_replace_touch(input, path);
            }
            (void)inkcell_input_attach_touch(input, fd, &axes);
            /* The kernel names a slot only when it changes, so the first events after opening
               are about whichever slot it last spoke of - not necessarily 0. */
            struct input_absinfo slot;
            if (axes.kind == INKCELL_INPUT_TOUCH_MULTI &&
                ioctl(fd, inkwell_ioctl_request_of(EVIOCGABS(ABS_MT_SLOT)), &slot) >= 0) {
                input->touch_slot =
                    slot.value >= 0 && (uint32_t)slot.value < INKCELL_INPUT_TOUCH_SLOTS
                        ? (int)slot.value
                        : -1;
            }
            inkwell_log_info("input", "Touchscreen %s (%s): %s, %d..%d x %d..%d", path, name,
                             inkcell_input_touch_kind_log(axes.kind), (int)axes.min_x,
                             (int)axes.max_x, (int)axes.min_y, (int)axes.max_y);
        }
        inkwell_log_debug("input", "Watching %s (%s)", path, name);
        input->fds[input->count++] = fd;
    }

    if (input->count == 0U) {
        /* Expected in the dev container and in CI; only the device really has buttons. A host
           where everything was *skipped* is the other case, and worth telling apart: it means
           nodes were readable and none of them reported a button this profile knows. */
        if (skipped > 0U) {
            inkwell_log_warn(
                "input",
                "None of %zu readable input device(s) report a button this client maps; "
                "check <PREFIX>_INPUT_PROFILE",
                skipped);
        }
        inkwell_log_warn("input",
                         "No readable /dev/input devices; buttons will not quit the client");
    } else {
        inkwell_log_info("input", "Watching %zu input device(s); %s", input->count,
                         inkcell_input_quit_hint());
    }

    return (int)input->count;
#endif
}

void inkcell_input_shutdown(struct inkcell_input *input) {
    if (input == NULL) {
        return;
    }

    for (size_t i = 0; i < input->count; ++i) {
        if (input->host.remove_fd != NULL) {
            input->host.remove_fd(input->host.ctx, input->fds[i]);
        }
        (void)inkwell_fd_close(input->fds[i]);
    }

    if (input->repeat_timer_fd > 0) {
        if (input->host.remove_fd != NULL) {
            input->host.remove_fd(input->host.ctx, input->repeat_timer_fd);
        }
        inkwell_timer_close(input->repeat_timer_fd);
    }
    input->repeat_timer_fd = -1;
    input->repeat_source_fd = -1;
    input->repeat_key = INKCELL_KEY_NONE;
    input->repeat_count = 0U;

    input->touch_attached = false;
    input->touch_fd = -1;
    input->touch_primary = 0U;
    input->on_touch = NULL;
    input->touch_userdata = NULL;

    input->count = 0U;
    input->host = (struct inkcell_input_host){0};
    input->stopping = false;
    input->on_key = NULL;
    input->key_userdata = NULL;
}
