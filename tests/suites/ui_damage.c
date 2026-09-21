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

static struct inkcell_backend_fb_state damage_state(uint8_t *mapping, size_t size) {
    struct inkcell_backend_fb_state state = {0};
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
    struct inkcell_backend_fb_state state = damage_state(mapping, sizeof mapping);

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
    struct inkcell_backend_fb_state state = damage_state(mapping, sizeof mapping);

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
    struct inkcell_backend_fb_state state = damage_state(mapping, sizeof mapping);
    state.surface.bytes_per_pixel = 0U;

    INKCELL_TEST_FAIL_IF(inkcell_fb_copy_damage(&state, frame, previous, true, false) != 0U,
                         "a surface with no pixel size must be refused");
    for (size_t i = 0U; i < sizeof mapping; ++i) {
        INKCELL_TEST_FAIL_IF(mapping[i] != 0xA5, "a refused copy must write nothing");
    }
    record_success(test_name);
}
