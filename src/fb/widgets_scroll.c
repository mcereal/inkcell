#define _POSIX_C_SOURCE 200809L

/*
 * A region with more content than room: the viewport, its rail, and the heading that gives its
 * own row back as the body moves.
 *
 * See include/inkcell/ui/widgets/scroll.h. The viewport is deliberately three lines of
 * substance - a scroll's offset, a view pushed by it, a view popped - because the difficulty
 * in scrolling by pixels was never the drawing. It was that the drawing layer had one content
 * transform and it belonged to the frame.
 */

#include "inkcell/ui/widgets/scroll.h"

#include "inkcell/ui/widgets/button.h"
#include "inkcell/ui/widgets/chrome.h"

#include "inkcell/ui/layout.h"
#include "inkwell/base/text.h"

#include <string.h>

/* ---- the viewport ---------------------------------------------------------------------------- */

bool inkcell_fb_viewport_begin(struct inkcell_draw_state *state, struct inkcell_fb_viewport *view,
                               struct inkcell_fb_rect box, struct inkcell_scroll *scroll,
                               int32_t content_h) {
    if (view != NULL) {
        memset(view, 0, sizeof *view);
    }
    if (state == NULL || view == NULL || scroll == NULL || box.w <= 0 || box.h <= 0) {
        return false;
    }
    /*
     * The extent is set here rather than by the caller, because this is the one place that
     * holds both numbers at the same moment. A screen that set them itself would be a screen
     * that can measure its content against last frame's window - and a scroll measured against
     * a window that has moved is a scroll that can be past an end that is not there.
     */
    inkcell_scroll_extent(scroll, content_h, box.h);
    const int32_t offset = inkcell_scroll_offset(scroll, state->now_ms);

    view->box = box;
    view->offset = offset;
    /*
     * The window in the content's own coordinates, which is what a long list walks instead of
     * everything it holds.
     *
     * Inclusive of whatever is partly in view: the row cut by the top edge is on the panel,
     * and a screen that skipped it would leave a gap where the half-row goes. That is the
     * whole difference between this and a row-index window - the window has an edge that falls
     * inside a row, and saying so is what makes the scroll continuous.
     */
    view->visible = (struct inkcell_fb_rect){.x = 0, .y = offset, .w = box.w, .h = box.h};

    /*
     * The content's origin is the *window's* top-left, not the panel's: a row at content y of
     * 0 belongs at the top of the viewport. So the translation carries the box's own position
     * as well as the scroll - without it the content is laid out against the panel and the
     * window merely cuts a hole in it, which looks exactly like a scroll that has jumped by
     * however far down the panel the viewport happens to be.
     */
    view->pushed = inkcell_fb_view_push(state, box, box.x, box.y - offset);
    return view->pushed;
}

void inkcell_fb_viewport_end(struct inkcell_draw_state *state, struct inkcell_fb_viewport *view) {
    if (state == NULL || view == NULL) {
        return;
    }
    if (view->pushed) {
        inkcell_fb_view_pop(state);
        view->pushed = false;
    }
}

/* ---- the rail --------------------------------------------------------------------------------
 *
 * The same mark the list draws, measured from a pixel position instead of from a window of
 * items - and with the one behaviour a window of items could not have: it shortens when the
 * content is pulled past an end.
 */

/* The shortest a thumb may be drawn, in multiples of its own width: four, which is the number
   the list's rail already passes to inkcell_list_thumb(). A thumb shorter than that stops
   reading as a thumb and starts reading as a speck. */
#define INKCELL_FB_RAIL_MIN_THUMB 4

