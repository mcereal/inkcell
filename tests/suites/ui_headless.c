#define _POSIX_C_SOURCE 200809L

/*
 * The headless backend, the frame hook every pixel backend now has, and the writer that turns
 * what comes back into a file.
 *
 * What is held here is the part a driver leans on: that a frame comes back only once one was
 * drawn, that what comes back is the frame and not a copy of something older, and that the
 * writer reads a pixel the way the rasteriser wrote it - including a format that describes its
 * channels, which is the Brick's fb0 and not the off-screen page.
 */

#include "framework/inkcell_test.h"

#include "inkcell/ui/fb_capture.h"
#include "inkcell/ui/fb_draw.h"
#include "inkcell/ui/headless.h"
#include "inkcell/ui/key.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define HEADLESS_TEST_WIDTH 8U
#define HEADLESS_TEST_HEIGHT 4U

struct headless_test_app {
    unsigned renders;
    uint32_t fill;
};

static void headless_test_render(struct inkcell_draw_state *state, const void *snapshot,
                                 void *ctx) {
    struct headless_test_app *const app = (struct headless_test_app *)ctx;
    (void)snapshot;
    app->renders += 1U;
    for (uint32_t y = 0U; y < state->surface.height; ++y) {
        uint8_t *const row = state->surface.pixels + (size_t)y * state->surface.stride;
        for (uint32_t x = 0U; x < state->surface.width; ++x) {
            memcpy(row + (size_t)x * 4U, &app->fill, sizeof app->fill);
        }
    }
}

/* The whole of a small file, or NULL. */
static uint8_t *headless_test_slurp(const char *path, size_t *len) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }
    uint8_t *data = malloc(4096U);
    *len = data != NULL ? fread(data, 1U, 4096U, file) : 0U;
    fclose(file);
    return data;
}

static void headless_test_path(char *out, size_t len) {
    const char *dir = getenv("TMPDIR");
    snprintf(out, len, "%s/inkcell-headless-%ld.ppm", dir != NULL ? dir : "/tmp", (long)getpid());
}

INKCELL_TEST_CASE(headless_backend_hands_back_the_frame_it_drew, unit) {
    const struct inkcell_backend *const backend = inkcell_backend_headless();
    INKCELL_TEST_FAIL_IF(backend == NULL || strcmp(backend->name, "headless") != 0,
                         "the headless backend must exist and name itself");

    struct headless_test_app app = {.fill = 0xFF102030U};
    const struct inkcell_fb_app vtable = {.ctx = &app, .render = headless_test_render};
    struct inkcell_backend_headless_context context = {
        .app = &vtable, .width = HEADLESS_TEST_WIDTH, .height = HEADLESS_TEST_HEIGHT};
    void *state = NULL;
    INKCELL_TEST_FAIL_IF(backend->init(&state, &context) < 0 || state == NULL,
                         "the headless backend needs nothing to open");

    struct inkcell_surface frame;
    INKCELL_TEST_FAIL_IF_CLEANUP(backend->frame(state, NULL, &frame),
                                 backend->shutdown(state, NULL),
                                 "no frame must come back before one was drawn");

    const int snapshot = 0;
    backend->present(state, &snapshot, NULL);
    INKCELL_TEST_FAIL_IF_CLEANUP(app.renders != 1U, backend->shutdown(state, NULL),
                                 "present must draw the frame");
    INKCELL_TEST_FAIL_IF_CLEANUP(!backend->frame(state, NULL, &frame),
                                 backend->shutdown(state, NULL),
                                 "a frame must come back once one was drawn");
    INKCELL_TEST_FAIL_IF_CLEANUP(frame.width != HEADLESS_TEST_WIDTH ||
                                     frame.height != HEADLESS_TEST_HEIGHT,
                                 backend->shutdown(state, NULL), "...at the size it was opened");

    app.fill = 0xFFA0B0C0U;
    backend->present(state, &snapshot, NULL);
    (void)backend->frame(state, NULL, &frame);
    uint32_t pixel;
    memcpy(&pixel, frame.pixels, sizeof pixel);
    backend->shutdown(state, NULL);
    INKCELL_TEST_FAIL_IF(pixel != 0xFFA0B0C0U, "...and it must be the latest one");
    record_success(test_name);
}

/* The off-screen page's format: B,G,R,X in memory, described by bit depth alone. */
INKCELL_TEST_CASE(surface_ppm_reads_an_undescribed_page_as_bgrx, unit) {
    uint8_t pixels[2U * 4U];
    const uint32_t words[2] = {0xFF112233U, 0xFFFFFFFFU};
    memcpy(pixels, words, sizeof words);
    const struct inkcell_surface surface = {
        .pixels = pixels,
        .size = sizeof pixels,
        .width = 2U,
        .height = 1U,
        .stride = 8U,
        .bytes_per_pixel = 4U,
        .format = {.bits_per_pixel = 32U},
    };

    char path[256];
    headless_test_path(path, sizeof path);
    INKCELL_TEST_FAIL_IF(inkcell_surface_write_ppm(&surface, path) != 0, "the write must succeed");
    size_t len = 0U;
    uint8_t *data = headless_test_slurp(path, &len);
    unlink(path);

    static const uint8_t expected[] = {'P', '6',  '\n', '2',  ' ',  '1',  '\n', '2', '5',
                                       '5', '\n', 0x11, 0x22, 0x33, 0xFF, 0xFF, 0xFF};
    const bool same = data != NULL && len == sizeof expected && memcmp(data, expected, len) == 0;
    free(data);
    INKCELL_TEST_FAIL_IF(!same, "a pixel must come out as R, G, B");
    record_success(test_name);
}

