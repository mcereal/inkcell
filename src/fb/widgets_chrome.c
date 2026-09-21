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

/* ---- the frame ------------------------------------------------------------------------------ */

uint32_t inkcell_fb_layout_rows(const struct inkcell_backend_fb_state *state,
                                const struct inkcell_fb_layout *layout) {
    if (layout->line <= 0) {
        return 0U;
    }
    /* The gutter is the body's own bottom margin: rows are counted to where content may stand,
       not to where the footer's fill begins, or the last row would sit against the action bar's
       rule with nothing between them. */
    const int remaining = layout->footer_y - inkcell_fb_gutter(state) - layout->body_y;
    return remaining > 0 ? (uint32_t)(remaining / layout->line) : 0U;
}

struct inkcell_fb_layout inkcell_fb_layout_begin(const struct inkcell_backend_fb_state *state,
                                                 bool footer, bool back) {
    struct inkcell_fb_layout layout = {0};

    /* Chrome's glyph scale, resolved once here so that every bar in the frame agrees - see the
       field's note. Everything below reads it rather than asking the theme again. */
    layout.small = inkcell_fb_type_scale(state, INKCELL_TYPE_LABEL);
    layout.line = inkcell_fb_line_adv(state, state->scale);
    layout.back = back;

    /* The body starts at the edge the whole frame stands off. Nothing has been drawn above it,
       so the first body row and the first row below the navigation bar are the same row; the
       nav bar moves both when it draws. */
    layout.body_y = inkcell_fb_edge(state);
    layout.nav_y = layout.body_y;

    /*
     * The footer's room, taken now rather than when the bar is drawn.
     *
     * The action bar is drawn last, over a body that was laid out long before - so the room has
     * to be missing from the body's count from the start. Reserving it here and drawing the bar
     * at `footer_y` is what keeps a full list's last row off the keycaps.
     */
    layout.footer_y = (int)state->var.yres;
    if (footer) {
        layout.footer_y -= inkcell_fb_action_bar_height(state, &layout);
        if (layout.footer_y < layout.body_y) {
            layout.footer_y = layout.body_y;
        }
    }

    layout.rows = inkcell_fb_layout_rows(state, &layout);
    /* A character count at the body scale, and the pixel width that count is an estimate of.
       Both, because a proportional face makes them different questions - see `body_w`. */
    layout.cols = inkcell_fb_cols(state, state->scale);
    layout.body_w = (int)state->var.xres - 2 * inkcell_fb_margin(state);
    if (layout.body_w < 1) {
        layout.body_w = 1;
    }
    return layout;
}

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
    /* The rows the bar just cost, for the same reason the banner and the app bar recount after
       moving the body: `rows` is a fact about where the body now starts, and a piece of chrome
       that moves the top without recounting leaves a list measuring against a row it no longer
       has. */
    layout->rows = inkcell_fb_layout_rows(state, layout);
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
     * So the count is taken again from the same two numbers inkcell_fb_layout_begin() used,
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
       large glyph scale a flat one ran off the right edge with the verb on it. Centred, because
       the symbol above it is - a centred icon over a left-aligned caption is two decisions about
       one object, and the eye reads the disagreement before it reads the words. */
    (void)inkcell_fb_draw_wrapped_centered(state, y, text, (size_t)layout->body_w, (int)rows,
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

/* ---- the floating action button ---------------------------------------------------------- */

/*
 * The glyph multiplier the symbol is drawn at, and the one the verb beside it is set in.
 *
 * The symbol is a type role rather than a number, so a theme asking for bigger text gets a
 * bigger FAB along with everything else. The verb is one step under it, which is the proportion
 * a FAB has everywhere it exists: the picture is the control and the word is a gloss on it, and
 * a label set as large as the symbol makes the container a button with a big icon in it.
 */
static int inkcell_fb_fab_scale(const struct inkcell_backend_fb_state *state,
                                enum inkcell_fb_fab_size size) {
    switch (size) {
    case INKCELL_FB_FAB_SM:
        return inkcell_fb_type_scale(state, INKCELL_TYPE_BODY);
    case INKCELL_FB_FAB_LG:
        /* The one size that grows its symbol as well as its room, the way the large title is the
           one heading a step above INKCELL_TYPE_TITLE. Unclamped for that same reason: a theme
           already at the top of the range gets a symbol the font registry resamples rather than
           one the type scale flattened back into the body. */
        return inkcell_fb_type_scale(state, INKCELL_TYPE_TITLE) + 1;
    case INKCELL_FB_FAB_MD:
    default:
        return inkcell_fb_type_scale(state, INKCELL_TYPE_TITLE);
    }
}

static int inkcell_fb_fab_label_scale(const struct inkcell_backend_fb_state *state,
                                      enum inkcell_fb_fab_size size) {
    const int scale = inkcell_fb_fab_scale(state, size) - 1;
    return scale < INKCELL_SCALE_MIN ? INKCELL_SCALE_MIN : scale;
}

/*
 * The room around the symbol: one entry of the spacing scale per size.
 *
 * This is the axis the sizes differ on, which is Material's own - a small and a regular FAB
 * carry the same symbol in different amounts of container. Naming a token rather than a count
 * of steps is what lets a theme with a denser layout have a denser FAB without this being a
 * number anywhere.
 */
static int inkcell_fb_fab_pad(const struct inkcell_backend_fb_state *state,
                              enum inkcell_fb_fab_size size, int scale) {
    switch (size) {
    case INKCELL_FB_FAB_SM:
        return inkcell_fb_space_at(state, INKCELL_SPACE_SM, scale);
    case INKCELL_FB_FAB_LG:
        return inkcell_fb_space_at(state, INKCELL_SPACE_LG, scale);
    case INKCELL_FB_FAB_MD:
    default:
        return inkcell_fb_space_at(state, INKCELL_SPACE_MD, scale);
    }
}

/*
 * What the two forms measure, and where they stand.
 *
 * One struct because every answer below needs the same half-dozen numbers and each of them is
 * derived from the one before: a second function re-deriving the diameter is a second diameter.
 * `room` is what the frame can spare for the extended form - the body's own width - and is what
 * decides whether the verb is shown at all.
 */
struct inkcell_fb_fab_metrics {
    int scale;       /* the symbol's glyph multiplier */
    int label_scale; /* the verb's */
    int pad;
    int gap;      /* between the symbol and the verb */
    int label_w;  /* the verb, measured */
    int diameter; /* the collapsed form: square, and therefore a circle */
    int extended; /* the extended form's width, or `diameter` when there is no verb */
    int right;    /* where the trailing edge sits */
    int bottom;   /* and the bottom one */
    int room;     /* the widest the frame can spare */
    bool fits;    /* whether the extended form is one of the widths available */
};

static struct inkcell_fb_fab_metrics
inkcell_fb_fab_measure(const struct inkcell_backend_fb_state *state,
                       const struct inkcell_fb_layout *layout, const struct inkcell_fb_fab *fab) {
    struct inkcell_fb_fab_metrics m = {0};
    m.scale = inkcell_fb_fab_scale(state, fab->size);
    m.label_scale = inkcell_fb_fab_label_scale(state, fab->size);
    m.pad = inkcell_fb_fab_pad(state, fab->size, m.scale);
    /* Half a cell between a symbol and the word after it - inkcell_fb_draw_button()'s gap, at
       the symbol's own scale, because it is a space rather than a word. */
    m.gap = inkcell_fb_char_adv(state, m.scale) / 2;
    m.diameter = inkcell_fb_icon_box(state, m.scale) + 2 * m.pad;

    const bool has_label = fab->label != NULL && fab->label[0] != '\0';
    /* Measured, never counted: a verb is words, and on the proportional face two of the same
       length are not the same width. */
    m.label_w = has_label ? inkcell_fb_text_width(state, fab->label, m.label_scale) : 0;
    const int wanted = has_label ? m.diameter + m.gap + m.label_w : m.diameter;

    const int margin = inkcell_fb_margin(state);
    m.right = (int)state->var.xres - margin;
    /*
     * A full margin clear of the footer rather than the half a card stops at, which is the
     * snackbar's rule and for its reason: a card is *in* the body and belongs against the body's
     * own bottom edge, while a thing floating over everything keeps the distance the panel edge
     * keeps.
     */
    m.bottom = layout->footer_y - margin;
    m.room = m.right - margin;
    /* The verb is shown only while the frame can hold the whole of it. A label that does not
       fit is a FAB without a label, not a FAB running off the panel - the chip strip's elision,
       with one label instead of five. */
    m.fits = has_label && wanted <= m.room;
    /*
     * And a verb that cannot be shown moves the *endpoint*, not merely the target.
     *
     * The animation carries a fraction rather than a width, so the two ends have to be the two
     * ends the FAB is actually travelling between. Handed a longer verb than the frame can
     * hold, a FAB extended at ONE would spend that fraction against a `wanted` several hundred
     * pixels wider - so the first frame of the collapse would be a pill *growing* to fill the
     * body before it shrank, through widths nothing was ever drawn at.
     *
     * There is nothing honest to travel through there: the pill on the panel was holding a
     * different verb, and the new one has no pill. So the FAB is its disc on the frame the verb
     * stops fitting, which is the snackbar's rule for a notice replacing another - a different
     * thing arrives rather than the old one changing its words.
     */
    m.extended = m.fits ? wanted : m.diameter;
    return m;
}

/*
 * The box at a width, which is the one place the anchoring is written down: against the trailing
 * edge and against the bottom, so a FAB that grows a label grows leftwards and stays put.
 *
 * Only a disc the frame cannot hold at all answers with nothing. Every width the caller can
 * reach is between the two endpoints inkcell_fb_fab_measure() settled, and neither of those is
 * wider than the room - which is what that function's note about the endpoint is for.
 */
static struct inkcell_fb_rect inkcell_fb_fab_rect(const struct inkcell_fb_fab_metrics *m,
                                                  const struct inkcell_fb_layout *layout,
                                                  int width) {
    const struct inkcell_fb_rect none = {0, 0, 0, 0};
    if (m->diameter <= 0 || m->diameter > m->room || m->bottom - m->diameter < layout->body_y) {
        return none; /* a frame with nowhere to put one - see inkcell_fb_fab_box() */
    }
    return (struct inkcell_fb_rect){
        .x = m->right - width, .y = m->bottom - m->diameter, .w = width, .h = m->diameter};
}

struct inkcell_fb_rect inkcell_fb_fab_box(const struct inkcell_backend_fb_state *state,
                                          const struct inkcell_fb_layout *layout,
                                          const struct inkcell_fb_fab *fab) {
    const struct inkcell_fb_rect none = {0, 0, 0, 0};
    if (state == NULL || layout == NULL || fab == NULL || !inkcell_icon_is_valid(fab->icon)) {
        return none;
    }
    const struct inkcell_fb_fab_metrics m = inkcell_fb_fab_measure(state, layout, fab);
    /* No test against `fits` here: a verb that does not fit has already left `extended` sitting
       on the diameter, which is the whole point of settling the endpoint rather than the
       target. */
    return inkcell_fb_fab_rect(&m, layout, fab->extended ? m.extended : m.diameter);
}

int inkcell_fb_fab_clearance(const struct inkcell_backend_fb_state *state,
                             const struct inkcell_fb_layout *layout,
                             const struct inkcell_fb_fab *fab) {
    const struct inkcell_fb_rect box = inkcell_fb_fab_box(state, layout, fab);
    /* From the top of the disc to the foot of the body. The width is what moves as a FAB
       collapses and the height is not, so this answer is the same on every frame of one. */
    return box.w > 0 ? layout->footer_y - box.y : 0;
}

struct inkcell_fb_rect inkcell_fb_draw_fab(struct inkcell_backend_fb_state *state,
                                           const struct inkcell_fb_layout *layout,
                                           const struct inkcell_fb_fab *fab) {
    const struct inkcell_fb_rect none = {0, 0, 0, 0};
    if (state == NULL || layout == NULL || fab == NULL || !inkcell_icon_is_valid(fab->icon)) {
        return none;
    }
    const struct inkcell_fb_fab_metrics m = inkcell_fb_fab_measure(state, layout, fab);

    /*
     * How much of the verb is out, 0 collapsed and ONE extended.
     *
     * An entrance is longer than an exit, which is the motion scale's own division of the two
     * and every platform's: a label arriving has something to say and one leaving has said it.
     * An id of 0 is not a key - the table reports the target unanimated - so a FAB with no
     * identity draws correctly at whichever width it was asked for and simply never eases.
     */
    const bool out = fab->extended && m.fits;
    const int32_t shown = inkcell_anim_track(
        &state->anim, fab->id, state->now_ms, out ? INKCELL_ANIM_ONE : 0,
        inkcell_fb_motion(state, out ? INKCELL_MOTION_MEDIUM : INKCELL_MOTION_SHORT),
        INKCELL_EASE_OUT);

    const int width =
        m.diameter + (int)(((int64_t)(m.extended - m.diameter) * shown) / INKCELL_ANIM_ONE);
    const struct inkcell_fb_rect box = inkcell_fb_fab_rect(&m, layout, width);
    if (box.w <= 0) {
        return none;
    }

    /*
     * The pair a tonal button wears, asked for as a pair.
     *
     * Not drawn *as* a button, because the verb has to fade rather than appear - but filled from
     * the same table, so the container under the cursor commits to the family's full strength
     * here exactly as a tonal control does there. See inkcell_fb_button_paint().
     */
    const struct inkcell_fb_button spec = {
        .selected = fab->selected,
        .variant = INKCELL_FB_BUTTON_TONAL,
        .family = fab->family,
    };
    const struct inkcell_fb_button_paint paint = inkcell_fb_button_paint(state, &spec);

    /* Registered before anything is painted and before the clip below, which is the button's
       order and the reason for it: this is the frame saying the box exists rather than saying
       what colour it came out. With its shape, so a ring that lands here is the curve the FAB
       was filled with. */
    inkcell_fb_focus_register_shaped(state, fab->focus_id, &box, INKCELL_SHAPE_FULL);

    /*
     * Where it is moving, said before it draws - the switch's rule, and every other component
     * here that animates.
     *
     * Without it a frame drawn under a clip band rejects the FAB twice over: the band clips its
     * fills, and inkcell_fb_copy_damage() skips its rows on the way to the panel. A list
     * repainting its own rows is exactly such a frame, so a FAB extending beside one would
     * freeze until something unrelated redrew the whole screen.
     *
     * The span is the *widest it could be standing anywhere in*, not the box it came out as,
     * and that is the half a fixed-size control does not have to think about. A FAB is a
     * container whose width moves: the rows it vacates as it collapses have to be redrawn and
     * copied too, and they are not inside the box it is drawing now. There is nowhere to
     * remember last frame's box - a screen may have more than one of these - so the answer is
     * derived rather than stored: it travels along one row band with its trailing edge pinned,
     * so every width it has ever had here is inside the room, and the room is a number the
     * geometry already knows.
     */
    const int pad = inkcell_fb_space_at(state, INKCELL_SPACE_XS, m.scale);
    inkcell_fb_animation_damage(state, m.right - m.room - pad, box.y - pad, m.room + 2 * pad,
                                box.h + 2 * pad);

    inkcell_fb_fill_round_rect(state, box.x, box.y, box.w, box.h,
                               inkcell_fb_radius(state, INKCELL_SHAPE_FULL), paint.paint.fill);

    /*
     * The contents, inside a view cut to the container.
     *
     * A clip rather than a truncation, because the container is between its two widths for the
     * length of the collapse and a verb shortened by a cell at a time would *pop* through the
     * journey the easing exists to smooth. The view is the drawing layer's own answer to "cut
     * this where its window ends", and a FAB collapsing is a window closing on its label.
     *
     * It cuts at the padding rather than at the fill, so the last letter goes before it reaches
     * the capsule's curve - which costs the resting form nothing, since a label that fits ends
     * exactly there anyway, and is the difference between a verb disappearing into the end of
     * the pill and one running into it.
     */
    const struct inkcell_fb_rect inside = {
        .x = box.x + m.pad, .y = box.y, .w = box.w - 2 * m.pad, .h = box.h};
    if (!inkcell_fb_view_push(state, inside, 0, 0)) {
        return box; /* filled and registered; only its contents had nowhere to land */
    }
    /* The symbol and the verb sit on one text line, centred in the box the way a button centres
       its own content - so the symbol walks to the middle of the disc as the verb goes, rather
       than staying put while the container shrinks past it. */
    const int text_h = (int)inkcell_fb_font(state)->height * m.scale;
    const int content_w = m.diameter - 2 * m.pad + (width - m.diameter);
    const int x = box.x + (box.w - content_w) / 2;
    const int y = box.y + (box.h - text_h) / 2;
    inkcell_fb_draw_icon(state, x, y, fab->icon, m.scale, paint.paint.ink, paint.paint.fill);

    if (shown > 0 && m.label_w > 0) {
        /*
         * Faded towards the fill it is written on, by however much of the collapse is done.
         *
         * Towards the *fill* and not towards the ground: a glyph here carries coverage rather
         * than a mask, so ink mixed towards what is actually behind it is a partly faded letter
         * and ink mixed towards anything else is a letter with a halo. See inkcell_fb_fade().
         */
        const struct inkcell_rgb ink =
            inkcell_fb_fade(paint.paint.ink, paint.paint.fill, INKCELL_ANIM_ONE - shown);
        const int label_y =
            box.y + (box.h - (int)inkcell_fb_font(state)->height * m.label_scale) / 2;
        inkcell_fb_draw_text(state, x + inkcell_fb_icon_box(state, m.scale) + m.gap, label_y,
                             fab->label, m.label_scale, ink, paint.paint.fill);
    }
    inkcell_fb_view_pop(state);
    return box;
}
