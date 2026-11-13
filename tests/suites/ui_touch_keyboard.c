#define _POSIX_C_SOURCE 200809L

/*
 * The keyboard a finger types on: its rows, which key a point is on, and what a press amounts to
 * - each asserted against the functions the drawing and the caller ask, without a panel - and
 * the edits a field makes of those presses (keyboard.h's, at the caret). Then one frame, drawn.
 */

#include "framework/inkcell_test.h"

#include "inkcell/ui/fb_capture.h"
#include "inkcell/ui/font.h"
#include "inkcell/ui/font_ui.h"
#include "inkcell/ui/keyboard.h"
#include "inkcell/ui/touch_keyboard.h"
#include "inkcell/ui/widgets/touch_keyboard.h"
#include "inkwell/base/text.h"

#include <string.h>

/* A phone's keyboard, in pixels: the width of a Pixel 7's panel, and a third of its height. */
static const struct inkcell_tkb_box k_box = {0, 0, 1080, 720};

/* The middle of key `key` of row `row`. */
static void tkb_centre(unsigned layer, unsigned row, unsigned key, int32_t *x, int32_t *y) {
    const struct inkcell_tkb_box b = inkcell_touch_keyboard_key_box(layer, row, key, k_box);
    *x = b.x + b.w / 2;
    *y = b.y + b.h / 2;
}

/* The key on the layer showing whose text, or kind for a NULL text, is `text`/`kind`. */
static bool tkb_find(unsigned layer, enum inkcell_tkb_kind kind, const char *text, unsigned *row,
                     unsigned *key) {
    for (unsigned r = 0U; r < INKCELL_TKB_ROWS; ++r) {
        size_t count = 0U;
        const struct inkcell_tkb_key *keys = inkcell_touch_keyboard_row(layer, r, &count);
        for (unsigned k = 0U; k < count; ++k) {
            if (keys[k].kind == kind &&
                (text == NULL || (keys[k].text != NULL && strcmp(keys[k].text, text) == 0))) {
                *row = r;
                *key = k;
                return true;
            }
        }
    }
    return false;
}

/* A tap - down and up in the middle of the key - at `now`. What the lift amounted to. */
static struct inkcell_tkb_result tkb_tap(struct inkcell_touch_keyboard *kb,
                                         enum inkcell_tkb_kind kind, const char *text,
                                         uint64_t now) {
    unsigned row = 0U;
    unsigned key = 0U;
    if (!tkb_find(kb->layer, kind, text, &row, &key)) {
        struct inkcell_tkb_result none;
        memset(&none, 0, sizeof none);
        return none;
    }
    int32_t x = 0;
    int32_t y = 0;
    tkb_centre(kb->layer, row, key, &x, &y);
    (void)inkcell_touch_keyboard_touch(kb, k_box, INKCELL_TOUCH_DOWN, x, y, now);
    return inkcell_touch_keyboard_touch(kb, k_box, INKCELL_TOUCH_UP, x, y, now + 40U);
}

INKCELL_TEST_CASE(touch_keyboard_rows_fill_the_width, unit) {
    for (unsigned layer = 0U; layer < INKCELL_TKB_LAYER_COUNT; ++layer) {
        for (unsigned row = 0U; row < INKCELL_TKB_ROWS; ++row) {
            size_t count = 0U;
            const struct inkcell_tkb_key *keys = inkcell_touch_keyboard_row(layer, row, &count);
            INKCELL_TEST_FAIL_IF(keys == NULL || count == 0U, "every layer has four rows");
            unsigned units = 0U;
            for (size_t k = 0U; k < count; ++k) {
                units += keys[k].lead + keys[k].units;
            }
            /* The stagger's lead is on the left only: a row with one is ten units short of the
               width on the right by the same amount, centred. */
            const bool staggered = keys[0].lead != 0U;
            INKCELL_TEST_FAIL_IF(units + (staggered ? keys[0].lead : 0U) != INKCELL_TKB_UNITS,
                                 "a row spans the keyboard");
        }
    }
    size_t count = 0U;
    INKCELL_TEST_FAIL_IF(inkcell_touch_keyboard_row(INKCELL_TKB_LAYER_COUNT, 0U, &count) != NULL ||
                             count != 0U,
                         "no row past the last layer");
}

/* A key whose character the font has no glyph for is a blank keycap that types something no
   field will show. */
