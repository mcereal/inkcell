#define _POSIX_C_SOURCE 200809L

/*
 * The chat bubble: the wrapped body, the quoted line over it, the reactions under it, and the
 * meta row carrying the time, the relay chip and the delivery mark.
 */

#include "inkcell/ui/widgets/bubble.h"
#include "inkcell/ui/widgets/chrome.h"

#include "inkcell/ui/emoji.h"
#include "inkcell/ui/layout.h"

#include <string.h>

/* ---- chat bubbles ------------------------------------------------------------------------- */

/* One cell of air separates two parts of the trailing run, counted by the measure and spent by
   the draw. The icon's own width is not a cell - see inkcell_fb_icon_box(). */
#define INKCELL_FB_BUBBLE_META_GAP 1U

/* Reactions, relay chip, padlock, clock, delivery mark: the run is never longer than its five
   slots. */
#define INKCELL_FB_BUBBLE_META_PARTS 5U

/* One drawable part of a trailing run: a text or an icon, never both. */
struct inkcell_fb_bubble_part {
    const char *text; /* NULL when this part is an icon */
    enum inkcell_icon icon;
    size_t width; /* measured, in pixels - never a cell count */
};

/*
 * What a bubble works out to at this width, computed once and handed to both the measure and
 * the draw. Every width here is measured in pixels; `rows` is body rows. Nothing in this
 * file is a cell count except the two spacing constants, which are turned into pixels at
 * the point of use - see INKCELL_FB_BUBBLE_META_GAP.
 */
struct inkcell_fb_bubble_metrics {
    size_t width;       /* inner content width, in pixels */
    uint32_t lines;     /* wrapped text lines */
    uint32_t notes;     /* wrapped lines of the failure reason under it */
    size_t last;        /* the width of the last line drawn, which the run tucks onto */
    bool meta_own_line; /* the run did not fit on the last one */
    struct inkcell_fb_bubble_part parts[INKCELL_FB_BUBBLE_META_PARTS];
    size_t part_count;
    size_t meta_width;  /* what the parts and their gaps come to, in pixels; 0 for no run */
    size_t quote_width; /* the quote line's text, elided to fit; 0 when there is no quote */
    uint32_t rows;
};

/* The bar down a quote's left edge and the gap after it, in cells. Both live here so the
   measure's width and the draw's text origin cannot disagree about the indent. */
#define INKCELL_FB_BUBBLE_QUOTE_INDENT 2U

/* A bubble never spans the whole panel: the gutter down the other side is what says which end
   of the conversation it came from, so three quarters is a look rather than a limit. */
/*
 * The widest a bubble may grow, in pixels.
 *
 * Pixels rather than columns, and so is everything derived from it below: a bubble is sized to
 * its own words, so the measure and the draw have to agree to the pixel or a transcript paints
 * over itself. See `body_w` on struct inkcell_fb_layout.
 */
static size_t inkcell_fb_bubble_max_width(const struct inkcell_backend_fb_state *state,
                                          const struct inkcell_fb_layout *layout) {
    const size_t pct = inkcell_fb_metrics(state)->bubble_width_pct;
    const size_t body = layout->body_w > 0 ? (size_t)layout->body_w : 1U;
    const size_t max = body * pct / 100U;
    return max > 0U ? max : 1U;
}

static bool inkcell_fb_bubble_has(const char *text) {
    return text != NULL && text[0] != '\0';
}

/*
 * The fill and the ink a bubble is drawn in.
 *
 * Three cases, and each is a *container* rather than a colour of its own: ours is the secondary
 * family, one that failed is the error family, and one of theirs is the neutral raised tier -
 * because "somebody else said this" is not a verdict, and a hue there would compete with the
 * sender line, which is a verdict. The cursor is a state layer over whichever of the three it
 * is, not a second fill.
 *
 * This replaced five roles - three fills, two more for the same fills under the cursor - that
 * every theme had to state and match by eye against each other. The pairs it answers with are
 * the ones inkcell_theme_validate() already holds, selected included.
 */
