#define _POSIX_C_SOURCE 200809L

/*
 * The type scale, and the thing every layout in this toolkit is measured against.
 *
 * Three roles, two weights, and one line each at the scale the theme gives that role - plus the
 * measurement under each, because the rule this library is built on is that text is *measured*
 * and never counted. A specimen showing a string of `M`s would prove nothing: the bug worth
 * catching is a proportional face where a count of characters multiplied by a nominal advance
 * is not the width the string actually takes, and the only way to see it is to draw a rule at
 * each of the two answers and look at whether they land together.
 *
 * That is what the two ticks under each line are. The solid one is where
 * inkcell_fb_text_width() says the line ends; the hollow one is where a cell count times
 * inkcell_fb_char_adv() says it does. On a monospace face they coincide. On this one they do
 * not, and a screen that used the second to lay out the first would run off the panel by the
 * distance between them.
 */

#include "gallery.h"

/* A pangram, so that every letter of the alphabet is in the specimen. Not read as a sentence:
   it is a specimen, which is why it is here rather than in the catalog. */
#define GALLERY_PANGRAM "Sphinx of black quartz, judge my vow \xE2\x80\x94 0123456789"

static int specimen(const struct inkcell_backend_fb_state *state,
                    const struct inkcell_fb_layout *layout, int x, int y, enum inkcell_type type,
                    enum inkcell_weight weight) {
    const int scale = inkcell_fb_type_scale(state, type);
    const struct inkcell_rgb ink = inkcell_fb_color(state, INKCELL_COLOR_TEXT);
    const struct inkcell_rgb ground = inkcell_fb_color(state, INKCELL_COLOR_BG);

    inkcell_fb_draw_text_weight(state, x, y, GALLERY_PANGRAM, scale, weight, ink, ground);

    /* Measured, and counted. The gap between the two ticks is the bug the whole measuring rule
       exists to prevent, drawn to scale. */
    const int measured = inkcell_fb_text_width_weight(state, GALLERY_PANGRAM, scale, weight);
    const int counted = (int)inkcell_fb_text_cols(state, GALLERY_PANGRAM, scale) *
                        inkcell_fb_char_adv(state, scale);
    const int line = inkcell_fb_line_adv(state, scale);
    const int tick = inkcell_fb_rule_height(state, scale) * 2;

    inkcell_fb_fill_rect(state, x + measured, y + line - tick, tick * 2, tick,
                         inkcell_fb_tone_color(state, INKCELL_TONE_PRIMARY));
    inkcell_fb_fill_rect(state, x + counted, y + line - tick, tick * 2, tick,
                         inkcell_fb_tone_color(state, INKCELL_TONE_ERROR));

    (void)layout;
    return y + line + inkcell_fb_space(state, INKCELL_SPACE_SM);
}

void gallery_scene_typography(struct inkcell_backend_fb_state *state) {
    struct inkcell_fb_layout layout = gallery_frame(state, GALLERY_STR_HEAD_TYPE, 4U);
    const struct inkcell_fb_row_box box = inkcell_fb_row_box(state);
    int y = layout.body_y;

    y = gallery_section(state, &layout, y, GALLERY_STR_HEAD_TYPE);

    for (int t = 0; t < (int)INKCELL_TYPE_COUNT; ++t) {
        const enum inkcell_type type = (enum inkcell_type)t;
        /* The weight the theme sets this role in, and then the other one - because a role that
           is already strong has nowhere left to go when a row needs emphasis, and that is worth
           seeing rather than discovering. */
        y = specimen(state, &layout, box.text_x, y, type, inkcell_fb_type_weight(state, type));
        y = specimen(state, &layout, box.text_x, y, type,
                     inkcell_fb_type_weight(state, type) == INKCELL_WEIGHT_STRONG
                         ? INKCELL_WEIGHT_REGULAR
                         : INKCELL_WEIGHT_STRONG);
        y += inkcell_fb_space(state, INKCELL_SPACE_SM);
    }

    y += inkcell_fb_space(state, INKCELL_SPACE_MD);

    /* The tones, at the body scale, each on the ground. A tone that has collapsed onto the body
       colour is a tone that says nothing, and it is invisible in a diff. */
    const int body_scale = inkcell_fb_type_scale(state, INKCELL_TYPE_BODY);
    const int body_line = inkcell_fb_line_adv(state, body_scale);
    const int floor_y = layout.footer_y - inkcell_fb_gutter(state);
    for (int tone = 0; tone < (int)INKCELL_TONE_COUNT && y + body_line <= floor_y; ++tone) {
        inkcell_fb_draw_text(state, box.text_x, y, GALLERY_PANGRAM, body_scale,
                             inkcell_fb_tone_color(state, (enum inkcell_tone)tone),
                             inkcell_fb_color(state, INKCELL_COLOR_BG));
        y += body_line;
    }

    gallery_footer(state, &layout);
}
