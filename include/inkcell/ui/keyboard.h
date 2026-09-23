#ifndef INKCELL_KEYBOARD_H
#define INKCELL_KEYBOARD_H

/*
 * The on-screen keyboard: a grid of characters driven by a d-pad, and the edits it makes to
 * somebody else's buffer.
 *
 * inkcell_fb_draw_text_field() has been here for a while and draws a field with a caret in it.
 * Nothing in inkcell ever put a character *into* one - on a handheld there is no key to press,
 * so every application built on this toolkit had to write its own grid, its own layer ring and
 * its own append-with-a-cap before it could ask the user for a word. Two of them now have, and
 * the second one started by copying the first.
 *
 * What is here is the half that is the same in both: where the cursor is, which panel is
 * showing, what a press does to the text. What is *not* here is what the text is for - no
 * "send", no "this is a passkey", no restoring whatever the keyboard was opened over. That
 * half is the application's and it is the half with all the complexity in it; see the note on
 * `enum inkcell_keyboard_result` for where the line falls and why it falls there.
 *
 * This is a model, not a widget, for the reason actions.h is: which cell the cursor is on is a
 * fact about the nav, a second backend would want the same answer, and it is something a test
 * can assert about without a framebuffer. The drawing is
 * inkcell_fb_draw_keyboard() in the fb backend.
 */

#include "inkcell/i18n/strings.h"
#include "inkcell/ui/key.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Grid geometry: four rows of ten characters over one row of five actions.
 *
 * Fixed rather than configurable, and that is a decision rather than an omission. The grid is
 * sized to a panel a thumb reaches across, the action row's five keys are the five verbs a
 * keyboard has, and an application that wanted eleven columns would be an application whose
 * keys are too narrow to hit. What varies between applications is the *emoji pages* and the
 * submit key's word, and both of those are in `struct inkcell_keyboard_layout` below.
 */
#define INKCELL_KB_COLS 10U
#define INKCELL_KB_CHAR_ROWS 4U
#define INKCELL_KB_ROWS (INKCELL_KB_CHAR_ROWS + 1U)
#define INKCELL_KB_ACTIONS 5U

/* The most bytes one keycap's text can take, with its NUL: the longest emoji anybody puts in a
   page is a ZWJ sequence, and a cell holds one of those. */
#define INKCELL_KB_CELL_MAX 32U

enum inkcell_kb_layer {
    INKCELL_KB_LOWER = 0,
    INKCELL_KB_UPPER,
    INKCELL_KB_SYMBOLS,
    /*
     * The one layer whose cells are not ASCII, and the one whose contents inkcell does not
     * know. It is pages rather than a single grid - forty keys is what fits and a useful set is
     * several times that - and the pages come from the application, because a set chosen for a
     * radio on a hillside (tents, ambulances, SOS) is the wrong set for a music player. See
     * `struct inkcell_keyboard_layout`.
     */
    INKCELL_KB_EMOJI,
    INKCELL_KB_LAYER_COUNT,
};

/* The layers whose cells are one ASCII character: everything before the emoji one. These three
   are inkcell's own tables - a QWERTY grid is not a thing an application has an opinion about. */
#define INKCELL_KB_ASCII_LAYERS ((unsigned)INKCELL_KB_EMOJI)

/* Cells on one emoji page - the character grid, filled. */
#define INKCELL_KB_EMOJI_PAGE_CELLS (INKCELL_KB_CHAR_ROWS * INKCELL_KB_COLS)

/*
 * The most emoji pages a layout may carry.
 *
 * Two things point at a small number here and they agree. The ring is walked one press at a
 * time, so a page at the far end of a long one is a page nobody reaches; and the cursor stores
 * which page is showing in a byte, alongside a panel index that has the three ASCII layers in
 * front of it - a layout free to declare more pages than that byte can name would have pages
 * the cursor cannot address, which is a keycap that does nothing dressed up as a capability.
 *
 * Sixteen is 640 emoji, which is well past what anybody steps through and well inside the byte.
 * Like INKCELL_ACTIONS_MAX, it is a cap on the *array* rather than on what fits: raising it is
 * a one-line change, and `pages` being a uint8_t is what keeps the unrepresentable case from
 * being expressible at all. inkcell_keyboard_layout_drawable() rejects a layout over the cap.
 */