static struct inkcell_paint inkcell_fb_bubble_paint(const struct inkcell_backend_fb_state *state,
                                                    const struct inkcell_fb_bubble *bubble) {
    const enum inkcell_state ui_state =
        bubble->selected ? INKCELL_STATE_SELECTED : INKCELL_STATE_REST;
    if (bubble->failed) {
        return inkcell_fb_paint(state, INKCELL_FAMILY_ERROR, INKCELL_SLOT_CONTAINER, ui_state);
    }
    if (bubble->outbound) {
        return inkcell_fb_paint(state, INKCELL_FAMILY_SECONDARY, INKCELL_SLOT_CONTAINER, ui_state);
    }
    return (struct inkcell_paint){
        .fill =
            inkcell_fb_state_layer(state, INKCELL_COLOR_SURFACE_HIGH, INKCELL_COLOR_TEXT, ui_state),
        .ink = inkcell_fb_color(state, INKCELL_COLOR_TEXT),
    };
}

/*
 * The colour for a bubble's quieter lines - the sender on one of ours, the clock and the marks
 * beside it on any of them.
 *
 * Dim while the bubble sits at rest, and the bubble's own ink once the cursor is on it or the
 * fill has gone red. The selected fills are a step towards their ink by design, and dim over
 * one of those is the pairing a theme has least room for: on the dark palette it measured
 * 1.9:1, well under the 3:1 a secondary line is held to, and the bubble's own ink is a pair the
 * theme is validated on by construction.
 *
 * The delivery mark takes this ink too rather than a tone of its own. A failed message is
 * already drawn in the error family, so a red tick would be the fill said twice; and "gone out"
 * against "acknowledged" is a difference of one tick, which is the difference every messenger
 * has taught everybody to read. It also keeps the mark inside a pairing the theme is already
 * validated on, instead of asking every palette for a sixth one.
 */
static struct inkcell_rgb inkcell_fb_bubble_quiet(const struct inkcell_backend_fb_state *state,
                                                  const struct inkcell_fb_bubble *bubble,
                                                  struct inkcell_paint paint) {
    if (bubble->selected || bubble->failed) {
        return paint.ink;
    }
    return inkcell_fb_tone_color(state, INKCELL_TONE_DIM);
}

static void inkcell_fb_bubble_part_text(const struct inkcell_backend_fb_state *state,
                                        struct inkcell_fb_bubble_part *parts, size_t *count,
                                        const char *text) {
    if (!inkcell_fb_bubble_has(text) || *count >= INKCELL_FB_BUBBLE_META_PARTS) {
        return;
    }
    parts[*count].text = text;
    parts[*count].icon = INKCELL_ICON_NONE;
    parts[*count].width = (size_t)inkcell_fb_text_width(state, text, state->scale);
    *count += 1U;
}

static void inkcell_fb_bubble_part_icon(const struct inkcell_backend_fb_state *state,
                                        struct inkcell_fb_bubble_part *parts, size_t *count,
                                        enum inkcell_icon icon) {
    if (!inkcell_icon_is_valid(icon) || *count >= INKCELL_FB_BUBBLE_META_PARTS) {
        return;
    }
    parts[*count].text = NULL;
    parts[*count].icon = icon;
    /*
     * The box the symbol is drawn in, not the cell it sits on.
     *
     * A symbol stands as tall as the capitals beside it, and the cell advance is narrower than
     * the glyph body is tall - so an icon is drawn a little wider than a cell and centred on
     * it, which inkcell_fb_icon_box() is the number for. Measured as a cell, the run came out
     * narrower than it draws and the last part of it hung out of the bubble the run was fitted
     * into: a pending clock and a failed mark, each drawn half outside the fill behind it, on
     * every scale and both themes.
     */
    parts[*count].width = (size_t)inkcell_fb_icon_box(state, state->scale);
    *count += 1U;
}

/* The run's width in pixels: every part, plus `gap` pixels of air between each neighbouring
   pair. The caller converts the gap from cells, because a cell is what the spacing is stated
   in and a pixel is what everything here is measured in. */
