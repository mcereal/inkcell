#define _POSIX_C_SOURCE 200809L

/*
 * The layer stack: three overlays at once, over a body, so the picture is of the *stacking*
 * rather than of any one of them.
 *
 * scenes_overlays.c is the four shapes that already existed - the dialog, the snackbar, the QR
 * code - and it is a picture of each. This one is the thing that was missing: a sheet up over
 * a scrimmed screen, a menu anchored to a control on that sheet, and a tooltip over the lot
 * with no scrim of its own. Each is a separate call to inkcell_fb_overlay_begin() and the
 * order they are made in is the order they stack, which is the whole of what the model asks a
 * screen to keep track of.
 *
 * Three things this picture is the evidence for, and each of them is a thing that could not be
 * checked before:
 *
 *   - **Two scrims compound.** The sheet dims the body and the menu dims the sheet, so the
 *     content furthest from the reader is furthest dimmed - which is what depth looks like on
 *     a panel with no alpha layer to cast a shadow into.
 *   - **A menu is placed against its anchor**, and flipped above it when there is no room
 *     below. The anchor here is deliberately low on the sheet, so the flip is what the picture
 *     shows.
 *   - **A tooltip does not travel and does not dim.** It is the one layer here that is not an
 *     event, and if it ever starts looking like one this is the page that says so.
 */

#include "gallery.h"

#include "inkcell/ui/overlay.h"

