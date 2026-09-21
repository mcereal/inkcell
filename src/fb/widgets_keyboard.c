#define _POSIX_C_SOURCE 200809L

/*
 * The on-screen keyboard's grid: forty character keys over a row of five actions.
 *
 * Every key is inkcell_fb_draw_button() - the character grid and the action row are the same
 * component sized differently, which is why there is no keycap primitive under this. What is
 * here is the arithmetic between the model and those buttons: how tall a key is, where the
 * slack goes, and how large the letter on a key is set.
 *
 * Nothing here reads the text being typed. The grid draws the same whether the field above it
 * is empty or full, which is what lets the model be tested without a panel and this be drawn
 * without a draft.
 */

#include "inkcell/ui/widgets/keyboard.h"

#include "inkcell/ui/widgets/button.h"

/*
 * How tall one key is, given the room between `y` and the footer.
 *
 * Three numbers and the middle one is the answer. A key is as tall as the body has room for
 * rather than as tall as a line of text: five rows costing a text line each leaves a third of
 * the panel blank under keys a tenth of it wide and a twentieth of it tall, and nothing else on
 * a keyboard screen wants that space.
 *
 * The cap is the key's own width, because past square a keycap stops looking like a key and a
 * row of characters starts reading as a column of bars. The floor is what a row cost before the
 * grid grew - one line plus a cell's padding - so a shorter body lays the grid out exactly as
 * it always did rather than crushing it: a small panel, a large glyph scale, or a heading that
 * took two lines.
 */
static int inkcell_fb_keyboard_cell_h(const struct inkcell_backend_fb_state *state,
                                      const struct inkcell_fb_layout *layout, int y, int cell_w) {
    const int floor_h = layout->line + inkcell_fb_space(state, INKCELL_SPACE_MD);
    int cell_h = (layout->footer_y - y) / (int)INKCELL_KB_ROWS;
    if (cell_h > cell_w) {
        cell_h = cell_w;
    }
    if (cell_h < floor_h) {
        cell_h = floor_h;
    }
    return cell_h;
}

/*
 * The glyph multiplier a keycap's text is set at.
 *
 * A letter is legible at the body scale, so this is not the emoji layer's problem over again -
 * but a 24 px "q" adrift in a 99 px key reads as a key with nothing on it, and the scale a
 * keycap wants is simply the tallest line the key has room for. Derived from what one step
 * costs rather than stepped up in a loop.
 *
 * Clamped at three points: never below the body scale, never past the largest multiplier the
 * glyph cache is sized for, and never more than twice the text being typed - a reader who has
 * asked for small text has asked for it, and a keycap three times the draft under it would be
 * the screen arguing with them.
 */
static int inkcell_fb_keyboard_key_scale(const struct inkcell_backend_fb_state *state, int cell_h) {
    const int scale = state->scale;
    /* The room one whole step costs, and the count of them the key has room for - converted
       back into a scale at the end. The division has to be in whole steps: a key grown by a
       quarter step is a keycap a hair different from its neighbour rather than a size up. */
    const int step = inkcell_fb_line_adv(state, INKCELL_SCALE(1));
    int key_scale =
        (step > 0) ? INKCELL_SCALE((cell_h - 2 * inkcell_fb_space(state, INKCELL_SPACE_MD)) / step)
                   : scale;
    const int key_scale_max = (scale * 2 < INKCELL_SCALE_MAX) ? scale * 2 : INKCELL_SCALE_MAX;
    if (key_scale < scale) {
        key_scale = scale;
    }
    if (key_scale > key_scale_max) {
        key_scale = key_scale_max;
    }
    return key_scale;
}

/*
 * The symbol on one action key, and INKCELL_ICON_NONE for the presses that keep a word.
 *
 * The layer key is where both cases live. Three of its destinations *are* their own keycap -
 * "ABC", "abc", "#+=" - and no symbol carries a layer better than the layer's own letters do.
 * The emoji pages are the other half: a set of faces has no name to put on a keycap, and what
 * stood there instead was ":)" and ":)>", two cells of punctuation drawn at keycap size in the
 * font the letters are set in. A face is what that key means, so a face is what it draws.
 *
 * The submit key's symbol is the application's and arrives on the struct; the other three are
 * the same verb in every program that has a keyboard.
 */
