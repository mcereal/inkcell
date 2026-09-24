#define _POSIX_C_SOURCE 200809L

/*
 * The type scale, and the thing every layout in this toolkit is measured against.
 *
 * Seven roles, one line each, set exactly as the theme sets them - size, weight, tracking and
 * line height together, because a role is all four at once and a specimen showing only the
 * sizes would be a specimen of something this toolkit does not draw. The lines step by each
 * role's *own* line advance, so the page also shows what the leading does: the supporting body
 * sits further from its neighbours than the body does, and the display sits closer.
 *
 * Under each specimen is the measurement, because the rule this library is built on is that
 * text is *measured* and never counted. A specimen showing a string of `M`s would prove
 * nothing: the bug worth catching is a proportional face where a count of characters multiplied
 * by a nominal advance is not the width the string actually takes, and the only way to see it
 * is to draw a rule at each of the two answers and look at whether they land together.
 *
 * That is what the two ticks under each line are. The solid one is where
 * inkcell_fb_text_width_styled() says the line ends; the hollow one is where a cell count times
 * inkcell_fb_char_adv() says it does. On a monospace face they coincide. On this one they do
 * not, and a screen that used the second to lay out the first would run off the panel by the
 * distance between them.
 *
 * Then the figures, which are the other half of the same argument and the reason a status
 * screen was hard to read. The same three readings are drawn twice, right-aligned to the same
 * edge: proportional on the left, tabular on the right. The proportional column's digits do not
 * fall in line with each other, and a reading redrawn once a second in that column moves
 * sideways under the reader for no reason at all.
 */

#include "gallery.h"

#include <stdio.h>

/* A pangram, so that every letter of the alphabet is in the specimen. Not read as a sentence:
   it is a specimen, which is why it is here rather than in the catalog. */
#define GALLERY_PANGRAM "Sphinx of black quartz, judge my vow \xE2\x80\x94 0123456789"

/* Three readings with the same number of digits and different ones - which is exactly the case
   proportional figures cannot keep in a column. */
static const char *const k_figures[] = {"1,118.41", "4,470.00", "1,141.11"};

static int specimen(const struct inkcell_draw_state *state, int x, int y, int width,
                    enum inkcell_type type) {
    const struct inkcell_type_style style = inkcell_fb_type_style(state, type);
    const struct inkcell_rgb ink = inkcell_fb_color(state, INKCELL_COLOR_TEXT);
    const struct inkcell_rgb ground = inkcell_fb_color(state, INKCELL_COLOR_BG);

    /*
     * Cut to the body, in the role's own style.
     *
     * The specimen is the same sentence at every role, so the two large ones do not fit the
     * panel - and a line that runs off the edge takes the measurement with it, which is the
     * one thing this page exists to show. Trimmed by measuring rather than by counting cells,
     * for exactly the reason the ticks below are drawn.
     */
    char fitted[INKCELL_LINE_MAX];
    (void)snprintf(fitted, sizeof fitted, "%s", GALLERY_PANGRAM);
    while (inkcell_fb_text_width_styled(state, fitted, &style) > width) {
        const size_t cells = inkcell_text_cells(fitted);
        if (cells <= 1U) {
            break;
        }
        inkcell_text_cell_truncate(fitted, cells - 1U);
    }

    inkcell_fb_draw_text_styled(state, x, y, fitted, &style, ink, ground);

    /* Measured, and counted. The gap between the two ticks is the bug the whole measuring rule
       exists to prevent, drawn to scale. */
    const int measured = inkcell_fb_text_width_styled(state, fitted, &style);
    const int counted = (int)inkcell_fb_text_cols(state, fitted, style.scale) *
                        inkcell_fb_char_adv(state, style.scale);
    const int line = inkcell_fb_line_adv_styled(state, &style);
    const int tick = inkcell_fb_rule_height(state, style.scale) * 2;

    inkcell_fb_fill_rect(state, x + measured, y + line - tick, tick * 2, tick,
                         inkcell_fb_tone_color(state, INKCELL_TONE_PRIMARY));
    inkcell_fb_fill_rect(state, x + counted, y + line - tick, tick * 2, tick,
                         inkcell_fb_tone_color(state, INKCELL_TONE_ERROR));

    return y + line;
}

/*
 * One column of readings, right-aligned to `right`, in `style`.
 *
 * Right-aligned because that is how a reading is set wherever it means anything - against a
 * unit, against the edge of a row - and because a left-aligned column hides the whole effect:
 * proportional figures all start in the same place and end wherever they end.
 */
static void figures(const struct inkcell_draw_state *state, int right, int y,
                    const struct inkcell_type_style *style, enum gallery_str_id label) {
    const struct inkcell_rgb ink = inkcell_fb_color(state, INKCELL_COLOR_TEXT);
    const struct inkcell_rgb dim = inkcell_fb_color(state, INKCELL_COLOR_TEXT_DIM);
    const struct inkcell_rgb ground = inkcell_fb_color(state, INKCELL_COLOR_BG);
    const struct inkcell_type_style caption = inkcell_fb_type_style(state, INKCELL_TYPE_CAPTION);
    const char *const heading = gallery_text(label);

    inkcell_fb_draw_text_styled(state,
                                right - inkcell_fb_text_width_styled(state, heading, &caption), y,
                                heading, &caption, dim, ground);
    y += inkcell_fb_line_adv_styled(state, &caption);

    for (size_t i = 0U; i < sizeof k_figures / sizeof k_figures[0]; ++i) {
        const int w = inkcell_fb_text_width_styled(state, k_figures[i], style);
        inkcell_fb_draw_text_styled(state, right - w, y, k_figures[i], style, ink, ground);
        y += inkcell_fb_line_adv_styled(state, style);
    }
}

void gallery_scene_typography(struct inkcell_draw_state *state) {
    struct inkcell_fb_layout layout = gallery_frame(state, GALLERY_STR_HEAD_TYPE, 4U);
    const struct inkcell_fb_row_box box = inkcell_fb_row_box(state);
    int y = layout.body_y;

    y = gallery_section(state, &layout, y, GALLERY_STR_HEAD_TYPE);

    for (int t = 0; t < (int)INKCELL_TYPE_COUNT; ++t) {
        /* A tick's width of room kept at the right, so the measured mark has somewhere to
           land on the roles whose specimen had to be cut. */
        y = specimen(state, box.text_x, y,
                     box.text_right - box.text_x - inkcell_fb_char_adv(state, state->scale),
                     (enum inkcell_type)t);
        y += inkcell_fb_space(state, INKCELL_SPACE_XS);
    }

    y += inkcell_fb_space(state, INKCELL_SPACE_SM);
    y = gallery_section(state, &layout, y, GALLERY_STR_HEAD_FIGURES);

    /*
     * The same readings twice: the body role as the face draws it, and the same role with
     * tabular figures. Two columns a third of the body apart, each right-aligned to its own
     * edge, so the question the page asks is whether the digits under one another line up.
     */
    const struct inkcell_type_style body = inkcell_fb_type_style(state, INKCELL_TYPE_BODY);
    const int third = layout.body_w / 3;
    const struct inkcell_type_style tabular = inkcell_type_style_tabular(body);
    figures(state, box.text_x + third, y, &body, GALLERY_STR_FIGURES_PROPORTIONAL);
    figures(state, box.text_x + 2 * third, y, &tabular, GALLERY_STR_FIGURES_TABULAR);

    gallery_footer(state, &layout);
}
