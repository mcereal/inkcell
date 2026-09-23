#define _POSIX_C_SOURCE 200809L

/*
 * The on-screen keyboard's model: the grid, the panel ring, and what a press does to a buffer.
 *
 * All of it without a framebuffer, which is the point of the keyboard being a model rather than
 * a widget - every case here is about what the user would see happen, asserted against the same
 * functions a renderer asks.
 */

#include "framework/inkcell_test.h"

#include "inkcell/ui/keyboard.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

/* A grinning face: one code point, no variation selector, and a sprite every build of the emoji
   table carries. The cell the drawability check is happy with, so the failing variants below
   differ from it in exactly one way each. */
#define TEST_EMOJI "\U0001F600"

static const char *test_page[INKCELL_KB_EMOJI_PAGE_CELLS];
static const char *test_page_two[INKCELL_KB_EMOJI_PAGE_CELLS * 2U];

static void fill_page(const char **page, size_t cells, const char *cell) {
    for (size_t i = 0U; i < cells; ++i) {
        page[i] = cell;
    }
}

static struct inkcell_keyboard_layout one_page_layout(void) {
    fill_page(test_page, INKCELL_KB_EMOJI_PAGE_CELLS, TEST_EMOJI);
    struct inkcell_keyboard_layout layout;
    memset(&layout, 0, sizeof layout);
    layout.emoji = test_page;
    layout.pages = 1U;
    layout.submit_label = INKCELL_STR_KEY_SPACE; /* any id: these cases never read the word */
    return layout;
}

/* A layout with no emoji layer at all - what a keyboard collecting six digits wants. */
static struct inkcell_keyboard_layout bare_layout(void) {
    struct inkcell_keyboard_layout layout;
    memset(&layout, 0, sizeof layout);
    layout.submit_label = INKCELL_STR_KEY_SPACE;
    return layout;
}

/*
 * A row one character short is a blank keycap the cursor stops on and A does nothing to, which
 * is the press a handheld refuses everywhere else. The array's declared width stops a row that
 * is too long at compile time; this is the other half.
 */
INKCELL_TEST_CASE(keyboard_layers_fill_the_grid, unit) {
    struct inkcell_keyboard kb;
    inkcell_keyboard_reset(&kb);
    const struct inkcell_keyboard_layout layout = bare_layout();

    for (unsigned layer = 0U; layer < INKCELL_KB_ASCII_LAYERS; ++layer) {
        kb.layer = (uint8_t)layer;
        for (unsigned row = 0U; row < INKCELL_KB_CHAR_ROWS; ++row) {
            for (unsigned col = 0U; col < INKCELL_KB_COLS; ++col) {
                char scratch[INKCELL_KB_CELL_MAX];
                const char *const cell = inkcell_keyboard_cell(&kb, &layout, row, col, scratch);
                char message[96];
                snprintf(message, sizeof message, "layer %u row %u column %u draws no key", layer,
                         row, col);
                INKCELL_TEST_FAIL_IF(cell[0] == '\0', message);
            }
        }
    }
}

/*
 * Every printable ASCII character is somewhere in the three layers.
 *
 * Space is the action key rather than a cell, so the range starts after it. Without this a
 * character can go missing when a row is rearranged - which is how the layer that looked like
 * it ended at its third row was found.
 */
INKCELL_TEST_CASE(keyboard_reaches_every_printable_character, unit) {
    bool seen[128] = {false};
    struct inkcell_keyboard kb;
    inkcell_keyboard_reset(&kb);
    const struct inkcell_keyboard_layout layout = bare_layout();

    for (unsigned layer = 0U; layer < INKCELL_KB_ASCII_LAYERS; ++layer) {
        kb.layer = (uint8_t)layer;
        for (unsigned row = 0U; row < INKCELL_KB_CHAR_ROWS; ++row) {
            for (unsigned col = 0U; col < INKCELL_KB_COLS; ++col) {
                char scratch[INKCELL_KB_CELL_MAX];
                const char *const cell = inkcell_keyboard_cell(&kb, &layout, row, col, scratch);
                const unsigned char ch = (unsigned char)cell[0];
                if (ch < sizeof seen / sizeof seen[0]) {
                    seen[ch] = true;
                }
            }
        }
    }

    for (unsigned char ch = '!'; ch <= '~'; ++ch) {
        char message[80];
        snprintf(message, sizeof message, "'%c' is on no layer of the keyboard", ch);
        INKCELL_TEST_FAIL_IF(!seen[ch], message);
    }
}