static enum inkcell_icon inkcell_fb_keyboard_action_icon(const struct inkcell_fb_keyboard *kb,
                                                         enum inkcell_kb_action action) {
    switch (action) {
    case INKCELL_KB_ACTION_SPACE:
        return INKCELL_ICON_SPACE;
    case INKCELL_KB_ACTION_DELETE:
        return INKCELL_ICON_BACKSPACE;
    case INKCELL_KB_ACTION_CANCEL:
        return INKCELL_ICON_CLOSE;
    case INKCELL_KB_ACTION_SUBMIT:
        return kb->submit_icon;
    case INKCELL_KB_ACTION_LAYER:
        switch (inkcell_keyboard_layer_dest(kb->keyboard, kb->layout)) {
        case INKCELL_KB_DEST_EMOJI:
            return INKCELL_ICON_EMOJI;
        case INKCELL_KB_DEST_EMOJI_MORE:
            return INKCELL_ICON_EMOJI_MORE;
        case INKCELL_KB_DEST_LAYER:
        default:
            return INKCELL_ICON_NONE;
        }
    default:
        return INKCELL_ICON_NONE;
    }
}

void inkcell_fb_draw_keyboard(const struct inkcell_backend_fb_state *state,
                              const struct inkcell_fb_layout *layout, int *y,
                              const struct inkcell_fb_keyboard *keyboard) {
    if (state == NULL || layout == NULL || y == NULL || keyboard == NULL ||
        keyboard->keyboard == NULL) {
        return;
    }
    const struct inkcell_keyboard *const kb = keyboard->keyboard;
    const int scale = state->scale;
    const int margin = inkcell_fb_margin(state);
    const int grid_w = (int)state->var.xres - 2 * margin;
    const int cell_w = grid_w / (int)INKCELL_KB_COLS;
    const int cell_h = inkcell_fb_keyboard_cell_h(state, layout, *y, cell_w);
    const int key_scale = inkcell_fb_keyboard_key_scale(state, cell_h);

    /*
     * Whatever the cap left over goes above the grid rather than below it. When there is no
     * slack - and when the floor means the grid is taller than the room, which is a small panel
     * at a large glyph scale - it starts where the caller left off, and the last row is what the
     * footer overlaps rather than the first.
     */
    int top = *y;
    if (layout->footer_y - (int)INKCELL_KB_ROWS * cell_h > top) {
        top = layout->footer_y - (int)INKCELL_KB_ROWS * cell_h;
    }

    for (unsigned row = 0U; row < INKCELL_KB_CHAR_ROWS; ++row) {
        for (unsigned col = 0U; col < INKCELL_KB_COLS; ++col) {
            /* The cell rather than the character: three of the four layers are one ASCII byte
               and the fourth is an emoji, which is several of them and one keycap. What the
               button does with either is its own business - see `emoji_face` below. */
            char scratch[INKCELL_KB_CELL_MAX];
            const char *const key = inkcell_keyboard_cell(kb, keyboard->layout, row, col, scratch);
            const struct inkcell_fb_button button = {
                .rect = {.x = margin + (int)col * cell_w,
                         .y = top,
                         .w = cell_w - inkcell_step_px(scale),
                         .h = cell_h - inkcell_step_px(scale)},
                .label = key,
                .selected = (kb->row == row && kb->col == col),
                .variant = INKCELL_FB_BUTTON_TEXT,
                .shape = INKCELL_SHAPE_SM,
                .idle_tone = INKCELL_TONE_NORMAL,
                .scale = key_scale,
                /* Only the emoji layer's cells are sprites, and the button ignores this for the
                   three layers that are not - so the grid is described once for all four. */
                .emoji_face = true,
            };
            inkcell_fb_draw_button(state, &button);
        }
        top += cell_h;
    }

    const int action_w = grid_w / (int)INKCELL_KB_ACTIONS;
    for (unsigned col = 0U; col < INKCELL_KB_ACTIONS; ++col) {
        const enum inkcell_kb_action action = (enum inkcell_kb_action)col;
        const enum inkcell_icon icon = inkcell_fb_keyboard_action_icon(keyboard, action);
        const struct inkcell_fb_button button = {
            .rect = {.x = margin + (int)col * action_w,
                     .y = top,
                     .w = action_w - inkcell_step_px(scale),
                     .h = cell_h - inkcell_step_px(scale)},
            .icon = icon,
            .label = (icon == INKCELL_ICON_NONE)
                         ? inkcell_keyboard_action_label(kb, keyboard->layout, action)
                         : "",
            .selected = (kb->row == INKCELL_KB_CHAR_ROWS && kb->col == col),
            .variant = INKCELL_FB_BUTTON_FILLED,
            .shape = INKCELL_SHAPE_SM,
            .idle_tone = INKCELL_TONE_NORMAL,
            .scale = key_scale,
        };
        inkcell_fb_draw_button(state, &button);
    }
    *y = top + cell_h;
}
