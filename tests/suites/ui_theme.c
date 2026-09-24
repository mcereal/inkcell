#define _POSIX_C_SOURCE 200809L

/*
 * Themes, fonts, and the promise that switching one changes the whole frame.
 *
 * Two kinds of case live here. The first are about the tables themselves: every theme is
 * complete, names a font that exists, and is readable - inkcell_theme_validate() measures the
 * contrast rather than trusting a palette that looked fine on the monitor it was picked on.
 *
 * The second kind is the one that matters for the future theme switcher: a snapshot rendered
 * under two themes must differ, and the ground it is drawn on must be the ground the theme
 * names. That is what catches a renderer that quietly kept a colour of its own - the failure
 * mode a role-based palette exists to prevent, and one no amount of looking at the dark theme
 * would reveal.
 */

#include "framework/inkcell_test.h"

#include "inkcell/ui/anim.h"
#include "inkcell/ui/fb_capture.h"
#include "inkcell/ui/font.h"
#include "inkcell/ui/theme.h"
#include "inkwell/base/env.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

INKCELL_TEST_CASE(ui_theme_registry_round_trips, unit) {
    INKCELL_TEST_FAIL_IF(inkcell_theme_count() == 0U, "no themes are registered");
    INKCELL_TEST_FAIL_IF(inkcell_theme_default() != inkcell_theme_at(0U),
                         "the default is not the first theme");

    for (size_t i = 0; i < inkcell_theme_count(); ++i) {
        const struct inkcell_theme *theme = inkcell_theme_at(i);
        INKCELL_TEST_FAIL_IF(theme == NULL, "a registered theme is NULL");
        INKCELL_TEST_FAIL_IF(inkcell_theme_by_id(theme->id) != theme,
                             "a theme does not come back under its own id");
        for (size_t j = 0; j < i; ++j) {
            INKCELL_TEST_FAIL_IF(strcmp(inkcell_theme_at(j)->id, theme->id) == 0,
                                 "two themes share an id");
        }
    }

    INKCELL_TEST_FAIL_IF(inkcell_theme_at(inkcell_theme_count()) != NULL,
                         "an index past the end returned a theme");
    INKCELL_TEST_FAIL_IF(inkcell_theme_by_id("no-such-theme") != NULL,
                         "an unknown id returned a theme");
    INKCELL_TEST_FAIL_IF(inkcell_theme_by_id(NULL) != NULL, "a NULL id returned a theme");

    /* The dark theme is the one the device has always drawn, and the id is what a saved
       preference and <PREFIX>_THEME will carry, so it is a compatibility surface. */
    INKCELL_TEST_FAIL_IF(strcmp(inkcell_theme_default()->id, "dark") != 0,
                         "the default theme is no longer 'dark'");
    record_success(test_name);
}

/* Every theme has to answer for every role and be readable on its own ground. */
INKCELL_TEST_CASE(ui_theme_tables_are_readable, unit) {
    for (size_t i = 0; i < inkcell_theme_count(); ++i) {
        const struct inkcell_theme *theme = inkcell_theme_at(i);
        char reason[128];
        if (!inkcell_theme_validate(theme, reason, sizeof reason)) {
            fprintf(stderr, "  theme '%s': %s\n", theme->id, reason);
            INKCELL_TEST_FAIL_IF(true, "a theme failed its readability contract");
        }

        /* Every tone has to resolve to a role a theme actually filled in, and the tones that
           mean opposite things must not come out the same colour - "connected" and "failed"
           telling you nothing apart is the whole failure the colour-blind theme is about. */
        const struct inkcell_rgb good = inkcell_theme_tone(theme, INKCELL_TONE_SUCCESS);
        const struct inkcell_rgb bad = inkcell_theme_tone(theme, INKCELL_TONE_ERROR);
        INKCELL_TEST_FAIL_IF(good.r == bad.r && good.g == bad.g && good.b == bad.b,
                             "a theme draws good and bad in the same colour");
    }
    record_success(test_name);
}

/*
 * The avatar palettes.
 *
 * Every theme has to offer at least one tint, answer for any seed at all, and hand back only
 * colours it actually stated - a lookup that ran off the end of a short palette would draw a
 * conversation's disc in whatever zeroed bytes follow it, which on a dark theme is a black
 * disc with black initials.
 *
 * The readability of each tint against the ground is inkcell_theme_validate()'s job, and
 * ui_theme_tables_are_readable already runs it over the whole registry.
 */
INKCELL_TEST_CASE(ui_theme_avatar_palettes, unit) {
    for (size_t i = 0; i < inkcell_theme_count(); ++i) {
        const struct inkcell_theme *theme = inkcell_theme_at(i);
        INKCELL_TEST_FAIL_IF(theme->avatar_count == 0U ||
                                 theme->avatar_count > INKCELL_AVATAR_TINTS,
                             "a theme states no avatar tints, or more than fit");

        /* Node numbers are consecutive off a vendor's block, so the seeds that matter are
           adjacent ones - and the palette has to spread them rather than hand a whole mesh the
           same colour. A theme offering more than one tint must use more than one here. */
        bool seen[INKCELL_AVATAR_TINTS];
        memset(seen, 0, sizeof seen);
        for (uint32_t seed = 0x8F21B000U; seed < 0x8F21B040U; ++seed) {
            const struct inkcell_rgb tint = inkcell_theme_avatar(theme, seed);
            bool known = false;
            for (uint8_t slot = 0; slot < theme->avatar_count; ++slot) {
                if (tint.r == theme->avatars[slot].r && tint.g == theme->avatars[slot].g &&
                    tint.b == theme->avatars[slot].b) {
                    seen[slot] = true;
                    known = true;
                }
            }
            INKCELL_TEST_FAIL_IF(!known,
                                 "an avatar seed resolved to a colour the theme never stated");
        }
        uint8_t used = 0U;
        for (uint8_t slot = 0; slot < theme->avatar_count; ++slot) {
            used = (uint8_t)(used + (seen[slot] ? 1U : 0U));
        }
        INKCELL_TEST_FAIL_IF(theme->avatar_count > 1U && used < 2U,
                             "a run of neighbouring node numbers all got the same avatar tint");

        /* The same conversation is the same colour every time, which is the whole reason the
           seed is an identity rather than a name. */
        const struct inkcell_rgb once = inkcell_theme_avatar(theme, 0x3000U);
        const struct inkcell_rgb twice = inkcell_theme_avatar(theme, 0x3000U);
        INKCELL_TEST_FAIL_IF(once.r != twice.r || once.g != twice.g || once.b != twice.b,
                             "the same seed gave two different tints");
    }

    /* A theme that states no palette at all still has to answer, because the lookup is on the
       drawing path and a NULL there would be a blank screen rather than a wrong colour. */
    struct inkcell_theme bare = *inkcell_theme_default();
    bare.avatar_count = 0U;
    const struct inkcell_rgb fallback = inkcell_theme_avatar(&bare, 7U);
    const struct inkcell_rgb accent = inkcell_theme_color(&bare, INKCELL_COLOR_PRIMARY);
    INKCELL_TEST_FAIL_IF(fallback.r != accent.r || fallback.g != accent.g || fallback.b != accent.b,
                         "a theme with no palette should fall back to the accent it already owes");
    record_success(test_name);
}

