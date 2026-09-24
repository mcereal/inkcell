#define _POSIX_C_SOURCE 200809L

/*
 * A list's look: what inkcell_fb_list_begin_styled() promises, held here rather than in the
 * gallery's pictures.
 *
 * The golden sheet is what shows the looks are *right*; this is what holds the arithmetic they
 * rest on, which a picture can agree with by accident. Three promises matter most:
 *
 *   - a zeroed style is the list every screen already had, field for field - the whole reason
 *     a screen can move to a look one list at a time;
 *   - a density is a step, so the window and every row measure from it rather than from a row
 *     that grew on its own;
 *   - the accessory column and a section's corners are properties of *where a row stands*, and
 *     every row in the same place answers the same.
 */

#include "framework/inkcell_test.h"

#include "inkcell/ui/fb.h"
#include "inkcell/ui/fb_capture.h"
#include "inkcell/ui/focus.h"
#include "inkcell/ui/widgets.h"

#include <stdlib.h>
#include <string.h>

#define STYLE_FOCUS_STORAGE 32U

enum { STYLE_ID_ROW = 1000 };

struct style_harness {
    struct inkcell_capture *capture;
    struct inkcell_draw_state *state;
    struct inkcell_fb_layout layout;
    struct inkcell_focus_item storage[STYLE_FOCUS_STORAGE];
    struct inkcell_focus_map map;
};

static bool style_harness_open(struct style_harness *h, bool with_map) {
    h->capture = NULL;
    if (inkcell_capture_open(&h->capture, INKCELL_CAPTURE_WIDTH, INKCELL_CAPTURE_HEIGHT,
                             INKCELL_SCALE(2)) < 0) {
        return false;
    }
    h->state = inkcell_capture_state(h->capture);
    if (with_map) {
        inkcell_focus_begin(&h->map, h->storage, STYLE_FOCUS_STORAGE);
        inkcell_fb_set_focus_map(h->state, &h->map);
    }
    inkcell_fb_clear(h->state, inkcell_fb_color(h->state, INKCELL_COLOR_BG));
    h->layout = inkcell_fb_layout_begin(h->state, false, false);
    return true;
}

static void style_harness_close(struct style_harness *h) {
    inkcell_capture_close(h->capture);
}

/* The colour at one pixel of the page, read back through the capture's own format. */
static struct inkcell_rgb style_pixel(const struct style_harness *h, int x, int y) {
    uint32_t width = 0U;
    uint32_t height = 0U;
    size_t stride = 0U;
    const uint8_t *pixels = inkcell_capture_pixels(h->capture, &width, &height, &stride);
    const uint8_t *p = pixels + (size_t)y * stride + (size_t)x * 4U;
    return (struct inkcell_rgb){.r = p[2], .g = p[1], .b = p[0]};
}

