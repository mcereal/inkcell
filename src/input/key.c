#define _POSIX_C_SOURCE 200809L

/* The names of the keys. See inkcell/ui/key.h. */

#include "inkcell/ui/key.h"

#include <stddef.h>
#include <strings.h>

static const char *const k_key_names[] = {
    [INKCELL_KEY_UP] = "up",       [INKCELL_KEY_DOWN] = "down",     [INKCELL_KEY_LEFT] = "left",
    [INKCELL_KEY_RIGHT] = "right", [INKCELL_KEY_A] = "a",           [INKCELL_KEY_B] = "b",
    [INKCELL_KEY_X] = "x",         [INKCELL_KEY_Y] = "y",           [INKCELL_KEY_L1] = "l1",
    [INKCELL_KEY_R1] = "r1",       [INKCELL_KEY_L2] = "l2",         [INKCELL_KEY_R2] = "r2",
    [INKCELL_KEY_START] = "start", [INKCELL_KEY_SELECT] = "select", [INKCELL_KEY_B_HELD] = "hold-b",
};

enum inkcell_key inkcell_key_from_name(const char *name) {
    if (name == NULL) {
        return INKCELL_KEY_NONE;
    }
    for (size_t i = 0U; i < sizeof k_key_names / sizeof k_key_names[0]; ++i) {
        if (k_key_names[i] != NULL && strcasecmp(k_key_names[i], name) == 0) {
            return (enum inkcell_key)i;
        }
    }
    return INKCELL_KEY_NONE;
}

const char *inkcell_key_name(enum inkcell_key key) {
    const size_t index = (size_t)key;
    return index < sizeof k_key_names / sizeof k_key_names[0] ? k_key_names[index] : NULL;
}