INKCELL_TEST_CASE(touch_keyboard_every_key_is_drawable, unit) {
    for (unsigned layer = 0U; layer < INKCELL_TKB_LAYER_COUNT; ++layer) {
        for (unsigned row = 0U; row < INKCELL_TKB_ROWS; ++row) {
            size_t count = 0U;
            const struct inkcell_tkb_key *keys = inkcell_touch_keyboard_row(layer, row, &count);
            for (size_t k = 0U; k < count; ++k) {
                if (keys[k].text == NULL) {
                    continue;
                }
                for (const char *p = keys[k].text; *p != '\0';) {
                    uint32_t cp = 0U;
                    const size_t step = inkwell_text_utf8_next(p, &cp);
                    INKCELL_TEST_FAIL_IF(step == 0U, "a key's text is UTF-8");
                    INKCELL_TEST_FAIL_IF(!inkcell_font_has_glyph(inkcell_font_ui(), cp),
                                         keys[k].text);
                    p += step;
                }
                INKCELL_TEST_FAIL_IF(strlen(keys[k].text) >= INKCELL_TKB_TEXT_MAX,
                                     "a key's text fits a result");
            }
        }
    }
}

INKCELL_TEST_CASE(touch_keyboard_hit_finds_the_nearest_key, unit) {
    for (unsigned layer = 0U; layer < INKCELL_TKB_LAYER_COUNT; ++layer) {
        for (unsigned row = 0U; row < INKCELL_TKB_ROWS; ++row) {
            size_t count = 0U;
            (void)inkcell_touch_keyboard_row(layer, row, &count);
            for (unsigned k = 0U; k < count; ++k) {
                int32_t x = 0;
                int32_t y = 0;
                tkb_centre(layer, row, k, &x, &y);
                unsigned hr = 99U;
                unsigned hk = 99U;
                INKCELL_TEST_FAIL_IF(!inkcell_touch_keyboard_hit(layer, k_box, x, y, &hr, &hk) ||
                                         hr != row || hk != k,
                                     "a key's middle is that key");
            }
        }
    }
    unsigned row = 0U;
    unsigned key = 0U;
    /* The stagger before "a" is "a"'s, and the room after "l" is "l"'s. */
    INKCELL_TEST_FAIL_IF(
        !inkcell_touch_keyboard_hit(INKCELL_TKB_LETTERS, k_box, 2, 270, &row, &key) || row != 1U ||
            key != 0U,
        "the lead is the first key's");
    INKCELL_TEST_FAIL_IF(
        !inkcell_touch_keyboard_hit(INKCELL_TKB_LETTERS, k_box, 1078, 270, &row, &key) ||
            row != 1U || key != 8U,
        "the tail is the last key's");
    INKCELL_TEST_FAIL_IF(
        inkcell_touch_keyboard_hit(INKCELL_TKB_LETTERS, k_box, 10, 720, &row, &key),
        "below the box is no key");
    INKCELL_TEST_FAIL_IF(inkcell_touch_keyboard_hit(INKCELL_TKB_LETTERS, k_box, -1, 10, &row, &key),
                         "left of the box is no key");
}

