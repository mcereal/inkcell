#define _POSIX_C_SOURCE 200809L

/*
 * The app bar's verbs and modes, and the compact action bar: what fits, what goes behind the
 * overflow button, and the heights none of it is allowed to change.
 *
 * Two kinds of claim, and neither is a picture. The first is about *which*: a bar reports the
 * actions it had room for in its fit, the overflow menu is built from that same fit, and the
 * focus map holds exactly the boxes that were drawn - so a verb is always in one place or the
 * other and never in both or neither. The second is about *how tall*: a mode that replaces the
 * heading, a large title at any offset and a compact footer each say in advance how much of the
 * body they will take, and a screen that laid its list out against that answer has to get the
 * body it measured. The golden sheet shows what these look like; this is what holds them to it.
 */

#include "framework/inkcell_test.h"

#include "inkcell/ui/fb_capture.h"
#include "inkcell/ui/focus.h"
#include "inkcell/ui/widgets/chrome.h"
#include "inkcell/ui/widgets/scroll.h"

#include <string.h>

#define APP_BAR_STORAGE 96U

/* A panel too narrow for a title and six icons, which is how a case reaches the overflow
   without inventing a long word for it - the widget suites' convention. */
#define APP_BAR_NARROW 360U

enum {
    AB_ID_OVERFLOW = 100,
    AB_ID_FIRST, /* actions take AB_ID_FIRST + i */
};

struct app_bar_harness {
    struct inkcell_capture *capture;
    struct inkcell_draw_state *state;
    struct inkcell_focus_item storage[APP_BAR_STORAGE];
    struct inkcell_focus_map map;
};

static bool app_bar_open(struct app_bar_harness *h, uint32_t width) {
    h->capture = NULL;
    if (inkcell_capture_open(&h->capture, width, INKCELL_CAPTURE_HEIGHT, INKCELL_SCALE(4)) < 0) {
        return false;
    }
    h->state = inkcell_capture_state(h->capture);
    inkcell_focus_begin(&h->map, h->storage, APP_BAR_STORAGE);
    inkcell_fb_set_focus_map(h->state, &h->map);
    return true;
}

static void app_bar_close(struct app_bar_harness *h) {
    inkcell_capture_close(h->capture);
}

/* Six verbs, every one with a symbol, the first of them emphasized. */
static void app_bar_actions(struct inkcell_fb_bar_action *out, size_t count) {
    static const enum inkcell_icon icons[] = {
        INKCELL_ICON_COMPOSE, INKCELL_ICON_SHARE,   INKCELL_ICON_REFRESH, INKCELL_ICON_DELETE,
        INKCELL_ICON_PINNED,  INKCELL_ICON_HISTORY, INKCELL_ICON_EDIT,    INKCELL_ICON_DOWNLOAD};
    static const char *const labels[] = {"Reply", "Share", "Refresh", "Delete",
                                         "Pin",   "Log",   "Edit",    "Save"};
    for (size_t i = 0; i < count; ++i) {
        out[i] = (struct inkcell_fb_bar_action){
            .icon = icons[i],
            .label = labels[i],
            .focus_id = AB_ID_FIRST + (uint32_t)i,
        };
    }
}

#define AB_CHECK(cond, msg) INKCELL_TEST_FAIL_IF_CLEANUP((cond), app_bar_close(&h), (msg))

/* ---- the bar as it was -------------------------------------------------------------------- */

INKCELL_TEST_CASE(app_bar_without_actions_is_the_bar_it_always_was, unit) {
    struct app_bar_harness h;
    INKCELL_TEST_FAIL_IF(!app_bar_open(&h, INKCELL_CAPTURE_WIDTH), "the capture should open");

    for (size_t trail = 0U; trail <= 2U; ++trail) {
        struct inkcell_fb_layout layout = inkcell_fb_layout_begin(h.state, true, true);
        const struct inkcell_fb_app_bar bar = {
            .trail = {"Settings", "Modules"}, .trail_count = trail, .title = "Telemetry"};
        const int top = layout.body_y;
        const struct inkcell_fb_app_bar_fit fit = inkcell_fb_draw_app_bar(h.state, &layout, &bar);
        AB_CHECK(layout.body_y - top != inkcell_fb_app_bar_height(h.state, &layout, trail),
                 "a bar with no verbs takes exactly the height it always did");
        AB_CHECK(inkcell_fb_app_bar_measure(h.state, &layout, &bar) !=
                     inkcell_fb_app_bar_height(h.state, &layout, trail),
                 "and measuring the whole bar agrees with counting its trail");
        AB_CHECK(fit.shown != 0U || fit.hidden != 0U || fit.overflow.w != 0,
                 "and reports nothing on it and nothing behind it");
    }
    app_bar_close(&h);
    record_success(test_name);
}

