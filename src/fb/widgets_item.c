#define _POSIX_C_SOURCE 200809L

/*
 * The slotted list row: where each slot lands, what ink it takes, and what gives way first when
 * the row runs out of width.
 */

#include "inkcell/ui/widgets/button.h"
#include "inkcell/ui/widgets/chrome.h"
#include "inkcell/ui/widgets/item.h"
#include "list_internal.h"

#include "inkcell/i18n/strings.h"
#include "inkcell/ui/emoji.h"

#include <stdio.h>
#include <string.h>

/* ---- the conversation cell ----------------------------------------------------------------- */

/* ---- the list item ------------------------------------------------------------------------ */

/*
 * An item's geometry, all of it derived once so the fill, the text and the slots cannot
 * disagree about where the row is. Every screen used to re-derive some part of this, and the
 * parts that drifted were exactly the ones nothing else could see: how far a trailing control
 * ate into the text, and which box a control centred itself on.
 */
struct inkcell_fb_item_geom {
    uint32_t rows;
    int fill_x, fill_w;   /* the row's own rectangle, inkcell_fb_row_box()'s and nobody else's */
    int fill_top, fill_h; /* the box the cursor fill paints, and a control centres on */
    int head_y;           /* headline baseline */
    int supp_y;           /* supporting baseline; only meaningful on a two-row item */
    int slot_h;           /* height of a box-shaped trailing - a badge */
    int head_slot_top, supp_slot_top;
    int text_x, text_right;
    int content_x;    /* where the row's content starts, before any leading slot is reserved */
    int lead_size;    /* the leading slot's side: one width per list, never per row */
    int lead_disc;    /* what may actually be drawn in it, once the cards either side have theirs */
    int lead_y;       /* and where: its own answer, because a shrunken disc is centred not seated */
    int marker_x;     /* a plain row's marker cell; only meaningful when the row reserved one */
    size_t cols;      /* text columns between the leading slot and the trailing edge */
    int bar_y, bar_h; /* a stacked meter's track; bar_h of 0 is a row that has none */
    int accessory_x;  /* the accessory column's cell; only meaningful when the row reserved one */
    /*
     * The tiers' type, on a list that sets them in roles of their own - see enum
     * inkcell_fb_list_type. `tiered` false is a list of uniform rows, where every line is at the
     * body scale and neither style is read.
     */
    bool tiered;
    struct inkcell_type_style soft; /* the supporting line: INKCELL_TYPE_BODY_SOFT */
    struct inkcell_type_style meta; /* a trailing figure: INKCELL_TYPE_CAPTION */
};

static struct inkcell_fb_item_geom inkcell_fb_item_measure(const struct inkcell_draw_state *state,
                                                           const struct inkcell_fb_list *list,
                                                           const struct inkcell_fb_list_item *item,
                                                           uint32_t index, uint32_t rows) {
    const int scale = state->scale;
    const int adv = inkcell_fb_char_adv(state, scale);
    const struct inkcell_fb_row_box box = inkcell_fb_row_box(state);
    struct inkcell_fb_item_geom g;
    memset(&g, 0, sizeof g);

    /*
     * The height is the *list's* answer, not this item's.
     *
     * It used to be read off the item - two rows if it had a supporting line, one otherwise -
     * which was a second opinion about something the window had already decided, and the two
     * were only ever equal because every list happened to be uniform. Once a list can mix
     * heights the model is the one that has to be right: it is what placed the window, what
     * the cursor's highlight is measured against and what the scroll thumb reports. So a row
     * that draws taller than the list was told simply cannot happen from here.
     */
    g.rows = rows > 0U ? rows : 1U;
    /*
     * The density's air, and where it goes.
     *
     * A step on a comfortable list is a text line with `pad` above and below it, and the list
     * has already put one pad above the first baseline. A row of one step spends exactly that:
     * its words sit in the middle of its box. A row of more steps has a pad per step above and
     * below its words, and spends all of it *outside* them - the lines of one row stay set as
     * close as they always were, and the room the extra steps brought is the margin round the
     * whole row rather than a gap opened between its headline and its second line, which would
     * read as two rows. So the headline drops by the pads the steps after the first brought,
     * and the fill grows by all of them.
     *
     * `text_line` is what a line of words advances, whatever the step is - a badge and a slot
     * are the height of a line of text, not of a step.
     */
    const int pad = list->pad;
    const int text_line = list->line - 2 * pad;
    const int spread = (int)g.rows * pad;
    g.head_y = list->y + (int)(g.rows - 1U) * pad;
    g.fill_x = box.x;
    g.fill_w = box.w;
    g.text_right = box.text_right;
    g.slot_h = text_line - inkcell_step_px(scale);

    if (g.rows >= 2U) {
        /* Two rows set closer together than two items are: the supporting line sits a scale
           above where a second row would put it, and the space that frees becomes the gap to
           the next item. */
        g.supp_y = g.head_y + text_line - inkcell_step_px(scale);
        g.head_slot_top = g.head_y - inkcell_fb_space(state, INKCELL_SPACE_XS);
        g.fill_top = g.head_slot_top - spread;
        g.fill_h = (int)g.rows * text_line - inkcell_fb_space(state, INKCELL_SPACE_MD) + 2 * spread;
        g.supp_slot_top = g.supp_y - inkcell_fb_space(state, INKCELL_SPACE_XS);
    } else {
        g.head_slot_top = g.head_y - inkcell_step_px(scale);
        g.fill_top = g.head_slot_top - pad;
        g.fill_h = list->line;
        g.supp_slot_top = g.head_slot_top;
    }

    /*
     * The leading slot's side, and the two answers are the point rather than an oversight.
     *
     * A disc is a *picture* and is sized to the row it leads, so a conversation cell's avatar
     * fills the two steps its preview line gave it. The empty slot is not a picture: its whole
     * job is to put the words of a row that carries nothing in the same column as the words of
     * the rows that do - and in every list that mixes the two, the row carrying a disc is one
     * step tall. A settings verb never takes a second step (a slider and a meter are settings,
     * and neither is a verb), and neither does a node detail's.
     *
     * So the slot is measured off `list->line`, the one step every row of the list is built
     * out of, where `g.fill_h` is how many of them this row happened to take. It used to be
     * `g.fill_h - scale` for all three, which is the same number on a one-step row and a
     * larger one on every row that took two - so a settings section that mixed a slider in
     * with its neighbours drew that row's label, its value and its track a cell to the right
     * of the rows above and below it, and the gutter promised to a row with nothing in it was
     * not the gutter the discs beside it actually stood in. Position and LoRa are where it
     * showed; the rule it breaks is the one stated on INKCELL_FB_LEADING_ICON directly below, and
     * `ui_capture_a_section_starts_every_row_in_one_column` is what holds it now.
     */
    /* Off the rows' words rather than their air: a comfortable list spaces its rows out and
       leaves its discs the size they were, which is how a denser or a looser list of the same
       things still reads as the same things. */
    g.lead_size = item->leading.kind == INKCELL_FB_LEADING_TONAL_SLOT
                      ? text_line - inkcell_step_px(scale)
                      : g.fill_h - 2 * spread - inkcell_step_px(scale);
    /*
     * A step on the panel gives back the hairline each card beside it spends into it, and what
     * may actually be *drawn* in its leading slot follows from what is left.
     *
     * A card's edge is drawn outside its own rows' boxes so that no row's highlight can paint it
     * out (inkcell_fb_list_cards()), and that "outside" is inside *this* step's box. Left there it
     * is the same bug from either side of one hairline: a highlight on this step paints over the
     * card's edge and its corners, and selecting the card's last row paints over the other one. So
     * the box stops short of both, which costs two pixels of fill that were not being used and
     * nothing else - the baseline does not move and a glyph's cell sits inside what is left.
     *
     * The slot is the gutter and the gutter may not move: it is what puts every row's words in
     * one column, which is the whole of what the slot is for. `lead_size` is therefore measured
     * before any of this, and only what is drawn in it gives way - centred in the slot, so the
     * room a card took comes off the mark and never off the column.
     *
     * A pixel of clearance at each end where a card is adjacent, because a mark laid against a
     * card's hairline reads as attached to that card rather than standing between two, which is
     * what a heading is.
     */
    if (inkcell_fb_list_has_cards(list) && !inkcell_fb_list_on_card(list, index)) {
        const int edge = inkcell_fb_edge(state);
        const bool card_above = index > 0U && inkcell_fb_list_on_card(list, index - 1U);
        const bool card_below = inkcell_fb_list_on_card(list, index + 1U);
        if (card_above) {
            g.fill_top += edge;
            g.fill_h -= edge;
        }
        if (card_below) {
            g.fill_h -= edge;
        }
        const int band_top = g.fill_top + (card_above ? 1 : 0);
        const int band = g.fill_top + g.fill_h - (card_below ? 1 : 0) - band_top;
        g.lead_disc = g.lead_size > band ? (band > 0 ? band : 0) : g.lead_size;
        /* Centred in what is left, so the room the cards took is ground at both ends of the disc
           rather than all of it at one. */
        g.lead_y = band_top + (band - g.lead_disc) / 2;
    } else {
        g.lead_disc = g.lead_size;
        /* A hairline inside the top of the row's box, which is where a glyph's own ink sits and
           so what keeps a disc reading as part of the line beside it. Below the density's air,
           which the disc stands clear of exactly as the words do. */
        g.lead_y = g.fill_top + spread + inkcell_fb_space(state, INKCELL_SPACE_XS);
    }
    g.content_x = box.text_x;
    g.text_x = g.content_x;
    if (item->leading.kind == INKCELL_FB_LEADING_AVATAR ||
        item->leading.kind == INKCELL_FB_LEADING_TONAL ||
        item->leading.kind == INKCELL_FB_LEADING_TONAL_SLOT) {
        /* One measurement for all three. A tonal container is an avatar that happens to be
           filled from a family rather than from a hash, and a gutter that differed between them
           would be a list unable to mix the two - which the node detail does a card at a time and
           a settings section does within one card, where a verb stands among the fields it
           applies. The empty slot measures with them for the same reason it exists: it is this
           gutter, promised to a row that has nothing to put in it. */
        g.text_x = g.content_x + g.lead_size + adv / 2;
    } else if (item->leading.kind == INKCELL_FB_LEADING_ICON) {
        /* Reserved whether or not this row filled it, so every row's words start in the same
           column - a list that indents only the rows with something to say is a list the eye
           cannot run down. */
        g.text_x = g.content_x + inkcell_fb_icon_box(state, scale) + adv / 2;
    }
    /* The plain row's marker gutter, between whatever the leading slot put down and the words.
       Reserved for the whole list rather than for the rows that filled it, on the same terms as
       the leading slot above - and only where there is no label column, which measures its own. */
    g.marker_x = g.text_x;
    if (item->label_cols == 0U && item->marker_slot) {
        g.text_x += inkcell_fb_icon_box(state, scale) + adv / 2;
    }
    /*
     * The accessory column, taken off the trailing edge before anything else is measured against
     * it - so the trailing slot, the value column and the words all stand to the left of it, and
     * a list that reserved it on every row has every one of those in one column whatever each
     * row's accessory is. A cell of air between it and whatever it follows, which is the gap a
     * trailing slot keeps from the words.
     */
    if (item->accessory != INKCELL_FB_ACCESSORY_NONE || item->accessory_slot) {
        const int cell = inkcell_fb_icon_box(state, scale);
        g.accessory_x = g.text_right - cell;
        g.text_right = g.accessory_x - adv / 2;
    }
    g.cols = g.text_right > g.text_x ? (size_t)((g.text_right - g.text_x) / adv) : 1U;

    g.tiered = list->style.type == INKCELL_FB_LIST_TYPE_TIERED;
    if (g.tiered) {
        g.soft = inkcell_fb_type_style(state, INKCELL_TYPE_BODY_SOFT);
        g.meta = inkcell_fb_type_style(state, INKCELL_TYPE_CAPTION);
    }

    /*
     * A stacked meter's track, and the one thing on a two-step item that the fill has to be
     * grown for.
     *
     * The supporting line's box overhangs the fill by design - a glyph's ink sits high in its
     * cell, so text stays comfortably inside a fill that stops short of the box, and the space
     * that leaves is the gap between one item and the next. A bar has no such slack: its ink is
     * the whole of its box, so a fill measured for text left the cursor's highlight ending a few
     * pixels above the bar it was meant to be under. The fill therefore takes the bar in, plus
     * the same breathing room it has at the top.
     */
    if ((item->meter != NULL || item->slider != NULL) && g.rows >= 2U) {
        /* One answer for both of the things that can be in that step, because the step is one
           step: a screen that swapped a reading for a control on the same row must not find the
           row a few pixels shorter. The slider is the taller of the two - it reserves the room
           its handle stands up into under the cursor - so it is what the step is measured by
           wherever it is the one present. */
        g.bar_h = item->slider != NULL ? inkcell_fb_slider_height(state, scale)
                                       : inkcell_fb_meter_thickness(state, scale);
        /* On the supporting line's geometry: a second line set closer to its headline than two
           rows would be, which is what keeps the bar reading as part of the row above it rather
           than as something floating between two rows. */
        g.bar_y =
            g.supp_y + (inkcell_scale_px((int)inkcell_fb_font(state)->height, scale) - g.bar_h) / 2;
        const int bottom = g.bar_y + g.bar_h + inkcell_fb_space(state, INKCELL_SPACE_XS);
        if (bottom - g.fill_top > g.fill_h) {
            g.fill_h = bottom - g.fill_top;
        }
    }
    return g;
}