/* A pair of colours far apart is a high ratio, a pair close together is near 1, and the extreme
   is exactly 21. The renderer never calls this, but every theme is admitted by it. */
INKCELL_TEST_CASE(ui_theme_contrast_is_the_wcag_ratio, unit) {
    const struct inkcell_rgb black = {0U, 0U, 0U};
    const struct inkcell_rgb white = {255U, 255U, 255U};
    const double extreme = inkcell_theme_contrast(black, white);
    INKCELL_TEST_FAIL_IF(extreme < 20.9 || extreme > 21.1, "black on white is not 21:1");
    INKCELL_TEST_FAIL_IF(inkcell_theme_contrast(white, black) != extreme,
                         "the ratio depends on the order of its arguments");
    INKCELL_TEST_FAIL_IF(inkcell_theme_contrast(white, white) != 1.0,
                         "a colour on itself is not 1:1");
    record_success(test_name);
}

/* A theme that failed the contract has to say which pair failed, or the message is useless to
   whoever added it. Built here rather than registered, so no bad theme ships in the table. */
/*
 * The tone a level has earned - the one sentence the airtime figure, the card heading and the
 * meter under them all speak, so that a number and the picture of it cannot disagree.
 */
INKCELL_TEST_CASE(ui_theme_tone_for_load_bands, unit) {
    INKCELL_TEST_FAIL_IF(inkcell_tone_for_load(0, 250, 500) != INKCELL_TONE_SUCCESS,
                         "nothing used should be good news");
    INKCELL_TEST_FAIL_IF(inkcell_tone_for_load(249, 250, 500) != INKCELL_TONE_SUCCESS,
                         "just under the warning is still good");
    /* Both thresholds are inclusive lower bounds, which is the half of this most likely to be
       got wrong later: exactly 25% of the air is already a mesh worth looking at. */
    INKCELL_TEST_FAIL_IF(inkcell_tone_for_load(250, 250, 500) != INKCELL_TONE_WARNING,
                         "the warning threshold itself should warn");
    INKCELL_TEST_FAIL_IF(inkcell_tone_for_load(499, 250, 500) != INKCELL_TONE_WARNING,
                         "just under the bad threshold is still a warning");
    INKCELL_TEST_FAIL_IF(inkcell_tone_for_load(500, 250, 500) != INKCELL_TONE_ERROR,
                         "the bad threshold itself should be bad");
    INKCELL_TEST_FAIL_IF(inkcell_tone_for_load(1000, 250, 500) != INKCELL_TONE_ERROR,
                         "a full track is bad news");

    /* Thresholds handed over backwards must not make the middle band unreachable: a screen
       permanently in the red reads as a mesh in trouble rather than as a caller's typo. */
    INKCELL_TEST_FAIL_IF(inkcell_tone_for_load(300, 500, 250) != INKCELL_TONE_WARNING,
                         "swapped thresholds should still band the middle");

    /* Every tone it can answer with names a family, which is exactly what a meter is allowed
       to be filled with: inkcell_theme_validate() holds every family against the track and
       inkcell_fb_draw_meter() folds anything that names none back to the primary. The two are one
       contract and this is where it is checked. */
    for (int32_t level = 0; level <= INKCELL_ANIM_ONE; level += 50) {
        const enum inkcell_tone tone = inkcell_tone_for_load(level, 250, 500);
        INKCELL_TEST_FAIL_IF(inkcell_tone_family(tone) == INKCELL_FAMILY_COUNT,
                             "a load tone escaped the families a meter is validated for");
    }
    record_success(test_name);
}

/*
 * The band, which is the load tones generalised to a domain and a direction.
 *
 * It matters that the ascending half agrees with inkcell_tone_for_load() exactly, because that
 * is the claim the implementation makes rather than restating it - and it matters that the
 * descending half exists at all, because a battery is the one reading on the mesh that is worse
 * when it is smaller and the ascending form would have called a flat one healthy.
 */
INKCELL_TEST_CASE(ui_theme_band_tone_directions, unit) {
    const struct inkcell_band rising = {.warn = 250, .bad = 500};

    /* No band earns nothing, so a caller without thresholds need not branch. */
    INKCELL_TEST_FAIL_IF(inkcell_band_tone(NULL, 900, INKCELL_TONE_PRIMARY) != INKCELL_TONE_PRIMARY,
                         "a reading with no band earned a tone anyway");

    INKCELL_TEST_FAIL_IF(inkcell_band_tone(&rising, 0, INKCELL_TONE_PRIMARY) !=
                             INKCELL_TONE_PRIMARY,
                         "a reading below every boundary did not rest in the tone it was given");
    INKCELL_TEST_FAIL_IF(inkcell_band_tone(&rising, 250, INKCELL_TONE_SUCCESS) !=
                             INKCELL_TONE_WARNING,
                         "the warning boundary itself did not warn");
    INKCELL_TEST_FAIL_IF(inkcell_band_tone(&rising, 500, INKCELL_TONE_SUCCESS) !=
                             INKCELL_TONE_ERROR,
                         "the bad boundary itself was not bad");

    /* The half that is delegated: past a boundary the two must give the same answer, or the
       generalisation has quietly become a second set of thresholds. */
    for (int32_t level = 250; level <= INKCELL_ANIM_ONE; level += 25) {
        INKCELL_TEST_FAIL_IF(inkcell_band_tone(&rising, level, INKCELL_TONE_SUCCESS) !=
                                 inkcell_tone_for_load(level, rising.warn, rising.bad),
                             "an ascending band disagreed with the load tones it delegates to");
    }

    /* Descending: a battery, in percent, worse as it falls. The pair is stated in the order it
       is read - warn first, then bad - so the reversal is the sentence rather than a typo. */
    const struct inkcell_band falling = {.warn = 30, .bad = 15};
    INKCELL_TEST_FAIL_IF(inkcell_band_tone(&falling, 87, INKCELL_TONE_SUCCESS) !=
                             INKCELL_TONE_SUCCESS,
                         "a healthy battery did not rest");
    INKCELL_TEST_FAIL_IF(inkcell_band_tone(&falling, 30, INKCELL_TONE_SUCCESS) !=
                             INKCELL_TONE_WARNING,
                         "the low boundary itself did not warn");
    INKCELL_TEST_FAIL_IF(inkcell_band_tone(&falling, 16, INKCELL_TONE_SUCCESS) !=
                             INKCELL_TONE_WARNING,
                         "just above critical stopped being a warning");
    INKCELL_TEST_FAIL_IF(inkcell_band_tone(&falling, 15, INKCELL_TONE_SUCCESS) !=
                             INKCELL_TONE_ERROR,
                         "the critical boundary itself was not bad");
    INKCELL_TEST_FAIL_IF(inkcell_band_tone(&falling, 0, INKCELL_TONE_SUCCESS) != INKCELL_TONE_ERROR,
                         "a flat battery was not bad news");

    /* Whatever it answers, a meter may be filled with it: inkcell_theme_validate() holds every
       family against the track. The resting tone is the caller's, so only the earned ones are
       this function's to keep inside the contract. */
    for (int32_t level = 0; level <= 100; level += 5) {
        const enum inkcell_tone tone = inkcell_band_tone(&falling, level, INKCELL_TONE_SUCCESS);
        INKCELL_TEST_FAIL_IF(inkcell_tone_family(tone) == INKCELL_FAMILY_COUNT,
                             "a band tone escaped the families a meter is validated for");
    }
    record_success(test_name);
}

