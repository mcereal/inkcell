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
#include "inkcell/utils/text.h"

#include <string.h>

/* ---- the viewport ---------------------------------------------------------------------------- */

bool inkcell_fb_viewport_begin(struct inkcell_backend_fb_state *state,
                               struct inkcell_fb_viewport *view, struct inkcell_fb_rect box,
                               struct inkcell_scroll *scroll, int32_t content_h) {
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

void inkcell_fb_viewport_end(struct inkcell_backend_fb_state *state,
                             struct inkcell_fb_viewport *view) {
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

void inkcell_fb_draw_scroll_rail(const struct inkcell_backend_fb_state *state,
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
     * Sized and placed exactly as the list's rail is - see inkcell_fb_list_rail(). A quarter
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

/* A colour mixed `progress` of the way towards another, in permille. What a cross-fade is on a
   panel with no alpha: text is drawn as coverage against a stated ground, so ink moved towards
   that ground is a partly faded glyph - drawn by the same blend that anti-aliases its edges. */
static uint8_t inkcell_fb_mix8(uint8_t from, uint8_t to, int32_t progress) {
    const int32_t span = (int32_t)to - (int32_t)from;
    return (uint8_t)((int32_t)from + (span * progress) / INKCELL_ANIM_ONE);
}

static struct inkcell_rgb inkcell_fb_fade(struct inkcell_rgb ink, struct inkcell_rgb ground,
                                          int32_t progress) {
    return (struct inkcell_rgb){
        .r = inkcell_fb_mix8(ink.r, ground.r, progress),
        .g = inkcell_fb_mix8(ink.g, ground.g, progress),
        .b = inkcell_fb_mix8(ink.b, ground.b, progress),
    };
}

/* The two heights the bar moves between: collapsed is an ordinary app bar with no trail, and
   expanded is that plus one line of the large title. */
static int inkcell_fb_large_title_collapsed(const struct inkcell_backend_fb_state *state,
                                            const struct inkcell_fb_layout *layout) {
    return inkcell_fb_app_bar_height(state, layout, 0U);
}

static int inkcell_fb_large_title_line(const struct inkcell_backend_fb_state *state) {
    /* One step above the title role, which is the size the whole shape is *for*: a heading
       that is merely the title size on its own row is a title with a gap over it. */
    return inkcell_fb_line_adv(state, inkcell_fb_type_scale(state, INKCELL_TYPE_TITLE) + 1);
}

int inkcell_fb_large_title_travel(const struct inkcell_backend_fb_state *state) {
    if (state == NULL) {
        return 0;
    }
    return inkcell_fb_large_title_line(state);
}

void inkcell_fb_draw_large_title(const struct inkcell_backend_fb_state *state,
                                 struct inkcell_fb_layout *layout,
                                 const struct inkcell_fb_large_title *bar, int32_t offset) {
    if (state == NULL || layout == NULL || bar == NULL) {
        return;
    }
    const int travel = inkcell_fb_large_title_travel(state);
    const int collapsed_h = inkcell_fb_large_title_collapsed(state, layout);

    /*
     * How far in it is, 0 fully expanded and ONE fully collapsed.
     *
     * Past the top of the content - a body being pulled down - is *not* clamped away: the
     * heading grows, which is the other half of what an overscroll is for and what every
     * platform does with a large title being pulled. The growth is the raw overscroll rather
     * than a fraction of it, because it is already through the band by the time it arrives
     * here.
     */
    int32_t progress = 0;
    int grown = 0;
    if (offset > 0 && travel > 0) {
        progress = (int32_t)(((int64_t)offset * INKCELL_ANIM_ONE) / travel);
        if (progress > INKCELL_ANIM_ONE) {
            progress = INKCELL_ANIM_ONE;
        }
    } else if (offset < 0) {
        grown = -offset;
    }

    const int large_line = inkcell_fb_large_title_line(state);
    const int extra = large_line - (int)(((int64_t)large_line * progress) / INKCELL_ANIM_ONE);
    const int height = collapsed_h + extra + grown;

    const int margin = inkcell_fb_margin(state);
    const int small = inkcell_fb_type_scale(state, INKCELL_TYPE_LABEL);
    const int title_scale = inkcell_fb_type_scale(state, INKCELL_TYPE_TITLE);
    const int large_scale = title_scale + 1;
    const struct inkcell_rgb ground = inkcell_fb_color(state, INKCELL_COLOR_BG);

    /* The bar's own ground, so the two titles have something honest to fade against - a fade
       towards a colour that is not what is actually behind the glyph is a glyph with a halo. */
    inkcell_fb_fill_rect(state, 0, layout->body_y, (int)state->var.xres, height, ground);

    int right = (int)state->var.xres - margin;
    /*
     * The badge sits on both sizes of the bar and never fades.
     *
     * It is a *state* rather than a decoration - "unsaved", "3 waiting" - and the collapsed
     * bar is precisely the one a reader is looking at while they scroll, so a badge that faded
     * out as the heading shrank would take the fact with it at the moment it is most wanted.
     */
    const int badge_w = inkcell_fb_badge_width(state, bar->badge, small);
    if (badge_w > 0) {
        const int badge_h = (int)inkcell_fb_font(state)->height * small;
        const int text_y = layout->body_y + (collapsed_h - badge_h) / 2;
        const struct inkcell_fb_rect box = {.x = right - badge_w,
                                            .y = text_y - small,
                                            .w = badge_w,
                                            .h = inkcell_fb_line_adv(state, small) - small};
        inkcell_fb_draw_badge(state, &box, text_y, bar->badge, bar->badge_family, small);
        right -= badge_w + inkcell_fb_char_adv(state, small);
    }

    const int back_w = layout->back ? inkcell_fb_icon_box(state, title_scale) : 0;
    const int text_x = layout->back ? margin + back_w + inkcell_fb_char_adv(state, small) : margin;
    if (layout->back) {
        /* The leading affordance, on the bar at both sizes and never faded: what B does is not
           less true while the heading is large. */
        inkcell_fb_draw_icon(state, margin, layout->body_y + (collapsed_h - back_w) / 2,
                             INKCELL_ICON_BACK, title_scale,
                             inkcell_fb_tone_color(state, INKCELL_TONE_DIM), ground);
    }

    /*
     * The small title, in the bar. Faded *in* as the heading collapses, so it is absent while
     * the large one has the screen's name and takes over as that one goes.
     */
    if (progress > 0 && bar->title != NULL) {
        const struct inkcell_rgb ink =
            inkcell_fb_fade(inkcell_fb_tone_color(state, INKCELL_TONE_PRIMARY), ground,
                            INKCELL_ANIM_ONE - progress);
        char fitted[INKCELL_LINE_MAX];
        inkcell_str_copy(fitted, sizeof fitted, bar->title);
        while (inkcell_fb_text_width(state, fitted, title_scale) > right - text_x) {
            const size_t cells = inkcell_text_cells(fitted);
            if (cells <= 1U) {
                break;
            }
            inkcell_text_cell_truncate(fitted, cells - 1U);
        }
        inkcell_fb_draw_text_weight(state, text_x, layout->body_y, fitted, title_scale,
                                    inkcell_fb_type_weight(state, INKCELL_TYPE_TITLE), ink, ground);
    }

    /*
     * And the large one, on its own row under the bar, faded out as it goes.
     *
     * Its row is the room that is being given back, so it is drawn at the bottom of whatever
     * of that room is left - which is what makes the heading slide up into the bar rather than
     * shrink in place. The detail beside it is the first thing to go: the collapsed bar has
     * room for a title and a badge, not for all three.
     */
    if (extra > 0 && bar->title != NULL) {
        const int y = layout->body_y + collapsed_h + extra - large_line;
        const struct inkcell_rgb ink =
            inkcell_fb_fade(inkcell_fb_tone_color(state, INKCELL_TONE_PRIMARY), ground, progress);
        int large_right = (int)state->var.xres - margin;
        if (bar->detail != NULL && bar->detail[0] != '\0') {
            const struct inkcell_rgb dim =
                inkcell_fb_fade(inkcell_fb_color(state, INKCELL_COLOR_TEXT_DIM), ground, progress);
            const int w = inkcell_fb_text_width(state, bar->detail, small);
            const int lift =
                (inkcell_fb_line_adv(state, large_scale) - inkcell_fb_line_adv(state, small)) / 2;
            inkcell_fb_draw_text(state, large_right - w, y + lift, bar->detail, small, dim, ground);
            large_right -= w + inkcell_fb_space(state, INKCELL_SPACE_SM);
        }
        char fitted[INKCELL_LINE_MAX];
        inkcell_str_copy(fitted, sizeof fitted, bar->title);
        while (inkcell_fb_text_width(state, fitted, large_scale) > large_right - margin) {
            const size_t cells = inkcell_text_cells(fitted);
            if (cells <= 1U) {
                break;
            }
            inkcell_text_cell_truncate(fitted, cells - 1U);
        }
        inkcell_fb_draw_text_weight(state, margin, y, fitted, large_scale,
                                    inkcell_fb_type_weight(state, INKCELL_TYPE_TITLE), ink, ground);
    }

    /*
     * The rule under it, once the heading has gone.
     *
     * Faded in with the collapse rather than always drawn, and that is the whole argument for
     * having one at all: a rule under an expanded heading is a line between a screen's name
     * and its content, which are the same thing. A rule under a *collapsed* bar is what says
     * the content has been scrolled underneath it - which is the one fact the shape exists to
     * carry, and the reason a bar that has swallowed its heading does not read as a bar that
     * never had one.
     */
    if (progress > 0) {
        const struct inkcell_rgb rule = inkcell_fb_fade(inkcell_fb_color(state, INKCELL_COLOR_RULE),
                                                        ground, INKCELL_ANIM_ONE - progress);
        inkcell_fb_fill_rect(state, 0, layout->body_y + height - inkcell_fb_rule_height(state, 1),
                             (int)state->var.xres, inkcell_fb_rule_height(state, 1), rule);
    }

    /*
     * What is left of the body, recomputed from the body's real bottom rather than deducted.
     *
     * inkcell_fb_draw_app_bar()'s rule, and its reason: `rows` is a floored division, so the
     * body carries a remainder the count never included and subtracting a row count charges
     * the bar for it twice. It matters more here than there, because this height changes every
     * frame while the heading is collapsing - a deduction would drift.
     */
    layout->body_y += height;
    layout->rows = inkcell_fb_layout_rows(state, layout);
}