#define INKCELL_KB_EMOJI_PAGES_MAX 16U

enum inkcell_kb_action {
    INKCELL_KB_ACTION_LAYER = 0, /* step the panel ring: abc, ABC, symbols, then the emoji pages */
    INKCELL_KB_ACTION_SPACE,
    INKCELL_KB_ACTION_DELETE,
    INKCELL_KB_ACTION_SUBMIT,
    INKCELL_KB_ACTION_CANCEL,
};

/*
 * What an application hands the keyboard: the things that differ between two programs using
 * the same grid.
 *
 * Deliberately separate from `struct inkcell_keyboard` and deliberately full of pointers. The
 * state below goes in a snapshot and is compared with memcmp; this does not go anywhere - it is
 * a const description the caller already has, passed in at each press and each draw.
 */
struct inkcell_keyboard_layout {
    /*
     * The emoji pages, `pages` of them, each INKCELL_KB_EMOJI_PAGE_CELLS entries in row-major
     * order, and at most INKCELL_KB_EMOJI_PAGES_MAX of them. NULL with `pages` 0 is a keyboard
     * with no emoji layer at all, which is the right answer for one collecting six digits.
     *
     * Every cell must be a glyph this build can actually draw, or the user gets a keycap that
     * looks blank and a press that appends something no panel will show. inkcell owns the glyph
     * tables, so inkcell is where that can be checked: inkcell_keyboard_layout_drawable() is
     * for an application's test to call, not for the draw path.
     */
    const char *const *emoji;
    /* A byte for the reason `emoji_page` is one: the two are the same number seen from either
       end, and a width the cursor cannot match is a page the cursor cannot reach. */
    uint8_t pages;
    /*
     * The word on the submit key.
     *
     * Handed in rather than taken from inkcell's catalog, and that follows the rule the catalog
     * states about itself: a string belongs to inkcell only when inkcell has nowhere to be
     * handed it. The other four keys are inkcell's - space is space in every program that has
     * one - but this key says "Send" over a message and "Done" over a setting, and the
     * difference is about what the application is *for*. A toolkit that picked one would be
     * telling a music player it is sending something.
     */
    inkcell_str_id submit_label;
    /*
     * The most bytes of text this keyboard will accept, not counting the NUL. 0 means "whatever
     * the buffer holds".
     *
     * A byte count rather than a character count because that is what the buffer downstream of
     * it is measured in - a name field is as long as the record it goes into. The append path
     * is all-or-nothing about a multi-byte cell for exactly this reason: appending an emoji as
     * far as the cap would leave a truncated UTF-8 sequence in a field about to be written
     * somewhere, which is the one outcome worse than the character not fitting.
     */
    size_t cap;
};

/*
 * Where the user is in the grid.
 *
 * Pointer-free and fixed-size on purpose: an application keeps this in whatever record its
 * backends read, and at least one of them compares two of those with memcmp to decide whether a
 * frame can be a partial redraw. A pointer in here would make that compare addresses.
 *
 * The text is not in here either, and that is the same decision from the other side. The buffer
 * belongs to the caller - it is the caller that parks it when something interrupts, restores it
 * afterwards, and hands it to whatever consumes it - so the keyboard is passed the buffer at
 * each press rather than owning a copy that then has to be kept in step with the real one.
 */
struct inkcell_keyboard {
    uint8_t row;
    uint8_t col;
    uint8_t layer;      /* enum inkcell_kb_layer */
    uint8_t emoji_page; /* which page, while `layer` is INKCELL_KB_EMOJI */
};

