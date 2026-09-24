#define _POSIX_C_SOURCE 200809L

/*
 * The type scale: the roles, and the three facets that are not a size.
 *
 * The scale used to be three roles and two arrays, and what held it together was that a caller
 * asked for a size and drew at it. It is seven roles and one table now, and each role carries a
 * tracking and a line height as well - which means the properties worth pinning are no longer
 * about one number. They are:
 *
 *   - the *ordering*, because a vocabulary in which a caption can come out larger than a label
 *     is a vocabulary that says nothing;
 *   - that measuring and drawing agree, because tracking and tabular figures both change a
 *     width, and a line measured in one style and drawn in another is a line that overruns the
 *     box its own layout reserved for it;
 *   - and that a digit in a tabular run steps the same distance whichever digit it is, which is
 *     the whole of what the flag is for.
 *
 * The golden sheet shows what the scale *looks* like, and shows it better than any assertion
 * here could. These are the facts a picture cannot check.
 */

#include "framework/inkcell_test.h"

#include "inkcell/ui/fb_capture.h"
#include "inkcell/ui/fb_draw.h"
#include "inkcell/ui/theme.h"

#include <stdint.h>
#include <string.h>

#define TYPE_PANEL_W 480U
#define TYPE_PANEL_H 320U

/* The roles in the order the vocabulary puts them, largest first. The enum is declared in this
   order on purpose - it is what lets the ordering case walk it as a scale rather than as a set
   of unrelated names - so the case restates it and fails if the two ever drift apart. */
static const enum inkcell_type k_descending[] = {
    INKCELL_TYPE_DISPLAY,   INKCELL_TYPE_HEADLINE, INKCELL_TYPE_TITLE,  INKCELL_TYPE_BODY,
    INKCELL_TYPE_BODY_SOFT, INKCELL_TYPE_LABEL,    INKCELL_TYPE_CAPTION};

/*
 * Every role, on every theme, at every body scale: never out of the drawable range, never out
 * of order.
 *
 * The clamp is why the second half holds at all. At the top of the range a display has nowhere
 * left to go and collapses onto the body; at the bottom a caption does. Collapsing is the scale
 * degrading, and it is fine - two roles landing on one size is a vocabulary with a synonym in
 * it. Two roles landing in the *wrong order* is a vocabulary that lies.
 */
INKCELL_TEST_CASE(ui_type_roles_stay_ordered, unit) {
    INKCELL_TEST_FAIL_IF(sizeof k_descending / sizeof k_descending[0] != (size_t)INKCELL_TYPE_COUNT,
                         "a type role was added without a place in the ordering");

    for (size_t i = 0U; i < inkcell_theme_count(); ++i) {
        const struct inkcell_theme *theme = inkcell_theme_at(i);
        for (int scale = INKCELL_SCALE_MIN; scale <= INKCELL_SCALE_MAX; ++scale) {
            int previous = INKCELL_SCALE_MAX + 1;
            for (size_t r = 0U; r < sizeof k_descending / sizeof k_descending[0]; ++r) {
                const struct inkcell_type_style style =
                    inkcell_theme_type_style(theme, k_descending[r], scale);
                INKCELL_TEST_FAIL_IF(style.scale < INKCELL_SCALE_MIN ||
                                         style.scale > INKCELL_SCALE_MAX,
                                     "a role resolved outside the drawable range");
                INKCELL_TEST_FAIL_IF(style.scale > previous,
                                     "a smaller role drew larger than the one above it");
                INKCELL_TEST_FAIL_IF(style.line_pct == 0U,
                                     "a role resolved to a line height of nothing");
                previous = style.scale;
            }
            /* The body role is the body scale by definition - it is the zero the others are
               offsets from, and a theme that moved it would be renaming the scale. */
            const struct inkcell_type_style body =
                inkcell_theme_type_style(theme, INKCELL_TYPE_BODY, scale);
            INKCELL_TEST_FAIL_IF(body.scale != inkcell_theme_clamp_scale(theme, scale),
                                 "the body role is not the body scale");
            INKCELL_TEST_FAIL_IF(body.tracking != 0,
                                 "the body role is tracked, so nothing is the face as drawn");
        }
    }
    record_success(test_name);
}

