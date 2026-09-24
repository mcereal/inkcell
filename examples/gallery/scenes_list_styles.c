#define _POSIX_C_SOURCE 200809L

/*
 * One settings screen, drawn in the list's looks.
 *
 * The "list" page is every slot a row has; these are every way a *list* can be set. The content
 * is the same on all three pages on purpose - two sections, the three kinds of row a settings
 * screen mixes, one row carrying a second line - so that what changes between them is the look
 * and nothing else:
 *
 *   - "list_grouped": inset sections, comfortable rows, tiered type, the accent cursor on a row
 *     in the middle of a section, where the fill has square corners and the capsule is the mark;
 *   - "list_sections": the same sections compact, with the theme's own fill on the last row of
 *     the first section - which takes the section's corners and reaches down to its end. No
 *     separators: a compact step has no leading for one to stand in (see the style's note);
 *   - "list_plain": no surfaces, comfortable rows with separators between them, and the ring on
 *     the cursor.
 *
 * What to look for is alignment. The values, the status pill and the switch stand in one column
 * on every row because each row reserves the accessory column whether it fills it or not, and
 * the options' check lands in the column the chevrons are in.
 */

#include "gallery.h"

#include <string.h>

enum {
    GALLERY_ANIM_STYLED_SWITCH = 0x1180,
};

enum {
    GALLERY_STYLED_HEAD_CONNECTION = 0U,
    GALLERY_STYLED_NETWORK,
    GALLERY_STYLED_NOTIFICATIONS,
    GALLERY_STYLED_SECURITY,
    GALLERY_STYLED_UPDATES,
    GALLERY_STYLED_HEAD_APPEARANCE,
    GALLERY_STYLED_LIGHT,
    GALLERY_STYLED_DARK,
    GALLERY_STYLED_AUTOMATIC,
    GALLERY_STYLED_COUNT,
};

