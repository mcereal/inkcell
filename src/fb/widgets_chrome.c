#define _POSIX_C_SOURCE 200809L

/*
 * The chrome: the navigation bar, the action bar, the app bar and its trail, the empty state
 * under it, the hairline, and the banner and progress bar that drop in below the title.
 *
 * The bars are strips of inkcell_fb_widgets_button.h's shapes given a place on the panel - which is
 * the whole of the split: what a chip looks like is one question, and whether it belongs at the top
 * of the screen or the bottom is another.
 */

#include "inkcell/ui/widgets/button.h"
#include "inkcell/ui/widgets/chrome.h"
#include "inkcell/ui/widgets/meter.h"

#include "inkcell/i18n/strings.h"
#include "inkcell/ui/emoji.h"
#include "inkcell/utils/text.h"

/* ---- the navigation bar --------------------------------------------------------------------- */

void inkcell_fb_draw_nav_bar(const struct inkcell_backend_fb_state *state,
                             struct inkcell_fb_layout *layout, const struct inkcell_fb_chip *tabs,
                             size_t count, size_t active) {
    const int small = layout->small;
    const int y = inkcell_fb_gutter(state) + small;
    const int bar_h = y + inkcell_fb_line_adv(state, small);
    const int width = (int)state->var.xres;

    inkcell_fb_fill_rect(state, 0, 0, width, bar_h,
                         inkcell_fb_color(state, INKCELL_COLOR_SURFACE_LOW));
    /* The bar under them, not the body ground: an unselected tab draws no fill of its own, and
       its icon has to blend into what the bar filled behind it. */
    (void)inkcell_fb_draw_chip_strip(state, inkcell_fb_gutter(state), y, tabs, count, active,
                                     width - inkcell_fb_margin(state), INKCELL_COLOR_SURFACE_LOW,
                                     small);
    inkcell_fb_draw_rule(state, 0, bar_h, width, small, INKCELL_COLOR_RULE_STRONG);

    layout->nav_y = bar_h + inkcell_fb_rule_height(state, small);
    layout->body_y =
        bar_h + inkcell_fb_space_at(state, INKCELL_SPACE_MD, small) + inkcell_fb_gutter(state);
}

/* ---- the screen progress bar ---------------------------------------------------------------- */

/*
 * Its key in the animation table, from the far end of the range where no screen's own id lands
 * - the same reasoning as INKCELL_FB_ANIM_ID_SNACKBAR, and the same neighbourhood.
 */
#define INKCELL_FB_ANIM_ID_PROGRESS 0xFFFFFF03U

/*
 * How tall the bar is.
 *
 * The meter's own thickness at the *body* scale rather than at the chrome scale it sits in.
 * Chrome is drawn smaller because it is text and text has to stay legible in less room; a
 * hairline has no such reason, and one scaled down with the tab labels is a bar nobody notices
 * on the screens it exists for.
 */
static int inkcell_fb_progress_thickness(const struct inkcell_backend_fb_state *state) {
    return inkcell_fb_meter_thickness(state, state->scale);
}

void inkcell_fb_draw_progress(struct inkcell_backend_fb_state *state,
                              const struct inkcell_fb_layout *layout, bool busy) {
    if (state == NULL || layout == NULL || !busy) {
        return;
    }
    const int height = inkcell_fb_progress_thickness(state);
    if (height <= 0 || layout->nav_y <= 0) {
        return;
    }
    /*
     * Full bleed, which is the one place in this UI a container reaches the panel edge.
     *
     * Everything else is inset by the margin because everything else is content. This is not:
     * it is the navigation bar's rule saying something, so it runs the width of the rule it
     * hangs off. Inset, it would read as the first row of the body - which is exactly the
     * mistake the tab strip made before it was given a surface of its own.
     */
    struct inkcell_fb_meter meter = {
        .rect = {.x = 0, .y = layout->nav_y, .w = (int)state->var.xres, .h = height},
        .kind = INKCELL_FB_METER_INDETERMINATE,
        .tone = INKCELL_TONE_PRIMARY,
        .id = INKCELL_FB_ANIM_ID_PROGRESS,
    };
    inkcell_fb_draw_meter(state, &meter);
}

/* ---- the banner ------------------------------------------------------------------------------ */

