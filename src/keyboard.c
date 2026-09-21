#define _POSIX_C_SOURCE 200809L

/*
 * The on-screen keyboard's model: where the cursor is, which panel is showing, and what a press
 * does to the caller's buffer.
 *
 * No drawing here and no framebuffer included, which is what lets the whole grid be tested
 * without a panel - see tests/suites/ui_keyboard.c. What a press *means* afterwards is the
 * application's, and inkcell_keyboard_key() says which of the five things happened rather than
 * deciding any of them.
 */

#include "inkcell/ui/keyboard.h"

#include "inkcell/i18n/strings.h"
/* For inkcell_text_cell_next(): a cell is what the user sees as one key, which is what both the
   backspace and the drawability check have to reason about. */
#include "inkcell/ui/emoji.h"

#include <string.h>

/*
 * The three ASCII layers, one row of INKCELL_KB_COLS cells each.
 *
 * A fixed-width array rather than a row of pointers, and that is the invariant rather than a
 * formatting choice: the grid draws a key per column whatever the string holds, so a row one
 * character short is a blank keycap the cursor stops on and A does nothing to - the press that
 * does nothing a handheld refuses everywhere else. Declared this way the compiler rejects a row
 * too long for the grid, and a row too short is NUL-padded rather than read past its own
 * terminator; keyboard_layers_fill_the_grid is what catches the short one.
 *
 * These are inkcell's rather than the application's because a QWERTY grid is not a thing a
 * program has an opinion about. A locale that wants AZERTY needs a second table here, not a
 * translation of this one - the cells are the keys, not words about them.
 */
static const char k_layers[INKCELL_KB_ASCII_LAYERS][INKCELL_KB_CHAR_ROWS][INKCELL_KB_COLS + 1U] = {
    {"1234567890", "qwertyuiop", "asdfghjkl'", "zxcvbnm,.?"},
    {"1234567890", "QWERTYUIOP", "ASDFGHJKL\"", "ZXCVBNM!-:"},
    /*
     * The symbols layer, arranged by *errand* rather than by code point, because finding a
     * character is the whole difficulty of a grid this size. The shifted number row first,
     * where a hand expects it; brackets and arithmetic next; then the sentence punctuation,
     * where `/` sits beside the comma and the full stop rather than between a backslash and
     * a pipe - a topic and a URL are the two things anybody types on a handheld that need
     * it.
     *
     * `'` and `"` are not on this layer and are not missing: they are the tenth cell of the
     * lower and upper layers' home row, where a hand already knows to find them, and the
     * two cells that buys are what `` ` `` and `~` are drawn in. Between the three layers
     * every one of the thirty-two ASCII punctuation marks is reachable, which is what
     * keyboard_reaches_every_printable_character holds.
     *
     * The digits repeat on the last row, the only repeat on the layer and the one that
     * saves a press rather than spending one: an address, a port and a topic are digits and
     * punctuation together, and every one of them would otherwise cost a bounce through two
     * layers per character.
     */
    {"!@#$%^&*()", "-_=+[]{}<>", ";:`~,.?/\\|", "1234567890"},
};

/* Panels in the ring: the three ASCII layers, then one per emoji page. A layout with no emoji
   pages has a three-panel ring, and INKCELL_KB_EMOJI is then a layer the cursor never reaches. */
static unsigned keyboard_panels(const struct inkcell_keyboard_layout *layout) {
    const unsigned pages = (layout != NULL) ? layout->pages : 0U;
    return INKCELL_KB_ASCII_LAYERS + pages;
}

/* Which page is showing, clamped to what the layout actually carries. A cursor left on page 2
   by one layout and then handed a layout with one page would otherwise read past its table. */
static unsigned keyboard_page(const struct inkcell_keyboard *kb,
                              const struct inkcell_keyboard_layout *layout) {
    const unsigned pages = (layout != NULL) ? layout->pages : 0U;
    if (pages == 0U) {
        return 0U;
    }
    return (kb->emoji_page < pages) ? kb->emoji_page : 0U;
}

/* Where the cursor is in the ring: the ASCII layers are panels 0..2 and each page is one after
   them. */
