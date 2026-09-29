#define _POSIX_C_SOURCE 200809L

/*
 * The shapes words are written in - a capsule, a chat bubble - and the one promise they share:
 * nothing written in one is painted outside it.
 *
 * Checked by looking at the pixels, on every theme, because the way it broke was a property of
 * a *face*: the 5x7 font leaves a row of the line empty under its letters and the UI face
 * reaches the bottom of its cell, so a box measured for one put the tail of every "y" of the
 * other below it. A case on one theme would hold whichever face that theme happens to name.
 *
 * The page is cleared to a colour no theme uses, so "untouched" is a comparison rather than a
 * judgement about what the ground looks like.
 */

#include "framework/inkcell_test.h"

#include "inkcell/ui/fb_capture.h"
#include "inkcell/ui/widgets/bubble.h"
#include "inkcell/ui/widgets/button.h"
#include "inkcell/ui/widgets/chrome.h"
#include "inkcell/ui/widgets/item.h"
#include "inkcell/ui/widgets/list.h"

#include <stdint.h>
#include <string.h>

/* Every letter that reaches the bottom of the cell, and two that reach the top. */
#define BUBBLE_DESCENDERS "gypsy jog, fljq"

static const struct inkcell_rgb k_sentinel = {1U, 2U, 3U};

struct bubble_page {
    struct inkcell_capture *capture;
    struct inkcell_draw_state *state;
    const uint8_t *pixels;
    size_t stride;
    int w, h;
};

static bool bubble_open(struct bubble_page *page, const struct inkcell_theme *theme) {
    page->capture = NULL;
    if (inkcell_capture_open(&page->capture, INKCELL_CAPTURE_WIDTH, INKCELL_CAPTURE_HEIGHT,
                             INKCELL_SCALE(4)) < 0) {
        return false;
    }
    inkcell_capture_set_theme(page->capture, theme);
    page->state = inkcell_capture_state(page->capture);
    uint32_t w = 0U;
    uint32_t h = 0U;
    page->pixels = inkcell_capture_pixels(page->capture, &w, &h, &page->stride);
    if (page->pixels == NULL) {
        inkcell_capture_close(page->capture);
        return false;
    }
    page->w = (int)w;
    page->h = (int)h;
    inkcell_fb_clear(page->state, k_sentinel);
    return true;
}

/* Whether (x, y) is still the colour the page was cleared to. B,G,R,X per pixel. */
static bool bubble_untouched(const struct bubble_page *page, int x, int y) {
    const uint8_t *px = page->pixels + (size_t)y * page->stride + (size_t)x * 4U;
    return px[0] == k_sentinel.b && px[1] == k_sentinel.g && px[2] == k_sentinel.r;
}

/* Whether a band of whole rows, [top, bottom), is still the ground from edge to edge. */
static bool bubble_band_untouched(const struct bubble_page *page, int top, int bottom) {
    for (int y = top; y < bottom; ++y) {
        for (int x = 0; x < page->w; ++x) {
            if (y >= 0 && y < page->h && !bubble_untouched(page, x, y)) {
                return false;
            }
        }
    }
    return true;
}

/* Whether everything within `margin` of `box` but outside it is still the ground. */
static bool bubble_outside_untouched(const struct bubble_page *page,
                                     const struct inkcell_fb_rect *box, int margin) {
    for (int y = box->y - margin; y < box->y + box->h + margin; ++y) {
        for (int x = box->x - margin; x < box->x + box->w + margin; ++x) {
            const bool inside =
                x >= box->x && x < box->x + box->w && y >= box->y && y < box->y + box->h;
            if (!inside && !bubble_untouched(page, x, y)) {
                return false;
            }
        }
    }
    return true;
}

/*
 * A capsule holds its own words: the neutral chip, a family chip and a badge, each written with
 * the letters that reach the bottom and the top of the cell, leave nothing outside the box
 * inkcell_fb_capsule_box() gave them.
 */
