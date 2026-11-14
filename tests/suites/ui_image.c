/*
 * A picture made to fit: where it lands in a box, contained or covering, and its pixels scaled -
 * averaged smaller, so a fine pattern turns grey rather than to whichever pixel was picked.
 */
#include "framework/inkcell_test.h"

#include "inkcell/ui/image.h"

#include <string.h>

INKCELL_TEST_CASE(image_fit_contains_and_covers, unit) {
    /* A tall screenshot in a square cell. */
    struct inkcell_image_fit f = inkcell_image_fit(1080U, 2400U, 10, 20, 300, 300, false);
    INKCELL_TEST_FAIL_IF(f.h != 300 || f.w != 135 || f.y != 20 || f.x != 10 + (300 - 135) / 2,
                         "contained: as tall as the box, centred across");
    INKCELL_TEST_FAIL_IF(f.src_w != 1080U || f.src_h != 2400U || f.src_x != 0U,
                         "all of the picture");
    f = inkcell_image_fit(1080U, 2400U, 10, 20, 300, 300, true);
    INKCELL_TEST_FAIL_IF(f.x != 10 || f.y != 20 || f.w != 300 || f.h != 300,
                         "covering: the whole box");
    INKCELL_TEST_FAIL_IF(f.src_w != 1080U || f.src_h != 1080U || f.src_y != (2400U - 1080U) / 2U,
                         "its middle, square");
    /* A wide picture covering a tall box: its sides cut. */
    f = inkcell_image_fit(400U, 100U, 0, 0, 100, 200, true);
    INKCELL_TEST_FAIL_IF(f.src_h != 100U || f.src_w != 50U || f.src_x != 175U, "the sides cut");
    /* And contained: as wide as the box. */
    f = inkcell_image_fit(400U, 100U, 0, 0, 100, 200, false);
    INKCELL_TEST_FAIL_IF(f.w != 100 || f.h != 25 || f.y != (200 - 25) / 2, "as wide as the box");
    f = inkcell_image_fit(0U, 100U, 5, 6, 100, 200, false);
    INKCELL_TEST_FAIL_IF(f.w != 0 || f.h != 0 || f.x != 5, "nothing fits to nothing");
    record_success(test_name);
}

INKCELL_TEST_CASE(image_scale_averages_down_and_repeats_up, unit) {
    /* A 4 x 2 checkerboard of black and white, to 2 x 1: each pixel half of each, mid-grey. */
    uint8_t src[2][4][4];
    for (unsigned y = 0U; y < 2U; ++y) {
        for (unsigned x = 0U; x < 4U; ++x) {
            const uint8_t v = ((x + y) % 2U) == 0U ? 0U : 254U;
            src[y][x][0] = src[y][x][1] = src[y][x][2] = v;
            src[y][x][3] = 255U;
        }
    }
    uint8_t dst[1][2][4];
    memset(dst, 0xAB, sizeof dst);
    inkcell_image_scale_bgra(&src[0][0][0], 4U, 2U, 16U, &dst[0][0][0], 2U, 1U, 8U);
    INKCELL_TEST_FAIL_IF(dst[0][0][0] != 127U || dst[0][1][2] != 127U || dst[0][0][3] != 255U,
                         "averaged to grey, opaque still");

    /* One red pixel to 3 x 2: every pixel red. */
    const uint8_t red[4] = {0U, 0U, 255U, 255U};
    uint8_t big[2][3][4];
    inkcell_image_scale_bgra(red, 1U, 1U, 4U, &big[0][0][0], 3U, 2U, 12U);
    INKCELL_TEST_FAIL_IF(big[1][2][2] != 255U || big[1][2][0] != 0U || big[0][1][3] != 255U,
                         "larger: the nearest pixel");

    /* A stride wider than the row: the rest is not read. */
    uint8_t padded[2][3][4];
    memset(padded, 0, sizeof padded);
    padded[0][2][0] = padded[1][2][0] = 200U; /* past the width */
    uint8_t one[4];
    inkcell_image_scale_bgra(&padded[0][0][0], 2U, 2U, 12U, one, 1U, 1U, 4U);
    INKCELL_TEST_FAIL_IF(one[0] != 0U, "only the width is read");
    record_success(test_name);
}
