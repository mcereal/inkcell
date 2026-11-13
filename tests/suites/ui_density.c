#define _POSIX_C_SOURCE 200809L

/*
 * Density: lengths in dp, and a body scale that is a length rather than a pixel count.
 *
 * The arithmetic is pinned to a real panel - the Pixel 7, 1080 px across 66 mm, 416 ppi - so a
 * change that moves a phone's text by a pixel is a change somebody chose. The other half is the
 * promise in inkcell/ui/density.h that an unknown density draws exactly what it always did:
 * every panel that never stated its size, and the golden sheet, depend on it.
 */

#include "framework/inkcell_test.h"

#include "inkcell/ui/density.h"
#include "inkcell/ui/fb_capture.h"
#include "inkcell/ui/fb_draw.h"
#include "inkcell/ui/theme.h"

#include <stdlib.h>

static void density_test_setenv(const char *name, const char *value) {
#ifdef _WIN32
    (void)_putenv_s(name, value != NULL ? value : "");
#else
    if (value != NULL) {
        (void)setenv(name, value, 1);
    } else {
        (void)unsetenv(name);
    }
#endif
}

INKCELL_TEST_CASE(ui_density_dp_to_pixels, unit) {
    INKCELL_TEST_FAIL_IF(inkcell_dp_px(16, 0U) != 16, "unknown density is not one pixel a dp");
    INKCELL_TEST_FAIL_IF(inkcell_dp_px(16, INKCELL_DP_PPI) != 16,
                         "the baseline density is not one pixel a dp");
    INKCELL_TEST_FAIL_IF(inkcell_dp_px(16, 416U) != 42, "16 dp on a Pixel 7 is not 42 px");
    INKCELL_TEST_FAIL_IF(inkcell_dp_px(48, 416U) != 125, "48 dp on a Pixel 7 is not 125 px");
    INKCELL_TEST_FAIL_IF(inkcell_dp_px(-16, 416U) != -42, "a negative length rounded differently");
    INKCELL_TEST_FAIL_IF(inkcell_dp_px(1, 60U) != 1, "a hairline rounded away to nothing");
    INKCELL_TEST_FAIL_IF(inkcell_dp_px(0, 416U) != 0, "nothing became something");
    record_success(test_name);
}

INKCELL_TEST_CASE(ui_density_from_a_panels_millimetres, unit) {
    INKCELL_TEST_FAIL_IF(inkcell_ppi_from_mm(1080U, 66U) != 416U, "a Pixel 7 is not 416 ppi");
    INKCELL_TEST_FAIL_IF(inkcell_ppi_from_mm(1920U, 478U) != 102U,
                         "a 21.5\" full-HD monitor is not 102 ppi");
    INKCELL_TEST_FAIL_IF(inkcell_ppi_from_mm(1080U, 0U) != 0U, "no millimetres gave a density");
    INKCELL_TEST_FAIL_IF(inkcell_ppi_from_mm(0U, 66U) != 0U, "no pixels gave a density");
    /* EDIDs that state the aspect ratio for a size, and a wall-sized one. */
    INKCELL_TEST_FAIL_IF(inkcell_ppi_from_mm(1920U, 16U) != 0U, "an aspect ratio was believed");
    INKCELL_TEST_FAIL_IF(inkcell_ppi_from_mm(1920U, 2000U) != 0U, "a 24 ppi panel was believed");
    record_success(test_name);
}

INKCELL_TEST_CASE(ui_density_scale_for_a_length, unit) {
    /* An 8-step cell at 16 dp: 41.6 px on a Pixel 7, which is 20.8 units - 21. */
    INKCELL_TEST_FAIL_IF(inkcell_scale_for_dp(16, 416U, 8U) != 21,
                         "16 dp body on a Pixel 7 is not scale 21");
    INKCELL_TEST_FAIL_IF(inkcell_scale_for_dp(16, INKCELL_DP_PPI, 8U) != INKCELL_SCALE(2),
                         "16 dp body at the baseline is not two whole steps");
    INKCELL_TEST_FAIL_IF(inkcell_scale_for_dp(16, 0U, 8U) != 0,
                         "an unknown density chose a scale instead of deferring to the theme");
    INKCELL_TEST_FAIL_IF(inkcell_scale_for_dp(0, 416U, 8U) != 0, "no length chose a scale");
    record_success(test_name);
}

