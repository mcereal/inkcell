#ifndef INKCELL_UI_POINTER_H
#define INKCELL_UI_POINTER_H

/*
 * A mouse, a trackpad or a finger: what a press at a point means, answered against the frame.
 *
 * The d-pad asks the focus map "what lies that way"; a pointer asks it "what is under here" -
 * inkcell_focus_hit(). Nothing else is needed to describe a screen to a pointer, because the
 * frame already registered the box of everything pressable it drew. So this file is not a
 * second description of anything. It is the small amount of *time* a pointer has that a key
 * does not: a click is a press and a release, and the two have to land on the same thing.
 *
 * It answers in one of two ways, and which is not the application's choice:
 *
 *   - **A key.** The box was registered under INKCELL_FOCUS_KEY() - a hint in the action bar -
 *     and clicking it *is* pressing that key. The backend delivers it through the same handler
 *     a keyboard does, so an application that has never heard of a pointer is already driven by
 *     one.
 *   - **A click.** Any other id: something the application named, and the application says what
 *     clicking it does. A backend hands these on when it was given somewhere to hand them and
 *     drops them otherwise.
 *
 * No SDL here, no allocation and nothing kept past a call beyond the struct below - so the rules
 * are tested without a window (tests/suites/ui_pointer.c), as the finder's are without a panel.
 */

#include "inkcell/ui/focus.h"
#include "inkcell/ui/key.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * One pointer's memory between events.
 *
 * `pressed` is what was under the button when it went down. A click is a release over the
 * *same* thing, which is the rule every desktop has: press on a button, change your mind, drag
 * off and let go, and nothing happens. The id rather than the rectangle, because a frame may be
 * drawn between the two and move the box - the thing is still the thing.
 *
 * `wheel` is the part of a step a scroll has not yet earned. A trackpad reports a scroll as a
 * stream of fractions of a notch, and a UI that steps by rows has to add them up rather than
 * round each one to nothing.
 */
struct inkcell_pointer {
    uint32_t pressed;
    bool down;
    float wheel;
};

enum inkcell_pointer_kind {
    INKCELL_POINTER_NONE = 0,
    /* `key` is to be pressed, exactly as if it had been. */
    INKCELL_POINTER_KEY,
    /* `target` was clicked, at `x`, `y`. */
    INKCELL_POINTER_CLICK,
};

struct inkcell_pointer_result {
    enum inkcell_pointer_kind kind;
    enum inkcell_key key;
    uint32_t target;
    int x, y;
};

/*
 * What a backend hands a click to. `target` is the id the frame registered under the point, and
 * `x`, `y` are in the frame's pixels. Optional: a backend without one drops the click, and a
 * key target still works.
 */
typedef void (*inkcell_click_handler)(void *userdata, uint32_t target, int x, int y);

void inkcell_pointer_reset(struct inkcell_pointer *pointer);

/* The button went down at (x, y). Remembers what was under it. */
void inkcell_pointer_down(struct inkcell_pointer *pointer, const struct inkcell_focus_map *map,
                          int x, int y);

/*
 * The button came up at (x, y): a key, a click, or nothing.
 *
 * Nothing when the release is over something other than what the press was over, when the press
 * was over nothing, and when there was no press - a release that arrives after the window got
 * focus by being clicked is one of those.
 */
struct inkcell_pointer_result inkcell_pointer_up(struct inkcell_pointer *pointer,
                                                 const struct inkcell_focus_map *map, int x, int y);

/*
 * A scroll of `dy` notches, positive away from the reader: how many whole rows it moves, with
 * the sign kept - positive is up, which is the key it becomes. The fraction is carried to the
 * next call. A reversal throws the carry away, so a flick back is not spent paying off the
 * remainder of the flick before it.
 *
 * Clamped to INKCELL_POINTER_WHEEL_MAX either way: a hard fling on a trackpad reports dozens of
 * notches in one event, and a list that took them as dozens of presses would have left the
 * screen before the reader saw it move.
 */
#define INKCELL_POINTER_WHEEL_MAX 8
int inkcell_pointer_wheel(struct inkcell_pointer *pointer, float dy);

/*
 * Whether there is something a click would do at (x, y). What a backend asks on a move, to show
 * a pointing hand rather than an arrow. `clickable` is whether the backend has somewhere to hand
 * a click: with nowhere, only a key target is worth a hand.
 */
bool inkcell_pointer_over_target(const struct inkcell_focus_map *map, int x, int y, bool clickable);

#ifdef __cplusplus
}
#endif

#endif /* INKCELL_UI_POINTER_H */
