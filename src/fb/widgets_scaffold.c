#define _POSIX_C_SOURCE 200809L

/*
 * The scaffold: which of the frame's arrangements this width class gets, and the two ways of
 * drawing destinations that the tab strip does not cover - a bar across the bottom and a rail
 * down the side.
 *
 * Both are built from the same indicator - a pill with the icon in it and the badge on its
 * shoulder - and the same button paint, so a destination lights, hovers and badges the same way
 * wherever it is drawn. What differs is where its word goes: under the pill in the bar, beside it
 * in an expanded rail, and nowhere in a collapsed one. See the rail's section for why the rail
 * does not borrow the bar's stacked cell.
 */

#include "inkcell/ui/widgets/scaffold.h"

#include "inkcell/ui/emoji.h"

#include <string.h>

/* ---- the destination cell ------------------------------------------------------------------- */

/*
 * The scale a destination's icon is drawn at: the body's, one step above its label.
 *
 * Larger than the tab strip's, which draws icon and word at one size because they sit side by
 * side on one line. Here the icon stands *over* its word, and a cell whose picture is no bigger
 * than the caption under it reads as a caption with a dot on top - the icon is what the eye finds
 * a destination by, which is why Material sets it half again the label's size.
 */
static int scaffold_icon_scale(const struct inkcell_draw_state *state) {
    return inkcell_fb_type_scale(state, INKCELL_TYPE_BODY);
}

/* The pill behind a destination's icon: wide and short, the proportion Material draws its active
   indicator at, so a destination reads as a place rather than as a round button. Stated from the
   spacing scale so a roomier theme gets a roomier pill. */
static struct inkcell_fb_rect scaffold_pill_size(const struct inkcell_draw_state *state, int icon,
                                                 int small) {
    return (struct inkcell_fb_rect){
        .w = inkcell_fb_icon_box(state, icon) +
             2 * inkcell_fb_space_at(state, INKCELL_SPACE_LG, icon),
        .h = inkcell_fb_line_adv(state, icon) + inkcell_fb_space_at(state, INKCELL_SPACE_SM, small),
    };
}

static struct inkcell_fb_rect scaffold_indicator_size(const struct inkcell_draw_state *state,
                                                      int small) {
    return scaffold_pill_size(state, scaffold_icon_scale(state), small);
}

/* One cell's height: air, the pill, a hairline of air, the label, air. The same in a bar and in a
   rail - and the same whether or not the labels fit, so a translation that drops them does not
   also move the body. */
static int scaffold_cell_height(const struct inkcell_draw_state *state, int small) {
    const int air = inkcell_fb_space_at(state, INKCELL_SPACE_MD, small);
    return air + scaffold_indicator_size(state, small).h +
           inkcell_fb_space_at(state, INKCELL_SPACE_XS, small) + inkcell_fb_line_adv(state, small) +
           air;
}

static bool scaffold_has_badge(const struct inkcell_fb_chip *chip) {
    return chip->badge != NULL && chip->badge[0] != '\0';
}

/*
 * The badge, on the indicator's trailing shoulder.
 *
 * Over the pill rather than beside it, which is where the tab strip puts one and cannot here: a
 * cell is a column, so beside the pill is beside the *label*, and a count there reads as part of
 * the word. The shoulder is the one place that belongs to the icon and nothing else - which is
 * Material's answer for the same reason.
 *
 * A figure when the labels are showing and a dot when they are not, the strip's rule: a strip
 * down to bare icons has told the reader it has no room for words, and a figure is a word.
 */
static void scaffold_draw_badge(const struct inkcell_draw_state *state,
                                const struct inkcell_fb_rect *indicator, const char *badge,
                                bool figure) {
    const int caption = inkcell_fb_type_scale(state, INKCELL_TYPE_CAPTION);
    const int shoulder = indicator->x + indicator->w / 2 +
                         inkcell_fb_icon_box(state, scaffold_icon_scale(state)) / 2;
    if (!figure) {
        const int size = inkcell_fb_char_adv(state, caption) / 2 > 0
                             ? inkcell_fb_char_adv(state, caption) / 2
                             : 1;
        const struct inkcell_paint paint =
            inkcell_fb_paint(state, INKCELL_FAMILY_ERROR, INKCELL_SLOT_BASE, INKCELL_STATE_REST);
        inkcell_fb_fill_round_rect(state, shoulder - size / 2, indicator->y + size / 2, size, size,
                                   inkcell_fb_radius(state, INKCELL_SHAPE_FULL), paint.fill);
        return;
    }
    const int height = inkcell_fb_line_adv(state, caption);
    const struct inkcell_fb_rect box = {
        .x = shoulder - inkcell_fb_char_adv(state, caption) / 2,
        .y = indicator->y - height / 3,
        .w = inkcell_fb_badge_width(state, badge, caption),
        .h = height,
    };
    const int text_h = inkcell_scale_px((int)inkcell_fb_font(state)->height, caption);
    /* The error family, which is Material's badge and the one fill on the theme guaranteed not to
       be the indicator's own: a primary count on a primary pill is a count that is not there. */
    inkcell_fb_draw_badge(state, &box, box.y + (height - text_h) / 2, badge, INKCELL_FAMILY_ERROR,
                          caption);
}

/*
 * One destination, centred in `cell`: the pill, the icon in it, the label under it, the badge on
 * it, and the cell registered under the chip's focus id.
 *
 * The whole cell is registered rather than the pill, because the cell is what a pointer is aiming
 * at - the label is part of the target on every platform with a navigation bar - and a target the
 * size of the icon is a target a click on the word misses.
 */