static unsigned keyboard_panel_index(const struct inkcell_keyboard *kb,
                                     const struct inkcell_keyboard_layout *layout) {
    if (kb->layer != INKCELL_KB_EMOJI) {
        return kb->layer;
    }
    return INKCELL_KB_ASCII_LAYERS + keyboard_page(kb, layout);
}

/*
 * The most bytes this keyboard will put in the buffer, NUL excluded.
 *
 * The layout's cap is policy and `size` is physics, and the smaller wins. A cap of 0 means the
 * application has no limit of its own and the buffer is the only one there is.
 */
static size_t keyboard_cap(const struct inkcell_keyboard_layout *layout, size_t size) {
    if (size == 0U) {
        return 0U;
    }
    const size_t room = size - 1U;
    if (layout == NULL || layout->cap == 0U) {
        return room;
    }
    return (layout->cap < room) ? layout->cap : room;
}

/*
 * Append a whole keycap's text, or none of it.
 *
 * All of it or nothing, and that is the emoji case rather than fussiness: the cap is a byte
 * count and an emoji is four of them, so appending as far as the cap would leave a truncated
 * UTF-8 sequence in a field that is about to be written somewhere - the one outcome worse than
 * the character not fitting.
 */
static bool keyboard_append(char *text, size_t size, size_t cap, const char *add) {
    if (add == NULL || add[0] == '\0') {
        return false;
    }
    const size_t len = strlen(text);
    const size_t extra = strlen(add);
    if (len + extra > cap || len + extra + 1U > size) {
        return false;
    }
    memcpy(&text[len], add, extra + 1U);
    return true;
}

/*
 * Remove the last cell.
 *
 * A cell rather than a byte or a code point, walked with the same inkcell_text_cell_next() that
 * measuring and drawing use. A value preloaded from somewhere else may hold UTF-8 this grid
 * cannot type - a flag is a pair of regional indicators and a family is a joined sequence - and
 * deleting by byte would leave a broken sequence behind while deleting by code point would take
 * three presses to remove one thing the user sees. Walking from the start each time is O(n) on a
 * buffer bounded by a text field, which is nothing beside the frame it causes.
 */
static bool keyboard_delete(char *text) {
    size_t last = 0U;
    size_t offset = 0U;
    for (;;) {
        const struct inkcell_text_cell cell = inkcell_text_cell_next(&text[offset]);
        if (cell.bytes == 0U) {
            break;
        }
        last = offset;
        offset += cell.bytes;
    }
    if (offset == 0U) {
        return false;
    }
    text[last] = '\0';
    return true;
}

void inkcell_keyboard_reset(struct inkcell_keyboard *kb) {
    if (kb == NULL) {
        return;
    }
    kb->row = 0U;
    kb->col = 0U;
    kb->layer = (uint8_t)INKCELL_KB_LOWER;
    kb->emoji_page = 0U;
}

const char *inkcell_keyboard_cell(const struct inkcell_keyboard *kb,
                                  const struct inkcell_keyboard_layout *layout, unsigned row,
                                  unsigned col, char scratch[INKCELL_KB_CELL_MAX]) {
    if (kb == NULL || scratch == NULL || row >= INKCELL_KB_CHAR_ROWS || col >= INKCELL_KB_COLS) {
        return "";
    }
    if (kb->layer == INKCELL_KB_EMOJI) {
        if (layout == NULL || layout->emoji == NULL || layout->pages == 0U) {
            return "";
        }
        const unsigned page = keyboard_page(kb, layout);
        const char *const cell =
            layout->emoji[(page * INKCELL_KB_EMOJI_PAGE_CELLS) + (row * INKCELL_KB_COLS) + col];
        return (cell != NULL) ? cell : "";
    }
    if (kb->layer >= INKCELL_KB_ASCII_LAYERS) {
        return "";
    }
    scratch[0] = k_layers[kb->layer][row][col];
    scratch[1] = '\0';
    return scratch;
}

