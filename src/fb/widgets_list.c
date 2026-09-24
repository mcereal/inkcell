#define _POSIX_C_SOURCE 200809L

/*
 * The list window and its furniture - and the disc, which a card heading draws here and a row's
 * leading slot draws in src/fb/widgets_item.c, and which is therefore neither's.
 */

#include "inkcell/ui/widgets/chrome.h"
#include "inkcell/ui/widgets/list.h"
#include "list_internal.h"

#include "inkcell/ui/emoji.h"
#include "inkcell/ui/layout.h"

#include <string.h>

/* ---- the disc ------------------------------------------------------------------------------
 *
 * Shared by two components and therefore neither's: a list row's leading slot draws one, and so
 * does a card heading over rows that carry them.
 */

/*
 * The disc and what is in it: a node's initials, or an icon for the rows that are not a person.
 * `size` is both its width and its height, so the radius is half of it and the shape is a
 * circle.
 *
 * What it holds is drawn at the largest multiplier that *fits inside the disc*, which is not
 * always the body's. A two-row item gives the disc two lines to be round in and the body scale
 * fits with room; a one-row item - a node, a picker row - gives it one, and two cells at the
 * body scale then overhang a circle barely taller than a single glyph. That drew initials
 * sliced off at both ends, which is worse than no disc at all: the whole job of the colour and
 * the two letters is to be recognised without being read.
 *
 * So the fit is measured rather than assumed. It is done here, once, because the caller cannot
 * answer it - which scale fits is a fact about this component's geometry, and a screen that had
 * to work it out would be computing a glyph size, which is the thing screens do not do. An icon
 * is measured by the same loop: it is one cell wide and drawn at the glyph body's height, which
 * is the taller of the two the loop tests.
 *
 * An icon takes the same ink the initials do and is blended over the fill it is standing on.
 *
 * The fill and that ink arrive as a pair rather than as a tint, which is what lets one disc
 * serve two meanings. An avatar's is a tint from the theme's avatar palette with the ground
 * colour on it, every one of which is validated that way; a leading tonal container's is a
 * family's held-back half with the ink that family states - and a component that took a colour
 * and chose the ink itself would be drawing the second combination against the first's contract.
 */
void inkcell_fb_draw_avatar(const struct inkcell_draw_state *state, int x, int y, int size,
                            const char *label, enum inkcell_icon icon, struct inkcell_paint paint) {
    inkcell_fb_fill_round_rect(state, x, y, size, size, size / 2, paint.fill);
    const bool has_icon = inkcell_icon_is_valid(icon);
    if (!has_icon && (label == NULL || label[0] == '\0')) {
        return;
    }
    /*
     * The inset a circle owes its contents: at the corners of the text box the disc has already
     * curved away, so text measured against the full diameter still touches the edge. An eighth
     * on each side is what keeps two cells clear of it at every scale the theme allows.
     */
    const int room = size - size / 4;
    int scale = state->scale;
    /* Measured at each scale rather than counted once: the initials are two letters of a name,
       and "WW" and "II" are the same two cells and nothing like the same width. */
    while (scale > INKCELL_SCALE(1) &&
           ((has_icon ? inkcell_fb_icon_box(state, scale)
                      : inkcell_fb_text_width(state, label, scale)) > room ||
            inkcell_scale_px((int)inkcell_fb_font(state)->height, scale) > room)) {
        scale -= INKCELL_SCALE(1);
    }

    /* Centred in cells, and vertically on the glyph body rather than the line advance - the
       advance carries the gap accents hang in, and counting it sits the initials low in the
       disc. The same reasoning as inkcell_fb_draw_button's label. */
    const int text_h = inkcell_scale_px((int)inkcell_fb_font(state)->height, scale);
    const int content_y = y + (size - text_h) / 2;
    if (has_icon) {
        inkcell_fb_draw_icon(state, x + (size - inkcell_fb_icon_box(state, scale)) / 2, content_y,
                             icon, scale, paint.ink, paint.fill);
        return;
    }
    const int text_w = inkcell_fb_text_width(state, label, scale);
    inkcell_fb_draw_text(state, x + (size - text_w) / 2, content_y, label, scale, paint.ink,
                         paint.fill);
}

/* ---- the list window ------------------------------------------------------------------------ */

/* Everything but the window, which is the one thing the three entry points differ in. */
static struct inkcell_fb_list inkcell_fb_list_open(const struct inkcell_fb_layout *layout,
                                                   struct inkcell_list model) {
    struct inkcell_fb_list list;
    memset(&list, 0, sizeof list);
    list.model = model;
    list.y = layout->body_y;
    list.line = layout->line;
    /* The window the rail measures, which is the body rather than the rows that happened to be
       filled: a list of three items in a body of fifteen has no rail at all, and a list of
       forty-two wants one the height of what a full window would have been. */
    list.track_y = layout->body_y;
    list.track_h = (int)layout->rows * layout->line;
    /* The body, until a caller says its window is smaller - see inkcell_fb_list_begin_visible(). */
    list.band_h = list.track_h;
    /* Resolved from AUTO by whichever entry point knows about a card array; a list with none is
       plain, which is what a list with none always looked like. */
    list.style.appearance = INKCELL_FB_LIST_PLAIN;
    return list;
}

struct inkcell_fb_list inkcell_fb_list_begin_visible(const struct inkcell_fb_layout *layout,
                                                     uint32_t count, uint32_t cursor,
                                                     uint32_t visible) {
    struct inkcell_fb_list list =
        inkcell_fb_list_open(layout, inkcell_list_begin(count, cursor, visible));
    /* The rows this list was given, which is what a glide may paint in. The rest of the body
       belongs to whatever the screen reserved it for. */
    list.band_h = (int)visible * layout->line;
    return list;
}

struct inkcell_fb_list inkcell_fb_list_begin(const struct inkcell_fb_layout *layout, uint32_t count,
                                             uint32_t cursor) {
    return inkcell_fb_list_begin_visible(layout, count, cursor, layout->rows);
}

struct inkcell_fb_list inkcell_fb_list_begin_rows(const struct inkcell_fb_layout *layout,
                                                  uint32_t count, uint32_t cursor,
                                                  uint32_t per_item) {
    /*
     * The division is the model's now rather than this line's, and that is not tidying: a body
     * of fifteen rows holding two-row items used to arrive here as a window of seven, and the
     * fifteenth row was rounded away before anything could know it had been there. The model is
     * told fifteen and two, so the slack stays a fact about the window - which is what lets a
     * list of mixed heights spend it on a one-row item.
     */
    const uint32_t step = per_item > 0U && per_item < 0xFFU ? per_item : 1U;
    return inkcell_fb_list_open(
        layout, inkcell_list_begin_step(count, cursor, layout->rows, (uint8_t)step));
}

struct inkcell_fb_list inkcell_fb_list_begin_heights(const struct inkcell_fb_layout *layout,
                                                     uint32_t count, uint32_t cursor,
                                                     const uint8_t *heights) {
    return inkcell_fb_list_open(layout,
                                inkcell_list_begin_heights(count, cursor, layout->rows, heights));
}

struct inkcell_fb_list inkcell_fb_list_begin_cards(const struct inkcell_fb_layout *layout,
                                                   uint32_t count, uint32_t cursor,
                                                   const uint8_t *heights, const uint8_t *cards) {
    struct inkcell_fb_list list =
        heights != NULL ? inkcell_fb_list_begin_heights(layout, count, cursor, heights)
                        : inkcell_fb_list_begin(layout, count, cursor);
    list.cards = cards;
    if (cards != NULL) {
        list.style.appearance = INKCELL_FB_LIST_CARD_GROUP;
    }
    return list;
}

struct inkcell_fb_list inkcell_fb_list_begin_focus(const struct inkcell_fb_layout *layout,
                                                   uint32_t count, uint32_t cursor,
                                                   const uint8_t *heights, const uint8_t *cards,
                                                   uint32_t first, uint32_t last, bool card) {
    struct inkcell_fb_list list = inkcell_fb_list_open(
        layout, inkcell_list_begin_span(count, cursor, first, last, layout->rows, heights));
    list.cards = cards;
    if (cards != NULL) {
        list.style.appearance = INKCELL_FB_LIST_CARD_GROUP;
    }
    list.focus_card = card && cards != NULL && count > 0U;
    return list;
}

