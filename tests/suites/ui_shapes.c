#define _POSIX_C_SOURCE 200809L

/*
 * The drawing primitives: the anti-aliased fill, the ring, and the arc.
 *
 * These are checked by looking at the pixels, because that is what they produce and there is
 * nothing else to look at. The golden sheet already compares whole pages and would catch a
 * change to any of this - but it says "something moved", and these say which promise broke.
 *
 * Every value here is exact rather than approximate: coverage is counted in sub-samples and the
 * angles come off a committed table, so a pixel is either the ground, the ink, or a blend of the
 * two that is the same blend on every machine. A case that had to say "about" would be a case
 * admitting the renderer is not reproducible, and the golden sheet depends on it being so.
 */

#include "framework/inkcell_test.h"

#include "inkcell/ui/fb_capture.h"
#include "inkcell/ui/fb_draw.h"

#include <limits.h>
#include <stdint.h>

#define SHAPES_W 256U
#define SHAPES_H 256U

static const struct inkcell_rgb k_ground = {0U, 0U, 0U};
static const struct inkcell_rgb k_ink = {255U, 255U, 255U};

struct shapes_page {
    struct inkcell_capture *capture;
    struct inkcell_draw_state *state;
    const uint8_t *pixels;
    size_t stride;
};

static bool shapes_open(struct shapes_page *page) {
    page->capture = NULL;
    if (inkcell_capture_open(&page->capture, SHAPES_W, SHAPES_H, INKCELL_SCALE(4)) < 0) {
        return false;
    }
    page->state = inkcell_capture_state(page->capture);
    uint32_t w = 0U;
    uint32_t h = 0U;
    page->pixels = inkcell_capture_pixels(page->capture, &w, &h, &page->stride);
    if (page->pixels == NULL) {
        inkcell_capture_close(page->capture);
        return false;
    }
    inkcell_fb_clear(page->state, k_ground);
    return true;
}

/* The page is B,G,R,X per pixel; these are all greys, so one channel says everything. */
static int shapes_at(const struct shapes_page *page, int x, int y) {
    return page->pixels[(size_t)y * page->stride + (size_t)x * 4U + 2U];
}

INKCELL_TEST_CASE(shapes_round_rect_corner_is_blended, unit) {
    struct shapes_page page;
    INKCELL_TEST_FAIL_IF(!shapes_open(&page), "the capture should open");

    const int radius = 24;
    inkcell_fb_fill_round_rect(page.state, 20, 20, 120, 120, radius, k_ink);

    /* Deep inside is the ink and well outside the corner is the ground: the shape is where it
       was asked for. */
    INKCELL_TEST_FAIL_IF(shapes_at(&page, 80, 80) != 255, "the middle should be the fill");
    INKCELL_TEST_FAIL_IF(shapes_at(&page, 21, 21) != 0, "outside the arc should be untouched");

    /*
     * And somewhere along the arc there is a pixel that is neither. That is the whole claim:
     * a hard-edged rasteriser produces only 0 and 255 here, so finding one value in between is
     * the difference between a curve and a staircase.
     */
    int blended = 0;
    for (int dy = 0; dy < radius; ++dy) {
        for (int dx = 0; dx < radius; ++dx) {
            const int value = shapes_at(&page, 20 + dx, 20 + dy);
            if (value > 0 && value < 255) {
                ++blended;
            }
        }
    }
    /* Roughly the arc's own length: a quarter circle of this radius crosses about that many
       pixel boundaries, and a hard-edged rasteriser would find none of them. The bound is loose
       on purpose - what is being asserted is that the edge is graded, not how it is graded. */
    INKCELL_TEST_FAIL_IF(blended < radius,
                         "the corner arc should be a band of partly covered pixels");

    /* And the grade runs the right way: further out along the arc's normal is less covered. */
    const int inner = shapes_at(&page, 20 + 7, 20 + 7);
    const int outer = shapes_at(&page, 20 + 3, 20 + 3);
    INKCELL_TEST_FAIL_IF(outer > inner, "coverage should fall off towards the outside of the arc");

    inkcell_capture_close(page.capture);
    record_success(test_name);
}

