#define _POSIX_C_SOURCE 200809L

/*
 * The list gliding between two windows, measured rather than looked at.
 *
 * A scroll is a few pixels of displacement that decay over a motion token, and a picture of one
 * would pin the easing curve to a digest. What every case here does instead is draw a real list
 * into a capture with a focus map under it, and read the rows' boxes back out: the map already
 * holds where every row came out, so the list can be asked where it drew things without
 * anything having to inspect a panel.
 *
 * The clock is named rather than read, for the reason every other animated thing in this tree
 * names it: a case whose answers depend on how fast the host got to the next frame is not one.
 */

#include "framework/inkcell_test.h"

#include "inkcell/ui/fb_capture.h"
#include "inkcell/ui/focus.h"
#include "inkcell/ui/widgets.h"

#include <limits.h>

#define GLIDE_STORAGE 64U
#define GLIDE_T0 1000U
#define GLIDE_ID 7U
#define GLIDE_ROWS 500U
#define GLIDE_ITEMS 400U

struct glide_harness {
    struct inkcell_capture *capture;
    struct inkcell_draw_state *state;
    struct inkcell_focus_item storage[GLIDE_STORAGE];
    struct inkcell_focus_map map;
};

static bool glide_open(struct glide_harness *h) {
    h->capture = NULL;
    if (inkcell_capture_open(&h->capture, INKCELL_CAPTURE_WIDTH, INKCELL_CAPTURE_HEIGHT,
                             INKCELL_SCALE(4)) < 0) {
        return false;
    }
    h->state = inkcell_capture_state(h->capture);
    inkcell_fb_state_set_now(h->state, GLIDE_T0);
    return true;
}

static void glide_close(struct glide_harness *h) {
    inkcell_capture_close(h->capture);
}

/*
 * One frame of a list of four hundred rows, with the cursor wherever the caller says.
 *
 * `id` of 0 draws a list that does not glide, which is what every screen written before this
 * existed draws and what the settled positions are measured from.
 */
static uint32_t glide_frame(struct glide_harness *h, uint32_t cursor, uint32_t id) {
    inkcell_focus_begin(&h->map, h->storage, GLIDE_STORAGE);
    inkcell_fb_set_focus_map(h->state, &h->map);
    inkcell_fb_app_frame_begin(h->state);

    struct inkcell_fb_layout layout = inkcell_fb_layout_begin(h->state, false, false);
    struct inkcell_fb_list list = inkcell_fb_list_begin(&layout, GLIDE_ITEMS, cursor);
    if (id != 0U) {
        inkcell_fb_list_glide(h->state, &list, id);
    }
    inkcell_fb_list_focus(&list, GLIDE_ROWS);

    uint32_t index = 0U;
    uint32_t drawn = 0U;
    while (inkcell_fb_list_next(&list, &index)) {
        inkcell_fb_list_row(h->state, &list, index, "row", INKCELL_TONE_NORMAL);
        drawn += 1U;
    }
    return drawn;
}

/* Where item `index` came out on the last frame, or INT_MIN when it was not drawn. */
static int glide_row_y(const struct glide_harness *h, uint32_t index) {
    struct inkcell_focus_rect rect = {0, 0, 0, 0};
    if (!inkcell_focus_rect_of(&h->map, GLIDE_ROWS + index, &rect)) {
        return INT_MIN;
    }
    return rect.y;
}

/*
 * One pixel of the page as it came out.
 *
 * Two of the cases below cannot be answered from the focus map, because what they are about is
 * what was *painted* - a divider that should be there, and a row of the body that should not
 * have been touched. The capture is the same page the golden sheet digests, so reading a pixel
 * out of it is the same question that sheet asks, narrowed to one place and one colour.
 */
static bool glide_pixel_is(const struct glide_harness *h, int x, int y, struct inkcell_rgb want) {
    uint32_t width = 0U;
    uint32_t height = 0U;
    size_t stride = 0U;
    const uint8_t *pixels = inkcell_capture_pixels(h->capture, &width, &height, &stride);
    if (pixels == NULL || x < 0 || y < 0 || (uint32_t)x >= width || (uint32_t)y >= height) {
        return false;
    }
    const uint8_t *p = pixels + ((size_t)y * stride) + ((size_t)x * 4U);
    return p[0] == want.b && p[1] == want.g && p[2] == want.r;
}

static uint32_t glide_motion(const struct glide_harness *h) {
    return inkcell_fb_motion(h->state, INKCELL_FB_LIST_GLIDE_MOTION);
}