/*
 * How wide an inline meter is, in cells.
 *
 * A length only means something against the container it is in, so the container has to be long
 * enough for the eye to divide: at four cells a half-full bar and a two-thirds-full one are the
 * same picture. Eight is about where they part company at this glyph scale, and it is still
 * short enough to leave a settings row its label and its value - which is the whole reason the
 * inline meter is a trailing slot and not a band across the row.
 */
#define INKCELL_FB_METER_INLINE_CELLS 8U

/*
 * Cells a trailing slot takes out of a line `cols` wide, its breathing room included.
 *
 * Zero has one meaning and it covers both ways a slot can come to nothing: there is nothing in
 * it, or the line is too narrow to give it its room. Either way the slot is not drawn and the
 * line keeps every column it has - because a trailing figure is worth less than the row it
 * would be laid across, and a row clipped to one cell has lost the thing it was about.
 *
 * The point of one function answering that is that *measuring and drawing ask it once*. A slot
 * squeezed out of the line by one calculation and then painted over that line by another is
 * exactly the bug this component exists to make unwritable - it is what a narrow panel or a
 * large glyph scale used to turn an age into, drawn back across the avatar.
 *
 * `reserved` is what the row has already promised to something else - the label column and its
 * marker gutter, or a plain row's marker cell. A slot is fitted against what is *free*, not
 * against the whole line: the headline is clipped from its tail, so a slot measured against the
 * line ate the value first and then the label, and a label column is the one thing on a settings
 * row that may not move. The wide slots are why this arrived - a switch is four cells and never
 * reached it, a segmented button is most of a value column and reaches it at every scale.
 */
/*
 * What the segmented slot takes out of a line `cols` wide, and which of its two forms it takes.
 *
 * The one slot with a second form, so it is the one slot that needs a function of its own: every
 * other kind either fits or is dropped, and a set of choices always has the chosen one in words
 * to fall back on. `out_as_text` comes back true when the control could not have its room -
 * which is a value column too narrow for the segments, not a caller that forgot to fill one in.
 *
 * Measuring and drawing both ask *this*, for the reason they both ask inkcell_fb_trailing_cols(): a
 * slot sized by one rule and painted by another is what turned a trailing age into a smear across
 * an avatar, and a control that decided its own form twice would do it again.
 */
/*
 * The glyph multiplier a row's segments are set at: the label role, not the row's own.
 *
 * Which is what Material asks for and, more to the point here, what makes the component reach
 * the settings it exists for. A segmented button spends its width `count` times over, so a
 * three-valued setting at body scale wants most of a panel - "Random PIN / Fixed PIN / No PIN"
 * does not fit a value column at any scale this ships with, and would have fallen back to the
 * word it was meant to replace on every theme. It is also the right answer on its own terms: a
 * control's label is chrome, which is the type role the action bar's verbs and the navigation
 * bar's tabs already take.
 */
static int inkcell_fb_segmented_scale(const struct inkcell_draw_state *state) {
    return inkcell_fb_type_scale(state, INKCELL_TYPE_LABEL);
}

/* The chosen word instead of the control, on the terms an ordinary trailing text takes. Both
   ways of ending up there - no room, and no drawable choice - go through this, so a slot that
   fell back for one reason is measured exactly like a slot that fell back for the other. */
