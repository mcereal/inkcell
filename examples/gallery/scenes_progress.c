#define _POSIX_C_SOURCE 200809L

/*
 * A job in stages, as the screen that is about it.
 *
 * The dial and the steps are drawn together because that is the only way either is used for a
 * long job: the ring says how far through this stage, the row says which stage of how many.
 * Two columns - one under way, one that stopped - because the thing worth checking is that a
 * failure changes the ring, the marker and nothing behind it.
 */

#include "gallery.h"

#include <stdio.h>

enum {
    GALLERY_ANIM_PROGRESS = 0x5000,
};

static void gallery_progress_column(struct inkcell_draw_state *state, int x, int w, int top,
                                    int bottom, int32_t permille, size_t current, bool halted,
                                    uint32_t id, enum gallery_str_id status) {
    static const enum gallery_str_id k_steps[] = {
        GALLERY_STR_STEP_DOWNLOAD,
        GALLERY_STR_STEP_VERIFY,
        GALLERY_STR_STEP_INSTALL,
        GALLERY_STR_STEP_FINISH,
    };
    const char *labels[sizeof k_steps / sizeof k_steps[0]];
    for (size_t i = 0U; i < sizeof k_steps / sizeof k_steps[0]; ++i) {
        labels[i] = gallery_text(k_steps[i]);
    }

    const int gap = inkcell_fb_space(state, INKCELL_SPACE_MD);
    const struct inkcell_type_style title = inkcell_fb_type_style(state, INKCELL_TYPE_TITLE);
    const int title_h = inkcell_scale_px((int)inkcell_fb_font(state)->height, title.scale);
    const int steps_h = inkcell_fb_steps_height(state, state->scale);

    /* The ring takes what is left once the status line and the steps have their room. */
    int side = bottom - top - title_h - steps_h - 2 * gap;
    side = side < w ? side : w;

    char figure[16];
    (void)snprintf(figure, sizeof figure, "%d%%", (int)(permille / 10));
    const struct inkcell_fb_dial dial = {
        .rect = {.x = x + (w - side) / 2, .y = top, .w = side, .h = side},
        .id = id,
        .kind = INKCELL_FB_DIAL_DETERMINATE,
        .value = permille,
        .scale = {.min = 0, .max = 1000},
        .tone = halted ? INKCELL_TONE_ERROR : INKCELL_TONE_PRIMARY,
        .ground = INKCELL_COLOR_BG,
        /* A job that stopped has no reading worth the middle of the ring; it says so. */
        .label = halted ? NULL : figure,
        .icon = halted ? INKCELL_ICON_CLOSE : INKCELL_ICON_NONE,
    };
    inkcell_fb_draw_dial(state, &dial);

    int y = top + side + gap;
    const char *line = gallery_text(status);
    const int line_w = inkcell_fb_text_width_styled(state, line, &title);
    inkcell_fb_draw_text_styled(state, x + (w - line_w) / 2, y, line, &title,
                                inkcell_fb_color(state, INKCELL_COLOR_TEXT),
                                inkcell_fb_color(state, INKCELL_COLOR_BG));
    y += title_h + gap;

    const struct inkcell_fb_steps steps = {
        .rect = {.x = x, .y = y, .w = w, .h = steps_h},
        .labels = labels,
        .count = sizeof labels / sizeof labels[0],
        .current = current,
        .tone = INKCELL_TONE_PRIMARY,
        .halted = halted,
        .ground = INKCELL_COLOR_BG,
    };
    inkcell_fb_draw_steps(state, &steps);
}

void gallery_scene_progress(struct inkcell_draw_state *state) {
    struct inkcell_fb_layout layout = gallery_frame(state, GALLERY_STR_HEAD_PROGRESS, 3U);
    const struct inkcell_fb_row_box box = inkcell_fb_row_box(state);
    const int gap = inkcell_fb_space(state, INKCELL_SPACE_LG);
    const int width = (box.text_right - box.text_x - gap) / 2;
    const int top = layout.body_y + inkcell_fb_space(state, INKCELL_SPACE_MD);
    const int bottom = layout.footer_y - inkcell_fb_space(state, INKCELL_SPACE_MD);

    gallery_progress_column(state, box.text_x, width, top, bottom, 620, 2U, false,
                            GALLERY_ANIM_PROGRESS, GALLERY_STR_PROGRESS_UNDER_WAY);
    gallery_progress_column(state, box.text_x + width + gap, width, top, bottom, 410, 2U, true,
                            GALLERY_ANIM_PROGRESS + 1U, GALLERY_STR_PROGRESS_STOPPED);
    gallery_footer(state, &layout);
}
