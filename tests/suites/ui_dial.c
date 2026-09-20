#define _POSIX_C_SOURCE 200809L

/*
 * The dial: the bar's arithmetic on a circle.
 *
 * What is worth pinning here is not that a ring appears - the golden sheet says that, and says
 * it better. It is that the ring and the bar answer the same questions the same way: the same
 * domain, the same band, the same refusal to round a real reading away to nothing. A dial that
 * drifted from the meter would be two components disagreeing about what three quarters means on
 * one screen, and that is not something a picture makes obvious.
 */

#include "framework/inkcell_test.h"

#include "inkcell/ui/fb_capture.h"
#include "inkcell/ui/fb_draw.h"
#include "inkcell/ui/widgets/meter.h"

#include <stdint.h>

#define DIAL_SIDE 200U

struct dial_page {
    struct inkcell_capture *capture;
    struct inkcell_backend_fb_state *state;
    const uint8_t *pixels;
    size_t stride;
};

static bool dial_open(struct dial_page *page) {
    page->capture = NULL;
    if (inkcell_capture_open(&page->capture, DIAL_SIDE, DIAL_SIDE, 4) < 0) {
        return false;
    }
    page->state = inkcell_capture_state(page->capture);
    inkcell_fb_state_set_now(page->state, 1000U);
    uint32_t w = 0U;
    uint32_t h = 0U;
    page->pixels = inkcell_capture_pixels(page->capture, &w, &h, &page->stride);
    if (page->pixels == NULL) {
        inkcell_capture_close(page->capture);
        return false;
    }
    inkcell_fb_clear(page->state, inkcell_fb_color(page->state, INKCELL_COLOR_BG));
    return true;
}

/* The pixel as a packed triple, so two readings can be compared without naming a theme's
   colours - what matters is whether they are the same, not what they are. */
static uint32_t dial_at(const struct dial_page *page, int x, int y) {
    const uint8_t *p = page->pixels + (size_t)y * page->stride + (size_t)x * 4U;
    return ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0];
}

static const struct inkcell_scale k_permille = {.min = 0, .max = 1000};
static const struct inkcell_band k_band = {.warn = 700, .bad = 880};

static struct inkcell_fb_dial dial_of(int32_t value, uint32_t id) {
    const struct inkcell_fb_dial dial = {
        .rect = {.x = 20, .y = 20, .w = 160, .h = 160},
        .id = id,
        .kind = INKCELL_FB_DIAL_DETERMINATE,
        .value = value,
        .scale = k_permille,
        .band = &k_band,
        .tone = INKCELL_TONE_PRIMARY,
        .ground = INKCELL_COLOR_BG,
    };
    return dial;
}

/*
 * The middle of the ring's band, at each cardinal point of a 160px box at 20,20.
 *
 * The middle rather than an edge, and stated rather than derived: the band runs from the radius
 * inwards by the thickness, so an arm equal to either bound lands on a boundary pixel whose
 * coverage is a rounding decision. A probe on a boundary is a test that reports on the
 * rasteriser's tie-breaking rather than on the dial.
 */
#define DIAL_CX 100
#define DIAL_CY 100
#define DIAL_ARM 78

INKCELL_TEST_CASE(dial_fills_clockwise_from_the_top, unit) {
    struct dial_page page;
    INKCELL_TEST_FAIL_IF(!dial_open(&page), "the capture should open");

    const uint32_t track = dial_at(&page, DIAL_CX, DIAL_CY); /* nothing drawn yet: the ground */

    /* A quarter: twelve o'clock and three o'clock carry the reading, six and nine carry the
       track. Same convention as the arc primitive, and the same as every progress ring there
       has ever been. */
    struct inkcell_fb_dial quarter = dial_of(250, 0x0D01);
    inkcell_fb_draw_dial(page.state, &quarter);

    const uint32_t top = dial_at(&page, DIAL_CX + 3, DIAL_CY - DIAL_ARM);
    const uint32_t right = dial_at(&page, DIAL_CX + DIAL_ARM, DIAL_CY - 3);
    const uint32_t bottom = dial_at(&page, DIAL_CX, DIAL_CY + DIAL_ARM);
    INKCELL_TEST_FAIL_IF(top == track, "a quarter should fill twelve o'clock");
    INKCELL_TEST_FAIL_IF(right == track, "a quarter should run clockwise to three o'clock");
    INKCELL_TEST_FAIL_IF(bottom == top, "a quarter should leave six o'clock on the track");
    INKCELL_TEST_FAIL_IF(bottom == track,
                         "the track should still be drawn where the reading has not reached");
    INKCELL_TEST_FAIL_IF(dial_at(&page, DIAL_CX, DIAL_CY) != track,
                         "a ring should leave its own middle alone");

    inkcell_capture_close(page.capture);
    record_success(test_name);
}