/* A screen opening is not a scroll: the first window a list is asked for is where it belongs. */
INKCELL_TEST_CASE(list_glide_adopts_the_first_window_it_is_given, unit) {
    struct glide_harness h;
    INKCELL_TEST_FAIL_IF(!glide_open(&h), "the capture should open");

    (void)glide_frame(&h, 0U, 0U);
    const int settled = glide_row_y(&h, 0U);
    (void)glide_frame(&h, 0U, GLIDE_ID);
    INKCELL_TEST_FAIL_IF_CLEANUP(glide_row_y(&h, 0U) != settled, glide_close(&h),
                                 "a list that has just been opened has nowhere to have come from");
    INKCELL_TEST_FAIL_IF_CLEANUP(inkcell_fb_state_animating(h.state), glide_close(&h),
                                 "and owes no frames for a glide it did not make");
    glide_close(&h);
    record_success(test_name);
}

/*
 * The case the whole thing is for: the window moves a row and the content does not jump with
 * it.
 *
 * Measured against where the same list draws when it is not gliding, so what is asserted is the
 * displacement rather than a coordinate - the body starts wherever the chrome left it, and a
 * case that hard-coded that would be a case about the app bar.
 */
INKCELL_TEST_CASE(list_glide_displaces_the_content_and_settles_it, unit) {
    struct glide_harness h;
    INKCELL_TEST_FAIL_IF(!glide_open(&h), "the capture should open");

    /* Settled positions, from a list that does not glide at all. */
    const uint32_t visible = glide_frame(&h, 0U, 0U);
    INKCELL_TEST_FAIL_IF_CLEANUP(visible < 4U, glide_close(&h), "the body should hold a few rows");
    /*
     * Measured on a row in the middle of the window rather than on the cursor's.
     *
     * The cursor's row is the one at the bottom, and on a forward glide it is still below the
     * body - it is what is sliding in. A row clipped away entirely is not registered, which is
     * the point of registering from the draw, so there would be nothing to measure there.
     */
    const uint32_t mid = visible / 2U;
    (void)glide_frame(&h, visible, 0U);
    const int settled = glide_row_y(&h, mid);

    /* And the same two frames on a list that glides: the window has moved by one item, so the
       row it moved to is drawn a row lower than it belongs and rises from there. */
    struct glide_harness g;
    INKCELL_TEST_FAIL_IF_CLEANUP(!glide_open(&g), glide_close(&h), "the second capture");
    (void)glide_frame(&g, 0U, GLIDE_ID);
    (void)glide_frame(&g, visible, GLIDE_ID);

    const int displaced = glide_row_y(&g, mid);
    INKCELL_TEST_FAIL_IF_CLEANUP(displaced <= settled, (glide_close(&h), glide_close(&g)),
                                 "the row should arrive below where it belongs");
    INKCELL_TEST_FAIL_IF_CLEANUP(!inkcell_fb_state_animating(g.state),
                                 (glide_close(&h), glide_close(&g)),
                                 "a list on the move owes another frame");

    /* Half a motion later it is part of the way there, and at the end it is exactly there. */
    inkcell_fb_state_set_now(g.state, GLIDE_T0 + glide_motion(&g) / 2U);
    (void)glide_frame(&g, visible, GLIDE_ID);
    const int midway = glide_row_y(&g, mid);
    INKCELL_TEST_FAIL_IF_CLEANUP(midway >= displaced || midway <= settled,
                                 (glide_close(&h), glide_close(&g)),
                                 "halfway through it should be between the two");

    inkcell_fb_state_set_now(g.state, GLIDE_T0 + glide_motion(&g));
    (void)glide_frame(&g, visible, GLIDE_ID);
    INKCELL_TEST_FAIL_IF_CLEANUP(glide_row_y(&g, mid) != settled,
                                 (glide_close(&h), glide_close(&g)),
                                 "and at the end it should be exactly where it belongs");
    INKCELL_TEST_FAIL_IF_CLEANUP(inkcell_fb_state_animating(g.state),
                                 (glide_close(&h), glide_close(&g)),
                                 "with nothing left to ask a frame for");
    glide_close(&h);
    glide_close(&g);
    record_success(test_name);
}

/*
 * The extra row.
 *
 * Content pushed down leaves a gap above the window, and a gap is the one thing a scroll must
 * not show: the item that is leaving has to be there to leave. The walk hands it to the screen
 * like any other index, which is what lets a screen keep building rows by number and know
 * nothing about any of this.
 */