void inkcell_fb_draw_banner(const struct inkcell_backend_fb_state *state,
                            struct inkcell_fb_layout *layout,
                            const struct inkcell_fb_banner *banner) {
    if (state == NULL || layout == NULL || banner == NULL || banner->text == NULL ||
        banner->text[0] == '\0') {
        return;
    }

    const int scale = state->scale;
    const int small = layout->small;
    const int adv = inkcell_fb_char_adv(state, scale);
    const int small_adv = inkcell_fb_char_adv(state, small);
    const int margin = inkcell_fb_margin(state);
    const int pad_x = inkcell_fb_space(state, INKCELL_SPACE_MD);
    const int pad_y = inkcell_fb_space(state, INKCELL_SPACE_SM);
    const int top = layout->body_y;
    const int width = (int)state->var.xres - 2 * margin;
    if (width <= 2 * pad_x || adv <= 0 || layout->line <= 0) {
        return;
    }

    /*
     * Measured in glyph *bodies* rather than in line advances, which is the same correction the
     * app bar's overline made: an advance carries the gap between two lines of running text,
     * and neither of these lines is running text. Spending it here would have cost a body row
     * for space nobody sees.
     */
    const int font_h = (int)inkcell_fb_font(state)->height;
    const int head_top = top + pad_y;
    const int head_h = font_h * scale;
    const int sup_gap = inkcell_fb_space_at(state, INKCELL_SPACE_XS, small);
    const int sup_h = font_h * small;
    const int gap = inkcell_fb_space(state, INKCELL_SPACE_SM);

    /*
     * Whether the second line is affordable.
     *
     * A banner is chrome that eats content, so the rule is that it may not eat all of it: when
     * taking the supporting line would leave the screen under it with no row at all, the
     * supporting line is what goes. The headline is the half that says what is true; the hint
     * is the half that says where to go about it, and a hint over an empty screen is worse than
     * no hint.
     */
    bool supporting = banner->supporting != NULL && banner->supporting[0] != '\0';
    int height = 2 * pad_y + head_h + (supporting ? sup_gap + sup_h : 0);
    if (supporting &&
        layout->footer_y - inkcell_fb_gutter(state) - (top + height + gap) < layout->line) {
        supporting = false;
        height = 2 * pad_y + head_h;
    }

    const struct inkcell_paint paint =
        inkcell_fb_paint(state, banner->family, INKCELL_SLOT_CONTAINER, INKCELL_STATE_REST);
    inkcell_fb_fill_round_rect(state, margin, top, width, height,
                               inkcell_fb_radius(state, INKCELL_SHAPE_MD), paint.fill);

    int x = margin + pad_x;
    int right = margin + width - pad_x;

    if (banner->icon != INKCELL_ICON_NONE) {
        inkcell_fb_draw_icon(state, x, head_top, banner->icon, scale, paint.ink, paint.fill);
        x += inkcell_fb_icon_box(state, scale) + adv / 2;
    }

    /*
     * The detail, against the trailing edge and at the label scale.
     *
     * It recedes by *size* rather than by colour, and that is the type scale doing the job a
     * second ink would otherwise have been invented for: the container has one validated pair,
     * and a dimmed variant of its ink is a contract no theme has been held to.
     */
    if (banner->detail != NULL && banner->detail[0] != '\0') {
        const int detail_w = inkcell_fb_text_width(state, banner->detail, layout->small);
        if (detail_w > 0 && right - detail_w > x) {
            right -= detail_w;
            /* Centred on the headline's glyph body rather than sharing its top edge: a smaller
               face hung from the same line reads as having slipped up off it. */
            inkcell_fb_draw_text(state, right, head_top + (head_h - sup_h) / 2, banner->detail,
                                 small, paint.ink, paint.fill);
            right -= small_adv;
        }
    }

    struct inkcell_line headline;
    inkcell_line_reset(&headline);
    inkcell_line_printf(&headline, "%s", banner->text);
    inkcell_line_fit(&headline, (size_t)(right > x ? (right - x) / adv : 0));
    inkcell_fb_draw_text(state, x, head_top, inkcell_line_text(&headline), scale, paint.ink,
                         paint.fill);

    if (supporting) {
        const int room = margin + width - pad_x - x;
        struct inkcell_line hint;
        inkcell_line_reset(&hint);
        inkcell_line_printf(&hint, "%s", banner->supporting);
        inkcell_line_fit(&hint, (size_t)(room > 0 ? room / small_adv : 0));
        /* Indented to the headline's own left edge, past the icon: the symbol leads the whole
           banner rather than only its first line, so a hint starting under it would read as a
           second, unmarked notice. */
        inkcell_fb_draw_text(state, x, head_top + head_h + sup_gap, inkcell_line_text(&hint), small,
                             paint.ink, paint.fill);
    }

    /* What is left of the body, recomputed from its real bottom rather than deducted - see the
       tail of inkcell_fb_draw_app_bar() for why a deduction is wrong here. */
    layout->body_y = top + height + gap;
    if (layout->line > 0) {
        const int remaining = layout->footer_y - inkcell_fb_gutter(state) - layout->body_y;
        layout->rows = remaining > 0 ? (uint32_t)(remaining / layout->line) : 0U;
    }
}