struct inkcell_fb_list inkcell_fb_list_begin_styled(const struct inkcell_draw_state *state,
                                                    const struct inkcell_fb_layout *layout,
                                                    uint32_t count, uint32_t cursor,
                                                    const uint8_t *heights, const uint8_t *cards,
                                                    const struct inkcell_fb_list_style *style) {
    struct inkcell_fb_list_style look;
    memset(&look, 0, sizeof look);
    if (style != NULL) {
        look = *style;
    }
    /*
     * The step, which is the one thing a density changes: half of INKCELL_SPACE_LG above the
     * words and half below. Taken off the space scale rather than stated in pixels so that it
     * grows with the text - a reader who turned the text up wants roomier rows, not the same
     * few pixels round larger words.
     *
     * The window is then however many of those steps the body holds. The body itself does not
     * move: the rail still measures it, the glide is still clipped to it and a card still may
     * not run past its bottom - it simply holds fewer, taller steps.
     */
    const int pad = (look.density == INKCELL_FB_LIST_COMFORTABLE && state != NULL)
                        ? inkcell_fb_space(state, INKCELL_SPACE_LG) / 2
                        : 0;
    const int step = layout->line + 2 * pad;
    const int body_h = (int)layout->rows * layout->line;
    const uint32_t steps = step > 0 ? (uint32_t)(body_h / step) : layout->rows;
    struct inkcell_fb_list list = inkcell_fb_list_open(
        layout, heights != NULL ? inkcell_list_begin_heights(count, cursor, steps, heights)
                                : inkcell_list_begin(count, cursor, steps));
    list.line = step;
    list.pad = pad;
    /* The first baseline sits a pad below where a compact list's would, so the air is above the
       words as well as below them - and every later baseline follows by whole steps. */
    list.y += pad;
    list.cards = cards;
    if (look.appearance == INKCELL_FB_LIST_APPEARANCE_AUTO) {
        look.appearance = cards != NULL ? INKCELL_FB_LIST_CARD_GROUP : INKCELL_FB_LIST_PLAIN;
    }
    list.style = look;
    return list;
}

/* Whether item `index` draws the cursor's highlight. Never on a list whose card is focused
   instead - see inkcell_fb_list_begin_focus(). */
bool inkcell_fb_list_is_cursor(const struct inkcell_fb_list *list, uint32_t index) {
    return !list->focus_card && inkcell_list_is_cursor(&list->model, index);
}

/* Which card item `index` is on, or INKCELL_FB_LIST_NO_CARD. Past the end counts as no card, which
   is what lets the run walk below terminate without knowing the list's length. */
static uint8_t inkcell_fb_list_card_of(const struct inkcell_fb_list *list, uint32_t index) {
    if (list == NULL || index >= list->model.count) {
        return INKCELL_FB_LIST_NO_CARD;
    }
    /* A grouped look with no array is one group: a settings screen of six rows is a section
       whether or not it bothered to say so. A plain list with none has no groups at all, which
       is every list opened before there was a look to ask for. */
    if (list->cards == NULL) {
        return (list->style.appearance == INKCELL_FB_LIST_INSET_GROUPED ||
                list->style.appearance == INKCELL_FB_LIST_CARD_GROUP)
                   ? 0U
                   : INKCELL_FB_LIST_NO_CARD;
    }
    return list->cards[index];
}

/* Whether item `index` stands on a card at all - the ground a row is drawn against, where the
   run walk breaks, and whether the leading slot gives the cards beside it their hairline back. */
bool inkcell_fb_list_on_card(const struct inkcell_fb_list *list, uint32_t index) {
    return inkcell_fb_list_has_cards(list) &&
           inkcell_fb_list_card_of(list, index) != INKCELL_FB_LIST_NO_CARD;
}

/* Whether two items stand in one group - the question a separator and a section's corners both
   ask. A plain list with no array is one run, so a separator there falls between every row. */
static bool inkcell_fb_list_same_group(const struct inkcell_fb_list *list, uint32_t a, uint32_t b) {
    if (a >= list->model.count || b >= list->model.count) {
        return false;
    }
    const uint8_t card = inkcell_fb_list_card_of(list, a);
    if (card != inkcell_fb_list_card_of(list, b)) {
        return false;
    }
    return card != INKCELL_FB_LIST_NO_CARD || list->cards == NULL;
}

void inkcell_fb_list_section_ends(const struct inkcell_fb_list *list, uint32_t index, bool *first,
                                  bool *last) {
    const bool grouped =
        list != NULL && inkcell_fb_list_card_of(list, index) != INKCELL_FB_LIST_NO_CARD;
    if (first != NULL) {
        *first = !grouped || index == 0U || !inkcell_fb_list_same_group(list, index - 1U, index);
    }
    if (last != NULL) {
        *last = !grouped || !inkcell_fb_list_same_group(list, index, index + 1U);
    }
}

/* Whether this list draws its groups as cards at all, which is a question about the list and
   not about one row of it - see inkcell_fb_list_subheader_icon(), where a heading stands *between*
   two cards and so has a card list's ground under it either way. */
bool inkcell_fb_list_has_cards(const struct inkcell_fb_list *list) {
    return list != NULL && list->style.appearance != INKCELL_FB_LIST_PLAIN &&
           list->style.appearance != INKCELL_FB_LIST_APPEARANCE_AUTO;
}

/*
 * A card's vertical inset, and the gap it leaves between one card and the next.
 *
 * The same number inkcell_fb_draw_card() insets by, so a card in a list and a card on the Status
 * tab are padded alike. Asked in two places - the surface takes it below its last row, and a
 * group's heading is centred in what it leaves - which is why it is a function rather than a local.
 *
 * Never more than the heading's own step can spare, and that bound is not a belt-and-braces
 * clamp: at INKCELL_SCALE_MIN the type scale clamps the label *onto* the body - a label cannot
 * be rasterised below the smallest size the registry has - so a heading's cell is exactly as
 * tall as a row's and the only air in the step is one line gap. An inset taken out of that
 * leaves the cell longer than the gap it is centred in, the halved remainder truncates to zero,
 * and the last row of the cell lands on the next card's top edge: every heading with a
 * descender in it paints through the hairline, and only at the one scale nothing is rendered at.
 * Where there is no air the cards give up their inset rather than the heading its room.
 */
static int inkcell_fb_list_card_pad(const struct inkcell_draw_state *state) {
    const int edge = inkcell_fb_edge(state);
    int pad = inkcell_scale_px((int)inkcell_fb_metrics(state)->card_pad, state->scale) / 2;
    if (pad < edge) {
        pad = edge;
    }
    const int label = inkcell_theme_type_scale(state->theme, INKCELL_TYPE_LABEL, state->scale);
    const int spare = inkcell_fb_line_adv(state, state->scale) -
                      inkcell_scale_px((int)inkcell_fb_font(state)->height, label) - edge;
    if (pad > spare) {
        pad = spare;
    }
    return pad > 0 ? pad : 0;
}

/*
 * The corner a group's surface is rounded with, and so the corner the rows at its ends take.
 *
 * A card is an object and keeps the row highlight's own small corner, which is what lets the
 * two nest (see inkcell_fb_list_cards()). A section is a region of the panel, and a region is
 * rounded like a panel is - INKCELL_SHAPE_MD, the text field's and the card's on the Status tab.
 * The rows at its ends then take the same corner on the ends they share with it, and the rows
 * between take none, which is the whole of what "a section with rounded first and last rows"
 * means.
 */
int inkcell_fb_list_section_radius(const struct inkcell_draw_state *state,
                                   const struct inkcell_fb_list *list) {
    return inkcell_fb_radius(state,
                             list != NULL && list->style.appearance == INKCELL_FB_LIST_INSET_GROUPED
                                 ? INKCELL_SHAPE_MD
                                 : INKCELL_SHAPE_SM);
}

int inkcell_fb_list_box_top(const struct inkcell_draw_state *state,
                            const struct inkcell_fb_list *list) {
    return list->y - inkcell_step_px(state->scale) - list->pad;
}

/* Whether this list marks its cursor any way but the one every list always did - the one test
   that decides whether a plain row keeps inkcell_fb_draw_row_fill_on()'s own path. */
static bool inkcell_fb_list_restyled(const struct inkcell_fb_list *list) {
    return list->pad != 0 || list->style.focus != INKCELL_FB_LIST_FOCUS_FILL ||
           list->style.appearance == INKCELL_FB_LIST_INSET_GROUPED;
}

/* The colour the row's cursor mark is drawn in: its tone's family where it names one, the
   primary otherwise - `accent_edge`'s rule, stated once for the bar and the ring alike. */