static size_t inkcell_fb_segmented_as_text(const struct inkcell_draw_state *state, size_t cols,
                                           const struct inkcell_fb_segmented *segmented,
                                           bool *out_as_text) {
    if (out_as_text != NULL) {
        *out_as_text = true;
    }
    if (segmented == NULL) {
        return 0U;
    }
    const size_t cells = inkcell_fb_text_cols(state, segmented->value, state->scale);
    return (cells > 0U && cols > cells + 1U) ? cells + 1U : 0U;
}

static size_t inkcell_fb_segmented_cols(const struct inkcell_draw_state *state, size_t cols,
                                        const struct inkcell_fb_segmented *segmented,
                                        bool *out_as_text) {
    if (out_as_text != NULL) {
        *out_as_text = true;
    }
    if (segmented == NULL || segmented->count == 0U ||
        segmented->count > INKCELL_FB_SEGMENTED_MAX || segmented->active >= segmented->count) {
        /*
         * A choice outside the set is a choice this control cannot draw, and it is a state the
         * radio can genuinely be in: an enum value from a newer firmware, or a corrupt one. The
         * item still carries it and still formats it - "Unknown" - so the words are the honest
         * answer and they are already here. Highlighting the first segment instead would have
         * the panel state that pairing is set to Random PIN when nobody knows what it is set to,
         * which is the one thing a picture is not allowed to do quietly.
         *
         * Not "every segment unlit" either: a set with nothing chosen says *none of these*,
         * which is a different false claim.
         */
        return inkcell_fb_segmented_as_text(state, cols, segmented, out_as_text);
    }
    const int adv = inkcell_fb_char_adv(state, state->scale);
    const int width =
        inkcell_fb_segmented_width(state, segmented, inkcell_fb_segmented_scale(state));
    /*
     * Counted in the *row's* cells whatever the segments are set at, because what it is being
     * fitted into is a line of the row's own text.
     *
     * And with no cell of air added, unlike every other slot here. The others sit next to the
     * row's value and the extra cell is the gap to it; this one *is* the value - the words it
     * replaces are inside it - so the only thing left of the line is the label column, which the
     * caller has already reserved. The gap is then exactly the cell the strictly-greater test
     * below keeps back, and adding a second one cost the three-valued settings the control at
     * the shipping scale by a single column.
     */
    const size_t want = (size_t)((width + adv - 1) / adv);
    if (width > 0 && cols > want) {
        if (out_as_text != NULL) {
            *out_as_text = false;
        }
        return want;
    }
    return inkcell_fb_segmented_as_text(state, cols, segmented, out_as_text);
}

/*
 * Whole body cells a figure takes: its own measure at the body scale on a uniform row, and on a
 * tiered one its measure in the caption style, rounded up to the cells the line is counted in -
 * so a smaller figure takes fewer of the line's cells and the words keep what it gave back.
 */
static size_t inkcell_fb_figure_cols(const struct inkcell_draw_state *state, const char *text,
                                     const struct inkcell_type_style *meta) {
    if (meta == NULL) {
        return inkcell_fb_text_cols(state, text, state->scale);
    }
    const int adv = inkcell_fb_char_adv(state, state->scale);
    const int width = inkcell_fb_text_width_styled(state, text, meta);
    return (width > 0 && adv > 0) ? (size_t)((width + adv - 1) / adv) : 0U;
}

/*
 * The glyph multiplier a trailing status capsule is set at: the row's own on a uniform list, and
 * the label role on a tiered one - where the capsule is a tier below the headline like every
 * other thing at the trailing edge, and at the body scale it stood as tall as the row's fill and
 * pressed against the cursor's ring on the one row being pointed at.
 */
static int inkcell_fb_status_scale(const struct inkcell_draw_state *state,
                                   const struct inkcell_type_style *meta) {
    return meta != NULL ? inkcell_fb_type_scale(state, INKCELL_TYPE_LABEL) : state->scale;
}

/* `meta` is the tiered list's caption style, NULL on a uniform one: the one slot kind a style
   changes the width of is the figure, and the figure beside a signal. */
static size_t inkcell_fb_trailing_cols(const struct inkcell_draw_state *state, size_t cols,
                                       size_t reserved, const struct inkcell_fb_trailing *trailing,
                                       const struct inkcell_type_style *meta) {
    const int adv = inkcell_fb_char_adv(state, state->scale);
    size_t want = 0U;
    switch (trailing->kind) {
    case INKCELL_FB_TRAILING_TEXT: {
        const size_t cells = inkcell_fb_figure_cols(state, trailing->text, meta);
        want = cells > 0U ? cells + 1U : 0U;
        break;
    }
    case INKCELL_FB_TRAILING_STATUS: {
        /* The capsule's own measure, padding included, and a cell of air before it - the badge's
           arithmetic, stated in pixels because the capsule is. */
        const int width =
            inkcell_fb_badge_width(state, trailing->text, inkcell_fb_status_scale(state, meta));
        want = (width > 0 && adv > 0) ? (size_t)((width + adv - 1) / adv) + 1U : 0U;
        break;
    }
    case INKCELL_FB_TRAILING_BADGE: {
        const size_t cells = inkcell_fb_text_cols(state, trailing->text, state->scale);
        want = cells > 0U ? cells + 2U : 0U;
        break;
    }
    case INKCELL_FB_TRAILING_SWITCH: {
        int width = 0;
        inkcell_fb_switch_size(state, state->scale, &width, NULL);
        want = (size_t)((width + adv - 1) / adv) + 2U;
        break;
    }
    case INKCELL_FB_TRAILING_ICON:
        want = inkcell_icon_is_valid(trailing->icon) ? 2U : 0U;
        break;
    case INKCELL_FB_TRAILING_METER:
        /* Stated in cells rather than measured from anything, because unlike a switch a bar has
           no natural width - it is as long as it is given. INKCELL_FB_METER_INLINE_CELLS is that
           choice, and the extra cell is the gap to the words. */
        want = trailing->meter != NULL ? INKCELL_FB_METER_INLINE_CELLS + 1U : 0U;
        break;
    case INKCELL_FB_TRAILING_SIGNAL: {
        /* The staircase, its gap to whatever is left of it, and the figure it carries - which
           may be nothing, and then costs nothing. Stated the same way the meter's width is, and
           for the same reason: rungs have no natural width either. */
        const size_t cells = inkcell_fb_figure_cols(state, trailing->text, meta);
        want = INKCELL_FB_SIGNAL_CELLS + 1U + (cells > 0U ? cells + 1U : 0U);
        break;
    }
    case INKCELL_FB_TRAILING_SPARK:
        /* The line and the cell of air between it and the words, stated the way the two above
           are. A trend with nothing in it costs nothing: the slot is not reserved against a
           second reading arriving, because a row whose value column was short by six cells
           until the radio repeated itself would reflow while being read. */
        want = (trailing->spark != NULL && trailing->spark->points != NULL &&
                trailing->spark->points->count >= 2U)
                   ? INKCELL_FB_SPARK_CELLS + 1U
                   : 0U;
        break;
    case INKCELL_FB_TRAILING_CHECKBOX:
    case INKCELL_FB_TRAILING_RADIO: {
        int width = 0;
        inkcell_fb_selection_size(state, state->scale, &width, NULL);
        want = (size_t)((width + adv - 1) / adv) + 2U;
        break;
    }
    case INKCELL_FB_TRAILING_SEGMENTED:
        return inkcell_fb_segmented_cols(state, cols > reserved ? cols - reserved : 0U,
                                         trailing->segmented, NULL);
    case INKCELL_FB_TRAILING_NONE:
    default:
        return 0U;
    }
    /* Strictly greater: the line keeps at least one cell of its own, which is the test the
       conversation cell made for its age before this was a shared slot. */
    return (want > 0U && cols > want + reserved) ? want : 0U;
}

/*
 * Whether this row's trailing slot will come out as a *control* - something the reader can see
 * is theirs to work - rather than as words, or as nothing at all.
 *
 * `marker_yields_to_control` is what asks, and the question is deliberately about what will be
 * drawn rather than about what was requested. Two ways a row can ask for a control and not get
 * one, and both leave a label with nothing beside it:
 *
 *   - **it does not fit.** inkcell_fb_trailing_cols() hands back zero when the line cannot spare
 *     the slot its cells, and the caller draws no trailing at all. A switch and the two
 *     selection controls have a fixed width, so this is the whole of their answer - asked here
 *     with the same call the measuring pass makes, because a fit worked out twice is a fit two
 *     pieces of code can come to disagree about.
 *   - **it has a second form.** A segmented button falls back to its chosen word, which
 *     inkcell_fb_segmented_cols() reports through `as_text`: room for the word is still room, so
 *     a non-zero answer there does not mean the segments were drawn.
 *
 * A meter and a signal staircase are readings. They are pictures of a value rather than offers
 * to change one, so neither supersedes anything in the gutter however well it fits.
 */