static bool style_same_rgb(struct inkcell_rgb a, struct inkcell_rgb b) {
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

/* ---- opening ------------------------------------------------------------------------------ */

INKCELL_TEST_CASE(list_style_zero_style_is_the_card_list_it_always_was, unit) {
    struct style_harness h;
    INKCELL_TEST_FAIL_IF(!style_harness_open(&h, false), "the capture should open");

    const uint8_t cards[] = {INKCELL_FB_LIST_NO_CARD, 0U, 0U, 0U, INKCELL_FB_LIST_NO_CARD, 1U};
    const uint32_t count = (uint32_t)sizeof cards;
    const struct inkcell_fb_list_style zero = {0};
    const struct inkcell_fb_list styled =
        inkcell_fb_list_begin_styled(h.state, &h.layout, count, 2U, NULL, cards, &zero);
    const struct inkcell_fb_list plain =
        inkcell_fb_list_begin_cards(&h.layout, count, 2U, NULL, cards);

    INKCELL_TEST_FAIL_IF_CLEANUP(
        styled.line != plain.line || styled.y != plain.y || styled.pad != 0,
        style_harness_close(&h), "a zeroed style should place its rows where a card list does");
    INKCELL_TEST_FAIL_IF_CLEANUP(
        styled.model.first != plain.model.first || styled.model.visible != plain.model.visible,
        style_harness_close(&h), "a zeroed style should open the same window");
    INKCELL_TEST_FAIL_IF_CLEANUP(styled.style.appearance != INKCELL_FB_LIST_CARD_GROUP ||
                                     plain.style.appearance != INKCELL_FB_LIST_CARD_GROUP,
                                 style_harness_close(&h),
                                 "a card array with no look asked for is a column of cards");
    for (uint32_t i = 0U; i < count; ++i) {
        INKCELL_TEST_FAIL_IF_CLEANUP(
            inkcell_fb_list_ground(&styled, i) != inkcell_fb_list_ground(&plain, i),
            style_harness_close(&h), "every item should stand on the ground it always did");
    }
    style_harness_close(&h);
    record_success(test_name);
}

INKCELL_TEST_CASE(list_style_a_list_with_no_cards_resolves_to_plain, unit) {
    struct style_harness h;
    INKCELL_TEST_FAIL_IF(!style_harness_open(&h, false), "the capture should open");
    const struct inkcell_fb_list styled =
        inkcell_fb_list_begin_styled(h.state, &h.layout, 4U, 0U, NULL, NULL, NULL);
    const struct inkcell_fb_list plain = inkcell_fb_list_begin(&h.layout, 4U, 0U);
    INKCELL_TEST_FAIL_IF_CLEANUP(styled.style.appearance != INKCELL_FB_LIST_PLAIN ||
                                     plain.style.appearance != INKCELL_FB_LIST_PLAIN,
                                 style_harness_close(&h), "no array and no look is the bare panel");
    INKCELL_TEST_FAIL_IF_CLEANUP(inkcell_fb_list_ground(&styled, 0U) != INKCELL_COLOR_BG,
                                 style_harness_close(&h), "a plain row stands on the panel");
    style_harness_close(&h);
    record_success(test_name);
}

/* ---- density ------------------------------------------------------------------------------ */

INKCELL_TEST_CASE(list_style_comfortable_steps_are_taller_and_fewer, unit) {
    struct style_harness h;
    INKCELL_TEST_FAIL_IF(!style_harness_open(&h, false), "the capture should open");

    const struct inkcell_fb_list_style comfortable = {.density = INKCELL_FB_LIST_COMFORTABLE};
    const struct inkcell_fb_list roomy =
        inkcell_fb_list_begin_styled(h.state, &h.layout, 400U, 0U, NULL, NULL, &comfortable);
    const struct inkcell_fb_list tight = inkcell_fb_list_begin(&h.layout, 400U, 0U);

    INKCELL_TEST_FAIL_IF_CLEANUP(roomy.pad <= 0, style_harness_close(&h),
                                 "a comfortable list should have air round its words");
    INKCELL_TEST_FAIL_IF_CLEANUP(roomy.line != tight.line + 2 * roomy.pad, style_harness_close(&h),
                                 "the step should be a text line with the air above and below");
    INKCELL_TEST_FAIL_IF_CLEANUP(roomy.y != tight.y + roomy.pad, style_harness_close(&h),
                                 "the first baseline should sit a pad below a compact one's");
    INKCELL_TEST_FAIL_IF_CLEANUP(roomy.model.visible >= tight.model.visible,
                                 style_harness_close(&h),
                                 "the same body should hold fewer comfortable steps");
    INKCELL_TEST_FAIL_IF_CLEANUP((int)roomy.model.visible * roomy.line > roomy.track_h,
                                 style_harness_close(&h),
                                 "the steps the window holds should fit inside the body");
    INKCELL_TEST_FAIL_IF_CLEANUP(roomy.track_h != tight.track_h, style_harness_close(&h),
                                 "the rail measures the body, which a density does not move");
    style_harness_close(&h);
    record_success(test_name);
}

/* ---- sections ----------------------------------------------------------------------------- */

INKCELL_TEST_CASE(list_style_section_ends_follow_the_card_array, unit) {
    struct style_harness h;
    INKCELL_TEST_FAIL_IF(!style_harness_open(&h, false), "the capture should open");

    /* A heading, a section of three, a heading, a section of one. */
    const uint8_t cards[] = {INKCELL_FB_LIST_NO_CARD, 0U, 0U, 0U, INKCELL_FB_LIST_NO_CARD, 1U};
    const bool want_first[] = {true, true, false, false, true, true};
    const bool want_last[] = {true, false, false, true, true, true};
    const struct inkcell_fb_list_style inset = {.appearance = INKCELL_FB_LIST_INSET_GROUPED};
    const struct inkcell_fb_list list = inkcell_fb_list_begin_styled(
        h.state, &h.layout, (uint32_t)sizeof cards, 0U, NULL, cards, &inset);
    for (uint32_t i = 0U; i < (uint32_t)sizeof cards; ++i) {
        bool first = false;
        bool last = false;
        inkcell_fb_list_section_ends(&list, i, &first, &last);
        INKCELL_TEST_FAIL_IF_CLEANUP(first != want_first[i] || last != want_last[i],
                                     style_harness_close(&h),
                                     "a row's ends should be where its section's are");
    }
    style_harness_close(&h);
    record_success(test_name);
}

INKCELL_TEST_CASE(list_style_a_grouped_list_with_no_array_is_one_section, unit) {
    struct style_harness h;
    INKCELL_TEST_FAIL_IF(!style_harness_open(&h, false), "the capture should open");

    const struct inkcell_fb_list_style inset = {.appearance = INKCELL_FB_LIST_INSET_GROUPED};
    const struct inkcell_fb_list list =
        inkcell_fb_list_begin_styled(h.state, &h.layout, 3U, 0U, NULL, NULL, &inset);
    bool first = false;
    bool last = false;
    inkcell_fb_list_section_ends(&list, 1U, &first, &last);
    INKCELL_TEST_FAIL_IF_CLEANUP(first || last, style_harness_close(&h),
                                 "the middle row of a one-section list opens and closes nothing");
    inkcell_fb_list_section_ends(&list, 0U, &first, &last);
    INKCELL_TEST_FAIL_IF_CLEANUP(!first || last, style_harness_close(&h),
                                 "the first row opens the section");
    INKCELL_TEST_FAIL_IF_CLEANUP(inkcell_fb_list_ground(&list, 1U) != INKCELL_COLOR_SURFACE,
                                 style_harness_close(&h), "a section's rows stand on its surface");
    style_harness_close(&h);
    record_success(test_name);
}

/*
 * The frame's ring lands on a section's end row in the section's corner, and on a middle row in
 * the row's. A ring that disagreed with the surface under it would be round where the section
 * is square, or square where it is round, on precisely the row the reader is looking at.
 */
INKCELL_TEST_CASE(list_style_inset_ends_register_the_section_corner, unit) {
    struct style_harness h;
    INKCELL_TEST_FAIL_IF(!style_harness_open(&h, true), "the capture should open");

    const struct inkcell_fb_list_style inset = {.appearance = INKCELL_FB_LIST_INSET_GROUPED};
    struct inkcell_fb_list list =
        inkcell_fb_list_begin_styled(h.state, &h.layout, 3U, 0U, NULL, NULL, &inset);
    inkcell_fb_list_focus(&list, STYLE_ID_ROW);
    uint32_t index = 0U;
    while (inkcell_fb_list_next(&list, &index)) {
        const struct inkcell_fb_list_item item = {.text = "row"};
        inkcell_fb_list_item(h.state, &list, index, &item);
    }
    const int md = inkcell_fb_radius(h.state, INKCELL_SHAPE_MD);
    const int sm = inkcell_fb_radius(h.state, INKCELL_SHAPE_SM);
    INKCELL_TEST_FAIL_IF_CLEANUP(inkcell_focus_radius_of(&h.map, STYLE_ID_ROW) != md ||
                                     inkcell_focus_radius_of(&h.map, STYLE_ID_ROW + 2U) != md,
                                 style_harness_close(&h),
                                 "a section's end rows should register its corner");
    INKCELL_TEST_FAIL_IF_CLEANUP(inkcell_focus_radius_of(&h.map, STYLE_ID_ROW + 1U) != sm,
                                 style_harness_close(&h),
                                 "a middle row should register the row's own corner");
    style_harness_close(&h);
    record_success(test_name);
}

/* ---- the accessory column ----------------------------------------------------------------- */

/*
 * A switch on a row that reserves the accessory column stands exactly where it does on a row
 * that fills it with a chevron, and further in than on a row that reserves nothing. The switch
 * writes its rect back, which is what makes the column readable from here.
 */
INKCELL_TEST_CASE(list_style_the_accessory_column_is_one_column, unit) {
    struct style_harness h;
    INKCELL_TEST_FAIL_IF(!style_harness_open(&h, false), "the capture should open");

    struct inkcell_fb_switch switches[3];
    memset(switches, 0, sizeof switches);
    for (uint32_t i = 0U; i < 3U; ++i) {
        switches[i].id = 0x5100U + i;
        switches[i].family = INKCELL_FAMILY_PRIMARY;
    }
    const struct inkcell_fb_list_item items[3] = {
        {.text = "reserved",
         .trailing = {.kind = INKCELL_FB_TRAILING_SWITCH, .sw = &switches[0]},
         .accessory_slot = true},
        {.text = "disclosed",
         .trailing = {.kind = INKCELL_FB_TRAILING_SWITCH, .sw = &switches[1]},
         .accessory = INKCELL_FB_ACCESSORY_DISCLOSURE},
        {.text = "bare", .trailing = {.kind = INKCELL_FB_TRAILING_SWITCH, .sw = &switches[2]}},
    };
    struct inkcell_fb_list list = inkcell_fb_list_begin(&h.layout, 3U, 0U);
    uint32_t index = 0U;
    while (inkcell_fb_list_next(&list, &index)) {
        inkcell_fb_list_item(h.state, &list, index, &items[index]);
    }
    INKCELL_TEST_FAIL_IF_CLEANUP(switches[0].rect.x != switches[1].rect.x, style_harness_close(&h),
                                 "a reserved column and a filled one should be the same column");
    INKCELL_TEST_FAIL_IF_CLEANUP(switches[2].rect.x <= switches[0].rect.x, style_harness_close(&h),
                                 "a row reserving no column should put its control at the edge");
    style_harness_close(&h);
    record_success(test_name);
}

/* ---- words -------------------------------------------------------------------------------- */

/*
 * A value is fitted to the pixels its column has, not to a count of cells.
 *
 * The UI face is proportional, so a cell count is a nominal width: a run of wide letters - a
 * base64 key is the case that showed it - is wider than the same number of average ones, and a
 * value clipped to its cells alone ran out past the row's edge and off the card it stood on.
 */
INKCELL_TEST_CASE(list_style_a_wide_value_stays_inside_its_row, unit) {
    struct style_harness h;
    INKCELL_TEST_FAIL_IF(!style_harness_open(&h, false), "the capture should open");

    char wide[160];
    memset(wide, 'W', sizeof wide - 1U);
    wide[sizeof wide - 1U] = '\0';
    const struct inkcell_fb_list_item item = {
        .label = "Public key", .label_cols = 12U, .label_quiet = true, .value = wide};
    struct inkcell_fb_list list = inkcell_fb_list_begin(&h.layout, 1U, 0U);
    uint32_t index = 0U;
    while (inkcell_fb_list_next(&list, &index)) {
        inkcell_fb_list_item(h.state, &list, index, &item);
    }

    const struct inkcell_fb_row_box box = inkcell_fb_row_box(h.state);
    const struct inkcell_rgb bg = inkcell_fb_color(h.state, INKCELL_COLOR_BG);
    for (int y = 0; y < (int)INKCELL_CAPTURE_HEIGHT; ++y) {
        for (int x = box.x + box.w; x < (int)INKCELL_CAPTURE_WIDTH; ++x) {
            INKCELL_TEST_FAIL_IF_CLEANUP(!style_same_rgb(style_pixel(&h, x, y), bg),
                                         style_harness_close(&h),
                                         "a value should not draw past its row's edge");
        }
    }
    style_harness_close(&h);
    record_success(test_name);
}

INKCELL_TEST_CASE(list_style_a_fitted_label_column_holds_its_widest_label, unit) {
    struct style_harness h;
    INKCELL_TEST_FAIL_IF(!style_harness_open(&h, false), "the capture should open");
    const int adv = inkcell_fb_char_adv(h.state, h.state->scale);

    const char *const labels[] = {"ID", "Relayed by", NULL, "Hops"};
    const size_t cols = inkcell_fb_field_label_cols_fit(h.state, &h.layout, labels, 4U);
    const int widest = inkcell_fb_text_width(h.state, "Relayed by", h.state->scale);
    INKCELL_TEST_FAIL_IF_CLEANUP((int)cols * adv < widest, style_harness_close(&h),
                                 "the column should hold the widest label's ink");
    INKCELL_TEST_FAIL_IF_CLEANUP((int)(cols - 1U) * adv >= widest, style_harness_close(&h),
                                 "and be no wider than the whole cells that takes");

    char wide[160];
    memset(wide, 'W', sizeof wide - 1U);
    wide[sizeof wide - 1U] = '\0';
    const char *const long_one[] = {wide};
    INKCELL_TEST_FAIL_IF_CLEANUP(
        inkcell_fb_field_label_cols_fit(h.state, &h.layout, long_one, 1U) != h.layout.cols / 2U,
        style_harness_close(&h), "a label wider than half the body is held to half");
    INKCELL_TEST_FAIL_IF_CLEANUP(inkcell_fb_field_label_cols_fit(h.state, &h.layout, NULL, 0U) !=
                                     1U,
                                 style_harness_close(&h), "no labels is one cell, never none");
    style_harness_close(&h);
    record_success(test_name);
}

/* The ink one plain row's label leaves on the panel, with the label column `cols` cells wide. */
static long style_label_ink(const char *label, size_t cols) {
    struct style_harness h;
    if (!style_harness_open(&h, false)) {
        return -1;
    }
    const struct inkcell_fb_list_item item = {.label = label, .label_cols = cols, .value = ""};
    struct inkcell_fb_list list = inkcell_fb_list_begin(&h.layout, 1U, 0U);
    uint32_t index = 0U;
    while (inkcell_fb_list_next(&list, &index)) {
        inkcell_fb_list_item(h.state, &list, index, &item);
    }
    /* Against the row's own ground, sampled at its far end where the empty value leaves it
       bare: the row is the cursor's, so its fill is not the panel's. */
    const struct inkcell_fb_row_box box = inkcell_fb_row_box(h.state);
    long ink = 0;
    const int top = h.layout.body_y;
    for (int y = top; y < top + h.layout.line && y < (int)INKCELL_CAPTURE_HEIGHT; ++y) {
        const struct inkcell_rgb ground = style_pixel(&h, box.x + box.w - 2, y);
        for (int x = box.x; x < box.x + box.w && x < (int)INKCELL_CAPTURE_WIDTH; ++x) {
            ink += style_same_rgb(style_pixel(&h, x, y), ground) ? 0 : 1;
        }
    }
    style_harness_close(&h);
    return ink;
}

INKCELL_TEST_CASE(list_style_a_label_is_fitted_by_ink_not_by_count, unit) {
    /* Six narrow letters in a column of four cells: they fit by ink and would not by count, so a
       count cut them to the four a shorter label draws. */
    struct style_harness h;
    INKCELL_TEST_FAIL_IF(!style_harness_open(&h, false), "the capture should open");
    const int room = 4 * inkcell_fb_char_adv(h.state, h.state->scale);
    const int six = inkcell_fb_text_width(h.state, "iiiiii", h.state->scale);
    style_harness_close(&h);
    INKCELL_TEST_FAIL_IF(six > room, "the premise: six narrow letters fit four cells of ink");
    INKCELL_TEST_FAIL_IF(style_label_ink("iiiiii", 4U) <= style_label_ink("iiii", 4U),
                         "a label that fits its column by ink should be drawn whole");
    record_success(test_name);
}

/* Draws one plain row whose label column is one cell wide, and copies out the band just past
   that cell: whatever a one-cell label spilled into. */
static bool style_one_cell_label(const char *label, struct inkcell_rgb *band, size_t band_len,
                                 int *band_w) {
    struct style_harness h;
    if (!style_harness_open(&h, false)) {
        return false;
    }
    const struct inkcell_fb_list_item item = {.label = label, .label_cols = 1U, .value = ""};
    struct inkcell_fb_list list = inkcell_fb_list_begin(&h.layout, 1U, 0U);
    uint32_t index = 0U;
    while (inkcell_fb_list_next(&list, &index)) {
        inkcell_fb_list_item(h.state, &list, index, &item);
    }
    const struct inkcell_fb_row_box box = inkcell_fb_row_box(h.state);
    const int adv = inkcell_fb_char_adv(h.state, h.state->scale);
    *band_w = adv;
    size_t n = 0U;
    for (int y = 0; y < (int)INKCELL_CAPTURE_HEIGHT && n < band_len; ++y) {
        for (int x = box.text_x + adv; x < box.text_x + 2 * adv && n < band_len; ++x) {
            band[n++] = style_pixel(&h, x, y);
        }
    }
    style_harness_close(&h);
    return true;
}

/* And when the room is a single cell and the one character in it is wider than a cell, the
   character goes: a piece fitted to pixels can come out empty, and an empty label is the honest
   answer to a column too narrow to hold its first letter. */
INKCELL_TEST_CASE(list_style_a_wide_character_leaves_a_one_cell_column_empty, unit) {
    static struct inkcell_rgb wide[INKCELL_CAPTURE_HEIGHT * 64U];
    static struct inkcell_rgb none[INKCELL_CAPTURE_HEIGHT * 64U];
    int w = 0;
    INKCELL_TEST_FAIL_IF(!style_one_cell_label("W", wide, sizeof wide / sizeof wide[0], &w) ||
                             !style_one_cell_label("", none, sizeof none / sizeof none[0], &w),
                         "the capture should open");
    INKCELL_TEST_FAIL_IF(w <= 0 || (size_t)w > 64U, "a cell should be a sane width");
    for (size_t i = 0U; i < (size_t)w * INKCELL_CAPTURE_HEIGHT; ++i) {
        INKCELL_TEST_FAIL_IF(!style_same_rgb(wide[i], none[i]),
                             "a one-cell label should not spill past its cell");
    }
    record_success(test_name);
}

/* ---- focus -------------------------------------------------------------------------------- */

/* Where a focused one-line row's box is on the panel: its left edge and its middle. */
static void style_row_probe(const struct style_harness *h, const struct inkcell_fb_list *list,
                            int *x, int *y) {
    const struct inkcell_fb_row_box box = inkcell_fb_row_box(h->state);
    *x = box.x;
    *y = list->y - inkcell_step_px(h->state->scale) - list->pad + list->line / 2;
}

/* Draws one focused row on a list with `focus`, and reports the colour at its left edge and at
   a point inside it that no word reaches. */
static bool style_focused_row(enum inkcell_fb_list_focus focus, bool with_map,
                              struct inkcell_rgb *edge, struct inkcell_rgb *inside,
                              struct inkcell_rgb *bg) {
    struct style_harness h;
    if (!style_harness_open(&h, with_map)) {
        return false;
    }
    const struct inkcell_fb_list_style look = {.focus = focus};
    struct inkcell_fb_list list =
        inkcell_fb_list_begin_styled(h.state, &h.layout, 1U, 0U, NULL, NULL, &look);
    if (with_map) {
        inkcell_fb_list_focus(&list, STYLE_ID_ROW);
    }
    int x = 0;
    int y = 0;
    style_row_probe(&h, &list, &x, &y);
    const struct inkcell_fb_row_box box = inkcell_fb_row_box(h.state);
    uint32_t index = 0U;
    while (inkcell_fb_list_next(&list, &index)) {
        const struct inkcell_fb_list_item item = {.text = "r"};
        inkcell_fb_list_item(h.state, &list, index, &item);
    }
    *edge = style_pixel(&h, x, y);
    *inside = style_pixel(&h, box.x + (box.w * 3) / 4, y);
    *bg = inkcell_fb_color(h.state, INKCELL_COLOR_BG);
    style_harness_close(&h);
    return true;
}

INKCELL_TEST_CASE(list_style_a_ring_leaves_the_row_its_own_ground, unit) {
    struct inkcell_rgb edge;
    struct inkcell_rgb inside;
    struct inkcell_rgb bg;
    INKCELL_TEST_FAIL_IF(!style_focused_row(INKCELL_FB_LIST_FOCUS_RING, false, &edge, &inside, &bg),
                         "the capture should open");
    INKCELL_TEST_FAIL_IF(!style_same_rgb(inside, bg),
                         "a ring should lay nothing under the row's words");
    INKCELL_TEST_FAIL_IF(style_same_rgb(edge, bg), "a ring should be drawn at the row's edge");

    INKCELL_TEST_FAIL_IF(!style_focused_row(INKCELL_FB_LIST_FOCUS_FILL, false, &edge, &inside, &bg),
                         "the capture should open");
    INKCELL_TEST_FAIL_IF(style_same_rgb(inside, bg), "a fill should be laid under the words");
    record_success(test_name);
}

/* One cursor, one ring: a list registered with a focus map leaves the ring to the frame. */
INKCELL_TEST_CASE(list_style_a_ring_yields_to_the_frames_ring, unit) {
    struct inkcell_rgb edge;
    struct inkcell_rgb inside;
    struct inkcell_rgb bg;
    INKCELL_TEST_FAIL_IF(!style_focused_row(INKCELL_FB_LIST_FOCUS_RING, true, &edge, &inside, &bg),
                         "the capture should open");
    INKCELL_TEST_FAIL_IF(!style_same_rgb(edge, bg) || !style_same_rgb(inside, bg),
                         "a row on a focus map should draw no ring of its own");
    record_success(test_name);
}

INKCELL_TEST_CASE(list_style_an_accent_is_lighter_than_a_fill, unit) {
    struct inkcell_rgb edge;
    struct inkcell_rgb accent;
    struct inkcell_rgb fill;
    struct inkcell_rgb bg;
    INKCELL_TEST_FAIL_IF(
        !style_focused_row(INKCELL_FB_LIST_FOCUS_ACCENT, false, &edge, &accent, &bg),
        "the capture should open");
    INKCELL_TEST_FAIL_IF(!style_focused_row(INKCELL_FB_LIST_FOCUS_FILL, false, &edge, &fill, &bg),
                         "the capture should open");
    const int accent_d = abs((int)accent.r - (int)bg.r) + abs((int)accent.g - (int)bg.g) +
                         abs((int)accent.b - (int)bg.b);
    const int fill_d =
        abs((int)fill.r - (int)bg.r) + abs((int)fill.g - (int)bg.g) + abs((int)fill.b - (int)bg.b);
    INKCELL_TEST_FAIL_IF(accent_d == 0, "an accent row should still be lifted off the ground");
    INKCELL_TEST_FAIL_IF(accent_d >= fill_d,
                         "an accent row's layer should sit nearer the ground than a fill");
    record_success(test_name);
}

/*
 * An inset section's end rows are marked out to the section's edges, whatever their height, and
 * the box the frame's ring is given is that same box. A two-line last row marks a fill shorter
 * than its steps on purpose, so this is the case where the mark and the section could part.
 */
INKCELL_TEST_CASE(list_style_inset_end_rows_register_out_to_the_section_edge, unit) {
    struct style_harness h;
    INKCELL_TEST_FAIL_IF(!style_harness_open(&h, true), "the capture should open");

    /* The body a few lines down the panel, as an app bar would leave it, so the first row's box
       is not clipped by the panel's own top edge before the map is asked about it. */
    h.layout.body_y += 4 * h.layout.line;
    h.layout.rows -= 4U;
    const uint8_t heights[] = {2U, 1U, 2U};
    const struct inkcell_fb_list_style inset = {.appearance = INKCELL_FB_LIST_INSET_GROUPED};
    struct inkcell_fb_list list =
        inkcell_fb_list_begin_styled(h.state, &h.layout, 3U, 0U, heights, NULL, &inset);
    inkcell_fb_list_focus(&list, STYLE_ID_ROW);
    int box_top[3] = {0, 0, 0};
    uint32_t index = 0U;
    while (inkcell_fb_list_next(&list, &index)) {
        box_top[index] = list.y - inkcell_step_px(h.state->scale) - list.pad;
        const struct inkcell_fb_list_item item = {.text = "row", .supporting = "second"};
        inkcell_fb_list_item(h.state, &list, index, &item);
    }
    struct inkcell_focus_rect first = {0, 0, 0, 0};
    struct inkcell_focus_rect middle = {0, 0, 0, 0};
    struct inkcell_focus_rect last = {0, 0, 0, 0};
    INKCELL_TEST_FAIL_IF_CLEANUP(!inkcell_focus_rect_of(&h.map, STYLE_ID_ROW, &first) ||
                                     !inkcell_focus_rect_of(&h.map, STYLE_ID_ROW + 1U, &middle) ||
                                     !inkcell_focus_rect_of(&h.map, STYLE_ID_ROW + 2U, &last),
                                 style_harness_close(&h), "every row should be registered");
    INKCELL_TEST_FAIL_IF_CLEANUP(first.y != box_top[0], style_harness_close(&h),
                                 "the first row should be marked from the section's top");
    INKCELL_TEST_FAIL_IF_CLEANUP(last.y + last.h <= box_top[2] + 2 * list.line,
                                 style_harness_close(&h),
                                 "a two-line last row should be marked past its steps, into the "
                                 "section's inset");
    INKCELL_TEST_FAIL_IF_CLEANUP(middle.y + middle.h > box_top[2], style_harness_close(&h),
                                 "a middle row should keep to its own steps");
    style_harness_close(&h);
    record_success(test_name);
}

/*
 * An accent list keeps its cue on the row: the row is still a target a press and a pointer
 * reach, but it is not the frame's mark, so the travelling ring does not circle a row that has
 * already said where the cursor is. A fill list, which has no capsule, still hands the ring the
 * row.
 */
INKCELL_TEST_CASE(list_style_an_accent_row_is_not_the_frames_mark, unit) {
    const enum inkcell_fb_list_focus looks[] = {INKCELL_FB_LIST_FOCUS_ACCENT,
                                                INKCELL_FB_LIST_FOCUS_FILL};
    for (size_t i = 0; i < sizeof looks / sizeof looks[0]; ++i) {
        struct style_harness h;
        INKCELL_TEST_FAIL_IF(!style_harness_open(&h, true), "the capture should open");
        const struct inkcell_fb_list_style look = {.focus = looks[i]};
        struct inkcell_fb_list list =
            inkcell_fb_list_begin_styled(h.state, &h.layout, 2U, 1U, NULL, NULL, &look);
        inkcell_fb_list_focus(&list, STYLE_ID_ROW);
        uint32_t index = 0U;
        while (inkcell_fb_list_next(&list, &index)) {
            const struct inkcell_fb_list_item item = {.text = "row"};
            inkcell_fb_list_item(h.state, &list, index, &item);
        }
        const bool registered = inkcell_focus_has(&h.map, STYLE_ID_ROW + 1U);
        const uint32_t marked = inkcell_focus_marked(&h.map);
        style_harness_close(&h);
        INKCELL_TEST_FAIL_IF(!registered, "the cursor's row should still be a target");
        if (looks[i] == INKCELL_FB_LIST_FOCUS_ACCENT) {
            INKCELL_TEST_FAIL_IF(marked != INKCELL_FOCUS_NONE,
                                 "an accent row should not be the frame's mark");
        } else {
            INKCELL_TEST_FAIL_IF(marked != STYLE_ID_ROW + 1U,
                                 "a filled row should be the frame's mark");
        }
    }
    record_success(test_name);
}

/*
 * And over something else that marked: an accent list drawn after a filled one - a sheet of
 * accent rows over a page whose row took the ring - moves the cursor off the page. Declining to
 * mark would leave the ring on the row underneath while the sheet's row shows its own cue.
 */
INKCELL_TEST_CASE(list_style_an_accent_row_takes_the_mark_from_what_is_beneath, unit) {
    struct style_harness h;
    INKCELL_TEST_FAIL_IF(!style_harness_open(&h, true), "the capture should open");

    const struct inkcell_fb_list_style fill = {.focus = INKCELL_FB_LIST_FOCUS_FILL};
    struct inkcell_fb_list page =
        inkcell_fb_list_begin_styled(h.state, &h.layout, 2U, 0U, NULL, NULL, &fill);
    inkcell_fb_list_focus(&page, STYLE_ID_ROW);
    uint32_t index = 0U;
    while (inkcell_fb_list_next(&page, &index)) {
        const struct inkcell_fb_list_item item = {.text = "page"};
        inkcell_fb_list_item(h.state, &page, index, &item);
    }
    const uint32_t under = inkcell_focus_marked(&h.map);

    const struct inkcell_fb_list_style accent = {.focus = INKCELL_FB_LIST_FOCUS_ACCENT};
    struct inkcell_fb_list sheet =
        inkcell_fb_list_begin_styled(h.state, &h.layout, 2U, 1U, NULL, NULL, &accent);
    inkcell_fb_list_focus(&sheet, STYLE_ID_ROW + 100U);
    while (inkcell_fb_list_next(&sheet, &index)) {
        const struct inkcell_fb_list_item item = {.text = "sheet"};
        inkcell_fb_list_item(h.state, &sheet, index, &item);
    }
    const uint32_t over = inkcell_focus_marked(&h.map);

    /* And a plain mark after it takes the ring back. */
    inkcell_focus_mark(&h.map, STYLE_ID_ROW);
    const uint32_t again = inkcell_focus_marked(&h.map);
    style_harness_close(&h);

    INKCELL_TEST_FAIL_IF(under != STYLE_ID_ROW, "the filled page's row should take the ring first");
    INKCELL_TEST_FAIL_IF(over != INKCELL_FOCUS_NONE,
                         "the ring should leave the page once an accent sheet's row is marked");
    INKCELL_TEST_FAIL_IF(again != STYLE_ID_ROW, "a plain mark after it should take the ring back");
    record_success(test_name);
}
