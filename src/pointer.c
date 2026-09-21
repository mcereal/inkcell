#define _POSIX_C_SOURCE 200809L

/*
 * A pointer's clicks and scrolls, over the focus map. See include/inkcell/ui/pointer.h.
 */

#include "inkcell/ui/pointer.h"

#include <stddef.h>

void inkcell_pointer_reset(struct inkcell_pointer *pointer) {
    if (pointer == NULL) {
        return;
    }
    pointer->pressed = INKCELL_FOCUS_NONE;
    pointer->down = false;
    pointer->wheel = 0.0f;
}

void inkcell_pointer_down(struct inkcell_pointer *pointer, const struct inkcell_focus_map *map,
                          int x, int y) {
    if (pointer == NULL) {
        return;
    }
    pointer->down = true;
    pointer->pressed = inkcell_focus_hit(map, x, y);
}

struct inkcell_pointer_result inkcell_pointer_up(struct inkcell_pointer *pointer,
                                                 const struct inkcell_focus_map *map, int x,
                                                 int y) {
    struct inkcell_pointer_result result = {
        .kind = INKCELL_POINTER_NONE, .key = INKCELL_KEY_NONE, .target = INKCELL_FOCUS_NONE};
    if (pointer == NULL || !pointer->down) {
        return result;
    }
    const uint32_t pressed = pointer->pressed;
    pointer->down = false;
    pointer->pressed = INKCELL_FOCUS_NONE;
    if (pressed == INKCELL_FOCUS_NONE || inkcell_focus_hit(map, x, y) != pressed) {
        return result;
    }
    result.target = pressed;
    result.x = x;
    result.y = y;
    result.key = inkcell_focus_key_of(pressed);
    result.kind = result.key != INKCELL_KEY_NONE ? INKCELL_POINTER_KEY : INKCELL_POINTER_CLICK;
    return result;
}

int inkcell_pointer_wheel(struct inkcell_pointer *pointer, float dy) {
    if (pointer == NULL || !(dy == dy)) {
        return 0;
    }
    if ((dy > 0.0f && pointer->wheel < 0.0f) || (dy < 0.0f && pointer->wheel > 0.0f)) {
        pointer->wheel = 0.0f;
    }
    float total = pointer->wheel + dy;
    const float max = (float)INKCELL_POINTER_WHEEL_MAX;
    if (total > max) {
        total = max;
    } else if (total < -max) {
        total = -max;
    }
    /* Truncated towards zero, so the carry keeps the sign of the scroll it belongs to. */
    const int steps = (int)total;
    pointer->wheel = total - (float)steps;
    return steps;
}

bool inkcell_pointer_over_target(const struct inkcell_focus_map *map, int x, int y,
                                 bool clickable) {
    const uint32_t id = inkcell_focus_hit(map, x, y);
    if (id == INKCELL_FOCUS_NONE) {
        return false;
    }
    return clickable || inkcell_focus_key_of(id) != INKCELL_KEY_NONE;
}
