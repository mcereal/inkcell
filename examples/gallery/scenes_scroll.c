#define _POSIX_C_SOURCE 200809L

/*
 * A body measured in pixels: three pictures of the same list, at three positions a row-index
 * window cannot hold.
 *
 * scenes_glide.c is the old answer - a window that has moved a whole row, with the content
 * drawn displaced from where the window says it belongs and the displacement easing to
 * nothing. It reads well and it is a pixel offset bolted onto an index. This page is the
 * position itself, and each of its three variants is a state the index model has no
 * expression for at all:
 *
 *   - **Between two rows.** Not on its way between two windows - *at* a position that is not a
 *     row boundary, settled, owing no further frames. The tell is the row cut by the top edge
 *     of the body, which is a half-row rather than a row that has not arrived.
 *   - **Past the end.** The content pulled below its last row and held there, with the rail's
 *     thumb shortened against the end it is being pulled away from. There is no index past the
 *     last one, so this picture could not exist.
 *   - **Collapsed.** The large title given its row back to the body. It is a continuous
 *     function of the offset, so a window that moves a row at a time has nothing to drive it
 *     with.
 *
 * All three draw the *whole* list into a viewport and let the view cut it, which is what the
 * scroll model asks for and what the comment on inkcell_fb_viewport_begin() is about. Forty
 * rows is small enough to do honestly; a list of four hundred would walk `view.visible`
 * instead.
 */

#include "gallery.h"

#include "inkcell/ui/scroll.h"

#include <stdio.h>

/* Long enough to scroll and short enough to draw whole. */
#define GALLERY_SCROLL_ROWS 40U

/* The clock a settled scroll is read at: past every duration in src/scroll.c, so a picture is
   of a position rather than of a travel. The scenes that want a travel step the clock
   themselves through `settle_ms`, which is a different mechanism for a different question. */
#define GALLERY_SCROLL_SETTLED_MS 4000U

/*
 * One row of the list, drawn at the content's own coordinates.
 *
 * `index` is the row and `y` is where it is in the content - not on the panel. That is the
 * whole of what a viewport changes about writing a screen: a row is at `index * line` whether
 * it is on the panel or four hundred rows above it, and the view decides which.
 */
static void gallery_scroll_row(struct inkcell_draw_state *state, uint32_t index, int y, int line,
                               bool selected) {
    static const enum gallery_str_id k_rows[] = {
        GALLERY_STR_ROW_DISPLAY, GALLERY_STR_ROW_SOUND,   GALLERY_STR_ROW_STORAGE,
        GALLERY_STR_ROW_NETWORK, GALLERY_STR_ROW_BATTERY, GALLERY_STR_ROW_SECURITY,
        GALLERY_STR_ROW_UPDATES, GALLERY_STR_ROW_THEME,
    };
    const size_t names = sizeof k_rows / sizeof k_rows[0];
    const struct inkcell_fb_row_box box = inkcell_fb_row_box(state);
    const int scale = state->scale;

    struct inkcell_rgb ground = inkcell_fb_color(state, INKCELL_COLOR_BG);
    if (selected) {
        ground = inkcell_fb_state_layer(state, INKCELL_COLOR_BG, INKCELL_COLOR_TEXT,
                                        INKCELL_STATE_SELECTED);
        inkcell_fb_fill_round_rect(state, box.x, y, box.w, line,
                                   inkcell_fb_radius(state, INKCELL_SHAPE_SM), ground);
    }

    const int icon = inkcell_fb_icon_box(state, scale);
    const struct inkcell_rgb ink = inkcell_fb_color(state, INKCELL_COLOR_TEXT);
    inkcell_fb_draw_icon(state, box.text_x, y, INKCELL_ICON_CHEVRON, scale,
                         inkcell_fb_color(state, INKCELL_COLOR_TEXT_DIM), ground);
    inkcell_fb_draw_text(state, box.text_x + icon + inkcell_fb_space(state, INKCELL_SPACE_SM), y,
                         gallery_text(k_rows[index % names]), scale, ink, ground);

    /* The row's ordinal against the trailing edge, in the quiet ink. It is what makes a
       picture of a scroll readable at all: two frames of the same eight words say nothing
       about where the body is, and "17" says all of it. */
    char ordinal[8];
    (void)snprintf(ordinal, sizeof ordinal, "%u", (unsigned)index + 1U);
    inkcell_fb_draw_text(state, box.text_right - inkcell_fb_text_width(state, ordinal, scale), y,
                         ordinal, scale, inkcell_fb_color(state, INKCELL_COLOR_TEXT_DIM), ground);
}

/*
 * The page, at whatever position the caller put the scroll in.
 *
 * `collapsing` is whether the heading is the large one. Both variants draw the same list in
 * the same viewport - what differs is the chrome above it, which is the point: a collapsing
 * bar is a reader of the scroll rather than a different kind of screen.
 */