static struct inkcell_rgb inkcell_fb_list_mark_color(const struct inkcell_draw_state *state,
                                                     enum inkcell_tone tone) {
    return inkcell_fb_tone_color(
        state, inkcell_tone_family(tone) != INKCELL_FAMILY_COUNT ? tone : INKCELL_TONE_PRIMARY);
}

struct inkcell_fb_list_cue inkcell_fb_list_cue(const struct inkcell_draw_state *state,
                                               const struct inkcell_fb_list *list, uint32_t index,
                                               int top, int h, enum inkcell_tone tone,
                                               bool accent_edge) {
    struct inkcell_fb_list_cue cue;
    cue.focused = inkcell_fb_list_is_cursor(list, index);
    cue.lifted = false;
    cue.rest = inkcell_fb_list_ground(list, index);
    cue.ground = inkcell_fb_color(state, cue.rest);
    if (!cue.focused) {
        return cue;
    }

    const struct inkcell_fb_row_box box = inkcell_fb_row_box(state);
    const int x = box.x;
    const int w = box.w;
    /*
     * The row's corners: every one of them on a list of rows, only the section's own on a row
     * standing in an inset section. A middle row's fill is square because the section is what
     * is rounded, and a highlight with four rounded corners in the middle of one reads as a
     * pill laid on the section rather than as a part of it.
     *
     * The section's last row reaches down into the inset the surface is padded by, so its fill
     * ends where the section does and its corners are the section's corners - rather than a
     * rounded fill floating a few pixels above a rounded edge, which is two curves where one is
     * meant.
     */
    const bool inset = list->style.appearance == INKCELL_FB_LIST_INSET_GROUPED &&
                       inkcell_fb_list_on_card(list, index);
    bool first = true;
    bool last = true;
    int radius = inkcell_fb_radius(state, INKCELL_SHAPE_SM);
    if (inset) {
        inkcell_fb_list_section_ends(list, index, &first, &last);
        radius = inkcell_fb_list_section_radius(state, list);
        if (last) {
            const int floor_y = list->track_y + list->track_h;
            h += inkcell_fb_list_card_pad(state);
            if (top + h > floor_y) {
                h = floor_y - top;
            }
        }
    }

    switch (list->style.focus) {
    case INKCELL_FB_LIST_FOCUS_ACCENT: {
        /*
         * The lightest layer there is, and a capsule down the leading edge. HOVERED rather than
         * FOCUSED on purpose: the capsule is what says where the cursor is, so the layer only has
         * to say which row the capsule belongs to - and a layer that light keeps every ink the
         * row had at rest legible without asking the theme for a second pair.
         */
        cue.ground =
            inkcell_fb_state_layer(state, cue.rest, INKCELL_COLOR_TEXT, INKCELL_STATE_HOVERED);
        inkcell_fb_fill_round_rect_ends(state, x, top, w, h, radius, cue.ground, first, last);
        /* Centred in the row's own leading padding - the strip between the fill's edge and
           where content starts - so it stands clear of both the section's edge and the row's
           first mark, and reads as belonging to the row rather than to the surface. */
        const int bar_w = inkcell_step_px(state->scale);
        const int inset_y = inkcell_fb_space(state, INKCELL_SPACE_XS);
        const int bar_h = h - 2 * inset_y;
        const int gutter = box.text_x - x;
        const int bar_x = gutter > bar_w ? x + (gutter - bar_w) / 2 : x;
        if (bar_w > 0 && bar_h > 0) {
            inkcell_fb_fill_round_rect(state, bar_x, top + inset_y, bar_w, bar_h, bar_w / 2,
                                       inkcell_fb_list_mark_color(state, tone));
        }
        return cue;
    }
    case INKCELL_FB_LIST_FOCUS_RING: {
        /* Nothing under the words. The ring is the frame's when the list registered with a
           focus map - one cursor, one ring - and the row's own otherwise, inside its box so it
           cannot land on a neighbour's. */
        if (list->focus_base == INKCELL_FOCUS_NONE) {
            const int ring = 2 * inkcell_fb_edge(state);
            inkcell_fb_stroke_round_rect(
                state, x, top, w, h,
                (first || last) ? radius : inkcell_fb_radius(state, INKCELL_SHAPE_SM), ring,
                inkcell_fb_list_mark_color(state, tone));
        }
        return cue;
    }
    case INKCELL_FB_LIST_FOCUS_FILL:
    default:
        break;
    }

    cue.lifted = true;
    cue.ground = inkcell_fb_focus_fill(state, cue.rest);
    if (accent_edge) {
        /*
         * The bar is laid the way a card's edge is: the marker shape first, the fill over it a
         * scale narrower on the left only. Both share their right edge, so the marker survives
         * just where the bar is meant to be - and it follows the corner instead of poking a
         * square end out of it, which is what a straight bar does once the row has ends.
         */
        const int step = inkcell_step_px(state->scale);
        inkcell_fb_fill_round_rect_ends(state, x, top, w, h, radius,
                                        inkcell_fb_list_mark_color(state, tone), first, last);
        inkcell_fb_fill_round_rect_ends(state, x + step, top, w - step, h, radius, cue.ground,
                                        first, last);
    } else {
        inkcell_fb_fill_round_rect_ends(state, x, top, w, h, radius, cue.ground, first, last);
    }
    return cue;
}

struct inkcell_fb_list_cue inkcell_fb_list_row_cue(const struct inkcell_draw_state *state,
                                                   const struct inkcell_fb_list *list,
                                                   uint32_t index, uint32_t rows) {
    if (!inkcell_fb_list_restyled(list)) {
        /* The path every list took before it had a look, kept as the call it was: a plain row
           and a heading mark themselves through inkcell_fb_draw_row_fill_on(), and a list that
           asked for nothing new must come out of this the same pixels it always did. */
        struct inkcell_fb_list_cue cue;
        cue.focused = inkcell_fb_list_is_cursor(list, index);
        cue.lifted = cue.focused;
        cue.rest = inkcell_fb_list_ground(list, index);
        cue.ground = inkcell_fb_draw_row_fill_on(state, list->y, rows, cue.focused, cue.rest);
        return cue;
    }
    return inkcell_fb_list_cue(state, list, index, inkcell_fb_list_box_top(state, list),
                               (int)(rows > 0U ? rows : 1U) * list->line, INKCELL_TONE_NORMAL,
                               false);
}

void inkcell_fb_list_separator(const struct inkcell_draw_state *state,
                               const struct inkcell_fb_list *list, uint32_t index, int x,
                               int bottom) {
    if (!list->style.separators) {
        return;
    }
    /* Not across the break between two groups, not under the last row the window holds, and
       not against the cursor's row from either side: its fill or its ring is already an edge,
       and a hairline laid along one is the same line drawn twice, a pixel apart. */
    const uint32_t end = list->model.first + list->model.visible + list->tail_count;
    if (index + 1U >= end || !inkcell_fb_list_same_group(list, index, index + 1U) ||
        inkcell_list_is_cursor(&list->model, index) ||
        inkcell_list_is_cursor(&list->model, index + 1U)) {
        return;
    }
    const struct inkcell_fb_row_box box = inkcell_fb_row_box(state);
    const int right = box.x + box.w;
    if (right <= x) {
        return;
    }
    /* Inside this row's box, on the boundary: the next row's own box - and so its fill - starts
       exactly under it. */
    const int thickness = inkcell_fb_rule_height(state, state->scale);
    inkcell_fb_draw_rule(state, x, bottom - thickness, right - x, state->scale, INKCELL_COLOR_RULE);
}

enum inkcell_color inkcell_fb_list_ground(const struct inkcell_fb_list *list, uint32_t index) {
    return inkcell_fb_list_on_card(list, index) ? INKCELL_COLOR_SURFACE : INKCELL_COLOR_BG;
}

uint32_t inkcell_fb_list_row_height(const struct inkcell_fb_list *list, uint32_t index) {
    return inkcell_list_item_height(&list->model, index);
}

/*
 * The card surfaces, for a list whose groups are drawn as cards.
 *
 * One rounded panel per *contiguous run* of items sharing a card ordinal, painted before any row
 * puts ink down - a fill has to go under text rather than over it, and there is no alpha on this
 * panel to recover from getting that the wrong way round.
 *
 * The run walk is over the window rather than over the list, which is what makes this cost the
 * same on a node with six rows and a node with a hundred and twenty. Heights come from the model
 * for the same reason every other measurement here does: the list is the authority once it has
 * been told, so a card can never be a row out from the rows standing on it.
 */