static void scaffold_draw_cell(const struct inkcell_draw_state *state,
                               const struct inkcell_fb_rect *cell,
                               const struct inkcell_fb_chip *chip, bool active, bool label,
                               int small) {
    const struct inkcell_fb_rect size = scaffold_indicator_size(state, small);
    const int gap = inkcell_fb_space_at(state, INKCELL_SPACE_XS, small);
    const int line = inkcell_fb_line_adv(state, small);
    const int content_h = size.h + gap + line;
    const struct inkcell_fb_rect indicator = {
        .x = cell->x + (cell->w - size.w) / 2,
        .y = cell->y + (cell->h - content_h) / 2,
        .w = size.w,
        .h = size.h,
    };

    inkcell_fb_focus_register_shaped(state, chip->focus_id, cell, INKCELL_SHAPE_MD);

    const struct inkcell_fb_button button = {
        .rect = indicator,
        .icon = chip->icon,
        .variant = active ? INKCELL_FB_BUTTON_TONAL : INKCELL_FB_BUTTON_TEXT,
        .shape = INKCELL_SHAPE_FULL,
        .idle_tone = INKCELL_TONE_DIM,
        .ground = INKCELL_COLOR_SURFACE_LOW,
        .scale = scaffold_icon_scale(state),
        /* The cell is what is registered, and the indicator lights for anywhere in it. */
        .hover_id = chip->focus_id,
    };
    inkcell_fb_draw_button(state, &button);

    if (label && chip->label != NULL && chip->label[0] != '\0') {
        const enum inkcell_weight weight =
            active ? inkcell_fb_type_weight(state, INKCELL_TYPE_LABEL) : INKCELL_WEIGHT_REGULAR;
        const int width = inkcell_fb_text_width_weight(state, chip->label, small, weight);
        inkcell_fb_draw_text_weight(
            state, cell->x + (cell->w - width) / 2,
            indicator.y + indicator.h + gap + inkcell_step_px(small), chip->label, small, weight,
            inkcell_fb_tone_color(state, active ? INKCELL_TONE_NORMAL : INKCELL_TONE_DIM),
            inkcell_fb_color(state, INKCELL_COLOR_SURFACE_LOW));
    }
    if (scaffold_has_badge(chip)) {
        scaffold_draw_badge(state, &indicator, chip->badge, label);
    }
}

/* ---- the bottom bar ------------------------------------------------------------------------- */

int inkcell_fb_scaffold_bar_height(const struct inkcell_draw_state *state) {
    const int small = inkcell_fb_type_scale(state, INKCELL_TYPE_LABEL);
    return scaffold_cell_height(state, small) + inkcell_fb_rule_height(state, small);
}

/*
 * Which labels an equal-width bar can carry: all of them, the active one's alone, or none - the
 * strip's three steps (enum inkcell_fb_chip_labels), asked of a cell rather than of a run. A
 * label fits when it clears its cell by a gutter, so two neighbours never touch.
 */
static enum inkcell_fb_chip_labels scaffold_bar_labels(const struct inkcell_draw_state *state,
                                                       const struct inkcell_fb_chip *chips,
                                                       size_t count, size_t active, int cell_w,
                                                       int small) {
    const int room = cell_w - inkcell_fb_gutter(state);
    const enum inkcell_weight bold = inkcell_fb_type_weight(state, INKCELL_TYPE_LABEL);
    bool all = true;
    bool mine = false;
    for (size_t i = 0U; i < count; ++i) {
        const char *label = chips[i].label != NULL ? chips[i].label : "";
        const int width = inkcell_fb_text_width_weight(state, label, small,
                                                       i == active ? bold : INKCELL_WEIGHT_REGULAR);
        const bool fits = width <= room;
        all = all && fits;
        if (i == active) {
            mine = fits;
        }
    }
    if (all) {
        return INKCELL_FB_CHIP_LABELS_ALL;
    }
    return mine ? INKCELL_FB_CHIP_LABELS_SELECTED : INKCELL_FB_CHIP_LABELS_NONE;
}

static void scaffold_draw_bar(const struct inkcell_draw_state *state, struct inkcell_box box,
                              const struct inkcell_fb_scaffold *scaffold, int small) {
    inkcell_fb_fill_rect(state, box.x, box.y, box.w, box.h,
                         inkcell_fb_color(state, INKCELL_COLOR_SURFACE_LOW));
    /* The action bar's rule, upside down: the frame closes off at the bottom edge the way the
       tab strip closes it off at the top. */
    inkcell_fb_draw_rule(state, box.x, box.y, box.w, small, INKCELL_COLOR_RULE_STRONG);
    const int top = box.y + inkcell_fb_rule_height(state, small);

    const size_t count = scaffold->count;
    const int cell_w = box.w / (int)count;
    const enum inkcell_fb_chip_labels labels =
        scaffold_bar_labels(state, scaffold->destinations, count, scaffold->active, cell_w, small);
    for (size_t i = 0U; i < count; ++i) {
        /* Cut from the whole width rather than stepped by `cell_w`, so the remainder of the
           division is spread across the cells instead of left as a gap at the trailing edge. */
        const int left = box.x + (int)((int64_t)box.w * (int64_t)i / (int64_t)count);
        const int right = box.x + (int)((int64_t)box.w * (int64_t)(i + 1U) / (int64_t)count);
        const struct inkcell_fb_rect cell = {
            .x = left, .y = top, .w = right - left, .h = box.y + box.h - top};
        const bool label = labels == INKCELL_FB_CHIP_LABELS_ALL ||
                           (labels == INKCELL_FB_CHIP_LABELS_SELECTED && i == scaffold->active);
        scaffold_draw_cell(state, &cell, &scaffold->destinations[i], i == scaffold->active, label,
                           small);
    }
}

/* ---- the rail ------------------------------------------------------------------------------- */

