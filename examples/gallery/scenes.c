#define _POSIX_C_SOURCE 200809L

/*
 * The scene table, and the frame every scene stands in.
 *
 * The frame is here rather than in each scene because a specimen sheet should show a component
 * where it would really sit. A button floating on a cleared panel is a picture of a button; a
 * button under a tab strip and above a keycap row is a picture of the toolkit.
 */

#include "gallery.h"

#include "inkcell/ui/actions.h"

#include <string.h>

/* ---- the table -------------------------------------------------------------------------------
 *
 * Order is part of the manifest: a golden run names its pictures after these, so moving an entry
 * renames nothing but reordering the file does change the order they are taken in. Append.
 */
static const struct gallery_scene k_scenes[] = {
    {"buttons", gallery_scene_buttons, 0U},
    {"chrome", gallery_scene_chrome, 0U},
    {"controls", gallery_scene_controls, 0U},
    {"list", gallery_scene_list, 0U},
    {"cards", gallery_scene_cards, 0U},
    {"meters", gallery_scene_meters, 0U},
    {"transcript", gallery_scene_transcript, 0U},
    /* The snackbar rises from off the panel, so this one is drawn twice with the motion's own
       duration between: long enough for it to have arrived, and taken from the theme rather
       than guessed at, so a theme with slower motion still gets a settled picture. */
    {"overlays", gallery_scene_overlays, 400U},
    /* Three layers at once, settled. Same reason the overlays page steps the clock: a sheet
       and a menu both arrive from somewhere, so the frame that introduces them is a picture of
       them on their way rather than of what they look like. */
    {"layers", gallery_scene_layers, 400U},
    /*
     * A body at three positions a row-index window cannot hold: between two rows, past the
     * end, and with the heading collapsed into the bar. No settle - each of these is a
     * *position* rather than a travel, and the scene puts the scroll at it and reads the
     * clock well past every duration in src/scroll.c.
     */
    {"scroll", gallery_scene_scroll, 0U},
    {"scroll_overscroll", gallery_scene_scroll_overscroll, 0U},
    {"scroll_title", gallery_scene_scroll_title, 0U},
    {"typography", gallery_scene_typography, 0U},
    {"palette", gallery_scene_palette, 0U},
    {"shapes", gallery_scene_shapes, 0U},
    /* Two pictures of one widget: the character grid sets its keycaps as text and the emoji
       panel draws them as sprites at the size of the key, which is a different path through the
       button and the one that looks wrong first. */
    {"keyboard", gallery_scene_keyboard, 0U},
    {"keyboard_emoji", gallery_scene_keyboard_emoji, 0U},
    /* Not a component at all: the four presses from one cell of a ragged screen, drawn as the
       finder answered them. Appended last for the manifest's sake - the table's order names the
       pictures. */
    {"focus", gallery_scene_focus, 0U},
    /*
     * The ring, caught in the middle of a move. A seventh of the motion token rather than half:
     * the curve is an ease-out, so it covers most of the ground early, and a picture taken at
     * the halfway point is of something that has nearly arrived. This one catches it in the
     * corridor between the two rows, which is the only place a still can show it travelling
     * rather than sitting on something.
     */
    {"focus_ring", gallery_scene_focus_ring, 20U},
    /*
     * A list between two windows, caught early. The curve is an ease-out and most of the
     * distance is gone within the first third of it, so a picture has to be taken while the
     * rows are still well out of place - and off a row boundary, because the tell that says a
     * list is moving rather than sitting is the half-row the body cuts at its top edge.
     */
    {"glide", gallery_scene_glide, 54U},
    /*
     * The FAB, with one of its specimens caught half way back to a disc. Under a third of the
     * exit token, for the focus ring's reason: the curve is an ease-out, so most of the ground
     * is covered early and a picture taken at the halfway point is of a pill that has nearly
     * finished shrinking. The page's other five are at rest - the scene starts that one's
     * collapse inside the first frame, which is what leaves anything to photograph at all.
     */
    {"fab", gallery_scene_fab, 40U},
    /*
     * The grid, twice, and appended for the manifest's sake like the pages above it. A home
     * screen scrolled off its first row, so the rail beside it has something to report; and a
     * shelf of covers ending on a short row, which is the shape a list cannot have and the
     * reason a press has a second axis.
     */
    {"grid", gallery_scene_grid, 0U},
    {"grid_covers", gallery_scene_grid_covers, 0U},
};

const struct gallery_scene *gallery_scenes(size_t *count) {
    if (count != NULL) {
        *count = sizeof k_scenes / sizeof k_scenes[0];
    }
    return k_scenes;
}

/* ---- the frame --------------------------------------------------------------------------------
 */

struct inkcell_fb_layout gallery_frame(struct inkcell_draw_state *state, enum gallery_str_id title,
                                       size_t tab) {
    inkcell_fb_clear(state, inkcell_fb_color(state, INKCELL_COLOR_BG));

    /* `true, true`: there is a keycap row at the bottom, so its room comes out of the body now;
       and B goes back, so the app bar draws its leading chevron. */
    struct inkcell_fb_layout layout = inkcell_fb_layout_begin(state, true, true);

    const struct inkcell_fb_chip tabs[] = {
        {.icon = INKCELL_ICON_MESSAGES, .label = gallery_text(GALLERY_STR_TAB_COMPONENTS)},
        {.icon = INKCELL_ICON_NODES, .label = gallery_text(GALLERY_STR_TAB_LISTS)},
        {.icon = INKCELL_ICON_DISPLAY, .label = gallery_text(GALLERY_STR_TAB_CONTROLS)},
        {.icon = INKCELL_ICON_TELEMETRY, .label = gallery_text(GALLERY_STR_TAB_READINGS)},
        {.icon = INKCELL_ICON_ABOUT, .label = gallery_text(GALLERY_STR_TAB_ABOUT)},
    };
    inkcell_fb_draw_nav_bar(state, &layout, tabs, sizeof tabs / sizeof tabs[0], tab);

    const struct inkcell_fb_app_bar bar = {.title = gallery_text(title)};
    inkcell_fb_draw_app_bar(state, &layout, &bar);
    return layout;
}

void gallery_footer(const struct inkcell_draw_state *state,
                    const struct inkcell_fb_layout *layout) {
    static const struct inkcell_button_action k_items[] = {
        {INKCELL_BUTTON_A, (enum inkcell_str_id)GALLERY_STR_ACT_SELECT},
        {INKCELL_BUTTON_B, (enum inkcell_str_id)GALLERY_STR_ACT_BACK},
        {INKCELL_BUTTON_UP_DOWN, (enum inkcell_str_id)GALLERY_STR_ACT_OPEN},
    };
    const struct inkcell_fb_action_bar bar = {
        .items = k_items,
        .count = sizeof k_items / sizeof k_items[0],
    };
    inkcell_fb_draw_action_bar(state, layout, &bar);
}

int gallery_section(const struct inkcell_draw_state *state, const struct inkcell_fb_layout *layout,
                    int y, enum gallery_str_id title) {
    const struct inkcell_fb_row_box box = inkcell_fb_row_box(state);
    const int small = layout->small;
    inkcell_fb_draw_text_weight(state, box.text_x, y, gallery_text(title), small,
                                inkcell_fb_type_weight(state, INKCELL_TYPE_LABEL),
                                inkcell_fb_tone_color(state, INKCELL_TONE_PRIMARY),
                                inkcell_fb_color(state, INKCELL_COLOR_BG));
    return y + inkcell_fb_line_adv(state, small) + inkcell_fb_space(state, INKCELL_SPACE_SM);
}
