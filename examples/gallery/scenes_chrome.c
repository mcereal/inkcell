#define _POSIX_C_SOURCE 200809L

/*
 * The chrome: everything that frames a screen rather than being on it.
 *
 * Drawn in one picture because that is the only way to see the thing that actually goes wrong
 * with chrome - not any one bar, but the stack. The navigation bar, the banner below it, the
 * app bar below that and the keycap row at the bottom each take rows from the body, and a bar
 * that takes its room without saying so is a bar that paints over the first thing on the screen.
 *
 * So this scene draws the whole stack and then an empty state in whatever is left, which is the
 * assertion: if the body's top or bottom is wrong, the empty state is off centre.
 */

#include "gallery.h"

#include "inkcell/ui/actions.h"

void gallery_scene_chrome(struct inkcell_draw_state *state) {
    inkcell_fb_clear(state, inkcell_fb_color(state, INKCELL_COLOR_BG));
    struct inkcell_fb_layout layout = inkcell_fb_layout_begin(state, true, true);

    const struct inkcell_fb_chip tabs[] = {
        {.icon = INKCELL_ICON_MESSAGES, .label = gallery_text(GALLERY_STR_TAB_COMPONENTS)},
        {.icon = INKCELL_ICON_NODES, .label = gallery_text(GALLERY_STR_TAB_LISTS), .badge = "12"},
        {.icon = INKCELL_ICON_DISPLAY, .label = gallery_text(GALLERY_STR_TAB_CONTROLS)},
        {.icon = INKCELL_ICON_TELEMETRY, .label = gallery_text(GALLERY_STR_TAB_READINGS)},
        {.icon = INKCELL_ICON_ABOUT, .label = gallery_text(GALLERY_STR_TAB_ABOUT)},
    };
    inkcell_fb_draw_nav_bar(state, &layout, tabs, sizeof tabs / sizeof tabs[0], 0U);

    /* Hangs in the gap the navigation bar left and moves nothing - which is the assertion in
       this picture, because a progress bar that reflowed the body would push everything below
       it down by a hairline. */
    inkcell_fb_draw_progress(state, &layout, true);

    /* A statement about the whole client, above the screen's own heading. */
    const struct inkcell_fb_banner banner = {
        .icon = INKCELL_ICON_DOWNLOAD,
        .text = gallery_text(GALLERY_STR_BANNER_UPDATE),
        .supporting = gallery_text(GALLERY_STR_BANNER_SUPPORTING),
        .family = INKCELL_FAMILY_PRIMARY,
    };
    inkcell_fb_draw_banner(state, &layout, &banner);

    /* The trail is the path back up, drawn by the bar rather than spelled by the screen: the
       separators between the levels belong to the component. */
    const struct inkcell_fb_app_bar bar = {
        .trail = {gallery_text(GALLERY_STR_TAB_ABOUT), gallery_text(GALLERY_STR_ROW_DISPLAY)},
        .trail_count = 2U,
        .title = gallery_text(GALLERY_STR_ROW_BRIGHTNESS),
        .badge = gallery_text(GALLERY_STR_VAL_ON),
        .badge_family = INKCELL_FAMILY_WARNING,
    };
    inkcell_fb_draw_app_bar(state, &layout, &bar);

    /* Whatever the stack left. An empty state centres itself in it, so this is where a bar that
       took room without saying so shows up. */
    inkcell_fb_draw_empty(state, &layout, INKCELL_ICON_MESSAGES,
                          gallery_text(GALLERY_STR_EMPTY_NOTHING));

    static const struct inkcell_button_action k_items[] = {
        {INKCELL_BUTTON_A, (inkcell_str_id)GALLERY_STR_ACT_OPEN},
        {INKCELL_BUTTON_B, (inkcell_str_id)GALLERY_STR_ACT_BACK},
        {INKCELL_BUTTON_X, (inkcell_str_id)GALLERY_STR_ACT_DELETE},
        {INKCELL_BUTTON_Y, (inkcell_str_id)GALLERY_STR_ACT_RETRY},
        {INKCELL_BUTTON_SELECT, (inkcell_str_id)GALLERY_STR_ACT_DISMISS},
    };
    const struct inkcell_fb_action_bar action = {
        .items = k_items,
        .count = sizeof k_items / sizeof k_items[0],
        .status = gallery_text(GALLERY_STR_BANNER_SUPPORTING),
        .status_tone = INKCELL_TONE_DIM,
    };
    inkcell_fb_draw_action_bar(state, &layout, &action);
}
