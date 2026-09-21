#define _POSIX_C_SOURCE 200809L

/*
 * The focus finder, drawn: a screen made of three unrelated components, and the four presses
 * that connect them.
 *
 * Every other page on this sheet is a picture of a component. This one is a picture of
 * *navigation*, which is the thing a specimen sheet normally cannot show - and it can be shown
 * here because inkcell_focus_find() answers in ids and rectangles rather than in pixels, so the
 * scene is free to draw the answer it got.
 *
 * The layout is deliberately ragged, because a tidy one proves nothing. The filter chips are
 * each as wide as their own word, the keypad is a grid on its own rhythm, and the card's verbs
 * sit against its trailing edge lined up with nothing at all. No cursor index can walk that;
 * the lines drawn on it are where a d-pad goes.
 *
 * What the scene does *not* contain is the part worth noticing. There is no call here that
 * registers a chip, a key or a verb. The strip registers its own pills, the card registers the
 * verbs it had room for, and the keys are buttons carrying an id - all this file does is push a
 * map in with inkcell_fb_set_focus_map() and name things. A screen written on this toolkit says
 * what is on it; where those things came out is the toolkit's answer, and it is the only one
 * that can be right.
 */

#include "gallery.h"

#include "inkcell/ui/focus.h"

/* The screen's own vocabulary: an id per pressable thing, which is what every component here
   takes. Only a list folds an index into a base, and this screen has no list on it. */
enum focus_id {
    FOCUS_ID_CHIP = 1, /* .. + 2 */
    FOCUS_ID_KEY = 8,  /* .. + 5, in reading order */
    /* The card's verbs are a base and an offset - a card declares its verbs one call at a time,
       so naming them here and there would be naming them twice. */
    FOCUS_ID_CARD_ACTION = 20,
};

/* The cell the cursor is on when the picture is taken: the middle of the keypad's second row,
   which is the one place on this screen where all four presses land on something different. */
#define FOCUS_CURSOR (FOCUS_ID_KEY + 4U)

/* The ring page's journey: the first chip, a pill, to the last key, a rounded square. Chosen to
   travel in both directions at once and to change shape on the way, because a ring that only
   ever slid along a row would be a picture of half of what it does. */
#define FOCUS_ID_RING_FROM FOCUS_ID_CHIP
#define FOCUS_ID_RING_TO (FOCUS_ID_KEY + 5U)

/* What the screen below can register: the keypad, three chips and the card's two verbs, with
   room to spare so `dropped` stays a real signal rather than a number this page lives with. */
#define FOCUS_STORAGE (FOCUS_KEY_COLS * FOCUS_KEY_ROWS + 8U)

#define FOCUS_KEY_COLS 3
#define FOCUS_KEY_ROWS 2

/*
 * One keypad key: a button that carries an id.
 *
 * Nothing here calls the focus map. `focus_id` is a field on the button like its label and its
 * shape, and inkcell_fb_draw_button() is the single place in the toolkit where a pressable box
 * reaches the map - which is why a chip, a card's verb and a key all arrive there having said
 * nothing about it. Returns the x past the key, so a row of them is a loop with no arithmetic.
 */
static int focus_key(struct inkcell_backend_fb_state *state, uint32_t id, uint32_t selected,
                     struct inkcell_fb_rect rect, const char *label, int scale) {
    const struct inkcell_fb_button button = {
        .rect = rect,
        .label = label,
        .selected = id == selected,
        .variant = INKCELL_FB_BUTTON_FILLED,
        .family = INKCELL_FAMILY_PRIMARY,
        .shape = INKCELL_SHAPE_SM,
        .idle_tone = INKCELL_TONE_NORMAL,
        .ground = INKCELL_COLOR_BG,
        .scale = scale,
        .focus_id = id,
    };
    inkcell_fb_draw_button(state, &button);
    return rect.x + rect.w;
}

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

/*
 * The screen both pages are of: a filter row, a keypad and a card with two verbs, laid out
 * ragged on purpose.
 *
 * Shared because the two pages are the same screen answering two different questions - where a
 * press goes, and what the move looks like - and a second copy of the layout would be two
 * screens that drift apart a component at a time.
 *
 * `selected` is the key that carries the cursor's own fill. The map is left in `out` for the
 * caller to ask questions of.
 */