static void inkcell_fb_list_cards(const struct inkcell_draw_state *state,
                                  struct inkcell_fb_list *list) {
    if (!inkcell_fb_list_has_cards(list) || list->model.visible == 0U) {
        return;
    }

    /*
     * The row fill's own rectangle with the hairline outside it.
     *
     * A card has to be at least as wide as the widest thing standing in it, and the widest thing
     * in a list is the cursor's highlight: drawn to the same rectangle, the highlight lands on
     * the hairline and paints it out for the length of one row, so the card appears to lose its
     * sides wherever the cursor is - and only there, which is the row the reader is looking at.
     * So the edge is spent outward, into the gutter inkcell_fb_rail_gutter() keeps clear beside the
     * box. The highlight then fills the card's interior exactly, which is where Material puts a
     * state layer inside a container - and the card, being the box plus its hairline, is the
     * widest thing on the list and so the thing the rail measures its clearance from.
     */
    const int edge = inkcell_fb_edge(state);
    const struct inkcell_fb_row_box box = inkcell_fb_row_box(state);
    const int x = box.x - edge;
    const int width = box.w + 2 * edge;
    /*
     * The row highlight's shape, not inkcell_fb_draw_card()'s - and that is the same rule as the
     * width, one axis over. A card's fill and a row's highlight are the same rectangle here (the
     * fill is the card inset by its hairline, which is exactly the row gutter), so wherever the two
     * disagree about a corner the highlight wins: it reaches its full width while the card is
     * still curving, and the cursor's ends stand outside the card on the first and last rows of
     * every group. Drawn to one shape they nest exactly, and the hairline stays outside the
     * highlight all the way round.
     */
    const int radius = inkcell_fb_list_section_radius(state, list);
    const bool inset = list->style.appearance == INKCELL_FB_LIST_INSET_GROUPED;

    /*
     * The card's vertical inset, and it is spent at the bottom only.
     *
     * The same number inkcell_fb_draw_card() insets by, so a card here and a card on the Status tab
     * are padded alike - but a list row is not a card row, and where that inset is *needed*
     * differs. A row's box is a line advance tall and a glyph's ink sits high in its cell, so the
     * top of the first row already carries most of a line's leading as air while the bottom of the
     * last carries none: its descenders run to the box's edge. Padding both ends equally would
     * leave the card top-heavy by exactly that leading. So the top of the box is the first row's
     * own, which is also what keeps the cursor's highlight inside the card on the row that opens
     * it, and the whole of the inset goes under the last row.
     */
    const int pad = inkcell_fb_list_card_pad(state);

    /* The window, widened by whatever the glide added: a card under the item sliding in has to
       be painted or the row arrives on the panel's own ground and flickers as it lands. */
    const uint32_t first = list->model.first - list->lead_count;
    const uint32_t last = list->model.first + list->model.visible + list->tail_count;
    /* Where the first visible row's fill starts, which is a glyph scale above its baseline -
       inkcell_fb_draw_row_fill()'s own top edge. A card measured from the baseline would sit a few
       pixels low and clip the ascenders of the row it opens with. */
    /* Where the first run's fill starts, displaced with the rows: the surfaces move with what
       stands on them, and the band is what keeps the moved edge inside the body. */
    int top = list->track_y - inkcell_step_px(state->scale) + list->glide_dy;
    for (uint32_t back = first; back < list->model.first; ++back) {
        top -= (int)inkcell_list_item_height(&list->model, back) * list->line;
    }
    const bool band = inkcell_fb_list_band_begin(list);
    uint32_t i = first;
    while (i < last && i < list->model.count) {
        const uint8_t card = inkcell_fb_list_card_of(list, i);
        int height = (int)inkcell_list_item_height(&list->model, i) * list->line;
        uint32_t run = i + 1U;
        while (run < last && run < list->model.count &&
               inkcell_fb_list_card_of(list, run) == card) {
            height += (int)inkcell_list_item_height(&list->model, run) * list->line;
            run++;
        }
        if (!inkcell_fb_list_on_card(list, i)) {
            top += height;
            i = run;
            continue;
        }

        /*
         * Whether this run is the whole card or a slice of one the window cut, asked of the
         * items on either side of it rather than of the window - which is the same question and
         * the one that stays right when a card happens to end exactly where the window does.
         *
         * A cut end keeps square corners and takes no padding, so the card runs to the edge of
         * the body and reads as continuing past it. Everything drawn here is inside the window
         * by construction: the first run starts at the body's own top edge and the last ends
         * where the visible steps do, so there is nothing to clip.
         */
        const bool cut_top = i > 0U && inkcell_fb_list_card_of(list, i - 1U) == card;
        const bool cut_bottom =
            run < list->model.count && inkcell_fb_list_card_of(list, run) == card;
        /*
         * The hairline is spent outward at the top, exactly as it is at the sides and for the
         * same reason: the first row of a card is a row the cursor can stand on, and a fill
         * drawn to the card's own rectangle lands on the edge and paints it out - so the card
         * reads as open at the top on precisely the row being pointed at.
         *
         * The ceiling is the first row's own top *less that hairline*, which is the correction
         * rather than the rule. Clamped to where the rows start, a card whose first row is the
         * body's first row - a list that opens on a card rather than on a heading, which is what
         * a node's verbs are - could not spend the hairline at all, and lost its top edge under
         * the cursor on the row it opens with: precisely the failure this whole paragraph is
         * about, reached from the one direction the clamp did not cover. The room is there to
         * spend: inkcell_fb_draw_app_bar() leaves a space and a gutter between the bar and body_y,
         * and an edge is one or two pixels of it.
         */
        int box_top = top;
        if (!cut_top) {
            box_top -= edge;
            const int ceiling = list->track_y - inkcell_step_px(state->scale) - edge;
            if (box_top < ceiling) {
                box_top = ceiling;
            }
        }
        int box_bottom = top + height;
        if (!cut_bottom) {
            /*
             * Into the step the group's next heading stands in, which is where the break between
             * two cards comes from - never past the body, or the bottom card of a list that
             * filled its window would put its edge through the action bar.
             *
             * A heading is the only step a card is ever padded into, and that is what makes the
             * inset safe to spend whole: a heading is drawn small and centres itself in whatever
             * room is left, where a full row is a line advance with a glyph cell in it and a
             * leading disc nearly as tall as the step, so a card padding into one would land its
             * edge on the disc's crown. Nothing hands this function a card followed straight by a
             * panel row any more - a settings group is one card whatever is in it, verbs
             * included - so the step below a card is a heading or it is the end of the list.
             */
            box_bottom += pad;
            const int floor_y = list->track_y + list->track_h;
            if (box_bottom > floor_y) {
                box_bottom = floor_y;
            }
        }
        const int box_h = box_bottom - box_top;

        /*
         * The edge first and the fill inside it, which is inkcell_fb_draw_card()'s shape and for
         * its reason: inkcell_fb_fill_round_rect() fills rather than strokes, so an outline is the
         * larger shape with the smaller one laid over it. The hairline is not decoration - on every
         * theme that ships the surface is one step off the ground, and the edge is most of what
         * says a card is there. It is OUTLINE rather than RULE for inkcell_fb_draw_card()'s reason
         * too: a separator may fade politely into what it divides and an edge may not.
         *
         * A cut end loses its inset along with its corners. Insetting there would draw the
         * hairline *across* the cut, which is the card claiming to end again - in a straight
         * line this time.
         */
        /*
         * The card the cursor stands on, when it stands on a card: inkcell_fb_draw_card()'s focus
         * ring, in the accent and twice the hairline, grown inward so nothing else on the list
         * moves. The rows of a focused card draw no highlight, so there is nothing for it to paint
         * over.
         */
        const bool focused =
            list->focus_card && list->model.cursor >= i && list->model.cursor < run;
        /*
         * An inset section is the surface and nothing else: no hairline, because a section is
         * not an object and an edge is what says one is there. It keeps the card's geometry to
         * the pixel - the hairline's width is still spent outward, only in the surface's own
         * colour - so a row's fill nests inside a section exactly as it does inside a card, and
         * the two looks can be swapped on one screen without a row moving.
         */
        if (inset && !focused) {
            inkcell_fb_fill_round_rect_ends(state, x, box_top, width, box_h, radius + edge,
                                            inkcell_fb_color(state, INKCELL_COLOR_SURFACE),
                                            !cut_top, !cut_bottom);
            top += height;
            i = run;
            continue;
        }
        const int ring = focused ? 2 * edge : edge;
        inkcell_fb_fill_round_rect_ends(state, x, box_top, width, box_h, radius + edge,
                                        focused ? inkcell_fb_tone_color(state, INKCELL_TONE_PRIMARY)
                                                : inkcell_fb_color(state, INKCELL_COLOR_OUTLINE),
                                        !cut_top, !cut_bottom);
        const int inner_top = cut_top ? box_top : box_top + ring;
        const int inner_bottom = cut_bottom ? box_top + box_h : box_top + box_h - ring;
        const int inner_radius = radius + edge - ring > 0 ? radius + edge - ring : 0;
        inkcell_fb_fill_round_rect_ends(
            state, x + ring, inner_top, width - 2 * ring, inner_bottom - inner_top, inner_radius,
            inkcell_fb_color(state, INKCELL_COLOR_SURFACE), !cut_top, !cut_bottom);

        top += height;
        i = run;
    }
    inkcell_fb_list_band_end(list, band);
}