/* The ring is one ring: the three ASCII layers, then a panel per emoji page, and the step wraps
   both ways. Two rings - a layer ring with a page ring inside it - would be two things to learn
   about a grid whose whole job is to be obvious. */
INKCELL_TEST_CASE(keyboard_panel_ring_wraps_both_ways, unit) {
    fill_page(test_page_two, INKCELL_KB_EMOJI_PAGE_CELLS * 2U, TEST_EMOJI);
    struct inkcell_keyboard_layout layout = one_page_layout();
    layout.emoji = test_page_two;
    layout.pages = 2U;

    struct inkcell_keyboard kb;
    inkcell_keyboard_reset(&kb);

    const uint8_t expect_layer[5] = {INKCELL_KB_UPPER, INKCELL_KB_SYMBOLS, INKCELL_KB_EMOJI,
                                     INKCELL_KB_EMOJI, INKCELL_KB_LOWER};
    const uint8_t expect_page[5] = {0U, 0U, 0U, 1U, 0U};
    for (unsigned step = 0U; step < 5U; ++step) {
        inkcell_keyboard_panel_step(&kb, &layout, 1);
        char message[96];
        snprintf(message, sizeof message, "step %u should land on layer %u page %u", step,
                 expect_layer[step], expect_page[step]);
        INKCELL_TEST_FAIL_IF(kb.layer != expect_layer[step] || kb.emoji_page != expect_page[step],
                             message);
    }

    /* One back from the first panel is the last one, which is the case C's sign-keeping modulo
       gets wrong if the step is wrapped after it rather than before. */
    inkcell_keyboard_reset(&kb);
    inkcell_keyboard_panel_step(&kb, &layout, -1);
    INKCELL_TEST_FAIL_IF(kb.layer != INKCELL_KB_EMOJI || kb.emoji_page != 1U,
                         "stepping back from the first panel should land on the last");
}

/* A layout with no emoji pages has a three-panel ring, and the emoji layer is one the cursor
   never reaches - including by the layer key, which must not promise a layer that is not there. */
INKCELL_TEST_CASE(keyboard_ring_skips_an_absent_emoji_layer, unit) {
    const struct inkcell_keyboard_layout layout = bare_layout();
    struct inkcell_keyboard kb;
    inkcell_keyboard_reset(&kb);

    for (unsigned step = 0U; step < 3U; ++step) {
        inkcell_keyboard_panel_step(&kb, &layout, 1);
        INKCELL_TEST_FAIL_IF(kb.layer == INKCELL_KB_EMOJI,
                             "a layout with no pages should never reach the emoji layer");
    }
    INKCELL_TEST_FAIL_IF(kb.layer != INKCELL_KB_LOWER,
                         "three steps of a three-panel ring should come back to the start");

    kb.layer = (uint8_t)INKCELL_KB_SYMBOLS;
    const char *const label = inkcell_keyboard_action_label(&kb, &layout, INKCELL_KB_ACTION_LAYER);
    INKCELL_TEST_FAIL_IF(strcmp(label, inkcell_str(INKCELL_STR_KEY_LAYER_LOWER)) != 0,
                         "with no emoji pages the layer key should promise the letters back");
    INKCELL_TEST_FAIL_IF(inkcell_keyboard_layer_dest(&kb, &layout) != INKCELL_KB_DEST_LAYER,
                         "a key that goes back to the letters is not a key that offers a face");
}

