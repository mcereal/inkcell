#define _POSIX_C_SOURCE 200809L

/*
 * The scaffold: which arrangement a width class gets, where each piece of it lands, and that the
 * compact arrangement a handheld keeps is the frame it always had.
 *
 * All of it is geometry read back off `struct inkcell_fb_scaffold_frame` and the focus map, bar
 * the first case, which compares pixels - because "moving onto the scaffold changes nothing on
 * the device" is a claim about pixels and nothing weaker holds it.
 */

#include "framework/inkcell_test.h"

#include "inkcell/ui/fb_capture.h"
#include "inkcell/ui/focus.h"
#include "inkcell/ui/widgets/scaffold.h"
#include "inkcell/ui/widgets/scroll.h"

#include <stdlib.h>
#include <string.h>

#define SCAFFOLD_WIDE_W 2240U
#define SCAFFOLD_WIDE_H 1260U

static const struct inkcell_fb_chip k_destinations[] = {
    {.icon = INKCELL_ICON_MESSAGES, .label = "Messages", .badge = "3", .focus_id = 11U},
    {.icon = INKCELL_ICON_NODES, .label = "Nodes", .focus_id = 12U},
    {.icon = INKCELL_ICON_SETTINGS, .label = "Settings", .focus_id = 13U},
};
#define SCAFFOLD_COUNT (sizeof k_destinations / sizeof k_destinations[0])

static struct inkcell_capture *scaffold_open(uint32_t width, uint32_t height, int scale,
                                             struct inkcell_draw_state **state) {
    struct inkcell_capture *capture = NULL;
    if (inkcell_capture_open(&capture, width, height, scale) < 0) {
        return NULL;
    }
    inkcell_capture_set_scale(capture, scale);
    *state = inkcell_capture_state(capture);
    return capture;
}

static bool box_inside(struct inkcell_box inner, struct inkcell_box outer) {
    return inner.x >= outer.x && inner.y >= outer.y && inner.x + inner.w <= outer.x + outer.w &&
           inner.y + inner.h <= outer.y + outer.h;
}

INKCELL_TEST_CASE(scaffold_compact_top_is_the_hand_built_frame, unit) {
    struct inkcell_draw_state *manual = NULL;
    struct inkcell_draw_state *scaffolded = NULL;
    struct inkcell_capture *a =
        scaffold_open(INKCELL_CAPTURE_WIDTH, INKCELL_CAPTURE_HEIGHT, INKCELL_SCALE(4), &manual);
    struct inkcell_capture *b =
        scaffold_open(INKCELL_CAPTURE_WIDTH, INKCELL_CAPTURE_HEIGHT, INKCELL_SCALE(4), &scaffolded);
    INKCELL_TEST_FAIL_IF(a == NULL || b == NULL, "the captures should open");
    INKCELL_TEST_FAIL_IF(inkcell_fb_width_class(manual) != INKCELL_WIDTH_COMPACT,
                         "the panel at the body scale is compact");

    const struct inkcell_fb_banner banner = {.icon = INKCELL_ICON_DOWNLOAD, .text = "Update"};
    const struct inkcell_fb_app_bar bar = {.title = "Settings"};
    const struct inkcell_button_action items[] = {{INKCELL_BUTTON_A, INKCELL_STR_NONE}};
    const struct inkcell_fb_action_bar actions = {.items = items, .count = 1U, .status = "ok"};

    /* The sequence every application wrote by hand. */
    inkcell_fb_clear(manual, inkcell_fb_color(manual, INKCELL_COLOR_BG));
    struct inkcell_fb_layout layout = inkcell_fb_layout_begin(manual, true, true);
    inkcell_fb_draw_nav_bar(manual, &layout, k_destinations, SCAFFOLD_COUNT, 1U);
    inkcell_fb_draw_progress(manual, &layout, true);
    inkcell_fb_draw_banner(manual, &layout, &banner);
    inkcell_fb_draw_app_bar(manual, &layout, &bar);
    inkcell_fb_draw_action_bar(manual, &layout, &actions);

    /* And the scaffold asked for the same thing. */
    inkcell_fb_clear(scaffolded, inkcell_fb_color(scaffolded, INKCELL_COLOR_BG));
    const struct inkcell_fb_scaffold scaffold = {
        .destinations = k_destinations,
        .count = SCAFFOLD_COUNT,
        .active = 1U,
        .compact_nav = INKCELL_FB_COMPACT_NAV_TOP,
        .footer = true,
        .back = true,
        .split = true,
        .busy = true,
        .banner = &banner,
        .app_bar = &bar,
    };
    struct inkcell_fb_scaffold_frame frame;
    inkcell_fb_scaffold_begin(scaffolded, &scaffold, &frame);
    inkcell_fb_scaffold_end(scaffolded, &frame, &actions);

    INKCELL_TEST_FAIL_IF(frame.nav != INKCELL_FB_NAV_TOP, "compact-top keeps the strip");
    INKCELL_TEST_FAIL_IF(frame.split, "a compact frame never splits, whatever the screen says");
    INKCELL_TEST_FAIL_IF(frame.layout.body_y != layout.body_y || frame.layout.rows != layout.rows ||
                             frame.layout.footer_y != layout.footer_y,
                         "the scaffold's layout should be the hand-built one");

    uint32_t w = 0U, h = 0U;
    size_t stride = 0U;
    const uint8_t *pa = inkcell_capture_pixels(a, &w, &h, &stride);
    const uint8_t *pb = inkcell_capture_pixels(b, &w, &h, &stride);
    INKCELL_TEST_FAIL_IF(pa == NULL || pb == NULL, "both frames should have pixels");
    INKCELL_TEST_FAIL_IF(memcmp(pa, pb, stride * h) != 0,
                         "compact-top should draw the hand-built frame to the pixel");

    inkcell_capture_close(a);
    inkcell_capture_close(b);
    record_success(test_name);
}