/*
 * The scroll rail beside a window of items. Drawn once per list, by the first row that draws -
 * see inkcell/ui/widgets.h.
 *
 * It stands in inkcell_fb_rail_gutter()'s strip, which every list has already been measured to
 * leave clear - so it is beside the content rather than over it, on a flat list and on a column of
 * cards alike. Where that strip *is* is asked of inkcell_fb_row_box() rather than worked out from
 * the margin: the card spends its hairline outward from the box, so the free space starts one
 * hairline past the box's own edge and a rail measured from the margin lands on the card.
 *
 * The window and the track arrive separately because the two are not the same fact: the window
 * is what the proportion is derived from and the track is how much panel the mark has to say it
 * in. A grid is what made that worth splitting - its window is a list of *rows* while its track
 * is the body the tiles fill - and it is why this is not a `static` of the list's own. The mark
 * itself is the same mark in the same strip either way, which is the whole reason there is one
 * of these rather than two.
 */
void inkcell_fb_draw_list_rail(const struct inkcell_draw_state *state,
                               const struct inkcell_list *window, int track_y, int track_h) {
    if (state == NULL || window == NULL || track_h <= 0) {
        return;
    }

    /*
     * Sized from the gutter it lives in rather than from the glyph scale, unlike every other
     * control here. The gutter is half a margin wide and does not grow when a theme asks for
     * bigger text, so a rail measured in glyph steps ran out of clearance and ended up flush
     * against the panel edge at the larger scales. A quarter-margin leaves the same gap either
     * side at every scale, which is what makes it read as inset rather than as a screen edge.
     */
    int width = inkcell_fb_gutter(state) / 2;
    if (width < 2) {
        width = 2;
    }

    /*
     * The free strip: from the outer edge of the widest thing the list draws - the box plus the
     * hairline a card spends outward - to one margin past the content column. The rail is
     * centred in it, so the gap to the content and the gap to the column's edge are the same
     * number and neither is a constant anybody has to keep in step with the card.
     *
     * A margin past the *column*, not to the panel edge, and the difference only shows once the
     * two stop being the same place. On a compact surface the column ends one margin short of
     * the panel, so this is the panel edge exactly, which is where the rail has always been.
     * Where the column is capped and centred there is a whole empty half-panel beyond it, and a
     * strip measured to the panel would centre the rail in *that* - stranding it in the middle
     * of nowhere with the list it reports on far to its left. A rail is beside its content or
     * it is not a rail.
     *
     * Narrowed rather than moved if the strip cannot hold it with clearance either side: a rail
     * touching the card is what this exists to prevent, and a thinner one still reports the
     * scroll.
     */
    const struct inkcell_fb_row_box box = inkcell_fb_row_box(state);
    const int strip_x = box.x + box.w + inkcell_fb_edge(state);
    const int strip_right =
        inkcell_fb_content_x(state) + inkcell_fb_content_w(state) + inkcell_fb_margin(state);
    const int strip_w = strip_right - strip_x;
    if (strip_w < 3) {
        return;
    }
    if (width > strip_w - 2) {
        width = strip_w - 2;
    }

    /* The proportion is inkcell_list_thumb()'s - no pixels in it, and unit tested there. A
       length of 0 is a list that fits, which draws nothing at all rather than a full track. */
    const struct inkcell_scroll_thumb thumb = inkcell_list_thumb(window, track_h, 4 * width);
    if (thumb.length <= 0) {
        return;
    }

    const int x = strip_x + (strip_w - width) / 2;
    const int radius = inkcell_fb_radius(state, INKCELL_SHAPE_FULL);

    /* The track is the role that already means one - the same groove a meter's fill sits in -
       and the thumb is quiet ink, named as a tone the way an icon in a row slot is. That pairing
       is contract-checked: INKCELL_TONE_DIM owes the ground 3:1 on every theme, so the thumb is
       findable on all four, which a second neutral role chosen by eye against the track was
       not. */
    inkcell_fb_fill_round_rect(state, x, track_y, width, track_h, radius,
                               inkcell_fb_color(state, INKCELL_COLOR_METER_TRACK));
    inkcell_fb_fill_round_rect(state, x, track_y + thumb.offset, width, thumb.length, radius,
                               inkcell_fb_tone_color(state, INKCELL_TONE_DIM));
}

/*
 * Everything the list draws that is not a row: the card surfaces its groups stand on, then the
 * scroll rail.
 *
 * **No screen asks for it.** It is drawn by the first row that draws, because both halves are
 * derived entirely from the model - `count`, `first`, `visible`, the heights and the grouping -
 * so a screen has nothing to say about either and a screen that had to remember the call is a
 * screen that would forget on one list out of nine. Same reasoning the list item's clipping
 * follows: geometry belongs down here and the screen describes content.
 *
 * The order is the only thing this function decides, and it decides it once: a surface goes
 * under the ink standing on it, and the rail is outside both.
 */
void inkcell_fb_list_chrome(const struct inkcell_draw_state *state, struct inkcell_fb_list *list) {
    if (list == NULL || list->chrome_drawn) {
        return;
    }
    list->chrome_drawn = true;
    inkcell_fb_list_cards(state, list);
    inkcell_fb_draw_list_rail(state, &list->model, list->track_y, list->track_h);
}

/* ---- the glide ------------------------------------------------------------------------------
 *
 * What is remembered between frames is one window position. Everything else here is derived
 * from it and from the model the current frame opened with.
 */

/*
 * How far the content moved between two windows, in pixels: the heights of the items between
 * them, signed so that a window moving forward comes out positive.
 *
 * Asked of the model rather than multiplied out, because a list of mixed heights moves by what
 * is actually between the two tops - a two-line conversation cell and a one-line row are not
 * the same step, and a glide computed from a row count would undershoot on one and overshoot
 * on the other.
 */
static int inkcell_fb_list_span(const struct inkcell_fb_list *list, uint32_t from, uint32_t to) {
    const uint32_t lo = (from < to) ? from : to;
    const uint32_t hi = (from < to) ? to : from;
    int span = 0;
    for (uint32_t i = lo; i < hi && i < list->model.count; ++i) {
        span += (int)inkcell_list_item_height(&list->model, i) * list->line;
    }
    return (from < to) ? span : -span;
}

/*
 * Past this, a window did not step - it went somewhere.
 *
 * Half the window, because that is where the two views stop overlapping in any useful sense:
 * a filter emptying a list, or a jump to its end, has no pixel in common with where it came
 * from, and sliding between them is a smear rather than a movement. It is stated as a fraction
 * of the window rather than a row count because a press does not move a window by a row - the
 * model keeps a few rows of context ahead of the cursor (see list_lookahead() in src/layout.c),
 * so crossing the edge moves it by several at once, and a cap in rows would have refused the
 * ordinary case on the ordinary panel.
 *
 * A window only a few rows tall can take a step wider than this and will jump instead. That is
 * the right way round: on a window that small the two views really do share almost nothing.
 */
static int inkcell_fb_list_glide_cap(const struct inkcell_fb_list *list) {
    return list->track_h / 2;
}

/* The band a gliding list draws inside: the body, from the top edge of where its first row's
   fill starts to the bottom of the window it was opened against. */
bool inkcell_fb_list_band_begin(const struct inkcell_fb_list *list) {
    if (list == NULL || list->glide_state == NULL) {
        return false;
    }
    const int top = list->track_y - inkcell_step_px(list->glide_state->scale);
    inkcell_fb_shift_begin(list->glide_state, 0, top, top + list->band_h);
    return true;
}

void inkcell_fb_list_band_end(const struct inkcell_fb_list *list, bool began) {
    if (began) {
        inkcell_fb_shift_end(list->glide_state);
    }
}