/* A format that names its channels - RGB565 here - is read through them, and a full channel
   reads as 255 rather than as the 248 a bare shift would give. */
INKCELL_TEST_CASE(surface_ppm_reads_a_described_format_through_its_channels, unit) {
    const uint16_t word = 0xF800U; /* full red, nothing else */
    uint8_t pixels[2];
    memcpy(pixels, &word, sizeof word);
    const struct inkcell_surface surface = {
        .pixels = pixels,
        .size = sizeof pixels,
        .width = 1U,
        .height = 1U,
        .stride = 2U,
        .bytes_per_pixel = 2U,
        .format = {.r = {11U, 5U}, .g = {5U, 6U}, .b = {0U, 5U}, .bits_per_pixel = 16U},
    };

    char path[256];
    headless_test_path(path, sizeof path);
    INKCELL_TEST_FAIL_IF(inkcell_surface_write_ppm(&surface, path) != 0, "the write must succeed");
    size_t len = 0U;
    uint8_t *data = headless_test_slurp(path, &len);
    unlink(path);

    const bool red = data != NULL && len >= 3U && data[len - 3U] == 0xFFU &&
                     data[len - 2U] == 0x00U && data[len - 1U] == 0x00U;
    free(data);
    INKCELL_TEST_FAIL_IF(!red, "full red in RGB565 must read as 255, 0, 0");
    record_success(test_name);
}

/* A 10-bit channel (XRGB2101010) keeps its top eight bits rather than reading as nothing. */
INKCELL_TEST_CASE(surface_ppm_reads_a_channel_wider_than_eight_bits, unit) {
    const uint32_t word = 0x3FFU << 20; /* full red */
    uint8_t pixels[4];
    memcpy(pixels, &word, sizeof word);
    const struct inkcell_surface surface = {
        .pixels = pixels,
        .size = sizeof pixels,
        .width = 1U,
        .height = 1U,
        .stride = 4U,
        .bytes_per_pixel = 4U,
        .format = {.r = {20U, 10U}, .g = {10U, 10U}, .b = {0U, 10U}, .bits_per_pixel = 32U},
    };

    char path[256];
    headless_test_path(path, sizeof path);
    INKCELL_TEST_FAIL_IF(inkcell_surface_write_ppm(&surface, path) != 0, "the write must succeed");
    size_t len = 0U;
    uint8_t *data = headless_test_slurp(path, &len);
    unlink(path);

    const bool red = data != NULL && len >= 3U && data[len - 3U] == 0xFFU &&
                     data[len - 2U] == 0x00U && data[len - 1U] == 0x00U;
    free(data);
    INKCELL_TEST_FAIL_IF(!red, "full red in a 10-bit channel must read as 255, 0, 0");
    record_success(test_name);
}

/* A surface shorter than its geometry is refused, not read past. */
INKCELL_TEST_CASE(surface_ppm_refuses_a_surface_shorter_than_its_rows, unit) {
    uint8_t pixels[4U * 4U] = {0};
    const struct inkcell_surface surface = {
        .pixels = pixels,
        .size = sizeof pixels,
        .width = 4U,
        .height = 2U, /* two rows of 16 bytes, in 16 */
        .stride = 16U,
        .bytes_per_pixel = 4U,
        .format = {.bits_per_pixel = 32U},
    };
    char path[256];
    headless_test_path(path, sizeof path);
    const int written = inkcell_surface_write_ppm(&surface, path);
    unlink(path);
    INKCELL_TEST_FAIL_IF(written != -EINVAL, "a short surface must be refused");
    record_success(test_name);
}

INKCELL_TEST_CASE(key_names_round_trip, unit) {
    for (int key = INKCELL_KEY_UP; key <= INKCELL_KEY_SELECT; ++key) {
        const char *const name = inkcell_key_name((enum inkcell_key)key);
        INKCELL_TEST_FAIL_IF(name == NULL, "every key must have a name");
        INKCELL_TEST_FAIL_IF(inkcell_key_from_name(name) != (enum inkcell_key)key,
                             "...that reads back as the same key");
    }
    INKCELL_TEST_FAIL_IF(inkcell_key_from_name("L1") != INKCELL_KEY_L1,
                         "case must not matter on the way in");
    INKCELL_TEST_FAIL_IF(inkcell_key_from_name("menu") != INKCELL_KEY_NONE,
                         "a name that is not a key must be NONE");
    INKCELL_TEST_FAIL_IF(inkcell_key_name(INKCELL_KEY_NONE) != NULL, "NONE has no name");
    record_success(test_name);
}
