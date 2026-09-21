#define _POSIX_C_SOURCE 200809L

/*
 * The layer: the lifetime, the placement and the stack.
 *
 * Nothing here looks at a pixel. What a dialog or a menu *looks* like is the golden sheet's
 * question, and a digest of a panel is the only honest answer to it; what this suite is about
 * is the three things the layer decides before a pixel is drawn, and each of them is a
 * question a picture cannot answer:
 *
 *   - **How long a layer lives.** The hard part, and the reason the model exists: a layer the
 *     app has let go of is still on the panel, and the call that describes it has to keep
 *     returning true until it is not. A still picture cannot show a lifetime.
 *   - **Where the box lands.** A menu with no room under its anchor goes above it; a sheet
 *     sits flush on the edge of its region; a layer that travels near stays inside that
 *     region. Each is arithmetic with an answer a test can state.
 *   - **Who owns the press.** Three modals stacked, and exactly one of them is the answer.
 *     There is no pixel anywhere on the frame that says which.
 *
 * The clock is stepped by hand throughout, which is what inkcell_fb_state_set_now() is for: a
 * layer's position is derived from the clock rather than accumulated, so a test can put it
 * anywhere in its travel and read the answer exactly.
 */

#include "framework/inkcell_test.h"

#include "inkcell/ui/fb_capture.h"
#include "inkcell/ui/overlay.h"
#include "inkcell/ui/theme.h"

#define OVERLAY_W 400U
#define OVERLAY_H 300U

/* Ids a screen would have in an enum of its own. */
enum {
    OVERLAY_ID_A = 1,
    OVERLAY_ID_B,
    OVERLAY_ID_C,
    OVERLAY_ID_D,
    OVERLAY_ID_E,
};

struct overlay_harness {
    struct inkcell_capture *capture;
    struct inkcell_draw_state *state;
    uint64_t now_ms;
};

static bool overlay_open(struct overlay_harness *h) {
    h->capture = NULL;
    if (inkcell_capture_open(&h->capture, OVERLAY_W, OVERLAY_H, INKCELL_SCALE(1)) < 0) {
        return false;
    }
    h->state = inkcell_capture_state(h->capture);
    h->now_ms = 1000U;
    inkcell_fb_state_set_now(h->state, h->now_ms);
    return true;
}

static void overlay_close(struct overlay_harness *h) {
    inkcell_capture_close(h->capture);
}

/* A frame boundary: the clock moves on and the layer order starts again, which is what the
   backend does at inkcell_fb_app_frame_begin(). Without the second half every begin() in a
   test would stack on the last one's order and "drawn last" would mean "drawn last ever". */
static void overlay_frame(struct overlay_harness *h, uint32_t step_ms) {
    h->now_ms += step_ms;
    inkcell_fb_state_set_now(h->state, h->now_ms);
    inkcell_fb_app_frame_begin(h->state);
}

/* How long a layer takes to arrive on this theme, with a frame's slack either side. Read from
   the theme rather than written down, so a theme with different motion does not fail this
   suite for having different motion. */
static uint32_t overlay_in_ms(const struct overlay_harness *h) {
    return inkcell_fb_motion(h->state, INKCELL_MOTION_MEDIUM) + 1U;
}

static uint32_t overlay_out_ms(const struct overlay_harness *h) {
    return inkcell_fb_motion(h->state, INKCELL_MOTION_SHORT) + 1U;
}

/* Opens and immediately closes a layer, which is what a screen's draw does around its content.
   Returns whether there was any of it on the panel. */
static bool overlay_show(struct overlay_harness *h, const struct inkcell_overlay *desc,
                         struct inkcell_overlay_frame *out) {
    struct inkcell_overlay_frame frame;
    const bool up = inkcell_fb_overlay_begin(h->state, desc, &frame);
    if (up) {
        inkcell_fb_overlay_end(h->state, &frame);
    }
    if (out != NULL) {
        *out = frame;
    }
    return up;
}

