#define _POSIX_C_SOURCE 200809L

/*
 * The keyboard a finger types on, drawn. See include/inkcell/ui/widgets/touch_keyboard.h.
 *
 * Nothing here decides anything: which key is lit, which layer, whether the letters are capitals
 * are all the model's, and the boxes are the model's boxes inset. What is here is how each kind
 * of key looks.
 */

#include "inkcell/ui/widgets/touch_keyboard.h"

#include "inkcell/ui/widgets/button.h"

/* The gap between two keys, of each, in dp: half of it on each side of a key. */
#define TKB_GAP_X_DP 6
#define TKB_GAP_Y_DP 10

/* The largest glyph scale whose line fits `room` pixels, between the body scale and the fill
   ceiling - widgets_keyboard.c's rule, for a key sized by a finger rather than a grid. */
static int tkb_scale(const struct inkcell_draw_state *state, int room) {
    const int step = inkcell_fb_line_adv(state, INKCELL_SCALE(1));
    int scale = step > 0 ? INKCELL_SCALE(room / step) : state->scale;
    const int fill_max =
        state->scale > INKCELL_SCALE_FILL_MAX ? state->scale : INKCELL_SCALE_FILL_MAX;
    if (scale > fill_max) {
        scale = fill_max;
    }
    if (scale < state->scale) {
        scale = state->scale;
    }
    return scale;
}

static enum inkcell_icon tkb_icon(const struct inkcell_fb_touch_keyboard *keyboard,
                                  const struct inkcell_tkb_key *key) {
    switch (key->kind) {
    case INKCELL_TKB_SHIFT:
        return keyboard->keyboard->shift == INKCELL_TKB_SHIFT_LOCKED ? INKCELL_ICON_SHIFT_LOCK
                                                                     : INKCELL_ICON_SHIFT;
    case INKCELL_TKB_DELETE:
        return INKCELL_ICON_BACKSPACE;
    case INKCELL_TKB_HIDE:
        return INKCELL_ICON_KEYBOARD_HIDE;
    case INKCELL_TKB_ENTER:
        if (keyboard->enter_icon != INKCELL_ICON_NONE) {
            return keyboard->enter_icon;
        }
        return keyboard->enter_label != NULL ? INKCELL_ICON_NONE : INKCELL_ICON_ENTER;
    default:
        return INKCELL_ICON_NONE;
    }
}

/* The character under the finger, risen over its key and larger: drawn last, over the row above.
   A key on the top row has no row above inside the box, and is only lit. */
static void tkb_preview(const struct inkcell_draw_state *state, struct inkcell_fb_rect box,
                        struct inkcell_fb_rect key, const char *label, int scale) {
    const int w = key.w + key.w / 3;
    const int h = key.h;
    int x = key.x + (key.w - w) / 2;
    if (x < box.x) {
        x = box.x;
    }
    if (x + w > box.x + box.w) {
        x = box.x + box.w - w;
    }
    const int y = key.y - h;
    if (y < box.y) {
        return;
    }
    const struct inkcell_fb_rect bubble = {x, y, w, h};
    const int radius = inkcell_fb_radius(state, INKCELL_SHAPE_SM);
    inkcell_fb_draw_shadow(state, bubble, radius, INKCELL_ELEVATION_FLOATING, INKCELL_ANIM_ONE);
    const struct inkcell_fb_button button = {
        .rect = bubble,
        .label = label,
        .variant = INKCELL_FB_BUTTON_FILLED,
        .shape = INKCELL_SHAPE_SM,
        .ground = INKCELL_COLOR_SURFACE_LOW,
        .scale = scale,
    };
    inkcell_fb_draw_button(state, &button);
}