enum inkcell_kb_layer_dest
inkcell_keyboard_layer_dest(const struct inkcell_keyboard *kb,
                            const struct inkcell_keyboard_layout *layout) {
    if (kb == NULL) {
        return INKCELL_KB_DEST_LAYER;
    }
    const unsigned pages = (layout != NULL) ? layout->pages : 0U;
    if (kb->layer == INKCELL_KB_SYMBOLS) {
        /* A layout with no emoji pages wraps from here straight back to the letters, so the key
           promises the letters rather than a layer that is not in the ring. */
        return (pages > 0U) ? INKCELL_KB_DEST_EMOJI : INKCELL_KB_DEST_LAYER;
    }
    if (kb->layer != INKCELL_KB_EMOJI) {
        return INKCELL_KB_DEST_LAYER;
    }
    /* The last page is the end of the ring: from there the key goes back to the letters, which
       is what stops three presses in a row landing on a key that promises the same thing. */
    return (keyboard_page(kb, layout) + 1U < pages) ? INKCELL_KB_DEST_EMOJI_MORE
                                                    : INKCELL_KB_DEST_LAYER;
}

const char *inkcell_keyboard_action_label(const struct inkcell_keyboard *kb,
                                          const struct inkcell_keyboard_layout *layout,
                                          enum inkcell_kb_action action) {
    switch (action) {
    case INKCELL_KB_ACTION_LAYER: {
        /*
         * The key names where it *goes*, which is the only thing about a layer key worth
         * drawing - and with the emoji pages in the same ring, the pages have to name themselves
         * apart or three presses in a row land on a key that says the same thing.
         *
         * The emoji destinations are the two with no word of their own, and what they get here
         * is what a backend with no symbols would have to fall back to. The fb backend draws
         * INKCELL_ICON_EMOJI over them instead, the way it draws a rune over "space" and "del".
         */
        if (kb == NULL) {
            return inkcell_str(INKCELL_STR_KEY_LAYER_LOWER);
        }
        switch (inkcell_keyboard_layer_dest(kb, layout)) {
        case INKCELL_KB_DEST_EMOJI:
            return inkcell_str(INKCELL_STR_KEY_LAYER_EMOJI);
        case INKCELL_KB_DEST_EMOJI_MORE:
            return inkcell_str(INKCELL_STR_KEY_LAYER_EMOJI_MORE);
        default:
            break;
        }
        switch (kb->layer) {
        case INKCELL_KB_LOWER:
            return inkcell_str(INKCELL_STR_KEY_LAYER_UPPER);
        case INKCELL_KB_UPPER:
            return inkcell_str(INKCELL_STR_KEY_LAYER_SYMBOLS);
        default:
            break;
        }
        return inkcell_str(INKCELL_STR_KEY_LAYER_LOWER);
    }
    case INKCELL_KB_ACTION_SPACE:
        return inkcell_str(INKCELL_STR_KEY_SPACE);
    case INKCELL_KB_ACTION_DELETE:
        return inkcell_str(INKCELL_STR_KEY_DELETE);
    case INKCELL_KB_ACTION_SUBMIT:
        /* The one key whose word is the application's. A layout that named none says nothing
           rather than borrowing a word from a neighbouring key. */
        return (layout != NULL) ? inkcell_str(layout->submit_label) : "";
    case INKCELL_KB_ACTION_CANCEL:
        return inkcell_str(INKCELL_STR_KEY_CANCEL);
    default:
        return "";
    }
}

void inkcell_keyboard_panel_step(struct inkcell_keyboard *kb,
                                 const struct inkcell_keyboard_layout *layout, int delta) {
    if (kb == NULL) {
        return;
    }
    const int panels = (int)keyboard_panels(layout);
    /*
     * The step is reduced before the current panel is added to it, not after.
     *
     * Both orders give the same panel, and only one of them is defined: this takes any `int`
     * and says nothing about its range, so a step near INT_MAX added to a non-zero panel index
     * overflows before the modulo ever runs. Reduced first, both operands are smaller than the
     * ring and the sum cannot leave it. `panels` is always positive, so INT_MIN % panels is the
     * well-defined case rather than the trapping one.
     */
    int next = ((int)keyboard_panel_index(kb, layout) + (delta % panels)) % panels;
    /* Modulo on a negative left operand keeps the sign in C, so the step is made positive after
       it wraps rather than before: one back from the first panel is the last one. */
    if (next < 0) {
        next += panels;
    }
    if (next < (int)INKCELL_KB_ASCII_LAYERS) {
        kb->layer = (uint8_t)next;
        kb->emoji_page = 0U;
        return;
    }
    kb->layer = (uint8_t)INKCELL_KB_EMOJI;
    kb->emoji_page = (uint8_t)(next - (int)INKCELL_KB_ASCII_LAYERS);
}

