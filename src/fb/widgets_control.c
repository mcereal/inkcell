#define _POSIX_C_SOURCE 200809L

/*
 * The editable controls. Each owns an animation id, and each states the size it wants at a
 * scale so that a row can leave it room before it knows what will go in.
 */

#include "inkcell/ui/widgets/button.h"
#include "inkcell/ui/widgets/control.h"

#include "inkcell/ui/anim.h"
#include "inkcell/ui/emoji.h"
#include "inkcell/ui/layout.h"

#include <stdio.h>

/* ---- the switch -------------------------------------------------------------------------- */

/*
 * How long a knob takes to cross, and on what curve.
 *
 * The short token: what a control answering a button press wants - long enough that the eye
 * follows the knob across rather than seeing it teleport, short enough that nobody waits for
 * it. Ease-out because the press has already happened; the movement is the screen catching up,
 * so it should leave briskly and settle rather than wind up first.
 */
#define INKCELL_FB_SWITCH_MOTION INKCELL_MOTION_SHORT

/* Shorter than the line advance, which carries the gap between rows: a control as tall as the
   advance touches the row above it. */
static int inkcell_fb_switch_height(const struct inkcell_backend_fb_state *state, int scale) {
    const int height =
        inkcell_fb_line_adv(state, scale) - inkcell_fb_space_at(state, INKCELL_SPACE_MD, scale);
    return height < 6 ? 6 : height;
}

/*
 * The whole of the control's geometry, derived from the glyph metrics.
 *
 * A switch is a wide pill: 9:5 is close to what the phone platforms use and is what stops it
 * reading as a circle at a small scale. Everything else - the corner radius, the ring the knob
 * sits in, how far it travels - falls out of the height below, so a theme that asks for bigger
 * text gets a proportionally bigger control and no renderer is touched.
 */
void inkcell_fb_switch_size(const struct inkcell_backend_fb_state *state, int scale, int *w,
                            int *h) {
    const int height = inkcell_fb_switch_height(state, scale);
    if (h != NULL) {
        *h = height;
    }
    if (w != NULL) {
        *w = height * 9 / 5;
    }
}

