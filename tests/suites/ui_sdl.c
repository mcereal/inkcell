#define _POSIX_C_SOURCE 200809L

/*
 * The window backend, without a window.
 *
 * SDL ships a video driver called "dummy" that creates windows and renderers which work in
 * every respect except being visible, so the whole of this backend - the texture, the damage
 * rectangles that go into it, the event pump's registration - can be exercised in a container
 * with no display anywhere near it. That is the only reason these cases exist: a backend whose
 * correctness could only be checked by looking at a screen would be a backend nothing checks.
 *
 * What is deliberately *not* here is a comparison of the pixels against the fb backend's. The
 * rasteriser is the same code drawing into the same kind of surface - that is the whole claim
 * of the refactor this backend sits on - and the golden sheet already holds it, sixty pages at
 * a time. Repeating it here would be testing inkcell_fb_render() a third time and this backend
 * not at all. What *is* this backend's own is everything after the render, so that is what the
 * present case measures: which bytes it decided to hand over, and when it decided to hand over
 * none.
 */

#include "framework/inkcell_test.h"

#include "inkcell/ui/fb_draw.h"
#include "inkcell/ui/input.h"
#include "inkcell/ui/latency.h"
#include "inkcell/ui/sdl.h"

#include <stdlib.h>
#include <string.h>

/* True whichever way the build went: the backend always exists, and says which it is. */
INKCELL_TEST_CASE(sdl_backend_is_always_offered, unit) {
    const struct inkcell_backend *const backend = inkcell_backend_sdl();
    INKCELL_TEST_FAIL_IF(backend == NULL, "the SDL backend must exist in every build");
    INKCELL_TEST_FAIL_IF(backend->name == NULL || strcmp(backend->name, "sdl") != 0,
                         "...and must name itself");
    INKCELL_TEST_FAIL_IF(backend->init == NULL, "...and must have something to refuse with");
    record_success(test_name);
}

#ifndef INKCELL_HAVE_SDL

/*
 * Built without SDL2.
 *
 * The stub is not a placeholder, it is the contract: an application picks a backend by asking
 * whether it is available and never by asking how inkcell was compiled, so the no it gets here
 * has to be the same shape as the no it gets from a missing /dev/fb0.
 */
INKCELL_TEST_CASE(sdl_without_the_library_refuses_rather_than_pretends, unit) {
    INKCELL_TEST_FAIL_IF(inkcell_backend_sdl_is_available(),
                         "a build with no SDL2 must not claim a window backend");
    void *state = NULL;
    struct inkcell_backend_sdl_context context = {0};
    INKCELL_TEST_FAIL_IF(inkcell_backend_sdl()->init(&state, &context) >= 0,
                         "...and must refuse to open one");
    INKCELL_TEST_FAIL_IF(state != NULL, "...leaving nothing behind");
    INKCELL_TEST_FAIL_IF(inkcell_sdl_evdev_code(0) != 0U, "...and mapping no key");
    record_success(test_name);
}

#else /* INKCELL_HAVE_SDL */

#include <SDL.h>
#include <linux/input-event-codes.h>

#define SDL_TEST_WIDTH 64U
#define SDL_TEST_HEIGHT 32U

/*
 * A loop that records rather than runs.
 *
 * All this backend asks of a host is a descriptor watched and later dropped, so a host that
 * remembers which is enough to hold it to that - and a real epoll loop in a unit test would be
 * a second thing that could fail.
 */
struct sdl_test_host {
    int added_fd;
    int removed_fd;
    unsigned adds;
    unsigned stops;
};

static int sdl_test_add_fd(void *ctx, int fd, int (*callback)(int, uint32_t, void *),
                           void *userdata) {
    struct sdl_test_host *const host = (struct sdl_test_host *)ctx;
    (void)callback;
    (void)userdata;
    host->added_fd = fd;
    host->adds += 1U;
    return 0;
}

static void sdl_test_remove_fd(void *ctx, int fd) {
    ((struct sdl_test_host *)ctx)->removed_fd = fd;
}