/* The last page's layer key says "back to letters" rather than "another page": three presses in
   a row landing on a key that says the same thing is what the EMOJI_MORE id exists to stop. */
INKCELL_TEST_CASE(keyboard_layer_key_names_where_it_goes, unit) {
    struct inkcell_keyboard_layout layout = one_page_layout();
    layout.emoji = test_page_two;
    layout.pages = 2U;
    fill_page(test_page_two, INKCELL_KB_EMOJI_PAGE_CELLS * 2U, TEST_EMOJI);

    struct inkcell_keyboard kb;
    inkcell_keyboard_reset(&kb);

    struct {
        uint8_t layer;
        uint8_t page;
        inkcell_str_id expect;
        enum inkcell_kb_layer_dest dest;
    } const cases[] = {
        {INKCELL_KB_LOWER, 0U, INKCELL_STR_KEY_LAYER_UPPER, INKCELL_KB_DEST_LAYER},
        {INKCELL_KB_UPPER, 0U, INKCELL_STR_KEY_LAYER_SYMBOLS, INKCELL_KB_DEST_LAYER},
        {INKCELL_KB_SYMBOLS, 0U, INKCELL_STR_KEY_LAYER_EMOJI, INKCELL_KB_DEST_EMOJI},
        {INKCELL_KB_EMOJI, 0U, INKCELL_STR_KEY_LAYER_EMOJI_MORE, INKCELL_KB_DEST_EMOJI_MORE},
        {INKCELL_KB_EMOJI, 1U, INKCELL_STR_KEY_LAYER_LOWER, INKCELL_KB_DEST_LAYER},
    };
    for (size_t i = 0U; i < sizeof cases / sizeof cases[0]; ++i) {
        kb.layer = cases[i].layer;
        kb.emoji_page = cases[i].page;
        const char *const label =
            inkcell_keyboard_action_label(&kb, &layout, INKCELL_KB_ACTION_LAYER);
        char message[96];
        snprintf(message, sizeof message, "layer %u page %u names the wrong destination",
                 cases[i].layer, cases[i].page);
        INKCELL_TEST_FAIL_IF(strcmp(label, inkcell_str(cases[i].expect)) != 0, message);
        /* And the same answer as a fact rather than as a word, which is what a backend with
           sprites draws the key from: the two must not be able to disagree. */
        snprintf(message, sizeof message, "layer %u page %u offers the wrong symbol",
                 cases[i].layer, cases[i].page);
        INKCELL_TEST_FAIL_IF(inkcell_keyboard_layer_dest(&kb, &layout) != cases[i].dest, message);
    }
}

/* The submit key is the one word the application supplies, because "Send" over a message and
   "Done" over a setting is a difference about what the program is for. */
INKCELL_TEST_CASE(keyboard_submit_label_comes_from_the_layout, unit) {
    struct inkcell_keyboard_layout layout = bare_layout();
    layout.submit_label = INKCELL_STR_KEY_CANCEL; /* a stand-in for an application's own id */

    struct inkcell_keyboard kb;
    inkcell_keyboard_reset(&kb);
    const char *const label = inkcell_keyboard_action_label(&kb, &layout, INKCELL_KB_ACTION_SUBMIT);
    INKCELL_TEST_FAIL_IF(strcmp(label, inkcell_str(INKCELL_STR_KEY_CANCEL)) != 0,
                         "the submit key should say what the layout named");
}

/* Five wide keys under ten narrow ones: crossing between them keeps the cursor under roughly
   the same place on the panel rather than under the same index. */
