#define _POSIX_C_SOURCE 200809L

/*
 * The scaffold: one screen, drawn by inkcell_fb_scaffold_begin(), at whatever width the page is.
 *
 * The same scene is three pictures depending on the room it is given, which is the assertion.
 * On the handheld panel at the body scale it is compact and the destinations are a bar across
 * the bottom; at the smaller scale the same panel is medium and they are a rail; on the wide page
 * it is expanded, the rail stays and the screen's list and its detail stand side by side. Nothing
 * in the scene branches on any of that - the screen says it *has* a detail and the scaffold
 * decides whether there is room to show it.
 *
 * The body is drawn with the ordinary list calls against the layout the scaffold hands back, and
 * that is the other half of the assertion: rows that land inside the rail, or a detail pane whose
 * rows start at the panel's margin rather than its own, are a region the widgets did not honour.
 */

#include "gallery.h"

#include "inkcell/ui/actions.h"

static const struct inkcell_fb_list_item k_settings[] = {
    {.leading = {.kind = INKCELL_FB_LEADING_ICON, .icon = INKCELL_ICON_DISPLAY},
     .trailing = {.kind = INKCELL_FB_TRAILING_ICON, .icon = INKCELL_ICON_CHEVRON}},
    {.leading = {.kind = INKCELL_FB_LEADING_ICON, .icon = INKCELL_ICON_LANGUAGE},
     .trailing = {.kind = INKCELL_FB_TRAILING_ICON, .icon = INKCELL_ICON_CHEVRON}},
    {.leading = {.kind = INKCELL_FB_LEADING_ICON, .icon = INKCELL_ICON_EXT_NOTIFY},
     .trailing = {.kind = INKCELL_FB_TRAILING_ICON, .icon = INKCELL_ICON_CHEVRON}},
    {.leading = {.kind = INKCELL_FB_LEADING_ICON, .icon = INKCELL_ICON_POWER},
     .trailing = {.kind = INKCELL_FB_TRAILING_ICON, .icon = INKCELL_ICON_CHEVRON}},
    {.leading = {.kind = INKCELL_FB_LEADING_ICON, .icon = INKCELL_ICON_SECURITY},
     .trailing = {.kind = INKCELL_FB_TRAILING_ICON, .icon = INKCELL_ICON_CHEVRON}},
};
static const enum gallery_str_id k_setting_names[] = {
    GALLERY_STR_ROW_DISPLAY, GALLERY_STR_ROW_LANGUAGE, GALLERY_STR_ROW_NOTIFICATIONS,
    GALLERY_STR_ROW_BATTERY, GALLERY_STR_ROW_SECURITY,
};
#define GALLERY_SETTINGS_COUNT (sizeof k_settings / sizeof k_settings[0])

/* The list pane: the sections, with the cursor on the one the detail is showing. */
static void scaffold_list(struct inkcell_draw_state *state,
                          const struct inkcell_fb_layout *layout) {
    struct inkcell_fb_list_item items[GALLERY_SETTINGS_COUNT];
    for (size_t i = 0U; i < GALLERY_SETTINGS_COUNT; ++i) {
        items[i] = k_settings[i];
        items[i].text = gallery_text(k_setting_names[i]);
    }
    struct inkcell_fb_list list =
        inkcell_fb_list_begin(layout, (uint32_t)GALLERY_SETTINGS_COUNT, 0U);
    uint32_t index = 0U;
    while (inkcell_fb_list_next(&list, &index)) {
        inkcell_fb_list_item(state, &list, index, &items[index]);
    }
}

/* The detail pane: what the selected section says. Words rather than rows, because a pane the
   cursor is not in has no row to be standing on - and a list model always has one. */
static void scaffold_detail(struct inkcell_draw_state *state,
                            const struct inkcell_fb_layout *layout) {
    (void)inkcell_fb_draw_wrapped_at(
        state, layout->body_x, layout->body_y + inkcell_step_px(state->scale),
        gallery_text(GALLERY_STR_NOTE_WRAPPED), (size_t)layout->body_w, (int)layout->rows,
        inkcell_fb_color(state, INKCELL_COLOR_TEXT), inkcell_fb_color(state, INKCELL_COLOR_BG));
}

static void scaffold_scene(struct inkcell_draw_state *state, enum inkcell_fb_compact_nav compact) {
    inkcell_fb_clear(state, inkcell_fb_color(state, INKCELL_COLOR_BG));

    const struct inkcell_fb_chip destinations[] = {
        {.icon = INKCELL_ICON_MESSAGES,
         .label = gallery_text(GALLERY_STR_TAB_COMPONENTS),
         .badge = "3"},
        {.icon = INKCELL_ICON_NODES, .label = gallery_text(GALLERY_STR_TAB_LISTS)},
        {.icon = INKCELL_ICON_DISPLAY, .label = gallery_text(GALLERY_STR_TAB_CONTROLS)},
        {.icon = INKCELL_ICON_TELEMETRY,
         .label = gallery_text(GALLERY_STR_TAB_READINGS),
         .badge = "12"},
        {.icon = INKCELL_ICON_SETTINGS, .label = gallery_text(GALLERY_STR_HEAD_SETTINGS)},
    };
    const struct inkcell_fb_app_bar list_bar = {.title = gallery_text(GALLERY_STR_HEAD_SETTINGS)};
    const struct inkcell_fb_scaffold scaffold = {
        .destinations = destinations,
        .count = sizeof destinations / sizeof destinations[0],
        .active = 4U,
        .compact_nav = compact,
        .footer = true,
        .split = true,
        .busy = true,
        .app_bar = &list_bar,
    };

    struct inkcell_fb_scaffold_frame frame;
    inkcell_fb_scaffold_begin(state, &scaffold, &frame);
    scaffold_list(state, &frame.layout);
    if (frame.split) {
        const struct inkcell_fb_app_bar detail_bar = {
            .trail = {gallery_text(GALLERY_STR_HEAD_SETTINGS)},
            .trail_count = 1U,
            .title = gallery_text(GALLERY_STR_ROW_DISPLAY),
        };
        const struct inkcell_fb_layout detail =
            inkcell_fb_scaffold_detail(state, &frame, &detail_bar);
        scaffold_detail(state, &detail);
    }

    static const struct inkcell_button_action k_items[] = {
        {INKCELL_BUTTON_A, (inkcell_str_id)GALLERY_STR_ACT_OPEN},
        {INKCELL_BUTTON_B, (inkcell_str_id)GALLERY_STR_ACT_BACK},
        {INKCELL_BUTTON_UP_DOWN, (inkcell_str_id)GALLERY_STR_ACT_SELECT},
    };
    const struct inkcell_fb_action_bar actions = {
        .items = k_items,
        .count = sizeof k_items / sizeof k_items[0],
        .status = gallery_text(GALLERY_STR_BANNER_SUPPORTING),
        .status_tone = INKCELL_TONE_DIM,
    };
    inkcell_fb_scaffold_end(state, &frame, &actions);
}

void gallery_scene_scaffold(struct inkcell_draw_state *state) {
    scaffold_scene(state, INKCELL_FB_COMPACT_NAV_BOTTOM);
}

void gallery_scene_scaffold_top(struct inkcell_draw_state *state) {
    scaffold_scene(state, INKCELL_FB_COMPACT_NAV_TOP);
}
