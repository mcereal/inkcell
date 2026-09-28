#define _POSIX_C_SOURCE 200809L

/*
 * The drawing primitives: the anti-aliased fill, the ring, the arc, and the emoji sprite.
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

#include "inkcell/ui/emoji.h"
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

/*
 * The round-ended arc reaches half a band past each end of its sweep and no further - which is
 * the whole of the difference from the square one, and the reason a caller cutting a gauge at a
 * boundary still has the square one to reach for.
 */
INKCELL_TEST_CASE(shapes_round_arc_caps_reach_past_the_sweep, unit) {
    struct shapes_page page;
    INKCELL_TEST_FAIL_IF(!shapes_open(&page), "the capture should open");

    const int cx = 128;
    const int cy = 128;
    const int radius = 60;
    const int band = 10;
    const int arm = radius - band / 2;

    inkcell_fb_stroke_arc(page.state, cx, cy, radius, band, 0, 250, k_ink);
    INKCELL_TEST_FAIL_IF(shapes_at(&page, cx - 4, cy - arm) != 0,
                         "a square end should stop at the start ray");
    INKCELL_TEST_FAIL_IF(shapes_at(&page, cx + arm, cy + 3) != 0,
                         "a square end should stop at the end ray");

    inkcell_fb_clear(page.state, k_ground);
    inkcell_fb_stroke_arc_round(page.state, cx, cy, radius, band, 0, 250, k_ink);
    INKCELL_TEST_FAIL_IF(shapes_at(&page, cx + 4, cy - arm) == 0,
                         "a round arc should still cover its own sweep");
    INKCELL_TEST_FAIL_IF(shapes_at(&page, cx - 4, cy - arm) == 0,
                         "a round end should reach back past the start ray");
    INKCELL_TEST_FAIL_IF(shapes_at(&page, cx + arm, cy + 3) == 0,
                         "a round end should reach on past the end ray");
    INKCELL_TEST_FAIL_IF(shapes_at(&page, cx - 9, cy - arm) != 0,
                         "a round end should reach half a band, not more");
    INKCELL_TEST_FAIL_IF(shapes_at(&page, cx - arm, cy) != 0,
                         "a round end should not change where the arc goes");

    /* A whole ring has no ends, so it is the square ring exactly. */
    inkcell_fb_clear(page.state, k_ground);
    inkcell_fb_stroke_arc_round(page.state, cx, cy, radius, band, 0, 1000, k_ink);
    INKCELL_TEST_FAIL_IF(shapes_at(&page, cx, cy) != 0, "a whole ring should leave its middle");

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

/* ---- emoji sprites ------------------------------------------------------------------------ */

static uint16_t shapes_sprite(uint32_t codepoint) {
    uint16_t sprite = 0U;
    const uint32_t codepoints[] = {codepoint};
    return inkcell_emoji_match(codepoints, 1U, &sprite) == 1U ? sprite : UINT16_MAX;
}

/* The whole pixel, B,G,R as one number: a sprite is not grey, so one channel no longer says
   everything. */
static uint32_t shapes_rgb_at(const struct shapes_page *page, int x, int y) {
    const uint8_t *px = &page->pixels[(size_t)y * page->stride + (size_t)x * 4U];
    return (uint32_t)px[0] | ((uint32_t)px[1] << 8) | ((uint32_t)px[2] << 16);
}

/* How many different colours a `box` square at (x, y) holds, up to `limit`. */
static int shapes_colours(const struct shapes_page *page, int x, int y, int box, int limit) {
    static uint32_t seen[4096];
    int count = 0;
    for (int dy = 0; dy < box; ++dy) {
        for (int dx = 0; dx < box && count < limit; ++dx) {
            const uint32_t rgb = shapes_rgb_at(page, x + dx, y + dy);
            bool known = false;
            for (int i = 0; i < count && !known; ++i) {
                known = seen[i] == rgb;
            }
            if (!known) {
                seen[count++] = rgb;
            }
        }
    }
    return count;
}

/* A sprite's opacity comes back graded rather than cut: solid inside, transparent in the margin,
   and a ring of steps between them, which is what the outline is drawn from. */
INKCELL_TEST_CASE(emoji_sprite_keeps_its_edge_opacity, unit) {
    const uint16_t sprite = shapes_sprite(0x1F600U); /* grinning face - a disc */
    INKCELL_TEST_FAIL_IF(sprite == UINT16_MAX, "the grinning face should be in the table");

    uint8_t index[INKCELL_EMOJI_SIZE * INKCELL_EMOJI_SIZE];
    uint8_t alpha[INKCELL_EMOJI_SIZE * INKCELL_EMOJI_SIZE];
    inkcell_emoji_decode_alpha(sprite, index, alpha);

    int solid = 0;
    int partial = 0;
    for (size_t i = 0; i < sizeof index; ++i) {
        INKCELL_TEST_FAIL_IF((index[i] == INKCELL_EMOJI_TRANSPARENT) != (alpha[i] == 0U),
                             "a pixel is transparent by its index and its opacity alike");
        solid += alpha[i] == 255U;
        partial += alpha[i] > 0U && alpha[i] < 255U;
    }
    INKCELL_TEST_FAIL_IF(solid == 0, "a disc should have a solid interior");
    INKCELL_TEST_FAIL_IF(partial == 0, "a disc's rim should be partly opaque");
    INKCELL_TEST_FAIL_IF(alpha[0] != 0U, "the corner of a disc's square is the margin");

    record_success(test_name);
}

/* The same sprite over black and over white: the interior is the sprite's own on both, the
   margin is the ground on both, and the rim is neither - it mixes with what is under it. A
   sprite cut at a threshold would have no pixel in that last group. */
INKCELL_TEST_CASE(emoji_outline_blends_into_the_ground, unit) {
    const uint16_t sprite = shapes_sprite(0x1F600U);
    INKCELL_TEST_FAIL_IF(sprite == UINT16_MAX, "the grinning face should be in the table");

    struct shapes_page dark;
    struct shapes_page light;
    INKCELL_TEST_FAIL_IF(!shapes_open(&dark) || !shapes_open(&light), "the captures should open");
    inkcell_fb_clear(light.state, k_ink);

    const int box = 64;
    inkcell_fb_draw_emoji_box(dark.state, 20, 20, box, sprite);
    inkcell_fb_draw_emoji_box(light.state, 20, 20, box, sprite);

    INKCELL_TEST_FAIL_IF(shapes_rgb_at(&dark, 20 + box / 2, 20 + box / 2) !=
                             shapes_rgb_at(&light, 20 + box / 2, 20 + box / 2),
                         "the middle of the face is the sprite's own colour on any ground");
    INKCELL_TEST_FAIL_IF(shapes_rgb_at(&dark, 20, 20) != 0U ||
                             shapes_rgb_at(&light, 20, 20) != 0xFFFFFFU,
                         "the corner of the box is the margin, left as the ground");

    int blended = 0;
    for (int dy = 0; dy < box; ++dy) {
        for (int dx = 0; dx < box; ++dx) {
            const uint32_t on_dark = shapes_rgb_at(&dark, 20 + dx, 20 + dy);
            const uint32_t on_light = shapes_rgb_at(&light, 20 + dx, 20 + dy);
            blended += on_dark != on_light && on_dark != 0U && on_light != 0xFFFFFFU;
        }
    }
    INKCELL_TEST_FAIL_IF(blended < 20, "the face's outline should blend into the ground");

    inkcell_capture_close(dark.capture);
    inkcell_capture_close(light.capture);
    record_success(test_name);
}

/* Drawn four times its stored size, a sprite is a gradient rather than a grid of squares: a
   nearest-neighbour enlargement has exactly the colours the sprite had at its own size, and a
   filtered one has the steps between them too. */
INKCELL_TEST_CASE(emoji_enlarged_is_smooth, unit) {
    const uint16_t sprite = shapes_sprite(0x1F600U);
    INKCELL_TEST_FAIL_IF(sprite == UINT16_MAX, "the grinning face should be in the table");

    struct shapes_page page;
    INKCELL_TEST_FAIL_IF(!shapes_open(&page), "the capture should open");

    const int stored = INKCELL_EMOJI_SIZE;
    const int large = 4 * INKCELL_EMOJI_SIZE;
    inkcell_fb_draw_emoji_box(page.state, 4, 4, stored, sprite);
    inkcell_fb_draw_emoji_box(page.state, 4, 4 + stored + 4, large, sprite);

    const int at_size = shapes_colours(&page, 4, 4, stored, 4096);
    const int enlarged = shapes_colours(&page, 4, 4 + stored + 4, large, 4096);
    INKCELL_TEST_FAIL_IF(enlarged <= 2 * at_size,
                         "an enlarged sprite should have the colours between its pixels");

    inkcell_capture_close(page.capture);
    record_success(test_name);
}

/* Every size from one pixel to past the filter's bound draws inside its box and nowhere else,
   and past the bound it draws the largest it can, centred. */
INKCELL_TEST_CASE(emoji_box_stays_in_its_box, unit) {
    const uint16_t sprite = shapes_sprite(0x1F600U);
    INKCELL_TEST_FAIL_IF(sprite == UINT16_MAX, "the grinning face should be in the table");
    INKCELL_TEST_FAIL_IF(inkcell_fb_emoji_box_fit(20) != 20 || inkcell_fb_emoji_box_fit(100) != 100,
                         "a box inside the bound is used whole");
    INKCELL_TEST_FAIL_IF(inkcell_fb_emoji_box_fit(INKCELL_FB_EMOJI_BOX_MAX + 40) !=
                             INKCELL_FB_EMOJI_BOX_MAX,
                         "a box past the bound is the bound");

    for (int box = 1; box <= (int)SHAPES_W - 16; ++box) {
        struct shapes_page page;
        INKCELL_TEST_FAIL_IF(!shapes_open(&page), "the capture should open");
        inkcell_fb_draw_emoji_box(page.state, 8, 8, box, sprite);

        int inside = 0;
        for (int y = 0; y < (int)SHAPES_H; ++y) {
            for (int x = 0; x < (int)SHAPES_W; ++x) {
                if (shapes_rgb_at(&page, x, y) == 0U) {
                    continue;
                }
                INKCELL_TEST_FAIL_IF(x < 8 || y < 8 || x >= 8 + box || y >= 8 + box,
                                     "a sprite drew outside the box it was given");
                ++inside;
            }
        }
        INKCELL_TEST_FAIL_IF(box >= 4 && inside == 0, "a sprite should draw something");
        inkcell_capture_close(page.capture);
    }

    record_success(test_name);
}
