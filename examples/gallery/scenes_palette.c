#define _POSIX_C_SOURCE 200809L

/*
 * The theme itself: every fill a component can ask for, with the ink that goes on it.
 *
 * This is the sheet that makes a *theme* reviewable rather than a component, and it is the one
 * a new palette is judged by. A theme states its colours by role and promises a contrast ratio
 * for each pairing; the test suite checks the arithmetic, and this checks the thing arithmetic
 * cannot - whether the six families are still telling six stories apart from one another once
 * a person is looking at them.
 *
 * Every swatch carries its own ink rather than a label, and that is deliberate. A grid of
 * labelled colours is a picture of some words; a grid of `Ag` set in the ink the theme pairs
 * with each fill is a picture of the promise, and a pairing that fails is illegible rather than
 * merely wrong.
 */

#include "gallery.h"

/* A type specimen rather than a word: what a punchcutter puts on a sheet, and what shows an
   ascender, a descender and a bowl in two glyphs. It is not prose and does not translate. */
#define GALLERY_SPECIMEN "Ag"

static void swatch(const struct inkcell_draw_state *state, int x, int y, int w, int h,
                   struct inkcell_rgb fill, struct inkcell_rgb ink, int scale) {
    inkcell_fb_fill_round_rect(state, x, y, w, h, inkcell_fb_radius(state, INKCELL_SHAPE_SM), fill);
    const int text_w = inkcell_fb_text_width(state, GALLERY_SPECIMEN, scale);
    const int text_h = inkcell_scale_px((int)inkcell_fb_font(state)->height, scale);
    inkcell_fb_draw_text(state, x + (w - text_w) / 2, y + (h - text_h) / 2, GALLERY_SPECIMEN, scale,
                         ink, fill);
}

void gallery_scene_palette(struct inkcell_draw_state *state) {
    struct inkcell_fb_layout layout = gallery_frame(state, GALLERY_STR_HEAD_PALETTE, 4U);
    const struct inkcell_fb_row_box box = inkcell_fb_row_box(state);
    const int scale = layout.small;
    const int gap = inkcell_fb_space(state, INKCELL_SPACE_SM);
    int y = layout.body_y;

    /*
     * The families, one per row, and each family's four slots across.
     *
     * Base and container are the two weights a family comes in - a filled button and a tonal
     * one - and each is drawn once per interaction state: at rest, hovered, focused and pressed.
     * The container's last columns are the ones worth staring at, because they are the
     * combinations the contrast contract has the least room in. A base never moves: see
     * inkcell_theme_paint() for why a layer goes on a container and nowhere else.
     */
    const int cols = 2 * (int)INKCELL_STATE_COUNT;
    const int cell_w = (box.text_right - box.text_x - (cols - 1) * gap) / cols;
    const int cell_h = inkcell_fb_line_adv(state, scale) + 2 * gap;

    for (int f = 0; f < (int)INKCELL_FAMILY_COUNT; ++f) {
        const enum inkcell_family family = (enum inkcell_family)f;
        int x = box.text_x;

        for (int s = 0; s < (int)INKCELL_STATE_COUNT; ++s) {
            const struct inkcell_paint base =
                inkcell_fb_paint(state, family, INKCELL_SLOT_BASE, (enum inkcell_state)s);
            swatch(state, x, y, cell_w, cell_h, base.fill, base.ink, scale);
            x += cell_w + gap;
        }
        for (int s = 0; s < (int)INKCELL_STATE_COUNT; ++s) {
            const struct inkcell_paint container =
                inkcell_fb_paint(state, family, INKCELL_SLOT_CONTAINER, (enum inkcell_state)s);
            swatch(state, x, y, cell_w, cell_h, container.fill, container.ink, scale);
            x += cell_w + gap;
        }
        y += cell_h + gap;
    }

    y += inkcell_fb_space(state, INKCELL_SPACE_MD);

    /*
     * The surfaces, which are the other half of a palette: the tiers a panel stands on. Tonal
     * elevation means the distance from the ground is carried by the fill alone, so a theme
     * whose tiers are too close together is a theme where a card has no edge - and that is
     * exactly what this row shows.
     */
    static const enum inkcell_color k_surfaces[] = {
        INKCELL_COLOR_BG,           INKCELL_COLOR_SURFACE_LOW, INKCELL_COLOR_SURFACE,
        INKCELL_COLOR_SURFACE_HIGH, INKCELL_COLOR_SURFACE_SEL, INKCELL_COLOR_SURFACE_ACTIVE,
    };
    int x = box.text_x;
    for (size_t i = 0U; i < sizeof k_surfaces / sizeof k_surfaces[0]; ++i) {
        swatch(state, x, y, cell_w, cell_h, inkcell_fb_color(state, k_surfaces[i]),
               inkcell_fb_color(state, i >= 4U ? INKCELL_COLOR_TEXT_ON_SEL : INKCELL_COLOR_TEXT),
               scale);
        x += cell_w + gap;
    }
    y += cell_h + gap;

    /* The inverse tier, the code ground, and the categorical fills a chart divides a whole
       with. The last of those is drawn because a theme that states fewer than
       INKCELL_SERIES_COLORS leaves a part of every chart unpainted, and this is where that is
       visible rather than invisible. */
    x = box.text_x;
    swatch(state, x, y, cell_w, cell_h, inkcell_fb_color(state, INKCELL_COLOR_SURFACE_INVERSE),
           inkcell_fb_color(state, INKCELL_COLOR_TEXT_ON_INVERSE), scale);
    x += cell_w + gap;
    swatch(state, x, y, cell_w, cell_h, inkcell_fb_color(state, INKCELL_COLOR_CODE_GROUND),
           inkcell_fb_color(state, INKCELL_COLOR_CODE), scale);
    x += cell_w + gap;
    for (uint32_t i = 0U; i < INKCELL_SERIES_COLORS; ++i) {
        swatch(state, x, y, cell_w, cell_h, inkcell_theme_series(state->theme, i),
               inkcell_fb_color(state, INKCELL_COLOR_BG), scale);
        x += cell_w + gap;
    }
    y += cell_h + gap;

    /* And the avatar tints, which a theme may state fewer of. A list of people is a column of
       these, so what matters is that no two sit next to each other looking alike. */
    x = box.text_x;
    for (uint32_t seed = 0U; seed < INKCELL_AVATAR_TINTS; ++seed) {
        swatch(state, x, y, cell_w, cell_h, inkcell_theme_avatar(state->theme, seed),
               inkcell_fb_color(state, INKCELL_COLOR_BG), scale);
        x += cell_w + gap;
    }

    gallery_footer(state, &layout);
}
