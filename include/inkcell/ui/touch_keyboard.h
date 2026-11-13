#ifndef INKCELL_TOUCH_KEYBOARD_H
#define INKCELL_TOUCH_KEYBOARD_H

/*
 * The keyboard a finger types on: a phone's, four staggered rows that fill the width, where
 * keyboard.h is a handheld's grid walked by a d-pad.
 *
 * The two share nothing but the edits, and that is the line between them. A d-pad keyboard is a
 * cursor over equal cells and the key under the cursor is the one the press means; a finger
 * keyboard has no cursor, keys of different widths, and the key a press means is the one the
 * finger *lifts* on - a thumb that lands on the edge of "g" and slides onto "h" typed "h", and
 * the key it is on lights as it goes so the person sees which. What the press does to the text
 * is the same in both, so the text is not here at all: a press comes back as what it amounted to
 * (a character, a delete, the enter key), and whoever holds the text - an application's field,
 * or a system keyboard that only sends edits to one - does it. keyboard.h's
 * inkcell_keyboard_insert_text_at_caret() and inkcell_keyboard_delete_at_caret() are the edits.
 *
 * A model, not a widget, for keyboard.h's reason: which key is under a finger is arithmetic a
 * test asserts without a panel. The keys fill their box edge to edge, gaps included - a finger
 * between two keys is on the nearer one, never on nothing - and the box is the caller's: the
 * drawing (widgets/touch_keyboard.h) is handed the same box the touches are measured against.
 *
 * Three layers, as every phone has: the letters; the digits and the common symbols; and the rest
 * of the symbols, a press past those. The shift is one capital, or - pressed twice in quick
 * succession - capitals held on, as on every phone. Delete repeats while held: the caller asks
 * inkcell_touch_keyboard_due() when, and calls inkcell_touch_keyboard_tick() then.
 */

#include "inkcell/ui/touch.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define INKCELL_TKB_ROWS 4U
#define INKCELL_TKB_KEYS_MAX 10U /* the most keys in a row */
/* A row's width, in units: a letter is 10, so the top row of ten letters is a row. */
#define INKCELL_TKB_UNITS 100U
/* The most bytes a key types, NUL included: a key is one character, and the widest of them here
   is three bytes of UTF-8. */
#define INKCELL_TKB_TEXT_MAX 8U

/* Held this long, delete starts to repeat, every INKCELL_TKB_REPEAT_MS after. */
#define INKCELL_TKB_REPEAT_DELAY_MS 400U
#define INKCELL_TKB_REPEAT_MS 60U
/* Shift pressed again within this of the last press holds the capitals on. */
#define INKCELL_TKB_DOUBLE_MS 350U

enum inkcell_tkb_layer {
    INKCELL_TKB_LETTERS = 0,
    INKCELL_TKB_SYMBOLS,
    INKCELL_TKB_MORE,
    INKCELL_TKB_LAYER_COUNT,
};

enum inkcell_tkb_kind {
    INKCELL_TKB_CHAR = 0, /* types its text */
    INKCELL_TKB_SHIFT,    /* one capital, or capitals held on */
    INKCELL_TKB_DELETE,   /* the character before the caret; repeats while held */
    INKCELL_TKB_LAYER,    /* to layer `to`, its text the label */
    INKCELL_TKB_SPACE,
    INKCELL_TKB_ENTER, /* what the field's enter does: a new line, "Done", "Go" */
    INKCELL_TKB_HIDE,  /* the keyboard away, the text as it is */
};

struct inkcell_tkb_key {
    enum inkcell_tkb_kind kind;
    const char *text; /* CHAR: what it types, in lower case; LAYER: its label; else NULL */
    uint8_t units;    /* its width, of INKCELL_TKB_UNITS */
    uint8_t lead;     /* units of row before it, unkeyed: the stagger of the middle row */
    uint8_t to;       /* LAYER: enum inkcell_tkb_layer */
};

/* Layer `layer`'s row `row`: its keys, `*count` of them. NULL past the last row or layer. */
const struct inkcell_tkb_key *inkcell_touch_keyboard_row(unsigned layer, unsigned row,
                                                         size_t *count);

