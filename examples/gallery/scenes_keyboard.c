#define _POSIX_C_SOURCE 200809L

/*
 * The on-screen keyboard, drawn as a screen that collects a word really draws it: a heading
 * saying what is being typed, the field holding it, and the grid filling what is left.
 *
 * Two scenes rather than one, because the grid has two visibly different halves and a sheet
 * showing one of them would leave the other with no test at all. A character key sets its label
 * at the keycap scale this widget works out; an emoji key is a sprite drawn at the size of the
 * box, which is a different path through the button and the one that looks wrong first. The
 * second scene is also where the paging shows: the layer key on a panel with another page
 * behind it says something different from the one on the last page.
 *
 * The emoji pages are the gallery's own, which is the point of them being a pointer on
 * `struct inkcell_keyboard_layout`: a set chosen for a radio on a hillside is the wrong set for
 * a music player, so inkcell carries none and every application hands over its own. These are
 * written as \U escapes for the reason an application's would be - an editor or a patch tool
 * that mangles non-ASCII cannot quietly change what is on the keycap, and at forty cells a
 * code point naming itself is the readable form.
 */

#include "gallery.h"

/*
 * Two pages of forty, in row-major order: the table inkcell_keyboard_cell() indexes.
 *
 * Flat rather than [page][row][col] because that is the shape the layout's `emoji` is - a
 * pointer to the first of them - and an application that declared it as a cube would be handing
 * over a pointer whose shape only one of the two sides knows.
 *
 * Which leaves the formatter as the only thing keeping the grid legible, so it is turned off
 * over the table: ten cells is a row of keys, and which cell sits under which is the one thing
 * about this worth reading. Reflowed to a string a line it is a hundred and fourteen lines of
 * escapes nothing can be found in, and a cell in the wrong column is invisible in the diff.
 */
/* clang-format off */
static const char *const k_emoji[] = {
    /* Page one: faces, hands, and what lives out there. */
    /* grinning through smiling */
    "\U0001F600", "\U0001F603", "\U0001F604", "\U0001F601", "\U0001F606",
    "\U0001F605", "\U0001F602", "\U0001F642", "\U0001F609", "\U0001F60A",
    /* fond, thinking, tired, upset */
    "\U0001F60D", "\U0001F618", "\U0001F61C", "\U0001F914", "\U0001F610",
    "\U0001F634", "\U0001F60E", "\U0001F62D", "\U0001F622", "\U0001F631",
    /* cross, celebrating, unwell, then the hands */
    "\U0001F621", "\U0001F633", "\U0001F973", "\U0001F912", "\U0001F91D",
    "\U0001F44D", "\U0001F44E", "\U0001F44B", "\U0001F44C", "\U0000270C",
    /* please, strength, watching, people and what lives out there */
    "\U0001F64F", "\U0001F4AA", "\U0001F440", "\U0001F9E0", "\U0001F464",
    "\U0001F46A", "\U0001F415", "\U0001F43B", "\U0001F98C", "\U0001F40D",
    /* Page two: marks, symbols and status. */
    /* affection and occasions */
    "\U00002764", "\U0001F494", "\U0001F4AF", "\U00002728", "\U00002B50",
    "\U0001F525", "\U0001F389", "\U0001F382", "\U0001F381", "\U0000262E",
    /* yes, no, careful, asking, and time */
    "\U00002705", "\U0000274C", "\U000026A0", "\U00002753", "\U00002757",
    "\U0000203C", "\U00002795", "\U00002796", "\U000023F0", "\U0000231B",
    /* attention, places, and writing */
    "\U0001F514", "\U0001F515", "\U0001F4CC", "\U0001F4CD", "\U0001F4CE",
    "\U0001F4DD", "\U0001F4D6", "\U0001F4AC", "\U0001F4E3", "\U0001F517",
    /* locks, eyes, power, and the two arrows */
    "\U0001F512", "\U0001F513", "\U0001F511", "\U0001F441", "\U0001F4A4",
    "\U0000267B", "\U000026A1", "\U00002622", "\U00002B06", "\U00002B07",
};
/* clang-format on */

/* What this gallery's keyboard is for: two pages of pictures, a key that says "Send", and a
   message-sized cap. The submit key's word and icon are the application's - see the note on
   `submit_icon` - and a gallery that is pretending to write a message says so on both. */
static const struct inkcell_keyboard_layout k_layout = {
    .emoji = k_emoji,
    .pages = 2U,
    .submit_label = (inkcell_str_id)GALLERY_STR_ACT_SEND,
    .cap = 160U,
};

_Static_assert(sizeof k_emoji / sizeof k_emoji[0] == 2U * (size_t)INKCELL_KB_EMOJI_PAGE_CELLS,
               "the emoji table and the page count disagree about how many cells there are");

/* The frame both scenes stand in: the heading, the draft, and the row the grid starts on. */
static int gallery_keyboard_frame(struct inkcell_draw_state *state,
                                  struct inkcell_fb_layout *layout, const char *counter) {
    *layout = gallery_frame(state, GALLERY_STR_HEAD_KEYBOARD, 0U);
    int y = layout->body_y;
    const struct inkcell_fb_text_field draft = {
        .value = gallery_text(GALLERY_STR_FIELD_VALUE),
        .caret = true,
        .lines = 2U,
        .counter = counter,
    };
    inkcell_fb_draw_text_field(state, layout, &y, &draft);
    return y;
}

void gallery_scene_keyboard(struct inkcell_draw_state *state) {
    struct inkcell_fb_layout layout;
    int y = gallery_keyboard_frame(state, &layout, "29/160");

    /* Lower case with the cursor in the middle of the grid, which is where a keyboard spends
       almost all of its life. The cursor is on a letter rather than on an action key so that
       both states of a character key - focused and at rest - are in the same picture. */
    const struct inkcell_keyboard keyboard = {
        .row = 2U,
        .col = 4U,
        .layer = (uint8_t)INKCELL_KB_LOWER,
    };
    const struct inkcell_fb_keyboard grid = {
        .keyboard = &keyboard,
        .layout = &k_layout,
        .submit_icon = INKCELL_ICON_SEND,
    };
    inkcell_fb_draw_keyboard(state, &layout, &y, &grid);

    gallery_footer(state, &layout);
}

void gallery_scene_keyboard_emoji(struct inkcell_draw_state *state) {
    struct inkcell_fb_layout layout;
    int y = gallery_keyboard_frame(state, &layout, "31/160");

    /* The first of two pages, with the cursor on the layer key - so the picture carries both
       the sprites at keycap size and the face that says where the next press goes, which on a
       page with another behind it is not the one the last page shows. */
    const struct inkcell_keyboard keyboard = {
        .row = (uint8_t)INKCELL_KB_CHAR_ROWS,
        .col = (uint8_t)INKCELL_KB_ACTION_LAYER,
        .layer = (uint8_t)INKCELL_KB_EMOJI,
        .emoji_page = 0U,
    };
    const struct inkcell_fb_keyboard grid = {
        .keyboard = &keyboard,
        .layout = &k_layout,
        .submit_icon = INKCELL_ICON_SEND,
    };
    inkcell_fb_draw_keyboard(state, &layout, &y, &grid);

    gallery_footer(state, &layout);
}