void inkcell_keyboard_shift(struct inkcell_keyboard *kb) {
    if (kb == NULL) {
        return;
    }
    kb->emoji_page = 0U;
    kb->layer = (uint8_t)((kb->layer == INKCELL_KB_UPPER) ? INKCELL_KB_LOWER : INKCELL_KB_UPPER);
}

/* Columns in the row the cursor is on: the action row is five wide under ten narrow ones. */
static unsigned keyboard_row_cols(const struct inkcell_keyboard *kb) {
    return (kb->row == INKCELL_KB_CHAR_ROWS) ? INKCELL_KB_ACTIONS : INKCELL_KB_COLS;
}

/* What the submit and cancel keys do, which is the same whether they were reached by pressing A
   on the action row or by the button that shortcuts to them. */
static enum inkcell_keyboard_result keyboard_action(struct inkcell_keyboard *kb,
                                                    const struct inkcell_keyboard_layout *layout,
                                                    enum inkcell_kb_action action, char *text,
                                                    size_t size) {
    switch (action) {
    case INKCELL_KB_ACTION_LAYER:
        inkcell_keyboard_panel_step(kb, layout, 1);
        return INKCELL_KEYBOARD_CONSUMED;
    case INKCELL_KB_ACTION_SPACE:
        (void)keyboard_append(text, size, keyboard_cap(layout, size), " ");
        return INKCELL_KEYBOARD_CONSUMED;
    case INKCELL_KB_ACTION_DELETE:
        (void)keyboard_delete(text);
        return INKCELL_KEYBOARD_CONSUMED;
    case INKCELL_KB_ACTION_SUBMIT:
        return INKCELL_KEYBOARD_SUBMIT;
    case INKCELL_KB_ACTION_CANCEL:
        return INKCELL_KEYBOARD_CANCEL;
    default:
        return INKCELL_KEYBOARD_IGNORED;
    }
}