static size_t inkcell_fb_bubble_run_width(const struct inkcell_fb_bubble_part *parts, size_t count,
                                          int gap) {
    if (count == 0U) {
        return 0U;
    }
    size_t width = (count - 1U) * (size_t)gap;
    for (size_t i = 0; i < count; ++i) {
        width += parts[i].width;
    }
    return width;
}

/*
 * Assemble the trailing run and cut it down to what the bubble can hold.
 *
 * One function because the measure and the draw both need it, and a run assembled one way and
 * painted another is how a transcript comes to overlap itself - or, as it did while the run was
 * a string the screen concatenated, to paint outside the bubble entirely.
 *
 * Parts go in the order they are drawn and come off the *front*, so what is lost first is the
 * reaction chip and what survives longest is the mark saying the message failed. Dropping
 * rather than truncating, because half a clock is not a shorter clock. It bottoms out at
 * nothing, which is the honest answer for a bubble too narrow to say anything in the corner.
 */
static size_t inkcell_fb_bubble_run(const struct inkcell_backend_fb_state *state,
                                    const struct inkcell_fb_bubble_meta *meta, size_t budget,
                                    struct inkcell_fb_bubble_part *parts, size_t *count) {
    *count = 0U;
    inkcell_fb_bubble_part_text(state, parts, count, meta->reactions);
    inkcell_fb_bubble_part_text(state, parts, count, meta->relay);
    inkcell_fb_bubble_part_icon(state, parts, count, meta->lock);
    inkcell_fb_bubble_part_text(state, parts, count, meta->clock);
    inkcell_fb_bubble_part_icon(state, parts, count, meta->state);

    size_t width = inkcell_fb_bubble_run_width(
        parts, *count, (int)INKCELL_FB_BUBBLE_META_GAP * inkcell_fb_char_adv(state, state->scale));
    while (*count > 0U && width > budget) {
        for (size_t i = 1U; i < *count; ++i) {
            parts[i - 1U] = parts[i];
        }
        *count -= 1U;
        width = inkcell_fb_bubble_run_width(parts, *count,
                                            (int)INKCELL_FB_BUBBLE_META_GAP *
                                                inkcell_fb_char_adv(state, state->scale));
    }
    return width;
}

/* The widest and the last of the lines `text` wraps to at `max`, and how many there are. */
static uint32_t inkcell_fb_bubble_wrap(const struct inkcell_backend_fb_state *state,
                                       const char *text, size_t max, size_t *widest, size_t *last) {
    uint32_t lines = 0U;
    struct inkcell_fb_wrap_ctx wctx;
    const struct inkcell_wrap_metric metric = inkcell_fb_wrap_metric(&wctx, state, state->scale);
    struct inkcell_wrap wrap;
    inkcell_wrap_begin_measured(&wrap, text, max, &metric);
    while (inkcell_wrap_next(&wrap)) {
        *last = (size_t)inkcell_fb_text_width(state, wrap.line, state->scale);
        if (*last > *widest) {
            *widest = *last;
        }
        lines += 1U;
    }
    return lines;
}