/*
 * A role out of range, and a NULL theme, answer with something drawable.
 *
 * The same contract the rest of this header keeps: an application naming a role this table has
 * never heard of gets readable body text, not a frame with a hole in it.
 */
INKCELL_TEST_CASE(ui_type_unknown_role_reads_as_body, unit) {
    const struct inkcell_theme *theme = inkcell_theme_default();
    const struct inkcell_type_style body =
        inkcell_theme_type_style(theme, INKCELL_TYPE_BODY, INKCELL_SCALE(4));

    const struct inkcell_type_style past =
        inkcell_theme_type_style(theme, INKCELL_TYPE_COUNT, INKCELL_SCALE(4));
    INKCELL_TEST_FAIL_IF(past.scale != body.scale || past.tabular,
                         "a role past the end read something");
    const struct inkcell_type_style negative =
        inkcell_theme_type_style(theme, (enum inkcell_type) - 1, INKCELL_SCALE(4));
    INKCELL_TEST_FAIL_IF(negative.scale != body.scale, "a negative role read something");

    const struct inkcell_type_style null_theme =
        inkcell_theme_type_style(NULL, INKCELL_TYPE_TITLE, INKCELL_SCALE(4));
    const struct inkcell_type_style defaulted =
        inkcell_theme_type_style(theme, INKCELL_TYPE_TITLE, INKCELL_SCALE(4));
    INKCELL_TEST_FAIL_IF(null_theme.scale != defaulted.scale ||
                             null_theme.weight != defaulted.weight,
                         "a NULL theme did not fall back to the default");
    record_success(test_name);
}

/*
 * Tracking is in scale units and comes out in pixels, truncated towards zero.
 *
 * Truncation rather than inkcell_scale_px()'s "never nothing", and both directions of it are
 * the point: a quarter step of air at the smallest drawable size is nothing, and a scale that
 * rounded it up to a pixel would space a caption further apart than a body. Tightening and
 * loosening have to round the same way or the two ends of the scale stop being symmetric.
 */
INKCELL_TEST_CASE(ui_type_tracking_converts_symmetrically, unit) {
    struct inkcell_type_style loose =
        inkcell_type_style_plain(INKCELL_SCALE(4), INKCELL_WEIGHT_REGULAR);
    struct inkcell_type_style tight = loose;
    loose.tracking = INKCELL_SCALE(1);
    tight.tracking = -INKCELL_SCALE(1);
    INKCELL_TEST_FAIL_IF(inkcell_type_tracking_px(&loose) != 4,
                         "a whole step of tracking at the body scale is four pixels");
    INKCELL_TEST_FAIL_IF(inkcell_type_tracking_px(&tight) != -inkcell_type_tracking_px(&loose),
                         "tightening and loosening did not round the same way");

    /* A quarter step at the smallest scale rounds away rather than up. */
    struct inkcell_type_style hair =
        inkcell_type_style_plain(INKCELL_SCALE_MIN, INKCELL_WEIGHT_REGULAR);
    hair.tracking = 1;
    INKCELL_TEST_FAIL_IF(inkcell_type_tracking_px(&hair) != 0,
                         "a quarter step at the minimum scale was rounded up to a pixel");
    INKCELL_TEST_FAIL_IF(inkcell_type_tracking_px(NULL) != 0, "a NULL style tracked something");
    record_success(test_name);
}

/* A line height is a percentage of the font's own, and never nothing: a theme that states
   something absurd gets an ugly frame rather than a row drawn on top of the one before it. */
INKCELL_TEST_CASE(ui_type_line_height_scales_the_font, unit) {
    struct inkcell_type_style style =
        inkcell_type_style_plain(INKCELL_SCALE(4), INKCELL_WEIGHT_REGULAR);
    INKCELL_TEST_FAIL_IF(inkcell_type_line_px(40, &style) != 40,
                         "a plain style did not draw the font's own line");
    style.line_pct = 110U;
    INKCELL_TEST_FAIL_IF(inkcell_type_line_px(40, &style) != 44, "a looser line did not open up");
    style.line_pct = 95U;
    INKCELL_TEST_FAIL_IF(inkcell_type_line_px(40, &style) != 38, "a tighter line did not close up");
    style.line_pct = 1U;
    INKCELL_TEST_FAIL_IF(inkcell_type_line_px(40, &style) < 1,
                         "an absurd line height collapsed a row onto its neighbour");
    INKCELL_TEST_FAIL_IF(inkcell_type_line_px(0, &style) != 0,
                         "a font with no line advance grew one");
    record_success(test_name);
}

