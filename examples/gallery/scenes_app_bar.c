#define _POSIX_C_SOURCE 200809L

/*
 * The app bar's verbs and modes, and the compact footer under them.
 *
 * Every heading a screen can open with, stacked in one frame so they can be compared: a bar
 * with an emphasized verb, plain verbs, a status mark and an overflow button; the same bar as a
 * search field, typing and at rest; a selection; and a large title carrying the same verbs.
 * Under them is the compact footer - two presses and the cap that leads to the rest - with its
 * first-use tip resting above it.
 *
 * The table renders this twice, the second time on a narrow page. That is the picture the
 * component exists for: the same arrays, the same code, and the verbs falling into the menu from
 * the end of the list while the emphasized one keeps its place.
 */

#include "gallery.h"

#include "inkcell/ui/actions.h"

/* Focus ids, from a block of their own so none collides with the cursor on any other page. */
enum {
    APP_BAR_ID_OVERFLOW = 700,
    APP_BAR_ID_COMPOSE,
    APP_BAR_ID_SHARE,
    APP_BAR_ID_REFRESH,
    APP_BAR_ID_DELETE,
    APP_BAR_ID_SETTINGS,
    APP_BAR_ID_HELP,
    APP_BAR_ID_FIELD,
    APP_BAR_ID_CLEAR,
    APP_BAR_ID_PIN,
    APP_BAR_ID_MUTE,
};

/* The air between two specimens, so a selection's fill is not read as belonging to the bar
   above it. */
static void app_bar_gap(const struct inkcell_draw_state *state, struct inkcell_fb_layout *layout) {
    layout->body_y += inkcell_fb_space(state, INKCELL_SPACE_MD);
    layout->rows = inkcell_fb_layout_rows(state, layout);
}

