#define _POSIX_C_SOURCE 200809L

#include "inkcell/utils/env.h"
#include "inkcell/ui/input_profile.h"

#include "inkcell/utils/array.h"
#include "inkcell/utils/log.h"

#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/*
 * The caps.
 *
 * "A" through "START" are what is printed on the case; the two pairs are drawn from the font's
 * arrows (U+2190..U+2193), which is why they are a keycap the eye reads as a direction rather
 * than the words "Up/Down" spending four cells saying it.
 *
 * Shared by both profiles below, and that is the honest description of the two devices rather
 * than a saving: a Brick and an Xbox-convention pad print the same four letters and disagree
 * only about where they are. A device whose buttons are printed something else - circles and
 * crosses, or numbers - states a table of its own here, and states it beside its codes, which
 * is the whole point of the two living in one row.
 */
static const char *const k_caps_abxy[INKCELL_BUTTON_COUNT] = {
    [INKCELL_BUTTON_A] = "A",
    [INKCELL_BUTTON_B] = "B",
    [INKCELL_BUTTON_X] = "X",
    [INKCELL_BUTTON_Y] = "Y",
    [INKCELL_BUTTON_START] = "START",
    [INKCELL_BUTTON_SELECT] = "SELECT",
    [INKCELL_BUTTON_SHOULDERS] = "L/R",
    [INKCELL_BUTTON_TRIGGERS] = "L2/R2",
    [INKCELL_BUTTON_UP_DOWN] = "\xE2\x86\x91\xE2\x86\x93",    /* up arrow, down arrow */
    [INKCELL_BUTTON_LEFT_RIGHT] = "\xE2\x86\x90\xE2\x86\x92", /* left arrow, right arrow */
    [INKCELL_BUTTON_QUIT] = NULL,
};

/*
 * The TrimUI Brick, and the reason this file exists.
 *
 * Every code here was read off the device log by pressing that button, which is the only way to
 * get it right: the pad impersonates an Xbox 360 controller, so the BTN_ names carry that
 * convention's positions while the plastic carries Nintendo's. The button printed A is on the
 * RIGHT and reports BTN_EAST; B is at the bottom and reports BTN_SOUTH - the reverse of what
 * the BTN_A/BTN_B aliases suggest. X and Y are stranger still and do not follow the positional
 * reading at all: the button printed Y is on the LEFT and reports BTN_NORTH (nominally "top"),
 * so X on the top reports BTN_WEST.
 *
 * Mapping these by position leaves Y unreachable and fires X for every Y binding - saving a
 * settings section, above all. Do not "correct" this back to the positional reading; the
 * xbox profile below is where the positional convention lives, and it is a different device.
 */
static const struct inkcell_input_binding k_bindings_brick[] = {
    {BTN_EAST, INKCELL_KEY_A},  /* 305 - printed A, on the right */
    {BTN_SOUTH, INKCELL_KEY_B}, /* 304 - printed B, at the bottom */
    {BTN_WEST, INKCELL_KEY_X},  /* 308 - printed X, on top */
    {BTN_NORTH, INKCELL_KEY_Y}, /* 307 - printed Y, on the left */
};

/*
 * The other convention: an Xbox-style pad, which is what a Steam Deck, an Xbox pad and most USB
 * controllers report. The profile a desktop or a handheld that is not a Brick most likely wants.
 *
 * **Written with the legacy BTN_A/B/X/Y aliases, and the directional names are a trap here.**
 * BTN_X is BTN_NORTH (307) and BTN_Y is BTN_WEST (308), but an Xbox pad's X is on the LEFT and
 * its Y is on TOP - so the compass names describe neither this layout nor, for those two, any
 * other. They are aliases of the older BTN_X/BTN_Y values, whose numbering predates them.
 * Writing this table by position gets A and B right and silently swaps X and Y, which is the
 * Brick's own mistake one profile over.
 *
 * The Brick is the evidence rather than the header, because it is measured: it impersonates an
 * Xbox 360 pad, so xpad drives it, and its TOP button - the 360's Y slot - reports 308 while its
 * LEFT button - the 360's X slot - reports 307. A real Xbox pad puts Y on top and X on the left,
 * so it reports the same two codes for the buttons carrying those letters.
 *
 * It is here rather than in a doc because the pair of tables is the point: a device that gets
 * this wrong is not broken in a way anyone can see. Confirm and back trade places, which reads
 * as the client ignoring A and going back on its own.
 */
static const struct inkcell_input_binding k_bindings_xbox[] = {
    {BTN_A, INKCELL_KEY_A}, /* 304 (BTN_SOUTH) - printed A, at the bottom */
    {BTN_B, INKCELL_KEY_B}, /* 305 (BTN_EAST)  - printed B, on the right */
    {BTN_X, INKCELL_KEY_X}, /* 307 (BTN_NORTH) - printed X, on the LEFT */
    {BTN_Y, INKCELL_KEY_Y}, /* 308 (BTN_WEST)  - printed Y, on TOP */
};

/*
 * The registry. The default is first, which is also the order a listing walks.
 *
 * Only devices whose mapping somebody has actually measured belong here. A profile guessed from
 * a spec sheet is worse than no profile: the fallback is at least a known wrong answer, where a
 * guess that is nearly right is the one a user stops questioning.
 */
static const struct inkcell_input_profile k_profiles[] = {
    {
        .name = "brick",
        .bindings = k_bindings_brick,
        .binding_count = INKCELL_ARRAY_LEN(k_bindings_brick),
        .caps = k_caps_abxy,
    },
    {
        .name = "xbox",
        .bindings = k_bindings_xbox,
        .binding_count = INKCELL_ARRAY_LEN(k_bindings_xbox),
        .caps = k_caps_abxy,
    },
};