INKCELL_TEST_CASE(list_glide_yields_the_row_sliding_out, unit) {
    struct glide_harness h;
    INKCELL_TEST_FAIL_IF(!glide_open(&h), "the capture should open");

    const uint32_t visible = glide_frame(&h, 0U, GLIDE_ID);
    const uint32_t drawn = glide_frame(&h, visible, GLIDE_ID);
    /*
     * More rows than the window holds, and not one more: a press does not move a window by a
     * row. The model keeps a few rows of context ahead of the cursor, so crossing the edge moves
     * it by several at once and the gap left behind is that many rows tall.
     */
    INKCELL_TEST_FAIL_IF_CLEANUP(drawn <= visible, glide_close(&h),
                                 "a gliding window draws more rows than it holds");

    /* The items above the window, drawn above the body - which is where the band cuts them. */
    const int lead = glide_row_y(&h, 0U);
    INKCELL_TEST_FAIL_IF_CLEANUP(lead == INT_MIN, glide_close(&h),
                                 "the row on its way out should have been drawn");
    INKCELL_TEST_FAIL_IF_CLEANUP(lead >= glide_row_y(&h, 1U), glide_close(&h),
                                 "and drawn above the row that follows it");
    /* Every row between the gap and the window is there too, or the glide shows bare panel. */
    for (uint32_t i = 1U; i < drawn - visible; ++i) {
        INKCELL_TEST_FAIL_IF_CLEANUP(glide_row_y(&h, i) == INT_MIN, glide_close(&h),
                                     "every row the gap covers should have been drawn");
    }
    glide_close(&h);
    record_success(test_name);
}

/* A window that went somewhere rather than stepping is not a glide: gliding a filter emptying
   or a jump to the end would smear the panel on the way to an answer nobody is reading. */
INKCELL_TEST_CASE(list_glide_refuses_a_leap, unit) {
    struct glide_harness h;
    INKCELL_TEST_FAIL_IF(!glide_open(&h), "the capture should open");

    const uint32_t visible = glide_frame(&h, 0U, 0U);
    (void)glide_frame(&h, GLIDE_ITEMS - 1U, 0U);
    const int settled = glide_row_y(&h, GLIDE_ITEMS - 1U);

    struct glide_harness g;
    INKCELL_TEST_FAIL_IF_CLEANUP(!glide_open(&g), glide_close(&h), "the second capture");
    (void)glide_frame(&g, 0U, GLIDE_ID);
    (void)glide_frame(&g, GLIDE_ITEMS - 1U, GLIDE_ID);
    INKCELL_TEST_FAIL_IF_CLEANUP(glide_row_y(&g, GLIDE_ITEMS - 1U) != settled,
                                 (glide_close(&h), glide_close(&g)),
                                 "a leap should land rather than travel");
    INKCELL_TEST_FAIL_IF_CLEANUP(inkcell_fb_state_animating(g.state),
                                 (glide_close(&h), glide_close(&g)), "and owe nothing afterwards");
    (void)visible;
    glide_close(&h);
    glide_close(&g);
    record_success(test_name);
}

/* One slot, so a second list on the same frame takes it over rather than inheriting a window
   that was never its own. */
INKCELL_TEST_CASE(list_glide_hands_the_slot_to_whichever_list_asks, unit) {
    struct glide_harness h;
    INKCELL_TEST_FAIL_IF(!glide_open(&h), "the capture should open");

    const uint32_t visible = glide_frame(&h, 0U, GLIDE_ID);
    /* Another list, at a window that has nothing to do with the first one's. */
    (void)glide_frame(&h, visible, GLIDE_ID + 1U);
    INKCELL_TEST_FAIL_IF_CLEANUP(inkcell_fb_state_animating(h.state), glide_close(&h),
                                 "a list that has just taken the slot has nowhere to come from");
    glide_close(&h);
    record_success(test_name);
}

/*
 * What the band cut away is not somewhere the cursor can be.
 *
 * A gliding row is clipped to the window, and a row still entirely outside it has not been
 * drawn - so registering its whole box would put an id at a rectangle nobody can see. The ring
 * is drawn outside the band and would then paint the proof of it over whatever is under the
 * list.
 */