INKCELL_TEST_CASE(dial_reads_its_band_the_way_the_bar_does, unit) {
    struct dial_page page;
    INKCELL_TEST_FAIL_IF(!dial_open(&page), "the capture should open");

    /* Three readings either side of the two thresholds. The colours are the theme's business;
       what this asserts is that the band is consulted at all and that it changes the answer at
       the boundaries - which is the contract the bar is already held to. */
    struct inkcell_fb_dial healthy = dial_of(300, 0x0D11);
    inkcell_fb_draw_dial(page.state, &healthy);
    const uint32_t healthy_ink = dial_at(&page, DIAL_CX + 3, DIAL_CY - DIAL_ARM);

    inkcell_fb_clear(page.state, inkcell_fb_color(page.state, INKCELL_COLOR_BG));
    struct inkcell_fb_dial warning = dial_of(800, 0x0D12);
    inkcell_fb_draw_dial(page.state, &warning);
    const uint32_t warning_ink = dial_at(&page, DIAL_CX + 3, DIAL_CY - DIAL_ARM);

    inkcell_fb_clear(page.state, inkcell_fb_color(page.state, INKCELL_COLOR_BG));
    struct inkcell_fb_dial bad = dial_of(950, 0x0D13);
    inkcell_fb_draw_dial(page.state, &bad);
    const uint32_t bad_ink = dial_at(&page, DIAL_CX + 3, DIAL_CY - DIAL_ARM);

    INKCELL_TEST_FAIL_IF(healthy_ink == warning_ink,
                         "a reading past the warning threshold should change the ring's ink");
    INKCELL_TEST_FAIL_IF(warning_ink == bad_ink,
                         "a reading past the bad threshold should change it again");

    /* And with no band at all the reading keeps the tone it was given, whatever its value. */
    inkcell_fb_clear(page.state, inkcell_fb_color(page.state, INKCELL_COLOR_BG));
    struct inkcell_fb_dial unbanded = dial_of(950, 0x0D14);
    unbanded.band = NULL;
    inkcell_fb_draw_dial(page.state, &unbanded);
    INKCELL_TEST_FAIL_IF(dial_at(&page, DIAL_CX + 3, DIAL_CY - DIAL_ARM) != healthy_ink,
                         "with no band the tone should be the one the caller asked for");

    inkcell_capture_close(page.capture);
    record_success(test_name);
}

INKCELL_TEST_CASE(dial_draws_a_reading_too_small_to_round_to_a_pixel, unit) {
    struct dial_page page;
    INKCELL_TEST_FAIL_IF(!dial_open(&page), "the capture should open");

    /* Exactly nothing is an empty track. */
    struct inkcell_fb_dial empty = dial_of(0, 0x0D21);
    inkcell_fb_draw_dial(page.state, &empty);
    const uint32_t track = dial_at(&page, DIAL_CX + 3, DIAL_CY - DIAL_ARM);

    /*
     * Four tenths of a percent is not nothing, and rounding it away to an empty track says the
     * one thing the ring exists to distinguish from - the same refusal the bar makes.
     */
    inkcell_fb_clear(page.state, inkcell_fb_color(page.state, INKCELL_COLOR_BG));
    struct inkcell_fb_dial sliver = dial_of(4, 0x0D22);
    inkcell_fb_draw_dial(page.state, &sliver);
    int drawn = 0;
    for (int x = DIAL_CX - 2; x <= DIAL_CX + 4; ++x) {
        if (dial_at(&page, x, DIAL_CY - DIAL_ARM) != track) {
            ++drawn;
        }
    }
    INKCELL_TEST_FAIL_IF(drawn == 0, "a reading that is not zero should draw something");

    inkcell_capture_close(page.capture);
    record_success(test_name);
}

