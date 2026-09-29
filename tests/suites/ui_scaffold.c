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
#include "inkcell/ui/widgets/meter.h"
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
    /* Medium, but the detail would be narrower than a measure beside the list: one pane. */
    INKCELL_TEST_FAIL_IF(frame.split,
                         "a medium frame too narrow for a measured detail is one pane");
    INKCELL_TEST_FAIL_IF(inkcell_fb_scaffold_splittable(state, &scaffold),
                         "and has no room for one either");
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

/* A Mac window's buttons sit in the rail's top-leading corner (top_leading_inset). An expanded
   rail widens to hold them, as a sidebar does. A collapsed one keeps its own width - held to the
   buttons it was a column half again as wide as its icons, with the icons against one side - and
   the pane's heading steps clear of the buttons instead. */
INKCELL_TEST_CASE(scaffold_rail_holds_the_window_buttons_only_expanded, unit) {
    struct inkcell_draw_state *state = NULL;
    struct inkcell_capture *capture =
        scaffold_open(INKCELL_CAPTURE_WIDTH, INKCELL_CAPTURE_HEIGHT, INKCELL_SCALE(3), &state);
    INKCELL_TEST_FAIL_IF(capture == NULL, "the capture should open");

    const int bare = inkcell_fb_scaffold_rail_width(state, k_destinations, SCAFFOLD_COUNT);
    const int words =
        inkcell_fb_scaffold_rail_expanded_width(state, k_destinations, SCAFFOLD_COUNT);
    INKCELL_TEST_FAIL_IF_CLEANUP(words <= bare, inkcell_capture_close(capture),
                                 "the expanded rail should be the wider one");

    state->top_leading_inset = bare + 40;
    INKCELL_TEST_FAIL_IF_CLEANUP(
        inkcell_fb_scaffold_rail_width(state, k_destinations, SCAFFOLD_COUNT) != bare,
        inkcell_capture_close(capture), "a collapsed rail keeps its own width under the buttons");

    struct inkcell_focus_item storage[16];
    struct inkcell_focus_map map;
    inkcell_focus_begin(&map, storage, 16U);
    inkcell_fb_set_focus_map(state, &map);
    state->pointer = true;
    const struct inkcell_fb_scaffold scaffold = {.destinations = k_destinations,
                                                 .count = SCAFFOLD_COUNT};
    struct inkcell_fb_scaffold_frame frame;
    inkcell_fb_scaffold_begin(state, &scaffold, &frame);
    INKCELL_TEST_FAIL_IF_CLEANUP(frame.nav != INKCELL_FB_NAV_RAIL || frame.rail_expanded,
                                 inkcell_capture_close(capture), "medium gets a collapsed rail");
    INKCELL_TEST_FAIL_IF_CLEANUP(frame.content.x != bare, inkcell_capture_close(capture),
                                 "the content starts where the collapsed rail stops");
    /* A screen with no heading - a column of cards - starts below the buttons' band. */
    INKCELL_TEST_FAIL_IF_CLEANUP(
        frame.layout.body_y <
            inkcell_fb_top_band(state, inkcell_fb_type_scale(state, INKCELL_TYPE_LABEL)),
        inkcell_capture_close(capture), "the body should start below the window's buttons");
    /* Buttons reaching past the content column's own margin, which is what the heading has to
       move for. The heading's back arrow is the one mark on it with a box: it hangs a gutter
       ahead of where the title's column starts. */
    state->top_leading_inset = inkcell_fb_content_x(state) + 40;
    frame.layout.back = true;
    const struct inkcell_fb_app_bar heading = {.title = "Messages"};
    (void)inkcell_fb_draw_app_bar(state, &frame.layout, &heading);
    inkcell_fb_scaffold_end(state, &frame, NULL);
    inkcell_fb_set_focus_map(state, NULL);
    struct inkcell_focus_rect arrow;
    INKCELL_TEST_FAIL_IF_CLEANUP(
        !inkcell_focus_rect_of(&map, INKCELL_FOCUS_KEY(INKCELL_KEY_B), &arrow),
        inkcell_capture_close(capture), "the heading's back arrow should be registered");
    INKCELL_TEST_FAIL_IF_CLEANUP(arrow.x + inkcell_fb_gutter(state) < state->top_leading_inset,
                                 inkcell_capture_close(capture),
                                 "the heading should start clear of the window's buttons");

    state->top_leading_inset = words + 40;
    INKCELL_TEST_FAIL_IF_CLEANUP(inkcell_fb_scaffold_rail_expanded_width(
                                     state, k_destinations, SCAFFOLD_COUNT) != words + 40,
                                 inkcell_capture_close(capture),
                                 "an expanded rail widens to hold the window's buttons");
    state->top_leading_inset = 1;
    INKCELL_TEST_FAIL_IF_CLEANUP(
        inkcell_fb_scaffold_rail_expanded_width(state, k_destinations, SCAFFOLD_COUNT) != words,
        inkcell_capture_close(capture), "and an inset it already clears changes nothing");

    inkcell_capture_close(capture);
    record_success(test_name);
}

