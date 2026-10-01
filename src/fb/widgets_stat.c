#define _POSIX_C_SOURCE 200809L

/*
 * The stat tile: a figure, the question it answers and, at most, one row-height picture of it.
 */

#include "inkcell/ui/widgets/meter.h"
#include "inkcell/ui/widgets/stat.h"

#include "inkcell/i18n/strings.h"
#include "inkcell/ui/layout.h"

#include <string.h>

/*
 * The tile's geometry, shared by the measure and the draw for the card's reason: a tile that
 * measured one height and drew another would be a row of tiles whose bottoms do not line up, which
 * is the one thing a board exists to prevent.
 */
struct inkcell_fb_stat_metrics {
    int pad;   /* across */
    int pad_y; /* down - half the inset, on the card's argument about line advances */
    int edge;
    struct inkcell_type_style label;
    struct inkcell_type_style figure; /* the largest role the value fits at */
    struct inkcell_type_style caption;
    int label_h, figure_h, caption_h;
    int picture_h; /* the bar or the line, 0 for none */
    int picture_gap;
    enum inkcell_color fill;
};

static int inkcell_fb_stat_label_scale(const struct inkcell_fb_layout *layout) {
    return layout->small;
}

static struct inkcell_fb_stat_metrics
inkcell_fb_stat_measure(const struct inkcell_draw_state *state,
                        const struct inkcell_fb_layout *layout,
                        const struct inkcell_fb_stat *stat) {
    struct inkcell_fb_stat_metrics m;
    memset(&m, 0, sizeof m);
    m.pad = inkcell_scale_px((int)inkcell_fb_metrics(state)->card_pad, state->scale);
    m.pad_y = m.pad / 2 > 0 ? m.pad / 2 : m.pad;
    m.edge = inkcell_fb_edge(state);

    m.label = inkcell_fb_type_style(state, INKCELL_TYPE_LABEL);
    m.label.scale = inkcell_fb_stat_label_scale(layout);
    m.caption = inkcell_type_style_tabular(inkcell_fb_type_style(state, INKCELL_TYPE_CAPTION));

    /* The figure, at the largest role that fits across the tile - stepped down rather than cut,
       because half a number is not a number. The body role is the floor: below it the tile is
       a card row in a box, and fitting it is the caller's problem. */
    const int room = stat->rect.w - 2 * (m.pad + m.edge);
    static const enum inkcell_type k_roles[] = {INKCELL_TYPE_DISPLAY, INKCELL_TYPE_HEADLINE,
                                                INKCELL_TYPE_TITLE, INKCELL_TYPE_BODY};
    const char *value = stat->value != NULL ? stat->value : "";
    for (size_t i = 0U; i < sizeof k_roles / sizeof k_roles[0]; ++i) {
        m.figure = inkcell_type_style_tabular(inkcell_fb_type_style(state, k_roles[i]));
        if (inkcell_fb_text_width_styled(state, value, &m.figure) <= room) {
            break;
        }
    }

    m.label_h = inkcell_fb_line_adv_styled(state, &m.label);
    m.figure_h = inkcell_fb_line_adv_styled(state, &m.figure);
    m.caption_h = stat->caption != NULL && stat->caption[0] != '\0'
                      ? inkcell_fb_line_adv_styled(state, &m.caption)
                      : 0;
    switch (stat->picture) {
    case INKCELL_FB_STAT_METER:
        m.picture_h = inkcell_fb_meter_thickness(state, state->scale);
        break;
    case INKCELL_FB_STAT_TREND:
        m.picture_h = stat->trend != NULL && stat->trend->count >= 2U
                          ? inkcell_fb_sparkline_height(state, state->scale)
                          : 0;
        break;
    case INKCELL_FB_STAT_NONE:
    default:
        m.picture_h = 0;
        break;
    }
    m.picture_gap = m.picture_h > 0 ? inkcell_fb_space(state, INKCELL_SPACE_SM) : 0;

    switch (stat->variant) {
    case INKCELL_FB_CARD_ELEVATED:
        m.fill = INKCELL_COLOR_SURFACE_HIGH;
        break;
    case INKCELL_FB_CARD_OUTLINED:
        m.fill = INKCELL_COLOR_BG;
        break;
    case INKCELL_FB_CARD_FILLED:
    default:
        m.fill = INKCELL_COLOR_SURFACE;
        break;
    }
    return m;
}

int inkcell_fb_stat_height(const struct inkcell_draw_state *state,
                           const struct inkcell_fb_layout *layout,
                           const struct inkcell_fb_stat *stat) {
    if (state == NULL || layout == NULL || stat == NULL) {
        return 0;
    }
    const struct inkcell_fb_stat_metrics m = inkcell_fb_stat_measure(state, layout, stat);
    return 2 * (m.pad_y + m.edge) + m.label_h + m.figure_h + m.caption_h + m.picture_gap +
           m.picture_h;
}

