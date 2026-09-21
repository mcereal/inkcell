#define _POSIX_C_SOURCE 200809L

/*
 * The span arithmetic between a rendered frame and the surface it is copied into.
 *
 * inkcell_fb_copy_damage() is the half of presenting that is not the device's: it decides which
 * bytes of a frame differ from the one before it, and that decision is the same whether what
 * receives them is an mmap of /dev/fb0, the second page behind it, or a texture somebody
 * uploads. The device half - the pan, the descriptor - is in src/fb/fb.c and is not tested
 * here, because there is no panel in a container to test it against.
 *
 * The state is built by hand rather than through inkcell_capture_open(), which is the point of
 * the case: the geometry that catches a mistake here is a deliberately awkward one - three
 * pixels of content in a sixteen-byte row, so the stride has four bytes of padding that must
 * never be copied and never be compared - and a capture allocates tidy rows.
 *
 * It came from mesh-client, which had held it since before this code was inkcell's; a component
 * without the cases that held it is a downgrade, so it moved when the function became public.
 */

#include "framework/inkcell_test.h"

#include "inkcell/ui/fb_draw.h"

#include <string.h>

/* Three 4-byte pixels in a 16-byte row: 4 bytes of stride padding, twice over. */
#define DAMAGE_STRIDE 16U
#define DAMAGE_ROWS 2U
#define DAMAGE_PAGE (DAMAGE_STRIDE * DAMAGE_ROWS)

static struct inkcell_draw_state damage_state(uint8_t *mapping, size_t size) {
    struct inkcell_draw_state state = {0};
    state.surface = (struct inkcell_surface){
        .pixels = mapping,
        .size = size,
        .width = 3U, /* a padded row */
        .height = DAMAGE_ROWS,
        .stride = DAMAGE_STRIDE,
        .bytes_per_pixel = 4U,
        .format = {.bits_per_pixel = 32U},
    };
    return state;
}

INKCELL_TEST_CASE(damage_preserves_mirror_and_padding, unit) {
    /* Room for two pages and a bit, so "never wrote past the pages" is checkable. */
    uint8_t mapping[80];
    uint8_t previous[DAMAGE_PAGE] = {0};
    uint8_t frame[DAMAGE_PAGE];
    memset(mapping, 0xA5, sizeof mapping);
    memset(frame, 0x31, sizeof frame);
    struct inkcell_draw_state state = damage_state(mapping, sizeof mapping);

    INKCELL_TEST_FAIL_IF(inkcell_fb_copy_damage(&state, frame, previous, true, true) != 64U,
                         "first frame must initialize both pages");
    INKCELL_TEST_FAIL_IF(memcmp(mapping, frame, DAMAGE_PAGE) != 0 ||
                             memcmp(mapping + DAMAGE_PAGE, frame, DAMAGE_PAGE) != 0,
                         "display pages must match the rendered frame");

    INKCELL_TEST_FAIL_IF(inkcell_fb_copy_damage(&state, frame, previous, false, true) != 0U,
                         "an unchanged frame must not write display memory");

    frame[19] ^= 1U;
    INKCELL_TEST_FAIL_IF(inkcell_fb_copy_damage(&state, frame, previous, false, true) != 8U,
                         "one changed pixel must write only one pixel per page");
    INKCELL_TEST_FAIL_IF(memcmp(mapping, frame, DAMAGE_PAGE) != 0 ||
                             memcmp(mapping + DAMAGE_PAGE, frame, DAMAGE_PAGE) != 0,
                         "partial updates must preserve both complete pages");
    for (size_t i = 2U * DAMAGE_PAGE; i < sizeof mapping; ++i) {
        INKCELL_TEST_FAIL_IF(mapping[i] != 0xA5, "updates must not touch extra virtual pages");
    }

    frame[0] ^= 1U;
    INKCELL_TEST_FAIL_IF(inkcell_fb_copy_damage(&state, frame, previous, false, false) != 4U,
                         "a single-page display must receive one copy");
    record_success(test_name);
}

/*
 * A mirror is refused when there is nowhere to put it.
 *
 * The caller states how many pages the thing it is presenting to has, and a caller that says
 * two over a surface sized for one used to be a write `page_bytes` past the end. The guard is
 * inside, so the answer to "may I mirror" is the surface's rather than the caller's.
 */