/* The busy bar hangs off the top of the content, which beside a collapsed rail is the top of
   the band the window's buttons stand in: it starts past them rather than running under them. */
static bool scaffold_is_track(const struct inkcell_draw_state *state,
                              struct inkcell_capture *capture, int x, int y) {
    uint32_t width = 0U;
    uint32_t height = 0U;
    size_t stride = 0U;
    const uint8_t *pixels = inkcell_capture_pixels(capture, &width, &height, &stride);
    if (pixels == NULL || x < 0 || y < 0 || x >= (int)width || y >= (int)height) {
        return false;
    }
    const uint8_t *p = pixels + (size_t)y * stride + (size_t)x * 4U;
    const struct inkcell_rgb track = inkcell_fb_color(state, INKCELL_COLOR_METER_TRACK);
    return p[2] == track.r && p[1] == track.g && p[0] == track.b;
}

INKCELL_TEST_CASE(scaffold_busy_bar_clears_the_window_buttons, unit) {
    struct inkcell_draw_state *state = NULL;
    struct inkcell_capture *capture =
        scaffold_open(INKCELL_CAPTURE_WIDTH, INKCELL_CAPTURE_HEIGHT, INKCELL_SCALE(3), &state);
    INKCELL_TEST_FAIL_IF(capture == NULL, "the capture should open");

    const int bare = inkcell_fb_scaffold_rail_width(state, k_destinations, SCAFFOLD_COUNT);
    state->top_leading_inset = bare + 60;
    inkcell_fb_clear(state, inkcell_fb_color(state, INKCELL_COLOR_BG));
    const struct inkcell_fb_scaffold scaffold = {
        .destinations = k_destinations, .count = SCAFFOLD_COUNT, .busy = true};
    struct inkcell_fb_scaffold_frame frame;
    inkcell_fb_scaffold_begin(state, &scaffold, &frame);
    inkcell_fb_scaffold_end(state, &frame, NULL);
    INKCELL_TEST_FAIL_IF_CLEANUP(frame.rail_expanded || frame.content.x != bare,
                                 inkcell_capture_close(capture), "medium gets a collapsed rail");

    /* Across the middle of the bar: the moving fill covers some of the track, never all of it. */
    const int y = frame.layout.nav_y + inkcell_fb_meter_thickness(state, state->scale) / 2;
    bool under = false;
    bool past = false;
    for (int x = bare; x < (int)INKCELL_CAPTURE_WIDTH; ++x) {
        const bool track = scaffold_is_track(state, capture, x, y);
        under = under || (track && x < state->top_leading_inset);
        past = past || (track && x >= state->top_leading_inset);
    }
    INKCELL_TEST_FAIL_IF_CLEANUP(under, inkcell_capture_close(capture),
                                 "the busy bar should not run under the window's buttons");
    INKCELL_TEST_FAIL_IF_CLEANUP(!past, inkcell_capture_close(capture),
                                 "and should still be drawn past them");

    inkcell_capture_close(capture);
    record_success(test_name);
}

/* AUTO is the class's answer: the rail folds to its icons where the body wants the columns and
   unfolds into words where the window has room to spare. Either can be asked for outright. */