INKCELL_TEST_CASE(shapes_stroke_leaves_its_interior_alone, unit) {
    struct shapes_page page;
    INKCELL_TEST_FAIL_IF(!shapes_open(&page), "the capture should open");

    /* A filled panel, then a ring drawn over its edge. What the ring must not do is repaint the
       middle - which is exactly what the two-fills outline it replaced did, and why an outlined
       control punched a hole in the row fill behind it. */
    const struct inkcell_rgb panel = {64U, 64U, 64U};
    inkcell_fb_fill_rect(page.state, 20, 20, 120, 120, panel);
    inkcell_fb_stroke_round_rect(page.state, 20, 20, 120, 120, 24, 4, k_ink);

    INKCELL_TEST_FAIL_IF(shapes_at(&page, 80, 80) != 64,
                         "a ring should leave what is inside it untouched");
    INKCELL_TEST_FAIL_IF(shapes_at(&page, 80, 21) != 255,
                         "the ring itself should be drawn along the top edge");
    INKCELL_TEST_FAIL_IF(shapes_at(&page, 80, 30) != 64,
                         "past the thickness the panel should show through again");
    INKCELL_TEST_FAIL_IF(shapes_at(&page, 80, 19) != 0, "the ring should not spill outside itself");

    /* The two bands meet rather than overlap on a circle, which is the case that drew half a
       ring until it was fixed: both sides have to be there. */
    inkcell_fb_clear(page.state, k_ground);
    inkcell_fb_stroke_round_rect(page.state, 28, 28, 80, 80, 40, 4, k_ink);
    INKCELL_TEST_FAIL_IF(shapes_at(&page, 30, 68) != 255, "a circle's left edge should be drawn");
    INKCELL_TEST_FAIL_IF(shapes_at(&page, 105, 68) != 255, "a circle's right edge should be drawn");

    inkcell_capture_close(page.capture);
    record_success(test_name);
}

INKCELL_TEST_CASE(shapes_arc_sweeps_clockwise_from_the_top, unit) {
    struct shapes_page page;
    INKCELL_TEST_FAIL_IF(!shapes_open(&page), "the capture should open");

    const int cx = 128;
    const int cy = 128;
    const int radius = 60;
    const int band = 10;
    /* On the ring, a little inside the outer edge, at each of the four cardinal points. */
    const int arm = radius - band / 2;

    /* A quarter turn from twelve o'clock: the top and the right, and neither of the others.
       This is the whole convention in one assertion - where zero points, and which way round. */
    inkcell_fb_stroke_arc(page.state, cx, cy, radius, band, 0, 250, k_ink);
    INKCELL_TEST_FAIL_IF(shapes_at(&page, cx + 4, cy - arm) == 0,
                         "a quarter sweep should cover twelve o'clock");
    INKCELL_TEST_FAIL_IF(shapes_at(&page, cx + arm, cy - 4) == 0,
                         "a quarter sweep should run clockwise to three o'clock");
    INKCELL_TEST_FAIL_IF(shapes_at(&page, cx - arm, cy) != 0,
                         "a quarter sweep should not reach nine o'clock");
    INKCELL_TEST_FAIL_IF(shapes_at(&page, cx, cy + arm) != 0,
                         "a quarter sweep should not reach six o'clock");
    INKCELL_TEST_FAIL_IF(shapes_at(&page, cx, cy) != 0, "an arc should not fill its own middle");

    /* Past the half turn the wedge cannot be bounded by its own two rays and the test inverts.
       Three quarters covers everything but nine o'clock. */
    inkcell_fb_clear(page.state, k_ground);
    inkcell_fb_stroke_arc(page.state, cx, cy, radius, band, 0, 750, k_ink);
    INKCELL_TEST_FAIL_IF(shapes_at(&page, cx + arm, cy) == 0,
                         "three quarters should cover three o'clock");
    INKCELL_TEST_FAIL_IF(shapes_at(&page, cx, cy + arm) == 0,
                         "three quarters should cover six o'clock");
    INKCELL_TEST_FAIL_IF(shapes_at(&page, cx - arm, cy - 4) != 0,
                         "three quarters should stop short of nine o'clock");

    /* And the whole ring skips the angular test altogether. */
    inkcell_fb_clear(page.state, k_ground);
    inkcell_fb_stroke_arc(page.state, cx, cy, radius, band, 0, 1000, k_ink);
    INKCELL_TEST_FAIL_IF(
        shapes_at(&page, cx, cy - arm) == 0 || shapes_at(&page, cx + arm, cy) == 0 ||
            shapes_at(&page, cx, cy + arm) == 0 || shapes_at(&page, cx - arm, cy) == 0,
        "a whole sweep should close the ring");

    /* A sweep of nothing is nothing, rather than a hairline at the start ray. */
    inkcell_fb_clear(page.state, k_ground);
    inkcell_fb_stroke_arc(page.state, cx, cy, radius, band, 0, 0, k_ink);
    INKCELL_TEST_FAIL_IF(shapes_at(&page, cx, cy - arm) != 0, "an empty sweep should draw nothing");

    inkcell_capture_close(page.capture);
    record_success(test_name);
}