INKCELL_TEST_CASE(list_glide_registers_only_what_the_window_kept, unit) {
    struct glide_harness h;
    INKCELL_TEST_FAIL_IF(!glide_open(&h), "the capture should open");

    const uint32_t visible = glide_frame(&h, 0U, GLIDE_ID);
    (void)glide_frame(&h, visible, GLIDE_ID);

    /* The body's own bottom edge, which is where the band cuts. Taken from a row that is in the
       window either way rather than from the layout, so this is a statement about the list. */
    struct inkcell_focus_rect top_row = {0, 0, 0, 0};
    INKCELL_TEST_FAIL_IF_CLEANUP(
        !inkcell_focus_rect_of(&h.map, GLIDE_ROWS + visible / 2U, &top_row), glide_close(&h),
        "a row in the middle should be registered");

    for (uint32_t i = 0U; i < GLIDE_ITEMS; ++i) {
        struct inkcell_focus_rect rect = {0, 0, 0, 0};
        if (!inkcell_focus_rect_of(&h.map, GLIDE_ROWS + i, &rect)) {
            continue;
        }
        INKCELL_TEST_FAIL_IF_CLEANUP(rect.h <= 0, glide_close(&h),
                                     "a row with nothing left of it should not be registered");
    }
    /* The cursor's own row is the one sliding in from below, and on this frame it is still
       outside the window - so it is not yet anywhere a cursor can be drawn. */
    INKCELL_TEST_FAIL_IF_CLEANUP(inkcell_focus_has(&h.map, GLIDE_ROWS + visible) &&
                                     !inkcell_fb_state_animating(h.state),
                                 glide_close(&h), "a row still below the body is not on the frame");
    glide_close(&h);
    record_success(test_name);
}

/*
 * A list given an explicit window glides inside *that*, not inside the body.
 *
 * inkcell_fb_list_begin_visible() is for a screen that reserved body rows for something else,
 * and a glide painting into them would be this list drawing over somebody else's content. The
 * reserved rows are marked in a colour no theme draws and every pixel of the mark has to
 * survive the list - a sample would not do, because what leaks into them is a row's worth of
 * whatever that row happened to be.
 */
static bool glide_marker_intact(const struct glide_harness *h, int top, struct inkcell_rgb mark) {
    uint32_t width = 0U;
    uint32_t height = 0U;
    size_t stride = 0U;
    const uint8_t *pixels = inkcell_capture_pixels(h->capture, &width, &height, &stride);
    if (pixels == NULL) {
        return false;
    }
    /*
     * The rows' own span, and not the gutter beside it: the scroll rail is measured against the
     * *body* rather than against the window and always has been, so it runs the whole height
     * whatever window a list was opened with. That is the rail's question, asked and answered
     * before any of this, and a case about where a glide paints should not be re-opening it.
     */
    const struct inkcell_fb_row_box box = inkcell_fb_row_box(h->state);
    for (uint32_t y = (uint32_t)top; y < height; ++y) {
        for (uint32_t x = (uint32_t)box.x; x < (uint32_t)(box.x + box.w) && x < width; ++x) {
            const uint8_t *p = pixels + ((size_t)y * stride) + ((size_t)x * 4U);
            if (p[0] != mark.b || p[1] != mark.g || p[2] != mark.r) {
                return false;
            }
        }
    }
    return true;
}

INKCELL_TEST_CASE(list_glide_keeps_out_of_rows_it_was_not_given, unit) {
    struct glide_harness h;
    INKCELL_TEST_FAIL_IF(!glide_open(&h), "the capture should open");

    const struct inkcell_rgb marker = {255U, 0U, 255U};
    const uint32_t window = 6U;
    int below = 0;

    for (uint32_t frame = 0U; frame < 2U; ++frame) {
        inkcell_focus_begin(&h.map, h.storage, GLIDE_STORAGE);
        inkcell_fb_set_focus_map(h.state, &h.map);
        inkcell_fb_app_frame_begin(h.state);

        struct inkcell_fb_layout layout = inkcell_fb_layout_begin(h.state, false, false);
        /* Everything below the window is somebody else's, and says so in a colour no theme
           draws. Painted before the list, every frame, so what is asked afterwards is only
           whether the list stayed out of it. */
        below = layout.body_y + (int)window * layout.line;
        inkcell_fb_fill_rect(h.state, 0, below, (int)INKCELL_CAPTURE_WIDTH,
                             (int)INKCELL_CAPTURE_HEIGHT - below, marker);

        struct inkcell_fb_list list =
            inkcell_fb_list_begin_visible(&layout, GLIDE_ITEMS, frame == 0U ? 0U : window, window);
        inkcell_fb_list_glide(h.state, &list, GLIDE_ID);
        uint32_t index = 0U;
        while (inkcell_fb_list_next(&list, &index)) {
            /* Rows that paint across their width: an unselected plain row is text and nothing
               else, and a leak nobody draws into is a leak this case would not see. */
            const struct inkcell_fb_list_item item = {
                .leading = {.kind = INKCELL_FB_LEADING_ICON, .icon = INKCELL_ICON_NODES},
                .text = "row",
                .tone = INKCELL_TONE_NORMAL,
                .divider = true,
            };
            inkcell_fb_list_item(h.state, &list, index, &item);
        }
        INKCELL_TEST_FAIL_IF_CLEANUP(frame == 1U && !inkcell_fb_state_animating(h.state),
                                     glide_close(&h), "the list should be gliding");
        INKCELL_TEST_FAIL_IF_CLEANUP(!glide_marker_intact(&h, below, marker), glide_close(&h),
                                     "the rows below the window are not this list's to paint in");
    }
    glide_close(&h);
    record_success(test_name);
}