/*
 * A meter is a track with a fill in it, and a theme that loses either half loses the widget:
 * an unfindable track is a bar that vanishes when the reading is low, a fill that matches its
 * track is one that vanishes when the reading is high.
 */
INKCELL_TEST_CASE(ui_theme_validate_holds_the_meter_pairs, unit) {
    char reason[128];

    struct inkcell_theme flat = *inkcell_theme_default();
    flat.colors[INKCELL_COLOR_SUCCESS] = flat.colors[INKCELL_COLOR_METER_TRACK];
    reason[0] = '\0';
    INKCELL_TEST_FAIL_IF(inkcell_theme_validate(&flat, reason, sizeof reason),
                         "a meter fill the colour of its own track passed validation");
    INKCELL_TEST_FAIL_IF(reason[0] == '\0', "validation failed without saying why");

    struct inkcell_theme invisible_track = *inkcell_theme_default();
    invisible_track.colors[INKCELL_COLOR_METER_TRACK] = invisible_track.colors[INKCELL_COLOR_BG];
    INKCELL_TEST_FAIL_IF(inkcell_theme_validate(&invisible_track, reason, sizeof reason),
                         "a meter track the colour of the ground passed validation");

    /* And on a card, which is the half a borrowed SURFACE_SEL could not hold: a track validated
       against the body and invisible on a surface is a bar that exists on one screen. */
    struct inkcell_theme invisible_on_card = *inkcell_theme_default();
    invisible_on_card.colors[INKCELL_COLOR_METER_TRACK] =
        invisible_on_card.colors[INKCELL_COLOR_SURFACE];
    INKCELL_TEST_FAIL_IF(inkcell_theme_validate(&invisible_on_card, reason, sizeof reason),
                         "a meter track the colour of a card passed validation");

    /* And the geometry half: a bar with no thickness draws nothing at all, which is the one
       way a theme can turn the widget off without saying so. */
    struct inkcell_theme thin = *inkcell_theme_default();
    thin.metrics.meter_thickness = 0U;
    INKCELL_TEST_FAIL_IF(inkcell_theme_validate(&thin, reason, sizeof reason),
                         "a theme drawing meters no pixels tall passed validation");
    record_success(test_name);
}

/*
 * The series palette.
 *
 * Two things no other palette in this file has to promise, and both are what makes a *sequence*
 * different from a *set*. Every theme states all four, because a chart cannot draw fewer parts
 * than it has and there is no `series_count` to fall short in; and the four are told apart from
 * each other rather than only from the ground they sit on, because two slices of a bar are
 * never seen against a ground - they are seen against each other.
 *
 * The contrast arithmetic itself is inkcell_theme_validate()'s, and ui_theme_tables_are_readable
 * already runs it over the whole registry. What is here is that the lookup answers, and that
 * every theme spends its four on four different colours.
 */
INKCELL_TEST_CASE(ui_theme_series_palette, unit) {
    for (size_t i = 0; i < inkcell_theme_count(); ++i) {
        const struct inkcell_theme *theme = inkcell_theme_at(i);
        for (uint32_t slot = 0; slot < INKCELL_SERIES_COLORS; ++slot) {
            const struct inkcell_rgb color = inkcell_theme_series(theme, slot);
            INKCELL_TEST_FAIL_IF(color.r == theme->colors[INKCELL_COLOR_BG].r &&
                                     color.g == theme->colors[INKCELL_COLOR_BG].g &&
                                     color.b == theme->colors[INKCELL_COLOR_BG].b,
                                 "a theme left a series colour the same as its ground");
            /* Distinct as *values*, which is a weaker claim than validate()'s 1.4:1 and is here
               to catch the copy-paste rather than the contrast: a theme that stated one colour
               four times would otherwise read as a palette right up until the validator ran. */
            for (uint32_t other = 0; other < slot; ++other) {
                const struct inkcell_rgb earlier = inkcell_theme_series(theme, other);
                INKCELL_TEST_FAIL_IF(color.r == earlier.r && color.g == earlier.g &&
                                         color.b == earlier.b,
                                     "a theme states one colour in two series slots");
            }
        }

        /* An index past the end wraps rather than reading off the table. A chart wider than the
           palette is a bug upstream and drawing it is how that bug is visible; reading whatever
           follows the array is how it is not. */
        const struct inkcell_rgb wrapped = inkcell_theme_series(theme, INKCELL_SERIES_COLORS + 1U);
        const struct inkcell_rgb first = inkcell_theme_series(theme, 1U);
        INKCELL_TEST_FAIL_IF(wrapped.r != first.r || wrapped.g != first.g || wrapped.b != first.b,
                             "a series index past the end did not wrap");
    }
    record_success(test_name);
}

/*
 * And the contract itself, in both directions.
 *
 * The inward half is the one that is new here. Every other check in the validator asks whether
 * a colour can be seen against a ground; a pair of slices passes all of those and can still be
 * one undivided block, which on the high-contrast theme is exactly what the three non-status
 * families would have been.
 */
INKCELL_TEST_CASE(ui_theme_validate_holds_the_series_palette, unit) {
    char reason[128];

    struct inkcell_theme collapsed = *inkcell_theme_default();
    collapsed.series[2] = collapsed.series[1];
    reason[0] = '\0';
    INKCELL_TEST_FAIL_IF(inkcell_theme_validate(&collapsed, reason, sizeof reason),
                         "two series colours the eye cannot separate passed validation");
    INKCELL_TEST_FAIL_IF(reason[0] == '\0', "validation failed without saying why");

    /* The outward half, on a card rather than on the body - the ground a composition is actually
       drawn on, and the one a palette checked only against the body would disappear on. */
    struct inkcell_theme invisible = *inkcell_theme_default();
    invisible.series[0] = invisible.colors[INKCELL_COLOR_SURFACE];
    INKCELL_TEST_FAIL_IF(inkcell_theme_validate(&invisible, reason, sizeof reason),
                         "a series colour the colour of a card passed validation");
    record_success(test_name);
}

