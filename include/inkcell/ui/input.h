#pragma once

#include "inkcell/ui/key.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The loop this input is read on.
 *
 * inkcell does not own an event loop: an application has one already, and a UI library that
 * brought a second would be asking every app to run two. So the three things reading a device
 * needs from a loop are stated here and the host supplies them - epoll, poll, libuv, a hand
 * written select(), whatever the app is built on.
 *
 * `add_fd` watches `fd` for readability. `remove_fd` stops. `request_stop` is what a quit key
 * does, and is the one call that is about the application rather than about a descriptor: the
 * key that leaves is part of the device's button profile, so which press means "stop" is
 * inkcell's answer and what stopping *is* belongs to the host.
 */
struct inkcell_input_host {
    void *ctx;
    int (*add_fd)(void *ctx, int fd, int (*callback)(int fd, uint32_t events, void *userdata),
                  void *userdata);
    void (*remove_fd)(void *ctx, int fd);
    void (*request_stop)(void *ctx);
};

typedef void (*inkcell_key_handler)(void *userdata, enum inkcell_key key);

/*
 * How many nodes may be watched at once.
 *
 * Eight was one per node the Brick has, twice over. It is the wrong number for a host that also
 * has a keyboard, a mouse, two trackpads and an accelerometer - not because watching sixteen
 * costs anything, but because the scan stops at the cap, and a pad enumerated after the cap is
 * a client with no buttons. The filter in inkcell_input_init() is what usually keeps the count
 * far below this; the cap is the backstop.
 */
#define INKCELL_INPUT_MAX_DEVICES 16U

/* evdev button reader. The TrimUI Brick has no keyboard and no console, so this is the only
   way to drive the client from the device. Every /dev/input/event* node that reports a button
   this client maps is watched - see inkcell_input_init() for why the ones that do not are
   dropped rather than watched anyway; a press of any quit key asks the event loop to stop, and
   everything else that maps to a logical key (face buttons, shoulders, d-pad hat axes, and the
   arrow/Enter keys of a USB keyboard) goes to the handler. */
struct inkcell_input {
    struct inkcell_input_host host;
    /* A quit key has been pressed. What stopping means is the host's, but a batch still being
       drained has to stop handing presses to a UI that is going away. */
    bool stopping;
    int fds[INKCELL_INPUT_MAX_DEVICES];
    size_t count;
    inkcell_key_handler on_key;
    void *key_userdata;

    /* Software key repeat for a held direction. `repeat_timer_fd` is <= 0 when there is no
       timer - a zeroed struct (what the tests use) and a host where timerfd_create failed both
       land there, and repeat then simply never fires on its own. `repeat_type`/`repeat_code`
       are the raw evdev event that started the hold, kept so the matching release ends it, and
       `repeat_source_fd` the device it came from, so a hold ends when that device goes away. */
    int repeat_timer_fd;
    int repeat_source_fd;
    enum inkcell_key repeat_key;
    uint16_t repeat_type;
    uint16_t repeat_code;
    unsigned int repeat_count;

    /* Which triggers are held, one bit each (inkcell_input_trigger_bit). L2 and R2 arrive as
       absolute axes rather than as buttons, so nothing in the event stream marks the edge: a
       pad reporting the way up as a run of rising values would otherwise be one press per
       value. A zeroed struct starts with both up, which is what they are. */
    uint8_t triggers_down;
};

/* Never fails the caller: a host with no readable /dev/input (the dev container, CI) simply
   watches nothing. Returns the number of devices opened. */
int inkcell_input_init(struct inkcell_input *input, const struct inkcell_input_host *host);
void inkcell_input_shutdown(struct inkcell_input *input);

void inkcell_input_set_handler(struct inkcell_input *input, inkcell_key_handler handler,
                               void *userdata);

/* One raw evdev event (type/code/value as in struct input_event). Public so the mapping can be
   tested without a device: quit keys stop the loop, everything else is translated and handed
   to the handler. */
void inkcell_input_handle_event(struct inkcell_input *input, uint16_t type, uint16_t code,
                                int32_t value);

/* The same, naming the device fd the event arrived on so a hold can be tied to it. Anything
   that is not reading a real device passes -1, which is what the call above does. */
void inkcell_input_handle_device_event(struct inkcell_input *input, int source_fd, uint16_t type,
                                       uint16_t code, int32_t value);

/* The device behind `source_fd` is gone - unplugged, or its fd went bad. Ends a hold that
   started there: its release will never arrive, and a repeat with no release scrolls forever. */
void inkcell_input_device_lost(struct inkcell_input *input, int source_fd);