void inkcell_fb_list_glide(struct inkcell_draw_state *state, struct inkcell_fb_list *list,
                           uint32_t id) {
    if (state == NULL || list == NULL || id == 0U) {
        return;
    }
    struct inkcell_fb_list_glide *slot = &state->list_glide;
    const uint32_t first = list->model.first;

    /*
     * A frame already travelling keeps its one transform. The window is still remembered, so
     * the list glides again from the next frame rather than from wherever the transition left
     * it.
     */
    if (state->shift_active) {
        slot->id = id;
        slot->first = first;
        slot->from = 0;
        inkcell_anim_set(&slot->travel, 0);
        return;
    }

    if (slot->id != id) {
        /* Another list's window, or the first sight of this one: adopt it. Nothing glides on a
           frame that has nowhere to have come from. */
        slot->id = id;
        slot->first = first;
        slot->from = 0;
        inkcell_anim_set(&slot->travel, 0);
    } else if (slot->first != first) {
        /*
         * The window moved. What is left of any glide still running is added to the new step
         * rather than thrown away - a reader holding the d-pad down is one continuous movement,
         * and restarting from the new step alone would stutter once per row.
         */
        const int32_t left = inkcell_anim_value(&slot->travel, state->now_ms);
        const int carried = (int)(((int64_t)slot->from * left) / INKCELL_ANIM_ONE);
        const int step = inkcell_fb_list_span(list, slot->first, first);
        const int total = carried + step;
        const int cap = inkcell_fb_list_glide_cap(list);
        slot->first = first;
        if (total > cap || total < -cap) {
            slot->from = 0;
            inkcell_anim_set(&slot->travel, 0);
        } else {
            slot->from = total;
            inkcell_anim_set(&slot->travel, INKCELL_ANIM_ONE);
            inkcell_anim_to(&slot->travel, state->now_ms, 0,
                            inkcell_fb_motion(state, INKCELL_FB_LIST_GLIDE_MOTION),
                            INKCELL_EASE_OUT);
        }
    }

    const int32_t at = inkcell_anim_value(&slot->travel, state->now_ms);
    const int dy = (int)(((int64_t)slot->from * at) / INKCELL_ANIM_ONE);
    if (dy == 0) {
        return;
    }

    list->glide_state = state;
    list->glide_dy = dy;
    list->y += dy;

    /*
     * The gap, and how many items it takes to fill it.
     *
     * Content pushed down leaves one above the window and content pulled up leaves one below -
     * but not *one item*: a press moves the window by as many rows as the model's lookahead
     * gives back, so the gap is as many items as fit in the displacement. Counting them here is
     * what keeps a glide from showing a strip of bare panel where the rows it is leaving should
     * be.
     *
     * Bounded by the list's own ends, so a window at the top glides against nothing above it,
     * which is what the top of a list looks like.
     */
    if (dy > 0) {
        int covered = 0;
        while (covered < dy && list->lead_count < first) {
            list->lead_count += 1U;
            covered +=
                (int)inkcell_list_item_height(&list->model, first - list->lead_count) * list->line;
        }
        list->y -= covered;
        list->lead_pending = list->lead_count;
    } else {
        int covered = 0;
        uint32_t after = first + list->model.visible;
        while (covered < -dy && after + list->tail_count < list->model.count) {
            covered +=
                (int)inkcell_list_item_height(&list->model, after + list->tail_count) * list->line;
            list->tail_count += 1U;
        }
        list->tail_pending = list->tail_count;
    }

    /* The whole body is moving, so the whole body is this frame's to repaint - a partial redraw
       that took the window's word for what changed would leave the rows that slid. */
    const int top = list->track_y - inkcell_step_px(state->scale);
    inkcell_fb_animation_damage(state, inkcell_fb_region(state).x, top, inkcell_fb_region(state).w,
                                list->track_h);
}

void inkcell_fb_list_focus(struct inkcell_fb_list *list, uint32_t base) {
    if (list == NULL) {
        return;
    }
    list->focus_base = base;
}

void inkcell_fb_list_focus_row(const struct inkcell_draw_state *state,
                               const struct inkcell_fb_list *list, uint32_t index, int y, int h) {
    if (list == NULL || list->focus_base == INKCELL_FOCUS_NONE) {
        return;
    }
    /* The row fill, which is the box the cursor's highlight covers - not the text span inside
       it and not the panel. A cursor that could reach a rectangle other than the one the
       highlight draws would be a screen disagreeing with itself about where the reader is. */
    const struct inkcell_fb_row_box box = inkcell_fb_row_box(state);
    struct inkcell_fb_rect rect = {.x = box.x, .y = y, .w = box.w, .h = h};
    /*
     * A gliding row is clipped to the window, and what the clip took is not on the frame - so
     * it is not something the cursor may reach either. Registering the whole box would put an
     * id at a rectangle the band had cut away: the finder would answer with a row nobody can
     * see, and the ring, which is drawn outside the band, would draw the proof of it over
     * whatever is under the list.
     *
     * The part that survives is registered rather than nothing, because half a row is still a
     * row the reader is looking at.
     */
    if (list->glide_state != NULL) {
        const int band_top = list->track_y - inkcell_step_px(state->scale);
        const int band_bottom = band_top + list->band_h;
        const int top = (rect.y > band_top) ? rect.y : band_top;
        const int bottom = (rect.y + rect.h < band_bottom) ? rect.y + rect.h : band_bottom;
        if (bottom <= top) {
            return;
        }
        rect.y = top;
        rect.h = bottom - top;
    }
    /* INKCELL_SHAPE_SM because that is what inkcell_fb_draw_row_fill_on() rounds the highlight
       with, and the ring and the highlight describing one row have to be one shape. */
    /* On an inset section the rows at its ends are the section's corner, and the frame's ring
       landing on one is that shape too - the ring drawn round a row whose top is rounded like the
       section is a ring that agrees with the surface it is standing on. */
    enum inkcell_shape shape = INKCELL_SHAPE_SM;
    if (list->style.appearance == INKCELL_FB_LIST_INSET_GROUPED &&
        inkcell_fb_list_on_card(list, index)) {
        bool first = false;
        bool last = false;
        inkcell_fb_list_section_ends(list, index, &first, &last);
        if (first || last) {
            shape = INKCELL_SHAPE_MD;
        }
    }
    inkcell_fb_focus_register_shaped(state, list->focus_base + index, &rect, shape);
    /* And the cursor's row is where the frame's ring goes. A list drawn under a sheet still has
       a cursor, which is why this is a mark rather than a claim: the sheet's rows come later and
       take it. */
    if (inkcell_fb_list_is_cursor(list, index)) {
        inkcell_fb_focus_mark(state, list->focus_base + index);
    }
}

bool inkcell_fb_list_next(struct inkcell_fb_list *list, uint32_t *index) {
    /* The items above the window come first and the ones below it come last, each in the order
       they are drawn - the walk is also the y cursor's path down the body. */
    if (list->lead_pending > 0U) {
        *index = list->model.first - list->lead_pending;
        list->lead_pending -= 1U;
        return true;
    }
    if (inkcell_list_next(&list->model, index)) {
        return true;
    }
    if (list->tail_pending > 0U) {
        *index = list->model.first + list->model.visible + (list->tail_count - list->tail_pending);
        list->tail_pending -= 1U;
        return true;
    }
    return false;
}

void inkcell_fb_list_row(const struct inkcell_draw_state *state, struct inkcell_fb_list *list,
                         uint32_t index, const char *text, enum inkcell_tone tone) {
    inkcell_fb_list_chrome(state, list);
    /* Drawn out rather than through inkcell_fb_draw_row(), which lays its fill on the panel's own
       ground: a row in a list may be standing on a card, and the ink its glyph edges blend into
       has to be the colour actually under it. */
    const bool band = inkcell_fb_list_band_begin(list);
    const struct inkcell_fb_list_cue cue =
        inkcell_fb_list_row_cue(state, list, index, inkcell_fb_list_row_height(list, index));
    const int text_x = inkcell_fb_row_box(state).text_x;
    inkcell_fb_draw_text(state, text_x, list->y, text, state->scale,
                         cue.lifted ? inkcell_fb_focus_ink(state, tone, false)
                                    : inkcell_fb_tone_color(state, tone),
                         cue.ground);
    /* By what the model says this row is, not by one row: a plain row in a list of mixed
       heights is still whatever height that list gave it, and advancing by a row would put
       every row under it in the wrong place. */
    const int height = (int)inkcell_fb_list_row_height(list, index) * list->line;
    /* The fill's top and not the baseline: inkcell_fb_draw_row_fill_on() paints from
       `y - scale`, and a box registered a baseline's lift below the highlight is a cursor
       whose geometry disagrees with the one thing on the panel that shows where it is. The
       slotted item registers its own `fill_top`, which is this same number arrived at from the
       other side. */
    const int top = inkcell_fb_list_box_top(state, list);
    inkcell_fb_list_separator(state, list, index, text_x, top + height);
    inkcell_fb_list_band_end(list, band);
    inkcell_fb_list_focus_row(state, list, index, top, height);
    list->y += height;
}

