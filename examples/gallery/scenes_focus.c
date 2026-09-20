#define _POSIX_C_SOURCE 200809L

/*
 * The focus finder, drawn: a screen made of three unrelated things, and the four presses that
 * connect them.
 *
 * Every other page on this sheet is a picture of a component. This one is a picture of
 * *navigation*, which is the thing a specimen sheet normally cannot show - and it can be shown
 * here because inkcell_focus_find() answers in ids and rectangles rather than in pixels, so the
 * scene is free to draw the answer it got.
 *
 * The layout is deliberately ragged, because a tidy one proves nothing. The filter chips are
 * each as wide as their own word, the keypad is a grid on its own rhythm, and the two verbs sit
 * against the trailing edge lined up with nothing at all. No cursor index can walk that; the
 * lines drawn on it are where a d-pad goes.
 *
 * It is also the worked example of the registration rule, in the one place a reader will
 * actually look at it: every `focus_cell()` below draws a button and registers the box it drew,
 * in the same call, which is what makes "what is on the frame" and "what the cursor can reach"
 * the same list.
 */

#include "gallery.h"

#include "inkcell/ui/focus.h"

/* The screen's own vocabulary. A grid's ids fold its index in, which is also what keeps them
   distinct - inkcell_focus_add() refuses a second box under a name already spoken for. */
enum focus_id {
    FOCUS_ID_CHIP = 1, /* .. + 2 */
    FOCUS_ID_KEY = 8,  /* .. + 5, in reading order */
    FOCUS_ID_CANCEL = 20,
    FOCUS_ID_SEND,
};

/* The cell the cursor is on when the picture is taken: the middle of the keypad's second row,
   which is the one place on this screen where all four presses land on something different. */
#define FOCUS_CURSOR (FOCUS_ID_KEY + 4U)

#define FOCUS_KEY_COLS 3
#define FOCUS_KEY_ROWS 2

/* Draws a pressable thing and registers the box it came out as, in one breath. Returns the x
   past it, so a row of them is a loop with no arithmetic in it. */
static int focus_cell(struct inkcell_backend_fb_state *state, struct inkcell_focus_map *map,
                      uint32_t id, struct inkcell_fb_rect rect, const char *label,
                      enum inkcell_fb_button_variant variant, enum inkcell_shape shape, int scale) {
    const struct inkcell_fb_button button = {
        .rect = rect,
        .label = label,
        .selected = id == FOCUS_CURSOR,
        .variant = variant,
        .family = INKCELL_FAMILY_PRIMARY,
        .shape = shape,
        .idle_tone = INKCELL_TONE_NORMAL,
        .ground = INKCELL_COLOR_BG,
        .scale = scale,
    };
    inkcell_fb_draw_button(state, &button);
    (void)inkcell_focus_add(map, id, rect.x, rect.y, rect.w, rect.h);
    return rect.x + rect.w;
}

/*
 * The line from the cursor to what a press finds: out of the source, across the gap, into the
 * destination.
 *
 * Three segments rather than one, and the middle one is why: the corridor between two rows is
 * the one strip of the panel that is guaranteed empty, so a connector that does its sideways
 * travel there is a connector that never crosses a control it is not about. A diagonal would
 * be shorter and would be drawn straight through whatever it passed over.
 */
/* One arm of a connector: `thick` wide, centred on `at`, running from `a` to `b` along the axis
   `vertical` names. Written once so that the three arms below are three calls and not three
   rectangles worked out by hand. */
static void focus_segment(const struct inkcell_backend_fb_state *state, int at, int a, int b,
                          int thick, bool vertical, struct inkcell_rgb ink) {
    const int lo = (a < b) ? a : b;
    const int span = ((a < b) ? b - a : a - b) + thick;
    if (vertical) {
        inkcell_fb_fill_rect(state, at - thick / 2, lo, thick, span, ink);
    } else {
        inkcell_fb_fill_rect(state, lo, at - thick / 2, span, thick, ink);
    }
}

static void focus_link(const struct inkcell_backend_fb_state *state, struct inkcell_focus_rect from,
                       struct inkcell_focus_rect to, bool vertical) {
    const struct inkcell_rgb ink = inkcell_fb_color(state, INKCELL_COLOR_PRIMARY);
    const int thick = inkcell_fb_space(state, INKCELL_SPACE_XS);
    /* Where each box is crossed by the line, and where it is left and entered: `mid` is the
       middle of the corridor between them, which is the strip the sideways travel happens in. */
    const int from_mid = vertical ? from.x + from.w / 2 : from.y + from.h / 2;
    const int to_mid = vertical ? to.x + to.w / 2 : to.y + to.h / 2;
    const int from_edge = vertical ? ((to.y > from.y) ? from.y + from.h : from.y)
                                   : ((to.x > from.x) ? from.x + from.w : from.x);
    const int to_edge =
        vertical ? ((to.y > from.y) ? to.y : to.y + to.h) : ((to.x > from.x) ? to.x : to.x + to.w);
    const int mid = (from_edge + to_edge) / 2;

    focus_segment(state, from_mid, from_edge, mid, thick, vertical, ink);
    focus_segment(state, mid, from_mid, to_mid, thick, !vertical, ink);
    focus_segment(state, to_mid, mid, to_edge, thick, vertical, ink);

    /* And a ring around where the press lands, because a line arriving at an edge says which
       way it went and not what it reached. */
    inkcell_fb_stroke_round_rect(state, to.x - thick, to.y - thick, to.w + 2 * thick,
                                 to.h + 2 * thick, inkcell_fb_radius(state, INKCELL_SHAPE_MD),
                                 thick, ink);
}