INKCELL_TEST_CASE(touch_keyboard_types_the_key_it_lifts_on, unit) {
    struct inkcell_touch_keyboard kb;
    inkcell_touch_keyboard_reset(&kb);
    struct inkcell_tkb_result r = tkb_tap(&kb, INKCELL_TKB_CHAR, "q", 1000U);
    INKCELL_TEST_FAIL_IF(r.kind != INKCELL_TKB_TEXT || strcmp(r.text, "q") != 0, "q types q");

    /* Down on g, a slide onto h, up there: h, and the key lit followed the finger. */
    unsigned row = 0U;
    unsigned g = 0U;
    unsigned h = 0U;
    (void)tkb_find(INKCELL_TKB_LETTERS, INKCELL_TKB_CHAR, "g", &row, &g);
    (void)tkb_find(INKCELL_TKB_LETTERS, INKCELL_TKB_CHAR, "h", &row, &h);
    int32_t gx = 0;
    int32_t gy = 0;
    int32_t hx = 0;
    int32_t hy = 0;
    tkb_centre(INKCELL_TKB_LETTERS, row, g, &gx, &gy);
    tkb_centre(INKCELL_TKB_LETTERS, row, h, &hx, &hy);
    r = inkcell_touch_keyboard_touch(&kb, k_box, INKCELL_TOUCH_DOWN, gx, gy, 2000U);
    INKCELL_TEST_FAIL_IF(r.kind != INKCELL_TKB_REDRAW || kb.key != (int8_t)g, "g is lit");
    r = inkcell_touch_keyboard_touch(&kb, k_box, INKCELL_TOUCH_MOVE, hx, hy, 2020U);
    INKCELL_TEST_FAIL_IF(r.kind != INKCELL_TKB_REDRAW || kb.key != (int8_t)h, "h is lit");
    r = inkcell_touch_keyboard_touch(&kb, k_box, INKCELL_TOUCH_MOVE, hx + 1, hy, 2030U);
    INKCELL_TEST_FAIL_IF(r.kind != INKCELL_TKB_NOTHING, "a move on the same key changes nothing");
    r = inkcell_touch_keyboard_touch(&kb, k_box, INKCELL_TOUCH_UP, hx, hy, 2040U);
    INKCELL_TEST_FAIL_IF(r.kind != INKCELL_TKB_TEXT || strcmp(r.text, "h") != 0, "h is typed");
    INKCELL_TEST_FAIL_IF(kb.key != -1 || kb.row != -1, "nothing is lit after the lift");

    /* A contact the device lost types nothing. */
    (void)inkcell_touch_keyboard_touch(&kb, k_box, INKCELL_TOUCH_DOWN, gx, gy, 3000U);
    r = inkcell_touch_keyboard_touch(&kb, k_box, INKCELL_TOUCH_CANCEL, gx, gy, 3010U);
    INKCELL_TEST_FAIL_IF(r.kind != INKCELL_TKB_REDRAW, "a cancel only redraws");
    r = inkcell_touch_keyboard_touch(&kb, k_box, INKCELL_TOUCH_UP, gx, gy, 3020U);
    INKCELL_TEST_FAIL_IF(r.kind != INKCELL_TKB_NOTHING, "and the lift after it is nothing");

    INKCELL_TEST_FAIL_IF(tkb_tap(&kb, INKCELL_TKB_SPACE, NULL, 4000U).kind != INKCELL_TKB_TEXT,
                         "space types");
    INKCELL_TEST_FAIL_IF(tkb_tap(&kb, INKCELL_TKB_ENTER, NULL, 4100U).kind != INKCELL_TKB_SUBMIT,
                         "enter submits");
    INKCELL_TEST_FAIL_IF(tkb_tap(&kb, INKCELL_TKB_HIDE, NULL, 4200U).kind != INKCELL_TKB_AWAY,
                         "hide puts it away");
}

INKCELL_TEST_CASE(touch_keyboard_shift_is_one_capital_or_held, unit) {
    struct inkcell_touch_keyboard kb;
    inkcell_touch_keyboard_reset(&kb);
    (void)tkb_tap(&kb, INKCELL_TKB_SHIFT, NULL, 1000U);
    INKCELL_TEST_FAIL_IF(kb.shift != INKCELL_TKB_SHIFT_ONCE, "shift is one capital");
    struct inkcell_tkb_result r = tkb_tap(&kb, INKCELL_TKB_CHAR, "a", 2000U);
    INKCELL_TEST_FAIL_IF(strcmp(r.text, "A") != 0, "the capital");
    r = tkb_tap(&kb, INKCELL_TKB_CHAR, "a", 2100U);
    INKCELL_TEST_FAIL_IF(strcmp(r.text, "a") != 0, "then lower case");

    /* Twice quickly: held on, through letters, until shift again. */
    (void)tkb_tap(&kb, INKCELL_TKB_SHIFT, NULL, 3000U);
    (void)tkb_tap(&kb, INKCELL_TKB_SHIFT, NULL, 3200U);
    INKCELL_TEST_FAIL_IF(kb.shift != INKCELL_TKB_SHIFT_LOCKED, "twice quickly holds it");
    INKCELL_TEST_FAIL_IF(strcmp(tkb_tap(&kb, INKCELL_TKB_CHAR, "b", 3400U).text, "B") != 0 ||
                             strcmp(tkb_tap(&kb, INKCELL_TKB_CHAR, "c", 3500U).text, "C") != 0,
                         "held capitals");
    (void)tkb_tap(&kb, INKCELL_TKB_SHIFT, NULL, 4000U);
    INKCELL_TEST_FAIL_IF(kb.shift != INKCELL_TKB_SHIFT_OFF, "shift again lets go");

    /* Twice slowly is on, then off. */
    (void)tkb_tap(&kb, INKCELL_TKB_SHIFT, NULL, 5000U);
    (void)tkb_tap(&kb, INKCELL_TKB_SHIFT, NULL, 6000U);
    INKCELL_TEST_FAIL_IF(kb.shift != INKCELL_TKB_SHIFT_OFF, "twice slowly is on and off");
}

