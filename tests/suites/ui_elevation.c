#define _POSIX_C_SOURCE 200809L

/*
 * The opacity and elevation tokens, and the shadow they are drawn with.
 *
 * Three kinds of promise, and each is checked where it can be seen:
 *
 *   - **The tokens are the theme's.** The state layers, the disabled fade and the scrim read
 *     one table, a theme that moves a figure moves what is drawn, and the defaults are the
 *     constants they replaced - so nothing already on a panel changed colour when they became
 *     tokens.
 *   - **A shadow is where it says.** Below the box by the offset, fading with distance, gone
 *     past the bounds it reports, and absent altogether on a flat level or a flat theme.
 *   - **A shadow never darkens what was not repainted.** It is a read-modify-write, so it keeps
 *     to the clip band even where declared damage lets every other primitive through - the one
 *     rule that, broken, makes a screen darker every frame.
 */

#include "framework/inkcell_test.h"

#include "inkcell/ui/fb_capture.h"
#include "inkcell/ui/fb_draw.h"
#include "inkcell/ui/overlay.h"
#include "inkcell/ui/theme.h"

#include <stdint.h>

#define ELEVATION_W 240U
#define ELEVATION_H 240U

static const struct inkcell_rgb k_grey = {200U, 200U, 200U};

struct elevation_page {
    struct inkcell_capture *capture;
    struct inkcell_draw_state *state;
    const uint8_t *pixels;
    size_t stride;
};

/* A page on the default theme at the device's scale, cleared to a mid-light grey: light enough
   that a shadow towards black shows, and not white, so a stray lightening would too. */
