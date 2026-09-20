#define _POSIX_C_SOURCE 200809L

/*
 * Buttons, chips and badges: the shapes a press lands on.
 *
 * Laid out as a grid rather than as a screen, because the point of this sheet is the *axes* -
 * three variants down, the resting and selected states across, and the families the theme
 * answers for each. A picture that showed one button in one state would not show that a tonal
 * button and a filled one are two different promises about what the key does.
 */

#include "gallery.h"

static int button_height(const struct inkcell_backend_fb_state *state, int scale) {
    return inkcell_fb_line_adv(state, scale) +
           2 * inkcell_fb_space_at(state, INKCELL_SPACE_SM, scale);
}

/* One row of the grid: the same variant and family, at rest and then selected. */
static int button_row(const struct inkcell_backend_fb_state *state, int x, int y, int scale,
                      enum inkcell_fb_button_variant variant, enum inkcell_family family,
                      enum gallery_str_id label, enum inkcell_icon icon) {
    const int h = button_height(state, scale);
    const int gap = inkcell_fb_space(state, INKCELL_SPACE_SM);
    const char *text = gallery_text(label);

    for (int selected = 0; selected < 2; ++selected) {
        const struct inkcell_fb_button button = {
            .rect = {.x = x,
                     .y = y,
                     .w = inkcell_fb_button_width(state, icon, text, scale) +
                          2 * inkcell_fb_space(state, INKCELL_SPACE_MD),
                     .h = h},
            .icon = icon,
            .label = text,
            .selected = selected != 0,
            .variant = variant,
            .family = family,
            .shape = INKCELL_SHAPE_FULL,
            .idle_tone = INKCELL_TONE_NORMAL,
            .ground = INKCELL_COLOR_BG,
            .scale = scale,
        };
        inkcell_fb_draw_button(state, &button);
        x += button.rect.w + gap;
    }
    return y + h + gap;
}

void gallery_scene_buttons(struct inkcell_backend_fb_state *state) {
    struct inkcell_fb_layout layout = gallery_frame(state, GALLERY_STR_HEAD_BUTTONS, 0U);
    const struct inkcell_fb_row_box box = inkcell_fb_row_box(state);
    const int scale = layout.small;
    int y = layout.body_y;

    y = gallery_section(state, &layout, y, GALLERY_STR_HEAD_BUTTONS);

    /* The three variants, each at rest and selected. The family stays primary so that what
       changes down the column is the variant alone. */
    y = button_row(state, box.text_x, y, scale, INKCELL_FB_BUTTON_FILLED, INKCELL_FAMILY_PRIMARY,
                   GALLERY_STR_ACT_SEND, INKCELL_ICON_SEND);
    y = button_row(state, box.text_x, y, scale, INKCELL_FB_BUTTON_TONAL, INKCELL_FAMILY_PRIMARY,
                   GALLERY_STR_ACT_SAVE, INKCELL_ICON_CHECK);
    y = button_row(state, box.text_x, y, scale, INKCELL_FB_BUTTON_TEXT, INKCELL_FAMILY_PRIMARY,
                   GALLERY_STR_ACT_CANCEL, INKCELL_ICON_CLOSE);
    /* And the one family that means something different rather than looking different: a
       destructive verb is filled in error, which is the whole reason a button takes a family
       and not a colour. */
    y = button_row(state, box.text_x, y, scale, INKCELL_FB_BUTTON_FILLED, INKCELL_FAMILY_ERROR,
                   GALLERY_STR_ACT_DELETE, INKCELL_ICON_DELETE);

    y += inkcell_fb_space(state, INKCELL_SPACE_MD);
    y = gallery_section(state, &layout, y, GALLERY_STR_HEAD_CHIPS);

    /* A strip, which is what the navigation bar is made of: the active one carries its label and
       the rest fall back to their symbol when the room runs out. */
    const struct inkcell_fb_chip chips[] = {
        {.icon = INKCELL_ICON_MESSAGES, .label = gallery_text(GALLERY_STR_TAB_COMPONENTS)},
        {.icon = INKCELL_ICON_NODES, .label = gallery_text(GALLERY_STR_TAB_LISTS), .badge = "3"},
        {.icon = INKCELL_ICON_DISPLAY, .label = gallery_text(GALLERY_STR_TAB_CONTROLS)},
        {.icon = INKCELL_ICON_TELEMETRY, .label = gallery_text(GALLERY_STR_TAB_READINGS)},
    };
    /* The strip returns the x after its last chip, not a row: it is laid out along a line the
       caller chose, the way the navigation bar lays one out along its own. */
    (void)inkcell_fb_draw_chip_strip(state, box.text_x, y, chips, sizeof chips / sizeof chips[0],
                                     1U, box.text_right - box.text_x, INKCELL_COLOR_BG, scale);
    y += inkcell_fb_line_adv(state, scale) + inkcell_fb_space(state, INKCELL_SPACE_MD);

    /* The badge: a count that has to stay legible at the smallest thing this toolkit draws. One
       per family, because a badge is the shortest statement a family ever makes. */
    int x = box.text_x;
    static const char *const k_counts[] = {"1", "9", "12", "99+"};
    for (size_t i = 0U; i < sizeof k_counts / sizeof k_counts[0]; ++i) {
        const int w = inkcell_fb_badge_width(state, k_counts[i], scale);
        const struct inkcell_fb_rect rect = {
            .x = x, .y = y, .w = w, .h = inkcell_fb_line_adv(state, scale)};
        inkcell_fb_draw_badge(state, &rect, y, k_counts[i],
                              (enum inkcell_family)(INKCELL_FAMILY_PRIMARY + (int)i), scale);
        x += w + inkcell_fb_space(state, INKCELL_SPACE_SM);
    }

    gallery_footer(state, &layout);
}