INKCELL_TEST_CASE(damage_refuses_a_mirror_that_does_not_fit, unit) {
    uint8_t mapping[DAMAGE_PAGE];
    uint8_t previous[DAMAGE_PAGE] = {0};
    uint8_t frame[DAMAGE_PAGE];
    memset(mapping, 0xA5, sizeof mapping);
    memset(frame, 0x31, sizeof frame);
    struct inkcell_draw_state state = damage_state(mapping, sizeof mapping);

    INKCELL_TEST_FAIL_IF(inkcell_fb_copy_damage(&state, frame, previous, true, true) != DAMAGE_PAGE,
                         "a one-page surface must be written once however the caller asked");
    INKCELL_TEST_FAIL_IF(memcmp(mapping, frame, DAMAGE_PAGE) != 0,
                         "the one page must still hold the frame");
    record_success(test_name);
}

/* A surface with no pixel size cannot be measured in spans, and must not be guessed at. */
INKCELL_TEST_CASE(damage_refuses_a_surface_with_no_pixel_size, unit) {
    uint8_t mapping[DAMAGE_PAGE];
    uint8_t previous[DAMAGE_PAGE] = {0};
    uint8_t frame[DAMAGE_PAGE];
    memset(mapping, 0xA5, sizeof mapping);
    memset(frame, 0x31, sizeof frame);
    struct inkcell_draw_state state = damage_state(mapping, sizeof mapping);
    state.surface.bytes_per_pixel = 0U;

    INKCELL_TEST_FAIL_IF(inkcell_fb_copy_damage(&state, frame, previous, true, false) != 0U,
                         "a surface with no pixel size must be refused");
    for (size_t i = 0U; i < sizeof mapping; ++i) {
        INKCELL_TEST_FAIL_IF(mapping[i] != 0xA5, "a refused copy must write nothing");
    }
    record_success(test_name);
}

/*
 * ---- the same damage, as rectangles ----
 *
 * The other consumer of the row scan. A copy writes spans straight into a destination and can
 * afford to treat every row on its own; something handing a frame to a GPU has to name
 * rectangles, so consecutive damaged rows are gathered into bands. What these cases hold is the
 * property everything above depends on and none of them can check for itself: the rectangles
 * that come back cover *every* pixel that changed. One short of that is a stale row left in
 * front of the reader, on a frame that reported success.
 */

/* Every damaged row in one band, and the band as wide as its widest row. */
INKCELL_TEST_CASE(damage_rects_gather_consecutive_rows, unit) {
    uint8_t previous[DAMAGE_PAGE] = {0};
    uint8_t frame[DAMAGE_PAGE];
    uint8_t mapping[DAMAGE_PAGE];
    memset(frame, 0, sizeof frame);
    struct inkcell_draw_state state = damage_state(mapping, sizeof mapping);

    struct inkcell_fb_damage_rect rects[4];
    INKCELL_TEST_FAIL_IF(inkcell_fb_damage_rects(&state, frame, previous, false, rects, 4U) != 0U,
                         "an unchanged frame has no rectangles");

    /* Pixel 0 of row 0 and pixel 2 of row 1: two rows running, so one band three wide. */
    frame[0] ^= 1U;
    frame[DAMAGE_STRIDE + 8U] ^= 1U;
    const size_t count = inkcell_fb_damage_rects(&state, frame, previous, false, rects, 4U);
    INKCELL_TEST_FAIL_IF(count != 1U, "consecutive damaged rows must be one band");
    INKCELL_TEST_FAIL_IF(rects[0].x != 0 || rects[0].y != 0 || rects[0].right != 3 ||
                             rects[0].bottom != 2,
                         "a band must span the widest row it covers");
    INKCELL_TEST_FAIL_IF(memcmp(previous, frame, DAMAGE_PAGE) != 0,
                         "reporting a rectangle must bring the comparison up to date");
    record_success(test_name);
}

/*
 * Out of rectangles, and not out of damage.
 *
 * Two separated bands into a single rectangle: the second cannot start one of its own, so the
 * first grows over it. That re-sends the untouched rows between them, which is bandwidth; what
 * it must never do is leave the second band out, which would be a row that changed and was
 * never handed over.
 */
