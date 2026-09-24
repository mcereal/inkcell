#define _POSIX_C_SOURCE 200809L

/*
 * Focus, selection, press, hover and disabled, kept apart.
 *
 * The toolkit used to have one word - "selected" - for where the d-pad was, and a list painted
 * it as the loudest fill on the screen because it was the only mark there was. These cases hold
 * the split: which layer an interaction earns, what a disabled label fades to, what a focused
 * row is filled with on a theme that lifts and on one that fills, and which box the frame tells
 * the focus ring to go to - the focused one, never the selected one.
 */

#include "framework/inkcell_test.h"

#include "inkcell/ui/fb_capture.h"
#include "inkcell/ui/focus.h"
#include "inkcell/ui/theme.h"
#include "inkcell/ui/widgets.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define INTERACTION_STORAGE 32U

enum {
    I_ID_ROW = 1000,
    I_ID_SHEET = 2000,
    I_ID_TAB = 3000,
};

static bool same_rgb(struct inkcell_rgb a, struct inkcell_rgb b) {
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

INKCELL_TEST_CASE(interaction_layer_is_the_strongest_transient_state, unit) {
    const struct inkcell_interaction none = {0};
    INKCELL_TEST_FAIL_IF(inkcell_interaction_layer(none) != INKCELL_STATE_REST,
                         "nothing happening is the resting paint");
    INKCELL_TEST_FAIL_IF(inkcell_interaction_layer((struct inkcell_interaction){.hovered = true}) !=
                             INKCELL_STATE_HOVERED,
                         "a pointer over a control earns the hover layer");
    INKCELL_TEST_FAIL_IF(inkcell_interaction_layer((struct inkcell_interaction){
                             .hovered = true, .focused = true}) != INKCELL_STATE_FOCUSED,
                         "focus outranks hover: the d-pad is where the next press goes");
    INKCELL_TEST_FAIL_IF(inkcell_interaction_layer((struct inkcell_interaction){
                             .focused = true, .pressed = true}) != INKCELL_STATE_PRESSED,
                         "a press outranks focus: it is happening now");
    INKCELL_TEST_FAIL_IF(
        inkcell_interaction_layer((struct inkcell_interaction){.selected = true}) !=
            INKCELL_STATE_REST,
        "selection is a resting paint, never a layer - it has to survive the cursor leaving");
    INKCELL_TEST_FAIL_IF(inkcell_interaction_layer((struct inkcell_interaction){
                             .focused = true, .pressed = true, .disabled = true}) !=
                             INKCELL_STATE_REST,
                         "a disabled control answers neither the cursor nor the press");
    record_success(test_name);
}

/* Each layer a step further than the last, so the three are tellable apart on one fill. */
INKCELL_TEST_CASE(interaction_layers_step_in_order, unit) {
    const struct inkcell_rgb fill = {20, 20, 20};
    const struct inkcell_rgb ink = {240, 240, 240};
    const struct inkcell_rgb hovered = inkcell_theme_state_layer(fill, ink, INKCELL_STATE_HOVERED);
    const struct inkcell_rgb focused = inkcell_theme_state_layer(fill, ink, INKCELL_STATE_FOCUSED);
    const struct inkcell_rgb pressed = inkcell_theme_state_layer(fill, ink, INKCELL_STATE_PRESSED);
    INKCELL_TEST_FAIL_IF(!(fill.r < hovered.r && hovered.r < focused.r && focused.r < pressed.r),
                         "hover, focus and press should each move the fill further to its ink");
    INKCELL_TEST_FAIL_IF(!same_rgb(inkcell_theme_state_layer(fill, ink, INKCELL_STATE_REST), fill),
                         "rest leaves the fill alone");
    record_success(test_name);
}

INKCELL_TEST_CASE(interaction_disabled_ink_fades_towards_its_fill, unit) {
    const struct inkcell_theme *theme = inkcell_theme_default();
    const struct inkcell_rgb fill = theme->colors[INKCELL_COLOR_BG];
    const struct inkcell_rgb ink = theme->colors[INKCELL_COLOR_TEXT];
    const struct inkcell_rgb faded = inkcell_theme_disabled_ink(fill, ink);
    const double full = inkcell_theme_contrast(ink, fill);
    const double held = inkcell_theme_contrast(faded, fill);
    INKCELL_TEST_FAIL_IF(!(held > 1.0 && held < full),
                         "a disabled label is still there, and plainly less there");
    /* Past a dim label, because a disabled one that read as merely secondary would be a control
       nobody could tell was off. */
    INKCELL_TEST_FAIL_IF(held >=
                             inkcell_theme_contrast(theme->colors[INKCELL_COLOR_TEXT_DIM], fill),
                         "disabled should sit below the dim tier");
    INKCELL_TEST_FAIL_IF(!same_rgb(inkcell_theme_disabled_ink(fill, fill), fill),
                         "ink already on its fill has nowhere to fade to");
    record_success(test_name);
}

/*
 * A focused row lifts off whatever it is on, and the theme that exists to be loud still fills.
 *
 * The lift is relative, which is the half a single cursor colour could never do: on a card it
 * has to come out lighter than the card, not lighter than the panel.
 */
INKCELL_TEST_CASE(interaction_focused_row_lifts_or_fills_by_theme, unit) {
    struct inkcell_capture *capture = NULL;
    INKCELL_TEST_FAIL_IF(inkcell_capture_open(&capture, INKCELL_CAPTURE_WIDTH,
                                              INKCELL_CAPTURE_HEIGHT, INKCELL_SCALE(2)) < 0,
                         "the capture should open");
    struct inkcell_draw_state *state = inkcell_capture_state(capture);

    const struct inkcell_theme *dark = inkcell_theme_by_id("dark");
    const struct inkcell_theme *contrast = inkcell_theme_by_id("contrast");
    INKCELL_TEST_FAIL_IF_CLEANUP(dark == NULL || contrast == NULL, inkcell_capture_close(capture),
                                 "both themes should exist");
    inkcell_fb_state_set_theme(state, dark, 0);
    const struct inkcell_rgb bg = inkcell_fb_color(state, INKCELL_COLOR_BG);
    const struct inkcell_rgb card = inkcell_fb_color(state, INKCELL_COLOR_SURFACE);
    const struct inkcell_rgb on_bg = inkcell_fb_focus_fill(state, INKCELL_COLOR_BG);
    const struct inkcell_rgb on_card = inkcell_fb_focus_fill(state, INKCELL_COLOR_SURFACE);
    INKCELL_TEST_FAIL_IF_CLEANUP(same_rgb(on_bg, bg) || same_rgb(on_card, card),
                                 inkcell_capture_close(capture),
                                 "a focused row has to come off its ground");
    INKCELL_TEST_FAIL_IF_CLEANUP(same_rgb(on_bg, on_card), inkcell_capture_close(capture),
                                 "the lift is relative to the ground, so two grounds lift apart");
    INKCELL_TEST_FAIL_IF_CLEANUP(
        same_rgb(on_bg, inkcell_fb_color(state, INKCELL_COLOR_SURFACE_SEL)),
        inkcell_capture_close(capture), "a lifting theme should not paint the old cursor bar");
    INKCELL_TEST_FAIL_IF_CLEANUP(!same_rgb(inkcell_fb_focus_ink(state, INKCELL_TONE_DIM, true),
                                           inkcell_fb_tone_color(state, INKCELL_TONE_DIM)),
                                 inkcell_capture_close(capture), "a lifted row keeps its own inks");

    inkcell_fb_state_set_theme(state, contrast, 0);
    INKCELL_TEST_FAIL_IF_CLEANUP(!same_rgb(inkcell_fb_focus_fill(state, INKCELL_COLOR_SURFACE),
                                           inkcell_fb_color(state, INKCELL_COLOR_SURFACE_SEL)),
                                 inkcell_capture_close(capture),
                                 "the contrast theme keeps its inverse-video bar");
    INKCELL_TEST_FAIL_IF_CLEANUP(!same_rgb(inkcell_fb_focus_ink(state, INKCELL_TONE_NORMAL, false),
                                           inkcell_fb_color(state, INKCELL_COLOR_TEXT_ON_SEL)),
                                 inkcell_capture_close(capture),
                                 "and the ink that was checked against it");
    inkcell_capture_close(capture);
    record_success(test_name);
}

/* ---- where the ring is told to go ------------------------------------------------------- */

INKCELL_TEST_CASE(interaction_mark_is_last_writer_and_ignores_none, unit) {
    struct inkcell_focus_item storage[4];
    struct inkcell_focus_map map;
    inkcell_focus_begin(&map, storage, 4U);
    INKCELL_TEST_FAIL_IF(inkcell_focus_marked(&map) != INKCELL_FOCUS_NONE,
                         "a new frame has marked nothing");
    inkcell_focus_mark(&map, I_ID_ROW);
    inkcell_focus_mark(&map, I_ID_SHEET);
    INKCELL_TEST_FAIL_IF(inkcell_focus_marked(&map) != I_ID_SHEET,
                         "the last mark wins: it is the one drawn on top");
    inkcell_focus_mark(&map, INKCELL_FOCUS_NONE);
    INKCELL_TEST_FAIL_IF(inkcell_focus_marked(&map) != I_ID_SHEET,
                         "a component with no id cannot take the mark away");
    inkcell_focus_begin(&map, storage, 4U);
    INKCELL_TEST_FAIL_IF(inkcell_focus_marked(&map) != INKCELL_FOCUS_NONE,
                         "and the next frame starts clean");
    record_success(test_name);
}

/*
 * The list marks its cursor row, the tab strip marks nothing, and a sheet drawn after the list
 * takes the mark - so the ring follows the d-pad and never sits on the current tab.
 */
INKCELL_TEST_CASE(interaction_frame_marks_focus_and_not_selection, unit) {
    struct inkcell_capture *capture = NULL;
    INKCELL_TEST_FAIL_IF(inkcell_capture_open(&capture, INKCELL_CAPTURE_WIDTH,
                                              INKCELL_CAPTURE_HEIGHT, INKCELL_SCALE(2)) < 0,
                         "the capture should open");
    struct inkcell_draw_state *state = inkcell_capture_state(capture);
    struct inkcell_focus_item storage[INTERACTION_STORAGE];
    struct inkcell_focus_map map;
    inkcell_focus_begin(&map, storage, INTERACTION_STORAGE);
    inkcell_fb_set_focus_map(state, &map);

    /* The current tab is *selected*: drawn in its tonal pill, registered, never marked. */
    const struct inkcell_fb_chip chips[] = {
        {.label = "One", .focus_id = I_ID_TAB},
        {.label = "Two", .focus_id = I_ID_TAB + 1U},
    };
    struct inkcell_fb_layout layout = inkcell_fb_layout_begin(state, false, false);
    inkcell_fb_draw_nav_bar(state, &layout, chips, 2U, 1U);
    INKCELL_TEST_FAIL_IF_CLEANUP(inkcell_focus_marked(&map) != INKCELL_FOCUS_NONE,
                                 inkcell_capture_close(capture),
                                 "the selected tab is not where the d-pad is");

    struct inkcell_fb_list list = inkcell_fb_list_begin(&layout, 5U, 2U);
    inkcell_fb_list_focus(&list, I_ID_ROW);
    uint32_t index = 0U;
    while (inkcell_fb_list_next(&list, &index)) {
        const struct inkcell_fb_list_item item = {.text = "row", .tone = INKCELL_TONE_NORMAL};
        inkcell_fb_list_item(state, &list, index, &item);
    }
    INKCELL_TEST_FAIL_IF_CLEANUP(inkcell_focus_marked(&map) != I_ID_ROW + 2U,
                                 inkcell_capture_close(capture),
                                 "the list's cursor row is the focused box");

    /* A dialog over it: its focused answer is drawn later, so it is where the ring goes. */
    const struct inkcell_fb_dialog dialog = {
        .headline = "Delete?",
        .text = "",
        .accept = "Delete",
        .cancel = "Keep",
        .cursor = 1U,
        .action_focus_id = I_ID_SHEET,
    };
    (void)inkcell_fb_draw_dialog(state, &layout, &dialog, 9000U, true);
    INKCELL_TEST_FAIL_IF_CLEANUP(inkcell_focus_marked(&map) != I_ID_SHEET + 1U,
                                 inkcell_capture_close(capture),
                                 "the answer under the dialog's cursor takes the mark");
    inkcell_capture_close(capture);
    record_success(test_name);
}

/*
 * The lift rows are checked on a card as well as the panel, and only on a theme that lifts.
 *
 * Found by walking the secondary family's base down the grey scale on a copy of the dark theme:
 * somewhere on the way it still clears everything the ground asks of it and fails only on a
 * focused row. The same palette with `focus_fill` set never draws that row, so it must pass
 * whatever the lift would have said - the check is about what is on the panel, not what could be.
 */
INKCELL_TEST_CASE(interaction_lift_contract_holds_on_cards_and_only_for_lifting_themes, unit) {
    const struct inkcell_theme *dark = inkcell_theme_by_id("dark");
    INKCELL_TEST_FAIL_IF(dark == NULL, "the dark theme should exist");
    INKCELL_TEST_FAIL_IF(!inkcell_theme_validate(dark, NULL, 0U),
                         "dark should validate as shipped");

    bool found = false;
    for (int grey = 160; grey >= 60 && !found; --grey) {
        struct inkcell_theme lifting = *dark;
        lifting.colors[INKCELL_COLOR_SECONDARY] =
            (struct inkcell_rgb){(uint8_t)grey, (uint8_t)grey, (uint8_t)grey};
        char reason[160] = "";
        if (inkcell_theme_validate(&lifting, reason, sizeof reason)) {
            continue;
        }
        struct inkcell_theme filling = lifting;
        filling.focus_fill = true;
        if (inkcell_theme_validate(&filling, NULL, 0U)) {
            /* The first grey that only a lift rejects. It has to be the lift *on a card* that
               caught it for this to be the case the card rows were added for; the panel's lift is
               the easier of the two, since the card sits nearer the text. */
            INKCELL_TEST_FAIL_IF(strstr(reason, "state") == NULL,
                                 "the rejection should come from a focused-state pair");
            found = true;
        } else {
            break; /* a rest contract caught it first; nothing lift-only lies further down */
        }
    }
    INKCELL_TEST_FAIL_IF(!found, "some secondary should fail the lift and pass everywhere else - "
                                 "and a filling theme should not be held to it");
    record_success(test_name);
}

/* A heading or note the cursor stands on is registered and ringed; one it is not on is neither,
   because most lists never park there and a click must not land on a title. */
INKCELL_TEST_CASE(interaction_focused_subheader_and_note_take_the_mark, unit) {
    struct inkcell_capture *capture = NULL;
    INKCELL_TEST_FAIL_IF(inkcell_capture_open(&capture, INKCELL_CAPTURE_WIDTH,
                                              INKCELL_CAPTURE_HEIGHT, INKCELL_SCALE(2)) < 0,
                         "the capture should open");
    struct inkcell_draw_state *state = inkcell_capture_state(capture);
    struct inkcell_focus_item storage[INTERACTION_STORAGE];
    struct inkcell_focus_map map;

    for (uint32_t cursor = 0U; cursor < 2U; ++cursor) {
        inkcell_focus_begin(&map, storage, INTERACTION_STORAGE);
        inkcell_fb_set_focus_map(state, &map);
        struct inkcell_fb_layout layout = inkcell_fb_layout_begin(state, false, false);
        struct inkcell_fb_list list = inkcell_fb_list_begin(&layout, 3U, cursor);
        inkcell_fb_list_focus(&list, I_ID_ROW);
        uint32_t index = 0U;
        while (inkcell_fb_list_next(&list, &index)) {
            if (index == 0U) {
                inkcell_fb_list_subheader(state, &list, index, "Section");
            } else if (index == 1U) {
                inkcell_fb_list_note(state, &list, index, "", "A note");
            } else {
                inkcell_fb_list_row(state, &list, index, "row", INKCELL_TONE_NORMAL);
            }
        }
        INKCELL_TEST_FAIL_IF_CLEANUP(inkcell_focus_marked(&map) != I_ID_ROW + cursor,
                                     inkcell_capture_close(capture),
                                     cursor == 0U ? "a focused heading should take the ring"
                                                  : "a focused note should take the ring");
        INKCELL_TEST_FAIL_IF_CLEANUP(
            inkcell_focus_has(&map, I_ID_ROW + (1U - cursor)), inkcell_capture_close(capture),
            "a heading or note the cursor is not on is not somewhere to click or land");
    }
    inkcell_capture_close(capture);
    record_success(test_name);
}