void inkcell_fb_draw_touch_keyboard(const struct inkcell_draw_state *state,
                                    struct inkcell_fb_rect box,
                                    const struct inkcell_fb_touch_keyboard *keyboard) {
    if (state == NULL || keyboard == NULL || keyboard->keyboard == NULL || box.w <= 0 ||
        box.h <= 0) {
        return;
    }
    const struct inkcell_touch_keyboard *const kb = keyboard->keyboard;
    inkcell_fb_fill_rect(state, box.x, box.y, box.w, box.h,
                         inkcell_fb_color(state, INKCELL_COLOR_SURFACE_LOW));
    const int gap_x = inkcell_fb_dp(state, TKB_GAP_X_DP) / 2;
    const int gap_y = inkcell_fb_dp(state, TKB_GAP_Y_DP) / 2;
    const struct inkcell_tkb_box area = inkcell_fb_touch_keyboard_box(box);
    const int row_h = box.h / (int)INKCELL_TKB_ROWS - 2 * gap_y;
    const int scale = tkb_scale(state, row_h / 2);

    struct inkcell_fb_rect lit = {0, 0, 0, 0};
    const char *lit_label = NULL;
    char lit_scratch[INKCELL_TKB_TEXT_MAX];
    for (unsigned row = 0U; row < INKCELL_TKB_ROWS; ++row) {
        size_t count = 0U;
        const struct inkcell_tkb_key *keys = inkcell_touch_keyboard_row(kb->layer, row, &count);
        for (unsigned k = 0U; k < count; ++k) {
            const struct inkcell_tkb_key *key = &keys[k];
            const struct inkcell_tkb_box b =
                inkcell_touch_keyboard_key_box(kb->layer, row, k, area);
            const struct inkcell_fb_rect rect = {b.x + gap_x, b.y + gap_y, b.w - 2 * gap_x,
                                                 b.h - 2 * gap_y};
            const bool pressed = kb->row == (int8_t)row && kb->key == (int8_t)k;
            char scratch[INKCELL_TKB_TEXT_MAX];
            const char *label = inkcell_touch_keyboard_label(kb, key, scratch);
            const enum inkcell_icon icon = tkb_icon(keyboard, key);
            if (key->kind == INKCELL_TKB_ENTER && icon == INKCELL_ICON_NONE) {
                label = keyboard->enter_label;
            }
            struct inkcell_fb_button button = {
                .rect = rect,
                .icon = icon,
                .label = icon != INKCELL_ICON_NONE ? "" : label,
                .focused = pressed,
                .variant = INKCELL_FB_BUTTON_FILLED,
                .shape = INKCELL_SHAPE_SM,
                .ground = INKCELL_COLOR_SURFACE_LOW,
                /* A letter and a symbol at the key's own size; a word ("?123", "Done") at the
                   body's, which is what fits a key a word wide. */
                .scale = (key->kind == INKCELL_TKB_CHAR || icon != INKCELL_ICON_NONE)
                             ? scale
                             : state->scale,
            };
            switch (key->kind) {
            case INKCELL_TKB_ENTER:
                button.variant = INKCELL_FB_BUTTON_TONAL;
                button.family = INKCELL_FAMILY_PRIMARY;
                break;
            case INKCELL_TKB_SHIFT:
                button.variant = INKCELL_FB_BUTTON_TONAL;
                button.family = kb->shift == INKCELL_TKB_SHIFT_OFF ? INKCELL_FAMILY_SECONDARY
                                                                   : INKCELL_FAMILY_PRIMARY;
                break;
            case INKCELL_TKB_DELETE:
            case INKCELL_TKB_LAYER:
            case INKCELL_TKB_HIDE:
                button.variant = INKCELL_FB_BUTTON_TONAL;
                button.family = INKCELL_FAMILY_SECONDARY;
                break;
            default:
                break;
            }
            inkcell_fb_draw_button(state, &button);
            if (pressed && key->kind == INKCELL_TKB_CHAR) {
                lit = rect;
                lit_label = inkcell_touch_keyboard_label(kb, key, lit_scratch);
            }
        }
    }
    if (lit_label != NULL) {
        tkb_preview(state, box, lit, lit_label, tkb_scale(state, (lit.h * 2) / 3));
    }
}