/* ---- the action bar ------------------------------------------------------------------------- */

/* The cap's pill and the verb after it, with the half cell between them that every icon-plus-
   label pair in this file uses. Measured rather than assumed, because the pill's padding is
   the button's business (see INKCELL_FB_CHIP_PAD_STEPS). */
static int inkcell_fb_action_width(const struct inkcell_backend_fb_state *state,
                                   const struct inkcell_button_action *action, int scale) {
    const int adv = inkcell_fb_char_adv(state, scale);
    const char *label = inkcell_str(action->label);
    return inkcell_fb_button_width(state, INKCELL_ICON_NONE, inkcell_button_cap(action->button),
                                   scale) +
           adv / 2 + inkcell_fb_text_width(state, label, scale);
}

int inkcell_fb_action_bar_height(const struct inkcell_backend_fb_state *state,
                                 const struct inkcell_fb_layout *layout) {
    const int small = layout->small;
    /* The keycap row, the status line under it, and a margin below - the same margin the two
       plain lines this replaced left, so the bar sits off the panel edge by the amount the rest
       of the frame does rather than by an amount of its own. */
    return inkcell_fb_space_at(state, INKCELL_SPACE_SM, small) + inkcell_fb_line_adv(state, small) +
           inkcell_fb_space_at(state, INKCELL_SPACE_XS, small) + inkcell_fb_line_adv(state, small) +
           inkcell_fb_margin(state);
}

void inkcell_fb_draw_action_bar(const struct inkcell_backend_fb_state *state,
                                const struct inkcell_fb_layout *layout,
                                const struct inkcell_fb_action_bar *bar) {
    const int small = layout->small;
    const int width = (int)state->var.xres;
    const int top = layout->footer_y;

    /* The mirror of the navigation bar: the same recessed tier, the same rule, on the other
       edge. Chrome that is a surface at the top and bare ground at the bottom reads as a frame
       with one side missing. */
    inkcell_fb_fill_rect(state, 0, top, width, (int)state->var.yres - top,
                         inkcell_fb_color(state, INKCELL_COLOR_SURFACE_LOW));
    inkcell_fb_draw_rule(state, 0, top, width, small, INKCELL_COLOR_RULE_STRONG);

    const int keys_y = top + inkcell_fb_space_at(state, INKCELL_SPACE_SM, small) + small;
    const int gap = inkcell_fb_space_at(state, INKCELL_SPACE_MD, small);
    const int right = width - inkcell_fb_gutter(state);
    int x = inkcell_fb_gutter(state);

    for (size_t i = 0; i < bar->count; ++i) {
        const struct inkcell_button_action *action = &bar->items[i];
        const char *cap = inkcell_button_cap(action->button);
        const int cap_w = inkcell_fb_button_width(state, INKCELL_ICON_NONE, cap, small);
        /* Dropped from the end rather than clipped: half a verb is a button whose meaning has
           to be guessed, and the tables are written with the least important action last. */
        if (x + inkcell_fb_action_width(state, action, small) > right) {
            break;
        }

        const struct inkcell_fb_button key = {
            .rect = {.x = x,
                     .y = keys_y - small,
                     .w = cap_w,
                     .h = inkcell_fb_line_adv(state, small)},
            .label = cap,
            .variant = INKCELL_FB_BUTTON_FILLED,
            .shape = INKCELL_SHAPE_SM,
            .ground = INKCELL_COLOR_SURFACE_LOW,
            .scale = small,
        };
        inkcell_fb_draw_button(state, &key);

        x += cap_w + inkcell_fb_char_adv(state, small) / 2;
        inkcell_fb_draw_text(state, x, keys_y, inkcell_str(action->label), small,
                             inkcell_fb_tone_color(state, INKCELL_TONE_DIM),
                             inkcell_fb_color(state, INKCELL_COLOR_SURFACE_LOW));
        x += inkcell_fb_text_width(state, inkcell_str(action->label), small) + gap;
    }

    if (bar->status == NULL || bar->status[0] == '\0') {
        return;
    }
    /* Sized to the line builder that produced it, not to the toast that used to share this row:
       a status line is a transport state and a radio's advertised name, and a name is only
       bounded by what the radio says it is called. */
    char status[INKCELL_LINE_MAX];
    inkcell_str_copy(status, sizeof status, bar->status);
    inkcell_fb_fit(status, inkcell_fb_cols(state, small));
    inkcell_fb_draw_text(state, inkcell_fb_margin(state),
                         keys_y - small + inkcell_fb_line_adv(state, small) +
                             inkcell_fb_space_at(state, INKCELL_SPACE_XS, small),
                         status, small, inkcell_fb_tone_color(state, bar->status_tone),
                         inkcell_fb_color(state, INKCELL_COLOR_SURFACE_LOW));
}