enum inkcell_tkb_shift {
    INKCELL_TKB_SHIFT_OFF = 0,
    INKCELL_TKB_SHIFT_ONCE,   /* the next letter is a capital, then off */
    INKCELL_TKB_SHIFT_LOCKED, /* every letter, until shift again */
};

/*
 * Where the keyboard is. Pointer-free and fixed-size, keyboard.h's struct's reason: it goes in a
 * snapshot that is compared and copied.
 */
struct inkcell_touch_keyboard {
    uint8_t layer; /* enum inkcell_tkb_layer */
    uint8_t shift; /* enum inkcell_tkb_shift */
    /* The key under the finger, which is lit and is the one a lift types; -1 for none. */
    int8_t row, key;
    bool down;
    /* Shift was set by the field rather than pressed (inkcell_touch_keyboard_capitalize()): a
       field may take that back, never a capital the person asked for. */
    bool auto_shift;
    uint64_t shift_ms; /* when shift was last pressed, for the double press */
    uint64_t due_ms;   /* delete's next repeat; 0 for none */
};

/* A box, in the caller's pixels. */
struct inkcell_tkb_box {
    int32_t x, y, w, h;
};

void inkcell_touch_keyboard_reset(struct inkcell_touch_keyboard *kb);

/*
 * Key `key` of row `row` of `layer`, laid in `box`: rows of equal height, each key as wide as its
 * units of the box's width. The box a touch is measured against, gaps and all; a drawing insets
 * it. Zeroes for no such key.
 */
struct inkcell_tkb_box inkcell_touch_keyboard_key_box(unsigned layer, unsigned row, unsigned key,
                                                      struct inkcell_tkb_box box);

/* The key at (`x`, `y`) of `box`: the row the height falls in, and the key nearest along it - a
   point in a row's lead or past its last key is the nearest key's. False outside the box. */
bool inkcell_touch_keyboard_hit(unsigned layer, struct inkcell_tkb_box box, int32_t x, int32_t y,
                                unsigned *row, unsigned *key);

enum inkcell_tkb_result_kind {
    INKCELL_TKB_NOTHING = 0,
    INKCELL_TKB_REDRAW, /* only the picture changed: a key lit, the layer, the shift */
    INKCELL_TKB_TEXT,   /* type `text` at the caret */
    INKCELL_TKB_BACK,   /* delete the character before the caret */
    INKCELL_TKB_SUBMIT, /* the enter key */
    INKCELL_TKB_AWAY,   /* the hide key */
};

struct inkcell_tkb_result {
    enum inkcell_tkb_result_kind kind;
    char text[INKCELL_TKB_TEXT_MAX];
};

/*
 * A finger at (`x`, `y`) of `box`, at `now_ms`. Down lights the key under it - and is when a
 * delete happens, since a delete that waited for the lift could not repeat - a move lights the key
 * it is on now, and up types the lit key. A cancel types nothing.
 */
struct inkcell_tkb_result inkcell_touch_keyboard_touch(struct inkcell_touch_keyboard *kb,
                                                       struct inkcell_tkb_box box,
                                                       enum inkcell_touch_phase phase, int32_t x,
                                                       int32_t y, uint64_t now_ms);

/* When the keyboard next wants inkcell_touch_keyboard_tick(): delete held. 0 for never. */
uint64_t inkcell_touch_keyboard_due(const struct inkcell_touch_keyboard *kb);

/* Delete again, if it is still held and due at `now_ms`; NOTHING otherwise. */
struct inkcell_tkb_result inkcell_touch_keyboard_tick(struct inkcell_touch_keyboard *kb,
                                                      uint64_t now_ms);

/*
 * The field says whether the next letter starts a sentence (`on`): shift once, as a person
 * would. Only ever its own capital: a shift the person pressed, or held on, is left alone, and
 * the letters layer is where it shows. True when that changed the picture.
 */
bool inkcell_touch_keyboard_capitalize(struct inkcell_touch_keyboard *kb, bool on);

/* What key `k` shows now: a letter in capitals while shifted. `scratch` holds a capital. */
const char *inkcell_touch_keyboard_label(const struct inkcell_touch_keyboard *kb,
                                         const struct inkcell_tkb_key *k,
                                         char scratch[INKCELL_TKB_TEXT_MAX]);

#ifdef __cplusplus
}
#endif

#endif /* INKCELL_TOUCH_KEYBOARD_H */