/*
 * One tier of a row's text: the tone on the ground, and inkcell_fb_focus_ink() over the fill.
 *
 * A row under the cursor is drawn against a different fill, so "which ink" is two questions and
 * not one - on a lifted row the answer is usually the same ink, on a filled theme it is not - and a
 * tier that is quiet on the ground has to stay quiet on the fill or a label column flashes to full
 * strength on precisely the row being read. INKCELL_TONE_DIM is what says a tier is the quiet one,
 * which is the same thing it says everywhere else the theme answers for ink.
 *
 * Shared by the headline, its label column and the supporting line, because the three were
 * three copies of this conditional and the supporting one had already grown a flag of its own.
 */
struct inkcell_rgb inkcell_fb_item_ink(const struct inkcell_draw_state *state,
                                       enum inkcell_tone tone, bool focused, bool quiet) {
    if (!focused) {
        return inkcell_fb_tone_color(state, tone);
    }
    return inkcell_fb_focus_ink(state, tone, quiet);
}

void inkcell_fb_list_subheader(const struct inkcell_draw_state *state, struct inkcell_fb_list *list,
                               uint32_t index, const char *text) {
    inkcell_fb_list_subheader_icon(state, list, index, text,
                                   (struct inkcell_fb_leading){.kind = INKCELL_FB_LEADING_NONE});
}

void inkcell_fb_list_subheader_icon(const struct inkcell_draw_state *state,
                                    struct inkcell_fb_list *list, uint32_t index, const char *text,
                                    struct inkcell_fb_leading leading) {
    inkcell_fb_list_chrome(state, list);
    const bool band = inkcell_fb_list_band_begin(list);
    const int scale = inkcell_theme_type_scale(state->theme, INKCELL_TYPE_LABEL, state->scale);
    const uint32_t rows = inkcell_fb_list_row_height(list, index);
    /* The fill is the whole step whatever size the words are, and it is the same rectangle a
       plain row lays down - a highlight that shrank to the label would be a cursor that changes
       shape as it walks down a list. */
    const struct inkcell_fb_list_cue cue = inkcell_fb_list_row_cue(state, list, index, rows);
    const bool focused = cue.focused;
    const struct inkcell_rgb ground = cue.ground;

    /*
     * Sat on the bottom of the step, so the space the smaller glyphs free is air above the
     * heading rather than under it. That is the whole of what makes it read as a section break:
     * the gap belongs to the group beginning, not to the row that ended.
     *
     * On a column of cards it is centred instead, because there the heading is not a break
     * between two runs of rows - it is the label of the card under it, standing in the gap
     * between that card and the one that ended. The gap is the whole of this step bar the inset
     * the card above took out of its top (inkcell_fb_list_cards()), and the *cell* is what is
     * centred in it rather than the line advance: a line carries its leading at the top, so
     * centring the advance would seat the words low and leave the heading hanging off the card
     * above.
     */
    const int step_top = inkcell_fb_list_box_top(state, list);
    const int step_h = (int)rows * list->line;
    int baseline =
        list->y + inkcell_fb_line_adv(state, state->scale) - inkcell_fb_line_adv(state, scale);
    if (inkcell_fb_list_has_cards(list)) {
        const int gap_top = step_top + inkcell_fb_list_card_pad(state);
        const int gap_bottom = step_top + step_h - inkcell_fb_edge(state);
        const int cell = inkcell_scale_px((int)inkcell_fb_font(state)->height, scale);
        baseline = gap_top + (gap_bottom - gap_top - cell) / 2;
    }
    /*
     * Indented to where its own rows start, when the list declares a leading slot.
     *
     * A heading that stayed at the margin over rows whose words begin an icon-box further in is
     * a heading naming a column nothing is in, which is the two-column problem the leading slot
     * already refuses one row at a time. The slot is measured at the *body* scale, not the
     * label scale this draws at, because it is the rows' gutter being matched rather than one
     * of this row's own.
     *
     * Whether anything is *drawn* in it is the card distinction, and it is asked of the *list*
     * rather than of this row's ground - a heading on a column of cards stands between two of
     * them, so the ground under it is the panel's either way. On a flat list the slot stays
     * empty: a heading there is a break between runs of rows, and a symbol on it would be a
     * second thing saying what the words underneath say - the icons on such a list are what each
     * row is about, and a group has no single answer to that. On a column of cards the heading
     * names the card below it, and there the symbol is the cell the eye finds when it is looking
     * for Signal rather than Identity, which is exactly what struct inkcell_fb_card's icon is for.
     * The list is asked which it is drawing, so a caller passes the icon either way and nothing
     * decides twice - and the indent is the same whether or not it was drawn.
     */
    /*
     * And the ink, from the same question the icon is: what this heading *is*.
     *
     * On a flat list it is a break between two runs of rows, so it stays quiet on the ground and
     * quiet on the fill alike - it is not one of the rows the cursor came here to read, and a
     * loud break would be the screen shouting its own furniture.
     *
     * On a column of cards it is the card's label, and there quiet is wrong twice over. It is
     * the cell the eye lands on when it is looking for Signal rather than Identity on a screen a
     * hundred and twenty rows long, which is the argument its icon is already drawn for - and
     * inkcell_fb_draw_card() has been inking the heading beside *its* icon in the card's own tone
     * since the Status tab got cards, so a heading dimmed here was the two card kinds holding two
     * opinions about the same line. The primary is what a card with nothing wrong with it takes
     * there, and it is what a group of a node's facts is: the brand colour marking where the
     * reader is meant to look, which is the whole of what this palette keeps it for.
     *
     * Asked of the list rather than declared by the caller, exactly as the icon's own drawn/not
     * drawn is: the list knows which of the two it is drawing, so a screen passes a heading and
     * nothing decides twice.
     */
    /* A section's heading is the quiet one even on a grouped list: a section is a region rather
       than an object, so its label is a caption over it and not a title for it - the words above
       an inset group on every phone. A card keeps its title in the primary. */
    const enum inkcell_tone tone = list->style.appearance == INKCELL_FB_LIST_CARD_GROUP
                                       ? INKCELL_TONE_PRIMARY
                                       : INKCELL_TONE_DIM;
    const struct inkcell_rgb ink =
        inkcell_fb_item_ink(state, tone, cue.lifted, tone == INKCELL_TONE_DIM);

    int x = inkcell_fb_row_box(state).text_x;
    if (leading.kind == INKCELL_FB_LEADING_TONAL || leading.kind == INKCELL_FB_LEADING_TONAL_SLOT) {
        /*
         * The card's symbol in a disc, which is what a heading over rows that carry discs has to
         * be: the slot is the rows' gutter being matched, so a heading that drew a bare icon
         * there would name a column a disc narrower than the one its rows start in.
         *
         * It is also the header every phone app gives a card - a circled mark beside the card's
         * name - and the two readings are the same one, which is why this takes the kind rather
         * than a flag. The disc wears the heading's own tone, exactly as a row's does.
         */
        const int gutter = list->line - inkcell_step_px(state->scale);
        if (inkcell_fb_list_has_cards(list)) {
            /*
             * Sized to the heading's own words, not to the break it stands in, and centred in
             * that break both ways.
             *
             * The gap is what a disc has to *fit inside*, and it is the wrong thing to measure
             * one against: the break between two cards is a body row less the inset the card
             * above took out of its top, so a disc grown to fill it comes out exactly as tall as
             * the gap and lands with its crown on the hairline above and its foot on the
             * hairline below. Nothing overlaps, so no edge check catches it - it just reads as a
             * bead jammed between two panels.
             *
             * Half again the cell the words are drawn in is the size a mark beside a heading
             * wants, and it scales with the type rather than with the furniture: a theme asking
             * for bigger text gets a bigger disc, and one asking for roomier cards gets more air
             * around the same one. The break is then only a clamp, and it keeps a clearance
             * step at each end - INKCELL_SPACE_SM, which is what that step is named for - so the
             * disc is seen to be *in* the gap rather than wedged between the two cards the gap
             * separates. Half a step is not enough to read as clearance at any scale this
             * ships: it leaves two pixels, and two pixels of ground between a disc and a
             * hairline looks exactly like the disc touching it.
             */
            const int inset = inkcell_fb_space(state, INKCELL_SPACE_SM);
            const int gap_top = step_top + inkcell_fb_list_card_pad(state);
            const int gap_h = step_top + step_h - inkcell_fb_edge(state) - gap_top;
            const int cell = inkcell_scale_px((int)inkcell_fb_font(state)->height, scale);
            const int room = gap_h - 2 * inset;
            int size = cell + cell / 2;
            if (size > room) {
                size = room;
            }
            if (size > gutter) {
                size = gutter;
            }
            const enum inkcell_family family = inkcell_tone_family(tone);
            const struct inkcell_paint disc = inkcell_fb_paint(
                state, family != INKCELL_FAMILY_COUNT ? family : INKCELL_FAMILY_PRIMARY,
                INKCELL_SLOT_CONTAINER, INKCELL_STATE_REST);
            /* The empty slot takes the gutter and draws nothing in it, which is the whole of
               what it is for here: a group whose subject this client has no rune for - "Sent
               with a position" is a sentence about ten bits - still has to begin where its rows
               begin. Without it the heading stood at the panel's margin over rows indented past
               a disc, which is the heading "naming a column nothing is in" that the note above
               is about, seen from the one side that had no answer. */
            if (size > 0 && leading.kind == INKCELL_FB_LEADING_TONAL) {
                inkcell_fb_draw_avatar(state, x + (gutter - size) / 2, gap_top + (gap_h - size) / 2,
                                       size, NULL, leading.icon, disc);
            }
        }
        x += gutter + inkcell_fb_char_adv(state, state->scale) / 2;
    } else if (leading.kind != INKCELL_FB_LEADING_NONE) {
        if (inkcell_icon_is_valid(leading.icon) && inkcell_fb_list_has_cards(list)) {
            /* On the heading's own baseline rather than the body's: this row draws at the label
               scale, and a symbol standing where a body row's would floats a third of a row
               clear of the word it belongs to. */
            inkcell_fb_draw_icon(state, x, baseline, leading.icon, scale, ink, ground);
        }
        x +=
            inkcell_fb_icon_box(state, state->scale) + inkcell_fb_char_adv(state, state->scale) / 2;
    }
    struct inkcell_line line;
    inkcell_line_reset(&line);
    inkcell_line_printf(&line, "%s", text != NULL ? text : "");
    inkcell_line_fit(&line, inkcell_fb_row_cols(state, scale));
    /* A section heading is a step *down* in size, which on its own reads as text that got
       smaller rather than as a break. The weight is what makes it a heading. */
    inkcell_fb_draw_text_weight(state, x, baseline, inkcell_line_text(&line), scale,
                                inkcell_fb_type_weight(state, INKCELL_TYPE_LABEL), ink, ground);
    inkcell_fb_list_band_end(list, band);
    /*
     * Registered only when the cursor is on it. A heading is a title, not a destination: most
     * lists never park the cursor on one, and a box for every heading would give a click and
     * the d-pad somewhere to land that the list's own model refuses. But a list that *does* put
     * the cursor here has drawn the focused fill, and the ring has to have the same box to go
     * to - so that one heading goes in, the same box a plain row registers, and is marked.
     */
    if (focused) {
        inkcell_fb_list_focus_row(state, list, index, step_top, step_h);
    }
    list->y += (int)rows * list->line;
}