INKCELL_TEST_CASE(keyboard_cursor_maps_across_the_action_row, unit) {
    const struct inkcell_keyboard_layout layout = bare_layout();
    char text[32] = "";
    struct inkcell_keyboard kb;
    inkcell_keyboard_reset(&kb);

    kb.row = (uint8_t)(INKCELL_KB_CHAR_ROWS - 1U);
    kb.col = 8U;
    (void)inkcell_keyboard_key(&kb, &layout, INKCELL_KEY_DOWN, text, sizeof text);
    INKCELL_TEST_FAIL_IF(kb.row != INKCELL_KB_CHAR_ROWS, "down from the last row is the actions");
    INKCELL_TEST_FAIL_IF(kb.col != 4U, "column 8 of ten should land on column 4 of five");

    (void)inkcell_keyboard_key(&kb, &layout, INKCELL_KEY_UP, text, sizeof text);
    INKCELL_TEST_FAIL_IF(kb.col != 8U, "column 4 of five should come back to column 8 of ten");

    /* The row wraps, and the action row's own width is what left and right walk. */
    kb.row = INKCELL_KB_CHAR_ROWS;
    kb.col = 0U;
    (void)inkcell_keyboard_key(&kb, &layout, INKCELL_KEY_LEFT, text, sizeof text);
    INKCELL_TEST_FAIL_IF(kb.col != INKCELL_KB_ACTIONS - 1U,
                         "left from the first action wraps to the last");
}

/* A capital, then back to lower case, like a phone keyboard - but only when the character
   actually went in, so a field at its cap does not spend the shift the user is holding. */
INKCELL_TEST_CASE(keyboard_shift_is_one_shot_when_it_types, unit) {
    const struct inkcell_keyboard_layout layout = bare_layout();
    struct inkcell_keyboard kb;
    inkcell_keyboard_reset(&kb);

    char text[8] = "";
    inkcell_keyboard_shift(&kb);
    INKCELL_TEST_FAIL_IF(kb.layer != INKCELL_KB_UPPER, "shift should reach the upper layer");
    kb.row = 1U; /* the letter rows */
    kb.col = 0U;
    (void)inkcell_keyboard_key(&kb, &layout, INKCELL_KEY_A, text, sizeof text);
    INKCELL_TEST_FAIL_IF(strcmp(text, "Q") != 0, "the upper layer should type a capital");
    INKCELL_TEST_FAIL_IF(kb.layer != INKCELL_KB_LOWER, "one capital, then back to lower case");

    /* Now with no room: the shift has to survive a press that typed nothing. */
    char full[2] = "x";
    inkcell_keyboard_shift(&kb);
    (void)inkcell_keyboard_key(&kb, &layout, INKCELL_KEY_A, full, sizeof full);
    INKCELL_TEST_FAIL_IF(strcmp(full, "x") != 0, "a full field should take no more");
    INKCELL_TEST_FAIL_IF(kb.layer != INKCELL_KB_UPPER,
                         "a press that typed nothing should not spend the shift");
}

/*
 * All of an emoji or none of it.
 *
 * The cap is a byte count and an emoji is four of them, so appending as far as the cap would
 * leave a truncated UTF-8 sequence in a field about to be written somewhere - the one outcome
 * worse than the character not fitting.
 */
INKCELL_TEST_CASE(keyboard_append_is_all_of_a_cell_or_none, unit) {
    struct inkcell_keyboard_layout layout = one_page_layout();
    layout.cap = 8U;

    struct inkcell_keyboard kb;
    inkcell_keyboard_reset(&kb);
    kb.layer = (uint8_t)INKCELL_KB_EMOJI;

    char text[64] = "ab";
    (void)inkcell_keyboard_key(&kb, &layout, INKCELL_KEY_A, text, sizeof text);
    INKCELL_TEST_FAIL_IF(strcmp(text, "ab" TEST_EMOJI) != 0,
                         "an emoji that fits the cap should go in whole");

    /* Six bytes used of eight, so there is room left but not four bytes of it. Partial room is
       the case that matters: a byte-at-a-time append would leave half a sequence here. */
    (void)inkcell_keyboard_key(&kb, &layout, INKCELL_KEY_A, text, sizeof text);
    INKCELL_TEST_FAIL_IF(strcmp(text, "ab" TEST_EMOJI) != 0,
                         "an emoji that does not fit should not go in in part");

    /* The buffer is the other limit, and it wins when it is the smaller. */
    char tiny[4] = "";
    layout.cap = 0U; /* no policy limit: the buffer is all there is */
    (void)inkcell_keyboard_key(&kb, &layout, INKCELL_KEY_A, tiny, sizeof tiny);
    INKCELL_TEST_FAIL_IF(tiny[0] != '\0', "a four-byte emoji should not fit a four-byte buffer");
}

