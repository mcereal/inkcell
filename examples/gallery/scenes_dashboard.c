#define _POSIX_C_SOURCE 200809L

/*
 * The dashboard: a board of tiles on shared tracks, and cards drawn into the boxes it hands out.
 *
 * Drawn on a wide page, because a board is what a wide surface is for - on the handheld panel the
 * same readings are a column of cards, and this page would be a picture of one track.
 *
 * Three rows, each showing one thing the board is arranged to make true:
 *
 *   - a row of stat tiles of unlike content - a figure alone, one over a banded meter, one over a
 *     trend, one in a warning tone - whose bars all stand on one line because each tile sets its
 *     picture on its own floor;
 *   - a card two tracks wide holding a chart in the room under its rows, beside a card one track
 *     wide, so the wide one's right edge is visibly the boundary the narrow tiles above share;
 *   - three cards that fill the rest of the board, with rows dropped from the one that has more
 *     than its box holds.
 */

#include "gallery.h"

enum {
    GALLERY_ANIM_DASH_METER = 0xDA00,
};

void gallery_scene_dashboard(struct inkcell_draw_state *state) {
    struct inkcell_fb_layout layout = gallery_frame(state, GALLERY_STR_HEAD_DASHBOARD, 3U);
    static const struct inkcell_scale k_permille = {.min = 0, .max = 1000};
    static const struct inkcell_band k_band = {.warn = 250, .bad = 500};

    const struct inkcell_box body = inkcell_fb_full_box(state, &layout);
    const int gap = inkcell_fb_space(state, INKCELL_SPACE_MD);
    const int tile_min = 14 * inkcell_fb_char_adv(state, state->scale);
    struct inkcell_dash dash;
    inkcell_dash_begin(&dash, body, inkcell_dash_columns(body.w, tile_min, gap, 4U), gap);

    /* A plausible trend: a reading that climbs, breaks and recovers. */
    static struct inkcell_series series;
    static struct inkcell_polyline points;
    static const int32_t k_values[] = {120, 140, 190, 170, 210, 260, 240, 300, 280, 330, 310, 360};
    inkcell_series_reset(&series, 60000U);
    for (size_t i = 0U; i < sizeof k_values / sizeof k_values[0]; ++i) {
        inkcell_series_push(&series, (uint32_t)(i * 30000U), k_values[i]);
    }
    /* Twice: the tile's line on a domain that shows its shape, and the chart's on the one its
       band is stated in - a band on one domain over a line on another is the chart being wrong
       quietly. */
    static const struct inkcell_scale k_trend = {.min = 0, .max = 400};
    inkcell_series_project(&series, k_trend, &points);
    static struct inkcell_polyline chart_points;
    inkcell_series_project(&series, k_permille, &chart_points);

    struct inkcell_fb_stat tiles[] = {
        {.variant = INKCELL_FB_CARD_ELEVATED,
         .icon = INKCELL_ICON_NETWORK,
         .label = gallery_id(GALLERY_STR_READ_PEERS),
         .value = "9 / 42",
         .caption = gallery_text(GALLERY_STR_ROW_CHECKED_AGO),
         .tone = INKCELL_TONE_SUCCESS},
        {.variant = INKCELL_FB_CARD_FILLED,
         .label = gallery_id(GALLERY_STR_READ_UTILISATION),
         .value = "31%",
         .tone = INKCELL_TONE_WARNING,
         .picture = INKCELL_FB_STAT_METER,
         .meter_value = 310,
         .meter_scale = k_permille,
         .meter_band = &k_band,
         .meter_id = GALLERY_ANIM_DASH_METER},
        {.variant = INKCELL_FB_CARD_FILLED,
         .label = gallery_id(GALLERY_STR_READ_SIGNAL),
         .value = "-71 dBm",
         .caption = "+6.5 dB",
         .picture = INKCELL_FB_STAT_TREND,
         .trend = &points},
        {.variant = INKCELL_FB_CARD_OUTLINED,
         .icon = INKCELL_ICON_POWER,
         .label = gallery_id(GALLERY_STR_READ_UPTIME),
         .value = "9d 8h"},
    };
    const size_t tile_count = sizeof tiles / sizeof tiles[0];
    const uint32_t per_row = dash.columns < tile_count ? dash.columns : (uint32_t)tile_count;

    /* The row is as tall as its tallest tile, measured at the width it will actually get. */
    int tile_h = 0;
    for (size_t i = 0U; i < tile_count; ++i) {
        tiles[i].rect.w = (dash.edges[1] - dash.edges[0]);
        const int h = inkcell_fb_stat_height(state, &layout, &tiles[i]);
        tile_h = h > tile_h ? h : tile_h;
    }
    for (size_t i = 0U; i < tile_count; ++i) {
        if (i % per_row == 0U && !inkcell_dash_row(&dash, tile_h)) {
            break;
        }
        const struct inkcell_box cell = inkcell_dash_cell(&dash, dash.columns / per_row);
        tiles[i].rect = (struct inkcell_fb_rect){cell.x, cell.y, cell.w, cell.h};
        inkcell_fb_draw_stat(state, &layout, &tiles[i]);
    }

    /* A chart in a card two tracks wide, and a card of readings beside it. */
    const int chart_row = inkcell_dash_left(&dash) * 3 / 5;
    if (inkcell_dash_row(&dash, chart_row)) {
        const uint32_t wide_span = dash.columns > 1U ? dash.columns - 1U : 1U;
        struct inkcell_fb_card frame;
        inkcell_fb_card_begin(&frame, INKCELL_FB_CARD_FILLED, INKCELL_ICON_NONE,
                              gallery_id(GALLERY_STR_READ_UTILISATION), INKCELL_TONE_WARNING);
        inkcell_fb_card_action(&frame, gallery_id(GALLERY_STR_ACT_OPEN), false);
        const struct inkcell_box room =
            inkcell_fb_draw_card_in(state, &layout, inkcell_dash_cell(&dash, wide_span), &frame);
        if (room.h >= inkcell_fb_chart_min_height(state, &layout)) {
            const struct inkcell_fb_chart chart = {
                .rect = {room.x, room.y, room.w, room.h},
                .lines = {{.points = &chart_points, .area = true, .value = "31%"}},
                .count = 1U,
                .top = "100%",
                .bottom = "0%",
                .band = &k_band,
                .scale = k_permille,
            };
            inkcell_fb_draw_chart(state, &layout, &chart);
        }

        struct inkcell_fb_card side;
        inkcell_fb_card_begin(&side, INKCELL_FB_CARD_FILLED, INKCELL_ICON_POWER,
                              gallery_id(GALLERY_STR_READ_TRAFFIC), INKCELL_TONE_NORMAL);
        inkcell_fb_card_row_text(&side, INKCELL_TONE_NORMAL, gallery_id(GALLERY_STR_ROW_BATTERY),
                                 "64%");
        static const uint32_t k_parts[] = {45U, 25U, 20U, 10U};
        inkcell_fb_card_proportion(&side, INKCELL_TONE_NORMAL, INKCELL_STR_NONE, k_parts,
                                   (uint32_t)(sizeof k_parts / sizeof k_parts[0]));
        inkcell_fb_card_note(&side, INKCELL_TONE_DIM, gallery_text(GALLERY_STR_NOTE_WRAPPED));
        (void)inkcell_fb_draw_card_in(state, &layout, inkcell_dash_cell(&dash, 1U), &side);
    }

    /* The rest of the board, in cards that share it - the last one holding more rows than fit. */
    if (inkcell_dash_row(&dash, inkcell_dash_left(&dash))) {
        for (uint32_t i = 0U; i < dash.columns; ++i) {
            struct inkcell_fb_card card;
            inkcell_fb_card_begin(
                &card, INKCELL_FB_CARD_FILLED, INKCELL_ICON_NONE,
                gallery_id(i == 0U ? GALLERY_STR_ROW_NETWORK : GALLERY_STR_ROW_STORAGE),
                INKCELL_TONE_NORMAL);
            const uint32_t rows = 2U + i * 5U;
            for (uint32_t r = 0U; r < rows; ++r) {
                inkcell_fb_card_row_text(&card, INKCELL_TONE_NORMAL,
                                         gallery_id(GALLERY_STR_READ_SIGNAL), "-71 dBm");
            }
            (void)inkcell_fb_draw_card_in(state, &layout, inkcell_dash_cell(&dash, 1U), &card);
        }
    }

    gallery_footer(state, &layout);
}