/*
 * The rail comes in two widths, which is what every desktop application with a sidebar has
 * converged on: collapsed to a column of icons, and expanded into rows that carry the icon and
 * its word side by side. The icon-over-word cell the bar uses is not either of them. In a column
 * it spends a line of height per destination on a word set too small to scan, and it gets the
 * worst of both widths - wider than the icons need, narrower than the words want.
 *
 * Both widths put the icons in the same column, a gutter in from the leading edge, so a press on
 * the toggle reads as the words sliding out from behind the icons rather than the whole rail
 * being redrawn somewhere else. And both are one width whichever destination is active, so a
 * press down the rail never reflows the body.
 */

/*
 * The widest an expanded rail may be, in columns of body text.
 *
 * A rail is chrome, and chrome that took a fifth of a window to spell out one long translation
 * would be chrome taking the room the class change was meant to give the body. Past this the
 * rail stays collapsed, which is the elision the tab strip already does when it runs out of room.
 */
#define INKCELL_FB_RAIL_MAX_COLS 20U

/*
 * The narrowest an expanded rail is, in the same columns: room for a short word *and* a count
 * at the end of its row. Without a floor a rail of four-letter names is a column barely wider
 * than its icons, which reads as a collapsed rail that forgot to hide its words, and the first
 * badge to arrive would have nowhere to go.
 */
#define INKCELL_FB_RAIL_MIN_COLS 14U

/*
 * The rail's icons, a step above the bar's. On a panel the bar's icon sits over its word and the
 * pair make one mark; a rail's icon stands alone in a collapsed column, beside list rows whose
 * avatars and titles are set larger than the body, and at the body scale it read as the smallest
 * thing on the screen - a column of dots beside the thing it navigates.
 */
static int scaffold_rail_icon_scale(const struct inkcell_draw_state *state) {
    return inkcell_fb_type_scale(state, INKCELL_TYPE_TITLE);
}

static struct inkcell_fb_rect scaffold_rail_indicator_size(const struct inkcell_draw_state *state,
                                                           int small) {
    return scaffold_pill_size(state, scaffold_rail_icon_scale(state), small);
}

/* A row's height, in either width: the pill and the air above and below it. The same whether
   the words are showing, so the toggle moves nothing vertically. */
static int scaffold_rail_row_height(const struct inkcell_draw_state *state, int small) {
    return scaffold_rail_indicator_size(state, small).h +
           2 * inkcell_fb_space_at(state, INKCELL_SPACE_SM, small);
}

/* The widest label, measured bold whichever is active, so the rail is as wide as it will ever
   need to be and never changes width as the reader moves down it. */
static int scaffold_rail_widest(const struct inkcell_draw_state *state,
                                const struct inkcell_fb_chip *chips, size_t count, int small) {
    const enum inkcell_weight bold = inkcell_fb_type_weight(state, INKCELL_TYPE_LABEL);
    int most = 0;
    for (size_t i = 0U; i < count; ++i) {
        const int width = chips[i].label != NULL
                              ? inkcell_fb_text_width_weight(state, chips[i].label, small, bold)
                              : 0;
        most = width > most ? width : most;
    }
    return most;
}

/* The gap between an expanded row's icon and its word, and between the word and a badge. */
static int scaffold_rail_gap(const struct inkcell_draw_state *state, int small) {
    return inkcell_fb_space_at(state, INKCELL_SPACE_MD, small);
}

/* Where the icons stand, in either width: the pill's inset from the leading edge, then the air
   inside the pill. Asked by both widths so the column is the same one. */
static int scaffold_rail_icon_inset(const struct inkcell_draw_state *state) {
    return inkcell_fb_gutter(state) +
           inkcell_fb_space_at(state, INKCELL_SPACE_LG, scaffold_rail_icon_scale(state));
}

/* An expanded rail is never narrower than the window's buttons, where the host put them on the
   frame: its top-leading corner is where they sit (see scaffold_draw_rail()), and a sidebar holds
   its window's buttons, which is how every Mac app with one draws it. A collapsed rail does not:
   held to the buttons' width it is a column half again as wide as its icons with the icons
   against one side of it. It keeps its own width and the pane's heading steps clear of the
   buttons instead (see inkcell_fb_app_bar_margin()), as a Mac app with its sidebar hidden does. */
static int scaffold_rail_hold_buttons(const struct inkcell_draw_state *state, int width) {
    return width > state->top_leading_inset ? width : state->top_leading_inset;
}

int inkcell_fb_scaffold_rail_width(const struct inkcell_draw_state *state,
                                   const struct inkcell_fb_chip *destinations, size_t count) {
    if (state == NULL || destinations == NULL || count == 0U) {
        return 0;
    }
    const int small = inkcell_fb_type_scale(state, INKCELL_TYPE_LABEL);
    const int pill = scaffold_rail_indicator_size(state, small).w;
    return pill + 2 * inkcell_fb_gutter(state) + inkcell_fb_rule_height(state, small);
}

int inkcell_fb_scaffold_rail_expanded_width(const struct inkcell_draw_state *state,
                                            const struct inkcell_fb_chip *destinations,
                                            size_t count) {
    if (state == NULL || destinations == NULL || count == 0U) {
        return 0;
    }
    const int small = inkcell_fb_type_scale(state, INKCELL_TYPE_LABEL);
    const int icon = scaffold_rail_icon_scale(state);
    const int words = scaffold_rail_icon_inset(state) + inkcell_fb_icon_box(state, icon) +
                      scaffold_rail_gap(state, small) +
                      scaffold_rail_widest(state, destinations, count, small) +
                      inkcell_fb_space_at(state, INKCELL_SPACE_LG, icon) +
                      inkcell_fb_gutter(state) + inkcell_fb_rule_height(state, small);
    if (words > inkcell_fb_measure_width(state, INKCELL_FB_RAIL_MAX_COLS)) {
        return 0;
    }
    const int floor = inkcell_fb_measure_width(state, INKCELL_FB_RAIL_MIN_COLS);
    return scaffold_rail_hold_buttons(state, words > floor ? words : floor);
}