INKCELL_TEST_CASE(ui_theme_validate_rejects_an_unreadable_palette, unit) {
    struct inkcell_theme broken = *inkcell_theme_default();
    broken.colors[INKCELL_COLOR_TEXT] = broken.colors[INKCELL_COLOR_BG];

    char reason[128];
    reason[0] = '\0';
    INKCELL_TEST_FAIL_IF(inkcell_theme_validate(&broken, reason, sizeof reason),
                         "text drawn in the background colour passed validation");
    INKCELL_TEST_FAIL_IF(reason[0] == '\0', "validation failed without saying why");

    struct inkcell_theme no_font = *inkcell_theme_default();
    no_font.font_id = "not-a-font";
    INKCELL_TEST_FAIL_IF(inkcell_theme_validate(&no_font, reason, sizeof reason),
                         "a theme naming a font that does not exist passed validation");
    record_success(test_name);
}

/*
 * Cards: the geometry a theme owes them, and the contrast their text owes the fill.
 *
 * A card is the one component drawn on INKCELL_COLOR_SURFACE rather than on the ground, so
 * every tone a card row can take needs a pair in the validation table - and the pair that is
 * easiest to lose is the *edge*, because on a theme whose surface sits a step off the ground
 * the hairline is the whole of what says a card is there at all.
 */
INKCELL_TEST_CASE(ui_theme_states_its_geometry, unit) {
    for (size_t i = 0; i < inkcell_theme_count(); ++i) {
        const struct inkcell_theme *theme = inkcell_theme_at(i);
        const struct inkcell_metrics *metrics = inkcell_theme_metrics(theme);
        /* Zero padding is a card whose text touches its own edge, which is not a card. The
           radius may legitimately be zero: that is a theme asking for square corners. */
        INKCELL_TEST_FAIL_IF(metrics->card_pad == 0U, "a theme gives its cards no inset");
        /* Both are multiplied by the glyph scale, so a step is a whole cell of chrome. Two of
           them either way is already a quarter of a row on the Brick's panel; more than that is
           a theme spending its body rows on its own furniture. */
        INKCELL_TEST_FAIL_IF(metrics->card_pad > 4U, "a theme's card inset would eat the body");
        /* Also in glyph-scale steps, and bounded from both ends: nothing is an invisible bar,
           and a bar as tall as the text beside it is a block, not a meter. */
        INKCELL_TEST_FAIL_IF(metrics->meter_thickness == 0U, "a theme draws meters no pixels tall");
        INKCELL_TEST_FAIL_IF(metrics->meter_thickness > 3U,
                             "a theme's meter is as tall as the row it sits in");
        /*
         * The shape scale has to be a scale: rounder as it goes up, and never so round that a
         * corner eats the row it belongs to. A flat table - every shape the same radius - is
         * legal and is what an entirely square theme looks like; what is not legal is a large
         * shape squarer than a small one, because then a screen naming INKCELL_SHAPE_LG gets
         * something less round than one naming INKCELL_SHAPE_SM and the vocabulary is lying.
         */
        for (int shape = INKCELL_SHAPE_NONE; shape < INKCELL_SHAPE_FULL; ++shape) {
            INKCELL_TEST_FAIL_IF(metrics->shape[shape] > 4U,
                                 "a theme's corners are rounder than the box they are on");
            if (shape > INKCELL_SHAPE_NONE) {
                INKCELL_TEST_FAIL_IF(metrics->shape[shape] < metrics->shape[shape - 1],
                                     "a theme's shape scale gets squarer as it goes up");
            }
        }
        INKCELL_TEST_FAIL_IF(metrics->shape[INKCELL_SHAPE_NONE] != 0U,
                             "a theme rounds the corners of INKCELL_SHAPE_NONE");
    }

    /*
     * The radius accessor: steps times the scale, and the pill answering with something the
     * fill primitive will clamp rather than with a length of its own.
     *
     * The clamp is the contract worth pinning. inkcell_fb_fill_round_rect() takes half the shorter
     * side when a radius overshoots it, so INKCELL_SHAPE_FULL only has to be bigger than any box it
     * could be handed - and a number that merely looks big (a hundred pixels, say) stops being
     * big the day somebody draws a full-screen panel.
     */
    const struct inkcell_theme *shaped = inkcell_theme_default();
    const int scale = inkcell_theme_scale(shaped);
    const struct inkcell_metrics *shaped_metrics = inkcell_theme_metrics(shaped);
    for (int shape = INKCELL_SHAPE_NONE; shape < INKCELL_SHAPE_FULL; ++shape) {
        const int want = inkcell_scale_px((int)shaped_metrics->shape[shape], scale);
        INKCELL_TEST_FAIL_IF(inkcell_theme_radius(shaped, (enum inkcell_shape)shape, scale) != want,
                             "a shape's radius is not its step count times the glyph scale");
    }
    INKCELL_TEST_FAIL_IF(inkcell_theme_radius(shaped, INKCELL_SHAPE_FULL, scale) < 4096,
                         "a pill's radius is small enough for a panel to outgrow it");
    /* A NULL theme resolves to the default, like every other lookup here, and a shape outside
       the enum is square rather than undefined - a renderer with a stale enum draws a box, not
       a corner of garbage. */
    INKCELL_TEST_FAIL_IF(inkcell_theme_radius(NULL, INKCELL_SHAPE_MD, scale) !=
                             inkcell_theme_radius(shaped, INKCELL_SHAPE_MD, scale),
                         "a NULL theme did not resolve to the default for a radius");
    INKCELL_TEST_FAIL_IF(
        inkcell_theme_radius(shaped, (enum inkcell_shape)INKCELL_SHAPE_COUNT, scale) != 0,
        "a shape outside the scale was not square");

    char reason[128];
    struct inkcell_theme swallowed = *inkcell_theme_default();
    swallowed.colors[INKCELL_COLOR_SURFACE] = swallowed.colors[INKCELL_COLOR_PRIMARY];
    reason[0] = '\0';
    INKCELL_TEST_FAIL_IF(inkcell_theme_validate(&swallowed, reason, sizeof reason),
                         "a card fill that swallows the accent heading passed validation");

    struct inkcell_theme edgeless = *inkcell_theme_default();
    edgeless.colors[INKCELL_COLOR_RULE] = edgeless.colors[INKCELL_COLOR_SURFACE];
    reason[0] = '\0';
    INKCELL_TEST_FAIL_IF(inkcell_theme_validate(&edgeless, reason, sizeof reason),
                         "a card edge invisible against its own fill passed validation");
    record_success(test_name);
}

/*
 * Cycling, which is the whole of what the Settings row does.
 *
 * Every theme has to be reachable by pressing A enough times, and pressing it once more from
 * the last one has to come back to the first - a user who has stepped somewhere unreadable
 * gets home the same way they left.
 */
/* The three roles a screen reaches for most, held here because this suite is where a theme's
   tables are checked. The whole vocabulary - all seven roles, and the tracking, line height and
   figures each carries - is ui_type.c's. */