static bool inkcell_fb_trailing_is_control(const struct inkcell_draw_state *state, size_t cols,
                                           size_t reserved,
                                           const struct inkcell_fb_trailing *trailing) {
    switch (trailing->kind) {
    case INKCELL_FB_TRAILING_SWITCH:
        if (trailing->sw == NULL) {
            return false;
        }
        break;
    case INKCELL_FB_TRAILING_CHECKBOX:
    case INKCELL_FB_TRAILING_RADIO:
        if (trailing->sel == NULL) {
            return false;
        }
        break;
    case INKCELL_FB_TRAILING_SEGMENTED: {
        /* Already the room question and the form question at once: this returns zero, with
           `as_text` set, when there is not even room for the word. */
        bool as_text = true;
        (void)inkcell_fb_segmented_cols(state, cols > reserved ? cols - reserved : 0U,
                                        trailing->segmented, &as_text);
        return !as_text;
    }
    case INKCELL_FB_TRAILING_NONE:
    default:
        return false;
    }
    return inkcell_fb_trailing_cols(state, cols, reserved, trailing, NULL) > 0U;
}

/* One piece of a headline, clipped to the cells it was given - declared here for the segmented
   slot's fallback, which writes into the row's value column rather than the trailing edge and
   owes that column the same clipping every other piece of the line gets. */
static void inkcell_fb_item_piece(struct inkcell_draw_state *state, int x, int y, const char *text,
                                  size_t cols, struct inkcell_rgb ink, struct inkcell_rgb ground);

/* `reserved` is the same figure inkcell_fb_trailing_cols() was given - see there. Only the slot
   with two forms reads it, and it has to: a segmented button that measured itself against the free
   room and then drew itself against the whole line would be the one kind able to disagree with
   the measure that placed it. */
/*
 * `rest_role` is what the row is standing on when it is *not* the cursor's: the panel, or the
 * surface of the card its group was drawn on. A role rather than a mixed colour because the
 * controls in here take a role - inkcell_fb_draw_segmented() needs to name what it is over, not to
 * be handed pixels.
 *
 * The two grounds below are deliberately different and it is not a shortcut that they come from
 * one parameter. Words and symbols are drawn *on* the cursor fill, so they blend against it. A
 * control that lays a patch of its own - the switch's ring, the meter's track bed - is laying
 * the row's **resting** ground under itself precisely to escape that fill: both are contracted
 * against what the row rests on, and on two of the four themes the cursor fill is the resting
 * track's own colour, so a patch in it would make the control vanish on exactly the row being
 * pointed at. `focused` is already here, so the ink's ground is derived rather than passed and
 * the two cannot be handed the wrong way round.
 */
/* `value_ink` is the ink the row's own value column is written in, and only the segmented
   slot's fallback uses it - see there. */