enum inkcell_keyboard_result inkcell_keyboard_key(struct inkcell_keyboard *kb,
                                                  const struct inkcell_keyboard_layout *layout,
                                                  enum inkcell_key key, char *text, size_t size) {
    if (kb == NULL || text == NULL || size == 0U) {
        return INKCELL_KEYBOARD_IGNORED;
    }
    switch (key) {
    case INKCELL_KEY_UP:
    case INKCELL_KEY_DOWN: {
        const bool was_actions = (kb->row == INKCELL_KB_CHAR_ROWS);
        if (key == INKCELL_KEY_UP) {
            kb->row = (kb->row == 0U) ? (uint8_t)(INKCELL_KB_ROWS - 1U) : (uint8_t)(kb->row - 1U);
        } else {
            kb->row = (uint8_t)((kb->row + 1U) % INKCELL_KB_ROWS);
        }
        /* Five wide keys under ten narrow ones: keep the cursor under roughly the same spot
           when it crosses between them, rather than under the same index. */
        const bool is_actions = (kb->row == INKCELL_KB_CHAR_ROWS);
        if (!was_actions && is_actions) {
            kb->col = (uint8_t)(kb->col * INKCELL_KB_ACTIONS / INKCELL_KB_COLS);
        } else if (was_actions && !is_actions) {
            kb->col = (uint8_t)(kb->col * INKCELL_KB_COLS / INKCELL_KB_ACTIONS);
        }
        return INKCELL_KEYBOARD_CONSUMED;
    }
    case INKCELL_KEY_LEFT: {
        const unsigned cols = keyboard_row_cols(kb);
        kb->col = (kb->col == 0U) ? (uint8_t)(cols - 1U) : (uint8_t)(kb->col - 1U);
        return INKCELL_KEYBOARD_CONSUMED;
    }
    case INKCELL_KEY_RIGHT: {
        const unsigned cols = keyboard_row_cols(kb);
        kb->col = (uint8_t)((kb->col + 1U) % cols);
        return INKCELL_KEYBOARD_CONSUMED;
    }
    case INKCELL_KEY_A: {
        if (kb->row >= INKCELL_KB_CHAR_ROWS) {
            return keyboard_action(kb, layout, (enum inkcell_kb_action)kb->col, text, size);
        }
        char scratch[INKCELL_KB_CELL_MAX];
        const char *const cell = inkcell_keyboard_cell(kb, layout, kb->row, kb->col, scratch);
        if (keyboard_append(text, size, keyboard_cap(layout, size), cell)) {
            /* One capital, then back to lower case, like a phone keyboard. The emoji layer
               deliberately does not do the same: a run of them is the normal way to use it, and
               a picker that closed itself after one would be a picker nobody uses twice.
               Conditional on the character actually going in, so a field that is full does not
               silently spend the shift the user is still holding for the next one. */
            if (kb->layer == INKCELL_KB_UPPER) {
                kb->layer = (uint8_t)INKCELL_KB_LOWER;
            }
        }
        return INKCELL_KEYBOARD_CONSUMED;
    }
    case INKCELL_KEY_B:
        /*
         * Out of the keyboard, and that is all it does: B is back on every other screen a
         * handheld has, and a keyboard whose B was a backspace is the press people stumble
         * over - every pad-driven keyboard they have used puts backspace on X and leaves B for
         * leaving. What was typed survives; the grid's own cancel key is what discards.
         */
        return INKCELL_KEYBOARD_DISMISS;
    case INKCELL_KEY_X:
        /* Backspace, where a pad-driven keyboard puts it. */
        (void)keyboard_delete(text);
        return INKCELL_KEYBOARD_CONSUMED;
    case INKCELL_KEY_Y:
        (void)keyboard_append(text, size, keyboard_cap(layout, size), " ");
        return INKCELL_KEYBOARD_CONSUMED;
    case INKCELL_KEY_L1:
    case INKCELL_KEY_R1:
        /* The shoulders move between things everywhere else, and a ring of panels is a thing to
           move between. They are not a second backspace and a second space: two buttons spent on
           presses the face already has are two buttons wasted. */
        inkcell_keyboard_panel_step(kb, layout, (key == INKCELL_KEY_L1) ? -1 : 1);
        return INKCELL_KEYBOARD_CONSUMED;
    case INKCELL_KEY_L2:
    case INKCELL_KEY_R2:
        inkcell_keyboard_shift(kb);
        return INKCELL_KEYBOARD_CONSUMED;
    case INKCELL_KEY_START:
        return INKCELL_KEYBOARD_SUBMIT;
    case INKCELL_KEY_SELECT:
    case INKCELL_KEY_NONE:
    default:
        return INKCELL_KEYBOARD_IGNORED;
    }
}

bool inkcell_keyboard_layout_drawable(const struct inkcell_keyboard_layout *layout,
                                      uint32_t *bad_page, uint32_t *bad_cell) {
    if (layout == NULL) {
        return false;
    }
    if (layout->pages > INKCELL_KB_EMOJI_PAGES_MAX) {
        if (bad_page != NULL) {
            *bad_page = (uint32_t)INKCELL_KB_EMOJI_PAGES_MAX;
        }
        return false;
    }
    /* No emoji layer at all is a layout this grid can show perfectly well - it is what a
       keyboard collecting six digits wants. */
    if (layout->pages == 0U) {
        return true;
    }
    if (layout->emoji == NULL) {
        if (bad_page != NULL) {
            *bad_page = 0U;
        }
        return false;
    }
    for (uint32_t page = 0U; page < layout->pages; ++page) {
        for (uint32_t index = 0U; index < INKCELL_KB_EMOJI_PAGE_CELLS; ++index) {
            const char *const cell = layout->emoji[(page * INKCELL_KB_EMOJI_PAGE_CELLS) + index];
            /*
             * One drawable emoji and nothing else. Three ways to fail and they are one failure
             * to whoever is holding the device: a cell with no text is a keycap that does
             * nothing, a cell this build has no sprite for is a box, and a cell holding two
             * things draws one key over its neighbour.
             */
            bool ok = (cell != NULL && cell[0] != '\0');
            if (ok) {
                const struct inkcell_text_cell first = inkcell_text_cell_next(cell);
                ok = (first.bytes != 0U && first.is_emoji && cell[first.bytes] == '\0');
            }
            if (!ok) {
                if (bad_page != NULL) {
                    *bad_page = page;
                }
                if (bad_cell != NULL) {
                    *bad_cell = index;
                }
                return false;
            }
        }
    }
    return true;
}