/* ---- the note row -------------------------------------------------------------------------- */

/* The width a note's body wraps to. The list's own columns: a paragraph indented past the rows
   around it would be a second left margin on a panel that has room for one. */
static size_t inkcell_fb_note_cols(const struct inkcell_draw_state *state) {
    return inkcell_fb_row_cols(state, state->scale);
}

uint32_t inkcell_fb_list_note_steps(const struct inkcell_draw_state *state, const char *heading,
                                    const char *body) {
    if (state == NULL) {
        return 1U;
    }
    /* The heading costs a step even though it is drawn small, because a step is a body row and
       the list model counts in those. The air the smaller glyphs free goes above it, exactly as
       it does on a subheader. */
    uint32_t steps = (heading != NULL && heading[0] != '\0') ? 1U : 0U;
    steps += inkcell_wrap_lines(body != NULL ? body : "", inkcell_fb_note_cols(state));
    /* A note with nothing in it is still a row: a zero-height row would put every row under it
       at the wrong offset, which is the failure the whole heights mechanism exists to prevent. */
    return steps > 0U ? steps : 1U;
}

void inkcell_fb_list_note(const struct inkcell_draw_state *state, struct inkcell_fb_list *list,
                          uint32_t index, const char *heading, const char *body) {
    inkcell_fb_list_chrome(state, list);
    const bool band = inkcell_fb_list_band_begin(list);
    const uint32_t rows = inkcell_fb_list_row_height(list, index);
    /* One fill for the whole note, the height the *model* gave it - not the height its words
       want. The two agree when the screen measured with inkcell_fb_list_note_steps(), and when they
       do not it is the model that is right, because it is what every row below was placed against.
     */
    const struct inkcell_fb_list_cue cue = inkcell_fb_list_row_cue(state, list, index, rows);
    const bool focused = cue.focused;
    const bool lifted = cue.lifted;
    const struct inkcell_rgb ground = cue.ground;

    const int margin = inkcell_fb_row_box(state).text_x;
    const int body_line = inkcell_fb_line_adv(state, state->scale);
    const bool titled = heading != NULL && heading[0] != '\0';
    int y = list->y;

    if (titled) {
        const int scale = inkcell_theme_type_scale(state->theme, INKCELL_TYPE_LABEL, state->scale);
        /* Sat on the bottom of its step for the reason a subheader is: the gap the small glyphs
           leave belongs above the heading, separating it from the paragraph that ended. */
        struct inkcell_line line;
        inkcell_line_reset(&line);
        inkcell_line_printf(&line, "%s", heading);
        inkcell_line_fit(&line, inkcell_fb_row_cols(state, scale));
        /*
         * The ink is chosen with the fill rather than beside it. A heading painted in the
         * primary whether or not the row was focused is a pair no theme was measured against -
         * the accent over the selection fill is the one combination the contrast contract does
         * not cover, because on a light palette they are two shades of the same hue.
         */
        inkcell_fb_draw_text(state, margin, y + body_line - inkcell_fb_line_adv(state, scale),
                             inkcell_line_text(&line), scale,
                             lifted ? inkcell_fb_focus_ink(state, INKCELL_TONE_PRIMARY, false)
                                    : inkcell_fb_tone_color(state, INKCELL_TONE_PRIMARY),
                             ground);
        y += body_line;
    }

    /*
     * The paragraph, one wrapped line per step.
     *
     * Clipped to the row's own height rather than to the panel: a note the model was told is
     * three steps tall draws three lines and stops, so a measure that disagreed with the words
     * loses the tail of a sentence instead of painting it over the next note. Losing text is
     * visible; overlapping it is not, which is the trade this file makes everywhere.
     */
    uint32_t drawn = titled ? 1U : 0U;
    struct inkcell_wrap wrap;
    inkcell_wrap_begin(&wrap, body != NULL ? body : "", inkcell_fb_note_cols(state));
    while (drawn < rows && inkcell_wrap_next(&wrap)) {
        inkcell_fb_draw_text(state, margin, y, wrap.line, state->scale,
                             lifted ? inkcell_fb_focus_ink(state, INKCELL_TONE_NORMAL, false)
                                    : inkcell_fb_tone_color(state, INKCELL_TONE_NORMAL),
                             ground);
        y += body_line;
        drawn++;
    }

    inkcell_fb_list_band_end(list, band);
    /* The box the fill covers, when the cursor is on it - for the heading's reason, above. */
    if (focused) {
        inkcell_fb_list_focus_row(state, list, index, inkcell_fb_list_box_top(state, list),
                                  (int)rows * list->line);
    }
    list->y += (int)rows * list->line;
}

void inkcell_fb_list_row_line(const struct inkcell_draw_state *state, struct inkcell_fb_list *list,
                              uint32_t index, struct inkcell_line *line, enum inkcell_tone tone) {
    /* The row's columns, not the panel's: inkcell_fb_rail_gutter() is kept clear of the box every
       row is drawn in, so a line fitted to inkcell_fb_cols() is a line fitted to a width no row
       has. */
    inkcell_line_fit(line, inkcell_fb_row_cols(state, state->scale));
    inkcell_fb_list_row(state, list, index, inkcell_line_text(line), tone);
}