/*
 * The badge on a collapsed rail: on the pill's top trailing corner, and a figure rather than the
 * bar's dot, because a collapsed rail has chosen to lose its *words*, not its counts - the reader
 * folded it to give the body room, and an unread count is the one thing on it that changes while
 * they are reading something else.
 *
 * It starts a little inside the icon's trailing edge, as the bar's does, but its own trailing edge
 * is held inside the rail: the rail is only a gutter wider than the pill, and a two-figure count
 * hung off the shoulder would cross the rail's rule into the body.
 */
static void scaffold_draw_rail_badge(const struct inkcell_draw_state *state,
                                     const struct inkcell_fb_rect *indicator, const char *badge) {
    const int caption = inkcell_fb_type_scale(state, INKCELL_TYPE_CAPTION);
    const int height = inkcell_fb_line_adv(state, caption);
    const int width = inkcell_fb_badge_width(state, badge, caption);
    const int icon_box = inkcell_fb_icon_box(state, scaffold_rail_icon_scale(state));
    const int icon_right = indicator->x + (indicator->w + icon_box) / 2;
    const int limit = indicator->x + indicator->w + inkcell_fb_gutter(state) / 2;
    int x = icon_right - icon_box / 4;
    x = x + width > limit ? limit - width : x;
    /* Half above the pill, into the air the row leaves over it, so a two-figure count on a
       narrow rail covers the top of the icon rather than the middle of it. */
    const struct inkcell_fb_rect box = {
        .x = x,
        .y = indicator->y - height / 2,
        .w = width,
        .h = height,
    };
    const int text_h = inkcell_scale_px((int)inkcell_fb_font(state)->height, caption);
    inkcell_fb_draw_badge(state, &box, box.y + (height - text_h) / 2, badge, INKCELL_FAMILY_ERROR,
                          caption);
}

/* As much of `text` as `out` holds in whole cells, so a label longer than the buffer is cut
   between two characters rather than through one - an emoji or an accented letter is several
   bytes, and half of one draws as a replacement mark rather than as the start of a word. */
static void scaffold_copy_cells(char *out, size_t size, const char *text) {
    size_t used = 0U;
    for (;;) {
        const struct inkcell_text_cell cell = inkcell_text_cell_next(text + used);
        if (cell.bytes == 0U || used + cell.bytes >= size) {
            break;
        }
        used += cell.bytes;
    }
    memcpy(out, text, used);
    out[used] = '\0';
}

/*
 * One destination as a row of an expanded rail: the pill across the row, the icon in the icon
 * column, the word after it and the badge at the row's trailing end. The whole row is the target
 * and the whole row lights under the pointer, since a click on the word is aimed at it.
 */
static void scaffold_draw_rail_row(const struct inkcell_draw_state *state,
                                   const struct inkcell_fb_rect *row,
                                   const struct inkcell_fb_chip *chip, bool active, int small) {
    const int icon = scaffold_rail_icon_scale(state);
    const struct inkcell_fb_rect size = scaffold_rail_indicator_size(state, small);
    const int gutter = inkcell_fb_gutter(state);
    const struct inkcell_fb_rect pill = {
        .x = row->x + gutter,
        .y = row->y + (row->h - size.h) / 2,
        .w = row->w - 2 * gutter,
        .h = size.h,
    };
    inkcell_fb_focus_register_shaped(state, chip->focus_id, row, INKCELL_SHAPE_MD);

    const struct inkcell_fb_button button = {
        .rect = pill,
        .variant = active ? INKCELL_FB_BUTTON_TONAL : INKCELL_FB_BUTTON_TEXT,
        .shape = INKCELL_SHAPE_FULL,
        .idle_tone = INKCELL_TONE_DIM,
        .ground = INKCELL_COLOR_SURFACE_LOW,
        .scale = icon,
        .hover_id = chip->focus_id,
    };
    const struct inkcell_fb_button_paint paint = inkcell_fb_button_paint(state, &button);
    if (paint.has_fill) {
        inkcell_fb_fill_round_rect(state, pill.x, pill.y, pill.w, pill.h,
                                   inkcell_fb_radius(state, INKCELL_SHAPE_FULL), paint.paint.fill);
    }
    const struct inkcell_rgb ground =
        paint.has_fill ? paint.paint.fill : inkcell_fb_color(state, INKCELL_COLOR_SURFACE_LOW);
    /* On a fill the ink is the one the theme checked against it; with none, the idle tone the
       strip draws an inactive tab in. */
    const struct inkcell_rgb ink =
        paint.has_fill ? paint.paint.ink : inkcell_fb_tone_color(state, INKCELL_TONE_DIM);

    const int icon_x = row->x + scaffold_rail_icon_inset(state);
    const int icon_h = inkcell_scale_px((int)inkcell_fb_font(state)->height, icon);
    inkcell_fb_draw_icon(state, icon_x, pill.y + (pill.h - icon_h) / 2, chip->icon, icon, ink,
                         ground);

    const int gap = scaffold_rail_gap(state, small);
    const int text_x = icon_x + inkcell_fb_icon_box(state, icon) + gap;
    int end = pill.x + pill.w - inkcell_fb_space_at(state, INKCELL_SPACE_LG, icon);
    if (scaffold_has_badge(chip)) {
        const int caption = inkcell_fb_type_scale(state, INKCELL_TYPE_CAPTION);
        const int height = inkcell_fb_line_adv(state, caption);
        const int width = inkcell_fb_badge_width(state, chip->badge, caption);
        const struct inkcell_fb_rect box = {
            .x = end - width, .y = pill.y + (pill.h - height) / 2, .w = width, .h = height};
        const int text_h = inkcell_scale_px((int)inkcell_fb_font(state)->height, caption);
        inkcell_fb_draw_badge(state, &box, box.y + (height - text_h) / 2, chip->badge,
                              INKCELL_FAMILY_ERROR, caption);
        end = box.x - gap;
    }
    if (chip->label != NULL && chip->label[0] != '\0' && end > text_x) {
        const enum inkcell_weight weight =
            active ? inkcell_fb_type_weight(state, INKCELL_TYPE_LABEL) : INKCELL_WEIGHT_REGULAR;
        const int text_h = inkcell_scale_px((int)inkcell_fb_font(state)->height, small);
        /* A word that would run into the badge is cut rather than drawn under it; the width was
           measured for the words alone, so this is only a count much wider than a count. A word
           that fits is drawn as it came, whatever its length in bytes: only the cut is copied. */
        const struct inkcell_type_style style = {.scale = small, .weight = weight};
        const char *text = chip->label;
        char label[128];
        if (inkcell_fb_text_width_weight(state, chip->label, small, weight) > end - text_x) {
            scaffold_copy_cells(label, sizeof label, chip->label);
            (void)inkcell_fb_text_fit(state, label, sizeof label, end - text_x, &style);
            text = label;
        }
        inkcell_fb_draw_text_weight(
            state, text_x, pill.y + (pill.h - text_h) / 2, text, small, weight,
            active ? ink : inkcell_fb_tone_color(state, INKCELL_TONE_NORMAL), ground);
    }
}