INKCELL_TEST_CASE(scaffold_compact_puts_a_bar_across_the_bottom, unit) {
    struct inkcell_draw_state *state = NULL;
    struct inkcell_capture *capture =
        scaffold_open(INKCELL_CAPTURE_WIDTH, INKCELL_CAPTURE_HEIGHT, INKCELL_SCALE(4), &state);
    INKCELL_TEST_FAIL_IF(capture == NULL, "the capture should open");

    const struct inkcell_fb_scaffold scaffold = {
        .destinations = k_destinations, .count = SCAFFOLD_COUNT, .footer = true};
    struct inkcell_fb_scaffold_frame frame;
    inkcell_fb_scaffold_begin(state, &scaffold, &frame);

    const int panel_h = (int)INKCELL_CAPTURE_HEIGHT;
    INKCELL_TEST_FAIL_IF(frame.nav != INKCELL_FB_NAV_BOTTOM, "compact defaults to the bottom bar");
    INKCELL_TEST_FAIL_IF(frame.nav_box.y + frame.nav_box.h != panel_h,
                         "the bar sits on the bottom edge");
    INKCELL_TEST_FAIL_IF(frame.nav_box.h != inkcell_fb_scaffold_bar_height(state),
                         "the bar is as tall as it says it is");
    INKCELL_TEST_FAIL_IF(frame.content.y + frame.content.h != frame.nav_box.y,
                         "the content stops where the bar starts");
    /* The action bar is reserved above the destinations, not over them. */
    INKCELL_TEST_FAIL_IF(frame.layout.footer_y >= frame.nav_box.y,
                         "the footer should be reserved above the bar");

    inkcell_fb_scaffold_end(state, &frame, NULL);
    INKCELL_TEST_FAIL_IF(!inkcell_box_is_empty(state->region), "end puts the region back");
    inkcell_capture_close(capture);
    record_success(test_name);
}