static struct inkcell_fb_bubble_metrics
inkcell_fb_bubble_measure(const struct inkcell_backend_fb_state *state,
                          const struct inkcell_fb_layout *layout,
                          const struct inkcell_fb_bubble *bubble) {
    const size_t max = inkcell_fb_bubble_max_width(state, layout);
    struct inkcell_fb_bubble_metrics metrics;
    memset(&metrics, 0, sizeof metrics);

    /* One wrap walk per block, and the draw makes the same ones. Anything that measured the
       text a second way - a strlen, a second wrapper - is how a bubble comes to paint over the
       one below it. */
    size_t widest = 0U;
    metrics.lines = inkcell_fb_bubble_wrap(state, bubble->text, max, &widest, &metrics.last);
    if (metrics.lines == 0U) {
        metrics.lines = 1U; /* an empty message is still a bubble, just an empty one */
    }
    /* The reason a failed message failed, under it - so the run tucks onto *its* last line,
       which is the last line the bubble draws. */
    metrics.notes = inkcell_fb_bubble_wrap(state, bubble->note, max, &widest, &metrics.last);

    metrics.width = widest;
    if (inkcell_fb_bubble_has(bubble->name)) {
        const size_t name_cols = (size_t)inkcell_fb_text_width(state, bubble->name, state->scale);
        if (name_cols > metrics.width) {
            metrics.width = name_cols;
        }
    }
    /* The quote is one line whatever it says, so it is elided here rather than wrapped - and
       the width it asks for is what is left of it, never what it started as. */
    /* The indent is stated in cells because it is a *space*, and turned into pixels here
       because everything it is compared against is now measured. */
    const size_t quote_indent =
        (size_t)INKCELL_FB_BUBBLE_QUOTE_INDENT * (size_t)inkcell_fb_char_adv(state, state->scale);
    if (inkcell_fb_bubble_has(bubble->quote) && max > quote_indent) {
        const size_t room = max - quote_indent;
        metrics.quote_width = (size_t)inkcell_fb_text_width(state, bubble->quote, state->scale);
        if (metrics.quote_width > room) {
            metrics.quote_width = room;
        }
        if (metrics.quote_width + quote_indent > metrics.width) {
            metrics.width = metrics.quote_width + quote_indent;
        }
    }

    /*
     * The run rides the last line when there is room for it there, which is what keeps a
     * three-word message three words tall instead of doubling it.
     *
     * `max` is the budget either way, so the run can never be wider than the bubble - and
     * because the bubble is then widened to hold it, the draw's right-aligned run cannot reach
     * past the left padding. That is the invariant the whole component turns on.
     */
    metrics.meta_width =
        inkcell_fb_bubble_run(state, &bubble->meta, max, metrics.parts, &metrics.part_count);
    if (metrics.meta_width > 0U) {
        /* Every term here is a pixel width - `last` and `meta_width` are measured, and the gap
           is a cell converted to one. Adding the gap raw is how the run came to be measured a
           cell short of what it draws. */
        const size_t tucked =
            metrics.last +
            (size_t)INKCELL_FB_BUBBLE_META_GAP * (size_t)inkcell_fb_char_adv(state, state->scale) +
            metrics.meta_width;
        if (tucked <= max) {
            if (tucked > metrics.width) {
                metrics.width = tucked;
            }
        } else {
            metrics.meta_own_line = true;
            if (metrics.meta_width > metrics.width) {
                metrics.width = metrics.meta_width;
            }
        }
    }

    if (metrics.width > max) {
        metrics.width = max;
    }
    if (metrics.width == 0U) {
        metrics.width = 1U;
    }

    metrics.rows = metrics.lines + metrics.notes + (inkcell_fb_bubble_has(bubble->name) ? 1U : 0U) +
                   (metrics.quote_width > 0U ? 1U : 0U) + (metrics.meta_own_line ? 1U : 0U) +
                   (inkcell_fb_bubble_has(bubble->separator) ? 1U : 0U);
    return metrics;
}

uint32_t inkcell_fb_bubble_rows(const struct inkcell_backend_fb_state *state,
                                const struct inkcell_fb_layout *layout,
                                const struct inkcell_fb_bubble *bubble) {
    return inkcell_fb_bubble_measure(state, layout, bubble).rows;
}

void inkcell_fb_draw_separator(const struct inkcell_backend_fb_state *state, int y,
                               const char *label, enum inkcell_tone tone) {
    const int adv = inkcell_fb_char_adv(state, state->scale);
    const int rule_y = y + inkcell_scale_px((int)inkcell_fb_font(state)->height, state->scale) / 2;
    const int left = inkcell_fb_margin(state);
    const int right = inkcell_fb_panel_width(state) - left;

    if (!inkcell_fb_bubble_has(label)) {
        inkcell_fb_draw_rule(state, left, rule_y, right - left, state->scale, INKCELL_COLOR_RULE);
        return;
    }

    /* Centred on the measured width, so a label with an emoji in it - or any word at all on a
       proportional face - sits where it looks centred. */
    const int width = inkcell_fb_text_width(state, label, state->scale);
    const int x = left + (right - left - width) / 2;
    inkcell_fb_draw_rule(state, left, rule_y, x - left - adv, state->scale, INKCELL_COLOR_RULE);
    inkcell_fb_draw_rule(state, x + width + adv, rule_y, right - (x + width + adv), state->scale,
                         INKCELL_COLOR_RULE);
    inkcell_fb_draw_text(state, x, y, label, state->scale, inkcell_fb_tone_color(state, tone),
                         inkcell_fb_color(state, INKCELL_COLOR_BG));
}

