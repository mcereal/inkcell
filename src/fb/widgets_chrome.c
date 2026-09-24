#define _POSIX_C_SOURCE 200809L

/*
 * The chrome: the navigation bar, the action bar, the app bar and its trail, the empty state
 * under it, the hairline, and the banner and progress bar that drop in below the title.
 *
 * The bars are strips of inkcell/ui/widgets/button.h's shapes given a place on the panel - which is
 * the whole of the split: what a chip looks like is one question, and whether it belongs at the top
 * of the screen or the bottom is another.
 */

#include "inkcell/ui/widgets/button.h"
#include "inkcell/ui/widgets/chrome.h"
#include "inkcell/ui/widgets/meter.h"
#include "inkcell/ui/widgets/scroll.h"

#include "inkcell/i18n/strings.h"
#include "inkcell/ui/emoji.h"
#include "inkwell/base/text.h"

/* ---- the frame ------------------------------------------------------------------------------ */

uint32_t inkcell_fb_layout_rows(const struct inkcell_draw_state *state,
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

struct inkcell_fb_layout inkcell_fb_layout_begin(const struct inkcell_draw_state *state,
                                                 bool footer, bool back) {
    return inkcell_fb_layout_begin_footer(
        state, footer ? INKCELL_FB_FOOTER_FULL : INKCELL_FB_FOOTER_NONE, back);
}

struct inkcell_fb_layout inkcell_fb_layout_begin_footer(const struct inkcell_draw_state *state,
                                                        enum inkcell_fb_footer footer, bool back) {
    struct inkcell_fb_layout layout = {0};
    /* First, because the height below reads it: a compact bar is one row and a full one two. */
    layout.footer = footer;

    /* Chrome's glyph scale, resolved once here so that every bar in the frame agrees - see the
       field's note. Everything below reads it rather than asking the theme again. */
    layout.small = inkcell_fb_type_scale(state, INKCELL_TYPE_LABEL);
    layout.line = inkcell_fb_line_adv(state, state->scale);
    layout.back = back;

    /* The body starts at the edge the whole frame stands off. Nothing has been drawn above it,
       so the first body row and the first row below the navigation bar are the same row; the
       nav bar moves both when it draws. */
    const struct inkcell_box region = inkcell_fb_region(state);
    layout.body_y = region.y + inkcell_fb_edge(state);
    layout.nav_y = layout.body_y;

    /*
     * The footer's room, taken now rather than when the bar is drawn.
     *
     * The action bar is drawn last, over a body that was laid out long before - so the room has
     * to be missing from the body's count from the start. Reserving it here and drawing the bar
     * at `footer_y` is what keeps a full list's last row off the keycaps.
     */
    layout.footer_y = region.y + region.h;
    if (footer != INKCELL_FB_FOOTER_NONE) {
        layout.footer_y -= inkcell_fb_action_bar_height(state, &layout);
        if (layout.footer_y < layout.body_y) {
            layout.footer_y = layout.body_y;
        }
    }

    layout.rows = inkcell_fb_layout_rows(state, &layout);
    /* A character count at the body scale, and the pixel width that count is an estimate of.
       Both, because a proportional face makes them different questions - see `body_w`. */
    /*
     * The class first, and from the *panel's* count - which is what inkcell_fb_cols() answers,
     * and the one place that count is the right one. The column is a function of the class, so
     * a class taken from the column would be a circle.
     */
    layout.width = inkcell_width_class_of(inkcell_fb_cols(state, state->scale));

    const struct inkcell_box column = inkcell_fb_content_column(state);
    layout.body_x = column.x;
    layout.body_w = column.w;
    /*
     * And `cols` from the column, because `cols` is a fact about the *body* - it is what a
     * screen fitting a label column or an empty state counts against. Panel-based, it would
     * promise a screen more characters than the body it is drawn in can hold, and the screens
     * that trust it would run their labels out past the column's trailing edge.
     */
    const int adv = inkcell_fb_char_adv(state, state->scale);
    layout.cols = adv > 0 ? (size_t)(layout.body_w / adv) : 1U;
    if (layout.cols == 0U) {
        layout.cols = 1U;
    }
    return layout;
}

struct inkcell_box inkcell_fb_body_box(const struct inkcell_draw_state *state,
                                       const struct inkcell_fb_layout *layout) {
    if (state == NULL || layout == NULL) {
        return (struct inkcell_box){0};
    }
    struct inkcell_box box = {
        .x = layout->body_x,
        .y = layout->body_y,
        .w = layout->body_w,
        .h = layout->footer_y - layout->body_y,
    };
    /* A body with nothing left in it is empty rather than negative: every caller tests
       inkcell_box_is_empty(), and none of them should have to test for backwards as well. */
    if (box.h < 0) {
        box.h = 0;
    }
    return box;
}

struct inkcell_box inkcell_fb_full_box(const struct inkcell_draw_state *state,
                                       const struct inkcell_fb_layout *layout) {
    struct inkcell_box box = inkcell_fb_body_box(state, layout);
    const struct inkcell_box region = inkcell_fb_region(state);
    box.x = region.x + inkcell_fb_margin(state);
    box.w = region.w - 2 * inkcell_fb_margin(state);
    if (box.w < 1) {
        box.w = 1;
    }
    return box;
}

/* ---- the navigation bar --------------------------------------------------------------------- */

int inkcell_fb_nav_bar_height(const struct inkcell_draw_state *state, int small) {
    return inkcell_fb_gutter(state) + inkcell_scale_px(1, small) +
           inkcell_fb_line_adv(state, small);
}

void inkcell_fb_draw_nav_bar(const struct inkcell_draw_state *state,
                             struct inkcell_fb_layout *layout, const struct inkcell_fb_chip *tabs,
                             size_t count, size_t active) {
    const int small = layout->small;
    /* A step's worth of air over the tabs, at the chrome scale. One *step*, not one scale: the
       two were the same number while a scale was a whole multiplier, and adding the scale itself
       to a pixel gutter was the same arithmetic by accident. */
    const struct inkcell_box region = inkcell_fb_region(state);
    const int y = region.y + inkcell_fb_gutter(state) + inkcell_scale_px(1, small);
    const int bar_h = region.y + inkcell_fb_nav_bar_height(state, small);
    const int width = region.w;
    /* Whatever of the host's inset the gutter does not already cover, taken off the strip's
       room as well as its start - so the strip fits its labels to what is actually left, and a
       frame with no inset is the frame it always was. */
    const int gutter = inkcell_fb_gutter(state);
    const int shift = state->top_leading_inset > gutter ? state->top_leading_inset - gutter : 0;
    /*
     * The strip's own edges, which are the content column's less the half-gutter a chip's fill
     * stands outside its text - the same relationship a list row has to the column, and for
     * the same reason: a tab and a row are both content, and content that started in two
     * different places would read as two frames.
     *
     * The tier behind them is not: it is a fill, so it runs the width of the surface.
     */
    const int strip_x = inkcell_fb_content_x(state) - gutter;
    const int strip_w = inkcell_fb_content_w(state) + gutter;

    inkcell_fb_fill_rect(state, region.x, region.y, width, bar_h - region.y,
                         inkcell_fb_color(state, INKCELL_COLOR_SURFACE_LOW));
    /* The bar under them, not the body ground: an unselected tab draws no fill of its own, and
       its icon has to blend into what the bar filled behind it. */
    (void)inkcell_fb_draw_chip_strip(state, strip_x + shift, y, tabs, count, active,
                                     strip_x + strip_w - shift, INKCELL_COLOR_SURFACE_LOW, small);
    inkcell_fb_draw_rule(state, region.x, bar_h, width, small, INKCELL_COLOR_RULE_STRONG);

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
 * - the same reasoning as INKCELL_FB_OVERLAY_ID_SNACKBAR, and the same neighbourhood.
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
static int inkcell_fb_progress_thickness(const struct inkcell_draw_state *state) {
    return inkcell_fb_meter_thickness(state, state->scale);
}

void inkcell_fb_draw_progress(struct inkcell_draw_state *state,
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
        .rect = {.x = inkcell_fb_region(state).x,
                 .y = layout->nav_y,
                 .w = inkcell_fb_region(state).w,
                 .h = height},
        .kind = INKCELL_FB_METER_INDETERMINATE,
        .tone = INKCELL_TONE_PRIMARY,
        .id = INKCELL_FB_ANIM_ID_PROGRESS,
    };
    inkcell_fb_draw_meter(state, &meter);
}

/* ---- the banner ------------------------------------------------------------------------------ */

void inkcell_fb_draw_banner(const struct inkcell_draw_state *state,
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
    const int margin = inkcell_fb_content_x(state);
    const int pad_x = inkcell_fb_space(state, INKCELL_SPACE_MD);
    const int pad_y = inkcell_fb_space(state, INKCELL_SPACE_SM);
    const int top = layout->body_y;
    const int width = inkcell_fb_content_w(state);
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
    const int head_h = inkcell_scale_px(font_h, scale);
    const int sup_gap = inkcell_fb_space_at(state, INKCELL_SPACE_XS, small);
    const int sup_h = inkcell_scale_px(font_h, small);
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

/*
 * Whether a pointer draws this action as its verb alone, in a button of its own.
 *
 * A face button's letter is only worth printing to somebody holding the case it is printed on.
 * With a pointer the verb is the thing pressed, so it becomes the button. A pair keeps its cap:
 * the arrows and "L/R" are split down the middle into their two keys, and the verb alone
 * would not say which half is which.
 */
static bool inkcell_fb_action_is_verb(const struct inkcell_draw_state *state,
                                      enum inkcell_button button) {
    if (!state->pointer) {
        return false;
    }
    switch (button) {
    case INKCELL_BUTTON_A:
    case INKCELL_BUTTON_B:
    case INKCELL_BUTTON_X:
    case INKCELL_BUTTON_Y:
    case INKCELL_BUTTON_START:
    case INKCELL_BUTTON_SELECT:
        return true;
    default:
        return false;
    }
}

/*
 * Whether a pointer has this press somewhere better than the bar: the arrows are the wheel,
 * the way out is the window's close box, and a B that leaves is the app bar's arrow. Left out
 * rather than drawn, so the room goes to the verbs nothing else on the frame offers.
 *
 * The arrow is asked of the focus map rather than of `layout->back`, because `back` says B
 * leaves and not that an app bar was drawn to show it: a screen with no heading - the map, the
 * keyboard - would otherwise lose its only way out.
 */
static bool inkcell_fb_action_elsewhere(const struct inkcell_draw_state *state,
                                        const struct inkcell_fb_layout *layout,
                                        enum inkcell_button button) {
    if (!state->pointer) {
        return false;
    }
    if (button == INKCELL_BUTTON_UP_DOWN || button == INKCELL_BUTTON_QUIT) {
        return true;
    }
    struct inkcell_focus_rect arrow;
    return button == INKCELL_BUTTON_B && layout->back && state->focus != NULL &&
           inkcell_focus_rect_of(state->focus, INKCELL_FOCUS_KEY(INKCELL_KEY_B), &arrow);
}

/* The cap's pill and the verb after it, with the half cell between them that every icon-plus-
   label pair in this file uses. Measured rather than assumed, because the pill's padding is
   the button's business (see INKCELL_FB_CHIP_PAD_STEPS). */
static int inkcell_fb_action_width(const struct inkcell_draw_state *state,
                                   const struct inkcell_button_action *action, int scale) {
    const int adv = inkcell_fb_char_adv(state, scale);
    const char *label = inkcell_str(action->label);
    if (inkcell_fb_action_is_verb(state, action->button)) {
        return inkcell_fb_button_width(state, INKCELL_ICON_NONE, label, scale);
    }
    return inkcell_fb_button_width(state, INKCELL_ICON_NONE, inkcell_button_cap(action->button),
                                   scale) +
           adv / 2 + inkcell_fb_text_width(state, label, scale);
}

int inkcell_fb_action_bar_height(const struct inkcell_draw_state *state,
                                 const struct inkcell_fb_layout *layout) {
    const int small = layout->small;
    const int keys =
        inkcell_fb_space_at(state, INKCELL_SPACE_SM, small) + inkcell_fb_line_adv(state, small);
    /* A compact bar is the keycap row alone. Only a layout that *asked* for one gets it: every
       other layout - including one opened with no footer at all, which is how a caller asks the
       height of the bar it is about to reserve - gets the full bar it always did. */
    if (layout->footer == INKCELL_FB_FOOTER_COMPACT) {
        return keys + inkcell_fb_margin(state);
    }
    /* The keycap row, the status line under it, and a margin below - the same margin the two
       plain lines this replaced left, so the bar sits off the panel edge by the amount the rest
       of the frame does rather than by an amount of its own. */
    return keys + inkcell_fb_space_at(state, INKCELL_SPACE_XS, small) +
           inkcell_fb_line_adv(state, small) + inkcell_fb_margin(state);
}

/*
 * The hint, cap and verb together, as the key it names: clicking "A Open" is pressing A.
 *
 * Targets rather than focusable boxes, so a pointer can press them and the d-pad never walks
 * off a list into the footer. A pair - "L/R", the arrows - is split down the middle of the
 * whole hint, left half the first key and right half the second, which is how the cap reads.
 * Registered only for what was drawn: a hint the bar dropped for want of room is not clickable,
 * for the same reason it is not shown.
 */
static void inkcell_fb_action_targets(const struct inkcell_draw_state *state,
                                      enum inkcell_button button, int x, int y, int w, int h) {
    enum inkcell_key keys[2];
    const size_t count = inkcell_button_keys(button, keys);
    for (size_t k = 0U; k < count; ++k) {
        const int left = x + (int)((int64_t)w * (int64_t)k / (int64_t)count);
        const int right = x + (int)((int64_t)w * (int64_t)(k + 1U) / (int64_t)count);
        const struct inkcell_fb_rect rect = {.x = left, .y = y, .w = right - left, .h = h};
        inkcell_fb_target_register(state, INKCELL_FOCUS_ACTION_KEY(keys[k]), &rect);
    }
}

/*
 * One hint, drawn from `x` on the keycap row: a verb-only button to a pointer, a cap and its verb
 * to everyone else. Returns the width it took, so the loop that places a row of them is the
 * same arithmetic as the one that measured them.
 *
 * `emphasized` is the screen's primary press, and it changes the *fill* and nothing else - a
 * tonal pill in the primary family where the others are the neutral keycap - so an emphasized
 * cap measures exactly what a plain one does and the fit above it never has to ask.
 */
static int inkcell_fb_draw_action_hint(const struct inkcell_draw_state *state,
                                       const struct inkcell_button_action *action, int x,
                                       int keys_y, int small, bool emphasized) {
    const enum inkcell_fb_button_variant variant =
        emphasized ? INKCELL_FB_BUTTON_TONAL : INKCELL_FB_BUTTON_FILLED;
    const int w = inkcell_fb_action_width(state, action, small);
    if (inkcell_fb_action_is_verb(state, action->button)) {
        const struct inkcell_fb_button verb = {
            .rect = {.x = x,
                     .y = keys_y - inkcell_step_px(small),
                     .w = w,
                     .h = inkcell_fb_line_adv(state, small)},
            .label = inkcell_str(action->label),
            .variant = variant,
            .shape = INKCELL_SHAPE_SM,
            .ground = INKCELL_COLOR_SURFACE_LOW,
            .scale = small,
        };
        inkcell_fb_draw_button(state, &verb);
        inkcell_fb_action_targets(state, action->button, x, verb.rect.y, w, verb.rect.h);
        return w;
    }

    const char *cap = inkcell_button_cap(action->button);
    const struct inkcell_fb_button key = {
        .rect = {.x = x,
                 .y = keys_y - inkcell_step_px(small),
                 .w = inkcell_fb_button_width(state, INKCELL_ICON_NONE, cap, small),
                 .h = inkcell_fb_line_adv(state, small)},
        .label = cap,
        .variant = variant,
        .shape = INKCELL_SHAPE_SM,
        .ground = INKCELL_COLOR_SURFACE_LOW,
        .scale = small,
    };
    inkcell_fb_draw_button(state, &key);

    const int text_x = x + key.rect.w + inkcell_fb_char_adv(state, small) / 2;
    inkcell_fb_draw_text(state, text_x, keys_y, inkcell_str(action->label), small,
                         inkcell_fb_tone_color(state, INKCELL_TONE_DIM),
                         inkcell_fb_color(state, INKCELL_COLOR_SURFACE_LOW));
    inkcell_fb_action_targets(state, action->button, x, key.rect.y, w, key.rect.h);
    return w;
}

/*
 * How many of `items[0..end)` fit in `room`, counting each hint's trailing gap - the index of the
 * first that does not. A hint a pointer has somewhere better is free: it is not drawn, so it
 * costs nothing and it is not "dropped" either.
 */
static size_t inkcell_fb_action_fit(const struct inkcell_draw_state *state,
                                    const struct inkcell_fb_layout *layout,
                                    const struct inkcell_fb_action_bar *bar, size_t end, int room,
                                    int gap) {
    int used = 0;
    for (size_t i = 0; i < end; ++i) {
        const struct inkcell_button_action *action = &bar->items[i];
        if (inkcell_fb_action_elsewhere(state, layout, action->button)) {
            continue;
        }
        const int w = inkcell_fb_action_width(state, action, layout->small);
        /* Dropped from the end rather than clipped: half a verb is a button whose meaning has
           to be guessed, and the tables are written with the least important action last. */
        if (used + w > room) {
            return i;
        }
        used += w + gap;
    }
    return end;
}

size_t inkcell_fb_draw_action_bar(const struct inkcell_draw_state *state,
                                  const struct inkcell_fb_layout *layout,
                                  const struct inkcell_fb_action_bar *bar) {
    const int small = layout->small;
    const struct inkcell_box region = inkcell_fb_region(state);
    const int width = region.w;
    const int top = layout->footer_y;
    const bool compact = layout->footer == INKCELL_FB_FOOTER_COMPACT;

    /* The mirror of the navigation bar: the same recessed tier, the same rule, on the other
       edge. Chrome that is a surface at the top and bare ground at the bottom reads as a frame
       with one side missing. */
    inkcell_fb_fill_rect(state, region.x, top, width, region.y + region.h - top,
                         inkcell_fb_color(state, INKCELL_COLOR_SURFACE_LOW));
    inkcell_fb_draw_rule(state, region.x, top, width, small, INKCELL_COLOR_RULE_STRONG);

    const int keys_y =
        top + inkcell_fb_space_at(state, INKCELL_SPACE_SM, small) + inkcell_step_px(small);
    const int gap = inkcell_fb_space_at(state, INKCELL_SPACE_MD, small);
    /* In the column, like the tab strip at the other end and for its reason - the tier behind
       them is the fill and runs edge to edge. */
    const int gutter = inkcell_fb_gutter(state);
    const int right = inkcell_fb_content_x(state) + inkcell_fb_content_w(state) + gutter;
    int x = inkcell_fb_content_x(state) - gutter;

    /*
     * Which of the items go on the bar: the first `limit` of them, less whatever will not fit.
     *
     * The `more` cap is reserved *before* the items are fitted rather than after, whenever
     * anything is behind it - the limit held some back, or the room would. Fitted after, it
     * would be the one hint dropped for want of room, and the bar would have hidden the only
     * way to the presses it could not show.
     */
    size_t end = bar->count;
    if (bar->limit > 0U && bar->limit < end) {
        end = bar->limit;
    }
    int room = right - x;
    size_t shown = inkcell_fb_action_fit(state, layout, bar, end, room, gap);
    const bool more = bar->more != NULL && (end < bar->count || shown < end);
    if (more) {
        room -= inkcell_fb_action_width(state, bar->more, small) + gap;
        shown = inkcell_fb_action_fit(state, layout, bar, end, room, gap);
    }

    for (size_t i = 0; i < shown; ++i) {
        const struct inkcell_button_action *action = &bar->items[i];
        if (inkcell_fb_action_elsewhere(state, layout, action->button)) {
            continue;
        }
        x += inkcell_fb_draw_action_hint(state, action, x, keys_y, small,
                                         bar->emphasize_first && i == 0U) +
             gap;
    }
    if (more && x + inkcell_fb_action_width(state, bar->more, small) <= right) {
        x += inkcell_fb_draw_action_hint(state, bar->more, x, keys_y, small, false) + gap;
    }

    if (bar->status == NULL || bar->status[0] == '\0') {
        return shown;
    }
    /* Sized to the line builder that produced it, not to the toast that used to share this row:
       a status line is a transport state and a radio's advertised name, and a name is only
       bounded by what the radio says it is called. */
    char status[INKCELL_LINE_MAX];
    inkwell_str_copy(status, sizeof status, bar->status);

    if (compact) {
        /*
         * A compact bar has no second line, so the status rides the end of the keycap row -
         * against the trailing edge, where a reader looks for a fact rather than for a press -
         * in whatever room the keycaps left. It is fitted by *measure* into that room and
         * dropped entirely when the room will not hold a few cells of it: a status cut to one
         * letter says nothing, and the app bar's indicator is where a compact frame carries
         * the fact anyway.
         */
        const int trailing = inkcell_fb_content_x(state) + inkcell_fb_content_w(state);
        const int avail = trailing - x;
        if (avail < 4 * inkcell_fb_char_adv(state, small)) {
            return shown;
        }
        while (inkcell_fb_text_width(state, status, small) > avail) {
            const size_t cells = inkcell_text_cells(status);
            if (cells <= 1U) {
                return shown;
            }
            inkcell_text_cell_truncate(status, cells - 1U);
        }
        inkcell_fb_draw_text(state, trailing - inkcell_fb_text_width(state, status, small), keys_y,
                             status, small, inkcell_fb_tone_color(state, bar->status_tone),
                             inkcell_fb_color(state, INKCELL_COLOR_SURFACE_LOW));
        return shown;
    }

    inkcell_fb_fit(status, inkcell_fb_row_cols(state, small));
    inkcell_fb_draw_text(state, inkcell_fb_content_x(state),
                         keys_y - inkcell_step_px(small) + inkcell_fb_line_adv(state, small) +
                             inkcell_fb_space_at(state, INKCELL_SPACE_XS, small),
                         status, small, inkcell_fb_tone_color(state, bar->status_tone),
                         inkcell_fb_color(state, INKCELL_COLOR_SURFACE_LOW));
    return shown;
}

size_t inkcell_fb_action_bar_menu(const struct inkcell_fb_action_bar *bar, size_t from,
                                  struct inkcell_fb_menu_item *items, size_t max) {
    if (bar == NULL || items == NULL || bar->items == NULL) {
        return 0U;
    }
    size_t out = 0U;
    for (size_t i = from; i < bar->count && out < max; ++i) {
        items[out++] = (struct inkcell_fb_menu_item){.label = inkcell_str(bar->items[i].label)};
    }
    return out;
}

/* ---- the first-use tip ---------------------------------------------------------------------- */

/* How far through its journey the tip is: 0 behind the bar, ONE resting where it is read.
   Rising takes the medium motion and sinking the short one - an entrance longer than an exit,
   the motion scale's own division and the FAB's. */
static int32_t inkcell_fb_action_tip_shown(const struct inkcell_draw_state *state,
                                           const struct inkcell_fb_action_tip *tip, bool *live) {
    *live = false;
    if (tip == NULL || tip->text == NULL || tip->text[0] == '\0') {
        return 0;
    }
    const uint64_t rise = inkcell_fb_motion(state, INKCELL_MOTION_MEDIUM);
    const uint64_t fall = inkcell_fb_motion(state, INKCELL_MOTION_SHORT);
    /* A stamp in the future is a tip that has not started yet, rather than one whose journey is
       a negative number of milliseconds along. */
    const uint64_t t = state->now_ms > tip->since_ms ? state->now_ms - tip->since_ms : 0U;
    if (t >= rise + INKCELL_FB_ACTION_TIP_HOLD_MS + fall) {
        return 0;
    }
    *live = true;
    if (t < rise) {
        return inkcell_ease(INKCELL_EASE_OUT, rise > 0U ? (int32_t)((t * INKCELL_ANIM_ONE) / rise)
                                                        : INKCELL_ANIM_ONE);
    }
    if (t < rise + INKCELL_FB_ACTION_TIP_HOLD_MS) {
        return INKCELL_ANIM_ONE;
    }
    const uint64_t out = t - rise - INKCELL_FB_ACTION_TIP_HOLD_MS;
    return INKCELL_ANIM_ONE -
           inkcell_ease(INKCELL_EASE_IN_OUT,
                        fall > 0U ? (int32_t)((out * INKCELL_ANIM_ONE) / fall) : INKCELL_ANIM_ONE);
}

bool inkcell_fb_action_tip_live(const struct inkcell_draw_state *state,
                                const struct inkcell_fb_action_tip *tip) {
    bool live = false;
    if (state != NULL) {
        (void)inkcell_fb_action_tip_shown(state, tip, &live);
    }
    return live;
}

void inkcell_fb_draw_action_tip(struct inkcell_draw_state *state,
                                const struct inkcell_fb_layout *layout,
                                const struct inkcell_fb_action_tip *tip) {
    if (state == NULL || layout == NULL) {
        return;
    }
    bool live = false;
    const int32_t shown = inkcell_fb_action_tip_shown(state, tip, &live);
    if (!live) {
        return;
    }

    /* At the chrome's scale, because it is the bar talking: a tip set larger than the keycaps it
       is about reads as a notice about the screen, which is the snackbar's job. */
    const int small = layout->small;
    const int adv = inkcell_fb_char_adv(state, small);
    const int pad_x = inkcell_fb_space_at(state, INKCELL_SPACE_MD, small);
    const int pad_y = inkcell_fb_space_at(state, INKCELL_SPACE_XS, small);
    const int line = inkcell_fb_line_adv(state, small);
    const int height = line + 2 * pad_y;
    const int left = inkcell_fb_content_x(state);
    const int room = inkcell_fb_content_w(state) - 2 * pad_x;

    const char *cap = tip->action != NULL ? inkcell_button_cap(tip->action->button) : NULL;
    const int cap_w =
        cap != NULL ? inkcell_fb_button_width(state, INKCELL_ICON_NONE, cap, small) : 0;
    const int lead = cap_w > 0 ? cap_w + adv / 2 : 0;
    if (room - lead < adv) {
        return; /* a panel with no room for a word of it */
    }

    /* Measured into the room, cut from the end: a tip is a sentence, and its head is the half
       that says which press it is about. */
    char text[INKCELL_LINE_MAX];
    inkwell_str_copy(text, sizeof text, tip->text);
    while (inkcell_fb_text_width(state, text, small) > room - lead) {
        const size_t cells = inkcell_text_cells(text);
        if (cells <= 1U) {
            break;
        }
        inkcell_text_cell_truncate(text, cells - 1U);
    }
    const int width = 2 * pad_x + lead + inkcell_fb_text_width(state, text, small);

    /*
     * Where it stands: a small gap above the bar's rule at rest, entirely behind it at zero.
     *
     * The journey is only as long as the tip is tall plus that gap, so it reads as coming *out
     * of* the bar rather than dropping in from somewhere above - the same distance a keycap
     * would travel if it stood up.
     */
    const int clearance = inkcell_fb_space_at(state, INKCELL_SPACE_SM, small);
    const int rest = layout->footer_y - clearance - height;
    const int y =
        layout->footer_y + (int)(((int64_t)(rest - layout->footer_y) * shown) / INKCELL_ANIM_ONE);
    const struct inkcell_fb_rect box = {.x = left, .y = y, .w = width, .h = height};

    /* The whole journey, shadow and all, so a frame drawn under a clip band copies the rows the
       tip is leaving as well as the ones it is arriving in. See inkcell_fb_draw_fab(). */
    const struct inkcell_fb_rect span = {left, rest, width, layout->footer_y - rest + height};
    const struct inkcell_fb_rect reach =
        inkcell_fb_shadow_bounds(state, span, INKCELL_ELEVATION_FLOATING);
    inkcell_fb_animation_damage(state, reach.x, reach.y, reach.w, reach.h);

    /* Cut at the bar's top edge, so the half still behind it is behind it whichever of the two
       was drawn first. */
    const struct inkcell_box region = inkcell_fb_region(state);
    const struct inkcell_fb_rect body = {region.x, layout->nav_y, region.w,
                                         layout->footer_y - layout->nav_y};
    if (!inkcell_fb_view_push(state, body, 0, 0)) {
        return;
    }
    const int radius = inkcell_fb_radius(state, INKCELL_SHAPE_MD);
    /* The snackbar's surface and for its reason: the far end of the palette from the ground, so
       the fill is the whole cue and it floats without needing an edge. */
    const struct inkcell_rgb fill = inkcell_fb_color(state, INKCELL_COLOR_SURFACE_INVERSE);
    inkcell_fb_draw_shadow(state, box, radius, INKCELL_ELEVATION_FLOATING, INKCELL_ANIM_ONE);
    inkcell_fb_fill_round_rect(state, box.x, box.y, box.w, box.h, radius, fill);

    const int text_y = y + pad_y + inkcell_step_px(small);
    int x = left + pad_x;
    if (cap_w > 0) {
        /* The cap as the bar draws the screen's primary press - a tonal pill - because on the
           inverse surface the neutral keycap is the one fill that does not read as a key. */
        const struct inkcell_fb_button key = {
            .rect = {.x = x, .y = y + pad_y, .w = cap_w, .h = line},
            .label = cap,
            .variant = INKCELL_FB_BUTTON_TONAL,
            .shape = INKCELL_SHAPE_SM,
            .scale = small,
        };
        inkcell_fb_draw_button(state, &key);
        x += lead;
    }
    inkcell_fb_draw_text(state, x, text_y, text, small,
                         inkcell_fb_color(state, INKCELL_COLOR_TEXT_ON_INVERSE), fill);
    inkcell_fb_view_pop(state);
}

/* ---- the top app bar ------------------------------------------------------------------------ */

/* The trail, drawn left to right with a chevron between the levels, from `x`. Returns nothing:
   a trail that runs out of room stops, because the level nearest the title is the one worth
   keeping and it is drawn last. */
static void inkcell_fb_draw_app_bar_trail(const struct inkcell_draw_state *state,
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

int inkcell_fb_app_bar_height(const struct inkcell_draw_state *state,
                              const struct inkcell_fb_layout *layout, size_t trail_count) {
    /*
     * The same three terms inkcell_fb_draw_app_bar() advances `body_y` by, in the same order.
     *
     * Written out rather than shared with the drawing path because sharing it would mean the
     * draw calling this and then re-deriving `y` from it, which is the arithmetic in a different
     * arrangement rather than in one place. Two expressions for one height is a real risk and
     * the test below the fold is what holds them together: a screen that places things at
     * coordinates measures its body from this and draws under a bar laid out by that, so any
     * disagreement puts its ground a few pixels off its own heading, where a capture shows it.
     */
    int height = 0;
    if (trail_count > 0U) {
        height += inkcell_scale_px((int)inkcell_fb_font(state)->height, layout->small) +
                  inkcell_fb_space_at(state, INKCELL_SPACE_XS, layout->small);
    }
    height += inkcell_fb_line_adv(state, inkcell_fb_type_scale(state, INKCELL_TYPE_TITLE));
    height += inkcell_fb_space(state, INKCELL_SPACE_SM);
    return height;
}

/* ---- the app bar's trailing cluster ------------------------------------------------------ */

/*
 * How much of the title the actions may never take, in cells of the title's own face.
 *
 * The actions are fitted into what the heading leaves, not the other way round - a bar whose
 * title had been squeezed to its first letter to make room for a share button would have lost
 * the one thing it is for. Six cells is a short word whole or a long one recognisably begun,
 * and a title shorter than that keeps all of itself.
 */
#define INKCELL_FB_APP_BAR_TITLE_KEEP_CELLS 6

/* The search field's floor, in cells of the body face, for the same reason: a field narrower
   than this is a field that cannot show what is being typed into it. */
#define INKCELL_FB_APP_BAR_FIELD_KEEP_CELLS 8

/*
 * The row the trailing cluster is laid out against.
 *
 * Every piece of it centres on the title's glyph body rather than sharing its top edge -
 * `center` is that line - and the cluster's own buttons are one square each, a hair taller than
 * the glyph body so that a focused one's fill has a margin round its symbol. What they are drawn
 * in depends on the mode, so the ink and the ground travel with the geometry.
 */
struct inkcell_fb_app_bar_row {
    int center;
    int scale; /* the title's multiplier, which a plain action's symbol is drawn at */
    int small; /* the chrome's, which the pill's words and the status are set in */
    int side;  /* an icon button's square */
    int gap;
    struct inkcell_rgb ink;    /* a plain symbol's ink */
    struct inkcell_rgb ground; /* what the row is filled with */
    bool tinted;               /* the row is a container rather than the ground: selection */
};

static struct inkcell_fb_app_bar_row inkcell_fb_app_bar_row(const struct inkcell_draw_state *state,
                                                            const struct inkcell_fb_layout *layout,
                                                            int center, struct inkcell_rgb ink,
                                                            struct inkcell_rgb ground,
                                                            bool tinted) {
    struct inkcell_fb_app_bar_row row = {
        .center = center,
        .scale = inkcell_fb_type_scale(state, INKCELL_TYPE_TITLE),
        .small = layout->small,
        .ink = ink,
        .ground = ground,
        .tinted = tinted,
    };
    row.side = inkcell_fb_icon_box(state, row.scale) +
               2 * inkcell_fb_space_at(state, INKCELL_SPACE_XS, row.scale);
    row.gap = inkcell_fb_space_at(state, INKCELL_SPACE_XS, row.small);
    return row;
}

static size_t inkcell_fb_app_bar_count(const struct inkcell_fb_app_bar *bar) {
    if (bar->actions == NULL) {
        return 0U;
    }
    return bar->action_count < INKCELL_FB_APP_BAR_ACTIONS_MAX ? bar->action_count
                                                              : INKCELL_FB_APP_BAR_ACTIONS_MAX;
}

/* An action is anything with words; one with a symbol as well can stand on the bar. */
static bool inkcell_fb_app_bar_action_real(const struct inkcell_fb_bar_action *action) {
    return action->label != NULL && action->label[0] != '\0';
}

static bool inkcell_fb_app_bar_action_on_bar(const struct inkcell_fb_bar_action *action) {
    return inkcell_fb_app_bar_action_real(action) && inkcell_icon_is_valid(action->icon);
}

/* The emphasized action's index, or the count when there is none: the first that asks, and only
   if it can stand on the bar at all - a menu-only verb has no pill to be. */
static size_t inkcell_fb_app_bar_emphasized(const struct inkcell_fb_app_bar *bar, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (bar->actions[i].emphasized && inkcell_fb_app_bar_action_on_bar(&bar->actions[i])) {
            return i;
        }
    }
    return count;
}

static int inkcell_fb_app_bar_pill_w(const struct inkcell_draw_state *state,
                                     const struct inkcell_fb_app_bar_row *row,
                                     const struct inkcell_fb_bar_action *action) {
    return inkcell_fb_button_width(state, action->icon, action->label, row->small);
}

/* The status mark: its symbol, and its word when `text` is asked for and there is one. */
static int inkcell_fb_app_bar_status_w(const struct inkcell_draw_state *state,
                                       const struct inkcell_fb_app_bar_row *row,
                                       const struct inkcell_fb_bar_status *status, bool text) {
    if (!inkcell_icon_is_valid(status->icon)) {
        return 0;
    }
    int w = inkcell_fb_icon_box(state, row->small);
    if (text && status->text != NULL && status->text[0] != '\0') {
        w += inkcell_fb_char_adv(state, row->small) / 2 +
             inkcell_fb_text_width(state, status->text, row->small);
    }
    return w;
}

/* Which pieces of the cluster are drawn, and at what width. */
struct inkcell_fb_app_bar_plan {
    uint32_t shown;
    size_t emphasized;
    bool emphasized_label;
    bool overflow;
    bool status_icon;
    bool status_text;
    size_t hidden;
    int width; /* every piece with its trailing gap */
};

/*
 * One attempt at fitting the cluster into `budget`, with or without room kept for the overflow
 * button.
 *
 * In priority order, which is not left-to-right order: the overflow button first, because it is
 * the way to everything else; the emphasized verb, with its words if it can and as a disc if it
 * cannot; the status symbol; the plain verbs from the front of the array until one does not
 * fit; and the status word last of all, because it is the one thing here that is a gloss on
 * something already shown.
 */
static struct inkcell_fb_app_bar_plan inkcell_fb_app_bar_plan_at(
    const struct inkcell_draw_state *state, const struct inkcell_fb_app_bar *bar,
    const struct inkcell_fb_app_bar_row *row, int budget, int slack, bool overflow) {
    const size_t count = inkcell_fb_app_bar_count(bar);
    struct inkcell_fb_app_bar_plan plan = {
        .emphasized = inkcell_fb_app_bar_emphasized(bar, count),
        .overflow = overflow,
    };
    int used = overflow ? row->side + row->gap : 0;

    if (plan.emphasized < count) {
        const int full =
            inkcell_fb_app_bar_pill_w(state, row, &bar->actions[plan.emphasized]) + row->gap;
        if (used + full <= budget) {
            plan.shown |= 1U << plan.emphasized;
            plan.emphasized_label = true;
            used += full;
        } else if (used + row->side + row->gap <= budget) {
            plan.shown |= 1U << plan.emphasized;
            used += row->side + row->gap;
        }
    }

    const int symbol = inkcell_fb_app_bar_status_w(state, row, &bar->status, false);
    /* The symbol keeps a wider gap after it than the buttons keep between themselves: it is
       not one of them, and a status word run up against a pill reads as the pill's label. */
    const int status_gap = inkcell_fb_char_adv(state, row->small);
    if (symbol > 0 && used + symbol + status_gap <= budget) {
        plan.status_icon = true;
        used += symbol + status_gap;
    }

    /* From the front, and stopping at the first that does not fit rather than skipping it for a
       later one: every plain verb is the same width, so a later one would not fit either - and
       if it did, the bar would be showing a less important verb than one it had hidden. */
    bool full = false;
    for (size_t i = 0; i < count; ++i) {
        if (i == plan.emphasized || !inkcell_fb_app_bar_action_on_bar(&bar->actions[i])) {
            continue;
        }
        if (!full && used + row->side + row->gap <= budget) {
            plan.shown |= 1U << i;
            used += row->side + row->gap;
        } else {
            full = true;
        }
    }

    /* The word only when the title is whole beside it as well: it is a gloss on a symbol that
       is already there, and a heading cut short to make room for a gloss has its priorities the
       wrong way round. `slack` is what the title still needs past its floor. */
    if (plan.status_icon) {
        const int extra = inkcell_fb_app_bar_status_w(state, row, &bar->status, true) - symbol;
        if (extra > 0 && used + extra + slack <= budget) {
            plan.status_text = true;
            used += extra;
        }
    }

    for (size_t i = 0; i < count; ++i) {
        if (inkcell_fb_app_bar_action_real(&bar->actions[i]) && (plan.shown & (1U << i)) == 0U) {
            ++plan.hidden;
        }
    }
    plan.width = used;
    return plan;
}

/*
 * The cluster's plan: without an overflow button if everything fits, with one if anything has
 * to go behind it.
 *
 * Two attempts rather than one because the button costs room, so whether it is needed changes
 * what fits: a bar one icon short of holding everything would otherwise draw the dots *and*
 * hide an icon to make room for them.
 */
static struct inkcell_fb_app_bar_plan
inkcell_fb_app_bar_plan(const struct inkcell_draw_state *state,
                        const struct inkcell_fb_app_bar *bar,
                        const struct inkcell_fb_app_bar_row *row, int budget, int slack) {
    const size_t count = inkcell_fb_app_bar_count(bar);
    bool menu_only = false;
    for (size_t i = 0; i < count; ++i) {
        const struct inkcell_fb_bar_action *action = &bar->actions[i];
        menu_only = menu_only || (inkcell_fb_app_bar_action_real(action) &&
                                  !inkcell_fb_app_bar_action_on_bar(action));
    }
    struct inkcell_fb_app_bar_plan plan =
        inkcell_fb_app_bar_plan_at(state, bar, row, budget, slack, menu_only);
    if (!plan.overflow && plan.hidden > 0U) {
        plan = inkcell_fb_app_bar_plan_at(state, bar, row, budget, slack, true);
    }
    /* A bar too narrow for even the dots has a menu nothing can open. It is reported hidden all
       the same, so a screen can tell, rather than drawn as a button that would overrun the
       title. */
    if (plan.overflow && row->side + row->gap > budget) {
        plan.overflow = false;
        plan.width = 0;
    }
    return plan;
}

/*
 * A bare symbol that can be pressed: nothing at rest, the cursor's fill under it when focused -
 * INKCELL_FB_BUTTON_TEXT's two states, drawn here rather than through the button because on a
 * selection bar the symbol's ink is the container's, which is not a tone a button can be told.
 */
static void inkcell_fb_app_bar_icon_button(const struct inkcell_draw_state *state,
                                           const struct inkcell_fb_app_bar_row *row,
                                           struct inkcell_fb_rect rect, enum inkcell_icon icon,
                                           bool focused, bool disabled, uint32_t focus_id) {
    const uint32_t id = disabled ? INKCELL_FOCUS_NONE : focus_id;
    inkcell_fb_focus_register_shaped(state, id, &rect, INKCELL_SHAPE_FULL);
    struct inkcell_rgb ink =
        disabled ? inkcell_fb_fade(row->ink, row->ground, INKCELL_ANIM_ONE / 2) : row->ink;
    struct inkcell_rgb ground = row->ground;
    if (focused && id != INKCELL_FOCUS_NONE) {
        inkcell_fb_focus_mark(state, id);
        ground = inkcell_fb_color(state, INKCELL_COLOR_SURFACE_ACTIVE);
        ink = inkcell_fb_color(state, INKCELL_COLOR_TEXT_ON_SEL);
        inkcell_fb_fill_round_rect(state, rect.x, rect.y, rect.w, rect.h,
                                   inkcell_fb_radius(state, INKCELL_SHAPE_FULL), ground);
    }
    const int box = inkcell_fb_icon_box(state, row->scale);
    const int glyph = inkcell_scale_px((int)inkcell_fb_font(state)->height, row->scale);
    inkcell_fb_draw_icon(state, rect.x + (rect.w - box) / 2, row->center - glyph / 2, icon,
                         row->scale, ink, ground);
}

/*
 * Draws the cluster against `right`, trailing edge in, and returns the x its leading edge came
 * out at - `right` itself when there was nothing to draw, so a bar with no actions, no status and
 * no menu lays its badge and title out exactly as it always did.
 */
static int inkcell_fb_app_bar_draw_cluster(const struct inkcell_draw_state *state,
                                           const struct inkcell_fb_app_bar *bar,
                                           const struct inkcell_fb_app_bar_row *row,
                                           const struct inkcell_fb_app_bar_plan *plan, int right,
                                           struct inkcell_fb_app_bar_fit *fit) {
    const size_t count = inkcell_fb_app_bar_count(bar);
    const int top = row->center - row->side / 2;
    int x = right;

    fit->shown = plan->shown;
    fit->hidden = plan->hidden;

    if (plan->overflow) {
        const struct inkcell_fb_rect box = {x - row->side, top, row->side, row->side};
        inkcell_fb_app_bar_icon_button(state, row, box, INKCELL_ICON_MORE, bar->overflow_focused,
                                       false, bar->overflow_focus_id);
        fit->overflow = box;
        x -= row->side + row->gap;
    }

    /* Right to left over the array, so the verbs read left to right in the order the screen
       listed them and the overflow button is the last thing on the bar. */
    for (size_t i = count; i-- > 0U;) {
        if ((plan->shown & (1U << i)) == 0U) {
            continue;
        }
        const struct inkcell_fb_bar_action *action = &bar->actions[i];
        if (i != plan->emphasized) {
            const struct inkcell_fb_rect box = {x - row->side, top, row->side, row->side};
            inkcell_fb_app_bar_icon_button(state, row, box, action->icon, action->focused,
                                           action->disabled, action->focus_id);
            x -= row->side + row->gap;
            continue;
        }
        /* The emphasized verb: a tonal pill with its words, or a tonal disc when the words did
           not fit - still the one filled shape on the bar, so still findable as "the" action. */
        const int w =
            plan->emphasized_label ? inkcell_fb_app_bar_pill_w(state, row, action) : row->side;
        const struct inkcell_fb_button pill = {
            .rect = {.x = x - w, .y = top, .w = w, .h = row->side},
            .icon = action->icon,
            .label = plan->emphasized_label ? action->label : NULL,
            .focused = action->focused && !action->disabled,
            .variant = action->disabled ? INKCELL_FB_BUTTON_TEXT : INKCELL_FB_BUTTON_TONAL,
            .family = action->family,
            .shape = INKCELL_SHAPE_FULL,
            .idle_tone = INKCELL_TONE_DIM,
            .scale = plan->emphasized_label ? row->small : row->scale,
            .focus_id = action->disabled ? INKCELL_FOCUS_NONE : action->focus_id,
        };
        inkcell_fb_draw_button(state, &pill);
        x -= w + row->gap;
    }

    if (plan->status_icon) {
        const int w = inkcell_fb_app_bar_status_w(state, row, &bar->status, plan->status_text);
        /* The wider gap goes between the status and the buttons it stands beside - see the plan. */
        if (x != right) {
            x -= inkcell_fb_char_adv(state, row->small) - row->gap;
        }
        x -= w;
        /* The tone is the point on the ground; on a selection's container it is the container's
           ink, because a tone was validated against the ground and not against that fill. */
        const struct inkcell_rgb ink =
            row->tinted ? row->ink : inkcell_fb_tone_color(state, bar->status.tone);
        const int y =
            row->center - inkcell_scale_px((int)inkcell_fb_font(state)->height, row->small) / 2;
        inkcell_fb_draw_icon(state, x, y, bar->status.icon, row->small, ink, row->ground);
        if (plan->status_text) {
            inkcell_fb_draw_text(state,
                                 x + inkcell_fb_icon_box(state, row->small) +
                                     inkcell_fb_char_adv(state, row->small) / 2,
                                 y, bar->status.text, row->small, ink, row->ground);
        }
        x -= row->gap;
    }

    /* A little air between the cluster and whatever the heading puts beside it - but only when
       there is a cluster, so a bar without one is not moved by a pixel. */
    if (x != right) {
        x -= inkcell_fb_char_adv(state, row->small) / 2;
    }
    return x;
}

/* What the cluster may spend: everything between the heading's floor and the trailing edge,
   less the badge, which is a fact about the screen and keeps its place. */
static int inkcell_fb_app_bar_budget(const struct inkcell_draw_state *state, int text_x, int right,
                                     int keep, const char *badge, int small) {
    const int badge_w = inkcell_fb_badge_width(state, badge, small);
    const int reserve = badge_w > 0 ? badge_w + inkcell_fb_char_adv(state, small) : 0;
    return right - text_x - keep - reserve;
}

/* The title's floor: all of it when it is short, INKCELL_FB_APP_BAR_TITLE_KEEP_CELLS otherwise -
   and, in `slack`, how much more it would need to be whole. */
static int inkcell_fb_app_bar_title_keep(const struct inkcell_draw_state *state, const char *title,
                                         int scale, int *slack) {
    *slack = 0;
    if (title == NULL || title[0] == '\0') {
        return 0;
    }
    const int keep = INKCELL_FB_APP_BAR_TITLE_KEEP_CELLS * inkcell_fb_char_adv(state, scale);
    const int w = inkcell_fb_text_width(state, title, scale);
    if (w <= keep) {
        return w;
    }
    *slack = w - keep;
    return keep;
}

/* ---- the search field ----------------------------------------------------------------------- */

/*
 * The title slot as a search field, filling [x, right) and centred on the row.
 *
 * Set at the *body* scale rather than the title's, because it holds what the reader typed and
 * that is content, not a heading - and so that a query and the rows it is filtering read as the
 * same size of thing.
 */
static void inkcell_fb_app_bar_draw_field(const struct inkcell_draw_state *state,
                                          const struct inkcell_fb_app_bar *bar,
                                          const struct inkcell_fb_app_bar_row *row, int x,
                                          int right) {
    const int scale = state->scale;
    const int adv = inkcell_fb_char_adv(state, scale);
    const struct inkcell_fb_rect box = {x, row->center - row->side / 2, right - x, row->side};
    if (box.w < INKCELL_FB_APP_BAR_FIELD_KEEP_CELLS * adv / 2) {
        return;
    }
    const struct inkcell_rgb fill = inkcell_fb_color(state, INKCELL_COLOR_SURFACE_HIGH);
    inkcell_fb_focus_register_shaped(state, bar->field_focus_id, &box, INKCELL_SHAPE_FULL);
    if (bar->field_focused) {
        inkcell_fb_focus_mark(state, bar->field_focus_id);
    }
    inkcell_fb_fill_round_rect(state, box.x, box.y, box.w, box.h,
                               inkcell_fb_radius(state, INKCELL_SHAPE_FULL), fill);

    const int glyph = inkcell_scale_px((int)inkcell_fb_font(state)->height, scale);
    const int text_y = row->center - glyph / 2;
    const int pad = inkcell_fb_space(state, INKCELL_SPACE_SM);
    int left = box.x + pad;
    int end = box.x + box.w - pad;

    inkcell_fb_draw_icon(state, left, text_y, INKCELL_ICON_SEARCH, scale,
                         inkcell_fb_color(state, INKCELL_COLOR_TEXT_DIM), fill);
    left += inkcell_fb_icon_box(state, scale) + adv / 2;

    const bool has_query = bar->query != NULL && bar->query[0] != '\0';
    /* The clear mark, at the field's trailing end, once there is something to clear and a way
       to press it. A square the height of the field, so its focused fill is the field's own
       curve at that end. */
    if (has_query && bar->clear_focus_id != INKCELL_FOCUS_NONE) {
        const struct inkcell_fb_rect clear = {box.x + box.w - box.h, box.y, box.h, box.h};
        inkcell_fb_focus_register_shaped(state, bar->clear_focus_id, &clear, INKCELL_SHAPE_FULL);
        struct inkcell_rgb ink = inkcell_fb_color(state, INKCELL_COLOR_TEXT_DIM);
        struct inkcell_rgb ground = fill;
        if (bar->clear_focused) {
            inkcell_fb_focus_mark(state, bar->clear_focus_id);
            ground = inkcell_fb_color(state, INKCELL_COLOR_SURFACE_ACTIVE);
            ink = inkcell_fb_color(state, INKCELL_COLOR_TEXT_ON_SEL);
            inkcell_fb_fill_round_rect(state, clear.x, clear.y, clear.w, clear.h,
                                       inkcell_fb_radius(state, INKCELL_SHAPE_FULL), ground);
        }
        const int icon = inkcell_fb_icon_box(state, scale);
        inkcell_fb_draw_icon(state, clear.x + (clear.w - icon) / 2, text_y, INKCELL_ICON_CLOSE,
                             scale, ink, ground);
        end = clear.x;
    }

    /* The caret's width, and the gap it keeps from the text either side of it. */
    const int caret_w = inkcell_step_px(scale) > 1 ? inkcell_step_px(scale) / 2 : 1;
    const int room = end - left - (bar->editing ? 2 * caret_w : 0);
    if (room <= 0) {
        return;
    }

    char text[INKCELL_LINE_MAX];
    int caret_x = left;
    if (has_query) {
        /*
         * The *tail* of the query when it is too long, not its head: the reader is typing at the
         * end, and a field that showed the start of what they had typed would hide each letter
         * as it arrived. The same thing every single-line field does.
         */
        const char *tail = bar->query;
        while (tail[0] != '\0' && inkcell_fb_text_width(state, tail, scale) > room) {
            const struct inkcell_text_cell cell = inkcell_text_cell_next(tail);
            tail += cell.bytes > 0U ? cell.bytes : 1U;
        }
        inkwell_str_copy(text, sizeof text, tail);
        inkcell_fb_draw_text(state, left, text_y, text, scale,
                             inkcell_fb_color(state, INKCELL_COLOR_TEXT), fill);
        caret_x = left + inkcell_fb_text_width(state, text, scale) + caret_w / 2;
    } else if (bar->placeholder != NULL && bar->placeholder[0] != '\0') {
        inkwell_str_copy(text, sizeof text, bar->placeholder);
        while (inkcell_fb_text_width(state, text, scale) > room) {
            const size_t cells = inkcell_text_cells(text);
            if (cells <= 1U) {
                break;
            }
            inkcell_text_cell_truncate(text, cells - 1U);
        }
        /* After the caret rather than under it, so an empty field that has the keyboard reads
           as "type here" rather than as a placeholder with a line through its first letter. */
        inkcell_fb_draw_text(state, left + (bar->editing ? 2 * caret_w : 0), text_y, text, scale,
                             inkcell_fb_color(state, INKCELL_COLOR_TEXT_DIM), fill);
    }
    if (bar->editing) {
        inkcell_fb_fill_rect(state, caret_x, text_y, caret_w, glyph,
                             inkcell_fb_tone_color(state, INKCELL_TONE_PRIMARY));
    }
}

/* ---- the bar itself -------------------------------------------------------------------------- */

/*
 * The large title's height at `offset`, and how far it has collapsed - the one arithmetic both
 * the draw and inkcell_fb_app_bar_measure() read, so the body a screen measured is the body it
 * gets.
 *
 * Past the top of the content - a body being pulled down - is *not* clamped away: the heading
 * grows, which is the other half of what an overscroll is for and what every platform does with
 * a large title being pulled. The growth is the raw overscroll rather than a fraction of it,
 * because it is already through the band by the time it arrives here.
 */
struct inkcell_fb_app_bar_large {
    int collapsed; /* the ordinary bar's height, which the large one never goes below */
    int line;      /* the large heading's row */
    int32_t progress;
    int extra; /* how much of that row is left */
    int height;
};

static struct inkcell_fb_app_bar_large
inkcell_fb_app_bar_large_at(const struct inkcell_draw_state *state,
                            const struct inkcell_fb_layout *layout, int32_t offset) {
    struct inkcell_fb_app_bar_large large = {
        .collapsed = inkcell_fb_app_bar_height(state, layout, 0U),
        .line = inkcell_fb_large_title_travel(state),
    };
    int grown = 0;
    if (offset > 0 && large.line > 0) {
        large.progress = (int32_t)(((int64_t)offset * INKCELL_ANIM_ONE) / large.line);
        if (large.progress > INKCELL_ANIM_ONE) {
            large.progress = INKCELL_ANIM_ONE;
        }
    } else if (offset < 0) {
        grown = -offset;
    }
    large.extra = large.line - (int)(((int64_t)large.line * large.progress) / INKCELL_ANIM_ONE);
    large.height = large.collapsed + large.extra + grown;
    return large;
}

int inkcell_fb_app_bar_measure(const struct inkcell_draw_state *state,
                               const struct inkcell_fb_layout *layout,
                               const struct inkcell_fb_app_bar *bar) {
    if (state == NULL || layout == NULL || bar == NULL) {
        return 0;
    }
    if (bar->large && bar->mode == INKCELL_FB_APP_BAR_NORMAL) {
        return inkcell_fb_app_bar_large_at(state, layout, bar->offset).height;
    }
    return inkcell_fb_app_bar_height(
        state, layout, bar->mode == INKCELL_FB_APP_BAR_NORMAL ? bar->trail_count : 0U);
}

/* The title cut to fit [.., room] at the title's weight, a cell at a time from the end. */
static void inkcell_fb_app_bar_fit_title(const struct inkcell_draw_state *state, char *out,
                                         size_t out_len, const char *title, int scale, int room) {
    inkwell_str_copy(out, out_len, title != NULL ? title : "");
    while (inkcell_fb_text_width(state, out, scale) > room) {
        const size_t cells = inkcell_text_cells(out);
        if (cells <= 1U) {
            break;
        }
        inkcell_text_cell_truncate(out, cells - 1U);
    }
}

/*
 * The large title: the collapsing app bar of include/inkcell/ui/widgets/scroll.h, which is now
 * this bar with `large` set rather than a component of its own - so a large heading has the
 * actions, the menu and the status mark a small one has, and the two cannot drift.
 */
static struct inkcell_fb_app_bar_fit
inkcell_fb_app_bar_draw_large(const struct inkcell_draw_state *state,
                              struct inkcell_fb_layout *layout,
                              const struct inkcell_fb_app_bar *bar) {
    struct inkcell_fb_app_bar_fit fit = {0};
    const struct inkcell_fb_app_bar_large large =
        inkcell_fb_app_bar_large_at(state, layout, bar->offset);
    const int collapsed_h = large.collapsed;
    const int32_t progress = large.progress;
    const int extra = large.extra;
    const int height = large.height;
    const int large_line = large.line;

    const int margin = inkcell_fb_content_x(state);
    const int small = inkcell_fb_type_scale(state, INKCELL_TYPE_LABEL);
    const int title_scale = inkcell_fb_type_scale(state, INKCELL_TYPE_TITLE);
    /* The role the large heading is set in, which is what the whole shape is *for*: a heading
       that is merely the title size on its own row is a title with a gap over it. The display
       role brings the rest of a display's setting with it - the tighter tracking a heading at
       that size needs and the tighter line that keeps the expanded bar from eating a row it
       does not use. */
    const struct inkcell_type_style style = inkcell_fb_type_style(state, INKCELL_TYPE_DISPLAY);
    /* The detail beside the heading is metadata - a count, a time, a distance - which is the
       caption role and the second place in this toolkit that wants figures that do not move
       under the reader. */
    const struct inkcell_type_style caption = inkcell_fb_type_style(state, INKCELL_TYPE_CAPTION);
    const struct inkcell_rgb ground = inkcell_fb_color(state, INKCELL_COLOR_BG);

    /* The bar's own ground, so the two titles have something honest to fade against - a fade
       towards a colour that is not what is actually behind the glyph is a glyph with a halo. */
    const struct inkcell_box region = inkcell_fb_region(state);
    inkcell_fb_fill_rect(state, region.x, layout->body_y, region.w, height, ground);

    /* The column's own trailing edge, absolute: `margin` is where the column *starts*, which is
       only the same distance from the far edge while the column is centred on the whole surface. */
    const int column_right = margin + inkcell_fb_content_w(state);
    const int back_w = layout->back ? inkcell_fb_icon_box(state, title_scale) : 0;
    const int text_x = layout->back ? margin + back_w + inkcell_fb_char_adv(state, small) : margin;

    /*
     * The actions, on the collapsed row at both sizes and never faded: the verbs are not less
     * available while the heading is large, and the collapsed row is exactly the one a reader
     * is looking at while they scroll.
     */
    const struct inkcell_fb_app_bar_row row =
        inkcell_fb_app_bar_row(state, layout, layout->body_y + collapsed_h / 2,
                               inkcell_fb_tone_color(state, INKCELL_TONE_NORMAL), ground, false);
    int slack = 0;
    const int keep = inkcell_fb_app_bar_title_keep(state, bar->title, title_scale, &slack);
    const struct inkcell_fb_app_bar_plan plan = inkcell_fb_app_bar_plan(
        state, bar, &row,
        inkcell_fb_app_bar_budget(state, text_x, column_right, keep, bar->badge, small), slack);
    int right = inkcell_fb_app_bar_draw_cluster(state, bar, &row, &plan, column_right, &fit);

    /*
     * The badge sits on both sizes of the bar and never fades.
     *
     * It is a *state* rather than a decoration - "unsaved", "3 waiting" - and the collapsed
     * bar is precisely the one a reader is looking at while they scroll, so a badge that faded
     * out as the heading shrank would take the fact with it at the moment it is most wanted.
     */
    const int badge_w = inkcell_fb_badge_width(state, bar->badge, small);
    if (badge_w > 0) {
        const int badge_h = inkcell_scale_px((int)inkcell_fb_font(state)->height, small);
        const int text_y = layout->body_y + (collapsed_h - badge_h) / 2;
        const struct inkcell_fb_rect box = {.x = right - badge_w,
                                            .y = text_y - inkcell_step_px(small),
                                            .w = badge_w,
                                            .h = inkcell_fb_line_adv(state, small) -
                                                 inkcell_step_px(small)};
        inkcell_fb_draw_badge(state, &box, text_y, bar->badge, bar->badge_family, small);
        right -= badge_w + inkcell_fb_char_adv(state, small);
    }

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
        inkcell_fb_app_bar_fit_title(state, fitted, sizeof fitted, bar->title, title_scale,
                                     right - text_x);
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
        int large_right = column_right;
        if (bar->detail != NULL && bar->detail[0] != '\0') {
            const struct inkcell_rgb dim =
                inkcell_fb_fade(inkcell_fb_color(state, INKCELL_COLOR_TEXT_DIM), ground, progress);
            const int w = inkcell_fb_text_width_styled(state, bar->detail, &caption);
            const int lift = (large_line - inkcell_fb_line_adv_styled(state, &caption)) / 2;
            inkcell_fb_draw_text_styled(state, large_right - w, y + lift, bar->detail, &caption,
                                        dim, ground);
            large_right -= w + inkcell_fb_space(state, INKCELL_SPACE_SM);
        }
        char fitted[INKCELL_LINE_MAX];
        inkwell_str_copy(fitted, sizeof fitted, bar->title);
        while (inkcell_fb_text_width_styled(state, fitted, &style) > large_right - margin) {
            const size_t cells = inkcell_text_cells(fitted);
            if (cells <= 1U) {
                break;
            }
            inkcell_text_cell_truncate(fitted, cells - 1U);
        }
        inkcell_fb_draw_text_styled(state, margin, y, fitted, &style, ink, ground);
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
        inkcell_fb_fill_rect(state, region.x,
                             layout->body_y + height - inkcell_fb_rule_height(state, 1), region.w,
                             inkcell_fb_rule_height(state, 1), rule);
    }

    /*
     * What is left of the body, recomputed from the body's real bottom rather than deducted.
     *
     * The ordinary bar's rule, and its reason (see the tail of inkcell_fb_draw_app_bar()). It
     * matters more here than there, because this height changes every frame while the heading
     * is collapsing - a deduction would drift.
     */
    layout->body_y += height;
    layout->rows = inkcell_fb_layout_rows(state, layout);
    return fit;
}

struct inkcell_fb_app_bar_fit inkcell_fb_draw_app_bar(const struct inkcell_draw_state *state,
                                                      struct inkcell_fb_layout *layout,
                                                      const struct inkcell_fb_app_bar *bar) {
    struct inkcell_fb_app_bar_fit fit = {0};
    if (state == NULL || layout == NULL || bar == NULL) {
        return fit;
    }
    if (bar->large && bar->mode == INKCELL_FB_APP_BAR_NORMAL) {
        return inkcell_fb_app_bar_draw_large(state, layout, bar);
    }
    const bool search = bar->mode == INKCELL_FB_APP_BAR_SEARCH;
    const bool selection = bar->mode == INKCELL_FB_APP_BAR_SELECTION;
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
    const int margin = inkcell_fb_content_x(state);
    /* Stated rather than inferred as `panel_width - margin`. A centred column makes those two
       equal, but only to within the pixel an odd leftover rounds away - and equal-by-symmetry
       is not a thing the next reader should have to work out. */
    const int trailing = margin + inkcell_fb_content_w(state);
    struct inkcell_rgb ground = inkcell_fb_color(state, INKCELL_COLOR_BG);
    struct inkcell_rgb title_ink = inkcell_fb_tone_color(state, INKCELL_TONE_PRIMARY);
    struct inkcell_rgb plain_ink = inkcell_fb_tone_color(state, INKCELL_TONE_NORMAL);
    struct inkcell_rgb leading_ink = inkcell_fb_tone_color(state, INKCELL_TONE_DIM);
    /* In selection mode B clears the selection, whatever it does otherwise - so the leading
       mark is drawn either way, and it is a close rather than an arrow. */
    const bool leading = layout->back || selection;

    int y = layout->body_y;
    /* The content column: the trail and the title share a left edge, and the back arrow hangs
       in the gutter to the left of both - which is where every platform puts it, and what keeps
       the two lines reading as one block rather than as two things that happen to be stacked. */
    const int text_x = leading ? margin + inkcell_fb_icon_box(state, scale) + adv / 2 : margin;

    /*
     * The overline, when this screen is somewhere rather than at a tab's root.
     *
     * It costs a label-scale line and it buys the whole of the breadcrumb's width back: at the
     * title scale "Settings > Modules > Telemetry" is thirty cells of a thirty-four cell line,
     * so the leaf - the one word saying which screen this is - was the half that got elided.
     * Above the title, at the label scale, the same trail is half as wide and the title has the
     * panel to itself.
     */
    if (bar->trail_count > 0U && bar->mode == INKCELL_FB_APP_BAR_NORMAL) {
        inkcell_fb_draw_app_bar_trail(state, bar, text_x, y, trailing, small);
        /* The glyph body and a hair, not the label scale's whole line advance. The advance
           carries the gap between two lines of running text, and the trail is not running text
           - it is a caption sitting on the title. Spending the advance here cost a body row on
           every screen with a trail, which is a row of content for a gap nobody sees. */
        y += inkcell_scale_px((int)inkcell_fb_font(state)->height, small) +
             inkcell_fb_space_at(state, INKCELL_SPACE_XS, small);
    }

    const int title_h = inkcell_scale_px((int)inkcell_fb_font(state)->height, scale);

    /*
     * A selection fills the bar's row with the secondary family's container, edge to edge - a
     * fill, so it runs the width of the surface the way the navigation bar's tier does. Centred
     * on the title's glyph body and a pad taller than the cluster's buttons, so every mark on
     * the row sits inside it; it never reaches the body, so nothing below moves.
     */
    if (selection) {
        const struct inkcell_paint paint = inkcell_fb_paint(
            state, INKCELL_FAMILY_SECONDARY, INKCELL_SLOT_CONTAINER, INKCELL_STATE_REST);
        const int side = inkcell_fb_icon_box(state, scale) +
                         2 * inkcell_fb_space_at(state, INKCELL_SPACE_XS, scale);
        const int pad = inkcell_fb_space(state, INKCELL_SPACE_XS);
        const struct inkcell_box region = inkcell_fb_region(state);
        inkcell_fb_fill_rect(state, region.x, y + title_h / 2 - side / 2 - pad, region.w,
                             side + 2 * pad, paint.fill);
        ground = paint.fill;
        title_ink = paint.ink;
        plain_ink = paint.ink;
        leading_ink = paint.ink;
    }

    /*
     * The leading affordance: what B does, said by the chrome rather than only by the keycap
     * at the bottom of the panel. layout->back is read off the action bar by the application,
     * so the arrow and the keycap cannot disagree.
     */
    if (leading) {
        inkcell_fb_draw_icon(state, margin, y, selection ? INKCELL_ICON_CLOSE : INKCELL_ICON_BACK,
                             scale, leading_ink, ground);
    }
    /* And it is B, to a pointer: the arrow is the way back that the action bar leaves out once
       there is one. Out into the gutter and down the title's whole line, because an icon's own
       box is a small thing to have to hit. Only with a pointer, so a panel's map spends no slot
       on a box nothing can press. */
    if (leading && state->pointer) {
        const int gutter = inkcell_fb_gutter(state);
        const struct inkcell_fb_rect arrow = {.x = margin - gutter,
                                              .y = y,
                                              .w = gutter + inkcell_fb_icon_box(state, scale) +
                                                   adv / 2,
                                              .h = inkcell_fb_line_adv(state, scale)};
        inkcell_fb_target_register(state, INKCELL_FOCUS_KEY(INKCELL_KEY_B), &arrow);
    }

    /*
     * The trailing slot: a fact about the *screen*, which is the one thing a title could not
     * carry. "3 unsaved" used to be a " (unsaved)" glued onto the end of the title with a %s,
     * where it was neither countable nor a badge - a capsule cannot be spelled inside a
     * sentence.
     */
    /*
     * The cluster first, from the trailing edge in - overflow, actions, status - into whatever
     * the heading's floor and the badge leave it. What it did not have room for is in `fit`,
     * and is the overflow menu.
     */
    const struct inkcell_fb_app_bar_row row =
        inkcell_fb_app_bar_row(state, layout, y + title_h / 2, plain_ink, ground, selection);
    int slack = 0;
    const int keep =
        search ? INKCELL_FB_APP_BAR_FIELD_KEEP_CELLS * inkcell_fb_char_adv(state, state->scale)
               : inkcell_fb_app_bar_title_keep(state, bar->title, scale, &slack);
    const struct inkcell_fb_app_bar_plan plan = inkcell_fb_app_bar_plan(
        state, bar, &row,
        inkcell_fb_app_bar_budget(state, text_x, trailing, keep, bar->badge, small), slack);
    int right = inkcell_fb_app_bar_draw_cluster(state, bar, &row, &plan, trailing, &fit);

    const int badge_w = inkcell_fb_badge_width(state, bar->badge, small);
    if (badge_w > 0) {
        /* Centred on the title's glyph body rather than on its line advance: the advance
           carries the gap accents hang in, and counting it would sit the capsule low. */
        const int badge_h = inkcell_scale_px((int)inkcell_fb_font(state)->height, small);
        const int text_y = y + (title_h - badge_h) / 2;
        const struct inkcell_fb_rect box = {.x = right - badge_w,
                                            .y = text_y - inkcell_step_px(small),
                                            .w = badge_w,
                                            .h = inkcell_fb_line_adv(state, small) -
                                                 inkcell_step_px(small)};
        inkcell_fb_draw_badge(state, &box, text_y, bar->badge, bar->badge_family, small);
        right -= badge_w + inkcell_fb_char_adv(state, small);
    }

    if (search) {
        inkcell_fb_app_bar_draw_field(state, bar, &row, text_x, right);
    } else {
        struct inkcell_line line;
        inkcell_line_reset(&line);
        inkcell_line_printf(&line, "%s", bar->title != NULL ? bar->title : "");
        /* Fitted to the room *this* scale leaves between the two slots, not to the body's column
           count. Bigger glyphs mean fewer of them, and a title measured against a column count
           it is not drawn at is a title that runs off the panel. */
        const int room = right > text_x ? (right - text_x) / adv : 0;
        inkcell_line_fit(&line, (size_t)(room > 0 ? room : 0));
        inkcell_fb_draw_text_weight(state, text_x, y, inkcell_line_text(&line), scale,
                                    inkcell_fb_type_weight(state, INKCELL_TYPE_TITLE), title_ink,
                                    ground);
    }

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
    return fit;
}

size_t inkcell_fb_app_bar_menu(const struct inkcell_fb_app_bar *bar,
                               const struct inkcell_fb_app_bar_fit *fit,
                               struct inkcell_fb_menu_item *items, uint32_t *ids, size_t max) {
    if (bar == NULL || items == NULL) {
        return 0U;
    }
    const size_t count = inkcell_fb_app_bar_count(bar);
    const uint32_t shown = fit != NULL ? fit->shown : 0U;
    size_t out = 0U;
    for (size_t i = 0; i < count && out < max; ++i) {
        const struct inkcell_fb_bar_action *action = &bar->actions[i];
        if (!inkcell_fb_app_bar_action_real(action) || (shown & (1U << i)) != 0U) {
            continue;
        }
        items[out] = (struct inkcell_fb_menu_item){
            .label = action->label,
            .icon = action->icon,
            .disabled = action->disabled,
        };
        if (ids != NULL) {
            ids[out] = action->focus_id;
        }
        ++out;
    }
    return out;
}

void inkcell_fb_draw_empty(const struct inkcell_draw_state *state,
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
        const struct inkcell_box region = inkcell_fb_region(state);
        inkcell_fb_draw_icon(state, region.x + (region.w - box) / 2, y, icon, big,
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

int inkcell_fb_rule_height(const struct inkcell_draw_state *state, int scale) {
    return inkcell_fb_space_at(state, INKCELL_SPACE_XS, scale);
}

void inkcell_fb_draw_rule(const struct inkcell_draw_state *state, int x, int y, int w, int scale,
                          enum inkcell_color role) {
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
static int inkcell_fb_fab_scale(const struct inkcell_draw_state *state,
                                enum inkcell_fb_fab_size size) {
    switch (size) {
    case INKCELL_FB_FAB_SM:
        return inkcell_fb_type_scale(state, INKCELL_TYPE_BODY);
    case INKCELL_FB_FAB_LG:
        /* The one size that grows its symbol as well as its room, the way the large title is the
           one heading a step above INKCELL_TYPE_TITLE. Unclamped for that same reason: a theme
           already at the top of the range gets a symbol the font registry resamples rather than
           one the type scale flattened back into the body. */
        return inkcell_fb_type_scale(state, INKCELL_TYPE_TITLE) + INKCELL_SCALE(1);
    case INKCELL_FB_FAB_MD:
    default:
        return inkcell_fb_type_scale(state, INKCELL_TYPE_TITLE);
    }
}

static int inkcell_fb_fab_label_scale(const struct inkcell_draw_state *state,
                                      enum inkcell_fb_fab_size size) {
    const int scale = inkcell_fb_fab_scale(state, size) - INKCELL_SCALE(1);
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
static int inkcell_fb_fab_pad(const struct inkcell_draw_state *state, enum inkcell_fb_fab_size size,
                              int scale) {
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

static struct inkcell_fb_fab_metrics inkcell_fb_fab_measure(const struct inkcell_draw_state *state,
                                                            const struct inkcell_fb_layout *layout,
                                                            const struct inkcell_fb_fab *fab) {
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

    /* The clearance is the theme's margin - a vertical gap, the snackbar's rule - while the two
       edges are the content column's. Measuring the room from the panel's margin to the
       column's trailing edge overstates it by however far the column is inset, which on a wide
       window is enough to accept an extended label that then draws out past the column. */
    const int margin = inkcell_fb_margin(state);
    const int leading = inkcell_fb_content_x(state);
    m.right = leading + inkcell_fb_content_w(state);
    /*
     * A full margin clear of the footer rather than the half a card stops at, which is the
     * snackbar's rule and for its reason: a card is *in* the body and belongs against the body's
     * own bottom edge, while a thing floating over everything keeps the distance the panel edge
     * keeps.
     */
    m.bottom = layout->footer_y - margin;
    m.room = m.right - leading;
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

struct inkcell_fb_rect inkcell_fb_fab_box(const struct inkcell_draw_state *state,
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

int inkcell_fb_fab_clearance(const struct inkcell_draw_state *state,
                             const struct inkcell_fb_layout *layout,
                             const struct inkcell_fb_fab *fab) {
    const struct inkcell_fb_rect box = inkcell_fb_fab_box(state, layout, fab);
    /* From the top of the disc to the foot of the body. The width is what moves as a FAB
       collapses and the height is not, so this answer is the same on every frame of one. */
    return box.w > 0 ? layout->footer_y - box.y : 0;
}

struct inkcell_fb_rect inkcell_fb_draw_fab(struct inkcell_draw_state *state,
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
        .focused = fab->focused,
        .variant = INKCELL_FB_BUTTON_TONAL,
        .family = fab->family,
    };
    const struct inkcell_fb_button_paint paint = inkcell_fb_button_paint(state, &spec);

    /* Registered before anything is painted and before the clip below, which is the button's
       order and the reason for it: this is the frame saying the box exists rather than saying
       what colour it came out. With its shape, so a ring that lands here is the curve the FAB
       was filled with. */
    inkcell_fb_focus_register_shaped(state, fab->focus_id, &box, INKCELL_SHAPE_FULL);
    if (fab->focused) {
        inkcell_fb_focus_mark(state, fab->focus_id);
    }

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
    /* And the shadow's reach past the widest it could be, so the rows it darkens are copied
       with the rest. The shadow keeps itself to the band regardless - see
       inkcell_fb_draw_shadow() - so this is about the copy, not the draw. */
    const struct inkcell_fb_rect room = {m.right - m.room - pad, box.y - pad, m.room + 2 * pad,
                                         box.h + 2 * pad};
    const struct inkcell_fb_rect reach =
        inkcell_fb_shadow_bounds(state, room, INKCELL_ELEVATION_FLOATING);
    inkcell_fb_animation_damage(state, reach.x, reach.y, reach.w, reach.h);

    /* It floats: over the body, not of it - which is what Material's FAB says with a shadow and
       this one now does too, where the theme casts one. The fill is still the main cue (the
       only saturated container on the body), which is what carries it on a dark palette. */
    inkcell_fb_draw_shadow(state, box, inkcell_fb_radius(state, INKCELL_SHAPE_FULL),
                           INKCELL_ELEVATION_FLOATING, INKCELL_ANIM_ONE);
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
    const int text_h = inkcell_scale_px((int)inkcell_fb_font(state)->height, m.scale);
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
            box.y +
            (box.h - inkcell_scale_px((int)inkcell_fb_font(state)->height, m.label_scale)) / 2;
        inkcell_fb_draw_text(state, x + inkcell_fb_icon_box(state, m.scale) + m.gap, label_y,
                             fab->label, m.label_scale, ink, paint.paint.fill);
    }
    inkcell_fb_view_pop(state);
    return box;
}