static void inkcell_fb_draw_trailing(struct inkcell_draw_state *state,
                                     const struct inkcell_fb_item_geom *g, size_t reserved,
                                     const struct inkcell_fb_trailing *trailing, int baseline,
                                     int slot_top, const struct inkcell_fb_list_cue *cue,
                                     const struct inkcell_type_style *meta,
                                     struct inkcell_rgb value_ink) {
    /* Where the cursor is, and separately whether the row's inks moved for it: a control is told
       it is focused whatever the list's focus style, and only a filled row changes ink. */
    const bool focused = cue->focused;
    const bool lifted = cue->lifted;
    const enum inkcell_color rest_role = cue->rest;
    const struct inkcell_rgb ground = cue->ground;
    const struct inkcell_rgb quiet_ink = lifted
                                             ? inkcell_fb_focus_ink(state, INKCELL_TONE_DIM, true)
                                             : inkcell_fb_tone_color(state, INKCELL_TONE_DIM);
    const int scale = state->scale;
    const int adv = inkcell_fb_char_adv(state, scale);
    const size_t cells = trailing->kind == INKCELL_FB_TRAILING_TEXT
                             ? inkcell_fb_text_cols(state, trailing->text, scale)
                             : 0U;

    switch (trailing->kind) {
    case INKCELL_FB_TRAILING_TEXT:
        if (cells == 0U) {
            return;
        }
        /* Always the quiet ink, on the ground and on the fill alike: a trailing figure is
           something the eye glances at on its way past, never the row's own words. */
        if (meta != NULL) {
            /* Metadata, in its own role: flush to the edge by its measured width, and centred on
               the headline's line - the app bar's detail and the sheet's count sit beside their
               titles the same way. */
            const int width = inkcell_fb_text_width_styled(state, trailing->text, meta);
            const int lift =
                (inkcell_fb_line_adv(state, scale) - inkcell_fb_line_adv_styled(state, meta)) / 2;
            inkcell_fb_draw_text_styled(state, g->text_right - width, baseline + lift,
                                        trailing->text, meta, quiet_ink, ground);
            return;
        }
        inkcell_fb_draw_text(state, g->text_right - (int)cells * adv, baseline, trailing->text,
                             scale, quiet_ink, ground);
        return;
    case INKCELL_FB_TRAILING_STATUS: {
        /* The value chip's capsule, against the edge. The same ground rule it follows in the
           value column: the cursor's own surface is what tells the chip to commit to its
           family's full strength rather than vanish into a fill as quiet as its container. */
        const int chip_scale = inkcell_fb_status_scale(state, meta);
        const int width = inkcell_fb_badge_width(state, trailing->text, chip_scale);
        if (width <= 0) {
            return;
        }
        struct inkcell_fb_rect box = {
            .x = g->text_right - width, .y = slot_top, .w = width, .h = g->slot_h};
        int text_y = baseline;
        if (chip_scale != scale) {
            /* A smaller capsule is centred on the headline's line, the way the app bar seats
               its badge on the title: the box is the chip's own line, not the row's slot. */
            text_y =
                baseline +
                (inkcell_fb_line_adv(state, scale) - inkcell_fb_line_adv(state, chip_scale)) / 2;
            box.y = text_y - inkcell_step_px(chip_scale);
            box.h = inkcell_fb_line_adv(state, chip_scale) - inkcell_step_px(chip_scale);
        }
        inkcell_fb_draw_state_chip(state, &box, text_y, trailing->text, trailing->tone,
                                   lifted ? INKCELL_COLOR_SURFACE_SEL : rest_role, value_ink,
                                   chip_scale);
        return;
    }
    case INKCELL_FB_TRAILING_BADGE: {
        /* The capsule is inkcell_fb_draw_badge()'s, not this slot's: the top app bar's trailing
           slot draws the same thing, and two places filling their own round rect is two capsules
           that drift. What the row supplies is the box - a row knows where its own slot is. */
        const int width = inkcell_fb_badge_width(state, trailing->text, scale);
        const struct inkcell_fb_rect box = {
            .x = g->text_right - width, .y = slot_top, .w = width, .h = g->slot_h};
        inkcell_fb_draw_badge(state, &box, baseline, trailing->text, trailing->family, scale);
        return;
    }
    case INKCELL_FB_TRAILING_ICON:
        /* The quiet ink a trailing figure takes, for the same reason: a chevron is something the
           eye passes on its way down the list, never one of the row's own words. */
        inkcell_fb_draw_icon(state, g->text_right - inkcell_fb_icon_box(state, scale), baseline,
                             trailing->icon, scale, quiet_ink, ground);
        return;
    case INKCELL_FB_TRAILING_SWITCH: {
        if (trailing->sw == NULL) {
            return;
        }
        int width = 0;
        int height = 0;
        inkcell_fb_switch_size(state, scale, &width, &height);
        /*
         * Centred on the row's *fill* rather than on the glyph body. The two coincide for the
         * font that ships, but only the fill is what the control has to stay inside, and a
         * switch that overhangs it notches the highlight on the one row the cursor is on.
         */
        trailing->sw->rect.w = width;
        trailing->sw->rect.h = height;
        trailing->sw->rect.x = g->text_right - width;
        trailing->sw->rect.y = g->fill_top + (g->fill_h - height) / 2;
        trailing->sw->focused = focused;
        /* The row's resting ground, written here for the reason `focused` is: what a control
           is standing on is a fact about the row, and a screen asked to remember it is a screen
           that would forget on one list out of nine. Resting rather than current, because the
           ring exists to get the control *out* from under the cursor fill. */
        trailing->sw->ground = rest_role;
        inkcell_fb_draw_switch(state, trailing->sw);
        return;
    }
    case INKCELL_FB_TRAILING_CHECKBOX:
    case INKCELL_FB_TRAILING_RADIO: {
        if (trailing->sel == NULL) {
            return;
        }
        int width = 0;
        int height = 0;
        inkcell_fb_selection_size(state, scale, &width, &height);
        /* Centred on the row's *fill* rather than on the glyph body, for the reason the switch
           is: only the fill is what the control has to stay inside, and one that overhangs it
           notches the highlight on the one row the cursor is on. */
        trailing->sel->rect.w = width;
        trailing->sel->rect.h = height;
        trailing->sel->rect.x = g->text_right - width;
        trailing->sel->rect.y = g->fill_top + (g->fill_h - height) / 2;
        trailing->sel->focused = focused;
        /* Set from the kind, so a caller cannot name a radio and be handed a checkbox. There is
           one statement about which of the two this is and it is the slot's. */
        trailing->sel->shape = trailing->kind == INKCELL_FB_TRAILING_RADIO
                                   ? INKCELL_FB_SELECTION_RADIO
                                   : INKCELL_FB_SELECTION_CHECKBOX;
        inkcell_fb_draw_selection(state, trailing->sel);
        return;
    }
    case INKCELL_FB_TRAILING_SEGMENTED: {
        bool as_text = true;
        const size_t want = inkcell_fb_segmented_cols(
            state, g->cols > reserved ? g->cols - reserved : 0U, trailing->segmented, &as_text);
        if (want == 0U) {
            return;
        }
        if (as_text) {
            /*
             * The chosen word, in the row's own value column - because that is what it now is:
             * a setting whose value is a word, which is what every row around it is.
             *
             * It used to be drawn as a trailing text, quietly and against the right-hand edge,
             * and that was the control's fallback reasoning rather than the row's: the segments
             * were a trailing slot, so the word that replaced them took the trailing slot's
             * place. On the screen it read as a second value column - Display draws "Panel type
             * / Auto" in the column and drew "Layout / Default" against the edge, two rows
             * apart, for no reason a reader could see. A row that has a value column writes its
             * value there.
             *
             * Right-aligned only when there is no column to write in, which is a row that gave
             * its whole line to its words.
             */
            if (reserved > 0U && g->cols > reserved) {
                inkcell_fb_item_piece(state, g->text_x + (int)reserved * adv, baseline,
                                      trailing->segmented->value, g->cols - reserved, value_ink,
                                      ground);
                return;
            }
            inkcell_fb_draw_text(
                state,
                g->text_right - inkcell_fb_text_width(state, trailing->segmented->value, scale),
                baseline, trailing->segmented->value, scale, quiet_ink, ground);
            return;
        }
        const int seg_scale = inkcell_fb_segmented_scale(state);
        const int width = inkcell_fb_segmented_width(state, trailing->segmented, seg_scale);
        /* The switch's height at the *row's* scale, not the segments': two controls in one
           column have to stand the same distance off their rows, and it is the labels that are
           chrome-sized, not the control. */
        const int height = inkcell_fb_segmented_height(state, scale);
        const struct inkcell_fb_rect box = {.x = g->text_right - width,
                                            .y = g->fill_top + (g->fill_h - height) / 2,
                                            .w = width,
                                            .h = height};
        inkcell_fb_draw_segmented(state, &box, trailing->segmented, focused, rest_role, seg_scale);
        return;
    }
    case INKCELL_FB_TRAILING_METER: {
        if (trailing->meter == NULL) {
            return;
        }
        /* Centred on the row's fill, exactly as the switch is and for the same reason: the fill
           is what the control has to stay inside. */
        const int height = inkcell_fb_meter_thickness(state, scale);
        trailing->meter->rect.w = (int)INKCELL_FB_METER_INLINE_CELLS * adv;
        trailing->meter->rect.h = height;
        trailing->meter->rect.x = g->text_right - trailing->meter->rect.w;
        trailing->meter->rect.y = g->fill_top + (g->fill_h - height) / 2;
        trailing->meter->focused = focused;
        trailing->meter->ground = rest_role;
        inkcell_fb_draw_meter(state, trailing->meter);
        return;
    }
    case INKCELL_FB_TRAILING_SIGNAL: {
        /*
         * The rungs against the trailing edge and the figure to their left, which is the order
         * a status bar puts the two in - the signal is the thing being scanned down the column,
         * so it is the thing that keeps the fixed edge.
         *
         * Both take the row's quiet pairing, exactly as a trailing age does, and the lit rungs
         * take the row's own ink. That is deliberately the same two colours the slot already
         * draws everything else in: a staircase is furniture the eye passes on its way down a
         * list, not one of the row's words, and giving quality a colour of its own would put a
         * third statement about the link on a row that has made two.
         */
        const struct inkcell_rgb quiet = quiet_ink;
        const struct inkcell_rgb ink = lifted
                                           ? inkcell_fb_focus_ink(state, INKCELL_TONE_NORMAL, false)
                                           : inkcell_fb_tone_color(state, INKCELL_TONE_NORMAL);
        /*
         * An unlit rung is the meter's track and not the dim text colour, which is what it was
         * first drawn as. The two are different jobs: dim text is held *above* the ground so it
         * stays readable, and a rung that is not lit has nothing to read - it is there to be
         * counted against the lit ones, so it wants the role the theme already validates as the
         * empty part of an indicator. As dim text the gap between two rungs and four was there
         * but had to be looked for.
         *
         * Except under the cursor, where the track is not a colour that can be relied on: two of
         * the four themes make it exactly the cursor fill, which is the reason
         * inkcell_fb_draw_meter() lays a ground of its own. Rungs have gaps between them and no
         * ground to lay, so they take the pairing the cursor does validate.
         */
        const struct inkcell_rgb unlit =
            focused ? quiet : inkcell_fb_color(state, INKCELL_COLOR_METER_TRACK);
        const int width = (int)INKCELL_FB_SIGNAL_CELLS * adv;
        const int height = inkcell_fb_icon_box(state, scale);
        const struct inkcell_fb_rect box = {
            .x = g->text_right - width,
            /* Centred on the row's fill, as the switch and the meter are, so a list of them
               sits on one line however the glyph body and the fill differ. */
            .y = g->fill_top + (g->fill_h - height) / 2,
            .w = width,
            .h = height,
        };
        inkcell_fb_draw_signal(state, &box, trailing->signal, ink, unlit);
        if (meta != NULL) {
            const int figure = inkcell_fb_text_width_styled(state, trailing->text, meta);
            const int lift =
                (inkcell_fb_line_adv(state, scale) - inkcell_fb_line_adv_styled(state, meta)) / 2;
            if (figure > 0) {
                inkcell_fb_draw_text_styled(state, box.x - adv - figure, baseline + lift,
                                            trailing->text, meta, quiet, ground);
            }
            return;
        }
        const int figure = inkcell_fb_text_width(state, trailing->text, scale);
        if (figure > 0) {
            inkcell_fb_draw_text(state, box.x - adv - figure, baseline, trailing->text, scale,
                                 quiet, ground);
        }
        return;
    }
    case INKCELL_FB_TRAILING_SPARK: {
        if (trailing->spark == NULL) {
            return;
        }
        /*
         * On the *line's* slot rather than centred on the row's fill, which is where the switch,
         * the meter and the staircase sit.
         *
         * The difference only shows on the row this was built for and it shows badly: the node
         * detail's battery row is two steps, a fact on the first and a banded bar across the
         * second, so a trend centred on the fill lands in the gap between them and draws its
         * floor through the bar. Every other slot in this column is on a row whose fill is one
         * step, which is why the fill and the line were the same box until now. The slot is the
         * box that means "beside these words", and beside the words is where this belongs.
         */
        const int height = g->slot_h > 0 ? g->slot_h : inkcell_fb_sparkline_height(state, scale);
        trailing->spark->rect.w = (int)INKCELL_FB_SPARK_CELLS * adv;
        trailing->spark->rect.h = height;
        trailing->spark->rect.x = g->text_right - trailing->spark->rect.w;
        trailing->spark->rect.y = slot_top;
        trailing->spark->focused = focused;
        inkcell_fb_draw_sparkline(state, trailing->spark);
        return;
    }
    case INKCELL_FB_TRAILING_NONE:
    default:
        return;
    }
}