INKCELL_TEST_CASE(ui_density_drives_the_body_scale, unit) {
    density_test_setenv("INKWELL_FB_PPI", NULL);
    struct inkcell_capture *capture = NULL;
    INKCELL_TEST_FAIL_IF(inkcell_capture_open(&capture, 320U, 240U, 0) != 0, "no capture");
    struct inkcell_draw_state *state = inkcell_capture_state(capture);
    const struct inkcell_theme *theme = state->theme;
    const int theme_scale = inkcell_theme_scale(theme);
    const int theme_margin = (int)theme->metrics.margin;

    INKCELL_TEST_FAIL_IF_CLEANUP(state->ppi != 0U || state->scale != theme_scale,
                                 inkcell_capture_close(capture),
                                 "a panel of unknown density did not draw at the theme's scale");
    INKCELL_TEST_FAIL_IF_CLEANUP(inkcell_fb_margin(state) != theme_margin,
                                 inkcell_capture_close(capture),
                                 "the margin moved at an unknown density");

    inkcell_fb_state_set_ppi(state, 416U);
    const int want =
        inkcell_scale_for_dp(theme->metrics.body_dp, 416U, inkcell_fb_font(state)->height);
    INKCELL_TEST_FAIL_IF_CLEANUP(state->scale != inkcell_theme_clamp_scale(theme, want),
                                 inkcell_capture_close(capture),
                                 "the body scale did not follow the density");
    INKCELL_TEST_FAIL_IF_CLEANUP(inkcell_fb_margin(state) != inkcell_dp_px(theme_margin, 416U),
                                 inkcell_capture_close(capture),
                                 "the margin is not a length at a known density");
    INKCELL_TEST_FAIL_IF_CLEANUP(inkcell_fb_dp(state, 48) != 125, inkcell_capture_close(capture),
                                 "inkcell_fb_dp() is not the panel's density");

    /* A theme switch keeps the density: the new theme's body, measured the same way. */
    inkcell_fb_state_set_theme(state, inkcell_theme_by_id("light"), 0);
    INKCELL_TEST_FAIL_IF_CLEANUP(state->scale != inkcell_theme_clamp_scale(state->theme, want),
                                 inkcell_capture_close(capture),
                                 "a theme switch dropped the density's scale");

    inkcell_fb_state_set_ppi(state, 0U);
    INKCELL_TEST_FAIL_IF_CLEANUP(state->scale != inkcell_theme_scale(state->theme),
                                 inkcell_capture_close(capture),
                                 "forgetting the density did not restore the theme's scale");
    inkcell_capture_close(capture);
    record_success(test_name);
}

INKCELL_TEST_CASE(ui_density_pins, unit) {
    /* A scale somebody named outlives a density arriving. */
    density_test_setenv("INKWELL_FB_PPI", NULL);
    struct inkcell_capture *capture = NULL;
    INKCELL_TEST_FAIL_IF(inkcell_capture_open(&capture, 320U, 240U, INKCELL_SCALE(3)) != 0,
                         "no capture");
    struct inkcell_draw_state *state = inkcell_capture_state(capture);
    inkcell_fb_state_set_ppi(state, 416U);
    INKCELL_TEST_FAIL_IF_CLEANUP(state->scale != INKCELL_SCALE(3), inkcell_capture_close(capture),
                                 "a density overrode a pinned scale");
    INKCELL_TEST_FAIL_IF_CLEANUP(inkcell_fb_dp(state, 16) != 42, inkcell_capture_close(capture),
                                 "a pinned scale stopped dp being a length");
    inkcell_capture_close(capture);

    /* A density somebody named outlives what the panel says about itself. */
    density_test_setenv("INKWELL_FB_PPI", "300");
    INKCELL_TEST_FAIL_IF(inkcell_capture_open(&capture, 320U, 240U, 0) != 0, "no capture");
    state = inkcell_capture_state(capture);
    inkcell_fb_state_apply_theme_from_env(state);
    inkcell_fb_state_set_ppi(state, 416U);
    density_test_setenv("INKWELL_FB_PPI", NULL);
    INKCELL_TEST_FAIL_IF_CLEANUP(state->ppi != 300U, inkcell_capture_close(capture),
                                 "the panel's report overrode <PREFIX>_FB_PPI");
    inkcell_capture_close(capture);
    record_success(test_name);
}