/* A sheet over the whole panel: the descriptor most cases start from. */
static struct inkcell_overlay overlay_sheet(uint32_t id, bool up) {
    return (struct inkcell_overlay){
        .id = id,
        .up = up,
        .placement = INKCELL_OVERLAY_BOTTOM,
        .travel = INKCELL_OVERLAY_TRAVEL_OFF_PANEL,
        .w = (int)OVERLAY_W,
        .h = 80,
    };
}

/* ---- the lifetime --------------------------------------------------------------------------- */

/*
 * A layer arrives: it is on the panel from the first frame, and it moves.
 *
 * The first half is not obvious and is deliberate. inkcell_anim_track() adopts on first sight
 * precisely so nothing animates on the frame a screen opens, and a layer has to step around
 * that - an overlay appearing is the one thing on the panel that is worth showing arriving.
 */
INKCELL_TEST_CASE(overlay_arrives_from_off_the_panel, unit) {
    struct overlay_harness h;
    INKCELL_TEST_FAIL_IF(!overlay_open(&h), "capture should open");

    struct inkcell_overlay_frame first;
    const struct inkcell_overlay desc = overlay_sheet(OVERLAY_ID_A, true);
    INKCELL_TEST_FAIL_IF_CLEANUP(!overlay_show(&h, &desc, &first), overlay_close(&h),
                                 "a layer going up should be on the panel");
    INKCELL_TEST_FAIL_IF_CLEANUP(first.progress != 0, overlay_close(&h),
                                 "it should start at the beginning of its travel");
    INKCELL_TEST_FAIL_IF_CLEANUP(first.box.y < (int)OVERLAY_H, overlay_close(&h),
                                 "off the panel means off the panel");

    overlay_frame(&h, overlay_in_ms(&h));
    struct inkcell_overlay_frame settled;
    INKCELL_TEST_FAIL_IF_CLEANUP(!overlay_show(&h, &desc, &settled), overlay_close(&h),
                                 "it should still be up");
    INKCELL_TEST_FAIL_IF_CLEANUP(settled.progress != INKCELL_ANIM_ONE, overlay_close(&h),
                                 "it should have arrived");
    INKCELL_TEST_FAIL_IF_CLEANUP(settled.box.y != settled.rest.y, overlay_close(&h),
                                 "an arrived layer is at its resting place");

    overlay_close(&h);
    record_success(test_name);
}

/*
 * A layer the app has let go of is still on the panel, and the call still returns true.
 *
 * The whole model in one case. `up` false is not "gone" - it is the exit, and the screen's
 * branch has to keep running for the length of it or the content the layer is drawing on its
 * way out has nobody to describe it.
 */
INKCELL_TEST_CASE(overlay_outlives_the_app_letting_go, unit) {
    struct overlay_harness h;
    INKCELL_TEST_FAIL_IF(!overlay_open(&h), "capture should open");

    const struct inkcell_overlay up = overlay_sheet(OVERLAY_ID_A, true);
    (void)overlay_show(&h, &up, NULL);
    overlay_frame(&h, overlay_in_ms(&h));
    (void)overlay_show(&h, &up, NULL);

    /* The app lets go. The layer is still there, and still has somewhere to be. */
    overlay_frame(&h, 1U);
    const struct inkcell_overlay down = overlay_sheet(OVERLAY_ID_A, false);
    struct inkcell_overlay_frame leaving;
    INKCELL_TEST_FAIL_IF_CLEANUP(!overlay_show(&h, &down, &leaving), overlay_close(&h),
                                 "a layer let go of is still on the panel");
    INKCELL_TEST_FAIL_IF_CLEANUP(leaving.arriving, overlay_close(&h), "and it knows it is leaving");
    INKCELL_TEST_FAIL_IF_CLEANUP(!inkcell_fb_overlay_showing(h.state, OVERLAY_ID_A),
                                 overlay_close(&h), "showing() should say so too");

    /* And once the exit has run, it is not. */
    overlay_frame(&h, overlay_out_ms(&h));
    INKCELL_TEST_FAIL_IF_CLEANUP(overlay_show(&h, &down, NULL), overlay_close(&h),
                                 "a layer that has finished leaving is gone");
    INKCELL_TEST_FAIL_IF_CLEANUP(inkcell_fb_overlay_showing(h.state, OVERLAY_ID_A),
                                 overlay_close(&h), "and its slot is released");

    overlay_close(&h);
    record_success(test_name);
}