void inkcell_fb_draw_stat(struct inkcell_draw_state *state, const struct inkcell_fb_layout *layout,
                          const struct inkcell_fb_stat *stat) {
    if (state == NULL || layout == NULL || stat == NULL || stat->rect.w <= 0 || stat->rect.h <= 0) {
        return;
    }
    const struct inkcell_fb_stat_metrics m = inkcell_fb_stat_measure(state, layout, stat);
    const struct inkcell_fb_rect r = stat->rect;
    const int radius = inkcell_fb_radius(state, INKCELL_SHAPE_MD) + m.edge;
    const struct inkcell_rgb ground = inkcell_fb_color(state, m.fill);

    /* The panel, on the card's terms: the elevated variant casts the raised shadow, the outlined
       one is the ground held by its edge. */
    if (stat->variant == INKCELL_FB_CARD_ELEVATED) {
        inkcell_fb_draw_shadow(state, r, radius, INKCELL_ELEVATION_RAISED, INKCELL_ANIM_ONE);
    }
    inkcell_fb_fill_round_rect(state, r.x, r.y, r.w, r.h, radius, ground);
    if (stat->variant == INKCELL_FB_CARD_OUTLINED) {
        inkcell_fb_stroke_round_rect(state, r.x, r.y, r.w, r.h, radius, m.edge,
                                     inkcell_fb_color(state, INKCELL_COLOR_OUTLINE));
    }

    const int left = r.x + m.edge + m.pad;
    const int right = r.x + r.w - m.edge - m.pad;
    const int bottom = r.y + r.h - m.edge - m.pad_y;
    int y = r.y + m.edge + m.pad_y;

    /* The question, quiet: the dim ink every tile shares, so the row of them reads as a row of
       answers. */
    const struct inkcell_rgb quiet = inkcell_fb_color(state, INKCELL_COLOR_TEXT_DIM);
    if (y + m.label_h <= bottom) {
        int x = left;
        if (inkcell_icon_is_valid(stat->icon)) {
            inkcell_fb_draw_icon(state, x, y, stat->icon, m.label.scale, quiet, ground);
            x += inkcell_fb_icon_box(state, m.label.scale) +
                 inkcell_fb_char_adv(state, m.label.scale) / 2;
        }
        if (stat->label != INKCELL_STR_NONE && x < right) {
            struct inkcell_line line;
            inkcell_line_reset(&line);
            inkcell_line_printf(&line, "%s", inkcell_str(stat->label));
            const int adv = inkcell_fb_char_adv(state, m.label.scale);
            inkcell_line_fit(&line, adv > 0 ? (size_t)((right - x) / adv) : 1U);
            inkcell_fb_draw_text_styled(state, x, y, inkcell_line_text(&line), &m.label, quiet,
                                        ground);
        }
        y += m.label_h;
    }

    /* The answer. Not clipped across: the measure already picked a role it fits at, and a figure
       too wide even at the body size is drawn whole and runs into the padding rather than losing
       a digit. Clipped down, though - a tile too short for its figure shows its label alone. */
    if (stat->value != NULL && y + m.figure_h <= bottom) {
        inkcell_fb_draw_text_styled(state, left, y, stat->value, &m.figure,
                                    inkcell_fb_tone_color(state, stat->tone), ground);
        y += m.figure_h;
    }
    if (m.caption_h > 0 && y + m.caption_h <= bottom) {
        struct inkcell_line line;
        inkcell_line_reset(&line);
        inkcell_line_printf(&line, "%s", stat->caption);
        const int adv = inkcell_fb_char_adv(state, m.caption.scale);
        inkcell_line_fit(&line, adv > 0 ? (size_t)((right - left) / adv) : 1U);
        inkcell_fb_draw_text_styled(state, left, y, inkcell_line_text(&line), &m.caption, quiet,
                                    ground);
        y += m.caption_h;
    }

    /* The picture, on the tile's floor rather than under the caption: a row of tiles of
       different content then has its bars on one line, which is what lets them be compared. */
    if (m.picture_h <= 0 || bottom - m.picture_h < y + m.picture_gap) {
        return;
    }
    const struct inkcell_fb_rect slot = {left, bottom - m.picture_h, right - left, m.picture_h};
    if (stat->picture == INKCELL_FB_STAT_METER) {
        const struct inkcell_fb_meter meter = {
            .rect = slot,
            .id = stat->meter_id,
            .kind = INKCELL_FB_METER_DETERMINATE,
            .value = stat->meter_value,
            .scale = stat->meter_scale,
            .band = stat->meter_band,
            /* The bar takes the figure's tone at rest, so a band it is not past colours both
               alike; the band overrides it where the reading has crossed one. */
            .tone = stat->tone == INKCELL_TONE_NORMAL ? INKCELL_TONE_PRIMARY : stat->tone,
            .ground = m.fill,
        };
        inkcell_fb_draw_meter(state, &meter);
    } else if (stat->picture == INKCELL_FB_STAT_TREND) {
        const struct inkcell_fb_sparkline spark = {
            .rect = slot,
            .points = stat->trend,
            .tone = stat->tone == INKCELL_TONE_NORMAL ? INKCELL_TONE_PRIMARY : stat->tone,
        };
        inkcell_fb_draw_sparkline(state, &spark);
    }
}
