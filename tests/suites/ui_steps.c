#define _POSIX_C_SOURCE 200809L

/*
 * The steps: a job's stages as a row of markers.
 *
 * What is worth pinning is the reading, not the picture - the golden sheet has the picture. A
 * stage behind is filled, the stage under way is ringed, a stage to come is only a track; a
 * stage that stopped says so in a colour of its own; and on a row too narrow for every name,
 * the name that survives is the one under way.
 */

#include "framework/inkcell_test.h"

#include "inkcell/ui/fb_capture.h"
#include "inkcell/ui/fb_draw.h"
#include "inkcell/ui/widgets/meter.h"

#include <stdint.h>

#define STEPS_W 480U
#define STEPS_H 120U

struct steps_page {
    struct inkcell_capture *capture;
    struct inkcell_draw_state *state;
    const uint8_t *pixels;
    size_t stride;
};

static bool steps_open(struct steps_page *page) {
    page->capture = NULL;
    if (inkcell_capture_open(&page->capture, STEPS_W, STEPS_H, INKCELL_SCALE(4)) < 0) {
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
    inkcell_fb_clear(page->state, inkcell_fb_color(page->state, INKCELL_COLOR_BG));
    return true;
}

static uint32_t steps_at(const struct steps_page *page, int x, int y) {
    const uint8_t *p = page->pixels + (size_t)y * page->stride + (size_t)x * 4U;
    return ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0];
}

/* Whether anything but the ground was drawn in a box - the one question a name's presence
   needs answered, without depending on where its glyphs land. */
static bool steps_inked(const struct steps_page *page, int x, int y, int w, int h) {
    const uint32_t ground = steps_at(page, 0, STEPS_H - 1);
    for (int py = y; py < y + h; ++py) {
        for (int px = x; px < x + w; ++px) {
            if (steps_at(page, px, py) != ground) {
                return true;
            }
        }
    }
    return false;
}

static const char *const k_labels[] = {"Fetch", "Check", "Write", "Done"};

static struct inkcell_fb_steps steps_of(const struct steps_page *page, int w, size_t current,
                                        bool halted) {
    const struct inkcell_fb_steps steps = {
        .rect = {.x = 0,
                 .y = 0,
                 .w = w,
                 .h = inkcell_fb_steps_height(page->state, page->state->scale)},
        .labels = k_labels,
        .count = 4U,
        .current = current,
        .tone = INKCELL_TONE_PRIMARY,
        .halted = halted,
        .ground = INKCELL_COLOR_BG,
    };
    return steps;
}

/* The centre of stage `i`'s marker on a row `w` wide. */
static void steps_centre(const struct steps_page *page, int w, size_t i, int *x, int *y) {
    const int column = w / 4;
    *x = column * (int)i + column / 2;
    *y = (inkcell_fb_steps_height(page->state, page->state->scale) -
          inkcell_scale_px((int)inkcell_fb_font(page->state)->height,
                           inkcell_fb_type_style(page->state, INKCELL_TYPE_LABEL).scale) -
          inkcell_fb_space(page->state, INKCELL_SPACE_SM)) /
         2;
}

INKCELL_TEST_CASE(steps_mark_behind_under_way_and_to_come_apart, unit) {
    struct steps_page page;
    INKCELL_TEST_FAIL_IF(!steps_open(&page), "the capture should open");
    const uint32_t ground = steps_at(&page, 0, 0);

    const struct inkcell_fb_steps steps = steps_of(&page, (int)STEPS_W, 2U, false);
    inkcell_fb_draw_steps(page.state, &steps);

    int x = 0;
    int y = 0;
    steps_centre(&page, (int)STEPS_W, 2U, &x, &y);
    const uint32_t now = steps_at(&page, x, y);
    steps_centre(&page, (int)STEPS_W, 3U, &x, &y);
    const uint32_t later = steps_at(&page, x, y);
    INKCELL_TEST_FAIL_IF(now == ground, "the stage under way should carry a mark in its middle");
    INKCELL_TEST_FAIL_IF(later != ground, "a stage to come should be an empty ring");

    inkcell_capture_close(page.capture);
    record_success(test_name);
}

INKCELL_TEST_CASE(steps_a_halted_stage_changes_only_its_own_marker, unit) {
    struct steps_page page;
    INKCELL_TEST_FAIL_IF(!steps_open(&page), "the capture should open");

    int x = 0;
    int y = 0;
    const struct inkcell_fb_steps going = steps_of(&page, (int)STEPS_W, 2U, false);
    inkcell_fb_draw_steps(page.state, &going);
    steps_centre(&page, (int)STEPS_W, 0U, &x, &y);
    const uint32_t done_going = steps_at(&page, x - 3, y + 3);
    steps_centre(&page, (int)STEPS_W, 2U, &x, &y);
    const uint32_t now_going = steps_at(&page, x - 3, y + 3);

    inkcell_fb_clear(page.state, inkcell_fb_color(page.state, INKCELL_COLOR_BG));
    const struct inkcell_fb_steps stopped = steps_of(&page, (int)STEPS_W, 2U, true);
    inkcell_fb_draw_steps(page.state, &stopped);
    steps_centre(&page, (int)STEPS_W, 0U, &x, &y);
    const uint32_t done_stopped = steps_at(&page, x - 3, y + 3);
    steps_centre(&page, (int)STEPS_W, 2U, &x, &y);
    const uint32_t now_stopped = steps_at(&page, x - 3, y + 3);

    INKCELL_TEST_FAIL_IF(now_going == now_stopped, "a halted stage should look halted");
    INKCELL_TEST_FAIL_IF(done_going != done_stopped,
                         "the stages behind a halt were done, and should stay drawn done");

    inkcell_capture_close(page.capture);
    record_success(test_name);
}

INKCELL_TEST_CASE(steps_a_narrow_row_keeps_the_name_under_way, unit) {
    struct steps_page page;
    INKCELL_TEST_FAIL_IF(!steps_open(&page), "the capture should open");

    const int height = inkcell_fb_steps_height(page.state, page.state->scale);
    const int names_y = height / 2 + 1;

    const struct inkcell_fb_steps wide = steps_of(&page, (int)STEPS_W, 1U, false);
    inkcell_fb_draw_steps(page.state, &wide);
    INKCELL_TEST_FAIL_IF(!steps_inked(&page, 0, names_y, (int)STEPS_W / 4, height - names_y),
                         "a row with the room should name every stage");

    /* Too narrow for four names side by side: the first stage's column goes quiet, the one
       under way keeps its name. */
    inkcell_fb_clear(page.state, inkcell_fb_color(page.state, INKCELL_COLOR_BG));
    const int narrow = 120;
    const struct inkcell_fb_steps tight = steps_of(&page, narrow, 1U, false);
    inkcell_fb_draw_steps(page.state, &tight);
    const int marker_bottom = height - names_y;
    INKCELL_TEST_FAIL_IF(steps_inked(&page, 0, height - marker_bottom / 2, 4, marker_bottom / 2),
                         "a row without the room should not name a stage behind");
    INKCELL_TEST_FAIL_IF(!steps_inked(&page, 0, names_y, narrow, height - names_y),
                         "a row without the room should still name the stage under way");

    inkcell_capture_close(page.capture);
    record_success(test_name);
}