INKCELL_TEST_CASE(scaffold_medium_puts_a_rail_beside_the_body, unit) {
    struct inkcell_draw_state *state = NULL;
    /* The same panel at the smaller scale is medium - see the gallery's stack page. */
    struct inkcell_capture *capture =
        scaffold_open(INKCELL_CAPTURE_WIDTH, INKCELL_CAPTURE_HEIGHT, INKCELL_SCALE(3), &state);
    INKCELL_TEST_FAIL_IF(capture == NULL, "the capture should open");
    INKCELL_TEST_FAIL_IF(inkcell_fb_width_class(state) != INKCELL_WIDTH_MEDIUM,
                         "the panel at the smaller scale is medium");

    struct inkcell_focus_item storage[16];
    struct inkcell_focus_map map;
    inkcell_focus_begin(&map, storage, 16U);
    inkcell_fb_set_focus_map(state, &map);

    const struct inkcell_fb_scaffold scaffold = {
        .destinations = k_destinations, .count = SCAFFOLD_COUNT, .footer = true, .split = true};
    struct inkcell_fb_scaffold_frame frame;
    inkcell_fb_scaffold_begin(state, &scaffold, &frame);

    const int rail = inkcell_fb_scaffold_rail_width(state, k_destinations, SCAFFOLD_COUNT);
    INKCELL_TEST_FAIL_IF(frame.nav != INKCELL_FB_NAV_RAIL, "medium gets a rail");
    INKCELL_TEST_FAIL_IF(frame.split, "medium does not split");
    INKCELL_TEST_FAIL_IF(frame.nav_box.x != 0 || frame.nav_box.w != rail ||
                             frame.nav_box.h != (int)INKCELL_CAPTURE_HEIGHT,
                         "the rail runs the leading edge, top to bottom");
    INKCELL_TEST_FAIL_IF(frame.content.x != rail, "the content starts where the rail stops");
    /* The widgets follow the region: the column the body is drawn in is clear of the rail. */
    INKCELL_TEST_FAIL_IF(frame.layout.body_x <= rail, "the body column should clear the rail");
    INKCELL_TEST_FAIL_IF(inkcell_fb_content_x(state) <= rail,
                         "the content column should be measured inside the region");

    /* Every destination is a place to stand, inside the rail. */
    for (size_t i = 0U; i < SCAFFOLD_COUNT; ++i) {
        struct inkcell_focus_rect rect;
        INKCELL_TEST_FAIL_IF(!inkcell_focus_rect_of(&map, k_destinations[i].focus_id, &rect),
                             "each destination should be registered under its focus id");
        INKCELL_TEST_FAIL_IF(rect.x < 0 || rect.x + rect.w > rail,
                             "a destination's box should be inside the rail");
    }

    inkcell_fb_scaffold_end(state, &frame, NULL);
    inkcell_fb_set_focus_map(state, NULL);
    inkcell_capture_close(capture);
    record_success(test_name);
}

INKCELL_TEST_CASE(scaffold_rail_width_does_not_follow_the_cursor, unit) {
    struct inkcell_draw_state *state = NULL;
    struct inkcell_capture *capture =
        scaffold_open(INKCELL_CAPTURE_WIDTH, INKCELL_CAPTURE_HEIGHT, INKCELL_SCALE(3), &state);
    INKCELL_TEST_FAIL_IF(capture == NULL, "the capture should open");

    /* A rail that changed width with the active destination would reflow the body on every
       press down it. */
    int width = -1;
    for (size_t active = 0U; active < SCAFFOLD_COUNT; ++active) {
        const struct inkcell_fb_scaffold scaffold = {
            .destinations = k_destinations, .count = SCAFFOLD_COUNT, .active = active};
        struct inkcell_fb_scaffold_frame frame;
        inkcell_fb_scaffold_begin(state, &scaffold, &frame);
        inkcell_fb_scaffold_end(state, &frame, NULL);
        INKCELL_TEST_FAIL_IF(width >= 0 && frame.content.x != width,
                             "the rail should be one width whichever destination is active");
        width = frame.content.x;
    }

    inkcell_capture_close(capture);
    record_success(test_name);
}