/* One destination as a cell of a collapsed rail: the bar's pill and icon, in the icon column,
   with no word under it. */
static void scaffold_draw_rail_icon(const struct inkcell_draw_state *state,
                                    const struct inkcell_fb_rect *row,
                                    const struct inkcell_fb_chip *chip, bool active, int small) {
    const struct inkcell_fb_rect size = scaffold_rail_indicator_size(state, small);
    const struct inkcell_fb_rect indicator = {
        .x = row->x + inkcell_fb_gutter(state),
        .y = row->y + (row->h - size.h) / 2,
        .w = size.w,
        .h = size.h,
    };
    inkcell_fb_focus_register_shaped(state, chip->focus_id, row, INKCELL_SHAPE_MD);
    const struct inkcell_fb_button button = {
        .rect = indicator,
        .icon = chip->icon,
        .variant = active ? INKCELL_FB_BUTTON_TONAL : INKCELL_FB_BUTTON_TEXT,
        .shape = INKCELL_SHAPE_FULL,
        .idle_tone = INKCELL_TONE_DIM,
        .ground = INKCELL_COLOR_SURFACE_LOW,
        .scale = scaffold_rail_icon_scale(state),
        .hover_id = chip->focus_id,
    };
    inkcell_fb_draw_button(state, &button);
    if (scaffold_has_badge(chip)) {
        scaffold_draw_rail_badge(state, &indicator, chip->badge);
    }
}

/*
 * The press that folds and unfolds the rail, at its head and in the icon column: the collapsed
 * pill's size, so it reads as one of the rail's own marks, and quiet until the pointer is on it,
 * because it is furniture rather than a destination.
 *
 * Registered under `base + INKCELL_FB_RAIL_TOGGLE_*`, the offset saying what a press will do, so
 * the application answering the click needs nothing from the frame it was drawn in.
 */
static void scaffold_draw_rail_toggle(const struct inkcell_draw_state *state, int x, int y,
                                      uint32_t base, bool expanded, int small) {
    const struct inkcell_fb_rect size = scaffold_rail_indicator_size(state, small);
    const uint32_t id = base + (uint32_t)(expanded ? INKCELL_FB_RAIL_TOGGLE_COLLAPSE
                                                   : INKCELL_FB_RAIL_TOGGLE_EXPAND);
    const struct inkcell_fb_button button = {
        .rect = {.x = x + inkcell_fb_gutter(state), .y = y, .w = size.w, .h = size.h},
        .icon = expanded ? INKCELL_ICON_RAIL_COLLAPSE : INKCELL_ICON_RAIL_EXPAND,
        .variant = INKCELL_FB_BUTTON_TEXT,
        .shape = INKCELL_SHAPE_FULL,
        .idle_tone = INKCELL_TONE_DIM,
        .ground = INKCELL_COLOR_SURFACE_LOW,
        .scale = scaffold_rail_icon_scale(state),
        .focus_id = id,
    };
    inkcell_fb_draw_button(state, &button);
}