INKCELL_TEST_CASE(touch_keyboard_field_capitalizes_a_sentence, unit) {
    struct inkcell_touch_keyboard kb;
    inkcell_touch_keyboard_reset(&kb);
    INKCELL_TEST_FAIL_IF(!inkcell_touch_keyboard_capitalize(&kb, true) ||
                             kb.shift != INKCELL_TKB_SHIFT_ONCE,
                         "a sentence starts with a capital");
    INKCELL_TEST_FAIL_IF(!inkcell_touch_keyboard_capitalize(&kb, false) ||
                             kb.shift != INKCELL_TKB_SHIFT_OFF,
                         "the field may take its own capital back");
    (void)tkb_tap(&kb, INKCELL_TKB_SHIFT, NULL, 1000U);
    INKCELL_TEST_FAIL_IF(inkcell_touch_keyboard_capitalize(&kb, false) ||
                             kb.shift != INKCELL_TKB_SHIFT_ONCE,
                         "but never one the person pressed");
    /* A field's capital and a quick shift after it is not a double press: the person pressed once,
       and that took the capital away. */
    inkcell_touch_keyboard_reset(&kb);
    (void)inkcell_touch_keyboard_capitalize(&kb, true);
    (void)tkb_tap(&kb, INKCELL_TKB_SHIFT, NULL, 2000U);
    INKCELL_TEST_FAIL_IF(kb.shift != INKCELL_TKB_SHIFT_OFF, "shift over a field's capital is off");
}

INKCELL_TEST_CASE(touch_keyboard_delete_repeats_while_held, unit) {
    struct inkcell_touch_keyboard kb;
    inkcell_touch_keyboard_reset(&kb);
    unsigned row = 0U;
    unsigned key = 0U;
    (void)tkb_find(INKCELL_TKB_LETTERS, INKCELL_TKB_DELETE, NULL, &row, &key);
    int32_t x = 0;
    int32_t y = 0;
    tkb_centre(INKCELL_TKB_LETTERS, row, key, &x, &y);
    struct inkcell_tkb_result r =
        inkcell_touch_keyboard_touch(&kb, k_box, INKCELL_TOUCH_DOWN, x, y, 1000U);
    INKCELL_TEST_FAIL_IF(r.kind != INKCELL_TKB_BACK, "delete deletes going down");
    INKCELL_TEST_FAIL_IF(inkcell_touch_keyboard_due(&kb) != 1000U + INKCELL_TKB_REPEAT_DELAY_MS,
                         "and wants a tick after the delay");
    INKCELL_TEST_FAIL_IF(inkcell_touch_keyboard_tick(&kb, 1100U).kind != INKCELL_TKB_NOTHING,
                         "not before it");
    INKCELL_TEST_FAIL_IF(inkcell_touch_keyboard_tick(&kb, 1400U).kind != INKCELL_TKB_BACK ||
                             inkcell_touch_keyboard_due(&kb) != 1400U + INKCELL_TKB_REPEAT_MS,
                         "then again, quicker");
    r = inkcell_touch_keyboard_touch(&kb, k_box, INKCELL_TOUCH_UP, x, y, 1420U);
    INKCELL_TEST_FAIL_IF(r.kind != INKCELL_TKB_REDRAW || inkcell_touch_keyboard_due(&kb) != 0U,
                         "the lift deletes nothing more and stops");

    /* Down on delete and off it onto a letter: the repeat stops and the lift types nothing. */
    (void)inkcell_touch_keyboard_touch(&kb, k_box, INKCELL_TOUCH_DOWN, x, y, 2000U);
    int32_t mx = 0;
    int32_t my = 0;
    tkb_centre(INKCELL_TKB_LETTERS, row, key - 1U, &mx, &my);
    (void)inkcell_touch_keyboard_touch(&kb, k_box, INKCELL_TOUCH_MOVE, mx, my, 2050U);
    INKCELL_TEST_FAIL_IF(inkcell_touch_keyboard_due(&kb) != 0U, "sliding off stops the repeat");
    r = inkcell_touch_keyboard_touch(&kb, k_box, INKCELL_TOUCH_UP, mx, my, 2100U);
    INKCELL_TEST_FAIL_IF(r.kind == INKCELL_TKB_TEXT, "and the lift is not a letter");
}