void gallery_scene_layers(struct inkcell_draw_state *state) {
    struct inkcell_fb_layout layout = gallery_frame(state, GALLERY_STR_HEAD_LAYERS, 0U);

    /* A body to land on, and a busy one: the thing to check about an overlay is whether what
       is under it survives being dimmed as *content* rather than being replaced by a flat
       grey. A column of rows with icons and values is what makes that visible. */
    static const enum gallery_str_id k_rows[] = {
        GALLERY_STR_ROW_DISPLAY, GALLERY_STR_ROW_SOUND,   GALLERY_STR_ROW_STORAGE,
        GALLERY_STR_ROW_NETWORK, GALLERY_STR_ROW_BATTERY, GALLERY_STR_ROW_SECURITY,
        GALLERY_STR_ROW_UPDATES, GALLERY_STR_ROW_THEME,
    };
    const uint32_t row_count = (uint32_t)(sizeof k_rows / sizeof k_rows[0]);
    struct inkcell_fb_list list = inkcell_fb_list_begin(&layout, row_count, 2U);
    uint32_t index = 0U;
    while (inkcell_fb_list_next(&list, &index)) {
        const struct inkcell_fb_list_item item = {
            .text = gallery_text(k_rows[index]),
            .leading = {.kind = INKCELL_FB_LEADING_ICON, .icon = INKCELL_ICON_CHEVRON},
            .trailing = {.kind = INKCELL_FB_TRAILING_ICON, .icon = INKCELL_ICON_CHEVRON},
        };
        inkcell_fb_list_item(state, &list, index, &item);
    }

    /* The region every layer here belongs to: under the tab strip, over the keycaps. Stated
       once and handed to all three, which is what keeps the two scrims landing on the same
       rectangle - and what keeps the keycap row bright, because the presses that dismiss these
       things are printed on it. */
    const struct inkcell_fb_rect body = {
        .x = 0,
        .y = layout.nav_y,
        .w = inkcell_fb_panel_width(state),
        .h = layout.footer_y - layout.nav_y,
    };

    /* ---- the sheet, over the screen ---------------------------------------------------- */

    const int line = layout.line;
    const struct inkcell_fb_sheet sheet = {
        .title = gallery_text(GALLERY_STR_SHEET_TITLE),
        .detail = gallery_text(GALLERY_STR_SHEET_DETAIL),
    };
    /* Room for three rows of content. A sheet is sized by what is in it, so the height is
       asked for rather than stated: the grabber and the title are the sheet's own and a caller
       that added them up here would disagree with the component the moment either changed. */
    const int sheet_h = inkcell_fb_sheet_height(state, &sheet, 3 * line);

    struct inkcell_overlay_frame sheet_frame;
    if (inkcell_fb_overlay_begin(state,
                                 &(struct inkcell_overlay){
                                     .id = GALLERY_OVERLAY_SHEET,
                                     .up = true,
                                     .placement = INKCELL_OVERLAY_BOTTOM,
                                     .travel = INKCELL_OVERLAY_TRAVEL_OFF_PANEL,
                                     .w = body.w,
                                     .h = sheet_h,
                                     .bounds = body,
                                     .scrim = true,
                                     .modal = true,
                                 },
                                 &sheet_frame)) {
        const struct inkcell_fb_rect content =
            inkcell_fb_draw_sheet(state, sheet_frame.box, &sheet);

        /* The sheet's own content: three choices, the middle one in force. Drawn by this
           scene rather than by the component, which is the split the sheet is for - it owns
           the surface, the grabber and the title, and hands back the rectangle under them. */
        static const struct {
            enum gallery_str_id label;
            enum inkcell_icon icon;
            bool on;
        } k_filters[] = {
            {GALLERY_STR_ROW_NETWORK, INKCELL_ICON_NODES, false},
            {GALLERY_STR_ROW_BATTERY, INKCELL_ICON_TELEMETRY, true},
            {GALLERY_STR_ROW_SECURITY, INKCELL_ICON_ABOUT, false},
        };
        int y = content.y;
        for (size_t i = 0U; i < sizeof k_filters / sizeof k_filters[0]; ++i) {
            const struct inkcell_rgb ground = inkcell_fb_color(state, INKCELL_COLOR_SURFACE_HIGH);
            const int icon = inkcell_fb_icon_box(state, state->scale);
            const int gap = inkcell_fb_space(state, INKCELL_SPACE_SM);
            const struct inkcell_rgb ink = inkcell_fb_color(
                state, k_filters[i].on ? INKCELL_COLOR_TEXT_STRONG : INKCELL_COLOR_TEXT);
            inkcell_fb_draw_icon(state, content.x, y, k_filters[i].icon, state->scale, ink, ground);
            inkcell_fb_draw_text(state, content.x + icon + gap, y, gallery_text(k_filters[i].label),
                                 state->scale, ink, ground);
            if (k_filters[i].on) {
                inkcell_fb_draw_icon(state, content.x + content.w - icon, y, INKCELL_ICON_CHECK,
                                     state->scale,
                                     inkcell_fb_tone_color(state, INKCELL_TONE_PRIMARY), ground);
            }
            y += line;
        }

        /* ---- the menu, anchored to a row of the sheet -------------------------------- */

        /*
         * Anchored to the last filter row, which is deliberately near the bottom of the
         * panel: there is no room for a menu under it, so the layer flips it above - which is
         * the decision this page exists to show, and the one a caller would otherwise have
         * had to make by measuring the screen.
         */
        const struct inkcell_fb_rect anchor = {content.x + content.w / 2, y - line, content.w / 2,
                                               line};
        static const struct inkcell_fb_menu_item k_items[] = {
            {.label = NULL, .icon = INKCELL_ICON_PINNED},
            {.label = NULL, .icon = INKCELL_ICON_MUTED},
            {.label = NULL, .icon = INKCELL_ICON_SHARE, .disabled = true},
            {.label = NULL, .icon = INKCELL_ICON_DELETE, .tone = INKCELL_TONE_ERROR},
        };
        /* The labels come out of the catalog at run time, so the table above holds everything
           about a row that is not a word. A static array of `const char *` initialised from
           inkcell_str() is not a thing C will let a file have. */
        struct inkcell_fb_menu_item items[sizeof k_items / sizeof k_items[0]];
        static const enum gallery_str_id k_labels[] = {
            GALLERY_STR_MENU_PIN,
            GALLERY_STR_MENU_MUTE,
            GALLERY_STR_MENU_EXPORT,
            GALLERY_STR_MENU_DELETE,
        };
        for (size_t i = 0U; i < sizeof k_items / sizeof k_items[0]; ++i) {
            items[i] = k_items[i];
            items[i].label = gallery_text(k_labels[i]);
        }
        items[1].checked = true;

        const struct inkcell_fb_menu menu = {
            .items = items,
            .count = sizeof items / sizeof items[0],
            .heading = gallery_text(GALLERY_STR_MENU_HEADING),
            .cursor = 0U,
        };
        const struct inkcell_fb_rect want =
            inkcell_fb_menu_box(state, &menu, body.w - 2 * inkcell_fb_gutter(state));

        struct inkcell_overlay_frame menu_frame;
        if (inkcell_fb_overlay_begin(state,
                                     &(struct inkcell_overlay){
                                         .id = GALLERY_OVERLAY_MENU,
                                         .up = true,
                                         .placement = INKCELL_OVERLAY_ANCHOR,
                                         .travel = INKCELL_OVERLAY_TRAVEL_NEAR,
                                         .anchor = anchor,
                                         .w = want.w,
                                         .h = want.h,
                                         .bounds = body,
                                         /* A second scrim, over the sheet this time. Two of
                                            them compounding is what depth looks like when
                                            there is no shadow to cast. */
                                         .scrim = true,
                                         .modal = true,
                                     },
                                     &menu_frame)) {
            inkcell_fb_draw_menu(state, menu_frame.box, &menu);
            inkcell_fb_overlay_end(state, &menu_frame);
        }

        inkcell_fb_overlay_end(state, &sheet_frame);
    }

    /* ---- the tooltip, over everything -------------------------------------------------- */

    /*
     * No scrim, no travel, not modal: the three things that make it a tooltip rather than a
     * small menu. It names the thing under the cursor, so anything that drew attention to the
     * tooltip itself would be pointing at the wrong object.
     *
     * Drawn last, so it is on top - which is the model's one stacking rule, and the reason
     * this is a line at the end of the function rather than a z field somewhere.
     */
    /* Against a row well down the list and to the right of the words it is about, which is
       where a tooltip goes: beside the thing, not on top of it. */
    const struct inkcell_fb_rect tip_anchor = {inkcell_fb_panel_width(state) / 2,
                                               layout.body_y + 2 * line, line * 4, line};
    const int tip_pad = inkcell_fb_space(state, INKCELL_SPACE_SM);
    const int tip_scale = inkcell_fb_type_scale(state, INKCELL_TYPE_LABEL);
    const char *tip = gallery_text(GALLERY_STR_TOOLTIP_RANGE);

    struct inkcell_overlay_frame tip_frame;
    if (inkcell_fb_overlay_begin(
            state,
            &(struct inkcell_overlay){
                .id = GALLERY_OVERLAY_TOOLTIP,
                .up = true,
                .placement = INKCELL_OVERLAY_ANCHOR,
                .travel = INKCELL_OVERLAY_TRAVEL_NONE,
                .anchor = tip_anchor,
                .w = inkcell_fb_text_width(state, tip, tip_scale) + 2 * tip_pad,
                .h = inkcell_fb_line_adv(state, tip_scale) + tip_pad,
                .bounds = body,
            },
            &tip_frame)) {
        /* The inverted surface the snackbar takes, for the snackbar's reason: a thing that is
           over the UI rather than in it cannot say so with a tier, because every tier belongs
           to the screen. */
        const struct inkcell_rgb fill = inkcell_fb_color(state, INKCELL_COLOR_SURFACE_INVERSE);
        inkcell_fb_fill_round_rect(state, tip_frame.box.x, tip_frame.box.y, tip_frame.box.w,
                                   tip_frame.box.h, inkcell_fb_radius(state, INKCELL_SHAPE_SM),
                                   fill);
        inkcell_fb_draw_text(state, tip_frame.box.x + tip_pad, tip_frame.box.y + tip_pad / 2, tip,
                             tip_scale, inkcell_fb_color(state, INKCELL_COLOR_TEXT_ON_INVERSE),
                             fill);
        inkcell_fb_overlay_end(state, &tip_frame);
    }

    gallery_footer(state, &layout);
}