/*
 * A layer nobody ever put up is never on the panel.
 *
 * Worth its own case because the branch a screen writes is `if (begin(...))` with `up` coming
 * straight out of a snapshot, and the common value of that is false. A model that allocated a
 * slot for every overlay a screen *could* show would run out on the first screen with five.
 */
INKCELL_TEST_CASE(overlay_that_was_never_up_is_never_shown, unit) {
    struct overlay_harness h;
    INKCELL_TEST_FAIL_IF(!overlay_open(&h), "capture should open");

    const struct inkcell_overlay down = overlay_sheet(OVERLAY_ID_A, false);
    INKCELL_TEST_FAIL_IF_CLEANUP(overlay_show(&h, &down, NULL), overlay_close(&h),
                                 "a layer that is not up should not be on the panel");
    INKCELL_TEST_FAIL_IF_CLEANUP(inkcell_fb_overlay_showing(h.state, OVERLAY_ID_A),
                                 overlay_close(&h), "and should hold no slot");

    overlay_close(&h);
    record_success(test_name);
}

/* ---- where the box lands ----------------------------------------------------------------------
 */

/* A layer against the bottom of its region sits flush on it: that edge is where it came from,
   and a sheet standing a gutter clear of it is a card that happens to be low down. */
INKCELL_TEST_CASE(overlay_bottom_rests_on_the_edge, unit) {
    struct overlay_harness h;
    INKCELL_TEST_FAIL_IF(!overlay_open(&h), "capture should open");

    struct inkcell_overlay desc = overlay_sheet(OVERLAY_ID_A, true);
    desc.bounds = (struct inkcell_fb_rect){0, 40, (int)OVERLAY_W, 200};
    struct inkcell_overlay_frame frame;
    (void)overlay_show(&h, &desc, &frame);

    INKCELL_TEST_FAIL_IF_CLEANUP(frame.rest.y + frame.rest.h != 240, overlay_close(&h),
                                 "it should rest on the bottom of its region");
    INKCELL_TEST_FAIL_IF_CLEANUP(frame.rest.w != (int)OVERLAY_W, overlay_close(&h),
                                 "a sheet asked for the full width should get it");

    overlay_close(&h);
    record_success(test_name);
}

/*
 * A menu with no room under its anchor goes above it.
 *
 * Decided by the layer rather than by the caller, because the caller would have to measure the
 * panel to answer it - and would then be one more place that has an opinion about where the
 * bottom of the screen is.
 */
INKCELL_TEST_CASE(overlay_anchor_flips_when_there_is_no_room, unit) {
    struct overlay_harness h;
    INKCELL_TEST_FAIL_IF(!overlay_open(&h), "capture should open");

    struct inkcell_overlay desc = {
        .id = OVERLAY_ID_A,
        .up = true,
        .placement = INKCELL_OVERLAY_ANCHOR,
        .travel = INKCELL_OVERLAY_TRAVEL_NONE,
        .w = 120,
        .h = 100,
        .anchor = {20, 20, 60, 20},
    };
    struct inkcell_overlay_frame below;
    (void)overlay_show(&h, &desc, &below);
    INKCELL_TEST_FAIL_IF_CLEANUP(below.rest.y <= desc.anchor.y, overlay_close(&h),
                                 "with room under it, a menu hangs below its anchor");
    /* Its leading edge lines up with the anchor's: a menu is *about* the control it came out
       of, and that is what says so. */
    INKCELL_TEST_FAIL_IF_CLEANUP(below.rest.x != desc.anchor.x, overlay_close(&h),
                                 "and lines up with its anchor's leading edge");

    /* The same menu off a control near the bottom: there is no room under it now. */
    overlay_frame(&h, 1U);
    desc.id = OVERLAY_ID_B;
    desc.anchor.y = (int)OVERLAY_H - 40;
    struct inkcell_overlay_frame above;
    (void)overlay_show(&h, &desc, &above);
    INKCELL_TEST_FAIL_IF_CLEANUP(above.rest.y + above.rest.h > desc.anchor.y, overlay_close(&h),
                                 "with no room under it, a menu goes above its anchor");

    overlay_close(&h);
    record_success(test_name);
}