/* ---- what fits ---------------------------------------------------------------------------- */

INKCELL_TEST_CASE(app_bar_actions_all_fit_on_a_wide_panel, unit) {
    struct app_bar_harness h;
    INKCELL_TEST_FAIL_IF(!app_bar_open(&h, INKCELL_CAPTURE_WIDTH), "the capture should open");

    struct inkcell_fb_bar_action actions[3];
    app_bar_actions(actions, 3U);
    struct inkcell_fb_layout layout = inkcell_fb_layout_begin(h.state, true, true);
    const struct inkcell_fb_app_bar bar = {.title = "Inbox",
                                           .actions = actions,
                                           .action_count = 3U,
                                           .overflow_focus_id = AB_ID_OVERFLOW};
    const struct inkcell_fb_app_bar_fit fit = inkcell_fb_draw_app_bar(h.state, &layout, &bar);

    AB_CHECK(fit.shown != 0x7U, "three icons fit a full panel");
    AB_CHECK(fit.hidden != 0U || fit.overflow.w != 0,
             "and with nothing behind it there is no overflow button to press");
    AB_CHECK(inkcell_focus_has(&h.map, AB_ID_OVERFLOW),
             "a button that was not drawn is not a place the cursor can go");
    for (uint32_t i = 0; i < 3U; ++i) {
        AB_CHECK(!inkcell_focus_has(&h.map, AB_ID_FIRST + i),
                 "every icon on the bar is a box the d-pad can land on");
    }
    app_bar_close(&h);
    record_success(test_name);
}

INKCELL_TEST_CASE(app_bar_menu_only_action_always_opens_the_overflow, unit) {
    struct app_bar_harness h;
    INKCELL_TEST_FAIL_IF(!app_bar_open(&h, INKCELL_CAPTURE_WIDTH), "the capture should open");

    struct inkcell_fb_bar_action actions[2];
    app_bar_actions(actions, 2U);
    actions[1].icon = INKCELL_ICON_NONE; /* menu only */
    struct inkcell_fb_layout layout = inkcell_fb_layout_begin(h.state, true, true);
    const struct inkcell_fb_app_bar bar = {.title = "Inbox",
                                           .actions = actions,
                                           .action_count = 2U,
                                           .overflow_focus_id = AB_ID_OVERFLOW};
    const struct inkcell_fb_app_bar_fit fit = inkcell_fb_draw_app_bar(h.state, &layout, &bar);

    AB_CHECK(fit.shown != 0x1U, "the verb with a symbol stands on the bar");
    AB_CHECK(fit.hidden != 1U || fit.overflow.w <= 0,
             "and the one without is behind an overflow button, however much room there is");
    AB_CHECK(!inkcell_focus_has(&h.map, AB_ID_OVERFLOW), "which the d-pad can reach");

    struct inkcell_fb_menu_item items[4];
    uint32_t ids[4];
    const size_t rows = inkcell_fb_app_bar_menu(&bar, &fit, items, ids, 4U);
    AB_CHECK(rows != 1U || strcmp(items[0].label, actions[1].label) != 0,
             "the menu holds exactly what the bar left off");
    AB_CHECK(ids[0] != actions[1].focus_id,
             "under the id the action already had, so one switch answers bar and menu alike");
    app_bar_close(&h);
    record_success(test_name);
}