INKCELL_TEST_CASE(capsule_holds_its_descenders_on_every_theme, unit) {
    for (size_t t = 0U; t < inkcell_theme_count(); ++t) {
        struct bubble_page page;
        INKCELL_TEST_FAIL_IF(!bubble_open(&page, inkcell_theme_at(t)), "the capture should open");
        const int scale = page.state->scale;
        const int margin = inkcell_fb_line_adv(page.state, scale);

        const struct inkcell_fb_rect neutral =
            inkcell_fb_capsule_box(page.state, margin, 2 * margin, BUBBLE_DESCENDERS, scale);
        inkcell_fb_draw_state_chip(page.state, &neutral, 2 * margin, BUBBLE_DESCENDERS,
                                   INKCELL_TONE_NORMAL, INKCELL_COLOR_SURFACE,
                                   inkcell_fb_color(page.state, INKCELL_COLOR_TEXT), scale);
        const struct inkcell_fb_rect tonal =
            inkcell_fb_capsule_box(page.state, margin, 5 * margin, BUBBLE_DESCENDERS, scale);
        inkcell_fb_draw_state_chip(page.state, &tonal, 5 * margin, BUBBLE_DESCENDERS,
                                   INKCELL_TONE_TERTIARY, INKCELL_COLOR_SURFACE,
                                   inkcell_fb_color(page.state, INKCELL_COLOR_TEXT), scale);
        const struct inkcell_fb_rect badge =
            inkcell_fb_capsule_box(page.state, margin, 8 * margin, BUBBLE_DESCENDERS, scale);
        inkcell_fb_draw_badge(page.state, &badge, 8 * margin, BUBBLE_DESCENDERS,
                              INKCELL_FAMILY_PRIMARY, scale);

        const bool held = bubble_outside_untouched(&page, &neutral, margin) &&
                          bubble_outside_untouched(&page, &tonal, margin) &&
                          bubble_outside_untouched(&page, &badge, margin);
        inkcell_capture_close(page.capture);
        INKCELL_TEST_FAIL_IF(!held, "a capsule's words should stay inside the capsule");
    }
    record_success(test_name);
}

/*
 * The neutral chip is a filled tag, not a ring round plain words: the middle of it, between two
 * words, is a fill rather than whatever it was drawn on.
 */
INKCELL_TEST_CASE(capsule_neutral_chip_is_filled, unit) {
    for (size_t t = 0U; t < inkcell_theme_count(); ++t) {
        struct bubble_page page;
        INKCELL_TEST_FAIL_IF(!bubble_open(&page, inkcell_theme_at(t)), "the capture should open");
        const int scale = page.state->scale;
        const int x = inkcell_fb_line_adv(page.state, scale);
        const struct inkcell_fb_rect box =
            inkcell_fb_capsule_box(page.state, x, 2 * x, "a b", scale);
        inkcell_fb_draw_state_chip(page.state, &box, 2 * x, "a b", INKCELL_TONE_NORMAL,
                                   INKCELL_COLOR_SURFACE,
                                   inkcell_fb_color(page.state, INKCELL_COLOR_TEXT), scale);
        /* Inside the ring, just under the top edge: no glyph reaches there. */
        const bool filled = !bubble_untouched(&page, box.x + box.w / 2,
                                              box.y + 2 * inkcell_fb_edge(page.state) + 1);
        inkcell_capture_close(page.capture);
        INKCELL_TEST_FAIL_IF(!filled, "a neutral chip should be filled, not only outlined");
    }
    record_success(test_name);
}

/*
 * A bubble paints nothing in the step of ground it leaves under itself - which is where its last
 * line's descenders used to land - nor anywhere below it.
 */
INKCELL_TEST_CASE(bubble_keeps_its_last_line_inside, unit) {
    const struct inkcell_fb_bubble bubbles[] = {
        {.text = BUBBLE_DESCENDERS},
        {.name = "Jgy", .text = BUBBLE_DESCENDERS, .meta = {.clock = "19:07"}},
        {.text =
             BUBBLE_DESCENDERS " " BUBBLE_DESCENDERS " " BUBBLE_DESCENDERS " " BUBBLE_DESCENDERS,
         .note = "no public key for that node, gy",
         .outbound = true,
         .failed = true,
         .meta = {.clock = "19:12", .relay = "via gypsy"}},
        {.separator = "Yesterday", .quote = "quoting gy", .text = "jog"},
    };
    for (size_t t = 0U; t < inkcell_theme_count(); ++t) {
        for (size_t i = 0U; i < sizeof bubbles / sizeof bubbles[0]; ++i) {
            struct bubble_page page;
            INKCELL_TEST_FAIL_IF(!bubble_open(&page, inkcell_theme_at(t)),
                                 "the capture should open");
            const struct inkcell_fb_layout layout = inkcell_fb_layout_begin(page.state, true, true);
            const int step = inkcell_step_px(page.state->scale);
            const int y = layout.body_y + layout.line;
            const int height = inkcell_fb_bubble_height(page.state, &layout, &bubbles[i]);
            inkcell_fb_draw_bubble(page.state, &layout, y, &bubbles[i]);
            /* The box starts a step above `y` - the room an accent on the first line hangs in -
               so the band above that is ground too. At the foot, the extent ends with two steps
               of ground and then the step the next box starts in, so everything from three
               steps short of the extent down is ground. */
            const bool above = bubble_band_untouched(&page, 0, y - step);
            const bool below = bubble_band_untouched(&page, y + height - 3 * step, page.h);
            inkcell_capture_close(page.capture);
            INKCELL_TEST_FAIL_IF(!above, "a bubble should paint nothing above its box");
            INKCELL_TEST_FAIL_IF(!below, "a bubble should paint nothing in the gap under it");
        }
    }
    record_success(test_name);
}