INKCELL_TEST_CASE(scaffold_expanded_splits_a_screen_that_has_a_detail, unit) {
    struct inkcell_draw_state *state = NULL;
    struct inkcell_capture *capture =
        scaffold_open(SCAFFOLD_WIDE_W, SCAFFOLD_WIDE_H, INKCELL_SCALE(3), &state);
    INKCELL_TEST_FAIL_IF(capture == NULL, "the capture should open");
    INKCELL_TEST_FAIL_IF(inkcell_fb_width_class(state) != INKCELL_WIDTH_EXPANDED,
                         "the wide page is expanded");

    const struct inkcell_fb_scaffold scaffold = {
        .destinations = k_destinations, .count = SCAFFOLD_COUNT, .footer = true, .split = true};
    struct inkcell_fb_scaffold_frame frame;
    inkcell_fb_scaffold_begin(state, &scaffold, &frame);

    INKCELL_TEST_FAIL_IF(frame.nav != INKCELL_FB_NAV_RAIL, "expanded keeps the rail");
    INKCELL_TEST_FAIL_IF(!frame.split, "expanded splits a screen that has a detail");
    INKCELL_TEST_FAIL_IF(!box_inside(frame.list, frame.content) ||
                             !box_inside(frame.detail, frame.content),
                         "both panes are inside the content");
    INKCELL_TEST_FAIL_IF(frame.list.x + frame.list.w > frame.detail.x,
                         "the panes should not overlap");
    INKCELL_TEST_FAIL_IF(frame.detail.w <= frame.list.w,
                         "the detail, where the running text is, gets the larger share");
    INKCELL_TEST_FAIL_IF(frame.list.y != frame.detail.y || frame.list.h != frame.detail.h,
                         "the panes share their rows");
    INKCELL_TEST_FAIL_IF(frame.layout.body_x + frame.layout.body_w > frame.list.x + frame.list.w,
                         "the list's column stays in the list pane");

    const struct inkcell_fb_layout detail = inkcell_fb_scaffold_detail(state, &frame, NULL);
    INKCELL_TEST_FAIL_IF(detail.body_x < frame.detail.x, "the detail's column is in its pane");
    INKCELL_TEST_FAIL_IF(detail.body_y != frame.layout.body_y,
                         "the two panes start on the same row");
    INKCELL_TEST_FAIL_IF(detail.footer_y != frame.layout.footer_y,
                         "the two panes stop at the same footer");
    INKCELL_TEST_FAIL_IF(detail.back, "the detail is not somewhere B leaves");

    inkcell_fb_scaffold_end(state, &frame, NULL);
    INKCELL_TEST_FAIL_IF(!inkcell_box_is_empty(state->region), "end puts the region back");

    /* The same frame, for a screen without a detail: one pane, all the content. */
    const struct inkcell_fb_scaffold single = {
        .destinations = k_destinations, .count = SCAFFOLD_COUNT, .footer = true};
    inkcell_fb_scaffold_begin(state, &single, &frame);
    INKCELL_TEST_FAIL_IF(frame.split, "a screen without a detail is one pane");
    INKCELL_TEST_FAIL_IF(frame.list.x != frame.content.x || frame.list.w != frame.content.w,
                         "one pane is the whole content");
    INKCELL_TEST_FAIL_IF(!inkcell_box_is_empty(frame.detail), "no detail pane without a split");
    const struct inkcell_fb_layout none = inkcell_fb_scaffold_detail(state, &frame, NULL);
    INKCELL_TEST_FAIL_IF(none.line != 0, "asking for a detail that is not there gets nothing");
    inkcell_fb_scaffold_end(state, &frame, NULL);

    inkcell_capture_close(capture);
    record_success(test_name);
}