INKCELL_TEST_CASE(touch_keyboard_walks_its_layers, unit) {
    struct inkcell_touch_keyboard kb;
    inkcell_touch_keyboard_reset(&kb);
    (void)tkb_tap(&kb, INKCELL_TKB_SHIFT, NULL, 500U);
    (void)tkb_tap(&kb, INKCELL_TKB_LAYER, "?123", 1000U);
    INKCELL_TEST_FAIL_IF(kb.layer != INKCELL_TKB_SYMBOLS || kb.shift != INKCELL_TKB_SHIFT_OFF,
                         "?123 is the symbols, unshifted");
    INKCELL_TEST_FAIL_IF(strcmp(tkb_tap(&kb, INKCELL_TKB_CHAR, "1", 1100U).text, "1") != 0,
                         "a digit");
    (void)tkb_tap(&kb, INKCELL_TKB_LAYER, "=\\<", 1200U);
    INKCELL_TEST_FAIL_IF(kb.layer != INKCELL_TKB_MORE, "past them, the rest");
    INKCELL_TEST_FAIL_IF(strcmp(tkb_tap(&kb, INKCELL_TKB_CHAR, "€", 1300U).text, "€") != 0,
                         "a character of more than a byte");
    INKCELL_TEST_FAIL_IF(inkcell_touch_keyboard_capitalize(&kb, true),
                         "no capital off the letters");
    (void)tkb_tap(&kb, INKCELL_TKB_LAYER, "ABC", 1400U);
    INKCELL_TEST_FAIL_IF(kb.layer != INKCELL_TKB_LETTERS, "ABC is home");
}

/* The edits a field makes of those presses: keyboard.h's, at the caret. */
INKCELL_TEST_CASE(touch_keyboard_edits_at_the_caret, unit) {
    struct inkcell_keyboard caret;
    inkcell_keyboard_reset(&caret);
    char text[16] = "héllo";
    INKCELL_TEST_FAIL_IF(!inkcell_keyboard_delete_at_caret(&caret, text) ||
                             strcmp(text, "héll") != 0,
                         "delete takes the last character");
    caret.caret_back = 2U; /* before "ll" */
    INKCELL_TEST_FAIL_IF(!inkcell_keyboard_delete_at_caret(&caret, text) ||
                             strcmp(text, "hll") != 0,
                         "a two-byte character is one delete");
    INKCELL_TEST_FAIL_IF(
        !inkcell_keyboard_insert_text_at_caret(&caret, NULL, text, sizeof text, "e") ||
            strcmp(text, "hell") != 0,
        "typing lands at the caret");
    char empty[4] = "";
    INKCELL_TEST_FAIL_IF(inkcell_keyboard_delete_at_caret(&caret, empty),
                         "nothing to delete is no change");
}

INKCELL_TEST_CASE(touch_keyboard_draws_and_lights_the_key, unit) {
    struct inkcell_capture *capture = NULL;
    INKCELL_TEST_FAIL_IF(inkcell_capture_open(&capture, 1080U, 720U, INKCELL_SCALE(4)) < 0,
                         "the capture should open");
    struct inkcell_draw_state *state = inkcell_capture_state(capture);
    struct inkcell_touch_keyboard kb;
    inkcell_touch_keyboard_reset(&kb);
    const struct inkcell_fb_rect box = {0, 0, 1080, 720};
    const struct inkcell_fb_touch_keyboard draw = {.keyboard = &kb, .enter_label = "Done"};
    inkcell_fb_draw_touch_keyboard(state, box, &draw);
    uint32_t w = 0U;
    uint32_t h = 0U;
    size_t stride = 0U;
    const uint8_t *px = inkcell_capture_pixels(capture, &w, &h, &stride);
    static uint8_t before[1080U * 720U * 4U];
    INKCELL_TEST_FAIL_IF_CLEANUP(px == NULL || stride * h > sizeof before,
                                 inkcell_capture_close(capture), "a page");
    memcpy(before, px, stride * h);

    /* "h", on the second row, lit: its key and the bubble over it - in the row above - change. */
    unsigned row = 0U;
    unsigned key = 0U;
    (void)tkb_find(INKCELL_TKB_LETTERS, INKCELL_TKB_CHAR, "h", &row, &key);
    int32_t x = 0;
    int32_t y = 0;
    tkb_centre(INKCELL_TKB_LETTERS, row, key, &x, &y);
    (void)inkcell_touch_keyboard_touch(&kb, k_box, INKCELL_TOUCH_DOWN, x, y, 1000U);
    inkcell_fb_draw_touch_keyboard(state, box, &draw);
    px = inkcell_capture_pixels(capture, &w, &h, &stride);
    const struct inkcell_tkb_box key_box = inkcell_touch_keyboard_key_box(0U, row, key, k_box);
    const size_t above = (size_t)(key_box.y - key_box.h / 2) * stride + (size_t)x * 4U;
    INKCELL_TEST_FAIL_IF_CLEANUP(memcmp(&px[above], &before[above], 4U) == 0,
                                 inkcell_capture_close(capture),
                                 "the lit character rises over the row above");
    inkcell_capture_close(capture);
}