static bool elevation_open(struct elevation_page *page) {
    page->capture = NULL;
    if (inkcell_capture_open(&page->capture, ELEVATION_W, ELEVATION_H, INKCELL_SCALE(4)) < 0) {
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
    inkcell_fb_clear(page->state, k_grey);
    return true;
}

/* The red channel: the default theme's shadow is black and the ground is grey, so one channel
   says everything. */
static int elevation_at(const struct elevation_page *page, int x, int y) {
    return page->pixels[(size_t)y * page->stride + (size_t)x * 4U + 2U];
}

/* ---- the tokens ------------------------------------------------------------------------------ */

INKCELL_TEST_CASE(elevation_opacity_defaults_are_the_constants_they_replaced, unit) {
    const struct inkcell_theme *theme = inkcell_theme_default();
    INKCELL_TEST_FAIL_IF(inkcell_theme_opacity(theme, INKCELL_OPACITY_HOVER) != 8,
                         "the hover layer moved off 8%");
    INKCELL_TEST_FAIL_IF(inkcell_theme_opacity(theme, INKCELL_OPACITY_FOCUS) != 12,
                         "the focus layer moved off 12%");
    INKCELL_TEST_FAIL_IF(inkcell_theme_opacity(theme, INKCELL_OPACITY_PRESS) != 20,
                         "the press layer moved off 20%");
    INKCELL_TEST_FAIL_IF(inkcell_theme_opacity(theme, INKCELL_OPACITY_DISABLED) != 38,
                         "the disabled fade moved off Material's 38%");
    INKCELL_TEST_FAIL_IF(inkcell_theme_opacity(theme, INKCELL_OPACITY_SCRIM) != 32,
                         "the scrim moved off Material's 32%");
    INKCELL_TEST_FAIL_IF(inkcell_theme_opacity(theme, INKCELL_OPACITY_COUNT) != 0,
                         "an opacity past the end read something");

    /* The theme-less forms are the default theme's, exactly. */
    const struct inkcell_rgb fill = {40U, 44U, 52U};
    const struct inkcell_rgb ink = {230U, 232U, 236U};
    for (int s = INKCELL_STATE_REST; s < INKCELL_STATE_COUNT; ++s) {
        const struct inkcell_rgb a = inkcell_theme_state_layer(fill, ink, (enum inkcell_state)s);
        const struct inkcell_rgb b =
            inkcell_theme_state_layer_for(theme, fill, ink, (enum inkcell_state)s);
        INKCELL_TEST_FAIL_IF(a.r != b.r || a.g != b.g || a.b != b.b,
                             "the theme-less state layer is not the default theme's");
    }
    const struct inkcell_rgb a = inkcell_theme_disabled_ink(fill, ink);
    const struct inkcell_rgb b = inkcell_theme_disabled_ink_for(theme, fill, ink);
    INKCELL_TEST_FAIL_IF(a.r != b.r || a.g != b.g || a.b != b.b,
                         "the theme-less disabled fade is not the default theme's");
    record_success(test_name);
}

/*
 * The reason the figures became tokens: a theme that asks for a heavier focus layer gets one,
 * and so does every widget that paints through inkcell_theme_paint().
 */
INKCELL_TEST_CASE(elevation_state_layer_follows_the_theme, unit) {
    struct inkcell_theme heavy = *inkcell_theme_default();
    heavy.metrics.opacity[INKCELL_OPACITY_FOCUS] = 40U;

    const struct inkcell_rgb fill = {40U, 40U, 40U};
    const struct inkcell_rgb ink = {240U, 240U, 240U};
    const struct inkcell_rgb quiet =
        inkcell_theme_state_layer_for(inkcell_theme_default(), fill, ink, INKCELL_STATE_FOCUSED);
    const struct inkcell_rgb loud =
        inkcell_theme_state_layer_for(&heavy, fill, ink, INKCELL_STATE_FOCUSED);
    INKCELL_TEST_FAIL_IF(loud.r != 120U, "40% of the way from 40 to 240 is 120");
    INKCELL_TEST_FAIL_IF(loud.r <= quiet.r, "a heavier focus opacity did not lift further");

    const struct inkcell_paint rest =
        inkcell_theme_paint(inkcell_theme_default(), INKCELL_FAMILY_PRIMARY, INKCELL_SLOT_CONTAINER,
                            INKCELL_STATE_FOCUSED);
    const struct inkcell_paint lifted = inkcell_theme_paint(
        &heavy, INKCELL_FAMILY_PRIMARY, INKCELL_SLOT_CONTAINER, INKCELL_STATE_FOCUSED);
    INKCELL_TEST_FAIL_IF(rest.fill.r == lifted.fill.r && rest.fill.g == lifted.fill.g &&
                             rest.fill.b == lifted.fill.b,
                         "a family container ignored the theme's focus opacity");
    record_success(test_name);
}

INKCELL_TEST_CASE(elevation_mix_is_exact_at_its_ends, unit) {
    const struct inkcell_rgb from = {10U, 100U, 250U};
    const struct inkcell_rgb to = {250U, 100U, 10U};
    const struct inkcell_rgb none = inkcell_theme_mix(from, to, 0);
    const struct inkcell_rgb all = inkcell_theme_mix(from, to, 100);
    const struct inkcell_rgb past = inkcell_theme_mix(from, to, 400);
    INKCELL_TEST_FAIL_IF(none.r != 10U || none.b != 250U, "0% moved the colour");
    INKCELL_TEST_FAIL_IF(all.r != 250U || all.b != 10U, "100% did not arrive");
    INKCELL_TEST_FAIL_IF(past.r != 250U || past.b != 10U, "past 100% overshot");
    INKCELL_TEST_FAIL_IF(all.g != 100U, "a channel that does not differ moved");

    /* One percent of a four-step difference rounds to nothing, and moves one step anyway. */
    const struct inkcell_rgb near = inkcell_theme_mix((struct inkcell_rgb){100U, 100U, 100U},
                                                      (struct inkcell_rgb){104U, 96U, 100U}, 1);
    INKCELL_TEST_FAIL_IF(near.r != 101U || near.g != 99U || near.b != 100U,
                         "a small mix rounded away to no mark at all");
    record_success(test_name);
}

INKCELL_TEST_CASE(elevation_shadows_rise_and_scale, unit) {
    for (size_t i = 0; i < inkcell_theme_count(); ++i) {
        const struct inkcell_theme *theme = inkcell_theme_at(i);
        const int scale = inkcell_theme_scale(theme);

        const struct inkcell_shadow_px flat =
            inkcell_theme_shadow(theme, INKCELL_ELEVATION_FLAT, scale);
        INKCELL_TEST_FAIL_IF(flat.depth != 0 || flat.blur != 0 || flat.offset != 0,
                             "a flat level casts a shadow");

        struct inkcell_shadow_px below = flat;
        for (int e = INKCELL_ELEVATION_RAISED; e < INKCELL_ELEVATION_COUNT; ++e) {
            const struct inkcell_shadow_px level =
                inkcell_theme_shadow(theme, (enum inkcell_elevation)e, scale);
            INKCELL_TEST_FAIL_IF(level.depth < below.depth || level.blur < below.blur ||
                                     level.offset < below.offset,
                                 "a higher level casts less shadow than the one under it");
            INKCELL_TEST_FAIL_IF(level.depth > 100, "a shadow is more than all the way dark");
            INKCELL_TEST_FAIL_IF(level.depth > 0 && level.blur < 1,
                                 "a shadow with depth has no width to fade over");
            below = level;
        }

        /* Glyph-relative, like the spacing scale: a theme drawing bigger casts further. */
        const struct inkcell_shadow_px small =
            inkcell_theme_shadow(theme, INKCELL_ELEVATION_MODAL, INKCELL_SCALE_MIN);
        const struct inkcell_shadow_px large =
            inkcell_theme_shadow(theme, INKCELL_ELEVATION_MODAL, INKCELL_SCALE_MAX);
        INKCELL_TEST_FAIL_IF(large.blur < small.blur, "a shadow shrank as the scale grew");
    }

    /* The contrast theme is flat all the way up: its edges are its elevation. */
    const struct inkcell_theme *contrast = inkcell_theme_by_id("contrast");
    INKCELL_TEST_FAIL_IF(contrast == NULL, "the contrast theme should exist");
    INKCELL_TEST_FAIL_IF(
        inkcell_theme_shadow(contrast, INKCELL_ELEVATION_MODAL, INKCELL_SCALE(4)).depth != 0,
        "the contrast theme casts a shadow");

    INKCELL_TEST_FAIL_IF(inkcell_theme_shadow(NULL, INKCELL_ELEVATION_COUNT, 4).depth != 0,
                         "an elevation past the end read something");
    record_success(test_name);
}

INKCELL_TEST_CASE(elevation_validate_rejects_what_is_not_a_scale, unit) {
    char reason[128];

    struct inkcell_theme opaque = *inkcell_theme_default();
    opaque.metrics.opacity[INKCELL_OPACITY_PRESS] = 101U;
    reason[0] = '\0';
    INKCELL_TEST_FAIL_IF(inkcell_theme_validate(&opaque, reason, sizeof reason),
                         "an opacity past 100% passed validation");
    INKCELL_TEST_FAIL_IF(reason[0] == '\0', "validation failed without saying why");

    /* Hover is never contrast-checked itself; the order is what keeps it under focus, which is. */
    struct inkcell_theme loud_hover = *inkcell_theme_default();
    loud_hover.metrics.opacity[INKCELL_OPACITY_HOVER] = 100U;
    INKCELL_TEST_FAIL_IF(inkcell_theme_validate(&loud_hover, reason, sizeof reason),
                         "a hover layer heavier than focus passed validation");

    struct inkcell_theme deep = *inkcell_theme_default();
    deep.metrics.shadow[INKCELL_ELEVATION_MODAL].depth = 101U;
    INKCELL_TEST_FAIL_IF(inkcell_theme_validate(&deep, reason, sizeof reason),
                         "a shadow past 100% passed validation");

    /* The mistake a table edited one row at a time makes: a dialog that casts less than a card
       reads as sitting underneath it. */
    struct inkcell_theme inverted = *inkcell_theme_default();
    inverted.metrics.shadow[INKCELL_ELEVATION_MODAL] =
        inverted.metrics.shadow[INKCELL_ELEVATION_RAISED];
    inverted.metrics.shadow[INKCELL_ELEVATION_MODAL].blur = 1U;
    INKCELL_TEST_FAIL_IF(inkcell_theme_validate(&inverted, reason, sizeof reason),
                         "a modal casting less than a raised card passed validation");
    record_success(test_name);
}

/* ---- the shadow ------------------------------------------------------------------------------ */

INKCELL_TEST_CASE(elevation_shadow_falls_below_and_fades, unit) {
    struct elevation_page page;
    INKCELL_TEST_FAIL_IF(!elevation_open(&page), "the capture should open");

    const struct inkcell_fb_rect box = {60, 60, 100, 60};
    inkcell_fb_draw_shadow(page.state, box, 8, INKCELL_ELEVATION_MODAL, INKCELL_ANIM_ONE);
    const struct inkcell_fb_rect bounds =
        inkcell_fb_shadow_bounds(page.state, box, INKCELL_ELEVATION_MODAL);
    const int mid = box.x + box.w / 2;
    const int below = box.y + box.h; /* the first row under the box */

    /* Below the box it is dark, and it lightens with distance. */
    INKCELL_TEST_FAIL_IF_CLEANUP(elevation_at(&page, mid, below) >= 200,
                                 inkcell_capture_close(page.capture),
                                 "nothing darkened under the box");
    int previous = elevation_at(&page, mid, below);
    for (int y = below + 1; y < bounds.y + bounds.h; ++y) {
        const int value = elevation_at(&page, mid, y);
        INKCELL_TEST_FAIL_IF_CLEANUP(value < previous, inkcell_capture_close(page.capture),
                                     "the shadow got darker further from the box");
        previous = value;
    }

    /* It falls downward: the row under the box is darker than the row over it. */
    INKCELL_TEST_FAIL_IF_CLEANUP(
        elevation_at(&page, mid, below) >= elevation_at(&page, mid, box.y - 1),
        inkcell_capture_close(page.capture), "the shadow is not offset below the box");

    /* Left and right are mirror images, since the light is straight above. */
    const int row = box.y + box.h / 2;
    for (int d = 1; d <= 6; ++d) {
        INKCELL_TEST_FAIL_IF_CLEANUP(
            elevation_at(&page, box.x - d, row) != elevation_at(&page, box.x + box.w - 1 + d, row),
            inkcell_capture_close(page.capture), "the two sides of the shadow differ");
    }

    /* Nothing past the bounds it reported, and the plain middle of the box is left for the fill
       that is about to cover it. */
    INKCELL_TEST_FAIL_IF_CLEANUP(elevation_at(&page, mid, bounds.y + bounds.h) != 200,
                                 inkcell_capture_close(page.capture),
                                 "the shadow reached past its own bounds");
    INKCELL_TEST_FAIL_IF_CLEANUP(elevation_at(&page, bounds.x - 1, row) != 200,
                                 inkcell_capture_close(page.capture),
                                 "the shadow reached past its own left edge");
    INKCELL_TEST_FAIL_IF_CLEANUP(elevation_at(&page, mid, row) != 200,
                                 inkcell_capture_close(page.capture),
                                 "the shadow spent time under the box it will not show through");

    inkcell_capture_close(page.capture);
    record_success(test_name);
}

INKCELL_TEST_CASE(elevation_shadow_draws_nothing_when_flat, unit) {
    struct elevation_page page;
    INKCELL_TEST_FAIL_IF(!elevation_open(&page), "the capture should open");

    const struct inkcell_fb_rect box = {60, 60, 100, 60};
    inkcell_fb_draw_shadow(page.state, box, 8, INKCELL_ELEVATION_FLAT, INKCELL_ANIM_ONE);
    inkcell_fb_draw_shadow(page.state, box, 8, INKCELL_ELEVATION_MODAL, 0);
    for (int y = 0; y < (int)ELEVATION_H; y += 3) {
        for (int x = 0; x < (int)ELEVATION_W; x += 3) {
            INKCELL_TEST_FAIL_IF_CLEANUP(elevation_at(&page, x, y) != 200,
                                         inkcell_capture_close(page.capture),
                                         "a flat or un-arrived shadow touched the page");
        }
    }
    const struct inkcell_fb_rect same =
        inkcell_fb_shadow_bounds(page.state, box, INKCELL_ELEVATION_FLAT);
    INKCELL_TEST_FAIL_IF_CLEANUP(
        same.x != box.x || same.y != box.y || same.w != box.w || same.h != box.h,
        inkcell_capture_close(page.capture), "a flat level reported bounds past its box");

    /* And a theme that states no shadows at all draws none at any level. */
    inkcell_fb_state_set_theme(page.state, inkcell_theme_by_id("contrast"), INKCELL_SCALE(4));
    inkcell_fb_draw_shadow(page.state, box, 8, INKCELL_ELEVATION_MODAL, INKCELL_ANIM_ONE);
    INKCELL_TEST_FAIL_IF_CLEANUP(elevation_at(&page, box.x + box.w / 2, box.y + box.h) != 200,
                                 inkcell_capture_close(page.capture),
                                 "a theme with no shadows cast one");

    inkcell_capture_close(page.capture);
    record_success(test_name);
}

/*
 * The rule that matters most, because breaking it is invisible on one frame and ruinous over a
 * hundred: a shadow darkens what is there, so a pixel it touches twice without a repaint is
 * darker the second time. Declared animation damage lets every other primitive past the band;
 * it must not let this one, because nothing promised the ground outside the band was redrawn.
 */
INKCELL_TEST_CASE(elevation_shadow_keeps_to_the_band_inside_declared_damage, unit) {
    struct elevation_page page;
    INKCELL_TEST_FAIL_IF(!elevation_open(&page), "the capture should open");

    const struct inkcell_fb_rect box = {60, 60, 100, 60};
    const struct inkcell_fb_rect bounds =
        inkcell_fb_shadow_bounds(page.state, box, INKCELL_ELEVATION_MODAL);
    /* The band stops half way down the box; the damage covers everything. */
    page.state->clip_active = true;
    page.state->clip = (struct inkcell_fb_damage_rect){0, 0, (int)ELEVATION_W, 90, true};
    inkcell_fb_animation_damage(page.state, 0, 0, (int)ELEVATION_W, (int)ELEVATION_H);

    for (int frame = 0; frame < 3; ++frame) {
        inkcell_fb_draw_shadow(page.state, box, 8, INKCELL_ELEVATION_MODAL, INKCELL_ANIM_ONE);
    }
    /* Beside the box and under it, every row past the band is the ground it started as. */
    for (int y = 90; y < bounds.y + bounds.h; ++y) {
        INKCELL_TEST_FAIL_IF_CLEANUP(elevation_at(&page, box.x - 2, y) != 200,
                                     inkcell_capture_close(page.capture),
                                     "the shadow darkened rows beside the box outside the band");
    }
    for (int y = box.y + box.h; y < bounds.y + bounds.h; ++y) {
        INKCELL_TEST_FAIL_IF_CLEANUP(elevation_at(&page, box.x + box.w / 2, y) != 200,
                                     inkcell_capture_close(page.capture),
                                     "the shadow darkened rows under the box outside the band");
    }
    /* And inside the band it did draw - the clip is a limit, not a refusal. */
    INKCELL_TEST_FAIL_IF_CLEANUP(elevation_at(&page, box.x - 2, 80) >= 200,
                                 inkcell_capture_close(page.capture),
                                 "the shadow drew nothing inside the band");

    inkcell_capture_close(page.capture);
    record_success(test_name);
}

/*
 * The overlay layer's half of that bargain: the shadow's reach is in the span it carries to the
 * next frame, so the rows it darkened are repainted before it is drawn again.
 */
INKCELL_TEST_CASE(elevation_overlay_span_covers_its_shadow, unit) {
    struct inkcell_capture *capture = NULL;
    INKCELL_TEST_FAIL_IF(inkcell_capture_open(&capture, 400U, 300U, INKCELL_SCALE(4)) < 0,
                         "the capture should open");
    struct inkcell_draw_state *state = inkcell_capture_state(capture);
    uint64_t now = 1000U;
    inkcell_fb_state_set_now(state, now);

    const struct inkcell_overlay desc = {
        .id = 7U,
        .up = true,
        .placement = INKCELL_OVERLAY_CENTER,
        .travel = INKCELL_OVERLAY_TRAVEL_NEAR,
        .w = 200,
        .h = 100,
        .elevation = INKCELL_ELEVATION_MODAL,
        .shape = INKCELL_SHAPE_LG,
    };
    struct inkcell_overlay_frame frame;
    struct inkcell_fb_rect box = {0, 0, 0, 0};
    /* Long enough to have arrived, so the box is where it rests. */
    for (int i = 0; i < 40; ++i) {
        now += 16U;
        inkcell_fb_state_set_now(state, now);
        inkcell_fb_app_frame_begin(state);
        if (inkcell_fb_overlay_begin(state, &desc, &frame)) {
            box = frame.box;
            inkcell_fb_overlay_end(state, &frame);
        }
    }
    INKCELL_TEST_FAIL_IF_CLEANUP(box.w <= 0, inkcell_capture_close(capture),
                                 "the layer should have drawn");

    inkcell_fb_app_frame_begin(state);
    const struct inkcell_fb_damage_rect damage = state->animation_damage;
    const struct inkcell_fb_rect reach =
        inkcell_fb_shadow_bounds(state, box, INKCELL_ELEVATION_MODAL);
    INKCELL_TEST_FAIL_IF_CLEANUP(reach.y + reach.h <= box.y + box.h, inkcell_capture_close(capture),
                                 "a modal on the default theme should cast below its box");
    INKCELL_TEST_FAIL_IF_CLEANUP(!damage.valid || damage.bottom < reach.y + reach.h ||
                                     damage.x > reach.x || damage.right < reach.x + reach.w,
                                 inkcell_capture_close(capture),
                                 "the next frame does not owe a repaint of the whole shadow");

    inkcell_capture_close(capture);
    record_success(test_name);
}

/*
 * And while it travels: the band a frame is drawn under is what the frame before declared, and
 * a layer on its way in has moved since. The shadow is cut to the band, so every frame of the
 * journey has to find its own shadow's reach already declared - and once the layer is at rest
 * it goes back to declaring only where it is, or a snackbar's whole path off the panel would be
 * repainted for as long as it was up.
 */
INKCELL_TEST_CASE(elevation_a_travelling_layer_declares_where_its_shadow_is_going, unit) {
    struct inkcell_capture *capture = NULL;
    INKCELL_TEST_FAIL_IF(inkcell_capture_open(&capture, 400U, 300U, INKCELL_SCALE(4)) < 0,
                         "the capture should open");
    struct inkcell_draw_state *state = inkcell_capture_state(capture);
    uint64_t now = 1000U;
    inkcell_fb_state_set_now(state, now);

    const struct inkcell_overlay desc = {
        .id = 9U,
        .up = true,
        .placement = INKCELL_OVERLAY_BOTTOM,
        .travel = INKCELL_OVERLAY_TRAVEL_OFF_PANEL,
        .w = 200,
        .h = 60,
        .bounds = {.x = 0, .y = 0, .w = 400, .h = 260},
        .elevation = INKCELL_ELEVATION_FLOATING,
        .shape = INKCELL_SHAPE_SM,
    };
    struct inkcell_overlay_frame frame;
    struct inkcell_fb_rect box = {0, 0, 0, 0};
    int moving_frames = 0;
    for (int i = 0; i < 40; ++i) {
        now += 16U;
        inkcell_fb_state_set_now(state, now);
        inkcell_fb_app_frame_begin(state);
        const struct inkcell_fb_damage_rect band = state->animation_damage;
        if (!inkcell_fb_overlay_begin(state, &desc, &frame)) {
            continue;
        }
        box = frame.box;
        inkcell_fb_overlay_end(state, &frame);
        if (i == 0) {
            continue; /* the first frame has no frame before it to have declared anything */
        }
        const struct inkcell_fb_rect reach =
            inkcell_fb_shadow_bounds(state, box, INKCELL_ELEVATION_FLOATING);
        const int top = reach.y > 0 ? reach.y : 0;
        const int bottom = reach.y + reach.h < 300 ? reach.y + reach.h : 300;
        if (bottom <= top) {
            continue; /* still wholly off the panel */
        }
        ++moving_frames;
        INKCELL_TEST_FAIL_IF_CLEANUP(!band.valid || band.y > top || band.bottom < bottom ||
                                         band.x > reach.x || band.right < reach.x + reach.w,
                                     inkcell_capture_close(capture),
                                     "a frame of the journey drew its shadow outside the band");
    }
    INKCELL_TEST_FAIL_IF_CLEANUP(moving_frames < 2, inkcell_capture_close(capture),
                                 "the layer should have been seen travelling");

    inkcell_fb_app_frame_begin(state);
    const struct inkcell_fb_damage_rect rest = state->animation_damage;
    const struct inkcell_fb_rect reach =
        inkcell_fb_shadow_bounds(state, box, INKCELL_ELEVATION_FLOATING);
    INKCELL_TEST_FAIL_IF_CLEANUP(rest.bottom > reach.y + reach.h, inkcell_capture_close(capture),
                                 "a layer at rest should stop declaring the path it came up");

    inkcell_capture_close(capture);
    record_success(test_name);
}