INKCELL_TEST_CASE(keyboard_host_text_obeys_cap_and_utf8_rules, unit) {
    struct inkcell_keyboard_layout layout = bare_layout();
    layout.cap = 8U;
    char text[32] = "ab";

    INKCELL_TEST_FAIL_IF(!inkcell_keyboard_insert_text(&layout, text, sizeof text, "c" TEST_EMOJI),
                         "committed text and a complete emoji should fit together");
    INKCELL_TEST_FAIL_IF(strcmp(text, "abc" TEST_EMOJI) != 0,
                         "host text should append exactly what was committed");
    INKCELL_TEST_FAIL_IF(inkcell_keyboard_insert_text(&layout, text, sizeof text, "de"),
                         "a chunk past the cap should be refused whole");
    INKCELL_TEST_FAIL_IF(strcmp(text, "abc" TEST_EMOJI) != 0,
                         "a refused chunk must leave the existing text intact");
    INKCELL_TEST_FAIL_IF(inkcell_keyboard_insert_text(&layout, text, sizeof text, "\n"),
                         "single-line fields must reject line breaks");
    INKCELL_TEST_FAIL_IF(inkcell_keyboard_insert_text(&layout, text, sizeof text, "\xC0\xAF"),
                         "malformed UTF-8 must not enter the draft");
    INKCELL_TEST_FAIL_IF(strcmp(text, "abc" TEST_EMOJI) != 0,
                         "invalid input must leave the draft intact");
    char replacement[8] = "";
    INKCELL_TEST_FAIL_IF(
        !inkcell_keyboard_insert_text(&layout, replacement, sizeof replacement, "\xEF\xBF\xBD") ||
            strcmp(replacement, "\xEF\xBF\xBD") != 0,
        "a valid replacement character is still valid UTF-8 text");
}

/* Backspace removes what the user sees as one key, not one byte and not one code point. */
INKCELL_TEST_CASE(keyboard_delete_takes_a_whole_cell, unit) {
    const struct inkcell_keyboard_layout layout = bare_layout();
    struct inkcell_keyboard kb;
    inkcell_keyboard_reset(&kb);

    char text[32] = "hi" TEST_EMOJI;
    const enum inkcell_keyboard_result result =
        inkcell_keyboard_key(&kb, &layout, INKCELL_KEY_X, text, sizeof text);
    INKCELL_TEST_FAIL_IF(result != INKCELL_KEYBOARD_CONSUMED, "X is the keyboard's backspace");
    INKCELL_TEST_FAIL_IF(strcmp(text, "hi") != 0, "one press should remove the whole emoji");

    (void)inkcell_keyboard_key(&kb, &layout, INKCELL_KEY_X, text, sizeof text);
    (void)inkcell_keyboard_key(&kb, &layout, INKCELL_KEY_X, text, sizeof text);
    INKCELL_TEST_FAIL_IF(text[0] != '\0', "the field should empty");

    /* An empty field still takes the key: reporting it spare would let a caller act on X and
       navigate away from a keyboard somebody is typing into. */
    INKCELL_TEST_FAIL_IF(inkcell_keyboard_key(&kb, &layout, INKCELL_KEY_X, text, sizeof text) !=
                             INKCELL_KEYBOARD_CONSUMED,
                         "a backspace on an empty field is still the keyboard's key");
}

/*
 * The five verdicts, and the one distinction the whole seam turns on.
 *
 * B leaves with the text intact and the grid's own cancel key discards, because backing out is
 * not the same press as throwing away - B is "back" on every other screen a handheld has.
 * inkcell does not clear the buffer on either: what the field goes back to is the caller's,
 * which is the same reason the buffer lives on its side.
 */
