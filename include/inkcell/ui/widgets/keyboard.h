#ifndef INKCELL_BACKENDS_FB_WIDGETS_KEYBOARD_H
#define INKCELL_BACKENDS_FB_WIDGETS_KEYBOARD_H

/*
 * The on-screen keyboard's grid, drawn.
 *
 * include/inkcell/ui/keyboard.h is the model - where the cursor is, which panel is showing,
 * what a press does to the caller's buffer - and says at the end that the drawing is here. This
 * is that half: forty character keys over a row of five actions, filling what the body has left
 * and sitting at the bottom of it.
 *
 * It is one call rather than a grid the screen walks itself, because everything between the two
 * is arithmetic no screen should be repeating: how tall a key is when the body is short, how
 * tall it is when the body is generous and the cap is the key's own width, which way the slack
 * goes, and how big the letter on a key that large should be set. Each of those was got wrong
 * once already in the application this came out of.
 *
 * What is still the caller's is everything above the grid - the heading that says what is being
 * typed and the inkcell_fb_draw_text_field() holding it - because a keyboard's *frame* is about
 * what the text is for, which is the half the model deliberately does not know either.
 *
 * inkcell/ui/widgets.h is the umbrella over this file and its siblings; include either.
 */

#include "inkcell/ui/fb_draw.h"

#include "inkcell/ui/icon.h"
#include "inkcell/ui/keyboard.h"

/*
 * What to draw: the cursor, the layout behind it, and the one key whose face is the
 * application's.
 *
 * Both pointers are the same two the model takes at every press, handed in rather than copied:
 * the cursor lives in whatever record the application's backends read, and the layout is a
 * const description it already has.
 */
struct inkcell_fb_keyboard {
    const struct inkcell_keyboard *keyboard;
    const struct inkcell_keyboard_layout *layout;
    /*
     * The symbol on the submit key. INKCELL_ICON_NONE draws the layout's `submit_label` word
     * instead.
     *
     * The application's for the reason the word is: a soft keyboard's action row is symbols on
     * every platform there is - "space", "delete" and "send" are among the longest strings in
     * any catalog and these are the narrowest boxes on the panel - but *which* symbol this key
     * takes is what the keyboard is for. A send arrow over a security number that must never
     * leave the device would be teaching exactly the wrong thing, and picking one here would be
     * inkcell telling an application what it is doing.
     *
     * The other four keys are inkcell's, because they are the same verb in every program: the
     * layer key keeps its label (what it says - "ABC", "abc", "#+=" - *is* the layer it goes to,
     * and no symbol carries that), and space, delete and cancel take theirs from the icon table.
     */
    enum inkcell_icon submit_icon;
};

/*
 * Draws the grid between `*y` and the footer, and advances `*y` past it.
 *
 * The grid takes the room it is given rather than a line per row: the keys are the one thing on
 * a handheld aimed at with a d-pad rather than read, and nothing else on a keyboard screen
 * wants the space. Whatever is left over after the keys hit their own cap goes *above* the
 * grid - a keyboard sits at the bottom of what it is given, here as on every other device, and
 * slack under the field above reads as the gap over a keyboard rather than as a keyboard that
 * stopped short.
 */
void inkcell_fb_draw_keyboard(const struct inkcell_backend_fb_state *state,
                              const struct inkcell_fb_layout *layout, int *y,
                              const struct inkcell_fb_keyboard *keyboard);

#endif /* INKCELL_BACKENDS_FB_WIDGETS_KEYBOARD_H */