/* Paints one block of wrapped text from `y` down, and reports where the next row starts. The
   walk the measure made, so the rows painted are the rows reserved. */
static int inkcell_fb_bubble_draw_wrapped(const struct inkcell_backend_fb_state *state, int x,
                                          int y, const char *text, size_t max, int line_h,
                                          struct inkcell_rgb ink, struct inkcell_rgb fill) {
    struct inkcell_wrap wrap;
    struct inkcell_fb_wrap_ctx wctx;
    const struct inkcell_wrap_metric metric = inkcell_fb_wrap_metric(&wctx, state, state->scale);
    inkcell_wrap_begin_measured(&wrap, text, max, &metric);
    while (inkcell_wrap_next(&wrap)) {
        inkcell_fb_draw_text(state, x, y, wrap.line, state->scale, ink, fill);
        y += line_h;
    }
    return y;
}

void inkcell_fb_draw_bubble(const struct inkcell_backend_fb_state *state,
                            const struct inkcell_fb_layout *layout, int y,
                            const struct inkcell_fb_bubble *bubble) {
    const struct inkcell_fb_bubble_metrics metrics =
        inkcell_fb_bubble_measure(state, layout, bubble);
    const int adv = inkcell_fb_char_adv(state, state->scale);
    const int scale = state->scale;
    const size_t max = inkcell_fb_bubble_max_width(state, layout);

    if (inkcell_fb_bubble_has(bubble->separator)) {
        inkcell_fb_draw_separator(state, y, bubble->separator, bubble->separator_tone);
        y += layout->line;
    }

    /* The box: content plus half a cell of padding each side, against the edge the direction
       names. The fill stops a scale short of the row it ends on, so stacked bubbles read as
       separate messages rather than as one block. */
    const int pad = adv / 2 > 0 ? adv / 2 : 1;
    const int box_w = (int)metrics.width + 2 * pad;
    const int box_x = bubble->outbound
                          ? inkcell_fb_panel_width(state) - inkcell_fb_margin(state) - box_w
                          : inkcell_fb_margin(state);
    const uint32_t box_rows = metrics.rows - (inkcell_fb_bubble_has(bubble->separator) ? 1U : 0U);
    const int box_h = (int)box_rows * layout->line - inkcell_step_px(scale);

    const struct inkcell_paint paint = inkcell_fb_bubble_paint(state, bubble);
    const struct inkcell_rgb fill = paint.fill;
    /*
     * Rounded, because a bubble is the one shape in this UI that everybody already has a
     * picture of: a transcript of square boxes reads as a log, and the same boxes with their
     * corners off read as a conversation. It is the panel shape, so how round belongs to the
     * theme along with everything else about it.
     *
     * The cursor also gets a bar down its outer edge - on a small panel a fill one step lighter
     * is not by itself enough to find, and a colour-blind eye gets nothing from it at all - and
     * it is laid as the card's edge is: the accent shape first, the fill over it a scale
     * narrower on the outer side only, so the bar follows the corner rather than squaring it
     * off. The three edges the two shapes share leave no accent showing on them.
     */
    const int radius = inkcell_fb_radius(state, INKCELL_SHAPE_MD);
    if (bubble->selected) {
        inkcell_fb_fill_round_rect(state, box_x, y - inkcell_step_px(scale), box_w, box_h, radius,
                                   inkcell_fb_color(state, INKCELL_COLOR_PRIMARY));
    }
    const int fill_x =
        (bubble->selected && !bubble->outbound) ? box_x + inkcell_step_px(scale) : box_x;
    const int fill_w = bubble->selected ? box_w - inkcell_step_px(scale) : box_w;
    inkcell_fb_fill_round_rect(state, fill_x, y - inkcell_step_px(scale), fill_w, box_h, radius,
                               fill);

    const int text_x = box_x + pad;
    const struct inkcell_rgb body = paint.ink;
    const struct inkcell_rgb quiet = inkcell_fb_bubble_quiet(state, bubble, paint);

    if (inkcell_fb_bubble_has(bubble->name)) {
        struct inkcell_line line;
        inkcell_line_reset(&line);
        inkcell_line_printf(&line, "%s", bubble->name);
        inkcell_line_fit(&line, adv > 0 ? metrics.width / (size_t)adv : metrics.width);
        /* Ours is dimmed and theirs takes the primary: on our own bubble the name is a
           reminder, on theirs it is the thing being looked for. An alert overrides both - it is
           the one bubble whose heading is the point rather than the label on the point. A
           bubble that failed takes its own ink for all three, because the fill has already said
           the only thing a hue on top of it could add. */
        struct inkcell_rgb name_color = quiet;
        if (!bubble->failed) {
            if (bubble->alert) {
                name_color = inkcell_fb_tone_color(state, INKCELL_TONE_ERROR);
            } else if (!bubble->outbound) {
                name_color = inkcell_fb_tone_color(state, INKCELL_TONE_PRIMARY);
            }
        }
        inkcell_fb_draw_text(state, text_x, y, inkcell_line_text(&line), scale, name_color, fill);
        y += layout->line;
    }

    /*
     * The quote, marked the way every messenger marks one: a bar down its left edge and the
     * words beside it in the quiet ink. The bar rather than a glyph because there is no arrow
     * in either face here worth the cell, and because a rule is what the eye already reads as
     * "this is being cited" - the same job the accent edge does on a list row.
     */
    if (metrics.quote_width > 0U) {
        const int bar_w = inkcell_step_px(scale);
        inkcell_fb_fill_rect(state, text_x, y, bar_w, layout->line - inkcell_step_px(scale),
                             inkcell_fb_tone_color(state, INKCELL_TONE_DIM));
        struct inkcell_line quote;
        inkcell_line_reset(&quote);
        inkcell_line_printf(&quote, "%s", bubble->quote);
        inkcell_line_fit(&quote, metrics.quote_width);
        inkcell_fb_draw_text(state, text_x + (int)INKCELL_FB_BUBBLE_QUOTE_INDENT * adv, y,
                             inkcell_line_text(&quote), scale, quiet, fill);
        y += layout->line;
    }

    int last_y = y;
    y = inkcell_fb_bubble_draw_wrapped(state, text_x, y, bubble->text, max, layout->line, body,
                                       fill);
    if (y == last_y) {
        y += layout->line; /* the measure reserves a row for an empty message; spend it */
    }
    /* The reason under it, in the same ink: the bubble is already the error container, so a
       second red here would be the fill saying the same thing twice. */
    if (metrics.notes > 0U) {
        y = inkcell_fb_bubble_draw_wrapped(state, text_x, y, bubble->note, max, layout->line, body,
                                           fill);
    }
    last_y = y - layout->line;

    if (metrics.part_count == 0U) {
        return;
    }
    /* Right-aligned against the padding, which the measure widened the bubble to leave room
       for - so this can never reach back past `text_x`. */
    int meta_x = box_x + box_w - pad - (int)metrics.meta_width;
    int meta_y = metrics.meta_own_line ? y : last_y;
    if (!metrics.meta_own_line) {
        /* Tucked against the right edge of the line it shares, which is where every messenger
           puts it - and which is why the measure widened the bubble to make room. */
        const int floor_x = text_x + (int)metrics.last + (int)INKCELL_FB_BUBBLE_META_GAP * adv;
        if (meta_x < floor_x) {
            meta_x = floor_x;
        }
    }
    for (size_t i = 0; i < metrics.part_count; ++i) {
        const struct inkcell_fb_bubble_part *part = &metrics.parts[i];
        if (part->text != NULL) {
            inkcell_fb_draw_text(state, meta_x, meta_y, part->text, scale, quiet, fill);
        } else {
            inkcell_fb_draw_icon(state, meta_x, meta_y, part->icon, scale, quiet, fill);
        }
        meta_x += (int)part->width + (int)INKCELL_FB_BUBBLE_META_GAP * adv;
    }
}