static void scaffold_draw_rail(const struct inkcell_draw_state *state, struct inkcell_box box,
                               const struct inkcell_fb_scaffold *scaffold, bool expanded,
                               int small) {
    /*
     * A collapsed rail is narrower than the window's buttons (top_leading_inset), which sit across
     * its edge and on into the body. Under them is the body's ground, not the rail's, and the rail
     * starts below the band they stand in - so the band reads as one toolbar across the window and
     * no rule runs through the buttons, as a Mac window's toolbar reads with its sidebar hidden.
     */
    const int rule = inkcell_fb_rule_height(state, small);
    int top = box.y;
    if (!expanded && state->top_leading_inset > box.w) {
        const int band = inkcell_fb_top_band(state, small);
        inkcell_fb_fill_rect(state, box.x, box.y, box.w, band,
                             inkcell_fb_color(state, INKCELL_COLOR_BG));
        top += band;
    }
    inkcell_fb_fill_rect(state, box.x, top, box.w, box.y + box.h - top,
                         inkcell_fb_color(state, INKCELL_COLOR_SURFACE_LOW));
    /* The strip's rule, turned on its side: the edge where the chrome stops and the body begins. */
    inkcell_fb_fill_rect(state, box.x + box.w - rule, top, rule, box.y + box.h - top,
                         inkcell_fb_color(state, INKCELL_COLOR_RULE_STRONG));

    /*
     * Down from the top, below whatever the host has put in the corner. A window whose close
     * box sits on the frame (top_leading_inset, the Mac's unified title bar) puts it in the
     * rail's top-leading corner; the tab strip is shifted along to clear it, and a column of
     * cells is shifted down instead - by the strip's height, which is the band the backend
     * placed the buttons in.
     */
    int y = box.y + inkcell_fb_space_at(state, INKCELL_SPACE_LG, small);
    if (state->top_leading_inset > 0) {
        y += inkcell_fb_top_band(state, small);
    }
    const int row_h = scaffold_rail_row_height(state, small);
    const int gap = inkcell_fb_space_at(state, INKCELL_SPACE_XS, small);
    if (scaffold->rail_toggle_id != INKCELL_FOCUS_NONE) {
        /* Its own row, apart from the destinations by a row's air: it changes the rail, and a
           press on it goes nowhere. */
        const int pill_h = scaffold_rail_indicator_size(state, small).h;
        scaffold_draw_rail_toggle(state, box.x, y + (row_h - pill_h) / 2, scaffold->rail_toggle_id,
                                  expanded, small);
        y += row_h + inkcell_fb_space_at(state, INKCELL_SPACE_MD, small);
    }
    for (size_t i = 0U; i < scaffold->count; ++i) {
        const struct inkcell_fb_rect row = {.x = box.x, .y = y, .w = box.w - rule, .h = row_h};
        /* A row that would run off the bottom is not drawn, and so not registered - the rule
           the strip keeps for a chip past its room. Six destinations fit a handheld panel
           twice over; this is for a window dragged very short. */
        if (row.y + row.h > box.y + box.h) {
            break;
        }
        const bool active = i == scaffold->active;
        if (expanded) {
            scaffold_draw_rail_row(state, &row, &scaffold->destinations[i], active, small);
        } else {
            scaffold_draw_rail_icon(state, &row, &scaffold->destinations[i], active, small);
        }
        y += row_h + gap;
    }
}

/* Whether this frame's rail is the expanded one: what the application asked for, or on AUTO the
   class's answer - words where there is room to spare, icons where the body wants it - and in
   every case only when the words fit the cap. */
static bool scaffold_rail_expanded(const struct inkcell_draw_state *state,
                                   const struct inkcell_fb_scaffold *scaffold,
                                   enum inkcell_width_class width) {
    bool want = false;
    switch (scaffold->rail) {
    case INKCELL_FB_RAIL_EXPANDED:
        want = true;
        break;
    case INKCELL_FB_RAIL_COLLAPSED:
        want = false;
        break;
    case INKCELL_FB_RAIL_AUTO:
    default:
        want = width == INKCELL_WIDTH_EXPANDED;
        break;
    }
    return want && inkcell_fb_scaffold_rail_expanded_width(state, scaffold->destinations,
                                                           scaffold->count) > 0;
}

/* ---- the frame ------------------------------------------------------------------------------ */

static enum inkcell_fb_nav_placement scaffold_placement(const struct inkcell_fb_scaffold *scaffold,
                                                        enum inkcell_width_class width) {
    if (scaffold->destinations == NULL || scaffold->count == 0U) {
        return INKCELL_FB_NAV_NONE;
    }
    if (width != INKCELL_WIDTH_COMPACT) {
        return INKCELL_FB_NAV_RAIL;
    }
    return scaffold->compact_nav == INKCELL_FB_COMPACT_NAV_TOP ? INKCELL_FB_NAV_TOP
                                                               : INKCELL_FB_NAV_BOTTOM;
}

/* The bar the foot of the frame keeps room for: `footer_kind` when it names one, `footer`'s full
   bar or none otherwise. */
static enum inkcell_fb_footer scaffold_footer(const struct inkcell_fb_scaffold *scaffold) {
    if (scaffold->footer_kind != INKCELL_FB_FOOTER_NONE) {
        return scaffold->footer_kind;
    }
    return scaffold->footer ? INKCELL_FB_FOOTER_FULL : INKCELL_FB_FOOTER_NONE;
}

/* How wide the rail is on this frame, and whether it came out expanded. */
static int scaffold_rail_w(const struct inkcell_draw_state *state,
                           const struct inkcell_fb_scaffold *scaffold,
                           enum inkcell_width_class width, bool *expanded) {
    *expanded = scaffold_rail_expanded(state, scaffold, width);
    return *expanded
               ? inkcell_fb_scaffold_rail_expanded_width(state, scaffold->destinations,
                                                         scaffold->count)
               : inkcell_fb_scaffold_rail_width(state, scaffold->destinations, scaffold->count);
}

/*
 * The two panes `content` would split into, and whether the detail one holds a whole measure.
 *
 * Two to three rather than half and half, because the two panes are not the same kind of thing:
 * the list is a column of short rows that is read by its leading edge, and the detail is where
 * running text is. So the detail gets the larger share and the list gets enough - which is where
 * Material's list-detail layout puts the line too, as a fixed list width beside a detail that
 * takes the rest. A fixed width in pixels is the one thing this cannot have, since the panes are
 * measured in columns of whatever scale the reader chose; so the ratio is the start, and the list
 * stops at a list column's width in those columns (INKCELL_WIDTH_LIST_PANE_COLS) and gives the
 * rest to the detail.
 *
 * The line is the detail's rather than the list's because the two panes are not read the same
 * way. The detail is running text and is held to a measure, since a pane narrower than that
 * cannot be read; the list is short rows read by their leading edge, and reads the same at two
 * fifths of the width. Asking for two whole measures - the expanded class - kept a 1920 window at
 * the handheld's scale to one centred ribbon with most of the window empty either side of it.
 *
 * Leaves the region as it found it.
 */
