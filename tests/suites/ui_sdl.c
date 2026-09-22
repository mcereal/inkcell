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
#include "inkcell/ui/widgets.h"

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

#include "inkcell/ui/input_codes.h"
#include "inkcell/ui/stack.h"
#include <SDL.h>

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
    /* Kept so a case can turn the pump by hand: the timer read in it does not block, so calling
       it is one drain of the queue, which is what the loop would have done on a tick. */
    int (*callback)(int, uint32_t, void *);
    void *userdata;
};

static int sdl_test_add_fd(void *ctx, int fd, int (*callback)(int, uint32_t, void *),
                           void *userdata) {
    struct sdl_test_host *const host = (struct sdl_test_host *)ctx;
    host->callback = callback;
    host->userdata = userdata;
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

/* One drain of the event queue, which is what the loop would have done on a tick: the timer
   read inside the pump does not block, so a case may turn it by hand. */
static void sdl_pump(struct sdl_test_host *host) {
    (void)host->callback(host->added_fd, 0U, host->userdata);
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
 * A drag re-measures the frame, and the width class follows it.
 *
 * This is the property the window backend was missing, and it is why it is worth a case of its
 * own rather than a line in the damage one. Everything a modern layout decides about room -
 * whether there is space for a rail, whether two panes fit, how wide a column of text is
 * allowed to get - is decided from the surface's own geometry (inkcell/ui/stack.h). A backend
 * that scaled a fixed frame to the window answered every one of those questions with the
 * handheld panel's answer forever, however wide the window was dragged.
 *
 * So what is held here is the whole chain, end to end: an SDL resize event reaches the pump,
 * the surface is reallocated, the application is asked for a frame, and the frame it draws is
 * in a *different width class* from the one before it. The last of those is the one that
 * matters - the rest is plumbing that could be right while the layout still never noticed.
 *
 * The rest of the case is the three ways a resize must not misbehave: a size that did not
 * change costs nothing (SDL sends SDL_WINDOWEVENT_SIZE_CHANGED for a move between displays
 * too), a window dragged to nothing stops at the floor, and the mouse forgets a layout that is
 * no longer on screen.
 */
#define SDL_RESIZE_W 640U
#define SDL_RESIZE_H 480U

struct sdl_resize_app {
    unsigned renders;
    unsigned drops;
    uint32_t width;
    uint32_t height;
    enum inkcell_width_class class;
};

static void sdl_resize_render(struct inkcell_draw_state *state, const void *snapshot, void *ctx) {
    struct sdl_resize_app *const app = (struct sdl_resize_app *)ctx;
    (void)snapshot;
    app->renders += 1U;
    app->width = state->surface.width;
    app->height = state->surface.height;
    /* Read the way a screen reads it, through the layout, rather than off the surface - so
       what this case holds is the answer a renderer would actually get. */
    const struct inkcell_fb_layout layout = inkcell_fb_layout_begin(state, true, false);
    app->class = layout.width;
    inkcell_fb_clear(state, inkcell_fb_color(state, INKCELL_COLOR_BG));
}

static unsigned sdl_resize_frames_asked;

static void sdl_resize_request_frame(void *userdata) {
    (void)userdata;
    sdl_resize_frames_asked += 1U;
}

/* What an application memoised against the old geometry, and the count that says it was asked
   to let go of it. See struct inkcell_fb_app's `drop_caches`. */
static void sdl_resize_drop_caches(struct inkcell_draw_state *state, void *ctx) {
    struct sdl_resize_app *const app = (struct sdl_resize_app *)ctx;
    (void)state;
    app->drops += 1U;
}

static void sdl_push_resize(int width, int height) {
    SDL_Event event;
    memset(&event, 0, sizeof event);
    event.type = SDL_WINDOWEVENT;
    event.window.type = SDL_WINDOWEVENT;
    event.window.event = SDL_WINDOWEVENT_SIZE_CHANGED;
    event.window.data1 = width;
    event.window.data2 = height;
    SDL_PushEvent(&event);
}

INKCELL_TEST_CASE(sdl_resize_remeasures_and_moves_the_width_class, unit) {
    setenv("SDL_VIDEODRIVER", "dummy", 0);
    INKCELL_TEST_FAIL_IF(!inkcell_backend_sdl_is_available(), "the dummy driver must start");

    struct sdl_resize_app app = {0};
    struct inkcell_fb_app vtable = {
        .ctx = &app, .render = sdl_resize_render, .drop_caches = sdl_resize_drop_caches};
    struct sdl_test_host host = {.added_fd = -1, .removed_fd = -1, .adds = 0U, .stops = 0U};
    struct inkcell_backend_sdl_context context = {
        .app = &vtable,
        .host = {.ctx = &host,
                 .add_fd = sdl_test_add_fd,
                 .remove_fd = sdl_test_remove_fd,
                 .request_stop = sdl_test_request_stop},
        .title = "inkcell tests",
        .width = SDL_RESIZE_W,
        .height = SDL_RESIZE_H,
        .request_frame = sdl_resize_request_frame,
    };
    sdl_resize_frames_asked = 0U;

    const struct inkcell_backend *const backend = inkcell_backend_sdl();
    void *state = NULL;
    INKCELL_TEST_FAIL_IF(backend->init(&state, &context) != 0, "the dummy driver must open");

    const int snapshot = 0;
    backend->present(state, &snapshot, &context);
    INKCELL_TEST_FAIL_IF_CLEANUP(app.width != SDL_RESIZE_W || app.height != SDL_RESIZE_H,
                                 backend->shutdown(state, &context),
                                 "the first frame is drawn at the size the window opened at");
    INKCELL_TEST_FAIL_IF_CLEANUP(app.class != INKCELL_WIDTH_COMPACT,
                                 backend->shutdown(state, &context),
                                 "640 pixels at the body scale is the compact class");

    /* Wide enough for two readable columns, which is a different class by construction. */
    sdl_push_resize(2400, 900);
    sdl_pump(&host);
    INKCELL_TEST_FAIL_IF_CLEANUP(sdl_resize_frames_asked != 1U, backend->shutdown(state, &context),
                                 "a resize must ask the application for a frame it cannot draw");
    /*
     * And must tell it to forget what it measured for the old surface first.
     *
     * A resize is the geometry change struct inkcell_fb_app's `drop_caches` is named for. An
     * application that memoised a layout against the old width and was never asked to let go of
     * it draws the requested frame to that old width - on a window that is now a different one,
     * and with nothing to make it recover.
     */
    INKCELL_TEST_FAIL_IF_CLEANUP(app.drops != 1U, backend->shutdown(state, &context),
                                 "a resize must drop what the application memoised");

    backend->present(state, &snapshot, &context);
    INKCELL_TEST_FAIL_IF_CLEANUP(app.width != 2400U || app.height != 900U,
                                 backend->shutdown(state, &context),
                                 "the frame after a resize is drawn at the window's new size");
    INKCELL_TEST_FAIL_IF_CLEANUP(app.class != INKCELL_WIDTH_EXPANDED,
                                 backend->shutdown(state, &context),
                                 "and the layout it was handed is in the class that width is");

    /* The same size again is not a resize. SDL sends this event for a move between displays. */
    const unsigned asked = sdl_resize_frames_asked;
    const unsigned renders = app.renders;
    sdl_push_resize(2400, 900);
    sdl_pump(&host);
    INKCELL_TEST_FAIL_IF_CLEANUP(sdl_resize_frames_asked != asked,
                                 backend->shutdown(state, &context),
                                 "a size that did not change must not cost a reallocation");
    INKCELL_TEST_FAIL_IF_CLEANUP(app.drops != 1U, backend->shutdown(state, &context),
                                 "nor throw away caches that are still good");
    INKCELL_TEST_FAIL_IF_CLEANUP(app.renders != renders, backend->shutdown(state, &context),
                                 "nor a frame");

    /* And a window dragged at nothing stops somewhere a frame is still a frame. */
    sdl_push_resize(4, 4);
    sdl_pump(&host);
    backend->present(state, &snapshot, &context);
    INKCELL_TEST_FAIL_IF_CLEANUP(app.width < 64U || app.height < 64U,
                                 backend->shutdown(state, &context),
                                 "a resize to nothing must stop at the floor, not hand over one");

    backend->shutdown(state, &context);
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

/*
 * The mouse, end to end: an SDL event in, a key or a click out.
 *
 * The frame is a real action bar and one row the "application" registered itself, so what is
 * held here is that a window with no pointer code of its own is clickable through its hints,
 * that anything else it registered reaches `on_click`, and that the wheel and the thumb button
 * arrive as the keys they stand for.
 */
#define SDL_POINTER_W 640U
#define SDL_POINTER_H 480U
#define SDL_POINTER_ROW 7U

struct sdl_pointer_app {
    struct inkcell_focus_item storage[16];
    struct inkcell_focus_map map;
    struct inkcell_fb_rect row;
    struct inkcell_focus_rect hint;
};

static void sdl_pointer_render(struct inkcell_draw_state *state, const void *snapshot, void *ctx) {
    struct sdl_pointer_app *const app = (struct sdl_pointer_app *)ctx;
    (void)snapshot;
    inkcell_focus_begin(&app->map, app->storage, 16U);
    inkcell_fb_set_focus_map(state, &app->map);
    inkcell_fb_clear(state, inkcell_fb_color(state, INKCELL_COLOR_BG));
    const struct inkcell_fb_layout layout = inkcell_fb_layout_begin(state, true, false);
    app->row = (struct inkcell_fb_rect){.x = 0, .y = layout.body_y, .w = 200, .h = 40};
    inkcell_fb_focus_register(state, SDL_POINTER_ROW, &app->row);
    const struct inkcell_button_action items[] = {
        {.button = INKCELL_BUTTON_B, .label = INKCELL_STR_KEY_CANCEL},
    };
    const struct inkcell_fb_action_bar bar = {.items = items, .count = 1U};
    inkcell_fb_draw_action_bar(state, &layout, &bar);
    (void)inkcell_focus_rect_of(&app->map, INKCELL_FOCUS_KEY(INKCELL_KEY_B), &app->hint);
}

struct sdl_pointer_heard {
    enum inkcell_key keys[16];
    unsigned key_count;
    uint32_t clicked;
    unsigned clicks;
    uint32_t context;
    unsigned contexts;
};

static void sdl_pointer_on_key(void *userdata, enum inkcell_key key) {
    struct sdl_pointer_heard *const heard = (struct sdl_pointer_heard *)userdata;
    if (heard->key_count < 16U) {
        heard->keys[heard->key_count++] = key;
    }
}

static void sdl_pointer_on_click(void *userdata, uint32_t target, int x, int y) {
    struct sdl_pointer_heard *const heard = (struct sdl_pointer_heard *)userdata;
    (void)x;
    (void)y;
    heard->clicked = target;
    heard->clicks += 1U;
}

static void sdl_pointer_on_context(void *userdata, uint32_t target, int x, int y) {
    struct sdl_pointer_heard *const heard = (struct sdl_pointer_heard *)userdata;
    (void)x;
    (void)y;
    heard->context = target;
    heard->contexts += 1U;
}

static void sdl_push_button(Uint32 type, Uint8 button, int x, int y) {
    SDL_Event event;
    memset(&event, 0, sizeof event);
    event.type = type;
    event.button.type = type;
    event.button.button = button;
    event.button.state = type == SDL_MOUSEBUTTONDOWN ? SDL_PRESSED : SDL_RELEASED;
    event.button.x = x;
    event.button.y = y;
    SDL_PushEvent(&event);
}

static void sdl_click(int x, int y) {
    sdl_push_button(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_LEFT, x, y);
    sdl_push_button(SDL_MOUSEBUTTONUP, SDL_BUTTON_LEFT, x, y);
}

INKCELL_TEST_CASE(sdl_mouse_clicks_hints_and_hands_on_the_rest, unit) {
    setenv("SDL_VIDEODRIVER", "dummy", 0);
    INKCELL_TEST_FAIL_IF(!inkcell_backend_sdl_is_available(), "the dummy driver must start");

    struct sdl_pointer_app app;
    memset(&app, 0, sizeof app);
    struct inkcell_fb_app vtable = {.ctx = &app, .render = sdl_pointer_render};
    struct sdl_test_host host = {.added_fd = -1, .removed_fd = -1};
    struct sdl_pointer_heard heard;
    memset(&heard, 0, sizeof heard);
    struct inkcell_backend_sdl_context context = {
        .app = &vtable,
        .host = {.ctx = &host,
                 .add_fd = sdl_test_add_fd,
                 .remove_fd = sdl_test_remove_fd,
                 .request_stop = sdl_test_request_stop},
        .on_key = sdl_pointer_on_key,
        .key_userdata = &heard,
        .on_click = sdl_pointer_on_click,
        .on_context = sdl_pointer_on_context,
        .click_userdata = &heard,
        .title = "inkcell tests",
        .width = SDL_POINTER_W,
        .height = SDL_POINTER_H,
    };
    const struct inkcell_backend *const backend = inkcell_backend_sdl();
    void *state = NULL;
    INKCELL_TEST_FAIL_IF(backend->init(&state, &context) != 0, "the dummy driver must open");
    INKCELL_TEST_FAIL_IF_CLEANUP(host.callback == NULL, backend->shutdown(state, &context),
                                 "the pump must be on the host's loop");
    const int snapshot = 0;
    backend->present(state, &snapshot, &context);
    SDL_PumpEvents();
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);

    INKCELL_TEST_FAIL_IF_CLEANUP(app.hint.w <= 0, backend->shutdown(state, &context),
                                 "the frame should have registered its hint");
    /* The renderer's map goes away once the frame is drawn - as a map on its stack would. The
       click has to be answered from what the backend kept, not from the renderer's memory. */
    inkcell_focus_begin(&app.map, NULL, 0U);
    sdl_click(app.hint.x + app.hint.w / 2, app.hint.y + app.hint.h / 2);
    sdl_pump(&host);
    INKCELL_TEST_FAIL_IF_CLEANUP(heard.key_count != 1U || heard.keys[0] != INKCELL_KEY_B,
                                 backend->shutdown(state, &context),
                                 "clicking the B hint must be a press of B");

    sdl_click(app.row.x + 10, app.row.y + 10);
    sdl_pump(&host);
    INKCELL_TEST_FAIL_IF_CLEANUP(heard.clicks != 1U || heard.clicked != SDL_POINTER_ROW ||
                                     heard.key_count != 1U,
                                 backend->shutdown(state, &context),
                                 "a click on a row the app registered must reach on_click only");

    sdl_push_button(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_LEFT, app.row.x + 10, app.row.y + 10);
    sdl_push_button(SDL_MOUSEBUTTONUP, SDL_BUTTON_LEFT, app.hint.x + 1, app.hint.y + 1);
    sdl_pump(&host);
    INKCELL_TEST_FAIL_IF_CLEANUP(heard.clicks != 1U || heard.key_count != 1U,
                                 backend->shutdown(state, &context),
                                 "dragging off before letting go must do nothing");

    SDL_Event wheel;
    memset(&wheel, 0, sizeof wheel);
    wheel.type = SDL_MOUSEWHEEL;
    wheel.wheel.type = SDL_MOUSEWHEEL;
    wheel.wheel.y = -2;
#if SDL_VERSION_ATLEAST(2, 0, 18)
    wheel.wheel.preciseY = -2.0f;
#endif
    SDL_PushEvent(&wheel);
    sdl_push_button(SDL_MOUSEBUTTONUP, SDL_BUTTON_X1, 0, 0);
    sdl_pump(&host);
    INKCELL_TEST_FAIL_IF_CLEANUP(heard.key_count != 4U || heard.keys[1] != INKCELL_KEY_DOWN ||
                                     heard.keys[2] != INKCELL_KEY_DOWN ||
                                     heard.keys[3] != INKCELL_KEY_B,
                                 backend->shutdown(state, &context),
                                 "two notches down are two downs, and the thumb button is B");

    sdl_push_button(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_RIGHT, app.row.x + 10, app.row.y + 10);
    sdl_push_button(SDL_MOUSEBUTTONUP, SDL_BUTTON_RIGHT, app.row.x + 10, app.row.y + 10);
    sdl_pump(&host);
    INKCELL_TEST_FAIL_IF_CLEANUP(
        heard.contexts != 1U || heard.context != SDL_POINTER_ROW || heard.clicks != 1U,
        backend->shutdown(state, &context), "a right-click on a row is its context, never a click");

    sdl_push_button(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_RIGHT, app.hint.x + 1, app.hint.y + 1);
    sdl_push_button(SDL_MOUSEBUTTONUP, SDL_BUTTON_RIGHT, app.hint.x + 1, app.hint.y + 1);
    sdl_pump(&host);
    INKCELL_TEST_FAIL_IF_CLEANUP(
        heard.contexts != 2U || heard.context != INKCELL_FOCUS_KEY(INKCELL_KEY_B) ||
            heard.key_count != 4U,
        backend->shutdown(state, &context), "a right-click on a hint is handed on, not pressed");

    SDL_SetModState(KMOD_LCTRL);
    sdl_push_button(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_LEFT, app.row.x + 10, app.row.y + 10);
    sdl_pump(&host);
    SDL_SetModState(KMOD_NONE);
    sdl_push_button(SDL_MOUSEBUTTONUP, SDL_BUTTON_LEFT, app.row.x + 10, app.row.y + 10);
    sdl_pump(&host);
    INKCELL_TEST_FAIL_IF_CLEANUP(heard.contexts != 3U || heard.context != SDL_POINTER_ROW ||
                                     heard.clicks != 1U,
                                 backend->shutdown(state, &context),
                                 "a control-click is the secondary button, released or not");

    backend->shutdown(state, &context);
    record_success(test_name);
}

#endif /* INKCELL_HAVE_SDL */