/* ---- the top app bar ------------------------------------------------------------------------ */

/* The trail, drawn left to right with a chevron between the levels, from `x`. Returns nothing:
   a trail that runs out of room stops, because the level nearest the title is the one worth
   keeping and it is drawn last. */
static void inkcell_fb_draw_app_bar_trail(const struct inkcell_backend_fb_state *state,
                                          const struct inkcell_fb_app_bar *bar, int x, int y,
                                          int right, int scale) {
    const int adv = inkcell_fb_char_adv(state, scale);
    const struct inkcell_rgb ink = inkcell_fb_tone_color(state, INKCELL_TONE_DIM);
    const struct inkcell_rgb ground = inkcell_fb_color(state, INKCELL_COLOR_BG);
    const int step = inkcell_fb_icon_box(state, scale);

    struct inkcell_line word;
    for (size_t i = 0; i < bar->trail_count; ++i) {
        const char *text = bar->trail[i];
        if (text == NULL || text[0] == '\0') {
            continue;
        }
        if (i > 0U) {
            /* The separator is an icon, not a character. The breadcrumb this replaced spelled
               it "> " inside the translated title, which handed a translator the trail's
               grammar along with its words; a chevron drawn from the icon set is the same mark
               the rows that open something already use, and it is untranslated for the same
               reason an arrow on a keycap is. */
            if (x + step + adv / 2 > right) {
                return;
            }
            /* A quarter of a cell either side. The chevron sprite fills its cell, so a
               separator advanced by the bare icon box has the two level names touching it and
               the trail reads as one word. */
            inkcell_fb_draw_icon(state, x + adv / 4, y, INKCELL_ICON_CHEVRON, scale, ink, ground);
            x += step + adv / 2;
        }
        const int room = (right - x) / adv;
        if (room <= 0) {
            return;
        }
        inkcell_line_reset(&word);
        inkcell_line_printf(&word, "%s", text);
        inkcell_line_fit(&word, (size_t)room);
        inkcell_fb_draw_text(state, x, y, inkcell_line_text(&word), scale, ink, ground);
        x += inkcell_fb_text_width(state, inkcell_line_text(&word), scale);
    }
}

int inkcell_fb_app_bar_height(const struct inkcell_backend_fb_state *state,
                              const struct inkcell_fb_layout *layout, size_t trail_count) {
    /*
     * The same three terms inkcell_fb_draw_app_bar() advances `body_y` by, in the same order.
     *
     * Written out rather than shared with the drawing path because sharing it would mean the
     * draw calling this and then re-deriving `y` from it, which is the arithmetic in a different
     * arrangement rather than in one place. Two expressions for one height is a real risk and
     * the test below the fold is what holds them together: inkcell_fb_map's body is measured from
     * this and drawn under a bar laid out by that, so any disagreement puts the map's ground a few
     * pixels off its own heading, where it is visible in a capture.
     */
    int height = 0;
    if (trail_count > 0U) {
        height += (int)inkcell_fb_font(state)->height * layout->small +
                  inkcell_fb_space_at(state, INKCELL_SPACE_XS, layout->small);
    }
    height += inkcell_fb_line_adv(state, inkcell_fb_type_scale(state, INKCELL_TYPE_TITLE));
    height += inkcell_fb_space(state, INKCELL_SPACE_SM);
    return height;
}