void inkcell_fb_draw_switch(struct inkcell_backend_fb_state *state,
                            const struct inkcell_fb_switch *sw) {
    if (sw == NULL || sw->rect.w <= 0 || sw->rect.h <= 0) {
        return;
    }

    /*
     * Where the knob is, which is the one thing the snapshot cannot say. The table answers
     * with the target on first sight of this id and with a position in between afterwards -
     * so a screen opening does not animate and a press does.
     */
    const int32_t position =
        inkcell_anim_track(&state->anim, sw->id, state->now_ms, sw->on ? INKCELL_ANIM_ONE : 0,
                           inkcell_fb_motion(state, INKCELL_FB_SWITCH_MOTION), INKCELL_EASE_OUT);

    const int damage_pad = inkcell_fb_space(state, INKCELL_SPACE_XS);
    inkcell_fb_animation_damage(state, sw->rect.x - damage_pad, sw->rect.y - damage_pad,
                                sw->rect.w + 2 * damage_pad, sw->rect.h + 2 * damage_pad);
    const int radius = sw->rect.h / 2;

    /*
     * Its own ground under a cursor fill. Both colour pairs below are contracted against the
     * ground rather than against the cursor fill, and on the dark and colorblind themes the
     * cursor fill and the resting track are the same colour - so without this the control
     * would be invisible on precisely the row being pointed at.
     */
    if (sw->selected) {
        /* The ring is a fraction of the glyph scale rather than of the control, because the row
           fill it has to stay inside is measured from the scale too - a ring sized off the
           control overhung the highlight bar top and bottom and notched it. */
        const int pad = inkcell_fb_space(state, INKCELL_SPACE_XS);
        inkcell_fb_fill_round_rect(state, sw->rect.x - pad, sw->rect.y - pad, sw->rect.w + 2 * pad,
                                   sw->rect.h + 2 * pad, radius + pad,
                                   inkcell_fb_color(state, sw->ground));
    }

    /*
     * The track takes its colour from the end the knob has passed, rather than fading between
     * the two as it travels.
     *
     * A fade was the first thing tried and it is wrong for a reason worth writing down: the
     * pairs below are validated against each other, and a track halfway between two of them is
     * not validated against either knob colour - so the one moment the eye is actually
     * following the control is the moment its contrast is unaccounted for. It also looks bad,
     * because on the dark theme the two ends are yellow and blue and everything between them
     * is mud. Flipping at the midpoint keeps every frame a pair a theme was held to.
     *
     * A dim switch reports a state rather than offering one, so it stays muted at both ends and
     * never takes the accent.
     */
    const bool past_middle = position > INKCELL_ANIM_ONE / 2;
    const struct inkcell_paint on_paint =
        inkcell_fb_paint(state, sw->family, INKCELL_SLOT_BASE, INKCELL_STATE_REST);
    struct inkcell_rgb track;
    struct inkcell_rgb knob;
    if (sw->dim) {
        track =
            inkcell_fb_color(state, past_middle ? INKCELL_COLOR_RULE_STRONG : INKCELL_COLOR_RULE);
        knob = inkcell_fb_color(state, INKCELL_COLOR_TEXT_DIM);
    } else if (past_middle) {
        /* The on end is the family, fill and knob taken from the one pair. */
        track = on_paint.fill;
        knob = on_paint.ink;
    } else {
        track = inkcell_fb_color(state, INKCELL_COLOR_SURFACE_SEL);
        knob = inkcell_fb_color(state, INKCELL_COLOR_TEXT_ON_SEL);
    }
    inkcell_fb_fill_round_rect(state, sw->rect.x, sw->rect.y, sw->rect.w, sw->rect.h, radius,
                               track);

    /* The knob: a disc inside a ring of track, travelling the width less that ring. */
    const int inset = sw->rect.h / 8 + 1;
    const int knob_size = sw->rect.h - 2 * inset;
    const int travel = sw->rect.w - 2 * inset - knob_size;
    const int x = sw->rect.x + inset + (travel > 0 ? (travel * position) / INKCELL_ANIM_ONE : 0);
    inkcell_fb_fill_round_rect(state, x, sw->rect.y + inset, knob_size, knob_size, knob_size / 2,
                               knob);
}

/* ---- the selection control ----------------------------------------------------------------- */

/*
 * The same token as the switch's, and for the same reason: both answer a press, so both have to
 * be somewhere by the time the eye gets back to them.
 */
#define INKCELL_FB_SELECTION_MOTION INKCELL_MOTION_SHORT

void inkcell_fb_selection_size(const struct inkcell_backend_fb_state *state, int scale, int *w,
                               int *h) {
    /* A square of the switch's height, so a list mixing the two controls has them the same
       distance off its rows' top and bottom edges. */
    const int side = inkcell_fb_switch_height(state, scale);
    if (w != NULL) {
        *w = side;
    }
    if (h != NULL) {
        *h = side;
    }
}

/*
 * The largest glyph multiplier whose icon fits inside `box` pixels.
 *
 * A checkbox's tick is an icon, on the same terms as every other symbol in this UI, and an icon
 * is sized in glyph scales rather than in pixels - so fitting one inside a control means asking
 * the drawing code how big each scale comes out rather than picking a number. Zero when even the
 * smallest overruns, which is a checkbox that draws its fill and no tick: at that size the fill
 * is the whole of what can be read anyway.
 */
static int inkcell_fb_icon_scale_within(const struct inkcell_backend_fb_state *state, int box) {
    for (int scale = state->scale; scale > 0; scale -= INKCELL_SCALE(1)) {
        if (inkcell_fb_icon_drawn(state, scale) <= box) {
            return scale;
        }
    }
    return 0;
}

