#define _POSIX_C_SOURCE 200809L

/*
 * A finger's taps and drags, over the pointer rules. See include/inkcell/ui/touch.h.
 */

#include "inkcell/ui/touch.h"

#include "inkcell/ui/backend.h"
#include "inkcell/ui/fb_draw.h"
#include "inkcell/ui/latency.h"
#include "inkwell/base/env.h"

#include <stddef.h>

void inkcell_touch_init(struct inkcell_touch *touch) {
    if (touch == NULL) {
        return;
    }
    *touch = (struct inkcell_touch){0};
    inkcell_pointer_reset(&touch->pointer);
    const long panel =
        inkwell_env_int("KMS_ROTATE", 0, 270, inkwell_env_int("FB_ROTATE", 0, 270, 0));
    const long rotation = inkwell_env_int("TOUCH_ROTATE", 0, 270, panel);
    touch->rotation = rotation % 90 == 0 ? (unsigned)rotation : 0U;
}

/* One axis fraction as a pixel of `size`, counted from the far end when `flip`. */
static int inkcell_touch_scale(uint32_t n, uint32_t size, bool flip) {
    if (n >= INKCELL_TOUCH_UNIT) {
        n = INKCELL_TOUCH_UNIT - 1U;
    }
    if (flip) {
        n = INKCELL_TOUCH_UNIT - 1U - n;
    }
    return (int)(((uint64_t)n * size) / INKCELL_TOUCH_UNIT);
}

bool inkcell_touch_to_frame(uint32_t nx, uint32_t ny, uint32_t width, uint32_t height,
                            unsigned rotation, int *x, int *y) {
    if (width == 0U || height == 0U || x == NULL || y == NULL) {
        return false;
    }
    switch (rotation) {
    case 0U:
        *x = inkcell_touch_scale(nx, width, false);
        *y = inkcell_touch_scale(ny, height, false);
        return true;
    case 90U: /* the digitizer's top edge is the frame's right */
        *x = inkcell_touch_scale(ny, width, true);
        *y = inkcell_touch_scale(nx, height, false);
        return true;
    case 180U:
        *x = inkcell_touch_scale(nx, width, true);
        *y = inkcell_touch_scale(ny, height, true);
        return true;
    case 270U: /* the digitizer's top edge is the frame's left */
        *x = inkcell_touch_scale(ny, width, false);
        *y = inkcell_touch_scale(nx, height, true);
        return true;
    default:
        return false;
    }
}

/* The default slop and step, as shares of the short side: 24 and 90 pixels on a 1080-wide
   phone, which is about 1.5 mm and a list row at its density, and never under a few pixels. */
static int inkcell_touch_share(int set, uint32_t width, uint32_t height, uint32_t divisor,
                               int floor) {
    if (set > 0) {
        return set;
    }
    const uint32_t short_side = width < height ? width : height;
    const int share = (int)(short_side / divisor);
    return share > floor ? share : floor;
}

struct inkcell_touch_result inkcell_touch_handle(struct inkcell_touch *touch,
                                                 const struct inkcell_focus_map *map,
                                                 enum inkcell_touch_phase phase, int x, int y,
                                                 uint32_t width, uint32_t height) {
    struct inkcell_touch_result result = {.kind = INKCELL_TOUCH_NONE, .x = x, .y = y};
    result.press.kind = INKCELL_POINTER_NONE;
    result.press.key = INKCELL_KEY_NONE;
    result.press.target = INKCELL_FOCUS_NONE;
    if (touch == NULL) {
        return result;
    }
    const int slop = inkcell_touch_share(touch->slop_px, width, height, 45U, 4);
    const int step = inkcell_touch_share(touch->step_px, width, height, 12U, 16);

    if (phase == INKCELL_TOUCH_DOWN) {
        /* A down with one already held is the reader losing an up; this one starts afresh. */
        touch->down = true;
        touch->dragging = false;
        touch->start_x = x;
        touch->start_y = y;
        touch->last_y = y;
        inkcell_pointer_reset(&touch->pointer);
        inkcell_pointer_down(&touch->pointer, map, x, y);
        return result;
    }
    if (!touch->down) {
        return result;
    }
    if (phase == INKCELL_TOUCH_CANCEL) {
        touch->down = false;
        touch->dragging = false;
        inkcell_pointer_reset(&touch->pointer);
        return result;
    }

    if (!touch->dragging) {
        const int dx = x - touch->start_x;
        const int dy = y - touch->start_y;
        if (dx > slop || dx < -slop || dy > slop || dy < -slop) {
            /* Measured from here rather than from where it went down, so the slop the finger
               had to cross is not paid out as a row the moment the drag starts. */
            touch->dragging = true;
            touch->last_y = y;
            inkcell_pointer_reset(&touch->pointer);
        }
    }

    if (touch->dragging) {
        const int moved = y - touch->last_y;
        if (moved != 0) {
            const int steps = inkcell_pointer_wheel(&touch->pointer, (float)moved / (float)step);
            touch->last_y = y;
            if (steps != 0) {
                result.kind = INKCELL_TOUCH_SCROLL;
                result.steps = steps;
            }
        }
        if (phase == INKCELL_TOUCH_UP) {
            touch->down = false;
            touch->dragging = false;
            touch->pointer.wheel = 0.0f;
        }
        return result;
    }

    if (phase == INKCELL_TOUCH_UP) {
        touch->down = false;
        /* Released where it went down, give or take the slop: the box under the press is the
           one the reader meant, so the release is answered there rather than at a point a
           wobble may have carried over the edge. */
        result.kind = INKCELL_TOUCH_TAP;
        result.press = inkcell_pointer_up(&touch->pointer, map, touch->start_x, touch->start_y);
        if (result.press.kind != INKCELL_POINTER_NONE) {
            inkcell_latency_press();
        }
    }
    return result;
}

struct inkcell_touch_result inkcell_touch_feed(struct inkcell_touch *touch,
                                               const struct inkcell_backend *backend, void *state,
                                               void *userdata, enum inkcell_touch_phase phase,
                                               uint32_t nx, uint32_t ny) {
    struct inkcell_touch_result none = {.kind = INKCELL_TOUCH_NONE};
    none.press.kind = INKCELL_POINTER_NONE;
    none.press.key = INKCELL_KEY_NONE;
    none.press.target = INKCELL_FOCUS_NONE;
    struct inkcell_surface frame = {0};
    if (touch == NULL || backend == NULL || backend->frame == NULL ||
        !backend->frame(state, userdata, &frame)) {
        return none;
    }
    int x = 0;
    int y = 0;
    if (!inkcell_touch_to_frame(nx, ny, frame.width, frame.height, touch->rotation, &x, &y)) {
        return none;
    }
    const struct inkcell_focus_map *map =
        backend->focus_map != NULL ? backend->focus_map(state, userdata) : NULL;
    return inkcell_touch_handle(touch, map, phase, x, y, frame.width, frame.height);
}