INKCELL_TEST_CASE(scaffold_rail_expands_on_an_expanded_frame_unless_told, unit) {
    struct inkcell_draw_state *state = NULL;
    struct inkcell_capture *capture =
        scaffold_open(INKCELL_CAPTURE_WIDTH, INKCELL_CAPTURE_HEIGHT, INKCELL_SCALE(3), &state);
    INKCELL_TEST_FAIL_IF(capture == NULL, "the capture should open");
    const int collapsed = inkcell_fb_scaffold_rail_width(state, k_destinations, SCAFFOLD_COUNT);
    const int expanded =
        inkcell_fb_scaffold_rail_expanded_width(state, k_destinations, SCAFFOLD_COUNT);
    INKCELL_TEST_FAIL_IF_CLEANUP(expanded <= collapsed, inkcell_capture_close(capture),
                                 "the expanded rail is wider than its icons");

    struct inkcell_fb_scaffold scaffold = {.destinations = k_destinations, .count = SCAFFOLD_COUNT};
    struct inkcell_fb_scaffold_frame frame;
    inkcell_fb_scaffold_begin(state, &scaffold, &frame);
    inkcell_fb_scaffold_end(state, &frame, NULL);
    INKCELL_TEST_FAIL_IF_CLEANUP(frame.rail_expanded || frame.content.x != collapsed,
                                 inkcell_capture_close(capture),
                                 "a medium frame's rail is collapsed by default");

    scaffold.rail = INKCELL_FB_RAIL_EXPANDED;
    inkcell_fb_scaffold_begin(state, &scaffold, &frame);
    inkcell_fb_scaffold_end(state, &frame, NULL);
    INKCELL_TEST_FAIL_IF_CLEANUP(!frame.rail_expanded || frame.content.x != expanded,
                                 inkcell_capture_close(capture),
                                 "a medium frame expands its rail when asked");
    inkcell_capture_close(capture);

    capture = scaffold_open(SCAFFOLD_WIDE_W, SCAFFOLD_WIDE_H, INKCELL_SCALE(3), &state);
    INKCELL_TEST_FAIL_IF(capture == NULL, "the wide capture should open");
    scaffold.rail = INKCELL_FB_RAIL_AUTO;
    inkcell_fb_scaffold_begin(state, &scaffold, &frame);
    inkcell_fb_scaffold_end(state, &frame, NULL);
    INKCELL_TEST_FAIL_IF_CLEANUP(!frame.rail_expanded, inkcell_capture_close(capture),
                                 "an expanded frame's rail is expanded by default");
    scaffold.rail = INKCELL_FB_RAIL_COLLAPSED;
    inkcell_fb_scaffold_begin(state, &scaffold, &frame);
    inkcell_fb_scaffold_end(state, &frame, NULL);
    INKCELL_TEST_FAIL_IF_CLEANUP(frame.rail_expanded ||
                                     frame.content.x != inkcell_fb_scaffold_rail_width(
                                                            state, k_destinations, SCAFFOLD_COUNT),
                                 inkcell_capture_close(capture), "and collapses when asked");

    inkcell_capture_close(capture);
    record_success(test_name);
}

/* A label too long for the rail's cap cannot be laid out as a row, so the rail keeps its icons
   rather than taking the body's columns to spell it. */
INKCELL_TEST_CASE(scaffold_rail_stays_collapsed_when_its_words_do_not_fit, unit) {
    struct inkcell_draw_state *state = NULL;
    struct inkcell_capture *capture =
        scaffold_open(SCAFFOLD_WIDE_W, SCAFFOLD_WIDE_H, INKCELL_SCALE(3), &state);
    INKCELL_TEST_FAIL_IF(capture == NULL, "the capture should open");
    const struct inkcell_fb_chip wordy[] = {
        {.icon = INKCELL_ICON_MESSAGES,
         .label = "An improbably long destination name that no rail could hold",
         .focus_id = 11U},
    };
    INKCELL_TEST_FAIL_IF_CLEANUP(inkcell_fb_scaffold_rail_expanded_width(state, wordy, 1U) != 0,
                                 inkcell_capture_close(capture),
                                 "words past the cap have no expanded width");
    const struct inkcell_fb_scaffold scaffold = {
        .destinations = wordy, .count = 1U, .rail = INKCELL_FB_RAIL_EXPANDED};
    struct inkcell_fb_scaffold_frame frame;
    inkcell_fb_scaffold_begin(state, &scaffold, &frame);
    inkcell_fb_scaffold_end(state, &frame, NULL);
    INKCELL_TEST_FAIL_IF_CLEANUP(
        frame.rail_expanded || frame.content.x != inkcell_fb_scaffold_rail_width(state, wordy, 1U),
        inkcell_capture_close(capture), "the rail is drawn collapsed however it was asked");
    inkcell_capture_close(capture);
    record_success(test_name);
}