static void gallery_scroll_page(struct inkcell_draw_state *state, struct inkcell_scroll *scroll,
                                int32_t offset, bool holding, bool collapsing) {
    inkcell_fb_clear(state, inkcell_fb_color(state, INKCELL_COLOR_BG));
    struct inkcell_fb_layout layout = inkcell_fb_layout_begin(state, true, true);

    const struct inkcell_fb_chip tabs[] = {
        {.icon = INKCELL_ICON_MESSAGES, .label = gallery_text(GALLERY_STR_TAB_COMPONENTS)},
        {.icon = INKCELL_ICON_NODES, .label = gallery_text(GALLERY_STR_TAB_LISTS)},
        {.icon = INKCELL_ICON_DISPLAY, .label = gallery_text(GALLERY_STR_TAB_CONTROLS)},
        {.icon = INKCELL_ICON_TELEMETRY, .label = gallery_text(GALLERY_STR_TAB_READINGS)},
        {.icon = INKCELL_ICON_ABOUT, .label = gallery_text(GALLERY_STR_TAB_ABOUT)},
    };
    inkcell_fb_draw_nav_bar(state, &layout, tabs, sizeof tabs / sizeof tabs[0], 1U);

    const int line = layout.line;
    const int32_t content_h = (int32_t)GALLERY_SCROLL_ROWS * line;

    if (collapsing) {
        /* The heading reads the same offset the body is drawn at, which is the whole of the
           wiring: the bar is not told that a scroll happened, it is handed where the scroll
           is. */
        const struct inkcell_fb_large_title bar = {
            .title = gallery_text(GALLERY_STR_HEAD_SCROLL),
            .detail = gallery_text(GALLERY_STR_SCROLL_COUNT),
            .badge = gallery_text(GALLERY_STR_BADGE_UNSAVED),
            .badge_family = INKCELL_FAMILY_WARNING,
        };
        inkcell_fb_draw_large_title(state, &layout, &bar, offset);
    } else {
        const struct inkcell_fb_app_bar bar = {.title = gallery_text(GALLERY_STR_HEAD_SCROLL)};
        inkcell_fb_draw_app_bar(state, &layout, &bar);
    }

    const struct inkcell_fb_rect window = {
        .x = 0,
        .y = layout.body_y,
        .w = inkcell_fb_panel_width(state),
        .h = layout.footer_y - inkcell_fb_gutter(state) - layout.body_y,
    };

    /*
     * The scroll is put where this picture wants it and then let go of, or held.
     *
     * Held is what an overscroll needs: a reader with a direction pressed down. Released is
     * every other frame, and a screen calls it unconditionally - "nothing is being held" is
     * the state a frame is in almost always, and one that only released when it thought it
     * mattered would one day leave a list stretched.
     */
    inkcell_scroll_extent(scroll, content_h, window.h);
    inkcell_scroll_place(scroll, 0);
    inkcell_scroll_by(scroll, offset, 0U);
    if (!holding) {
        inkcell_scroll_release(scroll, 0U);
    }
    inkcell_fb_state_set_now(state, GALLERY_SCROLL_SETTLED_MS);

    struct inkcell_fb_viewport view;
    if (inkcell_fb_viewport_begin(state, &view, window, scroll, content_h)) {
        /*
         * The whole list, at its own coordinates. Forty rows is small enough to draw honestly
         * and the view cuts what does not fit - including, at the top edge, half of a row,
         * which is the thing this page is a picture of.
         */
        for (uint32_t i = 0U; i < GALLERY_SCROLL_ROWS; ++i) {
            gallery_scroll_row(state, i, (int)i * line, line, i == 12U);
        }
        inkcell_fb_viewport_end(state, &view);
        inkcell_fb_draw_scroll_rail(state, &view, scroll);
    }

    gallery_footer(state, &layout);
}

/* Settled between two rows: an offset that is not a multiple of a line, owing no frames. */
void gallery_scene_scroll(struct inkcell_draw_state *state) {
    static struct inkcell_scroll scroll;
    const int line = inkcell_fb_line_adv(state, state->scale);
    gallery_scroll_page(state, &scroll, 6 * line + line / 2, false, false);
}

/* Pulled past the last row and held there, so the band is at full stretch and the rail's thumb
   has shortened against the end. */
void gallery_scene_scroll_overscroll(struct inkcell_draw_state *state) {
    static struct inkcell_scroll scroll;
    /* Far past the end on purpose: the band caps what that becomes, and a picture taken at the
       cap is a picture of the most an overscroll can ever be. */
    gallery_scroll_page(state, &scroll, 4000, true, true);
}

/* The heading, collapsed - and the same heading expanded, one page up, so the pair is the
   before and after of the one piece of chrome that needed pixels to exist. */
void gallery_scene_scroll_title(struct inkcell_draw_state *state) {
    static struct inkcell_scroll scroll;
    gallery_scroll_page(state, &scroll, 0, false, true);
}