INKCELL_TEST_CASE(dial_declines_a_box_with_no_room_for_a_hole, unit) {
    struct dial_page page;
    INKCELL_TEST_FAIL_IF(!dial_open(&page), "the capture should open");

    const uint32_t ground = dial_at(&page, 30, 30);
    const int least = inkcell_fb_dial_min_side(page.state, page.state->scale);
    INKCELL_TEST_FAIL_IF(least <= 0, "a smallest side should be a real number of pixels");

    /* Below the smallest side the ring closes into a disc and stops being a reading, so it
       draws nothing rather than a dot that means the same at every value. */
    struct inkcell_fb_dial tiny = dial_of(500, 0x0D31);
    tiny.rect = (struct inkcell_fb_rect){.x = 20, .y = 20, .w = least - 1, .h = least - 1};
    inkcell_fb_draw_dial(page.state, &tiny);
    INKCELL_TEST_FAIL_IF(dial_at(&page, 20 + (least - 1) / 2, 20) != ground,
                         "a box too small for a hole should draw nothing at all");

    /* At the smallest side it draws. */
    struct inkcell_fb_dial least_dial = dial_of(500, 0x0D32);
    least_dial.rect = (struct inkcell_fb_rect){.x = 20, .y = 20, .w = least, .h = least};
    inkcell_fb_draw_dial(page.state, &least_dial);
    INKCELL_TEST_FAIL_IF(dial_at(&page, 20 + least / 2, 20) == ground,
                         "at the smallest side it should draw");

    /* And a degenerate box is refused rather than reaching the arc at all. */
    struct inkcell_fb_dial nothing = dial_of(500, 0x0D33);
    nothing.rect = (struct inkcell_fb_rect){.x = 20, .y = 20, .w = 0, .h = 0};
    inkcell_fb_draw_dial(page.state, &nothing);
    inkcell_fb_draw_dial(page.state, NULL);

    inkcell_capture_close(page.capture);
    record_success(test_name);
}

INKCELL_TEST_CASE(dial_label_fits_or_is_left_out, unit) {
    struct dial_page page;
    INKCELL_TEST_FAIL_IF(!dial_open(&page), "the capture should open");

    /* A short figure fits the hole and is drawn in it. */
    struct inkcell_fb_dial labelled = dial_of(500, 0x0D41);
    labelled.label = "50%";
    inkcell_fb_draw_dial(page.state, &labelled);
    const uint32_t ground = inkcell_fb_color(page.state, INKCELL_COLOR_BG).r;
    int inked = 0;
    for (int x = DIAL_CX - 30; x <= DIAL_CX + 30; ++x) {
        const uint8_t *p = page.pixels + (size_t)DIAL_CY * page.stride + (size_t)x * 4U;
        if (p[2] != ground) {
            ++inked;
        }
    }
    INKCELL_TEST_FAIL_IF(inked == 0, "a figure that fits should be drawn in the hole");

    /*
     * A sentence does not fit, and is left out rather than clipped. Nothing is the right answer:
     * a figure cut to fit a hole this size is a figure nobody can read, and the arc has already
     * said what it says.
     */
    inkcell_fb_clear(page.state, inkcell_fb_color(page.state, INKCELL_COLOR_BG));
    struct inkcell_fb_dial overlong = dial_of(500, 0x0D42);
    overlong.label = "an entire sentence, which cannot possibly fit inside a ring this size";
    inkcell_fb_draw_dial(page.state, &overlong);
    int spill = 0;
    for (int x = DIAL_CX - 30; x <= DIAL_CX + 30; ++x) {
        const uint8_t *p = page.pixels + (size_t)DIAL_CY * page.stride + (size_t)x * 4U;
        if (p[2] != ground) {
            ++spill;
        }
    }
    INKCELL_TEST_FAIL_IF(spill != 0, "a label with nowhere to go should be left out");

    inkcell_capture_close(page.capture);
    record_success(test_name);
}