/* ---- the measured half, which needs a panel ---------------------------------------------- */

struct type_page {
    struct inkcell_capture *capture;
    struct inkcell_draw_state *state;
};

static bool type_open(struct type_page *page) {
    page->capture = NULL;
    if (inkcell_capture_open(&page->capture, TYPE_PANEL_W, TYPE_PANEL_H, INKCELL_SCALE(4)) < 0) {
        return false;
    }
    page->state = inkcell_capture_state(page->capture);
    return page->state != NULL;
}

/*
 * Tracking widens a run by one gap per gap, not one per cell.
 *
 * The off-by-one here is the whole of what the trailing correction in
 * inkcell_fb_text_width_styled() is for: air after the last letter is not part of the line, so a
 * box measured with it has a gap inside its right edge and a centred line sits left of centre.
 */
INKCELL_TEST_CASE(ui_type_tracking_widens_by_the_gaps, unit) {
    struct type_page page;
    INKCELL_TEST_FAIL_IF(!type_open(&page), "the capture panel should open");

    const struct inkcell_type_style plain =
        inkcell_type_style_plain(INKCELL_SCALE(4), INKCELL_WEIGHT_REGULAR);
    struct inkcell_type_style tracked = plain;
    tracked.tracking = INKCELL_SCALE(1);
    const int step = inkcell_type_tracking_px(&tracked);

    static const char k_text[] = "ABCDE";
    const int bare = inkcell_fb_text_width_styled(page.state, k_text, &plain);
    const int wide = inkcell_fb_text_width_styled(page.state, k_text, &tracked);
    INKCELL_TEST_FAIL_IF_CLEANUP(step <= 0, inkcell_capture_close(page.capture),
                                 "a whole step of tracking should be pixels at the body scale");
    INKCELL_TEST_FAIL_IF_CLEANUP(wide - bare != step * 4, inkcell_capture_close(page.capture),
                                 "five cells should gain four gaps, not five");

    /* And a single cell gains nothing at all, because it has no gaps. */
    const int one_bare = inkcell_fb_text_width_styled(page.state, "A", &plain);
    const int one_wide = inkcell_fb_text_width_styled(page.state, "A", &tracked);
    INKCELL_TEST_FAIL_IF_CLEANUP(one_bare != one_wide, inkcell_capture_close(page.capture),
                                 "a single cell gained the tracking that follows it");
    inkcell_capture_close(page.capture);
    record_success(test_name);
}

/*
 * Tabular figures: every digit steps the widest of the ten, so two readings of the same length
 * measure the same.
 *
 * This is the property the status screen wanted. The UI face draws '1' at two thirds the width
 * of '4', which is right inside a word and wrong in a column - a value redrawn once a second
 * moves sideways under the eye, and two rows do not line up under one another. The case also
 * checks the *negative*: the same two strings in the plain style do not measure the same, which
 * is what says the flag is doing something rather than the face already being monospace.
 */
