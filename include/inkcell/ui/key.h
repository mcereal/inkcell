#ifndef INKCELL_UI_KEY_H
#define INKCELL_UI_KEY_H

/*
 * A press, as the UI thinks of it.
 *
 * This is the far side of the seam src/input/ sits on: evdev codes, hat axes and analogue
 * triggers go in, and one of these comes out. Everything above it - a nav, a controller, a
 * screen - reasons about `INKCELL_KEY_A`, never about `BTN_EAST`, which is what lets the same
 * UI run on a handheld whose buttons report differently. Which physical button produces which
 * of these is one fact about one piece of plastic, stated per device in
 * src/input/input_profile.c.
 *
 * Named for the cap rather than for the job, deliberately: what A *does* is a property of the
 * screen that is up, and a key enum that said `INKCELL_KEY_CONFIRM` would be deciding that for
 * every screen at once. The verbs live in a table - see inkcell/ui/actions.h.
 */

#ifdef __cplusplus
extern "C" {
#endif

enum inkcell_key {
    INKCELL_KEY_NONE = 0,
    INKCELL_KEY_UP,
    INKCELL_KEY_DOWN,
    INKCELL_KEY_LEFT,
    INKCELL_KEY_RIGHT,
    INKCELL_KEY_A,
    INKCELL_KEY_B,
    INKCELL_KEY_X,
    INKCELL_KEY_Y,
    INKCELL_KEY_L1,
    INKCELL_KEY_R1,
    /* The triggers. Analogue on the wire - a Brick reports them as ABS_Z/ABS_RZ rather than as
       buttons, and src/input/input.c is where that becomes a press - but digital in the hand,
       and logical keys like any other once they get here. */
    INKCELL_KEY_L2,
    INKCELL_KEY_R2,
    INKCELL_KEY_START,
    INKCELL_KEY_SELECT,
    /*
     * B, still down after the press it made. Not a cap - a second thing the same cap says, and
     * the one gesture here that is about time rather than about which button: the press has
     * already gone back one step, and a thumb still on it is asking to go all the way.
     *
     * Its own key rather than a flag on B because a screen that has no answer to it must be
     * able to ignore it without also ignoring B. Sent once per hold, and only by the evdev
     * reader - see inkcell_input_handle_device_event().
     */
    INKCELL_KEY_B_HELD,
};

/*
 * A key by the name printed on its cap - "a", "l1", "select", "up" - and back.
 *
 * For whatever drives an application by name rather than by button: a scene script, a control
 * socket, a test. One table, so a script written for one of them reads the same in the others.
 * Case does not matter on the way in; INKCELL_KEY_NONE for a name that is not a key, and NULL
 * for a key that has no name.
 */
enum inkcell_key inkcell_key_from_name(const char *name);
const char *inkcell_key_name(enum inkcell_key key);

#ifdef __cplusplus
}
#endif

#endif /* INKCELL_UI_KEY_H */