INKCELL_TEST_CASE(shapes_survive_degenerate_geometry, unit) {
    struct shapes_page page;
    INKCELL_TEST_FAIL_IF(!shapes_open(&page), "the capture should open");

    /* None of these should write anything, and none of them should reach past the page. The
       clamps are the whole of what is being checked; ASan is the rest of it. */
    inkcell_fb_fill_round_rect(page.state, 10, 10, 0, 40, 8, k_ink);
    inkcell_fb_fill_round_rect(page.state, 10, 10, 40, -5, 8, k_ink);
    inkcell_fb_stroke_round_rect(page.state, 10, 10, 40, 40, 8, 0, k_ink);
    inkcell_fb_stroke_arc(page.state, 128, 128, 0, 4, 0, 500, k_ink);
    inkcell_fb_stroke_arc(page.state, 128, 128, 40, 0, 0, 500, k_ink);
    inkcell_fb_stroke_arc(page.state, 128, 128, 40, 4, 0, -100, k_ink);
    INKCELL_TEST_FAIL_IF(shapes_at(&page, 20, 20) != 0, "a degenerate shape should draw nothing");

    /* A radius past half the shorter side is "as round as it goes", not an overflow, and a
       stroke thicker than the shape is the shape. Drawn partly off the page on purpose. */
    inkcell_fb_fill_round_rect(page.state, -20, -20, 60, 60, 900, k_ink);
    inkcell_fb_stroke_round_rect(page.state, (int)SHAPES_W - 30, (int)SHAPES_H - 30, 60, 60, 900,
                                 900, k_ink);
    inkcell_fb_stroke_arc(page.state, 4, 4, 40, 80, 0, 1000, k_ink);
    /* A sweep past a whole turn is a whole turn. */
    inkcell_fb_stroke_arc(page.state, 128, 200, 30, 6, 0, 5000, k_ink);
    INKCELL_TEST_FAIL_IF(shapes_at(&page, 128, 200 - 27) == 0,
                         "a sweep past a whole turn should still close the ring");

    inkcell_capture_close(page.capture);
    record_success(test_name);
}