/*
 * A near travel stays inside its region, and a layer the size of its region does not travel at
 * all.
 *
 * The second half is the one that matters, and it is not a degenerate case - it is what a
 * dialog filling the body does every time. A layer half outside its bounds is a layer with
 * half its content clipped and, because the focus map goes through the same clip, half its
 * controls unreachable: a dialog rising into place would not answer an A press on the frame it
 * arrived. What says such a modal arrived is its scrim easing in under it.
 */
INKCELL_TEST_CASE(overlay_near_travel_stays_inside_its_region, unit) {
    struct overlay_harness h;
    INKCELL_TEST_FAIL_IF(!overlay_open(&h), "capture should open");

    const struct inkcell_fb_rect bounds = {0, 0, (int)OVERLAY_W, (int)OVERLAY_H};
    struct inkcell_overlay desc = {
        .id = OVERLAY_ID_A,
        .up = true,
        .placement = INKCELL_OVERLAY_CENTER,
        .travel = INKCELL_OVERLAY_TRAVEL_NEAR,
        .w = 200,
        .h = 60,
        .bounds = bounds,
    };
    struct inkcell_overlay_frame small;
    (void)overlay_show(&h, &desc, &small);
    INKCELL_TEST_FAIL_IF_CLEANUP(small.box.y <= small.rest.y, overlay_close(&h),
                                 "a small layer should start below where it is going");
    INKCELL_TEST_FAIL_IF_CLEANUP(small.box.y + small.box.h > bounds.h, overlay_close(&h),
                                 "and should still be inside its region on the way");

    /* And one that fills the region has nowhere to travel. */
    overlay_frame(&h, 1U);
    desc.id = OVERLAY_ID_B;
    desc.h = (int)OVERLAY_H;
    struct inkcell_overlay_frame full;
    (void)overlay_show(&h, &desc, &full);
    INKCELL_TEST_FAIL_IF_CLEANUP(full.box.y != full.rest.y, overlay_close(&h),
                                 "a layer the size of its region arrives where it belongs");

    overlay_close(&h);
    record_success(test_name);
}

/* ---- the stack ------------------------------------------------------------------------------- */

/*
 * Three modals stacked, and the last one drawn owns the press.
 *
 * The one fact on the frame that no pixel states. A screen holds one `if` against this instead
 * of a priority order it keeps by hand and has to remember to update when it grows a fourth
 * overlay.
 */
INKCELL_TEST_CASE(overlay_modal_is_the_last_one_drawn, unit) {
    struct overlay_harness h;
    INKCELL_TEST_FAIL_IF(!overlay_open(&h), "capture should open");

    struct inkcell_overlay sheet = overlay_sheet(OVERLAY_ID_A, true);
    sheet.modal = true;
    struct inkcell_overlay menu = overlay_sheet(OVERLAY_ID_B, true);
    menu.modal = true;
    /* Not modal: a notice is not a question, so nothing about it is waiting for a press - and
       drawing it last must not make it the answer. */
    struct inkcell_overlay toast = overlay_sheet(OVERLAY_ID_C, true);

    (void)overlay_show(&h, &sheet, NULL);
    (void)overlay_show(&h, &menu, NULL);
    (void)overlay_show(&h, &toast, NULL);
    INKCELL_TEST_FAIL_IF_CLEANUP(inkcell_fb_overlay_modal(h.state) != OVERLAY_ID_B,
                                 overlay_close(&h), "the last modal drawn should own the press");

    /* The menu is dismissed. It is still on the panel for the length of its exit, and it no
       longer owns anything: a reader pressing B as a menu closes means the screen behind it. */
    overlay_frame(&h, 1U);
    menu.up = false;
    (void)overlay_show(&h, &sheet, NULL);
    (void)overlay_show(&h, &menu, NULL);
    (void)overlay_show(&h, &toast, NULL);
    INKCELL_TEST_FAIL_IF_CLEANUP(!inkcell_fb_overlay_showing(h.state, OVERLAY_ID_B),
                                 overlay_close(&h), "the menu should still be on the panel");
    INKCELL_TEST_FAIL_IF_CLEANUP(inkcell_fb_overlay_modal(h.state) != OVERLAY_ID_A,
                                 overlay_close(&h),
                                 "a layer on its way out should not own the press");

    overlay_close(&h);
    record_success(test_name);
}