INKCELL_TEST_CASE(damage_rects_widen_rather_than_drop_a_band, unit) {
    enum { ROWS = 5U };
    const size_t page = DAMAGE_STRIDE * ROWS;
    uint8_t previous[DAMAGE_STRIDE * ROWS] = {0};
    uint8_t frame[DAMAGE_STRIDE * ROWS];
    uint8_t mapping[DAMAGE_STRIDE * ROWS];
    memset(frame, 0, page);
    struct inkcell_draw_state state = damage_state(mapping, page);
    state.surface.height = ROWS;

    frame[0] ^= 1U;                       /* row 0 */
    frame[4U * DAMAGE_STRIDE + 8U] ^= 1U; /* row 4, with three clean rows between */

    struct inkcell_fb_damage_rect one[1];
    const size_t count = inkcell_fb_damage_rects(&state, frame, previous, false, one, 1U);
    INKCELL_TEST_FAIL_IF(count != 1U, "one rectangle is all that was offered");
    INKCELL_TEST_FAIL_IF(one[0].y != 0 || one[0].bottom != 5,
                         "the one rectangle must still cover both bands");
    INKCELL_TEST_FAIL_IF(one[0].x != 0 || one[0].right != 3, "and must be wide enough for both");
    record_success(test_name);
}

/* Damage that cannot be reported must not be consumed: `previous` is how the *next* frame
   knows, and a row quietly marked clean here is a row nothing ever hands over. */
INKCELL_TEST_CASE(damage_rects_refuse_to_forget_what_they_cannot_report, unit) {
    uint8_t previous[DAMAGE_PAGE] = {0};
    uint8_t frame[DAMAGE_PAGE];
    uint8_t mapping[DAMAGE_PAGE];
    memset(frame, 0x31, sizeof frame);
    struct inkcell_draw_state state = damage_state(mapping, sizeof mapping);

    struct inkcell_fb_damage_rect rects[1];
    INKCELL_TEST_FAIL_IF(inkcell_fb_damage_rects(&state, frame, previous, true, rects, 0U) != 0U,
                         "no room for a rectangle must report nothing");
    for (size_t i = 0U; i < DAMAGE_PAGE; ++i) {
        INKCELL_TEST_FAIL_IF(previous[i] != 0U, "...and must leave the comparison untouched");
    }
    record_success(test_name);
}

/*
 * A rectangle names pixels, and a stride may hold bytes that are not any.
 *
 * The two halves of one mistake, and the reason it is a mistake here and not in the copy
 * above: inkcell_fb_copy_damage() writes bytes into a destination that has the padding, so
 * sending it is harmless; a rectangle is a coordinate on a panel, and a coordinate past its
 * width is one nothing can accept. A presenter handed such a rectangle does not clip it - it
 * refuses the upload, and the whole frame is lost rather than a few padding bytes.
 */
INKCELL_TEST_CASE(damage_rects_stop_at_the_last_pixel, unit) {
    uint8_t previous[DAMAGE_PAGE] = {0};
    uint8_t frame[DAMAGE_PAGE];
    uint8_t mapping[DAMAGE_PAGE];
    memset(frame, 0x31, sizeof frame);
    struct inkcell_draw_state state = damage_state(mapping, sizeof mapping);

    /* A forced frame's span is the whole stride, padding included - the case that would
       otherwise report four pixels across a surface three pixels wide. */
    struct inkcell_fb_damage_rect rects[4];
    const size_t count = inkcell_fb_damage_rects(&state, frame, previous, true, rects, 4U);
    INKCELL_TEST_FAIL_IF(count != 1U, "a forced frame is one band over every row");
    INKCELL_TEST_FAIL_IF(rects[0].x != 0 || rects[0].right != (int)state.surface.width,
                         "a rectangle must stop at the surface's last pixel, not its stride");
    record_success(test_name);
}

/* ...and a row whose only difference is in that padding has nothing to report at all. */
INKCELL_TEST_CASE(damage_rects_ignore_a_change_only_in_the_padding, unit) {
    uint8_t previous[DAMAGE_PAGE];
    uint8_t frame[DAMAGE_PAGE];
    uint8_t mapping[DAMAGE_PAGE];
    memset(frame, 0x31, sizeof frame);
    memset(previous, 0x31, sizeof previous);
    struct inkcell_draw_state state = damage_state(mapping, sizeof mapping);

    /* Byte 12 of row 0: past the third pixel, inside the four bytes of padding. */
    frame[12] ^= 1U;
    struct inkcell_fb_damage_rect rects[4];
    INKCELL_TEST_FAIL_IF(inkcell_fb_damage_rects(&state, frame, previous, false, rects, 4U) != 0U,
                         "a difference confined to the padding is not a rectangle");
    INKCELL_TEST_FAIL_IF(previous[12] != frame[12],
                         "...but is still consumed, or it is rescanned for the rest of the run");
    record_success(test_name);
}
