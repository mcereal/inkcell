#define _POSIX_C_SOURCE 200809L

/*
 * The view stack: what a region of the frame does to the pixels drawn in it, and to the boxes
 * registered in it.
 *
 * It is tested by drawing, like tests/suites/ui_focus_widgets.c and for the same reason: the
 * claim is about what reaches the panel, and the only honest way to check that is to put
 * something on one and read it back. A fill is the probe throughout - one solid rectangle, one
 * colour, asked for at coordinates the view has to move and cut - because a fill is the
 * primitive every other drawing call here ends up in, so a view that is right for a fill is
 * right for a glyph.
 *
 * The cases are the four things a view promises: it moves content, it cuts content, it
 * composes when nested, and it takes the focus map with it. The last is the one that is not
 * obvious and is the reason the stack exists rather than a second shift - see
 * inkcell_fb_view_push().
 */

#include "framework/inkcell_test.h"

#include "inkcell/ui/fb_capture.h"
#include "inkcell/ui/focus.h"
#include "inkcell/ui/widgets.h"

#include <string.h>

#define VIEW_W 200U
#define VIEW_H 200U
#define VIEW_FOCUS_STORAGE 16U

enum { VIEW_ID_BOX = 7 };

struct view_harness {
    struct inkcell_capture *capture;
    struct inkcell_backend_fb_state *state;
    struct inkcell_focus_item storage[VIEW_FOCUS_STORAGE];
    struct inkcell_focus_map map;
};

static bool view_harness_open(struct view_harness *h) {
    h->capture = NULL;
    if (inkcell_capture_open(&h->capture, VIEW_W, VIEW_H, 1) < 0) {
        return false;
    }
    h->state = inkcell_capture_state(h->capture);
    /* A known ground under every case: the page is allocated zeroed, and zero is not
       necessarily any colour the theme names. */
    inkcell_fb_clear(h->state, inkcell_fb_color(h->state, INKCELL_COLOR_BG));
    inkcell_focus_begin(&h->map, h->storage, VIEW_FOCUS_STORAGE);
    inkcell_fb_set_focus_map(h->state, &h->map);
    return true;
}

static void view_harness_close(struct view_harness *h) {
    inkcell_capture_close(h->capture);
}

/*
 * Whether the pixel at (x, y) is the mark rather than the ground.
 *
 * Compared against a corner no case paints rather than against a colour the test worked out
 * for itself. What a colour role packs down to depends on the page's pixel format, and a test
 * that recomputed the packing would be a second copy of compose_color() - which is the one
 * thing it cannot be allowed to be, because then it agrees with itself rather than with the
 * panel. Any difference is a mark: a fill's edge is anti-aliased, and a partly covered pixel
 * is still a pixel the view let through.
 */
static bool view_marked(struct view_harness *h, int x, int y) {
    uint32_t width = 0U;
    uint32_t height = 0U;
    size_t stride = 0U;
    const uint8_t *pixels = inkcell_capture_pixels(h->capture, &width, &height, &stride);
    if (pixels == NULL || x < 0 || y < 0 || (uint32_t)x >= width || (uint32_t)y >= height) {
        return false;
    }
    const size_t bytes = INKCELL_FB_BGRA_BYTES;
    const uint8_t *ground =
        pixels + (size_t)(height - 1U) * stride + (size_t)(width - 1U) * bytes;
    const uint8_t *px = pixels + (size_t)y * stride + (size_t)x * bytes;
    return memcmp(px, ground, bytes) != 0;
}

/* One opaque square in the body's ink, at whatever coordinates the caller is testing. */
static void view_mark(struct view_harness *h, int x, int y, int w, int wh) {
    inkcell_fb_fill_rect(h->state, x, y, w, wh,
                         inkcell_fb_color(h->state, INKCELL_COLOR_TEXT));
}

/* A view moves what is drawn in it, and the offset is the content's rather than the box's. */
INKCELL_TEST_CASE(view_translates_content, unit) {
    struct view_harness h;
    INKCELL_TEST_FAIL_IF(!view_harness_open(&h), "capture should open");

    const struct inkcell_fb_rect box = {0, 0, (int)VIEW_W, (int)VIEW_H};
    INKCELL_TEST_FAIL_IF_CLEANUP(!inkcell_fb_view_push(h.state, box, 30, 40),
                                 view_harness_close(&h), "a view over the panel should push");
    view_mark(&h, 10, 10, 10, 10);
    inkcell_fb_view_pop(h.state);

    INKCELL_TEST_FAIL_IF_CLEANUP(view_marked(&h, 12, 12), view_harness_close(&h),
                                 "nothing should land where the content was asked for");
    INKCELL_TEST_FAIL_IF_CLEANUP(!view_marked(&h, 42, 52), view_harness_close(&h),
                                 "the content should land at its offset");

    view_harness_close(&h);
    record_success(test_name);
}