void inkcell_fb_draw_scroll_rail(const struct inkcell_draw_state *state,
                                 const struct inkcell_fb_viewport *view,
                                 const struct inkcell_scroll *scroll) {
    if (state == NULL || view == NULL || scroll == NULL || view->box.h <= 0) {
        return;
    }
    const int32_t max = inkcell_scroll_max(scroll);
    if (max <= 0) {
        return; /* it all fits, so there is nothing off screen to report */
    }

    /*
     * Sized and placed exactly as the list's rail is - see inkcell_fb_draw_list_rail(). A quarter
     * margin wide, centred in the strip outside the widest thing the content can draw, so the
     * two rails are the same mark in the same place and a screen that has one of each does not
     * look like a screen with two different scrollbars.
     *
     * Measured from the *viewport's* right edge rather than from the panel's, which is the
     * whole of the difference between this and a rail beside a list. A list fills the width of
     * the screen, so the two answers coincide and it was easy to write the panel's; a viewport
     * need not - a scrolling pane, or a sheet's content - and a rail derived from the panel
     * would then hang at the screen's edge with the thing it reports somewhere off to the
     * left, outside the surface it belongs to.
     *
     * The strip is derived the way inkcell_fb_row_box() derives the content's, so a viewport
     * that *does* span the panel puts its rail exactly where a list puts one: the row box is
     * the width less a gutter either side less the rail's own reserved strip, and the rail
     * stands in what is left with the hairline's clearance.
     */
    int width = inkcell_fb_gutter(state) / 2;
    if (width < 2) {
        width = 2;
    }
    const int strip_right = view->box.x + view->box.w;
    const int content_right =
        strip_right - inkcell_fb_gutter(state) - inkcell_fb_rail_gutter(state);
    int strip_x = content_right + inkcell_fb_edge(state);
    if (strip_x < view->box.x) {
        strip_x = view->box.x;
    }
    const int strip_w = strip_right - strip_x;
    if (strip_w < 3) {
        return;
    }
    if (width > strip_w - 2) {
        width = strip_w - 2;
    }

    const int track = view->box.h;
    /* The fraction of the content in view, as a length of track. */
    int length = (int)(((int64_t)track * (int64_t)view->box.h) /
                       (int64_t)(scroll->content > 0 ? scroll->content : 1));
    const int minimum = INKCELL_FB_RAIL_MIN_THUMB * width;
    if (length < minimum) {
        length = minimum;
    }
    if (length > track) {
        length = track;
    }

    /*
     * Where it sits, measured against the *travel* - the track less the thumb - rather than
     * against the track. That is the difference between a thumb that reaches the end exactly
     * when the last pixel of content is on screen and one that stops short and reports there
     * is more below. inkcell_list_thumb() states the same rule for the list's rail.
     */
    const int32_t at = view->offset < 0 ? 0 : (view->offset > max ? max : view->offset);
    int offset = (int)(((int64_t)(track - length) * (int64_t)at) / (int64_t)max);

    /*
     * And the thing the list's rail cannot do: the thumb shortens as the content is pulled
     * past an end, pinned at the end it is being pulled away from.
     *
     * A thumb that kept its length while the content stretched would be the one thing on the
     * panel disagreeing that anything had happened - which matters here more than it would
     * anywhere else, because on a handheld with no touch the stretch *is* the whole of the
     * feedback that says the list has ended. Two things saying it is what makes it read.
     */
    const int32_t over = inkcell_scroll_overscroll(scroll, state->now_ms);
    if (over != 0) {
        int shrink = over < 0 ? -over : over;
        if (shrink > length - minimum) {
            shrink = length - minimum;
        }
        if (shrink > 0) {
            length -= shrink;
            if (over > 0) {
                /* Pulled past the bottom: the thumb's bottom edge stays on the end of the
                   track and its top edge comes down to meet it. */
                offset = track - length;
            } else {
                offset = 0;
            }
        }
    }

    const int x = strip_x + (strip_w - width) / 2;
    const int radius = inkcell_fb_radius(state, INKCELL_SHAPE_FULL);
    /* The groove a meter's fill sits in, and quiet ink on it - the list's rail's pairing, and
       contract-checked the same way: INKCELL_TONE_DIM owes the ground 3:1 on every theme. */
    inkcell_fb_fill_round_rect(state, x, view->box.y, width, track, radius,
                               inkcell_fb_color(state, INKCELL_COLOR_METER_TRACK));
    inkcell_fb_fill_round_rect(state, x, view->box.y + offset, width, length, radius,
                               inkcell_fb_tone_color(state, INKCELL_TONE_DIM));
}

/* ---- the collapsing app bar -------------------------------------------------------------------
 */

/* The role the large heading is set in, which is what the whole shape is *for*: a heading that
   is merely the title size on its own row is a title with a gap over it. The display role is
   two steps over the body, which is exactly where this used to reach by hand - and it brings
   the rest of a display's setting with it, the tighter tracking a heading at that size needs
   and the tighter line that keeps the expanded bar from eating a row it does not use. */
static struct inkcell_type_style
inkcell_fb_large_title_style(const struct inkcell_draw_state *state) {
    return inkcell_fb_type_style(state, INKCELL_TYPE_DISPLAY);
}

static int inkcell_fb_large_title_line(const struct inkcell_draw_state *state) {
    const struct inkcell_type_style style = inkcell_fb_large_title_style(state);
    return inkcell_fb_line_adv_styled(state, &style);
}

int inkcell_fb_large_title_travel(const struct inkcell_draw_state *state) {
    if (state == NULL) {
        return 0;
    }
    return inkcell_fb_large_title_line(state);
}

/*
 * The drawing is the app bar's now - a large title is inkcell_fb_draw_app_bar() with `large` set,
 * so it has the actions, the overflow menu and the status mark a small heading has. This stays
 * as the name a screen with nothing but a heading already calls.
 */
void inkcell_fb_draw_large_title(const struct inkcell_draw_state *state,
                                 struct inkcell_fb_layout *layout,
                                 const struct inkcell_fb_large_title *bar, int32_t offset) {
    if (state == NULL || layout == NULL || bar == NULL) {
        return;
    }
    const struct inkcell_fb_app_bar heading = {
        .title = bar->title,
        .badge = bar->badge,
        .badge_family = bar->badge_family,
        .large = true,
        .offset = offset,
        .detail = bar->detail,
    };
    (void)inkcell_fb_draw_app_bar(state, layout, &heading);
}