void inkcell_fb_draw_selection(struct inkcell_backend_fb_state *state,
                               const struct inkcell_fb_selection *sel) {
    if (sel == NULL || sel->rect.w <= 0 || sel->rect.h <= 0) {
        return;
    }

    /*
     * How far the mark has grown, which - exactly as with the switch's knob - is the one thing
     * the snapshot cannot say: it knows on or off, not where between them this control is. The
     * table answers with the target the first time it sees an id, so a screen opening does not
     * animate and a press does.
     */
    const int32_t position =
        inkcell_anim_track(&state->anim, sel->id, state->now_ms, sel->on ? INKCELL_ANIM_ONE : 0,
                           inkcell_fb_motion(state, INKCELL_FB_SELECTION_MOTION), INKCELL_EASE_OUT);

    const int damage_pad = inkcell_fb_space(state, INKCELL_SPACE_XS);
    inkcell_fb_animation_damage(state, sel->rect.x - damage_pad, sel->rect.y - damage_pad,
                                sel->rect.w + 2 * damage_pad, sel->rect.h + 2 * damage_pad);
    const bool radio = (sel->shape == INKCELL_FB_SELECTION_RADIO);
    const int side = sel->rect.w < sel->rect.h ? sel->rect.w : sel->rect.h;
    /* A circle for one-of-these, a rounded square for any-of-these. The shape scale answers the
       square, because that is what a small container is; the circle is not a radius a theme gets
       to have an opinion about - a radio that is not round is not a radio. */
    const int radius = radio ? side / 2 : inkcell_fb_radius(state, INKCELL_SHAPE_SM);
    const int edge = inkcell_fb_edge(state);
    /* Two hairlines, not one: this ring has to be countable down a column at arm's length, and
       a card's edge is drawn against a fill that is doing half the work of saying it is there. */
    const int ring = edge * 2 > side / 2 ? edge : edge * 2;

    /*
     * Its own ground under a cursor fill, for the reason the switch lays one: the pairs below
     * are contracted against the ground, and on two of the four themes the cursor fill is the
     * colour the resting control is drawn in - so without this the control would vanish on
     * precisely the row being pointed at.
     */
    const struct inkcell_rgb ground = inkcell_fb_color(state, INKCELL_COLOR_BG);
    if (sel->selected) {
        const int pad = inkcell_fb_space(state, INKCELL_SPACE_XS);
        inkcell_fb_fill_round_rect(state, sel->rect.x - pad, sel->rect.y - pad,
                                   sel->rect.w + 2 * pad, sel->rect.h + 2 * pad, radius + pad,
                                   ground);
    }

    /*
     * The colours flip at the midpoint rather than fading, which is the rule
     * inkcell_fb_draw_switch() arrived at and wrote down: the theme validates pairs, and a colour
     * halfway between two of them is validated against neither - so the one moment the eye is
     * following the control is the moment its contrast is unaccounted for.
     *
     * A dim control reports a state rather than offering one, so it never takes the accent.
     */
    const bool past_middle = position > INKCELL_ANIM_ONE / 2;
    const struct inkcell_paint on_paint =
        inkcell_fb_paint(state, sel->family, INKCELL_SLOT_BASE, INKCELL_STATE_REST);
    struct inkcell_rgb mark = on_paint.fill;
    struct inkcell_rgb outline = inkcell_fb_color(state, INKCELL_COLOR_OUTLINE);
    if (sel->dim) {
        mark = inkcell_fb_color(state, INKCELL_COLOR_TEXT_DIM);
        outline = inkcell_fb_color(state, INKCELL_COLOR_RULE);
    } else if (past_middle) {
        /* The ring joins the mark once the mark is the thing being read: an accent dot inside a
           grey ring reads as a dot that has landed in the wrong control. */
        outline = mark;
    }

    /*
     * The ring, as a ring.
     *
     * This was two fills - the larger shape, then the smaller one in the ground colour laid
     * over it - which is how every outline in this file used to be drawn. That needs to know
     * what is behind the hole, and it steps twice, once on each of the two edges it paints.
     * inkcell_fb_stroke_round_rect() is one shape: the hole is left alone rather than repainted,
     * so a radio inside a selected row no longer punches the row's fill back to the body ground.
     */
    inkcell_fb_stroke_round_rect(state, sel->rect.x, sel->rect.y, side, side, radius, ring,
                                 outline);

    /*
     * The mark, grown from the centre.
     *
     * A radio's dot stays inside its ring - the ring is what says there are others, so it never
     * goes away. A checkbox's fill takes the whole box, ring included, because a checked box is
     * a filled box on every platform there is and the tick has to have a validated fill under
     * it: `on_paint` is a pair, and drawing its ink over the ground would be using half of one.
     */
    const int room = radio ? side - 2 * (ring + ring) : side;
    const int size = room > 0 ? (room * position) / INKCELL_ANIM_ONE : 0;
    if (size <= 0) {
        return;
    }
    const int mark_x = sel->rect.x + (side - size) / 2;
    const int mark_y = sel->rect.y + (side - size) / 2;
    inkcell_fb_fill_round_rect(state, mark_x, mark_y, size, size,
                               radio ? size / 2 : (radius < size / 2 ? radius : size / 2), mark);
    if (radio || !past_middle) {
        return;
    }

    /* The tick, once there is a fill validated to draw it on. Sized to the box rather than to
       the row's text, and centred on the control: inkcell_fb_draw_icon() places an icon against a
       text baseline, so what it is handed here is the baseline that would put one there. */
    const int icon_scale = inkcell_fb_icon_scale_within(state, size - 2 * ring);
    if (icon_scale <= 0) {
        return;
    }
    inkcell_fb_draw_icon(
        state, sel->rect.x + (side - inkcell_fb_icon_box(state, icon_scale)) / 2,
        sel->rect.y +
            (side - inkcell_scale_px((int)inkcell_fb_font(state)->height, icon_scale)) / 2,
        INKCELL_ICON_CHECK, icon_scale, on_paint.ink, mark);
}