INKCELL_TEST_CASE(app_bar_narrow_panel_overflows_from_the_end, unit) {
    struct app_bar_harness h;
    INKCELL_TEST_FAIL_IF(!app_bar_open(&h, APP_BAR_NARROW), "the capture should open");

    struct inkcell_fb_bar_action actions[6];
    app_bar_actions(actions, 6U);
    actions[0].emphasized = true;
    actions[3].disabled = true;
    struct inkcell_fb_layout layout = inkcell_fb_layout_begin(h.state, true, true);
    const struct inkcell_fb_app_bar bar = {.title = "Field station",
                                           .actions = actions,
                                           .action_count = 6U,
                                           .overflow_focus_id = AB_ID_OVERFLOW};
    const struct inkcell_fb_app_bar_fit fit = inkcell_fb_draw_app_bar(h.state, &layout, &bar);

    AB_CHECK(fit.hidden == 0U, "a narrow panel cannot hold a title and six verbs");
    AB_CHECK(fit.overflow.w <= 0, "so the rest are behind an overflow button");
    AB_CHECK((fit.shown & 0x1U) == 0U, "and the emphasized verb is the last to leave");

    /* What is on the bar is a prefix of the array: a verb never stands there while one listed
       before it is hidden. */
    bool gap = false;
    for (size_t i = 0; i < 6U; ++i) {
        const bool shown = (fit.shown & (1U << i)) != 0U;
        AB_CHECK(gap && shown, "the bar keeps verbs from the front, in the order given");
        gap = gap || !shown;
        const bool registered = inkcell_focus_has(&h.map, actions[i].focus_id);
        AB_CHECK(registered != (shown && !actions[i].disabled),
                 "the map holds a box for exactly the enabled verbs that were drawn");
    }

    struct inkcell_fb_menu_item items[8];
    uint32_t ids[8];
    const size_t rows = inkcell_fb_app_bar_menu(&bar, &fit, items, ids, 8U);
    AB_CHECK(rows != fit.hidden, "the menu is as long as what the fit says is hidden");
    size_t row = 0U;
    for (size_t i = 0; i < 6U; ++i) {
        if ((fit.shown & (1U << i)) != 0U) {
            continue;
        }
        AB_CHECK(ids[row] != actions[i].focus_id || items[row].disabled != actions[i].disabled,
                 "each hidden verb is a menu row, in the bar's order, disabled if it was");
        ++row;
    }
    app_bar_close(&h);
    record_success(test_name);
}

/* ---- heights the modes may not change ----------------------------------------------------- */

INKCELL_TEST_CASE(app_bar_modes_replace_the_heading_without_reflowing, unit) {
    struct app_bar_harness h;
    INKCELL_TEST_FAIL_IF(!app_bar_open(&h, INKCELL_CAPTURE_WIDTH), "the capture should open");

    struct inkcell_fb_bar_action actions[2];
    app_bar_actions(actions, 2U);
    const enum inkcell_fb_app_bar_mode modes[] = {
        INKCELL_FB_APP_BAR_NORMAL, INKCELL_FB_APP_BAR_SEARCH, INKCELL_FB_APP_BAR_SELECTION};
    int first = -1;
    for (size_t m = 0; m < sizeof modes / sizeof modes[0]; ++m) {
        struct inkcell_fb_layout layout = inkcell_fb_layout_begin(h.state, true, m % 2U == 0U);
        const struct inkcell_fb_app_bar bar = {
            .title = "Inbox",
            .actions = actions,
            .action_count = 2U,
            .mode = modes[m],
            .query = "ridge",
            .editing = true,
            .field_focus_id = AB_ID_OVERFLOW + 50U,
            .clear_focus_id = AB_ID_OVERFLOW + 51U,
        };
        const int top = layout.body_y;
        (void)inkcell_fb_draw_app_bar(h.state, &layout, &bar);
        const int taken = layout.body_y - top;
        AB_CHECK(taken != inkcell_fb_app_bar_measure(h.state, &layout, &bar),
                 "a bar takes the height it said it would, in every mode");
        if (first < 0) {
            first = taken;
        }
        AB_CHECK(taken != first, "and entering search or a selection never moves the body");
        if (modes[m] == INKCELL_FB_APP_BAR_SEARCH) {
            AB_CHECK(!inkcell_focus_has(&h.map, AB_ID_OVERFLOW + 50U) ||
                         !inkcell_focus_has(&h.map, AB_ID_OVERFLOW + 51U),
                     "the field and its clear mark are both places the d-pad can stand");
        }
    }

    /* A trail is the one thing that makes an ordinary bar taller, and a mode drops it. */
    struct inkcell_fb_layout layout = inkcell_fb_layout_begin(h.state, true, true);
    const struct inkcell_fb_app_bar search = {
        .trail = {"Settings"}, .trail_count = 1U, .mode = INKCELL_FB_APP_BAR_SEARCH};
    const int top = layout.body_y;
    (void)inkcell_fb_draw_app_bar(h.state, &layout, &search);
    AB_CHECK(layout.body_y - top != inkcell_fb_app_bar_height(h.state, &layout, 0U),
             "a search field is the screen's whole heading, so its trail is not drawn");
    app_bar_close(&h);
    record_success(test_name);
}

