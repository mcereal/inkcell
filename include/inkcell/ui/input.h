#pragma once

#include "inkcell/ui/key.h"
#include "inkcell/ui/touch.h"

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
 * A touchscreen's one contact that counts, as a fraction of each digitizer axis (0 to
 * INKCELL_TOUCH_UNIT): inkcell_touch_feed() turns it into the frame's pixels and a tap or a
 * scroll. See inkcell_input_set_touch_handler().
 */
typedef void (*inkcell_touch_handler)(void *userdata, enum inkcell_touch_phase phase, uint32_t nx,
                                      uint32_t ny);

/* How many fingers a multitouch screen is followed for. Ten, because that is how many a hand
   pair has; the one that counts is whichever went down first, and the rest are only kept so
   their lifting is not mistaken for its. */
#define INKCELL_INPUT_TOUCH_SLOTS 10U

enum inkcell_input_touch_kind {
    INKCELL_INPUT_TOUCH_NO = 0,
    /* ABS_X/ABS_Y and BTN_TOUCH: one contact, the way a resistive panel or an old driver says.
       Also an absolute pointer with BTN_LEFT that is not a touchpad - a virtual machine's tablet,
       where the host's mouse stands in for a finger. */
    INKCELL_INPUT_TOUCH_SINGLE,
    /* The multitouch slots, protocol B: ABS_MT_SLOT and a tracking id per contact. */
    INKCELL_INPUT_TOUCH_MULTI,
};

/* The digitizer's range on each axis, inclusive, as EVIOCGABS reports it, and which protocol it
   speaks. */
struct inkcell_input_touch_axes {
    enum inkcell_input_touch_kind kind;
    int32_t min_x, max_x;
    int32_t min_y, max_y;
};

/* One multitouch slot: a contact the kernel is tracking, and where it last said it was. */
struct inkcell_input_touch_slot {
    bool live;
    /* Went down in the report being read, so it may become the contact that counts. */
    bool fresh;
    /* Lifted in the report being read - even if it went down again after, which is a new
       contact in the same slot rather than the old one jumping. */
    bool lifted;
    int32_t x, y;
};
/* Whether a held `key` should repeat; see inkcell_input_set_repeat_policy(). */
typedef bool (*inkcell_key_repeats_fn)(void *userdata, enum inkcell_key key);

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
    /* Which keys besides the four directions repeat while held, asked at each press - see
       inkcell_input_set_repeat_policy(). NULL, the zeroed struct's answer, is none of them. */
    inkcell_key_repeats_fn repeats;
    void *repeats_userdata;
    enum inkcell_key repeat_key;
    uint16_t repeat_type;
    uint16_t repeat_code;
    unsigned int repeat_count;

    /* Which triggers are held, one bit each (inkcell_input_trigger_bit). L2 and R2 arrive as
       absolute axes rather than as buttons, so nothing in the event stream marks the edge: a
       pad reporting the way up as a run of rising values would otherwise be one press per
       value. A zeroed struct starts with both up, which is what they are. */
    uint8_t triggers_down;

    /*
     * The touchscreen, when there is one: the first node that reports a direct-touch digitizer
     * (inkcell_input_touch_kind()). Its events are a contact's, not a pad's, and go only to the
     * decoder below - BTN_TOUCH is not a button, and ABS_X on a screen is not a stick.
     *
     * The kernel speaks in reports that end with SYN_REPORT, and a contact's position, its going
     * down and its lifting can each arrive in any order inside one, so they are kept per slot
     * and acted on at the report's end. `touch_primary` is the slot of the contact that counts,
     * plus one, so a zeroed struct has none.
     */
    bool touch_attached;
    int touch_fd;
    struct inkcell_input_touch_axes touch_axes;
    struct inkcell_input_touch_slot touch_slots[INKCELL_INPUT_TOUCH_SLOTS];
    /* The slot the next ABS_MT_ events are about, or -1 for one past the slots kept. */
    int touch_slot;
    unsigned touch_primary;
    /* Where the contact that counts was last reported to the handler, in the digitizer's units. */
    int32_t touch_sent_x, touch_sent_y;
    /* A single-touch screen's one contact, and whether the handler last heard it down. */
    bool touch_single_down;
    bool touch_sent_down;
    /* BTN_TOUCH went down, or up, in the report being read: a tap faster than the scan says
       both inside one report, and the level alone would show neither. */
    bool touch_single_pressed;
    bool touch_single_released;
    int32_t touch_single_x, touch_single_y;
    /* SYN_DROPPED: the kernel's buffer overflowed and the report under way is incomplete, so the
       rest of it is ignored and the contact cancelled. */
    bool touch_dropped;
    inkcell_touch_handler on_touch;
    void *touch_userdata;
};

/* Never fails the caller: a host with no readable /dev/input (the dev container, CI) simply
   watches nothing. Returns the number of devices opened. */
int inkcell_input_init(struct inkcell_input *input, const struct inkcell_input_host *host);
void inkcell_input_shutdown(struct inkcell_input *input);

void inkcell_input_set_handler(struct inkcell_input *input, inkcell_key_handler handler,
                               void *userdata);

/*
 * Where a touchscreen's contact goes. NULL - and an application that never asks - drops them,
 * and the screen is then watched for nothing.
 */
void inkcell_input_set_touch_handler(struct inkcell_input *input, inkcell_touch_handler handler,
                                     void *userdata);

/*
 * Whether a node reporting these capability bitmaps is a touchscreen, and which protocol it
 * speaks. EVIOCGBIT bitmaps for EV_KEY and EV_ABS and EVIOCGPROP's, NULL where the node could
 * not say. A touchpad - INPUT_PROP_POINTER, a surface that moves a cursor rather than being
 * the screen - is not one. Pure, like inkcell_input_device_wanted().
 */
enum inkcell_input_touch_kind
inkcell_input_touch_kind(const unsigned long *key_bits, size_t key_words,
                         const unsigned long *abs_bits, size_t abs_words,
                         const unsigned long *prop_bits, size_t prop_words);

/*
 * Treat `fd` as the touchscreen, with these axes: what inkcell_input_init() does for the first
 * node inkcell_input_touch_kind() accepts, and public so a test can play a recorded screen
 * through inkcell_input_handle_device_event() without one. False for an empty axis.
 */
bool inkcell_input_attach_touch(struct inkcell_input *input, int fd,
                                const struct inkcell_input_touch_axes *axes);

/*
 * Which other keys this layer repeats while held.
 *
 * The four directions always do and nothing else does by default, because a face button
 * confirms or goes back and a held confirm that fired forty times would be a trap. That is
 * about this layer's own timer: a device whose kernel autorepeats a face button (a USB keyboard's
 * Enter, say) still delivers those repeats as it always has, whatever the policy answered. But
 * whether a press is a confirm is the application's to know, not the pad's: the same X that arms a
 * delete on one screen is a backspace on an on-screen keyboard, where clearing a sentence one press
 * per character is the trap instead. So the application is asked, at the moment the key goes down,
 * and a key it says yes to repeats on the directions' timer and ramp. B is never asked - its
 * hold is INKCELL_KEY_B_HELD. The triggers can be answered for too: they are axes, and their
 * return below the press threshold is the release that ends the repeat.
 *
 * Asked at the press rather than once, because the answer is about what is on screen: a
 * keyboard closing mid-hold is a later press, and the next hold asks again.
 */
void inkcell_input_set_repeat_policy(struct inkcell_input *input, inkcell_key_repeats_fn repeats,
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

/* The direction being held - or B, while its hold is still timing - or INKCELL_KEY_NONE when
   nothing is. */
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