/*
 * A fifth layer is refused rather than evicting one of the four.
 *
 * The opposite of the animation table's rule one header over, and the difference is what the
 * two hold. A table entry is a scalar a widget left behind, and forgetting the one off screen
 * longest costs a slide nobody is watching. A layer is a panel the reader is looking at and
 * pressing buttons against; taking its slot away would leave it on screen with nothing able to
 * dismiss it.
 */
INKCELL_TEST_CASE(overlay_stack_refuses_rather_than_evicts, unit) {
    struct overlay_harness h;
    INKCELL_TEST_FAIL_IF(!overlay_open(&h), "capture should open");

    const uint32_t ids[] = {OVERLAY_ID_A, OVERLAY_ID_B, OVERLAY_ID_C, OVERLAY_ID_D, OVERLAY_ID_E};
    uint32_t shown = 0U;
    for (uint32_t i = 0U; i < INKCELL_OVERLAY_SLOTS + 1U; ++i) {
        const struct inkcell_overlay desc = overlay_sheet(ids[i], true);
        if (overlay_show(&h, &desc, NULL)) {
            ++shown;
        }
    }
    INKCELL_TEST_FAIL_IF_CLEANUP(shown != INKCELL_OVERLAY_SLOTS, overlay_close(&h),
                                 "the stack should hold exactly its depth");
    INKCELL_TEST_FAIL_IF_CLEANUP(!inkcell_fb_overlay_showing(h.state, OVERLAY_ID_A),
                                 overlay_close(&h),
                                 "the first layer should not have been evicted for the fifth");

    overlay_close(&h);
    record_success(test_name);
}

/*
 * A layer whose clip cannot be pushed is not drawn at all.
 *
 * Unclipped is not a degraded version of clipped. The call promises its caller that what it
 * draws stays inside its region, and a sheet arriving without that would paint its way up
 * across the app bar and the tab strip - so the only other honest answer is to say there is
 * nothing of it on the panel. The refusal has to come *before* the scrim, too, or the frame
 * would be dimmed with no panel on it to explain why.
 */
INKCELL_TEST_CASE(overlay_refused_when_the_view_stack_is_full, unit) {
    struct overlay_harness h;
    INKCELL_TEST_FAIL_IF(!overlay_open(&h), "capture should open");

    const struct inkcell_fb_rect whole = {0, 0, (int)OVERLAY_W, (int)OVERLAY_H};
    uint32_t pushed = 0U;
    while (pushed < INKCELL_FB_VIEW_DEPTH && inkcell_fb_view_push(h.state, whole, 0, 0)) {
        ++pushed;
    }

    struct inkcell_overlay desc = overlay_sheet(OVERLAY_ID_A, true);
    desc.scrim = true;
    const bool shown = overlay_show(&h, &desc, NULL);
    for (uint32_t i = 0U; i < pushed; ++i) {
        inkcell_fb_view_pop(h.state);
    }

    INKCELL_TEST_FAIL_IF_CLEANUP(pushed != INKCELL_FB_VIEW_DEPTH, overlay_close(&h),
                                 "the views should have filled the stack");
    INKCELL_TEST_FAIL_IF_CLEANUP(shown, overlay_close(&h),
                                 "a layer with nowhere to clip should not be drawn");

    overlay_close(&h);
    record_success(test_name);
}

/*
 * A layer's position is declared as damage before the *next* frame draws anything.
 *
 * The bug this catches took a round of review to see, and it is one the frame's own code
 * already had a paragraph about: a layer is drawn over the body, so the body under it is
 * drawn first, and a layer that declared its old position when it drew would be declaring it
 * after everything that could have repainted those rows was told they had not changed. Under
 * a clip band the vacated rows are then never repainted - the layer leaves a trail - and the
 * scrim, which reads the panel back and writes it darker, dims the same pixels a second time
 * for every frame the modal is up.
 */