/*
 * What a press amounted to.
 *
 * This enum is the whole seam, so it is worth saying what each one leaves the caller to do.
 * inkcell moves the cursor, walks the panels and edits the bytes; it never decides what the
 * text was for, because "commit this as a passkey" and "put this on the air" are not facts a
 * toolkit can hold.
 *
 * CANCEL and DISMISS are two answers rather than one, and that distinction is the one piece of
 * hard-won behaviour being carried down here rather than reinvented. Backing out is not the
 * same press as throwing away: **B** leaves the keyboard with what was typed intact, because B
 * is "back" on every other screen a handheld has and a user who pressed it did not ask to lose
 * a paragraph. The grid's own cancel key is what discards. A keyboard that answered both with
 * one result would force every caller to work out which press it was, which is the thing this
 * enum exists to have already done.
 */
enum inkcell_keyboard_result {
    /* Nothing here uses that key. The caller may do what it likes with it. */
    INKCELL_KEYBOARD_IGNORED = 0,
    /*
     * The keyboard took the key. Redraw; nothing else is owed.
     *
     * "Took it", not "changed something", and the difference matters in one place: a letter
     * pressed on a field already at its cap, and a backspace on an empty one, are both this
     * rather than IGNORED. Reporting those as IGNORED would be telling the caller the key was
     * going spare, and a caller that then acted on it would navigate away from a keyboard the
     * user was still typing into. A redundant frame costs a memcmp; a wrong one costs the
     * draft.
     */
    INKCELL_KEYBOARD_CONSUMED,
    /* The user finished: submit key, or START. The text is in the caller's buffer and what it
       means is the caller's business. */
    INKCELL_KEYBOARD_SUBMIT,
    /* The user threw it away: the cancel key. inkcell does *not* clear the buffer - a caller
       restoring a parked draft needs to be the one that decides what the field goes back to. */
    INKCELL_KEYBOARD_CANCEL,
    /* The user backed out and the text stands: B. The caller closes the keyboard and keeps what
       is in the buffer. */
    INKCELL_KEYBOARD_DISMISS,
};

/* Cursor to the top-left, back to the lower-case layer. What a keyboard being opened wants, and
   what a keyboard being closed wants, so the next one starts the same way. Touches no text. */
void inkcell_keyboard_reset(struct inkcell_keyboard *kb);

/*
 * The text on one character cell, as a NUL-terminated string.
 *
 * Returns a pointer into either the layout's emoji table or `scratch`, which is why the caller
 * supplies the scratch: an ASCII cell is one character that has to live somewhere, and a cell
 * that returned a pointer to a static would not be re-entrant across a row being measured and
 * then drawn.
 *
 * "" for a cell that is off the grid or on a page that is not there. Never NULL.
 */
const char *inkcell_keyboard_cell(const struct inkcell_keyboard *kb,
                                  const struct inkcell_keyboard_layout *layout, unsigned row,
                                  unsigned col, char scratch[INKCELL_KB_CELL_MAX]);

/*
 * The word on one action key, in the current state.
 *
 * The layer key names where it *goes* rather than where it is, which is the only thing about a
 * layer key worth drawing - and with the emoji pages in the same ring, the pages have to name
 * themselves apart or three presses in a row land on a key that says the same thing.
 *
 * Three of the four panels are their own keycap: "abc", "ABC", "#+=". The emoji layer is not a
 * word in any language, and a renderer with symbols to hand should draw one rather than this
 * word - see inkcell_keyboard_layer_dest() below, which is the question it asks first.
 */
const char *inkcell_keyboard_action_label(const struct inkcell_keyboard *kb,
                                          const struct inkcell_keyboard_layout *layout,
                                          enum inkcell_kb_action action);