/* ---- the search field's caret ------------------------------------------------------------- */

/*
 * The leftmost column the caret lights, or -1 for none: the one thing on a search bar drawn in
 * the primary tone, so it can be found by colour without naming a theme. Drawn on a cleared page
 * each time, so a caret from the last call cannot be read as this one's.
 */
static int app_bar_caret_x(struct app_bar_harness *h, const struct inkcell_fb_app_bar *bar) {
    inkcell_fb_clear(h->state, inkcell_fb_color(h->state, INKCELL_COLOR_BG));
    inkcell_focus_begin(&h->map, h->storage, APP_BAR_STORAGE);
    struct inkcell_fb_layout layout = inkcell_fb_layout_begin(h->state, true, false);
    const int top = layout.body_y;
    (void)inkcell_fb_draw_app_bar(h->state, &layout, bar);
    const struct inkcell_rgb ink = inkcell_fb_tone_color(h->state, INKCELL_TONE_PRIMARY);
    uint32_t width = 0U;
    uint32_t height = 0U;
    size_t stride = 0U;
    const uint8_t *pixels = inkcell_capture_pixels(h->capture, &width, &height, &stride);
    for (uint32_t x = 0U; pixels != NULL && x < width; ++x) {
        for (int y = top; y < layout.body_y && y < (int)height; ++y) {
            const uint8_t *p = pixels + (size_t)y * stride + (size_t)x * 4U;
            if (p[2] == ink.r && p[1] == ink.g && p[0] == ink.b) {
                return (int)x;
            }
        }
    }
    return -1;
}

INKCELL_TEST_CASE(app_bar_search_caret_stands_where_the_keyboard_put_it, unit) {
    struct app_bar_harness h;
    INKCELL_TEST_FAIL_IF(!app_bar_open(&h, INKCELL_CAPTURE_WIDTH), "the capture should open");

    struct inkcell_fb_app_bar bar = {
        .mode = INKCELL_FB_APP_BAR_SEARCH, .query = "ridgeline", .editing = true};
    const int end = app_bar_caret_x(&h, &bar);
    AB_CHECK(end < 0, "an editing field draws a caret");
    bar.caret_back = 4U;
    const int middle = app_bar_caret_x(&h, &bar);
    AB_CHECK(middle < 0 || middle >= end, "a caret moved back is drawn before the end");
    bar.caret_back = strlen(bar.query);
    const int start = app_bar_caret_x(&h, &bar);
    AB_CHECK(start < 0 || start >= middle, "and one moved to the start before both");
    bar.caret_back = 99U;
    AB_CHECK(app_bar_caret_x(&h, &bar) != end, "a stale count is the end, not the start");
    bar.caret_back = 4U;
    bar.editing = false;
    AB_CHECK(app_bar_caret_x(&h, &bar) >= 0, "and a field without the keyboard draws none");

    /* Far longer than the field: the window follows the caret, so a caret at the start is on
       the panel where the short query's was, rather than scrolled off with the head. */
    char longer[200];
    memset(longer, 'm', sizeof longer - 1U);
    longer[sizeof longer - 1U] = '\0';
    bar = (struct inkcell_fb_app_bar){.mode = INKCELL_FB_APP_BAR_SEARCH,
                                      .query = longer,
                                      .editing = true,
                                      .caret_back = strlen(longer)};
    AB_CHECK(app_bar_caret_x(&h, &bar) != start, "a long query's caret at its start is shown");
    bar.caret_back = 0U;
    AB_CHECK(app_bar_caret_x(&h, &bar) <= start, "and at its end, after the tail");
    app_bar_close(&h);
    record_success(test_name);
}