INKCELL_TEST_CASE(ui_theme_states_its_type_scale, unit) {
    for (size_t i = 0; i < inkcell_theme_count(); ++i) {
        const struct inkcell_theme *theme = inkcell_theme_at(i);

        for (int scale = INKCELL_SCALE_MIN; scale <= INKCELL_SCALE_MAX; ++scale) {
            const int title = inkcell_theme_type_scale(theme, INKCELL_TYPE_TITLE, scale);
            const int body = inkcell_theme_type_scale(theme, INKCELL_TYPE_BODY, scale);
            const int label = inkcell_theme_type_scale(theme, INKCELL_TYPE_LABEL, scale);

            /* Every role stays inside what the font registry can rasterise. This is the whole
               reason the table holds offsets and the accessor clamps: a title one step above a
               body already at the maximum is a size nothing can draw. */
            INKCELL_TEST_FAIL_IF(title < INKCELL_SCALE_MIN || title > INKCELL_SCALE_MAX,
                                 "a title scale fell outside the drawable range");
            INKCELL_TEST_FAIL_IF(label < INKCELL_SCALE_MIN || label > INKCELL_SCALE_MAX,
                                 "a label scale fell outside the drawable range");

            /* The body role is the body scale by definition - it is the zero the other two are
               offsets from, and a theme that moved it would be renaming the scale. */
            INKCELL_TEST_FAIL_IF(body != inkcell_theme_clamp_scale(theme, scale),
                                 "the body role is not the body scale");

            /*
             * The ordering is the vocabulary. A screen naming TITLE must never get something
             * smaller than one naming BODY, and BODY never smaller than LABEL - at the ends of
             * the range they collapse onto each other, which is the scale degrading rather than
             * inverting.
             */
            INKCELL_TEST_FAIL_IF(title < body, "a title is drawn smaller than the body");
            INKCELL_TEST_FAIL_IF(body < label, "a label is drawn larger than the body");
        }

        /* At the top of the range the title has nowhere to go and collapses onto the body; at
           the bottom the label does. Pinned because it is the behaviour a caller relies on
           instead of a bounds check of its own. */
        INKCELL_TEST_FAIL_IF(inkcell_theme_type_scale(theme, INKCELL_TYPE_TITLE,
                                                      INKCELL_SCALE_MAX) != INKCELL_SCALE_MAX,
                             "a title at the maximum scale did not collapse onto it");
        INKCELL_TEST_FAIL_IF(inkcell_theme_type_scale(theme, INKCELL_TYPE_LABEL,
                                                      INKCELL_SCALE_MIN) != INKCELL_SCALE_MIN,
                             "a label at the minimum scale did not collapse onto it");
    }

    /* Out of range answers the body scale rather than reading past the table, and NULL is the
       default theme as everywhere else in this header. */
    const struct inkcell_theme *theme = inkcell_theme_default();
    const int body = inkcell_theme_type_scale(theme, INKCELL_TYPE_BODY, INKCELL_SCALE(4));
    INKCELL_TEST_FAIL_IF(
        inkcell_theme_type_scale(theme, (enum inkcell_type) - 1, INKCELL_SCALE(4)) != body,
        "a negative type role read something");
    INKCELL_TEST_FAIL_IF(inkcell_theme_type_scale(theme, INKCELL_TYPE_COUNT, INKCELL_SCALE(4)) !=
                             body,
                         "a type role past the end read something");
    INKCELL_TEST_FAIL_IF(inkcell_theme_type_scale(NULL, INKCELL_TYPE_TITLE, INKCELL_SCALE(4)) !=
                             inkcell_theme_type_scale(theme, INKCELL_TYPE_TITLE, INKCELL_SCALE(4)),
                         "a NULL theme did not fall back to the default");
    record_success(test_name);
}

/*
 * The room between two whole steps, which is what the scale unit bought.
 *
 * The type scale had three roles because five sizes is all a whole multiplier over [2, 6] can
 * name, and a vocabulary with more words than sizes is a vocabulary of synonyms. That argument
 * is the thing this case exists to keep from coming back by accident: if INKCELL_SCALE_UNIT is
 * ever quietly returned to 1, the roles a later commit adds all collapse onto five sizes and
 * nothing else in the suite notices, because every page still renders and every ordering still
 * holds. Counting the distinct sizes is the only assertion that catches it.
 *
 * Seventeen is not a target, it is what quarters of a step work out to; the threshold below is
 * Material's fifteen roles, which is the vocabulary this range has to be able to carry.
 */
INKCELL_TEST_CASE(ui_theme_type_scale_has_room_between_steps, unit) {
    const struct inkcell_font *font = inkcell_theme_font(inkcell_theme_default());

    INKCELL_TEST_FAIL_IF(INKCELL_SCALE_UNIT < 2,
                         "a scale unit of one is a whole multiplier and has no room in it");

    /* Every size the range can actually be drawn at, counted by the cap height rather than by
       the scale: two scales a pixel apart that rasterise to the same capitals are one size as
       far as a reader is concerned, and it is the reader the vocabulary is for. */
    int distinct = 0;
    int previous = -1;
    for (int scale = INKCELL_SCALE_MIN; scale <= INKCELL_SCALE_MAX; ++scale) {
        const int cap = inkcell_font_cap(font, scale);
        INKCELL_TEST_FAIL_IF(cap < previous, "a larger scale drew shorter capitals");
        if (cap != previous) {
            ++distinct;
            previous = cap;
        }
    }
    INKCELL_TEST_FAIL_IF(distinct < (int)INKCELL_TYPE_COUNT,
                         "the range cannot draw one size per type role");
    INKCELL_TEST_FAIL_IF(distinct < 15,
                         "the range has no room for a Material-sized set of type roles");

    /* And the half step is a real size, not the whole step it rounds to - which is the property
       a role placed between the body and the title depends on. */
    const int body = INKCELL_SCALE(4);
    const int half = body + INKCELL_SCALE(1) / 2;
    const int whole = body + INKCELL_SCALE(1);
    INKCELL_TEST_FAIL_IF(inkcell_font_line(font, half) <= inkcell_font_line(font, body),
                         "half a step above the body is the body");
    INKCELL_TEST_FAIL_IF(inkcell_font_line(font, half) >= inkcell_font_line(font, whole),
                         "half a step above the body is a whole step");
    record_success(test_name);
}