/* Cells between the end of the label column and the start of the value: a space, the marker's
   own cell, a space. Reserved on every row of a list whether or not that row has a marker in
   it, which is what keeps the values in one column. */
#define INKCELL_FB_ITEM_MARKER_CELLS 3U

/*
 * One piece of a headline: the words, clipped to the room it was given, in the ink it was
 * given.
 *
 * The headline is drawn in pieces rather than composed into one string and drawn once, which is
 * the whole of what lets a label and its value take two inks - see inkcell_fb_list_item.label_tone.
 * The clipping is per piece and the positions are absolute, so the value column lands in exactly
 * the cell the composed line used to put it in.
 *
 * The room is `cols` cells wide, and the words are fitted to that many *pixels* rather than that
 * many cells: the face is proportional, so a cell is a nominal width and a run of wide letters
 * is wider than the same count of average ones. Clipped by count alone, a base64 key ran past
 * the row's edge and off the card it stood on. Cut on cell boundaries, so a character is never
 * split and a flag or a joined emoji is never taken apart.
 */
static void inkcell_fb_item_piece(struct inkcell_draw_state *state, int x, int y, const char *text,
                                  size_t cols, struct inkcell_rgb ink, struct inkcell_rgb ground) {
    if (cols == 0U) {
        return;
    }
    /* Fitted to the room's pixels and nothing else. It used to be clipped to `cols` characters
       first, which is a count: on a proportional face six narrow letters are fewer than six
       cells of ink, and "Filter" in a column measured to hold it came out "Filt". Down to
       nothing if it has to be: one character wider than a one-cell room is still wider than
       the room, and an empty piece is the answer to a column too narrow for it. */
    char fitted[INKCELL_LINE_MAX];
    snprintf(fitted, sizeof fitted, "%s", text != NULL ? text : "");
    const int room = (int)cols * inkcell_fb_char_adv(state, state->scale);
    while (fitted[0] != '\0' && inkcell_fb_text_width(state, fitted, state->scale) > room) {
        const size_t cells = inkcell_text_cells(fitted);
        if (cells == 0U) {
            fitted[0] = '\0';
            break;
        }
        inkcell_text_cell_truncate(fitted, cells - 1U);
    }
    inkcell_fb_draw_text(state, x, y, fitted, state->scale, ink, ground);
}

/*
 * The same in a type style of its own, fitted to pixels rather than to cells.
 *
 * A tier set smaller than the body is narrower than the cells it was given, so clipping it to a
 * body cell count stops it short of room it has: measured, never counted. Cut on cell boundaries,
 * so a character is never split and a flag or a joined emoji is never taken apart.
 *
 * Centred on the body line it replaces rather than hung from its top, so a smaller second line
 * keeps the rhythm the row's two lines had - the lift is the difference between the two line
 * advances, halved, which is what the sheet's caption beside its title does too.
 */
static void inkcell_fb_item_piece_styled(struct inkcell_draw_state *state, int x, int y,
                                         const char *text, int max_w,
                                         const struct inkcell_type_style *style,
                                         struct inkcell_rgb ink, struct inkcell_rgb ground) {
    if (max_w <= 0) {
        return;
    }
    char fitted[INKCELL_LINE_MAX];
    snprintf(fitted, sizeof fitted, "%s", text != NULL ? text : "");
    while (fitted[0] != '\0' && inkcell_fb_text_width_styled(state, fitted, style) > max_w) {
        const size_t cells = inkcell_text_cells(fitted);
        if (cells == 0U) {
            fitted[0] = '\0';
            break;
        }
        inkcell_text_cell_truncate(fitted, cells - 1U);
    }
    const int lift =
        (inkcell_fb_line_adv(state, state->scale) - inkcell_fb_line_adv_styled(state, style)) / 2;
    inkcell_fb_draw_text_styled(state, x, y + lift, fitted, style, ink, ground);
}