static struct inkcell_fb_layout
focus_screen(struct inkcell_backend_fb_state *state, enum gallery_str_id title, uint32_t selected,
             struct inkcell_focus_map *out, struct inkcell_focus_item *storage, uint32_t capacity) {
    struct inkcell_fb_layout layout = gallery_frame(state, title, 0U);
    const struct inkcell_fb_row_box box = inkcell_fb_row_box(state);
    const int scale = layout.small;
    const int high =
        inkcell_fb_line_adv(state, scale) + 2 * inkcell_fb_space_at(state, INKCELL_SPACE_SM, scale);
    /* The gap left between every two things here, and it is deliberately more than a screen
       would spend: the connectors are drawn in it, and a corridor a reader cannot see into is a
       page that shows six buttons touching. Everything else on this sheet is a specimen of a
       component; this one is a diagram, and it is allowed to be spaced like one. */
    const int corridor = 2 * inkcell_fb_space(state, INKCELL_SPACE_LG);
    int y = layout.body_y + inkcell_fb_space(state, INKCELL_SPACE_MD);

    /*
     * The map is opened at the top of the draw and filled *by* the draw, which is the whole
     * arrangement: a map carried over from the last frame is a map of the layout before this
     * one, and a map filled by the screen is a screen agreeing with the components by hand.
     */
    inkcell_focus_begin(out, storage, capacity);
    inkcell_fb_set_focus_map(state, out);

    /* A filter row: pills as wide as their own words, which is where a row of equal steps stops
       being able to describe a screen. The strip registers the ones it had room for. */
    const struct inkcell_fb_chip chips[] = {
        {.icon = INKCELL_ICON_NONE,
         .label = gallery_text(GALLERY_STR_ROW_BATTERY),
         .focus_id = FOCUS_ID_CHIP},
        {.icon = INKCELL_ICON_NONE,
         .label = gallery_text(GALLERY_STR_ROW_NETWORK),
         .focus_id = FOCUS_ID_CHIP + 1U},
        {.icon = INKCELL_ICON_NONE,
         .label = gallery_text(GALLERY_STR_ROW_SECURITY),
         .focus_id = FOCUS_ID_CHIP + 2U},
    };
    /* The strip takes the baseline its pills hang from rather than their top edge, which is
       what `+ scale` is: see inkcell_fb_chip_box(). */
    (void)inkcell_fb_draw_chip_strip(state, box.text_x, y + scale, chips,
                                     sizeof chips / sizeof chips[0], 1U,
                                     box.text_right - box.text_x, INKCELL_COLOR_BG, scale);
    y += inkcell_fb_line_adv(state, scale) + corridor;

    /* The keypad: a grid on its own rhythm, lined up with neither the row above it nor the card
       below, and the shape nothing in this toolkit could walk before. */
    static const char *const k_keys[FOCUS_KEY_ROWS][FOCUS_KEY_COLS] = {{"1", "2", "3"},
                                                                       {"4", "5", "6"}};
    const int key_w = 3 * high;
    for (int row = 0; row < FOCUS_KEY_ROWS; ++row) {
        for (int col = 0; col < FOCUS_KEY_COLS; ++col) {
            const struct inkcell_fb_rect rect = {
                .x = box.text_x + col * (key_w + corridor), .y = y, .w = key_w, .h = high};
            (void)focus_key(state, FOCUS_ID_KEY + (uint32_t)(row * FOCUS_KEY_COLS + col), selected,
                            rect, k_keys[row][col], scale);
        }
        y += high + corridor;
    }

    /*
     * And a card with two verbs on its heading line: the component the whole mechanism was
     * argued from.
     *
     * Nothing about this card says where its verbs went. They are laid out from its trailing
     * edge, against whatever width the card came out at, and dropped from the end when there is
     * no room - so the one honest answer to "what does down from key 5 reach" is the one the
     * card gives by registering what it drew.
     */
    struct inkcell_fb_card card;
    inkcell_fb_card_begin(&card, INKCELL_FB_CARD_OUTLINED, INKCELL_ICON_TELEMETRY,
                          gallery_id(GALLERY_STR_TAB_READINGS), INKCELL_TONE_NORMAL);
    card.action_focus_id = FOCUS_ID_CARD_ACTION;
    inkcell_fb_card_row_text(&card, INKCELL_TONE_NORMAL, gallery_id(GALLERY_STR_READ_CHARGE),
                             "82%");
    inkcell_fb_card_row_text(&card, INKCELL_TONE_NORMAL, gallery_id(GALLERY_STR_READ_SIGNAL),
                             "-71 dBm");
    inkcell_fb_card_action(&card, gallery_id(GALLERY_STR_ACT_RETRY), false);
    inkcell_fb_card_action(&card, gallery_id(GALLERY_STR_ACT_OPEN), false);
    (void)inkcell_fb_draw_card(state, &layout, &y, &card);

    return layout;
}