/* ---- the segmented button ------------------------------------------------------------------ */

int inkcell_fb_segmented_height(const struct inkcell_backend_fb_state *state, int scale) {
    return inkcell_fb_switch_height(state, scale);
}

int inkcell_fb_segmented_width(const struct inkcell_backend_fb_state *state,
                               const struct inkcell_fb_segmented *segmented, int scale) {
    if (segmented == NULL || segmented->count == 0U ||
        segmented->count > INKCELL_FB_SEGMENTED_MAX) {
        return 0;
    }
    /*
     * Every segment as wide as the widest label, and a segment is exactly a button's worth of
     * room around it - inkcell_fb_button_width() rather than a padding of this component's own,
     * because that is what actually draws here and a strip that measured itself differently from
     * the way it draws is a strip whose last segment falls off the row.
     *
     * Equal shares are not a simplification. A strip whose segments were each sized to their own
     * words is a chip strip, and what separates the two components is precisely that these are
     * *alternatives*: the eye has to compare them, and three boxes of three different widths are
     * read as three different kinds of thing.
     */
    int widest = 0;
    for (size_t i = 0; i < segmented->count; ++i) {
        const int width =
            inkcell_fb_button_width(state, INKCELL_ICON_NONE, segmented->labels[i], scale);
        widest = width > widest ? width : widest;
    }
    return (int)segmented->count * widest;
}