INKCELL_TEST_CASE(overlay_damage_is_declared_before_the_next_frame, unit) {
    struct overlay_harness h;
    INKCELL_TEST_FAIL_IF(!overlay_open(&h), "capture should open");

    struct inkcell_overlay desc = overlay_sheet(OVERLAY_ID_A, true);
    desc.scrim = true;
    desc.bounds = (struct inkcell_fb_rect){0, 20, (int)OVERLAY_W, 200};
    (void)overlay_show(&h, &desc, NULL);

    /* The next frame, before a single pixel of it is drawn. */
    overlay_frame(&h, 16U);
    const struct inkcell_fb_damage_rect damage = h.state->animation_damage;

    INKCELL_TEST_FAIL_IF_CLEANUP(!damage.valid, overlay_close(&h),
                                 "the frame should open owing a repaint");
    /* The scrim's whole region, not just the panel's box: a region dimmed last frame and not
       repainted this one would be dimmed again. */
    INKCELL_TEST_FAIL_IF_CLEANUP(
        damage.y > desc.bounds.y || damage.bottom < desc.bounds.y + desc.bounds.h,
        overlay_close(&h), "and it should cover the whole of what the scrim touched");

    overlay_close(&h);
    record_success(test_name);
}

/*
 * A layer dropped comes back from nothing rather than from where it was.
 *
 * What the snackbar needs and what a re-aim cannot express: a second notice reading the same
 * words as the first is a second arrival, and a layer told to go where it already is does
 * nothing at all - deliberately, because that no-op is what stops every settled widget
 * animating on every frame.
 */
INKCELL_TEST_CASE(overlay_drop_makes_the_next_one_arrive, unit) {
    struct overlay_harness h;
    INKCELL_TEST_FAIL_IF(!overlay_open(&h), "capture should open");

    const struct inkcell_overlay desc = overlay_sheet(OVERLAY_ID_A, true);
    (void)overlay_show(&h, &desc, NULL);
    overlay_frame(&h, overlay_in_ms(&h));
    struct inkcell_overlay_frame settled;
    (void)overlay_show(&h, &desc, &settled);
    INKCELL_TEST_FAIL_IF_CLEANUP(settled.progress != INKCELL_ANIM_ONE, overlay_close(&h),
                                 "it should have arrived");

    overlay_frame(&h, 1U);
    inkcell_fb_overlay_drop(h.state, OVERLAY_ID_A);
    struct inkcell_overlay_frame again;
    (void)overlay_show(&h, &desc, &again);
    INKCELL_TEST_FAIL_IF_CLEANUP(again.progress != 0, overlay_close(&h),
                                 "a dropped layer should come back from the start");

    overlay_close(&h);
    record_success(test_name);
}

/*
 * A reset takes every layer off with no exit.
 *
 * For the change that is not a change of mind: a theme or scale swap re-measures everything,
 * so the boxes these layers are travelling between describe a geometry that no longer exists.
 */
INKCELL_TEST_CASE(overlay_reset_clears_the_frame, unit) {
    struct overlay_harness h;
    INKCELL_TEST_FAIL_IF(!overlay_open(&h), "capture should open");

    const struct inkcell_overlay a = overlay_sheet(OVERLAY_ID_A, true);
    const struct inkcell_overlay b = overlay_sheet(OVERLAY_ID_B, true);
    (void)overlay_show(&h, &a, NULL);
    (void)overlay_show(&h, &b, NULL);
    inkcell_fb_overlay_reset(h.state);

    INKCELL_TEST_FAIL_IF_CLEANUP(inkcell_fb_overlay_showing(h.state, OVERLAY_ID_A) ||
                                     inkcell_fb_overlay_showing(h.state, OVERLAY_ID_B),
                                 overlay_close(&h), "a reset should leave nothing on the frame");
    INKCELL_TEST_FAIL_IF_CLEANUP(inkcell_fb_overlay_modal(h.state) != INKCELL_OVERLAY_NONE,
                                 overlay_close(&h), "and nothing owning the press");

    overlay_close(&h);
    record_success(test_name);
}