INKCELL_TEST_CASE(dial_is_drawn_at_the_thickness_it_was_measured_at, unit) {
    /*
     * The sizing helpers take a scale and the draw has to agree with them.
     *
     * It did not. inkcell_fb_dial_min_side() answered for whatever scale it was asked about and
     * the draw recomputed the thickness from the state's - so a caller sizing a compact dial at
     * the chrome scale got a box the helper called the minimum and the draw called too small,
     * and drew nothing at all. Anything larger got a ring thicker than the helper advertised,
     * which is the same disagreement with a subtler symptom.
     *
     * The fix is that the thickness is *carried* rather than recomputed. A bar's is simply the
     * height of the box it was handed, so the two can never diverge; a ring's is not derivable
     * from a square, which makes the dial the first component here that could get this wrong.
     */
    struct dial_page page;
    INKCELL_TEST_FAIL_IF(!dial_open(&page), "the capture should open");

    const int small = inkcell_fb_type_scale(page.state, INKCELL_TYPE_LABEL);
    INKCELL_TEST_FAIL_IF(small >= page.state->scale,
                         "this case needs a chrome scale below the body scale to say anything");
    const int thin = inkcell_fb_dial_thickness(page.state, small);
    const int thick = inkcell_fb_dial_thickness(page.state, page.state->scale);
    INKCELL_TEST_FAIL_IF(thin >= thick, "the chrome scale should advise a thinner ring");

    const uint32_t ground = dial_at(&page, 10, 10);

    /* The band at twelve o'clock, on a box big enough that the ring is not degenerate: the
       outer edge lands on the box's own top row, so counting down from it is the thickness. */
    const int side = 60;
    int measured[2] = {0, 0};
    const int wanted[2] = {0, thin}; /* 0 means "the state's own", which is the other answer */
    for (int i = 0; i < 2; ++i) {
        inkcell_fb_clear(page.state, inkcell_fb_color(page.state, INKCELL_COLOR_BG));
        struct inkcell_fb_dial dial = dial_of(500, (uint32_t)(0x0D51 + i));
        dial.rect = (struct inkcell_fb_rect){.x = 20, .y = 20, .w = side, .h = side};
        dial.thickness = wanted[i];
        inkcell_fb_draw_dial(page.state, &dial);
        for (int y = 20; y < 20 + side; ++y) {
            if (dial_at(&page, 20 + side / 2, y) == ground) {
                break;
            }
            ++measured[i];
        }
    }
    INKCELL_TEST_FAIL_IF(measured[0] != thick,
                         "a dial naming no thickness should take the state's");
    INKCELL_TEST_FAIL_IF(measured[1] != thin, "a dial naming one should be drawn at exactly that");

    /*
     * And the box the helper calls the smallest worth drawing at that scale is one the draw
     * accepts, which is the half of this that used to draw nothing at all.
     */
    inkcell_fb_clear(page.state, inkcell_fb_color(page.state, INKCELL_COLOR_BG));
    const int least = inkcell_fb_dial_min_side(page.state, small);
    struct inkcell_fb_dial compact = dial_of(500, 0x0D53);
    compact.thickness = thin;
    compact.rect = (struct inkcell_fb_rect){.x = 20, .y = 20, .w = least, .h = least};
    inkcell_fb_draw_dial(page.state, &compact);
    INKCELL_TEST_FAIL_IF(dial_at(&page, 20 + least / 2, 20) == ground,
                         "a box at the helper's own minimum should draw at that scale");

    inkcell_capture_close(page.capture);
    record_success(test_name);
}