void gallery_scene_app_bar(struct inkcell_draw_state *state) {
    inkcell_fb_clear(state, inkcell_fb_color(state, INKCELL_COLOR_BG));
    struct inkcell_fb_layout layout =
        inkcell_fb_layout_begin_footer(state, INKCELL_FB_FOOTER_COMPACT, true);

    const struct inkcell_fb_chip tabs[] = {
        {.icon = INKCELL_ICON_MESSAGES, .label = gallery_text(GALLERY_STR_TAB_COMPONENTS)},
        {.icon = INKCELL_ICON_NODES, .label = gallery_text(GALLERY_STR_TAB_LISTS)},
        {.icon = INKCELL_ICON_DISPLAY, .label = gallery_text(GALLERY_STR_TAB_CONTROLS)},
        {.icon = INKCELL_ICON_TELEMETRY, .label = gallery_text(GALLERY_STR_TAB_READINGS)},
        {.icon = INKCELL_ICON_ABOUT, .label = gallery_text(GALLERY_STR_TAB_ABOUT)},
    };
    inkcell_fb_draw_nav_bar(state, &layout, tabs, sizeof tabs / sizeof tabs[0], 0U);

    /* One array for a conversation's verbs, in priority order: the one the screen is for, then
       the plain ones, then two that only ever live in the menu. One of them is under the cursor,
       so the page shows what a focused icon looks like. */
    const struct inkcell_fb_bar_action actions[] = {
        {.icon = INKCELL_ICON_COMPOSE,
         .label = gallery_text(GALLERY_STR_ACT_REPLY),
         .emphasized = true,
         .focus_id = APP_BAR_ID_COMPOSE},
        {.icon = INKCELL_ICON_SHARE,
         .label = gallery_text(GALLERY_STR_ACT_SHARE),
         .focused = true,
         .focus_id = APP_BAR_ID_SHARE},
        {.icon = INKCELL_ICON_REFRESH,
         .label = gallery_text(GALLERY_STR_ACT_REFRESH),
         .focus_id = APP_BAR_ID_REFRESH},
        {.icon = INKCELL_ICON_DELETE,
         .label = gallery_text(GALLERY_STR_ACT_DELETE),
         .focus_id = APP_BAR_ID_DELETE},
        {.label = gallery_text(GALLERY_STR_ACT_SETTINGS), .focus_id = APP_BAR_ID_SETTINGS},
        {.label = gallery_text(GALLERY_STR_ACT_HELP), .focus_id = APP_BAR_ID_HELP},
    };
    const size_t action_count = sizeof actions / sizeof actions[0];
    const struct inkcell_fb_bar_status linked = {
        .icon = INKCELL_ICON_BLUETOOTH,
        .tone = INKCELL_TONE_SUCCESS,
        .text = gallery_text(GALLERY_STR_STATUS_LINKED),
    };

    const struct inkcell_fb_app_bar normal = {
        .title = gallery_text(GALLERY_STR_CHAT_NAME),
        .actions = actions,
        .action_count = action_count,
        .overflow_focus_id = APP_BAR_ID_OVERFLOW,
        .status = linked,
    };
    (void)inkcell_fb_draw_app_bar(state, &layout, &normal);
    app_bar_gap(state, &layout);

    /* Search, with the keyboard up and something typed: the field shows the tail of the query
       and a caret after it, and the clear mark has a query to clear. */
    const struct inkcell_fb_bar_action filter[] = {
        {.icon = INKCELL_ICON_DEVICE, .label = gallery_text(GALLERY_STR_SHEET_TITLE)},
    };
    const struct inkcell_fb_app_bar searching = {
        .mode = INKCELL_FB_APP_BAR_SEARCH,
        .query = gallery_text(GALLERY_STR_SEARCH_QUERY),
        .placeholder = gallery_text(GALLERY_STR_SEARCH_PLACEHOLDER),
        .editing = true,
        .field_focus_id = APP_BAR_ID_FIELD,
        .clear_focus_id = APP_BAR_ID_CLEAR,
        .actions = filter,
        .action_count = 1U,
        .badge = gallery_text(GALLERY_STR_SHEET_DETAIL),
        .badge_family = INKCELL_FAMILY_SECONDARY,
    };
    (void)inkcell_fb_draw_app_bar(state, &layout, &searching);
    app_bar_gap(state, &layout);

    /* And at rest: nothing typed, so the placeholder, no caret and no clear mark. */
    const struct inkcell_fb_app_bar idle = {
        .mode = INKCELL_FB_APP_BAR_SEARCH,
        .placeholder = gallery_text(GALLERY_STR_SEARCH_PLACEHOLDER),
        .field_focus_id = APP_BAR_ID_FIELD,
        .clear_focus_id = APP_BAR_ID_CLEAR,
    };
    (void)inkcell_fb_draw_app_bar(state, &layout, &idle);
    app_bar_gap(state, &layout);

    /* A selection: the container fill, the close where the arrow was, the count as the title and
       the verbs that apply to what is selected. */
    const struct inkcell_fb_bar_action selected[] = {
        {.icon = INKCELL_ICON_PINNED,
         .label = gallery_text(GALLERY_STR_ACT_PIN),
         .focus_id = APP_BAR_ID_PIN},
        {.icon = INKCELL_ICON_DELETE,
         .label = gallery_text(GALLERY_STR_ACT_DELETE),
         .focus_id = APP_BAR_ID_DELETE},
        {.label = gallery_text(GALLERY_STR_ACT_MUTE), .focus_id = APP_BAR_ID_MUTE},
    };
    const struct inkcell_fb_app_bar selection = {
        .mode = INKCELL_FB_APP_BAR_SELECTION,
        .title = gallery_text(GALLERY_STR_SELECTED_COUNT),
        .actions = selected,
        .action_count = sizeof selected / sizeof selected[0],
        .overflow_focus_id = APP_BAR_ID_OVERFLOW,
    };
    (void)inkcell_fb_draw_app_bar(state, &layout, &selection);
    app_bar_gap(state, &layout);

    /* The large title at rest, carrying the same verbs on its collapsed row. */
    const struct inkcell_fb_app_bar large = {
        .title = gallery_text(GALLERY_STR_TILE_MESSAGES),
        .detail = gallery_text(GALLERY_STR_SCROLL_COUNT),
        .large = true,
        .actions = actions,
        .action_count = action_count,
        .overflow_focus_id = APP_BAR_ID_OVERFLOW,
        .status = linked,
    };
    (void)inkcell_fb_draw_app_bar(state, &layout, &large);

    /* The compact footer: the two presses that matter here, the primary one filled, and START
       for the rest - with the tip that says so resting above it, part way through its hold. */
    static const struct inkcell_button_action k_items[] = {
        {INKCELL_BUTTON_A, (inkcell_str_id)GALLERY_STR_ACT_OPEN},
        {INKCELL_BUTTON_B, (inkcell_str_id)GALLERY_STR_ACT_BACK},
        {INKCELL_BUTTON_X, (inkcell_str_id)GALLERY_STR_ACT_DELETE},
        {INKCELL_BUTTON_Y, (inkcell_str_id)GALLERY_STR_ACT_RETRY},
        {INKCELL_BUTTON_SELECT, (inkcell_str_id)GALLERY_STR_ACT_HELP},
    };
    static const struct inkcell_button_action k_more = {INKCELL_BUTTON_START,
                                                        (inkcell_str_id)GALLERY_STR_ACT_MORE};
    const struct inkcell_fb_action_tip tip = {
        .action = &k_more,
        .text = gallery_text(GALLERY_STR_TIP_MORE),
        .since_ms = GALLERY_CLOCK_MS - 500U,
    };
    inkcell_fb_draw_action_tip(state, &layout, &tip);
    const struct inkcell_fb_action_bar bar = {
        .items = k_items,
        .count = sizeof k_items / sizeof k_items[0],
        .limit = 2U,
        .more = &k_more,
        .emphasize_first = true,
        .status = gallery_text(GALLERY_STR_STATUS_OFFLINE),
        .status_tone = INKCELL_TONE_DIM,
    };
    (void)inkcell_fb_draw_action_bar(state, &layout, &bar);
}