static bool scaffold_panes(struct inkcell_draw_state *state, struct inkcell_box content, int rule,
                           struct inkcell_box panes[2]) {
    struct inkcell_stack row;
    inkcell_stack_begin(&row, content, INKCELL_AXIS_X, rule);
    (void)inkcell_stack_add_grow(&row, 0, 2U);
    (void)inkcell_stack_add_grow(&row, 0, 3U);
    (void)inkcell_stack_resolve(&row, panes, 2U);
    /* The list no wider than a list column (INKCELL_WIDTH_LIST_PANE_COLS); the detail takes
       the rest. */
    const int list_cap = inkcell_fb_measure_width(state, INKCELL_WIDTH_LIST_PANE_COLS) +
                         2 * inkcell_fb_margin(state);
    if (panes[0].w > list_cap) {
        const int give = panes[0].w - list_cap;
        panes[0].w -= give;
        panes[1].x -= give;
        panes[1].w += give;
    }
    const struct inkcell_box saved = inkcell_fb_set_region(state, panes[1]);
    const bool holds = inkcell_fb_cols(state, state->scale) >= INKCELL_WIDTH_MEASURE_COLS;
    (void)inkcell_fb_set_region(state, saved);
    return holds;
}

/* The layout a pane gets: the frame's own, with its top at the panes and its rows recounted. */
static struct inkcell_fb_layout scaffold_pane_layout(const struct inkcell_draw_state *state,
                                                     const struct inkcell_fb_scaffold_frame *frame,
                                                     enum inkcell_fb_footer footer, bool back) {
    struct inkcell_fb_layout layout = inkcell_fb_layout_begin_footer(state, footer, back);
    layout.nav_y = frame->layout.nav_y;
    layout.body_y = frame->panes_y;
    if (frame->layout.heading_from == frame->panes_y) {
        layout.heading_from = frame->layout.heading_from;
        layout.heading_to = frame->layout.heading_to;
    }
    layout.rows = inkcell_fb_layout_rows(state, &layout);
    return layout;
}

void inkcell_fb_scaffold_begin(struct inkcell_draw_state *state,
                               const struct inkcell_fb_scaffold *scaffold,
                               struct inkcell_fb_scaffold_frame *frame) {
    if (state == NULL || scaffold == NULL || frame == NULL) {
        return;
    }
    *frame = (struct inkcell_fb_scaffold_frame){0};
    frame->saved_region = state->region;
    /* Every frame states its measure before anything is placed against the column, the app bar
       and the layout's widths included: a screen that turned the measure off answers for its own
       frame and not for whichever screen is drawn next. */
    (void)inkcell_fb_set_measured(state, !scaffold->unmeasured);
    state->measure_cols = 0U;

    const struct inkcell_box whole = inkcell_fb_region(state);
    const int small = inkcell_fb_type_scale(state, INKCELL_TYPE_LABEL);
    frame->width = inkcell_fb_width_class(state);
    frame->nav = scaffold_placement(scaffold, frame->width);
    frame->content = whole;

    switch (frame->nav) {
    case INKCELL_FB_NAV_BOTTOM: {
        const int bar_h = inkcell_fb_scaffold_bar_height(state);
        frame->nav_box = (struct inkcell_box){whole.x, whole.y + whole.h - bar_h, whole.w, bar_h};
        frame->content.h -= bar_h;
        scaffold_draw_bar(state, frame->nav_box, scaffold, small);
        break;
    }
    case INKCELL_FB_NAV_RAIL: {
        const int rail_w = scaffold_rail_w(state, scaffold, frame->width, &frame->rail_expanded);
        frame->nav_box = (struct inkcell_box){whole.x, whole.y, rail_w, whole.h};
        frame->content.x += rail_w;
        frame->content.w -= rail_w;
        scaffold_draw_rail(state, frame->nav_box, scaffold, frame->rail_expanded, small);
        break;
    }
    case INKCELL_FB_NAV_TOP:
    case INKCELL_FB_NAV_NONE:
    default:
        break;
    }

    /*
     * The content from here on. Everything below - the layout, the hairline, the banner - is
     * measured against this box, which is how the rail and the bottom bar take their room without
     * any of the calls after them hearing about it.
     */
    (void)inkcell_fb_set_region(state, frame->content);
    frame->layout =
        inkcell_fb_layout_begin_footer(state, scaffold_footer(scaffold), scaffold->back);
    if (frame->nav == INKCELL_FB_NAV_TOP) {
        inkcell_fb_draw_nav_bar(state, &frame->layout, scaffold->destinations, scaffold->count,
                                scaffold->active);
        frame->nav_box =
            (struct inkcell_box){whole.x, whole.y, whole.w, frame->layout.nav_y - whole.y};
    } else if (frame->nav != INKCELL_FB_NAV_NONE) {
        /* No strip over the body, so the hairline hangs from the top of the content and the body
           leaves it the air the strip would have: the same gap, so a screen's first row is the
           same distance under the frame's top edge in every arrangement. */
        frame->layout.nav_y = frame->content.y + inkcell_fb_edge(state);
        frame->layout.body_y = frame->content.y +
                               inkcell_fb_space_at(state, INKCELL_SPACE_MD, small) +
                               inkcell_fb_gutter(state);
        /* A collapsed rail is narrower than the window's buttons, which then stand over the
           body's corner: the body starts below their band, and only a heading rises into it -
           see `heading_from`. */
        const int band = inkcell_fb_top_band(state, small);
        if (!frame->rail_expanded && state->top_leading_inset > frame->content.x &&
            frame->layout.body_y < band) {
            frame->layout.heading_from = band;
            frame->layout.heading_to = frame->layout.body_y;
            frame->layout.body_y = band;
        }
        frame->layout.rows = inkcell_fb_layout_rows(state, &frame->layout);
    }
    inkcell_fb_draw_progress(state, &frame->layout, scaffold->busy);
    if (scaffold->banner != NULL) {
        inkcell_fb_draw_banner(state, &frame->layout, scaffold->banner);
    }
    frame->panes_y = frame->layout.body_y;

    /* The split, when the class and the screen both want one - see scaffold_panes(). */
    const int rule = inkcell_fb_rule_height(state, small);
    struct inkcell_box panes[2] = {{0}};
    frame->split = scaffold->split && frame->width != INKCELL_WIDTH_COMPACT &&
                   scaffold_panes(state, frame->content, rule, panes);
    if (frame->split) {
        frame->list = panes[0];
        frame->detail = panes[1];

        (void)inkcell_fb_set_region(state, frame->list);
        frame->layout =
            scaffold_pane_layout(state, frame, scaffold_footer(scaffold), scaffold->back);
        /* The rule between them, down the rows the panes share. */
        inkcell_fb_fill_rect(state, frame->list.x + frame->list.w, frame->panes_y, rule,
                             frame->layout.footer_y - frame->panes_y,
                             inkcell_fb_color(state, INKCELL_COLOR_RULE));
    } else {
        frame->list = frame->content;
    }
    /* The boxes a caller reads are the rows the panes actually have, not the regions they are
       measured in: the region runs to the bottom so that the footer is reserved in both panes
       alike, and the part of it under the footer is the action bar's. */
    frame->list.y = frame->panes_y;
    frame->list.h = frame->layout.footer_y - frame->panes_y;
    if (frame->split) {
        frame->detail.y = frame->panes_y;
        frame->detail.h = frame->list.h;
    }

    if (scaffold->app_bar != NULL) {
        inkcell_fb_draw_app_bar(state, &frame->layout, scaffold->app_bar);
    }
}

