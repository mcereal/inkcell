#ifndef INKCELL_UI_TOUCH_H
#define INKCELL_UI_TOUCH_H

/*
 * A finger on the panel: a tap, or a drag that scrolls.
 *
 * A touchscreen is a pointer with no hover and no buttons, so most of what it means is already
 * in inkcell/ui/pointer.h: a tap is a press and a release over the same box, and lands as an
 * action key, a key or a click exactly as a mouse's would. What a finger has that a mouse does
 * not is that it is never still. A press lands a few pixels from where the reader aimed and
 * wanders before it lifts, and the same finger moving on purpose is how a list is scrolled. So
 * this file is the line between the two: a contact that stays within the slop of where it went
 * down is a tap, and one that leaves it is a drag - for the rest of that contact, with no tap at
 * the end, which is the rule every phone has.
 *
 * A drag scrolls the way a wheel does here, by moving the cursor a row per `step_px` of travel,
 * because a UI whose lists are windowed around a cursor has no view that could move without it
 * (see inkcell_pointer_wheel()). It follows the finger: dragging down brings earlier rows in,
 * which is the key Up.
 *
 * Where the contact is comes from the evdev reader (inkcell_input_set_touch_handler()) as a
 * fraction of the digitizer's two axes, and becomes the frame's pixels here, turned by
 * <PREFIX>_TOUCH_ROTATE when the digitizer is not mounted the way the frame is drawn. That knob
 * defaults to the panel's own - <PREFIX>_KMS_ROTATE, then <PREFIX>_FB_ROTATE, when that is the
 * 180 the backends draw - because a panel mounted upside down usually has its touch layer glued
 * on the same way.
 *
 * No device, no allocation and no clock: tests/suites/ui_touch.c plays contacts at it.
 */

#include "inkcell/ui/focus.h"
#include "inkcell/ui/pointer.h"

#include <stdbool.h>
#include <stdint.h>

struct inkcell_backend;

#ifdef __cplusplus
extern "C" {
#endif

/* A contact's position on one axis: 0 at the digitizer's minimum, up to (not reaching) this at
   its maximum. Independent of the panel's resolution and of the digitizer's own range. */
#define INKCELL_TOUCH_UNIT 65536U

enum inkcell_touch_phase {
    /* A finger went down: the first of a contact. */
    INKCELL_TOUCH_DOWN = 0,
    /* It moved. */
    INKCELL_TOUCH_MOVE,
    /* It lifted where it is. */
    INKCELL_TOUCH_UP,
    /* It is gone without lifting - the device went away - and is not a tap. */
    INKCELL_TOUCH_CANCEL,
};

/*
 * One contact's memory between events.
 *
 * `slop_px` and `step_px` of 0 take a share of the frame's short side, worked out per event so a
 * frame that changes size keeps the same feel: about a millimetre and a half and a list row on a
 * phone, and proportionally less on a small panel. `rotation` is 0, 90, 180 or 270.
 */
struct inkcell_touch {
    struct inkcell_pointer pointer;
    bool down;
    bool dragging;
    int start_x, start_y;
    /* The last position a drag was measured from. */
    int last_y;
    int slop_px;
    int step_px;
    unsigned rotation;
};

enum inkcell_touch_kind {
    INKCELL_TOUCH_NONE = 0,
    /* A contact lifted without leaving its slop. `press` is what the pointer rules made of it,
       and may itself be INKCELL_POINTER_NONE: a tap on nothing is still a tap. */
    INKCELL_TOUCH_TAP,
    /* A drag crossed `steps` rows, signed as inkcell_pointer_wheel()'s: positive is Up. */
    INKCELL_TOUCH_SCROLL,
};

struct inkcell_touch_result {
    enum inkcell_touch_kind kind;
    struct inkcell_pointer_result press;
    int steps;
    /* Where the contact is, in the frame's pixels. */
    int x, y;
};

/* Forgets any contact and reads <PREFIX>_TOUCH_ROTATE. */
void inkcell_touch_init(struct inkcell_touch *touch);

/*
 * A position from the reader (each axis 0..INKCELL_TOUCH_UNIT) as a pixel of a `width` by
 * `height` frame, turned by `rotation` degrees clockwise from the digitizer to the frame. False,
 * leaving the outputs alone, for an empty frame or a rotation that is not a right angle.
 */
bool inkcell_touch_to_frame(uint32_t nx, uint32_t ny, uint32_t width, uint32_t height,
                            unsigned rotation, int *x, int *y);

/*
 * One event of a contact, in the frame's pixels, against the last frame's boxes. `width` and
 * `height` are the frame's, for the default slop and step. A tap that resolves to something
 * is counted as a press by the latency probe, as a click is.
 */
struct inkcell_touch_result inkcell_touch_handle(struct inkcell_touch *touch,
                                                 const struct inkcell_focus_map *map,
                                                 enum inkcell_touch_phase phase, int x, int y,
                                                 uint32_t width, uint32_t height);

/*
 * The same, from the reader's fractions, against whatever `backend` last presented: its frame
 * for the size and its focus map for the boxes. `state` and `userdata` are what the backend was
 * initialised with. Nothing happens before the first frame - there is nothing to touch yet.
 */
struct inkcell_touch_result inkcell_touch_feed(struct inkcell_touch *touch,
                                               const struct inkcell_backend *backend, void *state,
                                               void *userdata, enum inkcell_touch_phase phase,
                                               uint32_t nx, uint32_t ny);

#ifdef __cplusplus
}
#endif

#endif /* INKCELL_UI_TOUCH_H */