/* And cuts it at the box, on every side. */
INKCELL_TEST_CASE(view_clips_to_its_box, unit) {
    struct view_harness h;
    INKCELL_TEST_FAIL_IF(!view_harness_open(&h), "capture should open");

    const struct inkcell_fb_rect box = {50, 50, 40, 40};
    INKCELL_TEST_FAIL_IF_CLEANUP(!inkcell_fb_view_push(h.state, box, 0, 0), view_harness_close(&h),
                                 "a view inside the panel should push");
    /* Wider and taller than the box in both directions, so all four edges are tested at once. */
    view_mark(&h, 0, 0, (int)VIEW_W, (int)VIEW_H);
    inkcell_fb_view_pop(h.state);

    INKCELL_TEST_FAIL_IF_CLEANUP(!view_marked(&h, 60, 60), view_harness_close(&h),
                                 "the inside of the box should be painted");
    INKCELL_TEST_FAIL_IF_CLEANUP(view_marked(&h, 49, 60), view_harness_close(&h),
                                 "nothing should land left of the box");
    INKCELL_TEST_FAIL_IF_CLEANUP(view_marked(&h, 60, 49), view_harness_close(&h),
                                 "nothing should land above the box");
    INKCELL_TEST_FAIL_IF_CLEANUP(view_marked(&h, 90, 60), view_harness_close(&h),
                                 "nothing should land right of the box");
    INKCELL_TEST_FAIL_IF_CLEANUP(view_marked(&h, 60, 90), view_harness_close(&h),
                                 "nothing should land below the box");

    view_harness_close(&h);
    record_success(test_name);
}

/*
 * Nested, the offsets add and the boxes intersect.
 *
 * The inner box is stated in the outer view's coordinates, which is the half of the
 * composition a caller can get wrong: a sheet's content asks for a region of the sheet, not a
 * region of the panel.
 */
INKCELL_TEST_CASE(view_nests_by_composing, unit) {
    struct view_harness h;
    INKCELL_TEST_FAIL_IF(!view_harness_open(&h), "capture should open");

    const struct inkcell_fb_rect outer = {0, 0, (int)VIEW_W, (int)VIEW_H};
    INKCELL_TEST_FAIL_IF_CLEANUP(!inkcell_fb_view_push(h.state, outer, 10, 10),
                                 view_harness_close(&h), "the outer view should push");
    /* At 20,20 in the outer view's coordinates, which is 30,30 on the panel. */
    const struct inkcell_fb_rect inner = {20, 20, 40, 40};
    INKCELL_TEST_FAIL_IF_CLEANUP(!inkcell_fb_view_push(h.state, inner, 5, 5),
                                 (inkcell_fb_view_pop(h.state), view_harness_close(&h)),
                                 "the inner view should push");

    const struct inkcell_fb_rect window = inkcell_fb_view_box(h.state);
    const bool composed = window.x == 30 && window.y == 30 && window.w == 40 && window.h == 40;

    /* 20,20 + 10 + 5 lands at 35,35 - inside the intersected window, so it is drawn. */
    view_mark(&h, 20, 20, 4, 4);
    /* And this one is outside it: 0,0 + 15 is 15,15, which the inner box cuts away. */
    view_mark(&h, 0, 0, 4, 4);
    inkcell_fb_view_pop(h.state);
    inkcell_fb_view_pop(h.state);

    INKCELL_TEST_FAIL_IF_CLEANUP(!composed, view_harness_close(&h),
                                 "the window should be the two boxes intersected");
    INKCELL_TEST_FAIL_IF_CLEANUP(!view_marked(&h, 36, 36), view_harness_close(&h),
                                 "the offsets should add");
    INKCELL_TEST_FAIL_IF_CLEANUP(view_marked(&h, 16, 16), view_harness_close(&h),
                                 "the inner box should cut what falls outside it");

    view_harness_close(&h);
    record_success(test_name);
}