INKCELL_TEST_CASE(app_bar_large_title_measures_what_it_draws, unit) {
    struct app_bar_harness h;
    INKCELL_TEST_FAIL_IF(!app_bar_open(&h, INKCELL_CAPTURE_WIDTH), "the capture should open");

    struct inkcell_fb_bar_action actions[2];
    app_bar_actions(actions, 2U);
    const int travel = inkcell_fb_large_title_travel(h.state);
    const int32_t offsets[] = {-12, 0, travel / 2, travel, travel * 3};
    for (size_t i = 0; i < sizeof offsets / sizeof offsets[0]; ++i) {
        struct inkcell_fb_layout layout = inkcell_fb_layout_begin(h.state, true, false);
        const struct inkcell_fb_app_bar bar = {.title = "Messages",
                                               .detail = "40 rows",
                                               .large = true,
                                               .offset = offsets[i],
                                               .actions = actions,
                                               .action_count = 2U};
        const int top = layout.body_y;
        const struct inkcell_fb_app_bar_fit fit = inkcell_fb_draw_app_bar(h.state, &layout, &bar);
        AB_CHECK(layout.body_y - top != inkcell_fb_app_bar_measure(h.state, &layout, &bar),
                 "a large title takes what it measured at every offset, overscroll included");
        AB_CHECK(fit.shown != 0x3U, "and carries its verbs at every size");

        /* The name a screen with nothing but a heading calls is the same bar underneath. */
        struct inkcell_fb_layout old = inkcell_fb_layout_begin(h.state, true, false);
        const struct inkcell_fb_large_title title = {.title = "Messages", .detail = "40 rows"};
        inkcell_fb_draw_large_title(h.state, &old, &title, offsets[i]);
        AB_CHECK(old.body_y != layout.body_y,
                 "and the large title's own call lands the body in the same place");
    }
    app_bar_close(&h);
    record_success(test_name);
}

/* ---- the compact action bar --------------------------------------------------------------- */

INKCELL_TEST_CASE(action_bar_compact_gives_its_second_row_to_the_body, unit) {
    struct app_bar_harness h;
    INKCELL_TEST_FAIL_IF(!app_bar_open(&h, INKCELL_CAPTURE_WIDTH), "the capture should open");

    const struct inkcell_fb_layout full = inkcell_fb_layout_begin(h.state, true, false);
    const struct inkcell_fb_layout same =
        inkcell_fb_layout_begin_footer(h.state, INKCELL_FB_FOOTER_FULL, false);
    const struct inkcell_fb_layout compact =
        inkcell_fb_layout_begin_footer(h.state, INKCELL_FB_FOOTER_COMPACT, false);

    AB_CHECK(full.footer_y != same.footer_y || full.footer != INKCELL_FB_FOOTER_FULL,
             "the old call is the full footer, exactly");
    AB_CHECK(compact.footer_y <= full.footer_y, "a compact footer starts lower down the panel");
    AB_CHECK(compact.rows < full.rows, "and the body keeps the room it gave back");
    AB_CHECK(inkcell_fb_action_bar_height(h.state, &compact) >=
                 inkcell_fb_action_bar_height(h.state, &full),
             "because the compact bar is the shorter of the two");
    AB_CHECK((int)INKCELL_CAPTURE_HEIGHT - compact.footer_y !=
                 inkcell_fb_action_bar_height(h.state, &compact),
             "and it is exactly the room the layout kept for it");
    app_bar_close(&h);
    record_success(test_name);
}

static const struct inkcell_button_action k_ab_items[] = {
    {INKCELL_BUTTON_A, INKCELL_STR_KEY_SPACE},     {INKCELL_BUTTON_B, INKCELL_STR_KEY_DELETE},
    {INKCELL_BUTTON_X, INKCELL_STR_KEY_CANCEL},    {INKCELL_BUTTON_Y, INKCELL_STR_KEY_LAYER_UPPER},
    {INKCELL_BUTTON_SELECT, INKCELL_STR_TIME_NOW}, {INKCELL_BUTTON_SHOULDERS, INKCELL_STR_TIME_NOW},
};
static const struct inkcell_button_action k_ab_more = {INKCELL_BUTTON_START,
                                                       INKCELL_STR_KEY_LAYER_EMOJI_MORE};

