#ifndef INKCELL_BACKENDS_FB_WIDGETS_TOUCH_KEYBOARD_H
#define INKCELL_BACKENDS_FB_WIDGETS_TOUCH_KEYBOARD_H

/*
 * The keyboard a finger types on, drawn: the model in inkcell/ui/touch_keyboard.h laid in a box.
 *
 * The box is the one the touches are measured against - the caller hands both the same - and the
 * keys are drawn inset from their share of it, so the gap between two keys is still the nearer
 * key's to a finger. Every key is inkcell_fb_draw_button(), as the grid's are: the characters and
 * space filled, the keys that do something other than type in the secondary tone, the enter key
 * in the accent, and the key under the finger lit. A character under the finger also rises over
 * its key, larger, so the thumb covering it is not covering the only sign of which key it is.
 */

#include "inkcell/ui/fb_draw.h"

#include "inkcell/ui/icon.h"
#include "inkcell/ui/touch_keyboard.h"

#ifdef __cplusplus
extern "C" {
#endif

struct inkcell_fb_touch_keyboard {
    const struct inkcell_touch_keyboard *keyboard;
    /*
     * The enter key's face, which is the field's: what pressing it does is what the text is for.
     * An icon when not INKCELL_ICON_NONE; otherwise `enter_label` when not NULL ("Done", "Go");
     * otherwise INKCELL_ICON_ENTER, the new line.
     */
    enum inkcell_icon enter_icon;
    const char *enter_label;
};

/* Fills `box` with the keyboard's ground and its keys. */
void inkcell_fb_draw_touch_keyboard(const struct inkcell_draw_state *state,
                                    struct inkcell_fb_rect box,
                                    const struct inkcell_fb_touch_keyboard *keyboard);

/* The same box as the model measures in. */
static inline struct inkcell_tkb_box inkcell_fb_touch_keyboard_box(struct inkcell_fb_rect box) {
    const struct inkcell_tkb_box out = {box.x, box.y, box.w, box.h};
    return out;
}

#ifdef __cplusplus
}
#endif

#endif /* INKCELL_BACKENDS_FB_WIDGETS_TOUCH_KEYBOARD_H */