static void gallery_styled_settings(struct inkcell_draw_state *state, enum gallery_str_id title,
                                    const struct inkcell_fb_list_style *look, uint32_t cursor) {
    struct inkcell_fb_layout layout = gallery_frame(state, title, 1U);

    struct inkcell_fb_switch row_switch = {
        .id = GALLERY_ANIM_STYLED_SWITCH,
        .family = INKCELL_FAMILY_PRIMARY,
        .on = true,
    };

    /* The rows, by index. Every row reserves the accessory column (`accessory_slot`), which is
       what puts the switch, the pill and the figure in one column above the chevrons. */
    struct inkcell_fb_list_item items[GALLERY_STYLED_COUNT];
    memset(items, 0, sizeof items);
    items[GALLERY_STYLED_NETWORK] = (struct inkcell_fb_list_item){
        .text = gallery_text(GALLERY_STR_ROW_NETWORK),
        .leading = {.kind = INKCELL_FB_LEADING_ICON, .icon = INKCELL_ICON_NETWORK},
        .trailing = {.kind = INKCELL_FB_TRAILING_TEXT, .text = gallery_text(GALLERY_STR_VAL_HOME)},
        .accessory = INKCELL_FB_ACCESSORY_DISCLOSURE,
        .accessory_slot = true,
    };
    /* A control row: the switch is the offer, so it ends in no accessory. */
    items[GALLERY_STYLED_NOTIFICATIONS] = (struct inkcell_fb_list_item){
        .text = gallery_text(GALLERY_STR_ROW_NOTIFICATIONS),
        .leading = {.kind = INKCELL_FB_LEADING_ICON, .icon = INKCELL_ICON_MESSAGES},
        .trailing = {.kind = INKCELL_FB_TRAILING_SWITCH, .sw = &row_switch},
        .accessory_slot = true,
    };
    /* A state, said as a pill against the trailing edge of a row with no value column. */
    items[GALLERY_STYLED_SECURITY] = (struct inkcell_fb_list_item){
        .text = gallery_text(GALLERY_STR_ROW_SECURITY),
        .leading = {.kind = INKCELL_FB_LEADING_ICON, .icon = INKCELL_ICON_SECURITY},
        .trailing = {.kind = INKCELL_FB_TRAILING_STATUS,
                     .tone = INKCELL_TONE_SUCCESS,
                     .text = gallery_text(GALLERY_STR_STATUS_VERIFIED)},
        .accessory = INKCELL_FB_ACCESSORY_DISCLOSURE,
        .accessory_slot = true,
    };
    /* Three tiers on one row: the headline, the supporting line under it, and a figure. */
    items[GALLERY_STYLED_UPDATES] = (struct inkcell_fb_list_item){
        .text = gallery_text(GALLERY_STR_ROW_UPDATES),
        .supporting = gallery_text(GALLERY_STR_ROW_CHECKED_AGO),
        .supporting_tone = INKCELL_TONE_DIM,
        .supporting_quiet = true,
        .leading = {.kind = INKCELL_FB_LEADING_ICON, .icon = INKCELL_ICON_DOWNLOAD},
        .trailing = {.kind = INKCELL_FB_TRAILING_TEXT, .text = "v2.4.1"},
        .accessory = INKCELL_FB_ACCESSORY_DISCLOSURE,
        .accessory_slot = true,
    };
    /* A column of options: the chosen one checked, the others an empty column in the same
       place. No leading slot, which is how a set of choices under a heading usually stands. */
    items[GALLERY_STYLED_LIGHT] = (struct inkcell_fb_list_item){
        .text = gallery_text(GALLERY_STR_VAL_LIGHT),
        .accessory_slot = true,
    };
    items[GALLERY_STYLED_DARK] = (struct inkcell_fb_list_item){
        .text = gallery_text(GALLERY_STR_VAL_DARK),
        .accessory = INKCELL_FB_ACCESSORY_CHECK,
        .accessory_slot = true,
    };
    items[GALLERY_STYLED_AUTOMATIC] = (struct inkcell_fb_list_item){
        .text = gallery_text(GALLERY_STR_VAL_AUTOMATIC),
        .accessory_slot = true,
    };

    /* The two sections, and the headings in the breaks between them standing on no card. */
    uint8_t sections[GALLERY_STYLED_COUNT];
    uint8_t heights[GALLERY_STYLED_COUNT];
    for (uint32_t i = 0U; i < GALLERY_STYLED_COUNT; ++i) {
        sections[i] = (uint8_t)(i < GALLERY_STYLED_HEAD_APPEARANCE ? 0U : 1U);
        heights[i] = (uint8_t)(items[i].supporting != NULL ? 2U : 1U);
    }
    sections[GALLERY_STYLED_HEAD_CONNECTION] = INKCELL_FB_LIST_NO_CARD;
    sections[GALLERY_STYLED_HEAD_APPEARANCE] = INKCELL_FB_LIST_NO_CARD;

    struct inkcell_fb_list list = inkcell_fb_list_begin_styled(state, &layout, GALLERY_STYLED_COUNT,
                                                               cursor, heights, sections, look);
    uint32_t index = 0U;
    while (inkcell_fb_list_next(&list, &index)) {
        if (index == GALLERY_STYLED_HEAD_CONNECTION) {
            inkcell_fb_list_subheader(state, &list, index,
                                      gallery_text(GALLERY_STR_HEAD_CONNECTION));
        } else if (index == GALLERY_STYLED_HEAD_APPEARANCE) {
            inkcell_fb_list_subheader(state, &list, index,
                                      gallery_text(GALLERY_STR_HEAD_APPEARANCE));
        } else {
            inkcell_fb_list_item(state, &list, index, &items[index]);
        }
    }
    gallery_footer(state, &layout);
}

void gallery_scene_list_grouped(struct inkcell_draw_state *state) {
    const struct inkcell_fb_list_style look = {
        .appearance = INKCELL_FB_LIST_INSET_GROUPED,
        .density = INKCELL_FB_LIST_COMFORTABLE,
        .focus = INKCELL_FB_LIST_FOCUS_ACCENT,
        .type = INKCELL_FB_LIST_TYPE_TIERED,
        .separators = true,
    };
    gallery_styled_settings(state, GALLERY_STR_HEAD_GROUPED, &look, GALLERY_STYLED_NOTIFICATIONS);
}

void gallery_scene_list_sections(struct inkcell_draw_state *state) {
    const struct inkcell_fb_list_style look = {
        .appearance = INKCELL_FB_LIST_INSET_GROUPED,
        .focus = INKCELL_FB_LIST_FOCUS_FILL,
        .type = INKCELL_FB_LIST_TYPE_TIERED,
    };
    gallery_styled_settings(state, GALLERY_STR_HEAD_SECTIONS, &look, GALLERY_STYLED_UPDATES);
}

void gallery_scene_list_plain(struct inkcell_draw_state *state) {
    const struct inkcell_fb_list_style look = {
        .appearance = INKCELL_FB_LIST_PLAIN,
        .density = INKCELL_FB_LIST_COMFORTABLE,
        .focus = INKCELL_FB_LIST_FOCUS_RING,
        .type = INKCELL_FB_LIST_TYPE_TIERED,
        .separators = true,
    };
    gallery_styled_settings(state, GALLERY_STR_HEAD_PLAIN, &look, GALLERY_STYLED_SECURITY);
}
