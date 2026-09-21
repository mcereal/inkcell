#define _POSIX_C_SOURCE 200809L

/*
 * The headless backend: the off-screen capture page, presented against the real clock. See
 * inkcell/ui/headless.h.
 *
 * It is a backend rather than a flag on the capture because the two answer to different
 * people. The capture is a harness, and everything about a frame is named by the scene that
 * asked for it; this is what an application runs when there is no panel, and it takes the
 * panel's defaults - the environment's theme, the monotonic clock - so that what it draws is
 * what the device would have.
 */

#include "inkcell/ui/fb_draw.h"

#include "inkcell/ui/fb_capture.h"
#include "inkcell/ui/headless.h"
#include "inkcell/ui/latency.h"

#include "inkwell/base/log.h"
#include "inkwell/base/time.h"

#include <errno.h>

/* The page, and whether a frame has been drawn into it yet - which is what frame() needs to
   know, since the capture's page reads as black before one has. */
struct inkcell_headless {
    struct inkcell_capture *capture;
    bool presented;
};

static int inkcell_backend_headless_init(void **state_out, void *userdata) {
    const struct inkcell_backend_headless_context *context =
        (const struct inkcell_backend_headless_context *)userdata;
    if (context == NULL || state_out == NULL) {
        return -EINVAL;
    }

    static struct inkcell_headless panel;
    panel = (struct inkcell_headless){0};
    const uint32_t width = context->width != 0U ? context->width : INKCELL_CAPTURE_WIDTH;
    const uint32_t height = context->height != 0U ? context->height : INKCELL_CAPTURE_HEIGHT;
    const int opened = inkcell_capture_open(&panel.capture, width, height, 0);
    if (opened < 0) {
        return opened;
    }

    struct inkcell_draw_state *const state = inkcell_capture_state(panel.capture);
    inkcell_fb_state_apply_theme_from_env(state);
    inkcell_fb_set_app(state, context->app);
    inkwell_log_info("ui", "Headless UI backend active (%ux%u in memory, theme %s at scale %d)",
                     width, height, state->theme->id, state->scale);

    *state_out = &panel;
    return 0;
}

static void inkcell_backend_headless_shutdown(void *state_ptr, void *userdata) {
    (void)userdata;
    struct inkcell_headless *const panel = (struct inkcell_headless *)state_ptr;
    if (panel != NULL) {
        inkcell_capture_close(panel->capture);
        panel->capture = NULL;
        panel->presented = false;
    }
}

static void inkcell_backend_headless_present(void *state_ptr, const void *snapshot,
                                             void *userdata) {
    (void)userdata;
    struct inkcell_headless *const panel = (struct inkcell_headless *)state_ptr;
    if (panel == NULL || panel->capture == NULL || snapshot == NULL) {
        return;
    }
    struct inkcell_draw_state *const state = inkcell_capture_state(panel->capture);
    /* The clock the device draws against, read once a frame as the fb backend reads it. */
    inkcell_fb_state_set_now(state, inkwell_time_monotonic_ms());
    inkcell_latency_frame_begin();
    inkcell_fb_render(state, snapshot);
    panel->presented = true;
    const size_t page_bytes = (size_t)state->surface.stride * state->surface.height;
    inkcell_latency_frame_drawn(page_bytes);
    inkcell_latency_frame_end(page_bytes);
}

static const struct inkcell_draw_state *inkcell_headless_state(void *state_ptr) {
    const struct inkcell_headless *const panel = (const struct inkcell_headless *)state_ptr;
    return panel != NULL && panel->capture != NULL ? inkcell_capture_state(panel->capture) : NULL;
}

static bool inkcell_backend_headless_animating(void *state_ptr, void *userdata) {
    (void)userdata;
    const struct inkcell_draw_state *const state = inkcell_headless_state(state_ptr);
    return state != NULL && inkcell_fb_state_animating(state);
}

static uint32_t inkcell_backend_headless_page_rows(void *state_ptr, void *userdata) {
    (void)userdata;
    const struct inkcell_draw_state *const state = inkcell_headless_state(state_ptr);
    return state != NULL ? state->page_rows : 0U;
}

static const struct inkcell_focus_map *inkcell_backend_headless_focus_map(void *state_ptr,
                                                                          void *userdata) {
    (void)userdata;
    const struct inkcell_draw_state *const state = inkcell_headless_state(state_ptr);
    return state != NULL ? state->focus : NULL;
}

static bool inkcell_backend_headless_frame(void *state_ptr, void *userdata,
                                           struct inkcell_surface *out) {
    (void)userdata;
    const struct inkcell_headless *const panel = (const struct inkcell_headless *)state_ptr;
    const struct inkcell_draw_state *const state = inkcell_headless_state(state_ptr);
    if (state == NULL || out == NULL || !panel->presented) {
        return false;
    }
    *out = state->surface;
    return true;
}

static const struct inkcell_backend k_headless_backend = {
    .name = "headless",
    .init = inkcell_backend_headless_init,
    .shutdown = inkcell_backend_headless_shutdown,
    .present = inkcell_backend_headless_present,
    .animating = inkcell_backend_headless_animating,
    .page_rows = inkcell_backend_headless_page_rows,
    .focus_map = inkcell_backend_headless_focus_map,
    .frame = inkcell_backend_headless_frame,
};

const struct inkcell_backend *inkcell_backend_headless(void) {
    return &k_headless_backend;
}