/*
 * A row with more rows under it has a divider, and a row sliding in from below is more rows
 * under it.
 *
 * The item suppresses the separator on the last row of the window, which is right when the
 * window is all there is - and wrong for the length of every backward glide, where the rows
 * beneath it are the ones arriving. What that costs is a separator winking out at the seam on
 * each scroll, which no digest of a settled page would ever catch.
 */
INKCELL_TEST_CASE(list_glide_keeps_the_divider_at_the_seam, unit) {
    struct glide_harness h;
    INKCELL_TEST_FAIL_IF(!glide_open(&h), "the capture should open");

    uint32_t visible = 0U;
    uint32_t last_window_row = 0U;
    struct inkcell_focus_rect seam = {0, 0, 0, 0};

    /* Down the list, then back up it: a cursor moving backwards is what pulls the content up
       and brings rows in underneath. */
    for (uint32_t frame = 0U; frame < 2U; ++frame) {
        inkcell_focus_begin(&h.map, h.storage, GLIDE_STORAGE);
        inkcell_fb_set_focus_map(h.state, &h.map);
        inkcell_fb_app_frame_begin(h.state);

        struct inkcell_fb_layout layout = inkcell_fb_layout_begin(h.state, false, false);
        struct inkcell_fb_list list =
            inkcell_fb_list_begin(&layout, GLIDE_ITEMS, frame == 0U ? 30U : 27U);
        inkcell_fb_list_glide(h.state, &list, GLIDE_ID);
        inkcell_fb_list_focus(&list, GLIDE_ROWS);
        visible = list.model.visible;
        last_window_row = list.model.first + visible - 1U;

        uint32_t index = 0U;
        while (inkcell_fb_list_next(&list, &index)) {
            const struct inkcell_fb_list_item item = {
                .leading = {.kind = INKCELL_FB_LEADING_ICON, .icon = INKCELL_ICON_NODES},
                .text = "row",
                .tone = INKCELL_TONE_NORMAL,
                .divider = true,
            };
            inkcell_fb_list_item(h.state, &list, index, &item);
        }
        if (frame == 1U) {
            INKCELL_TEST_FAIL_IF_CLEANUP(!inkcell_fb_state_animating(h.state), glide_close(&h),
                                         "the list should be gliding backwards");
            INKCELL_TEST_FAIL_IF_CLEANUP(list.tail_count == 0U, glide_close(&h),
                                         "and pulling rows in under the window");
            INKCELL_TEST_FAIL_IF_CLEANUP(
                !inkcell_focus_rect_of(&h.map, GLIDE_ROWS + last_window_row, &seam),
                glide_close(&h), "the window's last row should be on the frame");
        }
    }

    /*
     * Where the item puts a separator: below the box it filled, by the same half-step it insets
     * every other gap by. Read from the map rather than worked out here, so this asks where the
     * row actually came out.
     */
    const struct inkcell_fb_row_box box = inkcell_fb_row_box(h.state);
    const int rule_y = seam.y + seam.h + inkcell_fb_space(h.state, INKCELL_SPACE_XS);
    const struct inkcell_rgb rule = inkcell_fb_color(h.state, INKCELL_COLOR_RULE);
    INKCELL_TEST_FAIL_IF_CLEANUP(!glide_pixel_is(&h, box.text_right - 8, rule_y, rule),
                                 glide_close(&h),
                                 "the row above the arriving ones should keep its separator");
    glide_close(&h);
    record_success(test_name);
}