static void sdl_test_request_stop(void *ctx) {
    ((struct sdl_test_host *)ctx)->stops += 1U;
}

/*
 * A renderer that paints rows rather than a UI.
 *
 * The frame is written straight into the surface so a case can say exactly which rows changed
 * and then check exactly how many bytes the backend handed over. Anything drawn through the
 * component set would be a picture whose damage nobody could predict, which is the golden
 * sheet's job and not this one's.
 */
struct sdl_test_app {
    unsigned renders;
    uint32_t fill; /* every row gets this */
    int dirty_row; /* ...except this one, which gets `fill` inverted. -1 for none. */
};

static void sdl_test_render(struct inkcell_draw_state *state, const void *snapshot, void *ctx) {
    struct sdl_test_app *const app = (struct sdl_test_app *)ctx;
    (void)snapshot;
    app->renders += 1U;
    for (uint32_t y = 0U; y < state->surface.height; ++y) {
        const uint32_t value = ((int)y == app->dirty_row) ? ~app->fill : app->fill;
        uint8_t *const row = state->surface.pixels + (size_t)y * state->surface.stride;
        for (uint32_t x = 0U; x < state->surface.width; ++x) {
            memcpy(row + (size_t)x * 4U, &value, sizeof value);
        }
    }
}

static uint64_t sdl_test_written(void) {
    struct inkcell_latency_counts counts;
    inkcell_latency_counts(&counts);
    return counts.written;
}

/*
 * Opening a window, drawing into it, and handing over only what changed.
 *
 * The three properties are the whole of what this backend adds over the rasteriser: the first
 * frame goes up whole because nothing is known about the panel yet, a frame identical to the
 * one before it goes up not at all, and a frame that changed one row hands over a band around
 * that row rather than the page. The third is the one worth a test - it is the difference
 * between a texture upload that is worth doing and one that is just a slower memcpy.
 */
INKCELL_TEST_CASE(sdl_hands_over_only_what_changed, unit) {
    /* Not an overwrite: a developer who wants to watch this case run says so with
       SDL_VIDEODRIVER=x11 and gets a window. Unset - a container, CI - is the dummy driver,
       which is the whole reason this is testable at all. */
    setenv("SDL_VIDEODRIVER", "dummy", 0);
    if (!inkcell_backend_sdl_is_available()) {
        INKCELL_TEST_FAIL_IF(true, "SDL2 is built in but no video driver would start");
    }

    struct sdl_test_app app = {.renders = 0U, .fill = 0xFF102030U, .dirty_row = -1};
    struct inkcell_fb_app vtable = {.ctx = &app, .render = sdl_test_render};
    struct sdl_test_host host = {.added_fd = -1, .removed_fd = -1, .adds = 0U, .stops = 0U};
    struct inkcell_backend_sdl_context context = {
        .app = &vtable,
        .host = {.ctx = &host,
                 .add_fd = sdl_test_add_fd,
                 .remove_fd = sdl_test_remove_fd,
                 .request_stop = sdl_test_request_stop},
        .title = "inkcell tests",
        .width = SDL_TEST_WIDTH,
        .height = SDL_TEST_HEIGHT,
    };

    const struct inkcell_backend *const backend = inkcell_backend_sdl();
    void *state = NULL;
    INKCELL_TEST_FAIL_IF(backend->init(&state, &context) != 0, "the dummy driver must open");
    INKCELL_TEST_FAIL_IF(state == NULL, "a backend that opened must hand back its state");
    INKCELL_TEST_FAIL_IF(host.adds != 1U || host.added_fd < 0,
                         "the event pump must be one descriptor on the host's loop");
    /*
     * ...and starting that pump is what declares the clock a press will be timed on.
     *
     * An SDL event has no kernel stamp behind it and is drained on the tick above, so the
     * press metric here begins later than the fb backend's does. The point of the declaration
     * is that the report says so: two runs that measured different things must not look
     * comparable just because the row has the same name.
     */
    INKCELL_TEST_FAIL_IF(inkcell_latency_clock() != INKCELL_LATENCY_CLOCK_DELIVERY,
                         "a backend with no kernel stamp must say so before it reports one");

    /* The probe is how many bytes reached the panel is observable at all; it is the same
       number the fb backend reports, which is what makes the two comparable. */
    inkcell_latency_enable();
    inkcell_latency_reset();

    const uint64_t page = (uint64_t)SDL_TEST_WIDTH * SDL_TEST_HEIGHT * 4U;
    const int snapshot = 0;

    backend->present(state, &snapshot, &context);
    INKCELL_TEST_FAIL_IF(app.renders != 1U, "present must draw exactly one frame");
    INKCELL_TEST_FAIL_IF(sdl_test_written() != page,
                         "the first frame must go up whole - nothing is known about the panel");

    const uint64_t after_first = sdl_test_written();
    backend->present(state, &snapshot, &context);
    INKCELL_TEST_FAIL_IF(app.renders != 2U, "present must draw every time it is called");
    INKCELL_TEST_FAIL_IF(sdl_test_written() != after_first,
                         "a frame identical to the last must upload nothing");

    app.dirty_row = 7;
    backend->present(state, &snapshot, &context);
    INKCELL_TEST_FAIL_IF(sdl_test_written() != after_first + SDL_TEST_WIDTH * 4U,
                         "one changed row must upload one row, not the page");

    backend->shutdown(state, &context);
    INKCELL_TEST_FAIL_IF(host.removed_fd != host.added_fd,
                         "shutdown must take the pump's descriptor back off the loop");
    inkcell_latency_reset();
    record_success(test_name);
}