INKCELL_TEST_CASE(keyboard_results_tell_the_presses_apart, unit) {
    const struct inkcell_keyboard_layout layout = bare_layout();
    struct inkcell_keyboard kb;
    char text[32] = "draft";

    inkcell_keyboard_reset(&kb);
    INKCELL_TEST_FAIL_IF(inkcell_keyboard_key(&kb, &layout, INKCELL_KEY_B, text, sizeof text) !=
                             INKCELL_KEYBOARD_DISMISS,
                         "B should dismiss");
    INKCELL_TEST_FAIL_IF(strcmp(text, "draft") != 0, "dismissing should leave the text alone");

    INKCELL_TEST_FAIL_IF(inkcell_keyboard_key(&kb, &layout, INKCELL_KEY_START, text, sizeof text) !=
                             INKCELL_KEYBOARD_SUBMIT,
                         "START should submit");

    kb.row = INKCELL_KB_CHAR_ROWS;
    kb.col = (uint8_t)INKCELL_KB_ACTION_CANCEL;
    INKCELL_TEST_FAIL_IF(inkcell_keyboard_key(&kb, &layout, INKCELL_KEY_A, text, sizeof text) !=
                             INKCELL_KEYBOARD_CANCEL,
                         "the cancel key should cancel");
    INKCELL_TEST_FAIL_IF(strcmp(text, "draft") != 0,
                         "cancelling is the caller's to carry out, not inkcell's");

    kb.col = (uint8_t)INKCELL_KB_ACTION_SUBMIT;
    INKCELL_TEST_FAIL_IF(inkcell_keyboard_key(&kb, &layout, INKCELL_KEY_A, text, sizeof text) !=
                             INKCELL_KEYBOARD_SUBMIT,
                         "the submit key should submit");

    INKCELL_TEST_FAIL_IF(inkcell_keyboard_key(&kb, &layout, INKCELL_KEY_SELECT, text,
                                              sizeof text) != INKCELL_KEYBOARD_IGNORED,
                         "a key the keyboard does not use should be left for the caller");
}

/* A page that is there and cannot be reached and a glyph that is there and cannot be seen are
   one bug to whoever is holding the device, so one call answers for both. */
INKCELL_TEST_CASE(keyboard_layout_drawable_rejects_what_cannot_be_shown, unit) {
    uint32_t bad_page = 0U;
    uint32_t bad_cell = 0U;

    struct inkcell_keyboard_layout good = one_page_layout();
    INKCELL_TEST_FAIL_IF(!inkcell_keyboard_layout_drawable(&good, &bad_page, &bad_cell),
                         "a full page of drawable emoji should pass");

    const struct inkcell_keyboard_layout bare = bare_layout();
    INKCELL_TEST_FAIL_IF(!inkcell_keyboard_layout_drawable(&bare, NULL, NULL),
                         "no emoji layer at all is a layout this grid can show");

    struct inkcell_keyboard_layout over = good;
    over.pages = (uint8_t)(INKCELL_KB_EMOJI_PAGES_MAX + 1U);
    bad_page = 0U;
    INKCELL_TEST_FAIL_IF(inkcell_keyboard_layout_drawable(&over, &bad_page, &bad_cell),
                         "more pages than the ring can address should be refused");
    INKCELL_TEST_FAIL_IF(bad_page != INKCELL_KB_EMOJI_PAGES_MAX,
                         "the first page past the cap should be named");

    struct inkcell_keyboard_layout no_table = good;
    no_table.emoji = NULL;
    INKCELL_TEST_FAIL_IF(inkcell_keyboard_layout_drawable(&no_table, NULL, NULL),
                         "pages with no table behind them should be refused");

    /* One failure per shape, each differing from the good page in exactly one way. */
    const char *const bad_cells[] = {
        "",             /* a keycap that does nothing */
        "x",            /* a letter where a sprite should be */
        TEST_EMOJI "x", /* one cell holding two things */
    };
    for (size_t i = 0U; i < sizeof bad_cells / sizeof bad_cells[0]; ++i) {
        fill_page(test_page, INKCELL_KB_EMOJI_PAGE_CELLS, TEST_EMOJI);
        test_page[7] = bad_cells[i];
        bad_page = 99U;
        bad_cell = 99U;
        char message[96];
        snprintf(message, sizeof message, "bad cell %zu should be refused", i);
        INKCELL_TEST_FAIL_IF(inkcell_keyboard_layout_drawable(&good, &bad_page, &bad_cell),
                             message);
        snprintf(message, sizeof message, "bad cell %zu should be named", i);
        INKCELL_TEST_FAIL_IF(bad_page != 0U || bad_cell != 7U, message);
    }
}