/* A box with nothing on the panel in it is refused, so a caller's `if` covers the whole test. */
INKCELL_TEST_CASE(view_refuses_an_empty_box, unit) {
    struct view_harness h;
    INKCELL_TEST_FAIL_IF(!view_harness_open(&h), "capture should open");

    const struct inkcell_fb_rect off_panel = {(int)VIEW_W + 10, 0, 20, 20};
    const bool pushed = inkcell_fb_view_push(h.state, off_panel, 0, 0);
    const struct inkcell_fb_rect flat = {10, 10, 20, 0};
    const bool flat_pushed = inkcell_fb_view_push(h.state, flat, 0, 0);

    INKCELL_TEST_FAIL_IF_CLEANUP(pushed, view_harness_close(&h),
                                 "a box off the panel should be refused");
    INKCELL_TEST_FAIL_IF_CLEANUP(flat_pushed, view_harness_close(&h),
                                 "a box with no height should be refused");

    view_harness_close(&h);
    record_success(test_name);
}

/*
 * The stack has a floor, and a push past it is refused rather than silently ignored.
 *
 * Worth a case because the failure it prevents is the quiet one: a push that returned true
 * without pushing would be popped by its caller, and the pop would take somebody else's view
 * off the stack.
 */
INKCELL_TEST_CASE(view_stack_has_a_floor, unit) {
    struct view_harness h;
    INKCELL_TEST_FAIL_IF(!view_harness_open(&h), "capture should open");

    const struct inkcell_fb_rect box = {0, 0, (int)VIEW_W, (int)VIEW_H};
    uint32_t pushed = 0U;
    while (pushed < INKCELL_FB_VIEW_DEPTH + 2U && inkcell_fb_view_push(h.state, box, 0, 0)) {
        ++pushed;
    }
    for (uint32_t i = 0U; i < pushed; ++i) {
        inkcell_fb_view_pop(h.state);
    }

    INKCELL_TEST_FAIL_IF_CLEANUP(pushed != INKCELL_FB_VIEW_DEPTH, view_harness_close(&h),
                                 "the stack should accept exactly its depth");

    view_harness_close(&h);
    record_success(test_name);
}

/*
 * A box registered inside a view is registered where the view put it.
 *
 * This is the property the frame's own transform deliberately does not have, and the reason
 * the two are different mechanisms. A press is resolved against the map, so a row a scroll
 * moved has to be *findable where it now is* - not where its own coordinates say it would be
 * if nothing had scrolled.
 */
INKCELL_TEST_CASE(view_moves_the_focus_map, unit) {
    struct view_harness h;
    INKCELL_TEST_FAIL_IF(!view_harness_open(&h), "capture should open");

    const struct inkcell_fb_rect box = {0, 0, (int)VIEW_W, (int)VIEW_H};
    INKCELL_TEST_FAIL_IF_CLEANUP(!inkcell_fb_view_push(h.state, box, 0, -25),
                                 view_harness_close(&h), "a scrolled view should push");
    const struct inkcell_fb_rect row = {10, 60, 100, 20};
    inkcell_fb_focus_register(h.state, VIEW_ID_BOX, &row);
    inkcell_fb_view_pop(h.state);

    struct inkcell_focus_rect found;
    const bool present = inkcell_focus_rect_of(&h.map, VIEW_ID_BOX, &found);
    INKCELL_TEST_FAIL_IF_CLEANUP(!present, view_harness_close(&h),
                                 "a row inside the view should be registered");
    INKCELL_TEST_FAIL_IF_CLEANUP(found.y != 35, view_harness_close(&h),
                                 "it should be registered where the scroll put it");

    view_harness_close(&h);
    record_success(test_name);
}

/*
 * And one the view cut away entirely is not registered at all.
 *
 * The whole point: a row scrolled off the top of a viewport is not somewhere to stand, and a
 * cursor that could reach it would be a cursor on something nobody can see.
 */
INKCELL_TEST_CASE(view_drops_what_it_cut_away, unit) {
    struct view_harness h;
    INKCELL_TEST_FAIL_IF(!view_harness_open(&h), "capture should open");

    const struct inkcell_fb_rect box = {0, 100, (int)VIEW_W, 100};
    INKCELL_TEST_FAIL_IF_CLEANUP(!inkcell_fb_view_push(h.state, box, 0, 0), view_harness_close(&h),
                                 "the lower half should push");
    /* Well above the view's box, so no part of it is in the window. */
    const struct inkcell_fb_rect row = {10, 10, 100, 20};
    inkcell_fb_focus_register(h.state, VIEW_ID_BOX, &row);
    inkcell_fb_view_pop(h.state);

    struct inkcell_focus_rect found;
    INKCELL_TEST_FAIL_IF_CLEANUP(inkcell_focus_rect_of(&h.map, VIEW_ID_BOX, &found),
                                 view_harness_close(&h),
                                 "a row outside the window should not be registered");

    view_harness_close(&h);
    record_success(test_name);
}