/* How long the next repeat of a held direction waits, given how many repeats it has already
   produced: 0 asks for the initial hold delay, and the interval ramps down after a few rows so
   a long roster does not take a minute to walk. Returns 0 when repeat is switched off with
   <PREFIX>_KEY_REPEAT_DELAY_MS=0. Pure, and public so the ramp is testable off-device. */
unsigned int inkcell_input_repeat_delay_ms(unsigned int repeats);

/* The direction being held, or INKCELL_KEY_NONE when nothing is repeating. */
enum inkcell_key inkcell_input_repeat_key(const struct inkcell_input *input);

/* Emits one repeat of the held key and schedules the next. The repeat timer calls this; it is
   public so a test can step a hold without a real timerfd. */
void inkcell_input_repeat_tick(struct inkcell_input *input);

/* evdev key code (or hat axis code with its direction) to logical key; INKCELL_KEY_NONE when
   the code has no meaning for the UI. */
enum inkcell_key inkcell_input_map_key(uint16_t code);
enum inkcell_key inkcell_input_map_hat(uint16_t code, int32_t value);

/*
 * The shoulder triggers, which this hardware reports as absolute axes: ABS_Z is L2 and ABS_RZ
 * is R2, and neither has a BTN_ code to be read as at all. See the note in src/ui/input/input.c.
 *
 * `inkcell_input_map_trigger` answers with the key only for a value at or above the press
 * threshold, so it is a press detector and not a state: the edge - and therefore the latch that
 * stops a rising axis being several presses - belongs to inkcell_input_handle_device_event().
 * All three are pure and public so the mapping is testable without a pad.
 */
#define INKCELL_INPUT_TRIGGER_PRESS 128
bool inkcell_input_axis_is_trigger(uint16_t code);
uint8_t inkcell_input_trigger_bit(uint16_t code);
enum inkcell_key inkcell_input_map_trigger(uint16_t code, int32_t value);

/* True when the evdev key code should quit. Defaults to MENU/POWER/ESC/MODE/SELECT and can be
   replaced with a comma-separated list of decimal codes in <PREFIX>_QUIT_KEYS. */
bool inkcell_input_is_quit_key(uint16_t code);

/* Footer text for the UI backends, e.g. "Press MENU to quit". */
const char *inkcell_input_quit_hint(void);

/*
 * The same fact as a keycap: "MENU", or "K139" when <PREFIX>_QUIT_KEYS has moved quitting to
 * a key whose name we do not know.
 *
 * What the action bar draws inside the pill, where the hint above is a sentence for the line
 * that reports the transport. Never NULL, and never translated - a cap is what is printed on
 * the case. See enum inkcell_button.
 */
const char *inkcell_input_quit_cap(void);

/*
 * Whether this client would do anything with an evdev key code: it maps to a logical key under
 * the selected profile, or it quits. What the device filter asks about each code a node claims
 * to report.
 */
bool inkcell_input_reads_code(uint16_t code);

/*
 * The same question for an absolute axis: the d-pad's hat, or a trigger. Two of this client's
 * controls are not buttons, so the capability filter has to ask about both halves - see
 * inkcell_input_device_wanted(), which asks this rather than naming the axes itself.
 */
bool inkcell_input_reads_axis(uint16_t code);

/*
 * Whether a node reporting these capability bitmaps is worth watching - the decision behind the
 * filter in inkcell_input_init(), with the ioctls taken off it so it can be exercised without a
 * device.
 *
 * `key_bits` and `abs_bits` are EVIOCGBIT bitmaps for EV_KEY and EV_ABS, and NULL means the
 * node could not say. A node that could not say either is wanted: being unable to tell is not
 * evidence of a useless device, and a dropped pad is a client nobody can drive, where a spare
 * fd on something silent costs nothing.
 */
bool inkcell_input_device_wanted(const unsigned long *key_bits, size_t key_words,
                                 const unsigned long *abs_bits, size_t abs_words);

/* Sizing for the two bitmaps above, in the units EVIOCGBIT fills. */
#define INKCELL_INPUT_BITS_PER_LONG (8U * (unsigned)sizeof(unsigned long))
#define INKCELL_INPUT_BIT_WORDS(count)                                                             \
    (((count) + INKCELL_INPUT_BITS_PER_LONG - 1U) / INKCELL_INPUT_BITS_PER_LONG)

/* The quit-key set is parsed from the environment once and cached. Exposed so tests can
   re-read <PREFIX>_QUIT_KEYS after changing it; not needed in normal use. */
void inkcell_input_reload_quit_keys(void);

/* The same, for <PREFIX>_KEY_REPEAT_DELAY_MS and <PREFIX>_KEY_REPEAT_MS. */
void inkcell_input_reload_key_repeat(void);

#ifdef __cplusplus
}
#endif