/*
 * A keyboard reaches the UI through the evdev table, not around it.
 *
 * The property this holds is that there is one keyboard convention in the library rather than
 * two. inkcell_sdl_evdev_code() translates only as far as the code that src/input/input.c
 * already has an opinion about, so a quit key stays whatever <PREFIX>_QUIT_KEYS says and a cap
 * stays whatever the device profile says. A backend that mapped SDL scancodes straight to
 * `enum inkcell_key` would compile, work, and drift.
 */
INKCELL_TEST_CASE(sdl_keys_go_through_the_one_convention, unit) {
    INKCELL_TEST_FAIL_IF(inkcell_sdl_evdev_code(SDL_SCANCODE_UP) != KEY_UP,
                         "an arrow must arrive as the evdev code for it");
    INKCELL_TEST_FAIL_IF(inkcell_input_map_key(inkcell_sdl_evdev_code(SDL_SCANCODE_RETURN)) !=
                             INKCELL_KEY_A,
                         "Enter must be the A cap, as it is for a USB keyboard on the device");
    INKCELL_TEST_FAIL_IF(inkcell_input_map_key(inkcell_sdl_evdev_code(SDL_SCANCODE_BACKSPACE)) !=
                             INKCELL_KEY_B,
                         "...and Backspace the B cap");
    INKCELL_TEST_FAIL_IF(!inkcell_input_is_quit_key(inkcell_sdl_evdev_code(SDL_SCANCODE_ESCAPE)),
                         "Escape must quit by the same table every other source quits by");

    /* The three caps a keyboard could not reach before this backend needed them. */
    INKCELL_TEST_FAIL_IF(inkcell_input_map_key(inkcell_sdl_evdev_code(SDL_SCANCODE_X)) !=
                             INKCELL_KEY_X,
                         "X must be reachable from a keyboard");
    INKCELL_TEST_FAIL_IF(inkcell_input_map_key(inkcell_sdl_evdev_code(SDL_SCANCODE_F1)) !=
                             INKCELL_KEY_START,
                         "...and START");
    INKCELL_TEST_FAIL_IF(inkcell_input_map_key(inkcell_sdl_evdev_code(SDL_SCANCODE_F2)) !=
                             INKCELL_KEY_SELECT,
                         "...and SELECT");

    INKCELL_TEST_FAIL_IF(inkcell_sdl_evdev_code(SDL_SCANCODE_F12) != 0U,
                         "a key this client has no use for must map to nothing");
    record_success(test_name);
}

#endif /* INKCELL_HAVE_SDL */