void inkcell_fb_draw_app_bar(const struct inkcell_backend_fb_state *state,
                             struct inkcell_fb_layout *layout,
                             const struct inkcell_fb_app_bar *bar) {
    /*
     * A title is drawn at INKCELL_TYPE_TITLE, which is a step above the body.
     *
     * It used to be drawn at the body scale and told apart from the rows beneath it by
     * INKCELL_TONE_PRIMARY alone - a heading exactly the size of its own content, with colour
     * carrying the whole of the hierarchy. The tone stays; it is now saying the same thing the
     * size already said, which is what a heading is supposed to look like.
     */
    const int scale = inkcell_fb_type_scale(state, INKCELL_TYPE_TITLE);
    const int small = layout->small;
    const int adv = inkcell_fb_char_adv(state, scale);
    const int margin = inkcell_fb_margin(state);
    const struct inkcell_rgb ground = inkcell_fb_color(state, INKCELL_COLOR_BG);

    int y = layout->body_y;
    /* The content column: the trail and the title share a left edge, and the back arrow hangs
       in the gutter to the left of both - which is where every platform puts it, and what keeps
       the two lines reading as one block rather than as two things that happen to be stacked. */
    const int text_x = layout->back ? margin + inkcell_fb_icon_box(state, scale) + adv / 2 : margin;

    /*
     * The overline, when this screen is somewhere rather than at a tab's root.
     *
     * It costs a label-scale line and it buys the whole of the breadcrumb's width back: at the
     * title scale "Settings > Modules > Telemetry" is thirty cells of a thirty-four cell line,
     * so the leaf - the one word saying which screen this is - was the half that got elided.
     * Above the title, at the label scale, the same trail is half as wide and the title has the
     * panel to itself.
     */
    if (bar->trail_count > 0U) {
        inkcell_fb_draw_app_bar_trail(state, bar, text_x, y, (int)state->var.xres - margin, small);
        /* The glyph body and a hair, not the label scale's whole line advance. The advance
           carries the gap between two lines of running text, and the trail is not running text
           - it is a caption sitting on the title. Spending the advance here cost a body row on
           every screen with a trail, which is a row of content for a gap nobody sees. */
        y += (int)inkcell_fb_font(state)->height * small +
             inkcell_fb_space_at(state, INKCELL_SPACE_XS, small);
    }

    /*
     * The leading affordance: what B does, said by the chrome rather than only by the keycap
     * at the bottom of the panel. layout->back is the action bar's own answer (see
     * inkcell_action_bar_goes_back), so the arrow and the keycap cannot disagree.
     */
    if (layout->back) {
        inkcell_fb_draw_icon(state, margin, y, INKCELL_ICON_BACK, scale,
                             inkcell_fb_tone_color(state, INKCELL_TONE_DIM), ground);
    }

    /*
     * The trailing slot: a fact about the *screen*, which is the one thing a title could not
     * carry. "3 unsaved" used to be a " (unsaved)" glued onto the end of the title with a %s,
     * where it was neither countable nor a badge - a capsule cannot be spelled inside a
     * sentence.
     */
    int right = (int)state->var.xres - margin;
    const int badge_w = inkcell_fb_badge_width(state, bar->badge, small);
    if (badge_w > 0) {
        /* Centred on the title's glyph body rather than on its line advance: the advance
           carries the gap accents hang in, and counting it would sit the capsule low. */
        const int title_h = (int)inkcell_fb_font(state)->height * scale;
        const int badge_h = (int)inkcell_fb_font(state)->height * small;
        const int text_y = y + (title_h - badge_h) / 2;
        const struct inkcell_fb_rect box = {.x = right - badge_w,
                                            .y = text_y - small,
                                            .w = badge_w,
                                            .h = inkcell_fb_line_adv(state, small) - small};
        inkcell_fb_draw_badge(state, &box, text_y, bar->badge, bar->badge_family, small);
        right -= badge_w + inkcell_fb_char_adv(state, small);
    }

    struct inkcell_line line;
    inkcell_line_reset(&line);
    inkcell_line_printf(&line, "%s", bar->title != NULL ? bar->title : "");
    /* Fitted to the room *this* scale leaves between the two slots, not to the body's column
       count. Bigger glyphs mean fewer of them, and a title measured against a column count it
       is not drawn at is a title that runs off the panel. */
    const int room = right > text_x ? (right - text_x) / adv : 0;
    inkcell_line_fit(&line, (size_t)(room > 0 ? room : 0));
    inkcell_fb_draw_text_weight(state, text_x, y, inkcell_line_text(&line), scale,
                                inkcell_fb_type_weight(state, INKCELL_TYPE_TITLE),
                                inkcell_fb_tone_color(state, INKCELL_TONE_PRIMARY), ground);

    /*
     * What is left of the body, recomputed rather than deducted.
     *
     * This used to advance by the body's line advance and deduct exactly one row, which was
     * already slightly out - the advance included a gap the deduction did not - and would be
     * properly wrong now that the line being advanced past is taller than a body row.
     *
     * Deducting a rounded-up row count is not the fix either, and that is the subtle part:
     * `rows` is a *floored* division of the body height, so the body carries a remainder of up
     * to one row that the count never included. Subtracting ceil(advance / line) from it
     * charges the bar for that remainder a second time and hides a row that does in fact
     * fit - on the Brick's panel the remainder is most of a row, so every titled screen lost
     * one for nothing.
     *
     * So the count is taken again from the same two numbers inkcell_fb_render_snapshot() used,
     * against the body's real bottom. Measuring it the same way twice is what keeps the two answers
     * from disagreeing.
     */
    layout->body_y =
        y + inkcell_fb_line_adv(state, scale) + inkcell_fb_space(state, INKCELL_SPACE_SM);
    if (layout->line > 0) {
        const int remaining = layout->footer_y - inkcell_fb_gutter(state) - layout->body_y;
        layout->rows = remaining > 0 ? (uint32_t)(remaining / layout->line) : 0U;
    }
}