/*
 * A line fitted to a width measures no wider than it once drawn, and says it was cut. Wide
 * capitals are the case: their nominal cells undercount them, which is how a quoted line
 * fitted by counting cells ran out of its bubble.
 */
INKCELL_TEST_CASE(text_fit_cuts_by_pixels_and_marks_the_cut, unit) {
    struct bubble_page page;
    INKCELL_TEST_FAIL_IF(!bubble_open(&page, NULL), "the capture should open");
    char line[64] = "WWWWWWWWWWWWWWWWWWWW MMMMMMMM";
    const int width = 8 * inkcell_fb_char_adv(page.state, page.state->scale);
    const size_t kept = inkcell_fb_text_fit(page.state, line, sizeof line, width, NULL);
    const int drawn = inkcell_fb_text_width(page.state, line, page.state->scale);

    char whole[16] = "gy";
    const size_t untouched = inkcell_fb_text_fit(page.state, whole, sizeof whole, width, NULL);

    /* A buffer with no room for the mark still gets a line that fits, just unmarked. */
    char tight[4] = "WWW";
    const int two = inkcell_fb_text_width(page.state, "WW", page.state->scale);
    (void)inkcell_fb_text_fit(page.state, tight, sizeof tight, two, NULL);
    const int tight_w = inkcell_fb_text_width(page.state, tight, page.state->scale);
    inkcell_capture_close(page.capture);

    INKCELL_TEST_FAIL_IF(drawn > width, "a fitted line should measure within its width");
    INKCELL_TEST_FAIL_IF(kept < 3U || strcmp(&line[kept - 3U], "\xE2\x80\xA6") != 0,
                         "a cut line should end in an ellipsis");
    INKCELL_TEST_FAIL_IF(untouched != 2U || strcmp(whole, "gy") != 0,
                         "a line that fits should be left alone");
    INKCELL_TEST_FAIL_IF(tight_w > two, "a line with no room for the mark should still fit");
    record_success(test_name);
}

/*
 * A state chip in a row's value column stays inside the row's own line: nothing it paints is
 * above the line or reaches the next one. A capsule a whole line tall met the rows either side
 * and, on a card's last row, sat on the card's border.
 */
INKCELL_TEST_CASE(capsule_value_chip_stays_inside_its_row, unit) {
    for (size_t t = 0U; t < inkcell_theme_count(); ++t) {
        struct bubble_page page;
        INKCELL_TEST_FAIL_IF(!bubble_open(&page, inkcell_theme_at(t)), "the capture should open");
        struct inkcell_fb_layout layout = inkcell_fb_layout_begin(page.state, false, false);
        /* The cursor on the second row, which is never drawn: the first is a row at rest. */
        struct inkcell_fb_list list = inkcell_fb_list_begin(&layout, 2U, 1U);
        const int step = inkcell_step_px(page.state->scale);
        const int top = list.y - step;
        const int bottom = top + list.line;
        uint32_t index = 0U;
        while (inkcell_fb_list_next(&list, &index)) {
            if (index != 0U) {
                continue;
            }
            const struct inkcell_fb_list_item item = {.label = "Via",
                                                      .label_cols = 6U,
                                                      .value = BUBBLE_DESCENDERS,
                                                      .value_chip = true,
                                                      .tone = INKCELL_TONE_NORMAL};
            inkcell_fb_list_item(page.state, &list, index, &item);
        }
        const bool inside =
            bubble_band_untouched(&page, 0, top) && bubble_band_untouched(&page, bottom, page.h);
        inkcell_capture_close(page.capture);
        INKCELL_TEST_FAIL_IF(!inside, "a row's chip should stay inside the row's line");
    }
    record_success(test_name);
}

/* How many pixels in the band [top, bottom) are exactly `color`, and which non-ground colour
   covers most of it - the bubble's fill, on a band a bubble spans edge to edge. */