void inkcell_fb_list_item(struct inkcell_draw_state *state, struct inkcell_fb_list *list,
                          uint32_t index, const struct inkcell_fb_list_item *item) {
    inkcell_fb_list_chrome(state, list);
    const bool band = inkcell_fb_list_band_begin(list);
    const int scale = state->scale;
    const uint32_t rows = inkcell_fb_list_row_height(list, index);
    const struct inkcell_fb_item_geom g = inkcell_fb_item_measure(state, list, item, index, rows);
    /* The box the row stands in, taken before anything advances: where a separator under it
       goes is where the next row's box begins. */
    const int box_top = inkcell_fb_list_box_top(state, list);

    /* What every icon on this row is blended against: the fill if the cursor laid one down, and
       otherwise whatever the row is standing on - the panel, or the surface of the card its
       group was drawn on. Asked of the list rather than assumed, because a glyph carries
       coverage and not a mask: text told the wrong ground keeps its shape and gains a halo of a
       colour that is nowhere near it. The focused fill is derived from it too - a lift is a lift
       off whatever is underneath. */
    /*
     * The cursor's mark, in the list's own focus style: the fill (and the accent bar along its
     * leading edge, where the row asked for one), the lighter layer and capsule, or the ring.
     * What comes back is what every ink below is blended against and whether those inks move.
     */
    const struct inkcell_fb_list_cue cue = inkcell_fb_list_cue(
        state, list, index, g.fill_top, g.fill_h, item->tone, item->accent_edge);
    const bool focused = cue.focused;
    const bool lifted = cue.lifted;
    const enum inkcell_color rest_role = cue.rest;
    const struct inkcell_rgb ground = cue.ground;
    const struct inkcell_type_style *meta = g.tiered ? &g.meta : NULL;

    /*
     * Under the cursor everything is drawn against that fill instead of against the ground,
     * which is a different pair of colours and not a dimmer version of the same one.
     */
    /* Where the row's tone is spent on its words. `label_plain` keeps them ordinary and leaves
       the tone to the disc and the accent edge, which is the whole of that flag; see
       inkcell_fb_list_item.label_plain. Resolved once here so the plain row below and the label
       column further down cannot disagree about it. */
    const enum inkcell_tone text_tone = item->label_plain ? INKCELL_TONE_NORMAL : item->tone;
    const struct inkcell_rgb head_ink = inkcell_fb_item_ink(state, text_tone, lifted, false);

    if (item->leading.kind == INKCELL_FB_LEADING_AVATAR ||
        item->leading.kind == INKCELL_FB_LEADING_TONAL) {
        /* The slot the measure reserved, and not a second opinion about it: a disc drawn to any
           other size either leaves a gap its list's other rows do not have or runs under the
           words. Centred in the slot where the measure had to make it smaller than one, so the
           room a card took comes off the disc and never off the column. See
           inkcell_fb_item_measure(). */
        const int size = g.lead_disc;
        const int lead_x = g.content_x + (g.lead_size - size) / 2;
        /*
         * Which pair the disc wears, and the tonal one reads it off the row's tone exactly as
         * the accent bar below does - the row says once what it means and the disc is one of
         * the renderings of that, never a second opinion.
         *
         * The container at rest, because a symbol has to sit on this and a column of them is
         * read rather than spotted. Under the cursor it commits to the family's full strength
         * instead, which is inkcell_fb_button_paint()'s rule for a tonal control word for word, and
         * it is a correction rather than a flourish: a container is picked to be a quiet fill on
         * the body ground, the cursor's own fill is picked to be a quiet fill on the body
         * ground, and two quiet fills are necessarily near each other. Laid on the cursor it
         * came to 1.01:1 on the dark palette's success and 1.04:1 on the colour-blind error -
         * a disc that vanishes on precisely the row being pointed at. The state layer made it
         * worse rather than better, because a layer can only move a fill towards its own ink.
         *
         * The base is the one half of a family the theme already holds to being findable on
         * that fill: it is the pair the marker bar down a focused row is checked as. So the
         * focused row's disc brightens instead of disappearing, and no palette had to move -
         * and on a theme that lifts rather than fills, it is the one part of the row that
         * changes by more than a tone, which is the tile-brightening a console cursor does.
         */
        struct inkcell_paint disc;
        if (item->leading.kind == INKCELL_FB_LEADING_TONAL) {
            const enum inkcell_family family = inkcell_tone_family(item->tone);
            disc = inkcell_fb_paint(
                state, family != INKCELL_FAMILY_COUNT ? family : INKCELL_FAMILY_PRIMARY,
                lifted ? INKCELL_SLOT_BASE : INKCELL_SLOT_CONTAINER, INKCELL_STATE_REST);
        } else {
            disc =
                (struct inkcell_paint){item->leading.role < INKCELL_COLOR_COUNT
                                           ? inkcell_fb_color(state, item->leading.role)
                                           : inkcell_theme_avatar(state->theme, item->leading.tint),
                                       inkcell_fb_color(state, INKCELL_COLOR_BG)};
        }
        inkcell_fb_draw_avatar(state, lead_x, g.lead_y, size, item->leading.label,
                               item->leading.icon, disc);
    } else if (item->leading.kind == INKCELL_FB_LEADING_ICON) {
        inkcell_fb_draw_icon(state, g.content_x, g.head_y, item->leading.icon, scale, head_ink,
                             ground);
    }

    /*
     * What the headline has already spent *inside `g.cols`* before its trailing slot gets a say.
     *
     * Only the label column, and that is the whole of the subtlety. A plain row's marker gutter
     * is spent too, but it is spent by inkcell_fb_item_measure() moving `g.text_x` past it before
     * the columns are counted - so it is already outside this number, and reserving it again took a
     * cell off every row with a marker slot. The label column is the other way round: it is
     * drawn inside `g.cols` rather than measured out of it, so nothing has counted it yet.
     */
    const size_t reserved =
        item->label_cols > 0U ? item->label_cols + INKCELL_FB_ITEM_MARKER_CELLS : 0U;
    const size_t head_take =
        inkcell_fb_trailing_cols(state, g.cols, reserved, &item->trailing, meta);
    const size_t head_cols = g.cols - head_take;
    if (item->label_cols > 0U) {
        /*
         * The label column, then the value in the cell the marker gutter leaves after it. Two
         * draws rather than one, so the question and the answer can be two tiers - which is the
         * whole of what inkcell_fb_list_item.label_tone is for.
         *
         * Both are clipped against `head_cols` rather than against their own widths, which is
         * what keeps this identical to the composed line it replaced: a label column wider than
         * the row cut the label and left the value nowhere to start, and a value column that
         * the trailing slot has eaten into is cut at the same cell either way.
         */
        /* The row's own tone unless the row said the label is its quiet tier, which is what
           keeps a dim section and a strong unsaved field marked across both halves. */
        const enum inkcell_tone label_tone = item->label_quiet ? INKCELL_TONE_DIM : text_tone;
        const size_t label_cols = item->label_cols < head_cols ? item->label_cols : head_cols;
        inkcell_fb_item_piece(state, g.text_x, g.head_y, item->label, label_cols,
                              inkcell_fb_item_ink(state, label_tone, lifted, item->label_quiet),
                              ground);
        const size_t gutter = item->label_cols + INKCELL_FB_ITEM_MARKER_CELLS;
        if (head_cols > gutter) {
            const int value_x = g.text_x + (int)gutter * inkcell_fb_char_adv(state, scale);
            const size_t value_cols = head_cols - gutter;
            /* The capsule, where the row said its value is a state and the column is wide
               enough to hold one. Measured against the room the words would have had, so a
               chip never runs under a trailing slot - and drawn as words when it does not fit,
               which is the fallback the row has because the caller supplied the text either
               way. */
            const int chip_w =
                item->value_chip ? inkcell_fb_badge_width(state, item->value, scale) : 0;
            if (chip_w > 0 && chip_w <= (int)value_cols * inkcell_fb_char_adv(state, scale)) {
                const struct inkcell_fb_rect box = {value_x, g.head_slot_top, chip_w, g.slot_h};
                inkcell_fb_draw_state_chip(state, &box, g.head_y, item->value, item->tone,
                                           lifted ? INKCELL_COLOR_SURFACE_SEL : rest_role, head_ink,
                                           scale);
            } else {
                inkcell_fb_item_piece(state, value_x, g.head_y, item->value, value_cols, head_ink,
                                      ground);
            }
        }
    } else {
        inkcell_fb_item_piece(state, g.text_x, g.head_y, item->text, head_cols, head_ink, ground);
    }
    /* Into the blank cell the label column and the value leave between them, and only when the
       value column actually got that far - a label column wider than the row is clipped, and a
       marker drawn at a column the words no longer reach would sit on top of the label. */
    /*
     * The control the row actually drew, if it drew one, supersedes a marker that was offering
     * the same thing - see marker_yields_to_control.
     *
     * The slider is asked separately because it is a band under the words rather than a trailing
     * slot, and the test is the draw's own, term for term: it needs the second step the list may
     * not have given, and it loses the bar to a meter that wants it. Writing "has a slider"
     * here instead would suppress the mark on a row that drew a *meter* - a reading, which
     * supersedes nothing.
     */
    const bool slider_drawn = item->slider != NULL && g.bar_h > 0 && item->meter == NULL;
    const bool marker_superseded =
        item->marker_yields_to_control &&
        (inkcell_fb_trailing_is_control(state, g.cols, reserved, &item->trailing) || slider_drawn);
    if (!marker_superseded && item->label_cols > 0U && inkcell_icon_is_valid(item->marker_icon) &&
        g.cols > item->label_cols + INKCELL_FB_ITEM_MARKER_CELLS) {
        inkcell_fb_draw_icon(
            state, g.text_x + (int)(item->label_cols + 1U) * inkcell_fb_char_adv(state, scale),
            g.head_y, item->marker_icon, scale, head_ink, ground);
    } else if (!marker_superseded && item->label_cols == 0U && item->marker_slot &&
               inkcell_icon_is_valid(item->marker_icon)) {
        /* Into the cell the measure held back before the words. In the row's own ink, like every
           other icon in a row's slots: the star is as loud as the name it sits beside. */
        inkcell_fb_draw_icon(state, g.marker_x, g.head_y, item->marker_icon, scale, head_ink,
                             ground);
    }
    if (head_take > 0U) {
        inkcell_fb_draw_trailing(state, &g, reserved, &item->trailing, g.head_y, g.head_slot_top,
                                 &cue, meta, head_ink);
    }

    /*
     * The accessory, in its own column against the edge and on the headline's line - a chevron
     * or a check is about the row as a whole, and the headline is what names it.
     *
     * Quiet for a disclosure, which is furniture the eye passes on its way down; the primary for
     * a check, because on a list of options the chosen one is the fact the list exists to state.
     */
    if (item->accessory != INKCELL_FB_ACCESSORY_NONE) {
        const bool check = item->accessory == INKCELL_FB_ACCESSORY_CHECK;
        const enum inkcell_tone tone = check ? INKCELL_TONE_PRIMARY : INKCELL_TONE_DIM;
        inkcell_fb_draw_icon(state, g.accessory_x, g.head_y,
                             check ? INKCELL_ICON_CHECK : INKCELL_ICON_CHEVRON, scale,
                             inkcell_fb_item_ink(state, tone, lifted, !check), ground);
    }

    if (g.rows >= 2U && item->supporting != NULL) {
        const struct inkcell_rgb supp_ink =
            inkcell_fb_item_ink(state, item->supporting_tone, lifted, item->supporting_quiet);
        /* Nothing reserved: a supporting line has no label column, and the icon that may take
           its first cell is tested against what the slot leaves rather than before it. */
        const size_t supp_take =
            inkcell_fb_trailing_cols(state, g.cols, 0U, &item->supporting_trailing, meta);
        /* An icon on the supporting line takes the first cell and the words move over, which is
           what "> " did when it was two characters of the preview. */
        const bool supp_icon =
            inkcell_icon_is_valid(item->supporting_icon) && g.cols > supp_take + 1U;
        const int supp_x = g.text_x + (supp_icon ? inkcell_fb_char_adv(state, scale) : 0);
        if (supp_icon) {
            inkcell_fb_draw_icon(state, g.text_x, g.supp_y, item->supporting_icon, scale, supp_ink,
                                 ground);
        }
        const size_t supp_cols = g.cols - supp_take - (supp_icon ? 1U : 0U);
        if (g.tiered) {
            inkcell_fb_item_piece_styled(state, supp_x, g.supp_y, item->supporting,
                                         (int)supp_cols * inkcell_fb_char_adv(state, scale),
                                         &g.soft, supp_ink, ground);
        } else {
            inkcell_fb_item_piece(state, supp_x, g.supp_y, item->supporting, supp_cols, supp_ink,
                                  ground);
        }
        if (supp_take > 0U) {
            inkcell_fb_draw_trailing(state, &g, 0U, &item->supporting_trailing, g.supp_y,
                                     g.supp_slot_top, &cue, meta, supp_ink);
        }
    }

    /*
     * The bar the row gave a step to, across the width the words had.
     *
     * Only when the list actually gave it that step. A screen that declared a meter row one
     * step tall has made a mistake, and the two ways of answering it are to draw the bar over
     * whatever is under this row or to leave it out; leaving it out is the one a screen notices
     * and the one that cannot corrupt the frame. It is the same rule the trailing slots follow
     * when the line is too narrow for them.
     */
    if (item->meter != NULL && g.bar_h > 0) {
        item->meter->rect.x = g.text_x;
        item->meter->rect.w = g.text_right - g.text_x;
        item->meter->rect.h = g.bar_h;
        item->meter->rect.y = g.bar_y;
        item->meter->focused = focused;
        item->meter->ground = rest_role;
        inkcell_fb_draw_meter(state, item->meter);
    } else if (item->slider != NULL && g.bar_h > 0) {
        /* The same box the meter would have had, and the control centres its own track in it -
           so a reading and the control that sets it start and end in the same two columns, which
           is what lets a section mix the two without the eye finding two different lists. */
        item->slider->rect.x = g.text_x;
        item->slider->rect.w = g.text_right - g.text_x;
        item->slider->rect.h = g.bar_h;
        item->slider->rect.y = g.bar_y;
        item->slider->focused = focused;
        inkcell_fb_draw_slider(state, item->slider);
    }

    /*
     * The divider goes in the gap the tightened leading opened up, inset to where the text
     * starts rather than run edge to edge: a leading disc already separates the items, and a
     * full-width rule under one reads as a box drawn around it.
     */
    /* The window, plus whatever a glide added below it: a row with more rows under it has a
       divider, and a tail row sliding into view is a row with more rows under it. Asking the
       window alone would drop the separator at the seam for the length of every scroll. */
    const uint32_t end = list->model.first + list->model.visible + list->tail_count;
    const bool last = (index + 1U >= list->model.count) || (index + 1U >= end);
    /* Yields to the list's own separators where it asked for them: one hairline per boundary,
       and the list's is the one that knows where the groups break. */
    if (item->divider && !list->style.separators && !focused && !last) {
        inkcell_fb_draw_rule(state, g.text_x,
                             g.fill_top + g.fill_h + inkcell_fb_space(state, INKCELL_SPACE_XS),
                             g.text_right - g.text_x, scale, INKCELL_COLOR_RULE);
    }
    /* The list's separator, on the boundary with the next row and from where the words start
       to the row's trailing edge - which is where a section's hairline runs on every phone. */
    inkcell_fb_list_separator(state, list, index, g.text_x, box_top + (int)g.rows * list->line);

    inkcell_fb_list_band_end(list, band);
    inkcell_fb_list_focus_row(state, list, index, g.fill_top, g.fill_h);
    list->y += (int)g.rows * list->line;
}

