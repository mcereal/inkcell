#define _POSIX_C_SOURCE 200809L

/*
 * The list row, and everything that can stand in its two slots.
 *
 * This is the busiest sheet because the row is the busiest component: nearly every screen a
 * handheld shows is a column of these, and a row is a leading slot, a label, a value and a
 * trailing slot, any of which may be absent. What the picture is for is the *alignment* - the
 * labels of eleven rows carrying eleven different trailing widgets have to form one column, and
 * the widgets have to form another, or a settings screen reads as a ransom note.
 *
 * The cursor sits on a row in the middle rather than on the first, so that the fill is visible
 * against rows above and below it rather than against the chrome.
 */

#include "gallery.h"

enum {
    GALLERY_ANIM_ROW_SWITCH = 0x1100,
    GALLERY_ANIM_ROW_SEL = 0x1120,
    GALLERY_ANIM_ROW_METER = 0x1140,
};

void gallery_scene_list(struct inkcell_backend_fb_state *state) {
    struct inkcell_fb_layout layout = gallery_frame(state, GALLERY_STR_HEAD_ROWS, 1U);
    const int scale = layout.small;

    /* The mutable widgets a trailing slot borrows. They live for the frame, and the animation
       positions behind them live on the state keyed by the ids above. */
    struct inkcell_fb_switch row_switch = {
        .id = GALLERY_ANIM_ROW_SWITCH,
        .family = INKCELL_FAMILY_PRIMARY,
        .on = true,
    };
    struct inkcell_fb_selection row_check = {
        .id = GALLERY_ANIM_ROW_SEL,
        .shape = INKCELL_FB_SELECTION_CHECKBOX,
        .family = INKCELL_FAMILY_PRIMARY,
        .on = true,
    };
    static const struct inkcell_scale k_permille = {.min = 0, .max = 1000};
    static const struct inkcell_band k_band = {.warn = 700, .bad = 880};
    struct inkcell_fb_meter row_meter = {
        .id = GALLERY_ANIM_ROW_METER,
        .kind = INKCELL_FB_METER_DETERMINATE,
        .value = 640,
        .scale = k_permille,
        .band = &k_band,
        .tone = INKCELL_TONE_PRIMARY,
    };

    /*
     * One row per thing a row can be. Index 0 is a subheader rather than a row, which is how a
     * list groups itself: the subheader is a row of the same list and takes its turn from the
     * same window, so a group heading scrolls with what it heads.
     */
    const struct inkcell_fb_list_item items[] = {
        /* A plain row with a value: the commonest thing a settings screen holds. */
        {.text = gallery_text(GALLERY_STR_ROW_LANGUAGE),
         .value = gallery_text(GALLERY_STR_VAL_ENGLISH),
         .leading = {.kind = INKCELL_FB_LEADING_ICON, .icon = INKCELL_ICON_LANGUAGE},
         .trailing = {.kind = INKCELL_FB_TRAILING_ICON, .icon = INKCELL_ICON_CHEVRON}},
        /* A switch in the trailing slot: the row *is* the control, and pressing it toggles. */
        {.text = gallery_text(GALLERY_STR_ROW_NOTIFICATIONS),
         .leading = {.kind = INKCELL_FB_LEADING_ICON, .icon = INKCELL_ICON_MESSAGES},
         .trailing = {.kind = INKCELL_FB_TRAILING_SWITCH, .sw = &row_switch}},
        /* A supporting line under the label: two lines' worth of row. */
        {.text = gallery_text(GALLERY_STR_ROW_UPDATES),
         .supporting = gallery_text(GALLERY_STR_VAL_EVERY_HOUR),
         .leading = {.kind = INKCELL_FB_LEADING_ICON, .icon = INKCELL_ICON_DOWNLOAD},
         .trailing = {.kind = INKCELL_FB_TRAILING_BADGE,
                      .text = "2",
                      .family = INKCELL_FAMILY_ERROR}},
        /* A label longer than its column, so that the picture shows what clipping looks like
           rather than leaving it to be discovered on a device. */
        {.text = gallery_text(GALLERY_STR_ROW_LONG),
         .leading = {.kind = INKCELL_FB_LEADING_ICON, .icon = INKCELL_ICON_REFRESH},
         .trailing = {.kind = INKCELL_FB_TRAILING_CHECKBOX, .sel = &row_check}},
        /* A reading in the trailing slot, as a length and then as a staircase. */
        {.text = gallery_text(GALLERY_STR_ROW_BATTERY),
         .value = "64%",
         .leading = {.kind = INKCELL_FB_LEADING_ICON, .icon = INKCELL_ICON_POWER},
         .trailing = {.kind = INKCELL_FB_TRAILING_METER, .meter = &row_meter}},
        {.text = gallery_text(GALLERY_STR_ROW_NETWORK),
         .value = "-71 dBm",
         .leading = {.kind = INKCELL_FB_LEADING_ICON, .icon = INKCELL_ICON_NETWORK},
         .trailing = {.kind = INKCELL_FB_TRAILING_SIGNAL, .signal = 3U}},
        /* A tonal leading slot: a letter in a filled disc, which is what a list of things with
           no picture of their own looks like. */
        {.text = gallery_text(GALLERY_STR_ROW_STORAGE),
         .value = "12.4 GB",
         .leading = {.kind = INKCELL_FB_LEADING_TONAL,
                     .label = "S",
                     .role = INKCELL_COLOR_TERTIARY_CONTAINER},
         .trailing = {.kind = INKCELL_FB_TRAILING_TEXT, .text = gallery_text(GALLERY_STR_VAL_ON)}},
        /* An accent edge: the row is the one the screen is about. */
        {.text = gallery_text(GALLERY_STR_ROW_SECURITY),
         .leading = {.kind = INKCELL_FB_LEADING_ICON, .icon = INKCELL_ICON_SECURITY},
         .accent_edge = true,
         .divider = true,
         .trailing = {.kind = INKCELL_FB_TRAILING_ICON, .icon = INKCELL_ICON_CHEVRON}},
        {.text = gallery_text(GALLERY_STR_ROW_SOUND),
         .tone = INKCELL_TONE_DIM,
         .leading = {.kind = INKCELL_FB_LEADING_ICON, .icon = INKCELL_ICON_EXT_NOTIFY},
         .trailing = {.kind = INKCELL_FB_TRAILING_TEXT, .text = gallery_text(GALLERY_STR_VAL_OFF)}},
    };
    const uint32_t item_count = (uint32_t)(sizeof items / sizeof items[0]);

    /*
     * The heights the model places rows against: one body row each, except the note, which is
     * as many rows as its words wrap to, and the row carrying a supporting line, which is two.
     *
     * This is what inkcell_fb_list_begin_heights() is for, and getting it wrong is the failure
     * it exists to prevent - a note drawn taller than the height the model gave it paints over
     * the row beneath, and every row below sits at the wrong offset. Measure with
     * inkcell_fb_list_note_steps() and hand the same number to both.
     */
    const uint32_t note_steps = inkcell_fb_list_note_steps(
        state, gallery_text(GALLERY_STR_HEAD_ROWS), gallery_text(GALLERY_STR_NOTE_WRAPPED));

    enum { GALLERY_ROW_SUBHEADER = 0U, GALLERY_ROW_NOTE = 1U, GALLERY_ROW_FIRST_ITEM = 2U };
    const uint32_t count = GALLERY_ROW_FIRST_ITEM + item_count;

    uint8_t heights[GALLERY_ROW_FIRST_ITEM + (sizeof items / sizeof items[0])];
    heights[GALLERY_ROW_SUBHEADER] = 1U;
    heights[GALLERY_ROW_NOTE] = (uint8_t)note_steps;
    for (uint32_t i = 0U; i < item_count; ++i) {
        /* Two rows for the one that carries a supporting line under its label; one for the
           rest. A row handed one row and asked to draw two draws one. */
        heights[GALLERY_ROW_FIRST_ITEM + i] = items[i].supporting != NULL ? 2U : 1U;
    }

    struct inkcell_fb_list list =
        inkcell_fb_list_begin_heights(&layout, count, GALLERY_ROW_FIRST_ITEM + 2U, heights);
    uint32_t index = 0U;
    while (inkcell_fb_list_next(&list, &index)) {
        if (index == GALLERY_ROW_SUBHEADER) {
            inkcell_fb_list_subheader_icon(
                state, &list, index, gallery_text(GALLERY_STR_HEAD_ROWS),
                (struct inkcell_fb_leading){.kind = INKCELL_FB_LEADING_ICON,
                                            .icon = INKCELL_ICON_SETTINGS});
        } else if (index == GALLERY_ROW_NOTE) {
            inkcell_fb_list_note(state, &list, index, gallery_text(GALLERY_STR_HEAD_ROWS),
                                 gallery_text(GALLERY_STR_NOTE_WRAPPED));
        } else {
            inkcell_fb_list_item(state, &list, index, &items[index - GALLERY_ROW_FIRST_ITEM]);
        }
    }

    (void)scale;
    gallery_footer(state, &layout);
}