void gallery_scene_focus(struct inkcell_backend_fb_state *state) {
    struct inkcell_fb_layout layout = gallery_frame(state, GALLERY_STR_HEAD_FOCUS, 0U);
    const struct inkcell_fb_row_box box = inkcell_fb_row_box(state);
    const int scale = layout.small;
    const int pad = inkcell_fb_space(state, INKCELL_SPACE_MD);
    const int high =
        inkcell_fb_line_adv(state, scale) + 2 * inkcell_fb_space_at(state, INKCELL_SPACE_SM, scale);
    /* The gap left between every two things here, and it is deliberately more than a screen
       would spend: the connectors are drawn in it, and a corridor a reader cannot see into is a
       page that shows six buttons touching. Everything else on this sheet is a specimen of a
       component; this one is a diagram, and it is allowed to be spaced like one. */
    const int corridor = 2 * inkcell_fb_space(state, INKCELL_SPACE_LG);
    struct inkcell_focus_item storage[FOCUS_KEY_COLS * FOCUS_KEY_ROWS + 8U];
    struct inkcell_focus_map map;
    int y = layout.body_y + pad;

    /* The map is opened at the top of the draw and filled by the draw. A map carried over from
       the last frame is a map of the layout before this one. */
    inkcell_focus_begin(&map, storage, (uint32_t)(sizeof storage / sizeof storage[0]));

    /* A filter row: pills as wide as their own words, which is where a row of equal steps stops
       being able to describe a screen. */
    static const enum gallery_str_id k_chips[] = {
        GALLERY_STR_ROW_BATTERY,
        GALLERY_STR_ROW_NETWORK,
        GALLERY_STR_ROW_SECURITY,
    };
    int x = box.text_x;
    for (size_t i = 0U; i < sizeof k_chips / sizeof k_chips[0]; ++i) {
        const char *label = gallery_text(k_chips[i]);
        const struct inkcell_fb_rect rect = {
            .x = x,
            .y = y,
            .w = inkcell_fb_button_width(state, INKCELL_ICON_NONE, label, scale) + 2 * pad,
            .h = high};
        x = focus_cell(state, &map, FOCUS_ID_CHIP + (uint32_t)i, rect, label,
                       INKCELL_FB_BUTTON_TONAL, INKCELL_SHAPE_FULL, scale);
        x += pad;
    }
    y += high + corridor;

    /* The keypad: a grid on its own rhythm, lined up with neither the row above it nor the one
       below, and the shape nothing in this toolkit could walk before. */
    static const char *const k_keys[FOCUS_KEY_ROWS][FOCUS_KEY_COLS] = {{"1", "2", "3"},
                                                                       {"4", "5", "6"}};
    const int key_w = 3 * high;
    for (int row = 0; row < FOCUS_KEY_ROWS; ++row) {
        for (int col = 0; col < FOCUS_KEY_COLS; ++col) {
            const struct inkcell_fb_rect rect = {
                .x = box.text_x + col * (key_w + corridor), .y = y, .w = key_w, .h = high};
            (void)focus_cell(state, &map, FOCUS_ID_KEY + (uint32_t)(row * FOCUS_KEY_COLS + col),
                             rect, k_keys[row][col], INKCELL_FB_BUTTON_FILLED, INKCELL_SHAPE_SM,
                             scale);
        }
        y += high + corridor;
    }

    /* And the verbs, against the trailing edge: lined up with nothing above them, which is what
       makes the press that reaches them a measurement rather than an index. */
    const char *cancel = gallery_text(GALLERY_STR_ACT_CANCEL);
    const char *send = gallery_text(GALLERY_STR_ACT_SEND);
    const int send_w = inkcell_fb_button_width(state, INKCELL_ICON_NONE, send, scale) + 2 * pad;
    const int cancel_w = inkcell_fb_button_width(state, INKCELL_ICON_NONE, cancel, scale) + 2 * pad;
    const struct inkcell_fb_rect send_rect = {
        .x = box.text_right - send_w, .y = y, .w = send_w, .h = high};
    const struct inkcell_fb_rect cancel_rect = {
        .x = send_rect.x - pad - cancel_w, .y = y, .w = cancel_w, .h = high};
    (void)focus_cell(state, &map, FOCUS_ID_CANCEL, cancel_rect, cancel, INKCELL_FB_BUTTON_TEXT,
                     INKCELL_SHAPE_FULL, scale);
    (void)focus_cell(state, &map, FOCUS_ID_SEND, send_rect, send, INKCELL_FB_BUTTON_FILLED,
                     INKCELL_SHAPE_FULL, scale);

    /*
     * The picture: from the cell the cursor is on, wherever each of the four presses lands.
     * Nothing below knows what it is drawing a line to - a chip, a key and a verb are all just
     * an id and the box it was registered as, which is the whole of what this file is for.
     */
    static const enum inkcell_focus_dir k_dirs[] = {INKCELL_FOCUS_LEFT, INKCELL_FOCUS_RIGHT,
                                                    INKCELL_FOCUS_UP, INKCELL_FOCUS_DOWN};
    struct inkcell_focus_rect from = {0, 0, 0, 0};
    if (inkcell_focus_rect_of(&map, FOCUS_CURSOR, &from)) {
        for (size_t i = 0U; i < sizeof k_dirs / sizeof k_dirs[0]; ++i) {
            struct inkcell_focus_rect to = {0, 0, 0, 0};
            const uint32_t found = inkcell_focus_find(&map, FOCUS_CURSOR, k_dirs[i]);
            if (found == INKCELL_FOCUS_NONE || !inkcell_focus_rect_of(&map, found, &to)) {
                continue;
            }
            focus_link(state, from, to,
                       k_dirs[i] == INKCELL_FOCUS_UP || k_dirs[i] == INKCELL_FOCUS_DOWN);
        }
    }

    gallery_footer(state, &layout);
}