/*
 * The toggle is registered under the offset that says what a press will do, so the application
 * answers a click from the id alone. With no id there is no toggle, and the destinations start
 * where they always did.
 */
INKCELL_TEST_CASE(scaffold_rail_toggle_says_what_a_press_does, unit) {
    struct inkcell_draw_state *state = NULL;
    struct inkcell_capture *capture =
        scaffold_open(INKCELL_CAPTURE_WIDTH, INKCELL_CAPTURE_HEIGHT, INKCELL_SCALE(3), &state);
    INKCELL_TEST_FAIL_IF(capture == NULL, "the capture should open");
    struct inkcell_focus_item storage[16];
    struct inkcell_focus_map map;
    const uint32_t base = 40U;
    const uint32_t expand = base + (uint32_t)INKCELL_FB_RAIL_TOGGLE_EXPAND;
    const uint32_t collapse = base + (uint32_t)INKCELL_FB_RAIL_TOGGLE_COLLAPSE;
    struct inkcell_focus_rect rect;
    struct inkcell_focus_rect first;

    inkcell_focus_begin(&map, storage, 16U);
    inkcell_fb_set_focus_map(state, &map);
    struct inkcell_fb_scaffold scaffold = {.destinations = k_destinations, .count = SCAFFOLD_COUNT};
    struct inkcell_fb_scaffold_frame frame;
    inkcell_fb_scaffold_begin(state, &scaffold, &frame);
    inkcell_fb_scaffold_end(state, &frame, NULL);
    INKCELL_TEST_FAIL_IF_CLEANUP(inkcell_focus_rect_of(&map, expand, &rect) ||
                                     inkcell_focus_rect_of(&map, collapse, &rect),
                                 inkcell_capture_close(capture), "no id, no toggle");
    INKCELL_TEST_FAIL_IF_CLEANUP(!inkcell_focus_rect_of(&map, k_destinations[0].focus_id, &first),
                                 inkcell_capture_close(capture),
                                 "the first destination is registered");

    inkcell_focus_begin(&map, storage, 16U);
    scaffold.rail_toggle_id = base;
    inkcell_fb_scaffold_begin(state, &scaffold, &frame);
    inkcell_fb_scaffold_end(state, &frame, NULL);
    INKCELL_TEST_FAIL_IF_CLEANUP(
        !inkcell_focus_rect_of(&map, expand, &rect) || inkcell_focus_rect_of(&map, collapse, &rect),
        inkcell_capture_close(capture), "a collapsed rail's toggle expands it");
    struct inkcell_focus_rect below;
    INKCELL_TEST_FAIL_IF_CLEANUP(
        !inkcell_focus_rect_of(&map, k_destinations[0].focus_id, &below) || below.y <= first.y,
        inkcell_capture_close(capture), "the destinations move down to make room for the toggle");
    INKCELL_TEST_FAIL_IF_CLEANUP(rect.x + rect.w > frame.nav_box.w, inkcell_capture_close(capture),
                                 "the toggle is inside the rail");

    inkcell_focus_begin(&map, storage, 16U);
    scaffold.rail = INKCELL_FB_RAIL_EXPANDED;
    inkcell_fb_scaffold_begin(state, &scaffold, &frame);
    inkcell_fb_scaffold_end(state, &frame, NULL);
    INKCELL_TEST_FAIL_IF_CLEANUP(
        inkcell_focus_rect_of(&map, expand, &rect) || !inkcell_focus_rect_of(&map, collapse, &rect),
        inkcell_capture_close(capture), "an expanded rail's toggle collapses it");
    /* An expanded row is the whole row, so a click on the word reaches it. */
    INKCELL_TEST_FAIL_IF_CLEANUP(
        !inkcell_focus_rect_of(&map, k_destinations[0].focus_id, &rect) ||
            rect.w <= inkcell_fb_scaffold_rail_width(state, k_destinations, SCAFFOLD_COUNT),
        inkcell_capture_close(capture), "an expanded row is wider than a collapsed rail");

    inkcell_fb_set_focus_map(state, NULL);
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

/*
 * A medium window wide enough to give the detail a whole measure splits too.
 *
 * The line two panes cross is the detail's, not the list's: the detail is where running text is,
 * so it is held to a measure, and the list beside it is a column of short rows read by their
 * leading edge, which reads the same at two fifths of the width. 1920x1080 at the handheld's
 * scale is the case this is for - medium, and before this one pane in a centred ribbon with most
 * of the window empty either side.
 */
INKCELL_TEST_CASE(scaffold_medium_splits_when_the_detail_gets_a_measure, unit) {
    struct inkcell_draw_state *state = NULL;
    struct inkcell_capture *capture = scaffold_open(1920U, 1080U, INKCELL_SCALE(4), &state);
    INKCELL_TEST_FAIL_IF(capture == NULL, "the capture should open");
    INKCELL_TEST_FAIL_IF(inkcell_fb_width_class(state) != INKCELL_WIDTH_MEDIUM,
                         "a 1920 window at the handheld's scale is medium");

    const struct inkcell_fb_scaffold scaffold = {
        .destinations = k_destinations, .count = SCAFFOLD_COUNT, .footer = true, .split = true};
    struct inkcell_fb_scaffold_frame frame;
    inkcell_fb_scaffold_begin(state, &scaffold, &frame);
    INKCELL_TEST_FAIL_IF(frame.nav != INKCELL_FB_NAV_RAIL, "medium keeps the rail");
    INKCELL_TEST_FAIL_IF(!frame.split, "a medium frame with room for a measured detail splits");
    INKCELL_TEST_FAIL_IF(frame.detail.w <= frame.list.w, "the detail gets the larger share");

    const struct inkcell_fb_layout detail = inkcell_fb_scaffold_detail(state, &frame, NULL);
    INKCELL_TEST_FAIL_IF(detail.line == 0, "the detail pane should be there to draw into");
    INKCELL_TEST_FAIL_IF(inkcell_fb_cols(state, state->scale) < INKCELL_WIDTH_MEASURE_COLS,
                         "the detail pane should hold a whole measure");
    inkcell_fb_scaffold_end(state, &frame, NULL);

    /* The same window for a screen with no detail: one pane, but the room was there. */
    const struct inkcell_fb_scaffold single = {
        .destinations = k_destinations, .count = SCAFFOLD_COUNT, .footer = true};
    inkcell_fb_scaffold_begin(state, &single, &frame);
    INKCELL_TEST_FAIL_IF(frame.split, "a screen that did not ask is one pane");
    inkcell_fb_scaffold_end(state, &frame, NULL);
    INKCELL_TEST_FAIL_IF(!inkcell_fb_scaffold_splittable(state, &single),
                         "but a split would have fit");

    inkcell_capture_close(capture);
    record_success(test_name);
}

/*
 * A split frame's action bar runs under both panes: its first keycap leads at the list's column
 * and its status trails at the detail's edge. Held to the measure it was a centred ribbon under
 * two panes that together already ran edge to edge, and lined up with neither of them.
 */
INKCELL_TEST_CASE(scaffold_split_action_bar_spans_both_panes, unit) {
    struct inkcell_draw_state *state = NULL;
    struct inkcell_capture *capture = scaffold_open(1920U, 1080U, INKCELL_SCALE(4), &state);
    INKCELL_TEST_FAIL_IF(capture == NULL, "the capture should open");

    struct inkcell_focus_item storage[16];
    struct inkcell_focus_map map;
    const struct inkcell_button_action items[] = {
        {.button = INKCELL_BUTTON_A, .label = INKCELL_STR_NONE},
    };
    const struct inkcell_fb_action_bar bar = {.items = items, .count = 1U, .status = "linked"};
    int lead[2] = {0, 0};
    for (int split = 0; split < 2; ++split) {
        inkcell_focus_begin(&map, storage, 16U);
        inkcell_fb_set_focus_map(state, &map);
        const struct inkcell_fb_scaffold scaffold = {.destinations = k_destinations,
                                                     .count = SCAFFOLD_COUNT,
                                                     .footer = true,
                                                     .split = split != 0};
        struct inkcell_fb_scaffold_frame frame;
        inkcell_fb_scaffold_begin(state, &scaffold, &frame);
        INKCELL_TEST_FAIL_IF(frame.split != (split != 0), "1920x1080 splits when it is asked to");
        const int list_x = frame.layout.body_x;
        const int content_x = frame.content.x;
        inkcell_fb_scaffold_end(state, &frame, &bar);
        inkcell_fb_set_focus_map(state, NULL);

        struct inkcell_focus_rect rect;
        INKCELL_TEST_FAIL_IF(
            !inkcell_focus_rect_of(&map, INKCELL_FOCUS_ACTION_KEY(INKCELL_KEY_A), &rect),
            "the A hint should be drawn");
        lead[split] = rect.x;
        if (split != 0) {
            /* The keycap's box starts at its cap's own padding, just ahead of the column. */
            INKCELL_TEST_FAIL_IF(rect.x < content_x || rect.x > list_x,
                                 "under a split the first keycap leads at the list's column");
        }
    }
    INKCELL_TEST_FAIL_IF(lead[0] <= lead[1],
                         "one pane keeps its bar under the measured column it is about");
    INKCELL_TEST_FAIL_IF(!inkcell_fb_set_measured(state, true),
                         "end puts the measure back as it found it");

    inkcell_capture_close(capture);
    record_success(test_name);
}

/*
 * A placeholder stands in the middle of its pane; an empty list starts at its head. Read back off
 * the pixels: the first row with anything but ground on it.
 */
static int first_inked_row(const struct inkcell_capture *capture, struct inkcell_box box) {
    uint32_t w = 0U;
    uint32_t h = 0U;
    size_t stride = 0U;
    const uint8_t *px = inkcell_capture_pixels(capture, &w, &h, &stride);
    if (px == NULL) {
        return -1;
    }
    /* The pane's top corner is ground: the pane starts under the frame's own chrome. */
    const uint8_t *ground = px + (size_t)box.y * stride + (size_t)box.x * 4U;
    for (int y = box.y; y < box.y + box.h && y < (int)h; ++y) {
        for (int x = box.x; x < box.x + box.w && x < (int)w; ++x) {
            if (memcmp(px + (size_t)y * stride + (size_t)x * 4U, ground, 3U) != 0) {
                return y;
            }
        }
    }
    return -1;
}

INKCELL_TEST_CASE(scaffold_detail_placeholder_is_centred_in_its_pane, unit) {
    struct inkcell_draw_state *state = NULL;
    struct inkcell_capture *capture = scaffold_open(1920U, 1080U, INKCELL_SCALE(4), &state);
    INKCELL_TEST_FAIL_IF(capture == NULL, "the capture should open");

    const struct inkcell_fb_scaffold scaffold = {
        .destinations = k_destinations, .count = SCAFFOLD_COUNT, .footer = true, .split = true};
    int top[2] = {-1, -1};
    struct inkcell_box pane = {0};
    struct inkcell_fb_layout detail = {0};
    for (int centred = 0; centred < 2; ++centred) {
        inkcell_fb_fill_rect(state, 0, 0, 1920, 1080, inkcell_fb_color(state, INKCELL_COLOR_BG));
        struct inkcell_fb_scaffold_frame frame;
        inkcell_fb_scaffold_begin(state, &scaffold, &frame);
        INKCELL_TEST_FAIL_IF(!frame.split, "1920x1080 splits");
        detail = inkcell_fb_scaffold_detail(state, &frame, NULL);
        pane = frame.detail;
        if (centred != 0) {
            inkcell_fb_draw_placeholder(state, &detail, INKCELL_ICON_NODES, "Nothing open");
        } else {
            inkcell_fb_draw_empty(state, &detail, INKCELL_ICON_NODES, "Nothing open");
        }
        inkcell_fb_scaffold_end(state, &frame, NULL);
        top[centred] = first_inked_row(capture, pane);
    }
    INKCELL_TEST_FAIL_IF(top[0] < detail.body_y || top[0] >= detail.body_y + detail.line,
                         "an empty list starts where its rows do");
    const int band = (int)detail.rows * detail.line;
    INKCELL_TEST_FAIL_IF(top[1] < detail.body_y + band / 4,
                         "a placeholder stands clear of the pane's head");
    INKCELL_TEST_FAIL_IF(top[1] > detail.body_y + band / 2,
                         "a placeholder starts above the middle of its pane");

    inkcell_capture_close(capture);
    record_success(test_name);
}

/*
 * The question asked before a frame gets the answer the frame gives: at every size, a scaffold
 * that asks for a split gets one exactly where inkcell_fb_scaffold_splittable() said it would, and
 * asking leaves the region and the measure as they were.
 */
INKCELL_TEST_CASE(scaffold_splittable_answers_before_the_frame, unit) {
    static const struct {
        uint32_t w;
        uint32_t h;
        int scale;
    } sizes[] = {
        {INKCELL_CAPTURE_WIDTH, INKCELL_CAPTURE_HEIGHT, INKCELL_SCALE(4)},
        {INKCELL_CAPTURE_WIDTH, INKCELL_CAPTURE_HEIGHT, INKCELL_SCALE(3)},
        {1600U, 900U, INKCELL_SCALE(4)},
        {1920U, 1080U, INKCELL_SCALE(4)},
        {1920U, 560U, INKCELL_SCALE(4)},
        {SCAFFOLD_WIDE_W, SCAFFOLD_WIDE_H, INKCELL_SCALE(3)},
    };
    bool saw[2] = {false, false};
    for (size_t i = 0U; i < sizeof sizes / sizeof sizes[0]; ++i) {
        for (int rail = INKCELL_FB_RAIL_AUTO; rail <= INKCELL_FB_RAIL_EXPANDED; ++rail) {
            struct inkcell_draw_state *state = NULL;
            struct inkcell_capture *capture =
                scaffold_open(sizes[i].w, sizes[i].h, sizes[i].scale, &state);
            INKCELL_TEST_FAIL_IF(capture == NULL, "the capture should open");
            const struct inkcell_fb_scaffold scaffold = {.destinations = k_destinations,
                                                         .count = SCAFFOLD_COUNT,
                                                         .footer = true,
                                                         .split = true,
                                                         .rail = (enum inkcell_fb_rail)rail};
            const struct inkcell_box region = state->region;
            const bool predicted = inkcell_fb_scaffold_splittable(state, &scaffold);
            INKCELL_TEST_FAIL_IF(state->region.x != region.x || state->region.w != region.w ||
                                     state->unmeasured,
                                 "asking should leave the region and the measure alone");
            struct inkcell_fb_scaffold_frame frame;
            inkcell_fb_scaffold_begin(state, &scaffold, &frame);
            inkcell_fb_scaffold_end(state, &frame, NULL);
            INKCELL_TEST_FAIL_IF(frame.split != predicted,
                                 "the frame should split exactly where it was said it would");
            saw[predicted ? 1 : 0] = true;
            inkcell_capture_close(capture);
        }
    }
    INKCELL_TEST_FAIL_IF(!saw[0] || !saw[1], "the sizes should cover both answers");
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

/*
 * A scaffold that names a compact bar keeps one row at the foot rather than two, gives the other
 * back to the body, and hands the bar a layout that says which shape to draw - in every
 * arrangement, the split's detail pane included, since both panes stand on the same foot.
 */
INKCELL_TEST_CASE(scaffold_compact_footer_keeps_one_row, unit) {
    struct inkcell_draw_state *state = NULL;
    struct inkcell_capture *capture =
        scaffold_open(INKCELL_CAPTURE_WIDTH, INKCELL_CAPTURE_HEIGHT, INKCELL_SCALE(4), &state);
    INKCELL_TEST_FAIL_IF(capture == NULL, "the capture should open");

    struct inkcell_fb_scaffold scaffold = {
        .destinations = k_destinations,
        .count = SCAFFOLD_COUNT,
        .compact_nav = INKCELL_FB_COMPACT_NAV_TOP,
        .footer = true,
    };
    struct inkcell_fb_scaffold_frame full;
    inkcell_fb_scaffold_begin(state, &scaffold, &full);
    inkcell_fb_scaffold_end(state, &full, NULL);

    scaffold.footer_kind = INKCELL_FB_FOOTER_COMPACT;
    struct inkcell_fb_scaffold_frame compact;
    inkcell_fb_scaffold_begin(state, &scaffold, &compact);
    inkcell_fb_scaffold_end(state, &compact, NULL);

    INKCELL_TEST_FAIL_IF_CLEANUP(full.layout.footer != INKCELL_FB_FOOTER_FULL,
                                 inkcell_capture_close(capture),
                                 "`footer` alone should still mean the full bar");
    INKCELL_TEST_FAIL_IF_CLEANUP(compact.layout.footer != INKCELL_FB_FOOTER_COMPACT,
                                 inkcell_capture_close(capture),
                                 "the layout should carry the bar it was measured for");
    INKCELL_TEST_FAIL_IF_CLEANUP(compact.layout.footer_y <= full.layout.footer_y,
                                 inkcell_capture_close(capture),
                                 "a compact bar should give rows back to the body");
    INKCELL_TEST_FAIL_IF_CLEANUP(inkcell_fb_panel_height(state) - compact.layout.footer_y !=
                                     inkcell_fb_action_bar_height(state, &compact.layout),
                                 inkcell_capture_close(capture),
                                 "the foot should be exactly one compact bar tall");
    inkcell_capture_close(capture);
    record_success(test_name);
}

INKCELL_TEST_CASE(scaffold_unmeasured_content_runs_to_the_region_and_resets_next_frame, unit) {
    struct inkcell_draw_state *state = NULL;
    struct inkcell_capture *capture =
        scaffold_open(SCAFFOLD_WIDE_W, SCAFFOLD_WIDE_H, INKCELL_SCALE(3), &state);
    INKCELL_TEST_FAIL_IF(capture == NULL, "the capture should open");

    const struct inkcell_fb_scaffold scaffold = {
        .destinations = k_destinations, .count = SCAFFOLD_COUNT, .footer = true};
    struct inkcell_fb_scaffold_frame frame;
    inkcell_fb_scaffold_begin(state, &scaffold, &frame);
    INKCELL_TEST_FAIL_IF(frame.width == INKCELL_WIDTH_COMPACT, "the wide page is not compact");
    const struct inkcell_box measured = inkcell_fb_content_column(state);
    const struct inkcell_box region = inkcell_fb_region(state);
    const int margin = inkcell_fb_margin(state);
    INKCELL_TEST_FAIL_IF(measured.w >= region.w - 2 * margin,
                         "a wide frame's column is held to the measure");

    INKCELL_TEST_FAIL_IF(!inkcell_fb_set_measured(state, false), "a frame starts measured");
    const struct inkcell_box full = inkcell_fb_content_column(state);
    INKCELL_TEST_FAIL_IF(full.x != region.x + margin || full.w != region.w - 2 * margin,
                         "an unmeasured column is the region less its margins");
    inkcell_fb_scaffold_end(state, &frame, NULL);

    /* Left off on purpose: the next frame is measured again all the same. */
    inkcell_fb_scaffold_begin(state, &scaffold, &frame);
    const struct inkcell_box again = inkcell_fb_content_column(state);
    INKCELL_TEST_FAIL_IF(again.x != measured.x || again.w != measured.w,
                         "the scaffold puts the measure back at the top of a frame");
    inkcell_fb_scaffold_end(state, &frame, NULL);

    inkcell_capture_close(capture);
    record_success(test_name);
}

INKCELL_TEST_CASE(scaffold_unmeasured_frame_lays_out_its_heading_without_the_measure, unit) {
    struct inkcell_draw_state *state = NULL;
    struct inkcell_capture *capture =
        scaffold_open(SCAFFOLD_WIDE_W, SCAFFOLD_WIDE_H, INKCELL_SCALE(3), &state);
    INKCELL_TEST_FAIL_IF(capture == NULL, "the capture should open");

    const struct inkcell_fb_app_bar bar = {.title = "Map"};
    struct inkcell_fb_scaffold scaffold = {
        .destinations = k_destinations, .count = SCAFFOLD_COUNT, .footer = true, .app_bar = &bar};
    struct inkcell_fb_scaffold_frame frame;
    inkcell_fb_scaffold_begin(state, &scaffold, &frame);
    const int measured_w = frame.layout.body_w;
    inkcell_fb_scaffold_end(state, &frame, NULL);

    /* Asked for through the scaffold, it reaches the heading and the layout the scaffold made -
       which a call after begin could not. */
    scaffold.unmeasured = true;
    inkcell_fb_scaffold_begin(state, &scaffold, &frame);
    const struct inkcell_box region = inkcell_fb_region(state);
    const int full_w = region.w - 2 * inkcell_fb_margin(state);
    INKCELL_TEST_FAIL_IF(measured_w >= full_w, "a wide frame's layout is held to the measure");
    INKCELL_TEST_FAIL_IF(frame.layout.body_w != full_w,
                         "an unmeasured frame's layout runs to the region less its margins");
    inkcell_fb_scaffold_end(state, &frame, NULL);

    inkcell_capture_close(capture);
    record_success(test_name);
}
