#define _POSIX_C_SOURCE 200809L

/*
 * The controls: a setting you change rather than a verb you press.
 *
 * Each one is drawn four ways - off and on, at rest and under the cursor - because those four
 * are what a control *is*. A switch that only ever appears on is a switch nobody can tell from
 * a badge, and two of the four states are the ones a theme most often gets wrong: a control
 * that is on and not focused has to read as on against the body ground, and a control that is
 * focused and off has to read as off against the cursor fill.
 *
 * They take `state` mutably because they slide. Where the knob has got to is kept on the state,
 * keyed by the control's own id - see include/inkcell/ui/anim.h - which is what lets a screen
 * renderer be rebuilt from nothing every frame and still animate.
 */

#include "gallery.h"

/*
 * Ids for the animation table, one block per control with room inside it for the four cells.
 *
 * Non-zero, because 0 is not a key - a control keyed on 0 reports its target unanimated. And
 * spaced, because the first draft of this file spaced them by one and the checkbox that is off
 * and focused drew itself checked: it had collided with a switch that was on, read that
 * switch's knob position out of the shared slot, and believed it. An id is identity, and two
 * controls sharing one is two controls sharing a position.
 */
enum {
    GALLERY_ANIM_SWITCH = 0x5700,
    GALLERY_ANIM_CHECK = 0x5720,
    GALLERY_ANIM_RADIO = 0x5740,
};

void gallery_scene_controls(struct inkcell_draw_state *state) {
    struct inkcell_fb_layout layout = gallery_frame(state, GALLERY_STR_HEAD_CONTROLS, 2U);
    const struct inkcell_fb_row_box box = inkcell_fb_row_box(state);
    const int scale = layout.small;
    const int gap = inkcell_fb_space(state, INKCELL_SPACE_MD);
    int y = layout.body_y;

    y = gallery_section(state, &layout, y, GALLERY_STR_HEAD_SWITCHES);

    int sw_w = 0;
    int sw_h = 0;
    inkcell_fb_switch_size(state, scale, &sw_w, &sw_h);
    int x = box.text_x;
    for (int on = 0; on < 2; ++on) {
        for (int focused = 0; focused < 2; ++focused) {
            const struct inkcell_fb_switch sw = {
                .rect = {.x = x, .y = y, .w = sw_w, .h = sw_h},
                /* One id per cell, so the four do not fight over one slot. */
                .id = (uint32_t)(GALLERY_ANIM_SWITCH + on * 2 + focused),
                .family = INKCELL_FAMILY_PRIMARY,
                .on = on != 0,
                .focused = focused != 0,
                .ground = INKCELL_COLOR_BG,
            };
            inkcell_fb_draw_switch(state, &sw);
            x += sw_w + gap;
        }
    }
    y += sw_h + gap;

    y = gallery_section(state, &layout, y, GALLERY_STR_HEAD_SELECTION);

    int sel_w = 0;
    int sel_h = 0;
    inkcell_fb_selection_size(state, scale, &sel_w, &sel_h);
    x = box.text_x;
    static const enum inkcell_fb_selection_shape k_shapes[] = {INKCELL_FB_SELECTION_CHECKBOX,
                                                               INKCELL_FB_SELECTION_RADIO};
    for (size_t s = 0U; s < sizeof k_shapes / sizeof k_shapes[0]; ++s) {
        for (int on = 0; on < 2; ++on) {
            for (int focused = 0; focused < 2; ++focused) {
                const struct inkcell_fb_selection sel = {
                    .rect = {.x = x, .y = y, .w = sel_w, .h = sel_h},
                    .id = (uint32_t)((k_shapes[s] == INKCELL_FB_SELECTION_CHECKBOX
                                          ? GALLERY_ANIM_CHECK
                                          : GALLERY_ANIM_RADIO) +
                                     on * 2 + focused),
                    .shape = k_shapes[s],
                    .family = INKCELL_FAMILY_PRIMARY,
                    .on = on != 0,
                    .focused = focused != 0,
                };
                inkcell_fb_draw_selection(state, &sel);
                x += sel_w + gap;
            }
        }
        x += gap;
    }
    y += sel_h + gap;

    /* The segmented button: one choice out of a few, all of them visible. It is the control a
       chart's span picker is, and it is here at both widths a strip is drawn at. */
    const struct inkcell_fb_segmented spans = {
        .labels = {inkcell_str(INKCELL_STR_TREND_SPAN_15M), inkcell_str(INKCELL_STR_TREND_SPAN_1H),
                   inkcell_str(INKCELL_STR_TREND_SPAN_6H), inkcell_str(INKCELL_STR_TREND_SPAN_ALL)},
        .count = 4U,
        .active = 1U,
    };
    const int seg_w = inkcell_fb_segmented_width(state, &spans, scale);
    const int seg_h = inkcell_fb_segmented_height(state, scale);
    for (int focused = 0; focused < 2; ++focused) {
        const struct inkcell_fb_rect rect = {
            .x = box.text_x + focused * (seg_w + gap), .y = y, .w = seg_w, .h = seg_h};
        inkcell_fb_draw_segmented(state, &rect, &spans, focused != 0, INKCELL_COLOR_BG, scale);
    }
    y += seg_h + gap;

    y = gallery_section(state, &layout, y, GALLERY_STR_HEAD_FIELDS);

    /* The field, in the three states it has: resting, being typed into, and refused. The caret
       is drawn rather than blinked - a blinking caret in a still picture is a caret that is
       there half the time the picture is taken. */
    const struct inkcell_fb_text_field resting = {
        .label = gallery_text(GALLERY_STR_FIELD_LABEL),
        .value = gallery_text(GALLERY_STR_FIELD_VALUE),
        .lines = 1U,
    };
    inkcell_fb_draw_text_field(state, &layout, &y, &resting);
    y += inkcell_fb_space(state, INKCELL_SPACE_SM);

    const struct inkcell_fb_text_field typing = {
        .label = gallery_text(GALLERY_STR_FIELD_LABEL),
        .value = gallery_text(GALLERY_STR_FIELD_VALUE),
        .caret = true,
        .lines = 2U,
        .counter = "29/160",
    };
    inkcell_fb_draw_text_field(state, &layout, &y, &typing);
    y += inkcell_fb_space(state, INKCELL_SPACE_SM);

    const struct inkcell_fb_text_field refused = {
        .label = gallery_text(GALLERY_STR_FIELD_LABEL),
        .value = gallery_text(GALLERY_STR_FIELD_VALUE),
        .lines = 1U,
        .counter = "200/160",
        .error = true,
    };
    inkcell_fb_draw_text_field(state, &layout, &y, &refused);

    gallery_footer(state, &layout);
}
