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
};

#ifdef __cplusplus
}
#endif

#endif /* INKCELL_UI_KEY_H */