INKCELL_TEST_CASE(action_bar_limit_says_where_the_menu_starts, unit) {
    struct app_bar_harness h;
    INKCELL_TEST_FAIL_IF(!app_bar_open(&h, INKCELL_CAPTURE_WIDTH), "the capture should open");

    const size_t count = sizeof k_ab_items / sizeof k_ab_items[0];
    const struct inkcell_fb_layout layout =
        inkcell_fb_layout_begin_footer(h.state, INKCELL_FB_FOOTER_COMPACT, false);

    const struct inkcell_fb_action_bar all = {.items = k_ab_items, .count = 3U};
    AB_CHECK(inkcell_fb_draw_action_bar(h.state, &layout, &all) != 3U,
             "with no limit and room to spare, every item is on the bar");

    inkcell_focus_begin(&h.map, h.storage, APP_BAR_STORAGE);
    const struct inkcell_fb_action_bar limited = {
        .items = k_ab_items, .count = count, .limit = 2U, .more = &k_ab_more};
    const size_t from = inkcell_fb_draw_action_bar(h.state, &layout, &limited);
    AB_CHECK(from != 2U, "a limit of two puts two on the bar");
    AB_CHECK(!inkcell_focus_has(&h.map, INKCELL_FOCUS_ACTION_KEY(INKCELL_KEY_START)),
             "and the cap to the rest is drawn, and pressable");
    AB_CHECK(inkcell_focus_has(&h.map, INKCELL_FOCUS_ACTION_KEY(INKCELL_KEY_X)),
             "while a press held back for the limit is not on the frame at all");

    struct inkcell_fb_menu_item items[INKCELL_ACTIONS_MAX];
    const size_t rows = inkcell_fb_action_bar_menu(&limited, from, items, INKCELL_ACTIONS_MAX);
    AB_CHECK(rows != count - from, "the menu behind it is the rest of the array");
    AB_CHECK(strcmp(items[0].label, inkcell_str(k_ab_items[from].label)) != 0,
             "row by row, starting from where the bar stopped");
    app_bar_close(&h);
    record_success(test_name);
}

INKCELL_TEST_CASE(action_bar_keeps_the_more_cap_when_room_runs_out, unit) {
    struct app_bar_harness h;
    INKCELL_TEST_FAIL_IF(!app_bar_open(&h, 240U), "the capture should open");

    const size_t count = sizeof k_ab_items / sizeof k_ab_items[0];
    const struct inkcell_fb_layout layout =
        inkcell_fb_layout_begin_footer(h.state, INKCELL_FB_FOOTER_COMPACT, false);
    const struct inkcell_fb_action_bar bar = {
        .items = k_ab_items, .count = count, .more = &k_ab_more, .emphasize_first = true};
    const size_t from = inkcell_fb_draw_action_bar(h.state, &layout, &bar);
    AB_CHECK(from >= count, "a narrow panel cannot hold six hints");
    AB_CHECK(!inkcell_focus_has(&h.map, INKCELL_FOCUS_ACTION_KEY(INKCELL_KEY_START)),
             "and the one it drops for room is never the way to the rest");
    app_bar_close(&h);
    record_success(test_name);
}

/* ---- the first-use tip -------------------------------------------------------------------- */

INKCELL_TEST_CASE(action_tip_is_live_for_its_journey_and_no_longer, unit) {
    struct app_bar_harness h;
    INKCELL_TEST_FAIL_IF(!app_bar_open(&h, INKCELL_CAPTURE_WIDTH), "the capture should open");

    const uint64_t since = 5000U;
    const uint64_t rise = inkcell_fb_motion(h.state, INKCELL_MOTION_MEDIUM);
    const uint64_t fall = inkcell_fb_motion(h.state, INKCELL_MOTION_SHORT);
    const uint64_t end = since + rise + INKCELL_FB_ACTION_TIP_HOLD_MS + fall;
    const struct inkcell_fb_action_tip tip = {
        .action = &k_ab_more, .text = "More under START", .since_ms = since};

    const uint64_t live_at[] = {since - 100U, since, since + rise, end - 1U};
    for (size_t i = 0; i < sizeof live_at / sizeof live_at[0]; ++i) {
        inkcell_fb_state_set_now(h.state, live_at[i]);
        AB_CHECK(!inkcell_fb_action_tip_live(h.state, &tip),
                 "a tip is live from before it starts until it has sunk behind the bar");
    }
    inkcell_fb_state_set_now(h.state, end);
    AB_CHECK(inkcell_fb_action_tip_live(h.state, &tip),
             "and not a millisecond after, which is when the app records it as seen");

    const struct inkcell_fb_action_tip empty = {.since_ms = since};
    inkcell_fb_state_set_now(h.state, since + rise);
    AB_CHECK(inkcell_fb_action_tip_live(h.state, &empty), "a tip with no words is no tip");
    AB_CHECK(inkcell_fb_action_tip_live(h.state, NULL), "and neither is no tip at all");
    app_bar_close(&h);
    record_success(test_name);
}