struct inkcell_fb_layout inkcell_fb_scaffold_detail(struct inkcell_draw_state *state,
                                                    const struct inkcell_fb_scaffold_frame *frame,
                                                    const struct inkcell_fb_app_bar *bar) {
    if (state == NULL || frame == NULL || !frame->split) {
        return (struct inkcell_fb_layout){0};
    }
    (void)inkcell_fb_set_region(state, (struct inkcell_box){frame->detail.x, frame->content.y,
                                                            frame->detail.w, frame->content.h});
    state->measure_cols = INKCELL_WIDTH_DETAIL_PANE_COLS;
    /* `back` is false whatever the list pane says: with the list standing beside it, the detail
       is not somewhere B leaves - the thing it would go back to is already on the panel. */
    struct inkcell_fb_layout layout =
        scaffold_pane_layout(state, frame, frame->layout.footer, false);
    if (bar != NULL) {
        inkcell_fb_draw_app_bar(state, &layout, bar);
    }
    return layout;
}

int inkcell_fb_scaffold_transition(struct inkcell_draw_state *state,
                                   const struct inkcell_fb_scaffold_frame *frame) {
    if (state == NULL || frame == NULL) {
        return 0;
    }
    const struct inkcell_box pane = inkcell_fb_set_region(state, frame->content);
    const int slide = inkcell_fb_transition_offset(state);
    if (slide != 0) {
        const int top = frame->layout.nav_y;
        const int bottom = frame->layout.footer_y;
        inkcell_fb_animation_damage(state, frame->content.x, top, frame->content.w, bottom - top);
        inkcell_fb_shift_begin(state, slide, top, bottom);
    }
    (void)inkcell_fb_set_region(state, pane);
    return slide;
}

void inkcell_fb_scaffold_end(struct inkcell_draw_state *state,
                             const struct inkcell_fb_scaffold_frame *frame,
                             const struct inkcell_fb_action_bar *actions) {
    if (state == NULL || frame == NULL) {
        return;
    }
    (void)inkcell_fb_set_region(state, frame->content);
    state->measure_cols = 0U;
    /*
     * Under a split the bar is not held to the measure. The measure is for one column of text,
     * and a split frame has two panes that between them already run edge to edge: a bar capped
     * and centred under them stood its keycaps somewhere in the list pane's second half and its
     * status in the middle of the detail's, under the edge of neither. Spanning the content, its
     * keycaps lead under the list and its status trails under the detail.
     */
    const bool measured = !state->unmeasured;
    if (frame->split) {
        (void)inkcell_fb_set_measured(state, false);
    }
    if (actions != NULL) {
        inkcell_fb_draw_action_bar(state, &frame->layout, actions);
    }
    (void)inkcell_fb_set_measured(state, measured);
    (void)inkcell_fb_set_region(state, frame->saved_region);
}

bool inkcell_fb_scaffold_splittable(struct inkcell_draw_state *state,
                                    const struct inkcell_fb_scaffold *scaffold) {
    if (state == NULL || scaffold == NULL) {
        return false;
    }
    /* What begin would measure against, and put back after: the measure the frame will be drawn
       with, and the region less the destinations. */
    const bool measured = inkcell_fb_set_measured(state, !scaffold->unmeasured);
    const enum inkcell_width_class width = inkcell_fb_width_class(state);
    bool holds = false;
    if (width != INKCELL_WIDTH_COMPACT) {
        struct inkcell_box content = inkcell_fb_region(state);
        if (scaffold_placement(scaffold, width) == INKCELL_FB_NAV_RAIL) {
            bool expanded = false;
            const int rail_w = scaffold_rail_w(state, scaffold, width, &expanded);
            content.x += rail_w;
            content.w -= rail_w;
        }
        const struct inkcell_box saved = inkcell_fb_set_region(state, content);
        struct inkcell_box panes[2] = {{0}};
        holds = scaffold_panes(
            state, content,
            inkcell_fb_rule_height(state, inkcell_fb_type_scale(state, INKCELL_TYPE_LABEL)), panes);
        (void)inkcell_fb_set_region(state, saved);
    }
    (void)inkcell_fb_set_measured(state, measured);
    return holds;
}