INKCELL_TEST_CASE(shapes_stroke_wider_than_any_row_buffer, unit) {
    /*
     * A ring whose two bands meet across a row wider than a buffer would plausibly be sized for.
     *
     * The shapes here used to gather a row of coverage into a fixed array and blend it in one
     * pass. Three shapes wanted three different lengths for it - the corner's radius, the
     * ring's band, the arc's diameter - and sizing one array for all three is how a thick
     * enough stroke on a big enough shape came to write past the end of it. UBSan named the
     * index. Nothing blends by the row any more, so there is no length left to get wrong, and
     * this is the case that would find it if one came back.
     *
     * Reaching it takes a shape *taller* than it is wide and more than a thousand pixels
     * across: the thickness is clamped to half the shorter side, so on a wide shape that is
     * half the height and the two bands never meet. No panel this library was written for is
     * that size, which is why the gallery could not have found it.
     */
    struct inkcell_capture *capture = NULL;
    INKCELL_TEST_FAIL_IF(inkcell_capture_open(&capture, 1200U, 1400U, INKCELL_SCALE(4)) < 0,
                         "a page taller than it is wide should open");
    struct inkcell_draw_state *state = inkcell_capture_state(capture);
    inkcell_fb_clear(state, k_ground);

    /* Half the shorter side, so `band` lands exactly on half the width and the bands meet. */
    inkcell_fb_stroke_round_rect(state, 0, 0, 1200, 1400, 600, 600, k_ink);

    uint32_t width = 0U;
    uint32_t height = 0U;
    size_t stride = 0U;
    const uint8_t *pixels = inkcell_capture_pixels(capture, &width, &height, &stride);
    INKCELL_TEST_FAIL_IF(pixels == NULL, "the page should have pixels");
    /* A stroke that thick is the whole shape, so its middle is drawn rather than left hollow -
       and drawn at full width, which is the part that used to run off the end. */
    INKCELL_TEST_FAIL_IF(pixels[(size_t)700 * stride + (size_t)600 * 4U + 2U] != 255,
                         "a stroke as thick as the shape should fill it");
    INKCELL_TEST_FAIL_IF(pixels[(size_t)700 * stride + (size_t)1199 * 4U + 2U] != 255,
                         "and should reach the far edge of the row");

    inkcell_capture_close(capture);
    record_success(test_name);
}

INKCELL_TEST_CASE(shapes_arc_takes_any_turn_a_caller_can_hold, unit) {
    /*
     * The start angle is an int32_t and the sine helper deliberately accepts wrapped and
     * negative turns - so a caller is entitled to hand it anything of that type and let the
     * modulo sort it out. It could not: the quarter turn for the cosine and the end of the
     * sweep were both added *before* the normalisation, so a turn near the top of the range
     * overflowed on the way in.
     *
     * Not a hypothetical. A spinner driven straight off the monotonic clock - the obvious way
     * to write one - passes a millisecond count, and INT32_MAX milliseconds is about
     * twenty-five days of uptime. A handheld left on a shelf gets there.
     */
    struct shapes_page page;
    INKCELL_TEST_FAIL_IF(!shapes_open(&page), "the capture should open");

    inkcell_fb_stroke_arc(page.state, 128, 128, 60, 10, INT32_MAX, 250, k_ink);
    inkcell_fb_stroke_arc(page.state, 128, 128, 60, 10, INT32_MIN, 250, k_ink);
    inkcell_fb_stroke_arc(page.state, 128, 128, 60, 10, INT32_MAX - 1, 1000, k_ink);

    /* A wrapped turn is the same turn: two thousand permille round is where zero is, so this
       draws the same quarter as a start of nothing. */
    inkcell_fb_clear(page.state, k_ground);
    inkcell_fb_stroke_arc(page.state, 128, 128, 60, 10, 2000, 250, k_ink);
    const int arm = 60 - 10 / 2;
    INKCELL_TEST_FAIL_IF(shapes_at(&page, 128 + 4, 128 - arm) == 0,
                         "a turn of two whole circles should start where zero does");
    INKCELL_TEST_FAIL_IF(shapes_at(&page, 128 - arm, 128) != 0,
                         "and should sweep the same quarter");

    inkcell_capture_close(page.capture);
    record_success(test_name);
}

