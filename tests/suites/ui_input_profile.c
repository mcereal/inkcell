#define _POSIX_C_SOURCE 200809L

/*
 * The keyboard's profile, and the preference that picks it: what a window's keycaps say about
 * the keys a keyboard has. The device profiles' own cases are with the application that ships
 * them; these hold the row a desktop window uses and the call that chooses it.
 */

#include "framework/inkcell_test.h"

#include "inkcell/ui/actions.h"
#include "inkcell/ui/input.h"
#include "inkcell/ui/input_codes.h"
#include "inkcell/ui/input_profile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void profile_test_setenv(const char *name, const char *value) {
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

/* Puts the process back the way every other case expects it: no variable, no preference. */
static void profile_test_reset(void) {
    profile_test_setenv("INKWELL_INPUT_PROFILE", NULL);
    (void)inkcell_input_profile_prefer(NULL);
    inkcell_input_profile_reload();
}

/* The keyboard's row is held to the same shape as a pad's: four face keys, a cap for each. */
INKCELL_TEST_CASE(input_profile_the_keyboard_is_a_complete_profile, unit) {
    const struct inkcell_input_profile *keyboard = inkcell_input_profile_by_name("keyboard");
    INKCELL_TEST_FAIL_IF(keyboard == NULL, "the registry should hold a keyboard");
    char reason[128];
    INKCELL_TEST_FAIL_IF(!inkcell_input_profile_validate(keyboard, reason, sizeof reason), reason);
    /* And its face bindings agree with the convention a keyboard reaches the UI through. */
    INKCELL_TEST_FAIL_IF(inkcell_input_profile_key(keyboard, KEY_ENTER) != INKCELL_KEY_A,
                         "Enter should be A on a keyboard, as it is by convention");
    INKCELL_TEST_FAIL_IF(inkcell_input_profile_key(keyboard, KEY_BACKSPACE) != INKCELL_KEY_B,
                         "...and Backspace B");
    INKCELL_TEST_FAIL_IF(strcmp(inkcell_input_profile_default()->name, "brick") != 0,
                         "the Brick's should stay the default");
    record_success(test_name);
}

/* The triggers had no key on a keyboard at all; Home and End are theirs now. */
INKCELL_TEST_CASE(input_profile_home_and_end_are_the_triggers, unit) {
    INKCELL_TEST_FAIL_IF(inkcell_input_map_key(KEY_HOME) != INKCELL_KEY_L2, "Home should be L2");
    INKCELL_TEST_FAIL_IF(inkcell_input_map_key(KEY_END) != INKCELL_KEY_R2, "End should be R2");
    record_success(test_name);
}

/*
 * A backend's preference is used when the environment names nothing, and the environment wins
 * when it does. The caps follow whichever was chosen: a pair a keyboard draws names its keys.
 */
INKCELL_TEST_CASE(input_profile_a_preference_yields_to_the_environment, unit) {
    profile_test_reset();
    INKCELL_TEST_FAIL_IF_CLEANUP(strcmp(inkcell_button_cap(INKCELL_BUTTON_SHOULDERS), "L/R") != 0,
                                 profile_test_reset(), "with nothing chosen the Brick's caps show");

    INKCELL_TEST_FAIL_IF_CLEANUP(!inkcell_input_profile_prefer("keyboard"), profile_test_reset(),
                                 "a profile the registry holds should be preferable");
    INKCELL_TEST_FAIL_IF_CLEANUP(
        strcmp(inkcell_button_cap(INKCELL_BUTTON_SHOULDERS), "PGUP/PGDN") != 0 ||
            strcmp(inkcell_button_cap(INKCELL_BUTTON_TRIGGERS), "HOME/END") != 0,
        profile_test_reset(), "a keyboard's pairs should name the keys it has");

    profile_test_setenv("INKWELL_INPUT_PROFILE", "xbox");
    inkcell_input_profile_reload();
    INKCELL_TEST_FAIL_IF_CLEANUP(strcmp(inkcell_input_profile_from_env()->name, "xbox") != 0,
                                 profile_test_reset(), "the environment should win");

    profile_test_setenv("INKWELL_INPUT_PROFILE", "no-such-pad");
    inkcell_input_profile_reload();
    INKCELL_TEST_FAIL_IF_CLEANUP(strcmp(inkcell_input_profile_from_env()->name, "keyboard") != 0,
                                 profile_test_reset(),
                                 "an unknown name should fall back to the preference");

    INKCELL_TEST_FAIL_IF_CLEANUP(inkcell_input_profile_prefer("no-such-pad"), profile_test_reset(),
                                 "an unknown preference should be refused");
    profile_test_setenv("INKWELL_INPUT_PROFILE", NULL);
    (void)inkcell_input_profile_prefer(NULL);
    INKCELL_TEST_FAIL_IF_CLEANUP(strcmp(inkcell_input_profile_from_env()->name, "brick") != 0,
                                 profile_test_reset(), "no preference should put the Brick back");
    profile_test_reset();
    record_success(test_name);
}