void gallery_scene_focus(struct inkcell_backend_fb_state *state) {
    struct inkcell_focus_item storage[FOCUS_STORAGE];
    struct inkcell_focus_map map;
    struct inkcell_fb_layout layout =
        focus_screen(state, GALLERY_STR_HEAD_FOCUS, FOCUS_CURSOR, &map, storage, FOCUS_STORAGE);

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

/*
 * The same screen, with the ring halfway through a move.
 *
 * A still picture of something that moves has to be *scripted*, and the script is the two
 * frames the gallery already renders: the first puts the cursor on the chip without animating
 * (`inkcell_fb_focus_ring_place()` - a screen opening is not a press) and then asks for the
 * key, which is what starts the journey; the second is drawn a fraction of a motion later and
 * is the picture. The clock is what tells them apart, and it is named rather than read, so the
 * ring is at the same point of the same curve on every host that renders this.
 *
 * What the picture shows that no other page can: the ring is a pill at one end and a rounded
 * square at the other, so halfway across it is neither - it is *becoming* the shape of the
 * thing it is landing on. The key it is heading for already carries the cursor's fill, which is
 * the honest depiction of a press: the screen's cursor moved when the button was pressed, and
 * the ring is what is catching up.
 */
/*
 * A hairline where the journey starts and where it ends.
 *
 * A still of something moving needs its endpoints drawn or it is a still of something sitting
 * in an odd place: without these the ring above reads as a badly aligned outline around key 2
 * rather than as a ring on its way from the chip to key 6. They are the diagram's furniture,
 * not the component's - nothing outside this page draws them, and a screen that did would be
 * telling the reader where the cursor is not.
 */
static void focus_ghost(const struct inkcell_backend_fb_state *state,
                        const struct inkcell_focus_map *map, uint32_t id) {
    struct inkcell_focus_rect box = {0, 0, 0, 0};
    if (!inkcell_focus_rect_of(map, id, &box)) {
        return;
    }
    const int inset = inkcell_fb_edge(state);
    inkcell_fb_stroke_round_rect(state, box.x - inset, box.y - inset, box.w + 2 * inset,
                                 box.h + 2 * inset, inkcell_focus_radius_of(map, id) + inset,
                                 inkcell_fb_edge(state),
                                 inkcell_fb_color(state, INKCELL_COLOR_OUTLINE));
}

void gallery_scene_focus_ring(struct inkcell_backend_fb_state *state) {
    struct inkcell_focus_item storage[FOCUS_STORAGE];
    struct inkcell_focus_map map;
    struct inkcell_fb_layout layout = focus_screen(state, GALLERY_STR_HEAD_FOCUS_RING,
                                                   FOCUS_ID_RING_TO, &map, storage, FOCUS_STORAGE);

    focus_ghost(state, &map, FOCUS_ID_RING_FROM);
    focus_ghost(state, &map, FOCUS_ID_RING_TO);

    if (state->now_ms <= GALLERY_CLOCK_MS) {
        inkcell_fb_focus_ring_place(state, &map, FOCUS_ID_RING_FROM);
    }
    inkcell_fb_draw_focus_ring(state, &map, FOCUS_ID_RING_TO);

    gallery_footer(state, &layout);
}
