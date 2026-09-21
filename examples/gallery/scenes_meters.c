#define _POSIX_C_SOURCE 200809L

/*
 * A quantity as a length, and a quantity over time.
 *
 * The bar, the slider and the staircase all answer "how much"; the sparkline and the chart
 * answer "how it got here". They are one sheet because the thing worth checking about them is
 * that they agree: a reading at three quarters has to look like three quarters on all of them,
 * and a band that calls a value bad has to say so in the same colour whichever one is drawing.
 *
 * The values are stated in the units the toolkit states them in - permille for a proportion,
 * decidegrees for a temperature - rather than as pixel counts, because that is what a screen
 * would hand over and the arithmetic in between is what this picture is checking.
 */

#include "gallery.h"

#include "inkcell/ui/layout.h"

#include <stdio.h>

#include <string.h>

enum {
    GALLERY_ANIM_METER = 0x4D00,
    GALLERY_ANIM_SLIDER = 0x4D40,
    GALLERY_ANIM_DIAL = 0x4D80,
};

/* A plausible series: a reading every half minute, with one silence in the middle so that the
   pen has somewhere to lift. A trend that never breaks is a trend that cannot show the one
   thing a break is for. */
static void gallery_series(struct inkcell_series *series, struct inkcell_polyline *out,
                           struct inkcell_scale scale) {
    static const int32_t k_values[] = {410, 455, 520, 505, 560, 640, 690, 655, 720,
                                       760, 740, 810, 860, 830, 780, 700, 660, 690};
    inkcell_series_reset(series, 60000U);
    for (size_t i = 0U; i < sizeof k_values / sizeof k_values[0]; ++i) {
        /* A minute and a half of nothing, two thirds of the way along: longer than `gap_ms`,
           so the line breaks here rather than sloping across a silence. */
        const uint32_t when = (uint32_t)(i * 30000U) + (i > 11U ? 90000U : 0U);
        inkcell_series_push(series, when, k_values[i]);
    }
    inkcell_series_project(series, scale, out);
}