INKCELL_TEST_CASE(ui_theme_states_its_spacing, unit) {
    for (size_t i = 0; i < inkcell_theme_count(); ++i) {
        const struct inkcell_theme *theme = inkcell_theme_at(i);
        const int scale = inkcell_theme_scale(theme);

        INKCELL_TEST_FAIL_IF(inkcell_theme_space(theme, INKCELL_SPACE_NONE, scale) != 0,
                             "INKCELL_SPACE_NONE is not nothing");

        int previous = 0;
        for (int space = INKCELL_SPACE_XS; space < INKCELL_SPACE_COUNT; ++space) {
            const int gap = inkcell_theme_space(theme, (enum inkcell_space)space, scale);
            /* A theme that asked for a gap gets at least a pixel of one, however small the
               scale: a half-step rounded away to zero is an inset that silently stops
               existing. */
            INKCELL_TEST_FAIL_IF(gap < 1, "a spacing step rounded away to nothing");
            /* And it has to be a scale, for the reason the shape steps do - a widget naming MD
               and getting less room than one naming SM is a vocabulary that lies. */
            INKCELL_TEST_FAIL_IF(gap < previous,
                                 "a theme's spacing scale gets tighter as it goes up");
            /* Nothing in the scale is a whole row: these are gaps between things, not rows.
               Four steps is already taller than the glyph cell at any scale, so a table that
               reaches it is a theme spending body rows on its own furniture. */
            INKCELL_TEST_FAIL_IF(gap > 4 * scale,
                                 "a theme's spacing step is as tall as the row it separates");
            previous = gap;
        }

        /* The scale is glyph-relative, so it grows with the text. A theme whose gaps did not
           move when the glyph scale did would be the literals this replaced. */
        INKCELL_TEST_FAIL_IF(inkcell_theme_space(theme, INKCELL_SPACE_MD, INKCELL_SCALE_MAX) <
                                 inkcell_theme_space(theme, INKCELL_SPACE_MD, INKCELL_SCALE_MIN),
                             "a spacing step did not grow with the glyph scale");
    }

    const struct inkcell_theme *theme = inkcell_theme_default();
    INKCELL_TEST_FAIL_IF(inkcell_theme_space(theme, (enum inkcell_space) - 1, 4) != 0,
                         "a negative spacing token read something");
    INKCELL_TEST_FAIL_IF(inkcell_theme_space(theme, INKCELL_SPACE_COUNT, 4) != 0,
                         "a spacing token past the end read something");
    INKCELL_TEST_FAIL_IF(inkcell_theme_space(NULL, INKCELL_SPACE_SM, 4) !=
                             inkcell_theme_space(theme, INKCELL_SPACE_SM, 4),
                         "a NULL theme did not fall back to the default");
    record_success(test_name);
}

INKCELL_TEST_CASE(ui_theme_states_its_motion, unit) {
    for (size_t i = 0; i < inkcell_theme_count(); ++i) {
        const struct inkcell_theme *theme = inkcell_theme_at(i);

        uint32_t previous = 0U;
        for (int motion = INKCELL_MOTION_SHORT; motion <= INKCELL_MOTION_LONG; ++motion) {
            const uint32_t ms = inkcell_theme_motion(theme, (enum inkcell_motion)motion);
            /* A duration of zero is "already there", which is a theme with no motion at all
               rather than a theme with a broken token. Nothing here is allowed to be that by
               accident, so the three transition tokens are held to a range a human can see and
               will not wait for. */
            INKCELL_TEST_FAIL_IF(ms < 60U, "a theme's transition is too short to be seen");
            INKCELL_TEST_FAIL_IF(ms > 600U, "a theme's transition is long enough to wait for");
            /*
             * The three have to be a scale, for the reason the shape steps do: a widget naming
             * INKCELL_MOTION_SHORT and getting something slower than INKCELL_MOTION_LONG is a
             * vocabulary that lies, and the asymmetry the snackbar depends on - out shorter
             * than in - is exactly this ordering.
             */
            INKCELL_TEST_FAIL_IF(motion > INKCELL_MOTION_SHORT && ms <= previous,
                                 "a theme's motion scale does not get longer as it goes up");
            previous = ms;
        }

        /* The loop is not a transition and is not on that scale: it is one pass of something
           with no end, and it has to be long enough to read as travel rather than as flicker. */
        const uint32_t loop = inkcell_theme_motion(theme, INKCELL_MOTION_LOOP);
        INKCELL_TEST_FAIL_IF(loop <= previous, "a theme's loop is shorter than a transition");
        INKCELL_TEST_FAIL_IF(loop < 600U, "a theme's indeterminate loop reads as flicker");
    }

    /* Out of range answers 0 rather than reading past the table - the same contract the shape
       accessor has, and what keeps a token added to the enum but not to a theme from being a
       buffer overrun instead of a still control. */
    const struct inkcell_theme *theme = inkcell_theme_default();
    INKCELL_TEST_FAIL_IF(inkcell_theme_motion(theme, (enum inkcell_motion) - 1) != 0U,
                         "a negative motion token read something");
    INKCELL_TEST_FAIL_IF(inkcell_theme_motion(theme, INKCELL_MOTION_COUNT) != 0U,
                         "a motion token past the end read something");
    /* NULL is the default theme, as it is everywhere else in this header. */
    INKCELL_TEST_FAIL_IF(inkcell_theme_motion(NULL, INKCELL_MOTION_SHORT) !=
                             inkcell_theme_motion(theme, INKCELL_MOTION_SHORT),
                         "a NULL theme did not fall back to the default");
    record_success(test_name);
}

INKCELL_TEST_CASE(ui_theme_cycles_through_every_theme, unit) {
    const size_t count = inkcell_theme_count();
    const struct inkcell_theme *theme = inkcell_theme_default();
    bool seen[16];
    memset(seen, 0, sizeof seen);
    INKCELL_TEST_FAIL_IF(count > (sizeof seen / sizeof seen[0]),
                         "more themes than this case can track; raise `seen`");

    for (size_t step = 0; step < count; ++step) {
        for (size_t i = 0; i < count; ++i) {
            if (inkcell_theme_at(i) == theme) {
                INKCELL_TEST_FAIL_IF(seen[i], "cycling revisited a theme before covering them all");
                seen[i] = true;
            }
        }
        theme = inkcell_theme_next(theme);
        INKCELL_TEST_FAIL_IF(theme == NULL, "cycling ran off the end of the registry");
    }
    for (size_t i = 0; i < count; ++i) {
        INKCELL_TEST_FAIL_IF(!seen[i], "cycling never reached one of the themes");
    }
    INKCELL_TEST_FAIL_IF(theme != inkcell_theme_default(),
                         "a full cycle did not come back to where it started");

    /* A theme that is not in the registry at all - a copy, say - lands somewhere usable
       rather than nowhere. */
    struct inkcell_theme stray = *inkcell_theme_default();
    INKCELL_TEST_FAIL_IF(inkcell_theme_next(&stray) != inkcell_theme_default(),
                         "cycling from an unregistered theme did not fall back to the default");
    record_success(test_name);
}