static const struct inkcell_input_profile *s_profile;
static bool s_profile_loaded;

size_t inkcell_input_profile_count(void) { return INKCELL_ARRAY_LEN(k_profiles); }

const struct inkcell_input_profile *inkcell_input_profile_at(size_t index) {
    return index < INKCELL_ARRAY_LEN(k_profiles) ? &k_profiles[index] : NULL;
}

const struct inkcell_input_profile *inkcell_input_profile_default(void) { return &k_profiles[0]; }

const struct inkcell_input_profile *inkcell_input_profile_by_name(const char *name) {
    if (name == NULL || name[0] == '\0') {
        return NULL;
    }
    for (size_t i = 0; i < INKCELL_ARRAY_LEN(k_profiles); ++i) {
        if (strcasecmp(k_profiles[i].name, name) == 0) {
            return &k_profiles[i];
        }
    }
    return NULL;
}

const struct inkcell_input_profile *inkcell_input_profile_from_env(void) {
    if (s_profile_loaded) {
        return s_profile;
    }
    s_profile_loaded = true;

    const char *const name = inkcell_env_get("INPUT_PROFILE");
    const struct inkcell_input_profile *profile = inkcell_input_profile_by_name(name);
    if (profile == NULL) {
        if (name != NULL && name[0] != '\0') {
            inkcell_log_warn("input", "Unknown <PREFIX>_INPUT_PROFILE='%s'; using %s", name,
                          inkcell_input_profile_default()->name);
        }
        profile = inkcell_input_profile_default();
    } else {
        inkcell_log_info("input", "Input profile %s", profile->name);
    }

    s_profile = profile;
    return s_profile;
}

void inkcell_input_profile_reload(void) {
    s_profile_loaded = false;
    s_profile = NULL;
    (void)inkcell_input_profile_from_env();
}

enum inkcell_key inkcell_input_profile_key(const struct inkcell_input_profile *profile,
                                           uint16_t code) {
    if (profile == NULL) {
        return INKCELL_KEY_NONE;
    }
    for (size_t i = 0; i < profile->binding_count; ++i) {
        if (profile->bindings[i].code == code) {
            return profile->bindings[i].key;
        }
    }
    return INKCELL_KEY_NONE;
}

const char *inkcell_input_profile_cap(const struct inkcell_input_profile *profile,
                                      enum inkcell_button button) {
    if (profile == NULL || profile->caps == NULL ||
        (unsigned)button >= (unsigned)INKCELL_BUTTON_COUNT || profile->caps[button] == NULL) {
        return "";
    }
    return profile->caps[button];
}

/* The four a profile is required to bind. The rest of the pad is a convention, so a profile
   that named the shoulders would be restating something no device has disagreed about. */
static const enum inkcell_key k_required_keys[] = {INKCELL_KEY_A, INKCELL_KEY_B, INKCELL_KEY_X,
                                                   INKCELL_KEY_Y};

bool inkcell_input_profile_validate(const struct inkcell_input_profile *profile, char *reason,
                                    size_t reason_len) {
    if (reason != NULL && reason_len > 0U) {
        reason[0] = '\0';
    }
    if (profile == NULL || profile->name == NULL || profile->name[0] == '\0') {
        if (reason != NULL) {
            snprintf(reason, reason_len, "profile has no name");
        }
        return false;
    }
    if (profile->bindings == NULL || profile->caps == NULL) {
        if (reason != NULL) {
            snprintf(reason, reason_len, "%s states no bindings or no caps", profile->name);
        }
        return false;
    }

    /* Each face button exactly once: a missing one is a press that does nothing, and a repeated
       one is two buttons doing the same job while a third does nothing. */
    for (size_t i = 0; i < INKCELL_ARRAY_LEN(k_required_keys); ++i) {
        size_t bound = 0U;
        for (size_t j = 0; j < profile->binding_count; ++j) {
            if (profile->bindings[j].key == k_required_keys[i]) {
                ++bound;
            }
        }
        if (bound != 1U) {
            if (reason != NULL) {
                snprintf(reason, reason_len, "%s binds key %d %zu times, want 1", profile->name,
                         (int)k_required_keys[i], bound);
            }
            return false;
        }
    }

    /* One code cannot be two keys: the lookup takes the first match, so the second binding
       would be a row nobody could reach and nothing would say so. */
    for (size_t i = 0; i < profile->binding_count; ++i) {
        for (size_t j = i + 1U; j < profile->binding_count; ++j) {
            if (profile->bindings[i].code == profile->bindings[j].code) {
                if (reason != NULL) {
                    snprintf(reason, reason_len, "%s binds code %u twice", profile->name,
                             (unsigned)profile->bindings[i].code);
                }
                return false;
            }
        }
    }

    /* A cap for everything but the quit key, which the input layer answers. */
    for (unsigned button = 0U; button < (unsigned)INKCELL_BUTTON_COUNT; ++button) {
        const bool wanted = button != (unsigned)INKCELL_BUTTON_QUIT;
        const bool stated = profile->caps[button] != NULL && profile->caps[button][0] != '\0';
        if (wanted != stated) {
            if (reason != NULL) {
                snprintf(reason, reason_len, "%s %s a cap for button %u", profile->name,
                         wanted ? "is missing" : "should not state", button);
            }
            return false;
        }
    }

    return true;
}