/*
 * A conversation is a two-line item with a disc on the front, a time on the first line and an
 * unread count on the second - which is the whole of what it is, now that the item can say
 * that. What is left here is the *translation*: which of a conversation's facts goes in which
 * slot, and which of them changes what the row says rather than how it looks.
 */
void inkcell_fb_draw_conversation(struct inkcell_draw_state *state, struct inkcell_fb_list *list,
                                  uint32_t index,
                                  const struct inkcell_fb_conversation *conversation) {
    inkcell_fb_list_chrome(state, list);
    /*
     * Unread, and allowed to say so.
     *
     * The two emphases a new message earns - the preview at full weight, and the row refusing
     * to stay quiet under the cursor - are exactly what a mute is asking this row to stop
     * doing, so they are decided together here rather than by the caller passing `unread`
     * false. The count itself is untouched: it goes on drawing, one family quieter.
     */
    const bool emphasis = conversation->unread && !conversation->muted;
    struct inkcell_line preview;
    inkcell_line_reset(&preview);
    if (conversation->armed) {
        /* The armed row says what the next press does, in place of the preview it would take
           away. Nothing else on screen changes, so the warning is on the row it is about - and
           it stays loud under the cursor, which a preview does not. */
        inkcell_line_printf(&preview, "%s", inkcell_str(conversation->armed_label));
    } else if (conversation->preview[0] != '\0') {
        inkcell_line_printf(&preview, "%s", conversation->preview);
    } else {
        inkcell_line_printf(&preview, "%s", inkcell_str(conversation->empty_label));
    }

    const struct inkcell_fb_list_item item = {
        .leading =
            {
                .kind = INKCELL_FB_LEADING_AVATAR,
                .label = conversation->avatar,
                /* A row armed to be deleted says so inside its own disc, which is already
                   wearing the bad tone: the question and the answer in one place. */
                .icon = conversation->armed ? INKCELL_ICON_DELETE : conversation->avatar_icon,
                .tint = conversation->tint,
                .role = conversation->armed    ? INKCELL_COLOR_ERROR
                        : conversation->accent ? INKCELL_COLOR_PRIMARY
                                               : INKCELL_COLOR_COUNT,
            },
        .text = conversation->name,
        .tone = conversation->name_tone,
        /* The bell with a stroke through it, in the gutter the pinned star uses. The slot is
           reserved on every row and not only on the muted ones, because a list that indents
           only some of its rows is a list whose names start in two columns. */
        .marker_icon = conversation->muted ? INKCELL_ICON_MUTED : INKCELL_ICON_NONE,
        .marker_slot = true,
        /* The age sits against the right edge, quietly, the way a messenger dates a
           conversation - a fact you glance at, not one you read. */
        .trailing = {.kind = INKCELL_FB_TRAILING_TEXT, .text = conversation->age},
        .supporting = inkcell_line_text(&preview),
        /* The reply arrow says the last word in the thread was ours, which is what tells you
           whether a quiet thread is waiting on you or on them. It was "> " until it was an
           icon slot, and it is dropped on an armed row - what that row says is the warning. */
        .supporting_icon = (conversation->preview_outbound && !conversation->armed)
                               ? INKCELL_ICON_REPLY
                               : INKCELL_ICON_NONE,
        /* An unread preview is the row's own words at full weight - never the name's tone,
           which says what kind of conversation this is rather than how much of it is new. The
           half that still reads once the badge has been marked away. */
        .supporting_tone = conversation->armed ? INKCELL_TONE_ERROR
                           : emphasis          ? INKCELL_TONE_STRONG
                                               : INKCELL_TONE_DIM,
        .supporting_quiet = !conversation->armed && !emphasis,
        /* Secondary rather than the accent on a muted row - see struct inkcell_fb_conversation. The
           capsule is still there and still counts; it just stops being the loudest thing on
           the screen, which is the whole of what the user asked for. */
        .supporting_trailing = {.kind = INKCELL_FB_TRAILING_BADGE,
                                .family = conversation->muted ? INKCELL_FAMILY_SECONDARY
                                                              : INKCELL_FAMILY_PRIMARY,
                                .text = conversation->badge},
        .accent_edge = true,
        .divider = true,
    };
    inkcell_fb_list_item(state, list, index, &item);
}

size_t inkcell_fb_field_label_cols(const struct inkcell_draw_state *state,
                                   const struct inkcell_fb_layout *layout, size_t preferred) {
    const struct inkcell_metrics *metrics = inkcell_fb_metrics(state);
    if (preferred == 0U) {
        preferred = metrics->field_label_cols;
    }
    return layout->cols < metrics->narrow_cols ? layout->cols / 2U : preferred;
}

size_t inkcell_fb_field_label_cols_fit(const struct inkcell_draw_state *state,
                                       const struct inkcell_fb_layout *layout,
                                       const char *const *labels, size_t count) {
    const int adv = inkcell_fb_char_adv(state, state->scale);
    int widest = 0;
    for (size_t i = 0U; labels != NULL && i < count; ++i) {
        if (labels[i] != NULL) {
            const int w = inkcell_fb_text_width(state, labels[i], state->scale);
            widest = w > widest ? w : widest;
        }
    }
    size_t cols = adv > 0 ? (size_t)((widest + adv - 1) / adv) : 0U;
    if (cols == 0U) {
        cols = 1U;
    }
    /* The value keeps at least half the row, whatever the labels ask for: a label trimmed to fit
       is still a label, and a value pushed off the row is nothing. */
    const size_t half = layout->cols / 2U;
    return half > 0U && cols > half ? half : cols;
}