/* What a saved preference is read back through, and what the environment is asked with. */
INKCELL_TEST_CASE(ui_theme_resolves_ids_and_the_environment, unit) {
    INKCELL_TEST_FAIL_IF(inkcell_theme_resolve("light") != inkcell_theme_by_id("light"),
                         "a known id did not resolve to its theme");
    INKCELL_TEST_FAIL_IF(inkcell_theme_resolve("no-such-theme") != inkcell_theme_default(),
                         "an unknown id did not resolve to the default");
    INKCELL_TEST_FAIL_IF(inkcell_theme_resolve("") != inkcell_theme_default(),
                         "an empty id did not resolve to the default");
    INKCELL_TEST_FAIL_IF(inkcell_theme_resolve(NULL) != inkcell_theme_default(),
                         "a NULL id did not resolve to the default");

    /* inkcell_theme_env() answers NULL when nobody named one, which is what tells the app the
       choice is the user's to make rather than the environment's. */
    char saved[64];
    saved[0] = '\0';
    const char *const previous = inkwell_env_get("THEME");
    const bool had_env = (previous != NULL);
    if (had_env) {
        snprintf(saved, sizeof saved, "%s", previous);
    }

    (void)unsetenv("INKWELL_THEME");
    INKCELL_TEST_FAIL_IF(inkcell_theme_env() != NULL, "an unset <PREFIX>_THEME named a theme");
    INKCELL_TEST_FAIL_IF(inkcell_theme_from_env() != inkcell_theme_default(),
                         "an unset <PREFIX>_THEME did not fall back to the default");

    (void)setenv("INKWELL_THEME", "light", 1);
    INKCELL_TEST_FAIL_IF(inkcell_theme_env() != inkcell_theme_by_id("light"),
                         "<PREFIX>_THEME did not name its theme");

    /* A typo must not leave a handheld with no UI, and must not read as a deliberate pin. */
    (void)setenv("INKWELL_THEME", "not-a-theme", 1);
    INKCELL_TEST_FAIL_IF(inkcell_theme_env() != NULL, "an unknown <PREFIX>_THEME named a theme");
    INKCELL_TEST_FAIL_IF(inkcell_theme_from_env() != inkcell_theme_default(),
                         "an unknown <PREFIX>_THEME did not fall back to the default");

    if (had_env) {
        (void)setenv("INKWELL_THEME", saved, 1);
    } else {
        (void)unsetenv("INKWELL_THEME");
    }
    record_success(test_name);
}

/* Metrics are theme data, and the scale a theme asks for is clamped rather than trusted. */
INKCELL_TEST_CASE(ui_theme_scale_is_clamped, unit) {
    const struct inkcell_theme *theme = inkcell_theme_default();
    INKCELL_TEST_FAIL_IF(inkcell_theme_clamp_scale(theme, 0) != inkcell_theme_scale(theme),
                         "scale 0 is not the theme's own");
    INKCELL_TEST_FAIL_IF(inkcell_theme_clamp_scale(theme, 99) != INKCELL_SCALE_MAX,
                         "an absurd scale was not clamped down");
    INKCELL_TEST_FAIL_IF(inkcell_theme_clamp_scale(theme, -4) != inkcell_theme_scale(theme),
                         "a negative scale is not the theme's own");
    INKCELL_TEST_FAIL_IF(inkcell_theme_clamp_scale(NULL, 1) != INKCELL_SCALE_MIN,
                         "a NULL theme did not fall back to the default and clamp");

    /* Chrome is smaller than the body but never below the floor, whatever the body is at. */
    for (int scale = INKCELL_SCALE_MIN; scale <= INKCELL_SCALE_MAX; ++scale) {
        const int chrome = inkcell_theme_type_scale(theme, INKCELL_TYPE_LABEL, scale);
        INKCELL_TEST_FAIL_IF(chrome < INKCELL_SCALE_MIN,
                             "chrome text fell below the minimum scale");
        INKCELL_TEST_FAIL_IF(chrome > scale, "chrome text is bigger than the body text");
    }
    record_success(test_name);
}

/* The font is a seam, not a constant. Whatever is registered has to measure sanely, because
   every column count, button width and bubble height in the UI is derived from it. */
INKCELL_TEST_CASE(ui_theme_fonts_measure, unit) {
    INKCELL_TEST_FAIL_IF(inkcell_font_count() == 0U, "no fonts are registered");
    for (size_t i = 0; i < inkcell_font_count(); ++i) {
        const struct inkcell_font *font = inkcell_font_at(i);
        INKCELL_TEST_FAIL_IF(font == NULL || font->id == NULL, "a registered font is unusable");
        INKCELL_TEST_FAIL_IF(inkcell_font_by_id(font->id) != font,
                             "a font does not come back under its own id");
        INKCELL_TEST_FAIL_IF(font->width == 0U || font->width > INKCELL_GLYPH_MAX_WIDTH,
                             "a font is wider than the glyph buffer");
        INKCELL_TEST_FAIL_IF(font->height == 0U || font->height > INKCELL_GLYPH_MAX_HEIGHT,
                             "a font is taller than the glyph buffer");
        /* The master is what the coverage is stored at, and it is what the resampler indexes
           with - a font declaring one bigger than the buffer would read off the end of it. */
        INKCELL_TEST_FAIL_IF(font->master_w == 0U ||
                                 font->master_w > INKCELL_GLYPH_MASTER_MAX_WIDTH,
                             "a font's master is wider than the glyph buffer");
        INKCELL_TEST_FAIL_IF(font->master_h == 0U ||
                                 font->master_h > INKCELL_GLYPH_MASTER_MAX_HEIGHT,
                             "a font's master is taller than the glyph buffer");
        INKCELL_TEST_FAIL_IF(font->sampling != INKCELL_FONT_PIXEL &&
                                 font->sampling != INKCELL_FONT_SMOOTH,
                             "a font asks for a sampling this layer does not have");

        /* Advances have to grow with the multiplier, or every measurement above breaks. */
        INKCELL_TEST_FAIL_IF(inkcell_font_advance(font, INKCELL_SCALE(2)) <=
                                 inkcell_font_advance(font, INKCELL_SCALE(1)),
                             "the character advance does not grow with the scale");
        INKCELL_TEST_FAIL_IF(inkcell_font_line(font, INKCELL_SCALE(1)) < (int)font->height,
                             "the line advance does not clear the cell");

        /* A glyph the font has, and one nothing has: both are drawable, one is the tofu. */
        struct inkcell_glyph glyph;
        INKCELL_TEST_FAIL_IF(!inkcell_font_glyph(font, (uint32_t)'A', &glyph),
                             "the font has no capital A");
        INKCELL_TEST_FAIL_IF(inkcell_font_glyph(font, 0x10FFFDU, &glyph),
                             "the font claims a private-use codepoint");

        /*
         * Coverage, not a mask. Every value has to be inside the ramp the renderer quantises
         * against - one above it indexes past the blend table - and a capital A has to carry
         * some, or the font is drawing nothing and reporting success.
         */
        (void)inkcell_font_glyph(font, (uint32_t)'A', &glyph);
        bool inked = false;
        bool in_range = true;
        for (size_t px = 0; px < (size_t)font->master_w * (size_t)font->master_h; ++px) {
            if (glyph.alpha[px] > INKCELL_GLYPH_MAX_ALPHA) {
                in_range = false;
            }
            if (glyph.alpha[px] > 0U) {
                inked = true;
            }
        }
        INKCELL_TEST_FAIL_IF(!in_range, "a glyph carries coverage above the ramp");
        INKCELL_TEST_FAIL_IF(!inked, "a capital A has no coverage at all");

        /* A pixel font is a mask stored as coverage: every value is off or solid, which is what
           lets the resampler block-replicate it and land on exactly the old spans. */
        if (font->sampling == INKCELL_FONT_PIXEL) {
            bool binary = true;
            for (size_t px = 0; px < (size_t)font->master_w * (size_t)font->master_h; ++px) {
                if (glyph.alpha[px] != 0U && glyph.alpha[px] != INKCELL_GLYPH_MAX_ALPHA) {
                    binary = false;
                }
            }
            INKCELL_TEST_FAIL_IF(!binary, "a pixel font carries partial coverage");
        }
    }

    /* A theme always resolves to a font, even asking for one that does not exist. */
    struct inkcell_theme no_font = *inkcell_theme_default();
    no_font.font_id = "not-a-font";
    INKCELL_TEST_FAIL_IF(inkcell_theme_font(&no_font) != inkcell_font_default(),
                         "an unknown font id did not fall back to the default");
    INKCELL_TEST_FAIL_IF(inkcell_theme_font(NULL) == NULL, "a NULL theme resolved to no font");
    record_success(test_name);
}