/*
 * Where the layer key goes, as a renderer that draws symbols needs it.
 *
 * The label above answers this in words, which is all a text-only backend can do. A backend
 * with sprites wants the answer as a fact rather than a string to compare: the emoji
 * destinations are a picture of a face, and the way to draw one is not to recognise ":)" coming
 * back out of the catalog.
 *
 * Which destination it is depends on the layout as well as the cursor - a keyboard with no
 * emoji pages never offers one, and the last page goes back to the letters rather than on to a
 * page that is not there.
 */
enum inkcell_kb_layer_dest {
    /* A layer that names itself, and the label is that name. */
    INKCELL_KB_DEST_LAYER = 0,
    /* The first page of emoji, which is where the symbols layer leads when there are any. */
    INKCELL_KB_DEST_EMOJI,
    /* Another page of them, from a page that is not the last. */
    INKCELL_KB_DEST_EMOJI_MORE,
};

enum inkcell_kb_layer_dest
inkcell_keyboard_layer_dest(const struct inkcell_keyboard *kb,
                            const struct inkcell_keyboard_layout *layout);

/*
 * Step the panel ring by `delta`, wrapping: the three ASCII layers are panels 0..2 and each
 * emoji page is a panel after them.
 *
 * One ring rather than a layer ring with a page ring inside it, because two rings is two things
 * to learn about a grid whose whole job is to be obvious.
 */
void inkcell_keyboard_panel_step(struct inkcell_keyboard *kb,
                                 const struct inkcell_keyboard_layout *layout, int delta);

/* Between lower and upper, from wherever. The shift key, which is a different press from
   stepping the ring - it is the one layer change a hand makes mid-word. */
void inkcell_keyboard_shift(struct inkcell_keyboard *kb);

/*
 * A press. Moves the cursor, walks the panels, and edits `text` in place.
 *
 * `size` is the buffer's own size, NUL included; the layout's `cap` is the policy limit on top
 * of it, and the effective limit is the smaller. Both, rather than one, because they answer
 * different questions - `size` is what must not be overrun and `cap` is what the field will
 * accept, and a caller that conflated them would be one refactor away from a field whose limit
 * silently became its buffer.
 *
 * `text` must be NUL-terminated on entry and is NUL-terminated on return. Deletion is by
 * character, not by byte: a value preloaded from somewhere else may hold UTF-8 this grid cannot
 * type, and deleting byte-wise would leave a broken sequence behind.
 */
enum inkcell_keyboard_result inkcell_keyboard_key(struct inkcell_keyboard *kb,
                                                  const struct inkcell_keyboard_layout *layout,
                                                  enum inkcell_key key, char *text, size_t size);

/* Append committed UTF-8 text from a host keyboard or IME. Rejects controls and malformed
   sequences, and applies the same byte cap as the grid. The whole input fits or none of it is
   appended. Returns true only when the buffer changed. */
bool inkcell_keyboard_insert_text(const struct inkcell_keyboard_layout *layout, char *text,
                                  size_t size, const char *input);

/*
 * The layout is one this grid can actually show: no more than INKCELL_KB_EMOJI_PAGES_MAX pages,
 * every page full, and every cell a glyph this build can draw.
 *
 * For an application's test, which is where this check has to live and where it could not live
 * before: the glyph tables are inkcell's, so an application asserting its own emoji table was
 * drawable was a test reaching down a layer to read data it does not own. The page cap is
 * checked in the same breath because it fails the same way - a page that is there and cannot be
 * reached and a cell that is there and cannot be seen are one bug to the person holding it.
 *
 * `bad_page` and `bad_cell` name the first offender when this returns false, for a failure
 * message that says which one rather than that there is one. Either may be NULL; over the page
 * cap, `bad_page` is the first page past it and `bad_cell` is left alone.
 */
bool inkcell_keyboard_layout_drawable(const struct inkcell_keyboard_layout *layout,
                                      uint32_t *bad_page, uint32_t *bad_cell);

#endif /* INKCELL_KEYBOARD_H */