void inkcell_fb_draw_segmented(const struct inkcell_backend_fb_state *state,
                               const struct inkcell_fb_rect *rect,
                               const struct inkcell_fb_segmented *segmented, bool selected,
                               enum inkcell_color ground, int scale) {
    /* `active` is checked rather than clamped, and that is the point: inkcell_fb_segmented_cols()
       sends a choice outside the set to the words, so reaching here with one means the measure and
       the draw have disagreed - and drawing the first segment lit would answer the disagreement
       with a claim about the radio's configuration. Nothing is the safe answer. */
    if (rect == NULL || segmented == NULL || segmented->count == 0U ||
        segmented->count > INKCELL_FB_SEGMENTED_MAX || segmented->active >= segmented->count ||
        rect->w <= 0 || rect->h <= 0) {
        return;
    }
    const int radius = inkcell_fb_radius(state, INKCELL_SHAPE_FULL);
    const int edge = inkcell_fb_edge(state);

    /* Its own ground under a cursor fill, on the same terms as the switch's and the selection
       control's: the container is an outline, so what shows through it is whatever is behind -
       and behind it on the row the cursor is on is a fill the outline was never contracted
       against. */
    if (selected) {
        const int pad = inkcell_fb_space(state, INKCELL_SPACE_XS);
        inkcell_fb_fill_round_rect(state, rect->x - pad, rect->y - pad, rect->w + 2 * pad,
                                   rect->h + 2 * pad, radius,
                                   inkcell_fb_color(state, INKCELL_COLOR_BG));
    }

    /*
     * The container: one outline around the set, which is the whole of what says these are
     * alternatives rather than a row of separate offers.
     *
     * A ring rather than the two fills this used to be. The second of those repainted the
     * inside with what the caller *said* was behind - so a strip drawn over anything else put a
     * patch of the wrong colour inside its own outline. A stroke leaves the inside alone, and
     * what the labels are blended against is the segment button's own `ground`, which is a
     * faint halo at worst rather than a hole.
     */
    inkcell_fb_stroke_round_rect(state, rect->x, rect->y, rect->w, rect->h, radius, edge,
                                 inkcell_fb_color(state, INKCELL_COLOR_OUTLINE));

    const size_t active = segmented->active;
    for (size_t i = 0; i < segmented->count; ++i) {
        /* Divided by multiplying rather than by accumulating a width, so the rounding error is
           spread over the strip instead of piling up on the last segment. */
        const int left = rect->x + (int)((size_t)rect->w * i / segmented->count);
        const int right = rect->x + (int)((size_t)rect->w * (i + 1U) / segmented->count);

        /*
         * A hairline between two segments, and not against the selected one - which is
         * Material's rule and is right for the reason the divider exists at all: a separator
         * says "these two are different things", and a filled segment has already said it.
         */
        if (i > 0U && i != active && i - 1U != active) {
            inkcell_fb_fill_rect(state, left, rect->y + edge, edge, rect->h - 2 * edge,
                                 inkcell_fb_color(state, INKCELL_COLOR_OUTLINE));
        }

        /*
         * The segment itself is a button, because that is what it is: the tonal fill on the
         * chosen one and a dim label on the rest is the pairing inkcell_fb_draw_chip() already uses
         * for a tab, and a second opinion about it here would be a segmented control that drifted
         * away from the tab strip it is a sibling of.
         *
         * Inset by the hairline so the container's outline survives underneath the fill, and by
         * it again at the two ends so a full-radius fill follows the container's corner rather
         * than squaring it off.
         */
        const struct inkcell_fb_button segment = {
            .rect = {.x = left + (i == 0U ? edge : 0),
                     .y = rect->y + edge,
                     .w = right - left - (i == 0U ? edge : 0) -
                          (i + 1U == segmented->count ? edge : 0),
                     .h = rect->h - 2 * edge},
            .label = segmented->labels[i],
            .variant = i == active ? INKCELL_FB_BUTTON_TONAL : INKCELL_FB_BUTTON_TEXT,
            .shape = INKCELL_SHAPE_FULL,
            .idle_tone = INKCELL_TONE_DIM,
            .ground = selected ? INKCELL_COLOR_BG : ground,
            .scale = scale,
        };
        inkcell_fb_draw_button(state, &segment);
    }
}

/* ---- the text field ------------------------------------------------------------------------ */

/* The box's own height, without the label over it or the counter under it. */
static int inkcell_fb_text_field_box_h(const struct inkcell_backend_fb_state *state,
                                       const struct inkcell_fb_layout *layout,
                                       const struct inkcell_fb_text_field *field) {
    const uint32_t lines = field->lines > 0U ? field->lines : 1U;
    return (int)lines * layout->line + inkcell_step_px(state->scale);
}

/* The label's line, and the counter's. Both are chrome: they report on the field rather than
   being part of what is in it, so they are drawn at the smaller scale the footer and the tab
   strip use. Zero when the field carries neither. */
static int inkcell_fb_text_field_label_h(const struct inkcell_backend_fb_state *state,
                                         const struct inkcell_fb_layout *layout,
                                         const struct inkcell_fb_text_field *field) {
    return (field->label != NULL && field->label[0] != '\0')
               ? inkcell_fb_line_adv(state, layout->small)
               : 0;
}

static int inkcell_fb_text_field_counter_h(const struct inkcell_backend_fb_state *state,
                                           const struct inkcell_fb_layout *layout,
                                           const struct inkcell_fb_text_field *field) {
    return (field->counter != NULL && field->counter[0] != '\0')
               ? inkcell_fb_line_adv(state, layout->small)
               : 0;
}