INKCELL_TEST_CASE(scaffold_without_destinations_is_all_content, unit) {
    struct inkcell_draw_state *state = NULL;
    struct inkcell_capture *capture =
        scaffold_open(INKCELL_CAPTURE_WIDTH, INKCELL_CAPTURE_HEIGHT, INKCELL_SCALE(3), &state);
    INKCELL_TEST_FAIL_IF(capture == NULL, "the capture should open");

    const struct inkcell_fb_scaffold scaffold = {.footer = true};
    struct inkcell_fb_scaffold_frame frame;
    inkcell_fb_scaffold_begin(state, &scaffold, &frame);
    INKCELL_TEST_FAIL_IF(frame.nav != INKCELL_FB_NAV_NONE, "nothing to navigate to, no chrome");
    INKCELL_TEST_FAIL_IF(frame.content.x != 0 || frame.content.w != (int)INKCELL_CAPTURE_WIDTH ||
                             frame.content.h != (int)INKCELL_CAPTURE_HEIGHT,
                         "the content is the whole surface");
    inkcell_fb_scaffold_end(state, &frame, NULL);

    inkcell_capture_close(capture);
    record_success(test_name);
}

/* Whether any pixel in the span [x0, x1) of row `y` differs between two copies of a frame. */
static bool span_changed(const uint8_t *before, const uint8_t *after, size_t stride, int y, int x0,
                         int x1, size_t bpp) {
    const size_t start = (size_t)y * stride + (size_t)x0 * bpp;
    return memcmp(before + start, after + start, (size_t)(x1 - x0) * bpp) != 0;
}

INKCELL_TEST_CASE(scaffold_detail_pane_title_badge_sits_on_the_pane_edge, unit) {
    struct inkcell_draw_state *state = NULL;
    struct inkcell_capture *capture =
        scaffold_open(SCAFFOLD_WIDE_W, SCAFFOLD_WIDE_H, INKCELL_SCALE(3), &state);
    INKCELL_TEST_FAIL_IF(capture == NULL, "the capture should open");

    const struct inkcell_fb_scaffold scaffold = {
        .destinations = k_destinations, .count = SCAFFOLD_COUNT, .footer = true, .split = true};
    /* The ground first, so that what the bar paints behind itself is not itself a change. */
    inkcell_fb_clear(state, inkcell_fb_color(state, INKCELL_COLOR_BG));
    struct inkcell_fb_scaffold_frame frame;
    inkcell_fb_scaffold_begin(state, &scaffold, &frame);
    struct inkcell_fb_layout detail = inkcell_fb_scaffold_detail(state, &frame, NULL);
    INKCELL_TEST_FAIL_IF(detail.line == 0, "the wide page splits");

    /* The column's trailing edge, measured while the region is the detail pane. */
    const int right = inkcell_fb_content_x(state) + inkcell_fb_content_w(state);
    const int probe = inkcell_fb_char_adv(state, state->scale);

    uint32_t w = 0U, h = 0U;
    size_t stride = 0U;
    const uint8_t *pixels = inkcell_capture_pixels(capture, &w, &h, &stride);
    INKCELL_TEST_FAIL_IF(pixels == NULL, "the frame should have pixels");
    const size_t bpp = state->surface.bytes_per_pixel;
    const size_t size = stride * h;
    uint8_t *before = malloc(size);
    INKCELL_TEST_FAIL_IF(before == NULL, "the copy should allocate");
    memcpy(before, pixels, size);

    /* The collapsed bar with a badge. The badge is drawn against the column's trailing edge, so
       the span just inside that edge has to change somewhere in the bar's first line - a badge
       placed from the region's width minus the column's *start* would land a pane-width away. */
    const struct inkcell_fb_large_title bar = {.title = "Display", .badge = "9"};
    const int top = detail.body_y;
    inkcell_fb_draw_large_title(state, &detail, &bar, inkcell_fb_large_title_travel(state));
    bool changed = false;
    /* Short of the hairline the collapsed bar draws across its foot, which spans the pane and so
       changes this span wherever the badge went. */
    const int stop = detail.body_y - 2 * inkcell_fb_rule_height(state, 1);
    for (int y = top; y < stop && !changed; ++y) {
        changed = span_changed(before, pixels, stride, y, right - probe, right, bpp);
    }
    free(before);
    INKCELL_TEST_FAIL_IF(!changed, "the badge should sit against the detail column's edge");

    inkcell_fb_scaffold_end(state, &frame, NULL);
    inkcell_capture_close(capture);
    record_success(test_name);
}