/*
 * Every font draws every character every other font can.
 *
 * A theme picks a font, so a face that covers less than another turns a node name the UI could
 * draw into a row of replacement boxes - and it does it only for the people whose names need
 * the letters it dropped, which is exactly the kind of bug nobody here would hit. The set is
 * whatever the fonts agree on rather than a list in this file, so adding a character to one
 * font is what makes this fail for the others.
 *
 * The range stops after the arrows and the ideographic space: past there is emoji, which is a
 * sprite table rather than a font, and no text face is expected to carry it.
 */
INKCELL_TEST_CASE(ui_theme_fonts_agree_on_coverage, unit) {
    for (uint32_t codepoint = 0x20U; codepoint <= 0x3000U; ++codepoint) {
        size_t covering = 0;
        for (size_t i = 0; i < inkcell_font_count(); ++i) {
            if (inkcell_font_has_glyph(inkcell_font_at(i), codepoint)) {
                ++covering;
            }
        }
        if (covering != 0 && covering != inkcell_font_count()) {
            char reason[96];
            snprintf(reason, sizeof reason, "only %zu of %zu fonts can draw U+%04X", covering,
                     inkcell_font_count(), (unsigned)codepoint);
            record_failure(test_name, reason);
            return;
        }
    }
    record_success(test_name);
}

/*
 * A font's capitals are as tall as it says they are, and no taller than its cell.
 *
 * Anything standing beside the text is sized off this - an icon in a row slot above all - so a
 * font whose cap height is a guess puts an overbearing symbol next to every row it appears in.
 * That is not hypothetical: it is what the first face with real ascenders did, because the cell
 * height had been standing in for the cap height while every font had the two the same.
 */
INKCELL_TEST_CASE(ui_theme_fonts_cap_height, unit) {
    for (size_t i = 0; i < inkcell_font_count(); ++i) {
        const struct inkcell_font *font = inkcell_font_at(i);
        for (int scale = INKCELL_SCALE_MIN; scale <= INKCELL_SCALE_MAX; ++scale) {
            const int cap = inkcell_font_cap(font, scale);
            INKCELL_TEST_FAIL_IF(cap <= 0, "a font's capitals have no height");
            INKCELL_TEST_FAIL_IF(cap > (int)font->height * scale,
                                 "a font's capitals are taller than its cell");
        }

        /* And the number is the truth about the glyphs: a capital must not reach into the
           overhang, which is the diacritics' room and nothing else's. */
        struct inkcell_glyph glyph;
        (void)inkcell_font_glyph(font, (uint32_t)'H', &glyph);
        int highest = (int)font->master_h;
        for (int row = 0; row < highest; ++row) {
            for (int col = 0; col < (int)font->master_w; ++col) {
                if (glyph.alpha[(size_t)row * font->master_w + (size_t)col] > 0U) {
                    highest = row;
                    break;
                }
            }
        }
        INKCELL_TEST_FAIL_IF(highest >= (int)font->master_h, "a capital H has no ink");
        INKCELL_TEST_FAIL_IF(highest < (int)font->master_top,
                             "a capital reaches into the accent overhang");
    }
    record_success(test_name);
}

/*
 * The prefix is the whole of how a knob is spelled, so a name that carries one already reads a
 * variable nobody sets.
 *
 * Worth a case of its own because nothing else catches it: inkwell_env_get("INKCELL_FB_SCALE") and
 * inkwell_env_get("INKCELL_FB_SCALE") both compile, both return NULL on a machine that has
 * neither set, and the second is wrong on every machine that has the first. The extraction
 * introduced exactly this bug once, mechanically, in fb.c.
 */
INKCELL_TEST_CASE(ui_env_prefix_is_applied_once, unit) {
    const char *const original = inkwell_env_prefix();
    char saved[32];
    snprintf(saved, sizeof saved, "%s", original != NULL ? original : "INKCELL");

    inkwell_env_set_prefix("TESTPREFIX");
    INKCELL_TEST_FAIL_IF_CLEANUP(strcmp(inkwell_env_prefix(), "TESTPREFIX") != 0,
                                 inkwell_env_set_prefix(saved), "the prefix did not take");

    (void)setenv("TESTPREFIX_KNOB", "yes", 1);
    INKCELL_TEST_FAIL_IF_CLEANUP(!inkwell_env_bool("KNOB", NULL, false),
                                 inkwell_env_set_prefix(saved),
                                 "a suffix did not resolve under the prefix");

    /* The bug: a name that already carries the prefix must not resolve. */
    (void)setenv("TESTPREFIX_TESTPREFIX_KNOB", "yes", 1);
    INKCELL_TEST_FAIL_IF_CLEANUP(inkwell_env_get("KNOB") == NULL, inkwell_env_set_prefix(saved),
                                 "the knob went missing");
    INKCELL_TEST_FAIL_IF_CLEANUP(
        strcmp(inkwell_env_get("TESTPREFIX_KNOB"), "yes") != 0, inkwell_env_set_prefix(saved),
        "a prefixed name resolved to something other than the doubly-prefixed variable");

    (void)unsetenv("TESTPREFIX_KNOB");
    (void)unsetenv("TESTPREFIX_TESTPREFIX_KNOB");

    /* An empty prefix restores the default rather than reading bare names. */
    inkwell_env_set_prefix("");
    INKCELL_TEST_FAIL_IF_CLEANUP(strcmp(inkwell_env_prefix(), "INKWELL") != 0,
                                 inkwell_env_set_prefix(saved),
                                 "an empty prefix did not restore the default");

    inkwell_env_set_prefix(saved);
    record_success(test_name);
}