int inkcell_fb_text_field_height(const struct inkcell_backend_fb_state *state,
                                 const struct inkcell_fb_layout *layout,
                                 const struct inkcell_fb_text_field *field) {
    if (field == NULL) {
        return 0;
    }
    return inkcell_fb_text_field_label_h(state, layout, field) +
           inkcell_fb_text_field_box_h(state, layout, field) +
           inkcell_fb_text_field_counter_h(state, layout, field) + inkcell_step_px(state->scale);
}

void inkcell_fb_draw_text_field(const struct inkcell_backend_fb_state *state,
                                const struct inkcell_fb_layout *layout, int *y,
                                const struct inkcell_fb_text_field *field) {
    if (field == NULL || y == NULL) {
        return;
    }
    const int scale = state->scale;
    const int margin = inkcell_fb_margin(state);
    const int box_x = inkcell_fb_gutter(state);
    const int box_w = inkcell_fb_panel_width(state) - margin;
    const int box_h = inkcell_fb_text_field_box_h(state, layout, field);
    const uint32_t lines = field->lines > 0U ? field->lines : 1U;
    int top = *y;

    if (inkcell_fb_text_field_label_h(state, layout, field) > 0) {
        inkcell_fb_draw_text(
            state, margin, top, field->label, layout->small,
            inkcell_fb_tone_color(state, field->error ? INKCELL_TONE_ERROR : INKCELL_TONE_DIM),
            inkcell_fb_color(state, INKCELL_COLOR_BG));
        top += inkcell_fb_text_field_label_h(state, layout, field);
    }

    /*
     * The edge first and the fill inside it, exactly as a card is built: there is no stroke
     * primitive here, and a rounded outline is the shape drawn once in the edge colour with the
     * shape drawn again a hairline smaller over the top of it.
     */
    const int edge = inkcell_fb_edge(state);
    const int radius = inkcell_fb_radius(state, INKCELL_SHAPE_MD);
    inkcell_fb_fill_round_rect(
        state, box_x, top, box_w, box_h, radius + edge,
        inkcell_fb_color(state, field->error ? INKCELL_COLOR_ERROR : INKCELL_COLOR_OUTLINE));
    inkcell_fb_fill_round_rect(state, box_x + edge, top + edge, box_w - 2 * edge, box_h - 2 * edge,
                               radius, inkcell_fb_color(state, INKCELL_COLOR_SURFACE_HIGH));

    /*
     * The value, with the caret on the end of it, showing the *tail* when it is longer than the
     * box holds: the caret is where the next press lands, so it is the end that has to stay on
     * screen. Measured and cut in cells, so a draft of emoji scrolls a glyph at a time rather
     * than splitting one down the middle.
     */
    char shown[INKCELL_LINE_MAX];
    snprintf(shown, sizeof shown, "%s%s", field->value != NULL ? field->value : "",
             field->caret ? "_" : "");
    const size_t visible = layout->cols * (size_t)lines;
    const char *tail = shown;
    const size_t width = inkcell_text_cells(shown);
    if (width > visible) {
        tail = shown + inkcell_text_cell_offset(shown, width - visible);
    }
    /* The value sits a scale down from the box's own top edge, which is the inset every other
       container here gives its contents. */
    inkcell_fb_draw_wrapped(state, top + inkcell_step_px(scale), tail, (size_t)layout->body_w,
                            (int)lines, inkcell_fb_tone_color(state, INKCELL_TONE_STRONG),
                            inkcell_fb_color(state, INKCELL_COLOR_SURFACE_HIGH));
    top += box_h;

    if (inkcell_fb_text_field_counter_h(state, layout, field) > 0) {
        /* Against the box's own trailing edge rather than the body's - it belongs to the field,
           and a figure that does not line up with the container it reports on reads as loose. */
        const int x = box_x + box_w - inkcell_fb_text_width(state, field->counter, layout->small);
        inkcell_fb_draw_text(
            state, x, top, field->counter, layout->small,
            inkcell_fb_tone_color(state, field->error ? INKCELL_TONE_ERROR : INKCELL_TONE_DIM),
            inkcell_fb_color(state, INKCELL_COLOR_BG));
        top += inkcell_fb_text_field_counter_h(state, layout, field);
    }

    *y = top + inkcell_step_px(scale);
}