/* The cursor keeps a page index a shorter layout cannot answer for; reading it has to clamp
   rather than walk off the table. */
INKCELL_TEST_CASE(keyboard_cell_clamps_a_stale_page, unit) {
    struct inkcell_keyboard_layout layout = one_page_layout();
    struct inkcell_keyboard kb;
    inkcell_keyboard_reset(&kb);
    kb.layer = (uint8_t)INKCELL_KB_EMOJI;
    kb.emoji_page = 9U; /* a page this layout does not have */

    char scratch[INKCELL_KB_CELL_MAX];
    const char *const cell = inkcell_keyboard_cell(&kb, &layout, 0U, 0U, scratch);
    INKCELL_TEST_FAIL_IF(strcmp(cell, TEST_EMOJI) != 0,
                         "a stale page should read the first one, not past the table");

    /* And with no emoji layer at all there is nothing to read. */
    layout.pages = 0U;
    layout.emoji = NULL;
    INKCELL_TEST_FAIL_IF(inkcell_keyboard_cell(&kb, &layout, 0U, 0U, scratch)[0] != '\0',
                         "an absent emoji layer should draw no key");
}

/*
 * Any int is a step.
 *
 * The signature takes an `int` and says nothing about its range, so a caller passing one is
 * within its rights - and adding the current panel to it before the modulo overflows for a step
 * near INT_MAX, which is undefined behaviour rather than a wrong answer. Reducing the step first
 * keeps both operands small; the landing is asserted as an invariant rather than an arithmetic
 * result, because what the contract owes is a panel that exists.
 */
INKCELL_TEST_CASE(keyboard_panel_step_takes_any_int, unit) {
    fill_page(test_page_two, INKCELL_KB_EMOJI_PAGE_CELLS * 2U, TEST_EMOJI);
    struct inkcell_keyboard_layout layout = one_page_layout();
    layout.emoji = test_page_two;
    layout.pages = 2U;

    const int deltas[] = {INT_MAX, INT_MIN, INT_MAX - 1, INT_MIN + 1, 1000003, -1000003};
    for (size_t i = 0U; i < sizeof deltas / sizeof deltas[0]; ++i) {
        for (unsigned panel = 0U; panel < INKCELL_KB_ASCII_LAYERS + layout.pages; ++panel) {
            struct inkcell_keyboard kb;
            inkcell_keyboard_reset(&kb);
            /* Park the cursor on each panel in turn: the overflow only bites once the index
               being added is non-zero. */
            inkcell_keyboard_panel_step(&kb, &layout, (int)panel);
            inkcell_keyboard_panel_step(&kb, &layout, deltas[i]);

            char message[112];
            snprintf(message, sizeof message, "delta %d from panel %u left layer %u", deltas[i],
                     panel, kb.layer);
            INKCELL_TEST_FAIL_IF(kb.layer >= INKCELL_KB_LAYER_COUNT, message);
            snprintf(message, sizeof message, "delta %d from panel %u left page %u of %u",
                     deltas[i], panel, kb.emoji_page, layout.pages);
            INKCELL_TEST_FAIL_IF(kb.layer == INKCELL_KB_EMOJI && kb.emoji_page >= layout.pages,
                                 message);
        }
    }
}
