#define _POSIX_C_SOURCE 200809L

/*
 * The scaffold: which of the frame's arrangements this width class gets, and the two ways of
 * drawing destinations that the tab strip does not cover - a bar across the bottom and a rail
 * down the side.
 *
 * Both are the same cell - an indicator pill with the icon in it, the label under it and the
 * badge on the pill's shoulder - laid out along a different axis. One cell drawn two ways is the
 * point: a destination that looked different in a rail from the way it looked in a bar would be
 * two components that happen to share a name, and the day one of them grew a state the other
 * would not have it.
 */

#include "inkcell/ui/widgets/scaffold.h"

/* ---- the destination cell ------------------------------------------------------------------- */

/*
 * The widest the rail may be, in columns of body text.
 *
 * A rail is chrome, and chrome that took a fifth of a medium window to spell out one long
 * translation would be chrome taking the room the class change was meant to give the body. Ten is
 * a comfortable word at the label scale; past it the labels go and the rail keeps its icons,
 * which is the elision the tab strip already does when it runs out of room.
 */
#define INKCELL_FB_RAIL_MAX_COLS 10U

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
static struct inkcell_fb_rect scaffold_indicator_size(const struct inkcell_draw_state *state,
                                                      int small) {
    const int icon = scaffold_icon_scale(state);
    return (struct inkcell_fb_rect){
        .w = inkcell_fb_icon_box(state, icon) +
             2 * inkcell_fb_space_at(state, INKCELL_SPACE_LG, icon),
        .h = inkcell_fb_line_adv(state, icon) + inkcell_fb_space_at(state, INKCELL_SPACE_SM, small),
    };
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

/* The rail's labels are all or nothing: a rail that showed one word among bare icons would change
   width, or leave a gap, every time the active destination did. */
static bool scaffold_rail_labels(const struct inkcell_draw_state *state,
                                 const struct inkcell_fb_chip *chips, size_t count, int small,
                                 int *widest) {
    const enum inkcell_weight bold = inkcell_fb_type_weight(state, INKCELL_TYPE_LABEL);
    int most = 0;
    for (size_t i = 0U; i < count; ++i) {
        /* Measured bold whichever is active, so the rail is as wide as it will ever need to be
           and never changes width as the reader moves down it. */
        const int width = chips[i].label != NULL
                              ? inkcell_fb_text_width_weight(state, chips[i].label, small, bold)
                              : 0;
        most = width > most ? width : most;
    }
    *widest = most;
    const int cap = inkcell_fb_measure_width(state, INKCELL_FB_RAIL_MAX_COLS);
    return most + 2 * inkcell_fb_gutter(state) <= cap;
}

int inkcell_fb_scaffold_rail_width(const struct inkcell_draw_state *state,
                                   const struct inkcell_fb_chip *destinations, size_t count) {
    if (state == NULL || destinations == NULL || count == 0U) {
        return 0;
    }
    const int small = inkcell_fb_type_scale(state, INKCELL_TYPE_LABEL);
    int widest = 0;
    const bool labels = scaffold_rail_labels(state, destinations, count, small, &widest);
    const int pill = scaffold_indicator_size(state, small).w;
    const int content = labels && widest > pill ? widest : pill;
    return content + 2 * inkcell_fb_gutter(state) + inkcell_fb_rule_height(state, small);
}

static void scaffold_draw_rail(const struct inkcell_draw_state *state, struct inkcell_box box,
                               const struct inkcell_fb_scaffold *scaffold, int small) {
    inkcell_fb_fill_rect(state, box.x, box.y, box.w, box.h,
                         inkcell_fb_color(state, INKCELL_COLOR_SURFACE_LOW));
    /* The strip's rule, turned on its side: the edge where the chrome stops and the body begins. */
    const int rule = inkcell_fb_rule_height(state, small);
    inkcell_fb_fill_rect(state, box.x + box.w - rule, box.y, rule, box.h,
                         inkcell_fb_color(state, INKCELL_COLOR_RULE_STRONG));

    int widest = 0;
    const bool labels =
        scaffold_rail_labels(state, scaffold->destinations, scaffold->count, small, &widest);
    /*
     * Down from the top, below whatever the host has put in the corner. A window whose close
     * box sits on the frame (top_leading_inset, the Mac's unified title bar) puts it in the
     * rail's top-leading corner; the tab strip is shifted along to clear it, and a column of
     * cells is shifted down instead - by the strip's height, which is the band the backend
     * placed the buttons in.
     */
    int y = box.y + inkcell_fb_space_at(state, INKCELL_SPACE_LG, small);
    if (state->top_leading_inset > 0) {
        y += inkcell_fb_nav_bar_height(state, small);
    }
    const int cell_h = scaffold_cell_height(state, small);
    const int gap = inkcell_fb_space_at(state, INKCELL_SPACE_SM, small);
    for (size_t i = 0U; i < scaffold->count; ++i) {
        const struct inkcell_fb_rect cell = {.x = box.x, .y = y, .w = box.w - rule, .h = cell_h};
        /* A cell that would run off the bottom is not drawn, and so not registered - the rule
           the strip keeps for a chip past its room. Six destinations fit a handheld panel
           twice over; this is for a window dragged very short. */
        if (cell.y + cell.h > box.y + box.h) {
            break;
        }
        scaffold_draw_cell(state, &cell, &scaffold->destinations[i], i == scaffold->active, labels,
                           small);
        y += cell_h + gap;
    }
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

/* The layout a pane gets: the frame's own, with its top at the panes and its rows recounted. */
static struct inkcell_fb_layout scaffold_pane_layout(const struct inkcell_draw_state *state,
                                                     const struct inkcell_fb_scaffold_frame *frame,
                                                     enum inkcell_fb_footer footer, bool back) {
    struct inkcell_fb_layout layout = inkcell_fb_layout_begin_footer(state, footer, back);
    layout.nav_y = frame->layout.nav_y;
    layout.body_y = frame->panes_y;
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
        const int rail_w =
            inkcell_fb_scaffold_rail_width(state, scaffold->destinations, scaffold->count);
        frame->nav_box = (struct inkcell_box){whole.x, whole.y, rail_w, whole.h};
        frame->content.x += rail_w;
        frame->content.w -= rail_w;
        scaffold_draw_rail(state, frame->nav_box, scaffold, small);
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
        frame->layout.rows = inkcell_fb_layout_rows(state, &frame->layout);
    }
    inkcell_fb_draw_progress(state, &frame->layout, scaffold->busy);
    if (scaffold->banner != NULL) {
        inkcell_fb_draw_banner(state, &frame->layout, scaffold->banner);
    }
    frame->panes_y = frame->layout.body_y;

    /*
     * The split, when the class and the screen both want one.
     *
     * Two to three rather than half and half, because the two panes are not the same kind of
     * thing: the list is a column of short rows that is read by its leading edge, and the detail
     * is where running text is. So the detail gets the larger share and the list gets enough -
     * which is where Material's list-detail layout puts the line too, as a fixed list width
     * beside a detail that takes the rest. A fixed width is the one thing this cannot have, since
     * the panes are measured in columns of whatever scale the reader chose, and a ratio is a
     * fixed width that scales with them.
     */
    /*
     * And which frames get one: any wider than compact whose detail would hold a whole measure.
     *
     * The line is the detail's rather than the list's because the two panes are not read the
     * same way. The detail is running text and is held to a measure, since a pane narrower than
     * that cannot be read; the list is short rows read by their leading edge, and reads the same
     * at two fifths of the width. Asking for two whole measures - the expanded class - kept a
     * 1920 window at the handheld's scale to one centred ribbon with most of the window empty
     * either side of it.
     */
    const int rule = inkcell_fb_rule_height(state, small);
    struct inkcell_box panes[2] = {{0}};
    if (scaffold->split && frame->width != INKCELL_WIDTH_COMPACT) {
        struct inkcell_stack row;
        inkcell_stack_begin(&row, frame->content, INKCELL_AXIS_X, rule);
        (void)inkcell_stack_add_grow(&row, 0, 2U);
        (void)inkcell_stack_add_grow(&row, 0, 3U);
        (void)inkcell_stack_resolve(&row, panes, 2U);
        (void)inkcell_fb_set_region(state, panes[1]);
        frame->split = inkcell_fb_cols(state, state->scale) >= INKCELL_WIDTH_MEASURE_COLS;
        (void)inkcell_fb_set_region(state, frame->content);
    }
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
    if (actions != NULL) {
        inkcell_fb_draw_action_bar(state, &frame->layout, actions);
    }
    (void)inkcell_fb_set_region(state, frame->saved_region);
}