INKCELL_TEST_CASE(ui_type_tabular_figures_share_an_advance, unit) {
    struct type_page page;
    INKCELL_TEST_FAIL_IF(!type_open(&page), "the capture panel should open");

    const struct inkcell_type_style plain =
        inkcell_type_style_plain(INKCELL_SCALE(4), INKCELL_WEIGHT_REGULAR);
    const struct inkcell_type_style tabular = inkcell_type_style_tabular(plain);

    const int ones = inkcell_fb_text_width_styled(page.state, "1111", &tabular);
    const int fours = inkcell_fb_text_width_styled(page.state, "4444", &tabular);
    INKCELL_TEST_FAIL_IF_CLEANUP(ones != fours, inkcell_capture_close(page.capture),
                                 "tabular digits measured different widths");

    const int digit = inkcell_font_digit_advance(inkcell_fb_font(page.state), INKCELL_SCALE(4));
    INKCELL_TEST_FAIL_IF_CLEANUP(ones != digit * 4, inkcell_capture_close(page.capture),
                                 "a tabular run is not four common advances wide");
    INKCELL_TEST_FAIL_IF_CLEANUP(
        inkcell_fb_cell_adv_styled(page.state, (uint32_t)'1', &tabular) != digit,
        inkcell_capture_close(page.capture), "a tabular '1' did not step the common advance");

    const bool proportional = inkcell_fb_font(page.state)->proportional;
    const int loose_ones = inkcell_fb_text_width_styled(page.state, "1111", &plain);
    const int loose_fours = inkcell_fb_text_width_styled(page.state, "4444", &plain);
    INKCELL_TEST_FAIL_IF_CLEANUP(proportional && loose_ones == loose_fours,
                                 inkcell_capture_close(page.capture),
                                 "the face is proportional, so its plain figures cannot agree");
    /* The common advance is the widest of the ten, so nothing is squeezed: a digit pulled into
       a cell narrower than it was drawn is a digit with its crossbar touching its stem. */
    INKCELL_TEST_FAIL_IF_CLEANUP(digit * 4 < loose_fours, inkcell_capture_close(page.capture),
                                 "the tabular advance is narrower than a digit is drawn");

    /* A letter is untouched - the flag is about figures, not about setting everything on a
       grid, which would turn a proportional face into a monospace one. */
    INKCELL_TEST_FAIL_IF_CLEANUP(inkcell_fb_text_width_styled(page.state, "www", &tabular) !=
                                     inkcell_fb_text_width_styled(page.state, "www", &plain),
                                 inkcell_capture_close(page.capture),
                                 "tabular figures changed the width of letters");
    inkcell_capture_close(page.capture);
    record_success(test_name);
}

/*
 * A wrap in a style breaks where that style's lines actually end.
 *
 * The one that catches a half-migrated call site: wrapping with the plain metric and drawing in
 * a tracked role puts a cell's worth of air per character over the budget, which is a paragraph
 * that runs out of its panel on the longest line and nowhere else - the kind of bug that
 * survives a screenshot because the sentence somebody tested with was short.
 */
INKCELL_TEST_CASE(ui_type_wrapped_lines_respect_the_style, unit) {
    struct type_page page;
    INKCELL_TEST_FAIL_IF(!type_open(&page), "the capture panel should open");

    static const char k_paragraph[] =
        "A supporting paragraph long enough to wrap onto a second and a third line, which is "
        "what a note row and an empty state both have to survive.";
    const size_t budget = 240U;

    struct inkcell_type_style tracked =
        inkcell_type_style_plain(INKCELL_SCALE(4), INKCELL_WEIGHT_REGULAR);
    tracked.tracking = INKCELL_SCALE(1);

    const uint32_t plain_lines =
        inkcell_fb_wrapped_lines(page.state, k_paragraph, budget, INKCELL_SCALE(4));
    const uint32_t tracked_lines =
        inkcell_fb_wrapped_lines_styled(page.state, k_paragraph, budget, &tracked);
    INKCELL_TEST_FAIL_IF_CLEANUP(tracked_lines <= plain_lines, inkcell_capture_close(page.capture),
                                 "a tracked paragraph did not need more lines than a plain one");

    /* And the supporting role's leading is the one a paragraph steps by, which is not the
       body's - the role loosened its line precisely for text that wraps. */
    const struct inkcell_type_style soft =
        inkcell_fb_type_style(page.state, INKCELL_TYPE_BODY_SOFT);
    INKCELL_TEST_FAIL_IF_CLEANUP(inkcell_fb_line_adv_styled(page.state, &soft) <=
                                     inkcell_fb_line_adv(page.state, soft.scale),
                                 inkcell_capture_close(page.capture),
                                 "the supporting role did not loosen its line");
    inkcell_capture_close(page.capture);
    record_success(test_name);
}