INKCELL_TEST_CASE(shapes_survive_a_radius_past_the_page, unit) {
    /*
     * Both primitives take an `int` and clip whatever lands off the page, so a radius far
     * larger than the panel is a legal call with almost nothing to draw. The squaring got there
     * first: the sample distances are in eighths of a pixel, so a radius over about 5,800
     * overflowed a 32-bit square before a single pixel had been clipped away.
     */
    struct shapes_page page;
    INKCELL_TEST_FAIL_IF(!shapes_open(&page), "the capture should open");

    inkcell_fb_stroke_arc(page.state, 128, 128, 6000, 12, 0, 500, k_ink);
    inkcell_fb_stroke_arc(page.state, 128, 128, 60000, 12, 0, 1000, k_ink);
    inkcell_fb_fill_round_rect(page.state, -6000, -6000, 12000, 12000, 6000, k_ink);
    inkcell_fb_stroke_round_rect(page.state, -6000, -6000, 12000, 12000, 6000, 40, k_ink);

    /* The huge filled disc covers this page completely; that it drew at all is the assertion
       that the arithmetic got through. */
    INKCELL_TEST_FAIL_IF(shapes_at(&page, 128, 128) != 255,
                         "a disc far larger than the page should still cover it");

    inkcell_capture_close(page.capture);
    record_success(test_name);
}

INKCELL_TEST_CASE(shapes_level_line_is_crisp_and_a_slope_is_graded, unit) {
    /*
     * The two promises inkcell_fb_stroke_line() makes. A level stroke with an even pen lands on
     * pixel boundaries, so it is whole rows of ink and nothing in between - a chart's flat
     * stretch drawn soft would read as out of focus. A slope is graded at its edge, which is the
     * whole difference between it and the column walk it replaced.
     */
    struct shapes_page page;
    INKCELL_TEST_FAIL_IF(!shapes_open(&page), "the capture should open");

    inkcell_fb_stroke_line(page.state, 40, 40, 200, 40, 4, k_ink);
    for (int y = 36; y < 48; ++y) {
        const int value = shapes_at(&page, 120, y);
        const int expected = (y >= 40 && y < 44) ? 255 : 0;
        INKCELL_TEST_FAIL_IF(value != expected, "a level stroke should cover whole rows exactly");
    }
    /* Round ends: the corner of the pen's square at the start is outside the disc. */
    INKCELL_TEST_FAIL_IF(shapes_at(&page, 40, 40) == 255, "a stroke's end should be round");

    inkcell_fb_clear(page.state, k_ground);
    inkcell_fb_stroke_line(page.state, 40, 60, 200, 180, 4, k_ink);
    int blended = 0;
    int full = 0;
    for (int y = 50; y < 200; ++y) {
        for (int x = 30; x < 220; ++x) {
            const int value = shapes_at(&page, x, y);
            blended += value > 0 && value < 255;
            full += value == 255;
        }
    }
    INKCELL_TEST_FAIL_IF(full == 0, "a sloped stroke should have a solid core");
    INKCELL_TEST_FAIL_IF(blended < 100, "a sloped stroke's edges should be graded");

    inkcell_capture_close(page.capture);
    record_success(test_name);
}

INKCELL_TEST_CASE(shapes_wash_fades_from_top_to_bottom, unit) {
    struct shapes_page page;
    INKCELL_TEST_FAIL_IF(!shapes_open(&page), "the capture should open");

    inkcell_fb_fill_wash(page.state, 20, 20, 40, 200, k_ink, 1000, 0);
    const int top = shapes_at(&page, 40, 20);
    const int middle = shapes_at(&page, 40, 120);
    const int bottom = shapes_at(&page, 40, 219);
    INKCELL_TEST_FAIL_IF(top < 250, "the first row should be nearly the full colour");
    INKCELL_TEST_FAIL_IF(middle < 100 || middle > 155, "halfway down should be about half");
    INKCELL_TEST_FAIL_IF(bottom > 5, "the last row should be nearly nothing");
    INKCELL_TEST_FAIL_IF(shapes_at(&page, 19, 120) != 0, "outside the rectangle is untouched");

    /* Over something already drawn it mixes rather than replaces: a quarter-strength wash of
       black over white is three quarters white. */
    inkcell_fb_fill_rect(page.state, 100, 20, 40, 40, k_ink);
    inkcell_fb_fill_wash(page.state, 100, 20, 40, 40, k_ground, 250, 250);
    const int mixed = shapes_at(&page, 120, 40);
    INKCELL_TEST_FAIL_IF(mixed < 185 || mixed > 196, "a wash should blend with what is under it");

    inkcell_capture_close(page.capture);
    record_success(test_name);
}