void inkcell_fb_draw_empty(const struct inkcell_backend_fb_state *state,
                           const struct inkcell_fb_layout *layout, enum inkcell_icon icon,
                           const char *text) {
    int y = layout->body_y;
    uint32_t rows = layout->rows;

    /*
     * The icon is drawn at three glyph scales - a cell is one line tall, so three of them is
     * three body rows and about a fifth of the panel - and it is only drawn when the screen has
     * the rows to spare. An empty state is the one place with room for it, and the one place
     * where a symbol says "nothing here yet" faster than the sentence under it does.
     */
    const int big = state->scale * 3;
    const uint32_t cost = 4U; /* three rows for the symbol, one of air under it */
    if (inkcell_icon_is_valid(icon) && big <= INKCELL_FB_ICON_SCALE_MAX && rows > cost + 1U) {
        const int box = inkcell_fb_icon_box(state, big);
        inkcell_fb_draw_icon(state, ((int)state->var.xres - box) / 2, y, icon, big,
                             inkcell_fb_tone_color(state, INKCELL_TONE_DIM),
                             inkcell_fb_color(state, INKCELL_COLOR_BG));
        y += (int)cost * layout->line;
        rows -= cost;
    }

    /* Wrapped rather than drawn flat: these strings say which button to press next, and at a
       large glyph scale a flat one ran off the right edge with the verb on it. */
    (void)inkcell_fb_draw_wrapped(state, y, text, (size_t)layout->body_w, (int)rows,
                                  inkcell_fb_tone_color(state, INKCELL_TONE_DIM),
                                  inkcell_fb_color(state, INKCELL_COLOR_BG));
}

int inkcell_fb_rule_height(const struct inkcell_backend_fb_state *state, int scale) {
    return inkcell_fb_space_at(state, INKCELL_SPACE_XS, scale);
}

void inkcell_fb_draw_rule(const struct inkcell_backend_fb_state *state, int x, int y, int w,
                          int scale, enum inkcell_color role) {
    inkcell_fb_fill_rect(state, x, y, w, inkcell_fb_rule_height(state, scale),
                         inkcell_fb_color(state, role));
}

void inkcell_fb_title_count(char *out, size_t out_len, const char *name, uint32_t count,
                            uint32_t dropped) {
    if (dropped > 0U) {
        inkcell_str_format(out, out_len, INKCELL_STR_LIST_TITLE_COUNT_OLDER, name, count, dropped);
    } else {
        inkcell_str_format(out, out_len, INKCELL_STR_LIST_TITLE_COUNT, name, count);
    }
}