static size_t bubble_band_count(const struct bubble_page *page, int top, int bottom,
                                struct inkcell_rgb color) {
    size_t count = 0U;
    for (int y = top; y < bottom; ++y) {
        for (int x = 0; x < page->w; ++x) {
            const uint8_t *px = page->pixels + (size_t)y * page->stride + (size_t)x * 4U;
            count += (px[0] == color.b && px[1] == color.g && px[2] == color.r) ? 1U : 0U;
        }
    }
    return count;
}

static struct inkcell_rgb bubble_band_fill(const struct bubble_page *page, int top, int bottom) {
    struct inkcell_rgb seen[32];
    size_t counts[32] = {0};
    size_t distinct = 0U;
    for (int y = top; y < bottom; ++y) {
        for (int x = 0; x < page->w; ++x) {
            if (bubble_untouched(page, x, y)) {
                continue;
            }
            const uint8_t *px = page->pixels + (size_t)y * page->stride + (size_t)x * 4U;
            const struct inkcell_rgb here = {px[2], px[1], px[0]};
            size_t i = 0U;
            while (i < distinct &&
                   (seen[i].r != here.r || seen[i].g != here.g || seen[i].b != here.b)) {
                ++i;
            }
            if (i == distinct) {
                if (distinct == 32U) {
                    continue;
                }
                seen[distinct++] = here;
            }
            counts[i] += 1U;
        }
    }
    size_t best = 0U;
    for (size_t i = 1U; i < distinct; ++i) {
        best = counts[i] > counts[best] ? i : best;
    }
    return distinct > 0U ? seen[best] : k_sentinel;
}

/*
 * A sender line asked to take its sender's avatar tint takes it only where it reads - body-text
 * contrast against the bubble, or the primary's own where that is lower, at rest and under the
 * cursor alike - and makes the same choice in
 * both, so a name does not change colour as the cursor crosses it. And the tints do vary on the
 * default theme, or the option would be a very roundabout way of drawing the primary.
 */
INKCELL_TEST_CASE(bubble_sender_tint_reads_and_holds_under_the_cursor, unit) {
    for (size_t t = 0U; t < inkcell_theme_count(); ++t) {
        const struct inkcell_theme *theme = inkcell_theme_at(t);
        size_t tinted_seeds = 0U;
        for (uint32_t seed = 0U; seed < 48U; ++seed) {
            const struct inkcell_rgb tint = inkcell_theme_avatar(theme, seed);
            /* A tint that *is* the primary is indistinguishable from the fallback, and the
               focus ring is drawn in the primary too - there is no choice here to look at. */
            const struct inkcell_rgb primary = theme->colors[INKCELL_COLOR_PRIMARY];
            if (tint.r == primary.r && tint.g == primary.g && tint.b == primary.b) {
                continue;
            }
            bool shown[2] = {false, false};
            for (int focused = 0; focused < 2; ++focused) {
                struct bubble_page page;
                INKCELL_TEST_FAIL_IF(!bubble_open(&page, theme), "the capture should open");
                const struct inkcell_fb_layout layout =
                    inkcell_fb_layout_begin(page.state, true, true);
                const int y = layout.body_y + layout.line;
                const struct inkcell_fb_bubble bubble = {.name = "WMWMWM",
                                                         .text = "x",
                                                         .focused = focused != 0,
                                                         .name_tinted = true,
                                                         .name_seed = seed};
                inkcell_fb_draw_bubble(page.state, &layout, y, &bubble);
                const int bottom = y + layout.line - inkcell_step_px(page.state->scale);
                const struct inkcell_rgb fill = bubble_band_fill(&page, y, bottom);
                shown[focused] = bubble_band_count(&page, y, bottom, tint) > 0U;
                inkcell_capture_close(page.capture);
                const double primary_reads = inkcell_theme_contrast(primary, fill);
                const double floor = primary_reads < 4.5 ? primary_reads : 4.5;
                INKCELL_TEST_FAIL_IF(shown[focused] && inkcell_theme_contrast(tint, fill) < floor,
                                     "a sender tint should never read worse than the primary");
            }
            INKCELL_TEST_FAIL_IF(shown[0] != shown[1],
                                 "a sender line should keep its colour under the cursor");
            tinted_seeds += shown[0] ? 1U : 0U;
        }
        /* The high-contrast theme's two tints are its primary and its ink; every other theme
           offers a real spread, and a rule that let none of it through would be decoration. */
        if (theme->avatar_count > 2U) {
            INKCELL_TEST_FAIL_IF(tinted_seeds == 0U, "a theme should tint at least some senders");
        }
    }
    record_success(test_name);
}