void gallery_scene_meters(struct inkcell_draw_state *state) {
    struct inkcell_fb_layout layout = gallery_frame(state, GALLERY_STR_HEAD_METERS, 3U);
    const struct inkcell_fb_row_box box = inkcell_fb_row_box(state);
    const int scale = layout.small;
    const int gap = inkcell_fb_space(state, INKCELL_SPACE_MD);
    const int width = (box.text_right - box.text_x) / 2 - gap;
    int y = layout.body_y;

    y = gallery_section(state, &layout, y, GALLERY_STR_HEAD_METERS);

    /*
     * The bar at four readings against one band. The band is what makes the colour a statement
     * rather than a decoration: the same widget says healthy, then warning, then bad, without
     * the caller choosing a tone for any of them.
     */
    static const struct inkcell_band k_band = {.warn = 700, .bad = 880};
    static const struct inkcell_scale k_permille = {.min = 0, .max = 1000};
    static const int32_t k_readings[] = {250, 620, 780, 940};
    const int thickness = inkcell_fb_meter_thickness(state, scale);
    for (size_t i = 0U; i < sizeof k_readings / sizeof k_readings[0]; ++i) {
        const struct inkcell_fb_meter meter = {
            .rect = {.x = box.text_x, .y = y, .w = width, .h = thickness},
            .id = (uint32_t)(GALLERY_ANIM_METER + i),
            .kind = INKCELL_FB_METER_DETERMINATE,
            .value = k_readings[i],
            .scale = k_permille,
            .band = &k_band,
            .tone = INKCELL_TONE_PRIMARY,
            .ground = INKCELL_COLOR_BG,
        };
        inkcell_fb_draw_meter(state, &meter);
        y += thickness + inkcell_fb_space(state, INKCELL_SPACE_SM);
    }

    /* And the one that is not a reading at all: work is happening and nobody can say how much
       of it is left. It travels, so this picture catches it mid-journey rather than at an end. */
    const struct inkcell_fb_meter busy = {
        .rect = {.x = box.text_x, .y = y, .w = width, .h = thickness},
        .id = GALLERY_ANIM_METER + 16U,
        .kind = INKCELL_FB_METER_INDETERMINATE,
        .scale = k_permille,
        .tone = INKCELL_TONE_PRIMARY,
        .ground = INKCELL_COLOR_BG,
    };
    inkcell_fb_draw_meter(state, &busy);
    y += thickness + gap;

    /* The slider: a meter you can move, so it has a handle and a resting size that does not
       change when the cursor arrives - see the note about the card's focus ring. */
    const int slider_h = inkcell_fb_slider_height(state, scale);
    for (int selected = 0; selected < 2; ++selected) {
        const struct inkcell_fb_slider slider = {
            .rect = {.x = box.text_x + selected * (width + gap), .y = y, .w = width, .h = slider_h},
            .id = (uint32_t)(GALLERY_ANIM_SLIDER + selected),
            .position = 620,
            .stops = 5U,
            .tone = INKCELL_TONE_PRIMARY,
            .selected = selected != 0,
        };
        inkcell_fb_draw_slider(state, &slider);
    }
    y += slider_h + gap;

    /* The staircase, at every level it has. A signal is the one reading drawn as a count rather
       than a length, because that is how every phone has drawn it. */
    const int icon = inkcell_fb_icon_box(state, scale);
    int x = box.text_x;
    for (uint8_t level = 0U; level <= 4U; ++level) {
        const struct inkcell_fb_rect rect = {.x = x, .y = y, .w = icon, .h = icon};
        inkcell_fb_draw_signal(state, &rect, level,
                               inkcell_fb_tone_color(state, INKCELL_TONE_PRIMARY),
                               inkcell_fb_color(state, INKCELL_COLOR_METER_TRACK));
        x += icon + inkcell_fb_space(state, INKCELL_SPACE_SM);
    }

    /* The proportion: a whole divided, rather than a value on a scale. Four parts, because that
       is as many as the theme states categorical fills for. */
    static const uint32_t k_parts[INKCELL_PROPORTION_PARTS] = {45U, 25U, 20U, 10U};
    const struct inkcell_fb_proportion split = {
        .rect = {.x = box.text_x + width + gap,
                 .y = y,
                 .w = width,
                 .h = inkcell_fb_proportion_thickness(state, scale)},
        .values = {k_parts[0], k_parts[1], k_parts[2], k_parts[3]},
        .count = INKCELL_PROPORTION_PARTS,
        .ground = inkcell_fb_color(state, INKCELL_COLOR_BG),
    };
    inkcell_fb_draw_proportion(state, &split);
    y += icon + gap;

    y += inkcell_fb_space(state, INKCELL_SPACE_MD);
    y = gallery_section(state, &layout, y, GALLERY_STR_HEAD_DIALS);

    /*
     * The same four readings as the bar above, against the same band, as rings - which is the
     * point of putting them on one sheet. A quantity that is bad at 94% has to look bad in both
     * shapes, and the two have to agree about where three quarters is.
     */
    const int dial_side = inkcell_fb_line_adv(state, state->scale) * 3;
    int dx = box.text_x;
    for (size_t i = 0U; i < sizeof k_readings / sizeof k_readings[0]; ++i) {
        char figure[16];
        (void)snprintf(figure, sizeof figure, "%d%%", (int)(k_readings[i] / 10));
        const struct inkcell_fb_dial dial = {
            .rect = {.x = dx, .y = y, .w = dial_side, .h = dial_side},
            .id = (uint32_t)(GALLERY_ANIM_DIAL + i),
            .kind = INKCELL_FB_DIAL_DETERMINATE,
            .value = k_readings[i],
            .scale = k_permille,
            .band = &k_band,
            .tone = INKCELL_TONE_PRIMARY,
            .ground = INKCELL_COLOR_BG,
            .label = figure,
        };
        inkcell_fb_draw_dial(state, &dial);
        dx += dial_side + gap;
    }

    /* The spinner, which is the ring's answer to the travelling pill; then one under the cursor,
       so the ground it lays for itself is visible; then one too small for a figure, which draws
       the ring and says nothing rather than a smudge. */
    const struct inkcell_fb_dial busy_dial = {
        .rect = {.x = dx, .y = y, .w = dial_side, .h = dial_side},
        .id = GALLERY_ANIM_DIAL + 16U,
        .kind = INKCELL_FB_DIAL_INDETERMINATE,
        .scale = k_permille,
        .tone = INKCELL_TONE_PRIMARY,
        .ground = INKCELL_COLOR_BG,
    };
    inkcell_fb_draw_dial(state, &busy_dial);
    dx += dial_side + gap;

    const struct inkcell_fb_dial picked = {
        .rect = {.x = dx, .y = y, .w = dial_side, .h = dial_side},
        .id = GALLERY_ANIM_DIAL + 17U,
        .kind = INKCELL_FB_DIAL_DETERMINATE,
        .value = 620,
        .scale = k_permille,
        .band = &k_band,
        .tone = INKCELL_TONE_PRIMARY,
        .selected = true,
        .ground = INKCELL_COLOR_BG,
        .label = "62%",
    };
    inkcell_fb_draw_dial(state, &picked);
    dx += dial_side + gap;

    const int tiny = inkcell_fb_dial_min_side(state, state->scale);
    const struct inkcell_fb_dial small = {
        .rect = {.x = dx, .y = y + (dial_side - tiny) / 2, .w = tiny, .h = tiny},
        .id = GALLERY_ANIM_DIAL + 18U,
        .kind = INKCELL_FB_DIAL_DETERMINATE,
        .value = 450,
        .scale = k_permille,
        .tone = INKCELL_TONE_TERTIARY,
        .ground = INKCELL_COLOR_BG,
        .label = "45%",
    };
    inkcell_fb_draw_dial(state, &small);
    y += dial_side + gap;

    y = gallery_section(state, &layout, y, GALLERY_STR_HEAD_SPARKLINES);

    static struct inkcell_series series;
    static struct inkcell_polyline points;
    gallery_series(&series, &points, k_permille);

    const int spark_h = inkcell_fb_sparkline_height(state, scale);
    for (int selected = 0; selected < 2; ++selected) {
        const struct inkcell_fb_sparkline spark = {
            .rect = {.x = box.text_x + selected * (width + gap), .y = y, .w = width, .h = spark_h},
            .points = &points,
            .tone = INKCELL_TONE_PRIMARY,
            .selected = selected != 0,
        };
        inkcell_fb_draw_sparkline(state, &spark);
    }
    y += spark_h + gap;

    /* And the full chart: the same readings with axes, a legend and a span picker under them. */
    const struct inkcell_fb_segmented spans = {
        .labels = {inkcell_str(INKCELL_STR_TREND_SPAN_15M), inkcell_str(INKCELL_STR_TREND_SPAN_1H),
                   inkcell_str(INKCELL_STR_TREND_SPAN_6H), inkcell_str(INKCELL_STR_TREND_SPAN_ALL)},
        .count = 4U,
        .active = 2U,
    };
    const int chart_h = layout.footer_y - inkcell_fb_gutter(state) - y;
    if (chart_h >= inkcell_fb_chart_min_height(state, &layout)) {
        const struct inkcell_fb_chart chart = {
            .rect = {.x = box.text_x, .y = y, .w = box.text_right - box.text_x, .h = chart_h},
            .lines = {{.points = &points,
                       .label = (enum inkcell_str_id)GALLERY_STR_READ_UTILISATION,
                       .value = "69%"}},
            .count = 1U,
            .top = "100%",
            .bottom = "0%",
            .span = inkcell_str(INKCELL_STR_TREND_SPAN_6H),
            .band = &k_band,
            .scale = k_permille,
            .spans = &spans,
        };
        inkcell_fb_draw_chart(state, &layout, &chart);
    }

    gallery_footer(state, &layout);
}
