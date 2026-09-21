#define _POSIX_C_SOURCE 200809L

/*
 * The window backend: the same frame, handed to a GPU instead of to /dev/fb0.
 *
 * See include/inkcell/ui/sdl.h for what this is and - more usefully - for what it is not. The
 * short version is that the rasteriser above this file has not been told anything: it draws
 * into a `struct inkcell_surface` exactly as it does for the panel, and the only difference is
 * the last step, where changed rows become texture uploads instead of memcpy's into an mmap.
 *
 * Why the frame is still drawn in ordinary RAM, when SDL offers a texture to lock and write
 * straight into: SDL_LockTexture() hands back *write-only* memory whose previous contents are
 * undefined, and this rasteriser reads the destination pixel back to blend against it - that is
 * how a rounded corner or a glyph edge gets its coverage (see the anti-aliasing note in
 * fb_draw.c). Locking would make every read a read of nothing. The fb backend keeps a draw
 * buffer for the same reason, one layer down: reading an mmap of the display is slow where
 * reading a locked texture is merely wrong.
 */

#include "inkcell/ui/sdl.h"

#include "inkcell/ui/fb_draw.h"
#include "inkcell/ui/latency.h"

#include <errno.h>

#ifndef INKCELL_HAVE_SDL

/*
 * No SDL2 in this build.
 *
 * Every entry point still exists and the ones that can refuse, do - the same arrangement
 * inkwell's net/tls.c has without Mbed TLS. An application offering this backend asks
 * inkcell_backend_sdl_is_available() and falls through to another one; nothing needs an #ifdef
 * of its own, and nothing silently opens a window that is not there.
 */

bool inkcell_backend_sdl_is_available(void) {
    return false;
}

uint16_t inkcell_sdl_evdev_code(int scancode) {
    (void)scancode;
    return 0U;
}

static int inkcell_backend_sdl_init(void **state_out, void *userdata) {
    (void)state_out;
    (void)userdata;
    return -ENOTSUP;
}

static const struct inkcell_backend k_sdl_backend = {
    .name = "sdl",
    .init = inkcell_backend_sdl_init,
};

const struct inkcell_backend *inkcell_backend_sdl(void) {
    return &k_sdl_backend;
}

#else /* INKCELL_HAVE_SDL */

#include "inkwell/base/env.h"
#include "inkwell/base/log.h"
#include "inkwell/base/time.h"

#include <SDL.h>
#include <linux/input-event-codes.h>
#include <stdlib.h>
#include <string.h>
#include <sys/timerfd.h>
#include <unistd.h>

/*
 * How many separate uploads one frame may make.
 *
 * Past this the last rectangle grows to cover the rest, which costs bandwidth and never
 * correctness - see inkcell_fb_damage_rects(). Eight is enough for the bands the screens
 * actually leave behind and small enough that a frame which moved everything spends its time
 * in one big upload rather than in four hundred small ones.
 *
 * A constant rather than a knob, because the honest way to choose it is to measure on the
 * device and nothing has yet.
 */
#define INKCELL_SDL_MAX_RECTS 8U

/* How often the event queue is drained, in milliseconds. See the header: SDL has no descriptor
   to wait on, so this is a timerfd and a poll, said out loud. */
#define INKCELL_SDL_POLL_MS 8

struct inkcell_sdl_panel {
    struct inkcell_draw_state state;
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    /* What the last frame looked like, for the damage comparison. The frame itself is
       state->surface.pixels, which is ordinary RAM here rather than a mapping of anything -
       so unlike the fb backend there is no third buffer. */
    uint8_t *previous_frame;
    bool frame_valid;
    /* The event pump. -1 when no host was given, which is a window that displays and does not
       listen - see the context doc. */
    int timer_fd;
    struct inkcell_input_host host;
    inkcell_key_handler on_key;
    void *key_userdata;
};

static struct inkcell_sdl_panel *inkcell_sdl_panel_of(void *state_ptr) {
    return (struct inkcell_sdl_panel *)state_ptr;
}

/* ---- the frame ------------------------------------------------------------------------- */

/*
 * The texture onto the window.
 *
 * Split out because it is wanted from two places: the end of a frame that had something new in
 * it, and a window event that did not change a pixel but did invalidate what the compositor is
 * holding - a resize, or an expose. The texture already holds the last frame in both cases, so
 * re-presenting costs one quad and nothing is redrawn.
 */
static void inkcell_sdl_blit(struct inkcell_sdl_panel *panel) {
    if (panel->renderer == NULL || panel->texture == NULL) {
        return;
    }
    /* The clear is for the letterbox rather than for the frame: with a logical size set, a
       window whose aspect does not match the panel's has bars down the sides, and they are the
       only pixels the texture does not cover. */
    SDL_RenderClear(panel->renderer);
    SDL_RenderCopy(panel->renderer, panel->texture, NULL, NULL);
    SDL_RenderPresent(panel->renderer);
}

/*
 * The rows that changed, uploaded.
 *
 * All of the deciding is inkcell_fb_damage_rects()' - it is the same arithmetic the fb backend
 * copies by, and keeping it there is what stops there being two answers to "what changed" to
 * keep in step. What is left here is the part that is actually SDL's: a rectangle in inkcell's
 * terms becomes one in SDL's, and the pixels under it go up as one texture write.
 *
 * Returns the bytes uploaded, which is what the latency probe counts as the frame's cost.
 */
static size_t inkcell_sdl_upload_damage(struct inkcell_sdl_panel *panel, bool force) {
    struct inkcell_draw_state *const state = &panel->state;
    const size_t stride = state->surface.stride;
    const size_t bpp = state->surface.bytes_per_pixel;
    const uint8_t *const frame = state->surface.pixels;

    struct inkcell_fb_damage_rect damage[INKCELL_SDL_MAX_RECTS];
    const size_t count = inkcell_fb_damage_rects(state, frame, panel->previous_frame, force, damage,
                                                 INKCELL_SDL_MAX_RECTS);

    size_t written = 0U;
    bool failed = false;
    for (size_t i = 0U; i < count; ++i) {
        const SDL_Rect rect = {
            .x = damage[i].x,
            .y = damage[i].y,
            .w = damage[i].right - damage[i].x,
            .h = damage[i].bottom - damage[i].y,
        };
        const uint8_t *const origin = frame + (size_t)rect.y * stride + (size_t)rect.x * bpp;
        if (SDL_UpdateTexture(panel->texture, &rect, origin, (int)stride) != 0) {
            inkwell_log_warn("ui", "SDL_UpdateTexture failed: %s", SDL_GetError());
            failed = true;
            continue;
        }
        written += (size_t)rect.w * (size_t)rect.h * bpp;
    }
    /*
     * Whether the comparison may be trusted next frame.
     *
     * inkcell_fb_damage_rects() brought `previous` up to date for every row it reported, which
     * is a statement that those pixels are on the panel. For a rectangle whose upload failed
     * that is not true, and nothing would ever correct it: a frame drawing the same thing again
     * produces no damage, so the stale region would sit there for the rest of the run. Dropping
     * the comparison costs one whole upload on the next frame and is the only answer that
     * recovers on its own.
     */
    panel->frame_valid = !failed;
    return written;
}

static void inkcell_backend_sdl_present(void *state_ptr, const void *snapshot, void *userdata) {
    struct inkcell_sdl_panel *const panel = inkcell_sdl_panel_of(state_ptr);
    (void)userdata;
    if (panel == NULL || snapshot == NULL || panel->state.surface.pixels == NULL) {
        return;
    }
    struct inkcell_draw_state *const state = &panel->state;

    /* One clock reading per frame, taken here rather than inside the drawing code, for the
       reason inkcell_backend_fb_present() gives: two halves of one frame drawn at two
       different times is a frame nothing can pin. */
    inkcell_fb_state_set_now(state, inkwell_time_monotonic_ms());
    inkcell_latency_frame_begin();

    inkcell_fb_render(state, snapshot);
    /* ...which also decides whether the next frame may trust the comparison: see the end of it. */
    const size_t written = inkcell_sdl_upload_damage(panel, !panel->frame_valid);

    inkcell_latency_frame_drawn(written);
    if (written > 0U) {
        inkcell_sdl_blit(panel);
    }
    /* After the blit, because the blit is what puts the frame in front of the reader - the
       same reason the fb backend ends its frame after the pan rather than after the copy. */
    inkcell_latency_frame_end(written);
}

/* ---- the event pump -------------------------------------------------------------------- */

/*
 * An SDL scancode as the evdev code that means the same key.
 *
 * Only as far as a code: what that code *means* is answered once, in src/input/input.c, over a
 * device profile and under <PREFIX>_QUIT_KEYS, and this backend has no business holding a
 * second opinion. So Escape arrives here as KEY_ESC and is a quit key because that table says
 * so, not because this one does.
 *
 * What is missing is a pad. SDL's game-controller layer would give one, and it brings a button
 * mapping of its own that may or may not agree with the profile in input_profile.c - and
 * deciding which of the two is right about a Brick is a question for a Brick, not for a
 * guess. Until then a window is driven from a keyboard and the device is driven from evdev.
 */
uint16_t inkcell_sdl_evdev_code(int scancode) {
    switch (scancode) {
    case SDL_SCANCODE_UP:
        return KEY_UP;
    case SDL_SCANCODE_DOWN:
        return KEY_DOWN;
    case SDL_SCANCODE_LEFT:
        return KEY_LEFT;
    case SDL_SCANCODE_RIGHT:
        return KEY_RIGHT;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER:
        return KEY_ENTER;
    case SDL_SCANCODE_BACKSPACE:
        return KEY_BACKSPACE;
    case SDL_SCANCODE_SPACE:
        return KEY_SPACE;
    case SDL_SCANCODE_PAGEUP:
        return KEY_PAGEUP;
    case SDL_SCANCODE_PAGEDOWN:
        return KEY_PAGEDOWN;
    case SDL_SCANCODE_TAB:
        return KEY_TAB;
    case SDL_SCANCODE_ESCAPE:
        return KEY_ESC;
    case SDL_SCANCODE_X:
        return KEY_X;
    case SDL_SCANCODE_F1:
        return KEY_F1;
    case SDL_SCANCODE_F2:
        return KEY_F2;
    default:
        return 0U;
    }
}

static void inkcell_sdl_request_stop(struct inkcell_sdl_panel *panel) {
    if (panel->host.request_stop != NULL) {
        panel->host.request_stop(panel->host.ctx);
    }
}

static void inkcell_sdl_handle_key(struct inkcell_sdl_panel *panel, const SDL_KeyboardEvent *key) {
    const uint16_t code = inkcell_sdl_evdev_code((int)key->keysym.scancode);
    if (code == 0U) {
        return;
    }
    if (inkcell_input_is_quit_key(code)) {
        inkcell_sdl_request_stop(panel);
        return;
    }
    const enum inkcell_key mapped = inkcell_input_map_key(code);
    if (mapped == INKCELL_KEY_NONE) {
        return;
    }
    /*
     * The press is offered to the probe before the application sees it, so that what gets
     * measured ends at the window rather than at the top of this function - and a repeat is
     * not offered, because a held button is one press and counting its repeats would weigh
     * whatever screen that button drives by how long somebody leant on it. That is the rule
     * inkcell_input_handle_device_event() follows for evdev, and there is no reason for a
     * window to count differently.
     *
     * The stamp is taken here and not by SDL: see enum inkcell_latency_clock. This is the
     * earliest moment this process can honestly claim, and it is later than the press by
     * however long the event sat in the queue.
     */
    if (key->repeat == 0U) {
        inkcell_latency_event(inkcell_latency_now_us());
        inkcell_latency_press();
    }
    if (panel->on_key != NULL) {
        panel->on_key(panel->key_userdata, mapped);
    }
}

static int inkcell_sdl_pump(int fd, uint32_t events, void *userdata) {
    struct inkcell_sdl_panel *const panel = (struct inkcell_sdl_panel *)userdata;
    (void)events;
    if (panel == NULL) {
        return 0;
    }
    /* The timerfd is level-triggered and counts what it owes us; not reading it is a loop that
       never sleeps again. The count itself is of no interest - several ticks that arrived while
       something else was running are still one drain of the queue. */
    uint64_t ticks;
    while (read(fd, &ticks, sizeof ticks) < 0 && errno == EINTR) {
    }

    SDL_Event event;
    while (SDL_PollEvent(&event) != 0) {
        switch (event.type) {
        case SDL_QUIT:
            inkcell_sdl_request_stop(panel);
            return 0;
        case SDL_KEYDOWN:
            inkcell_sdl_handle_key(panel, &event.key);
            break;
        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
                inkcell_sdl_request_stop(panel);
                return 0;
            }
            /*
             * Nothing was drawn, and what the compositor is holding is no longer right: the
             * back buffer after a resize is undefined and an expose is a request for the
             * pixels back. The texture still has the last frame, so this is a blit and not a
             * render - which matters, because a repaint that had to ask the application for a
             * frame would be a repaint that could not happen between two snapshots.
             */
            if (event.window.event == SDL_WINDOWEVENT_EXPOSED ||
                event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                inkcell_sdl_blit(panel);
            }
            break;
        default:
            break;
        }
    }
    return 0;
}

static void inkcell_sdl_pump_stop(struct inkcell_sdl_panel *panel) {
    if (panel->timer_fd < 0) {
        return;
    }
    if (panel->host.remove_fd != NULL) {
        panel->host.remove_fd(panel->host.ctx, panel->timer_fd);
    }
    close(panel->timer_fd);
    panel->timer_fd = -1;
}

/* Never fatal: a window that cannot be driven is still a window, and an application that got
   one is better placed to say so than this is. */
static void inkcell_sdl_pump_start(struct inkcell_sdl_panel *panel) {
    panel->timer_fd = -1;
    if (panel->host.add_fd == NULL) {
        return;
    }
    const long interval_ms = inkwell_env_int("SDL_POLL_MS", 1, 200, INKCELL_SDL_POLL_MS);
    const int fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (fd < 0) {
        inkwell_log_warn("ui", "timerfd for the SDL event pump failed: %s", strerror(errno));
        return;
    }
    const struct itimerspec every = {
        .it_interval = {.tv_sec = interval_ms / 1000L, .tv_nsec = (interval_ms % 1000L) * 1000000L},
        .it_value = {.tv_sec = interval_ms / 1000L, .tv_nsec = (interval_ms % 1000L) * 1000000L},
    };
    if (timerfd_settime(fd, 0, &every, NULL) < 0 ||
        panel->host.add_fd(panel->host.ctx, fd, inkcell_sdl_pump, panel) < 0) {
        inkwell_log_warn("ui", "the SDL event pump could not be put on the loop: %s",
                         strerror(errno));
        close(fd);
        return;
    }
    panel->timer_fd = fd;
    /*
     * An SDL event carries SDL's own clock, not the kernel's stamp on the evdev event behind
     * it, and it is drained on the tick above rather than the moment it arrives. Both make the
     * press metric a different measurement from the one the fb backend takes, so the probe is
     * told which it is getting and the report says so - rather than printing a shorter number
     * under the same name.
     */
    inkcell_latency_set_clock(INKCELL_LATENCY_CLOCK_DELIVERY);
    inkwell_log_info("ui", "SDL events are polled every %ld ms (SDL has no pollable descriptor)",
                     interval_ms);
}

/* ---- opening and closing ---------------------------------------------------------------- */

/*
 * How big a window to open.
 *
 * The context wins, then <PREFIX>_SDL_SIZE, then the Brick's own panel - see the note on
 * INKCELL_SDL_DEFAULT_WIDTH. Anything unparseable is the default rather than an error: a
 * mistyped knob should open the usual window and say so, not refuse to start a UI.
 */
static void inkcell_sdl_resolve_size(const struct inkcell_backend_sdl_context *context,
                                     uint32_t *width, uint32_t *height) {
    *width = context->width;
    *height = context->height;
    if (*width != 0U && *height != 0U) {
        return;
    }
    const char *const size = inkwell_env_get("SDL_SIZE");
    if (size != NULL) {
        unsigned int w = 0U;
        unsigned int h = 0U;
        if (sscanf(size, "%ux%u", &w, &h) == 2 && w > 0U && h > 0U) {
            *width = w;
            *height = h;
            return;
        }
        inkwell_log_warn("ui", "SDL_SIZE '%s' is not WxH; using the default panel", size);
    }
    *width = INKCELL_SDL_DEFAULT_WIDTH;
    *height = INKCELL_SDL_DEFAULT_HEIGHT;
}

bool inkcell_backend_sdl_is_available(void) {
    if (SDL_WasInit(SDL_INIT_VIDEO) != 0U) {
        return true;
    }
    /* The subsystem calls rather than SDL_VideoInit()/SDL_VideoQuit(): those bypass the
       reference count, so a probe made while the host had video up would take it down. */
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        return false;
    }
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    return true;
}

static int inkcell_backend_sdl_init(void **state_out, void *userdata) {
    struct inkcell_backend_sdl_context *const context =
        (struct inkcell_backend_sdl_context *)userdata;
    if (context == NULL) {
        return -EINVAL;
    }

    static struct inkcell_sdl_panel panel_storage;
    struct inkcell_sdl_panel *const panel = &panel_storage;
    memset(panel, 0, sizeof *panel);
    panel->timer_fd = -1;
    struct inkcell_draw_state *const state = &panel->state;

    /*
     * The video subsystem, and nothing else.
     *
     * SDL_Init()/SDL_Quit() would be the obvious pair and the wrong one for a library: SDL_Quit
     * tears down *every* subsystem in the process, so an application that had opened audio, a
     * pad or a second window would lose it when this backend's window closed. The subsystem
     * calls are reference counted by SDL, so starting video here and stopping it in shutdown()
     * leaves whatever the host started exactly as it was.
     */
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        inkwell_log_warn("ui", "SDL_InitSubSystem(VIDEO) failed: %s", SDL_GetError());
        return -ENODEV;
    }

    uint32_t width;
    uint32_t height;
    inkcell_sdl_resolve_size(context, &width, &height);

    panel->window = SDL_CreateWindow(context->title != NULL ? context->title : "inkcell",
                                     SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, (int)width,
                                     (int)height, SDL_WINDOW_RESIZABLE);
    if (panel->window == NULL) {
        inkwell_log_warn("ui", "SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return -ENODEV;
    }

    /*
     * Vsync is off by default, and that is the loop's decision rather than a picture-quality
     * one: SDL_RenderPresent() with vsync on waits for the scan-out, and this client has one
     * thread, so waiting there is up to a frame in which no transport is serviced and no
     * button is read. Tearing is the cheaper of the two. <PREFIX>_SDL_VSYNC asks for the other
     * trade on a host where the loop has nothing else to do.
     */
    uint32_t flags = SDL_RENDERER_ACCELERATED;
    if (inkwell_env_bool("SDL_VSYNC", "SDL vsync", false)) {
        flags |= SDL_RENDERER_PRESENTVSYNC;
    }
    panel->renderer = SDL_CreateRenderer(panel->window, -1, flags);
    if (panel->renderer == NULL) {
        /* No accelerated renderer is a development host with no GL, or the dummy video driver
           a test runs under. Software still presents the same pixels; it just does the blit on
           the CPU, which is the one thing this backend was meant to stop doing. */
        inkwell_log_info("ui", "No accelerated SDL renderer (%s); falling back to software",
                         SDL_GetError());
        panel->renderer = SDL_CreateRenderer(panel->window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (panel->renderer == NULL) {
        inkwell_log_warn("ui", "SDL_CreateRenderer failed: %s", SDL_GetError());
        SDL_DestroyWindow(panel->window);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return -ENODEV;
    }
    /* A logical size is what lets the window be resized without the UI being re-measured: the
       frame stays the panel's geometry and SDL scales it, letterboxing where the aspect does
       not match. Re-measuring into the new size would be the other choice and a different
       feature - it is what `make ui-capture -g WxH` is for. */
    SDL_RenderSetLogicalSize(panel->renderer, (int)width, (int)height);

    /*
     * ARGB8888 because that is what the rasteriser already produces: with no channel offsets
     * in the format, compose_color() packs 0xAARRGGBB into a native 32-bit word, and an SDL
     * packed format is defined in those same terms rather than in bytes. So the upload is a
     * copy and never a conversion, on either endianness - and a page drawn here is the same
     * page the capture harness writes, which is what lets one be compared against the other.
     */
    panel->texture = SDL_CreateTexture(panel->renderer, SDL_PIXELFORMAT_ARGB8888,
                                       SDL_TEXTUREACCESS_STREAMING, (int)width, (int)height);
    if (panel->texture == NULL) {
        inkwell_log_warn("ui", "SDL_CreateTexture failed: %s", SDL_GetError());
        SDL_DestroyRenderer(panel->renderer);
        SDL_DestroyWindow(panel->window);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return -ENODEV;
    }

    const size_t stride = (size_t)width * 4U;
    const size_t page_bytes = stride * (size_t)height;
    uint8_t *const pixels = calloc(1U, page_bytes);
    panel->previous_frame = calloc(1U, page_bytes);
    if (pixels == NULL || panel->previous_frame == NULL) {
        free(pixels);
        free(panel->previous_frame);
        panel->previous_frame = NULL;
        SDL_DestroyTexture(panel->texture);
        SDL_DestroyRenderer(panel->renderer);
        SDL_DestroyWindow(panel->window);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return -ENOMEM;
    }

    state->surface = (struct inkcell_surface){
        .pixels = pixels,
        .size = page_bytes,
        .width = width,
        .height = height,
        .stride = (uint32_t)stride,
        .bytes_per_pixel = 4U,
        .format = {.bits_per_pixel = 32U},
    };
    inkcell_fb_state_apply_theme_from_env(state);
    inkcell_fb_set_app(state, context->app);

    panel->host = context->host;
    panel->on_key = context->on_key;
    panel->key_userdata = context->key_userdata;
    inkcell_sdl_pump_start(panel);

    SDL_RendererInfo info;
    const bool named = SDL_GetRendererInfo(panel->renderer, &info) == 0;
    inkwell_log_info("ui", "SDL UI backend active (%ux%u, renderer %s, theme %s at scale %d)",
                     width, height, named ? info.name : "unknown", state->theme->id, state->scale);

    if (state_out != NULL) {
        *state_out = panel;
    }
    return 0;
}

static void inkcell_backend_sdl_shutdown(void *state_ptr, void *userdata) {
    struct inkcell_sdl_panel *const panel = inkcell_sdl_panel_of(state_ptr);
    (void)userdata;
    if (panel == NULL) {
        return;
    }
    struct inkcell_draw_state *const state = &panel->state;
    inkcell_sdl_pump_stop(panel);
    inkcell_fb_set_app(state, NULL);
    inkcell_fb_glyph_cache_free(state);
    free(panel->previous_frame);
    panel->previous_frame = NULL;
    free(state->surface.pixels);
    state->surface.pixels = NULL;
    if (panel->texture != NULL) {
        SDL_DestroyTexture(panel->texture);
        panel->texture = NULL;
    }
    if (panel->renderer != NULL) {
        SDL_DestroyRenderer(panel->renderer);
        panel->renderer = NULL;
    }
    if (panel->window != NULL) {
        SDL_DestroyWindow(panel->window);
        panel->window = NULL;
    }
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

static bool inkcell_backend_sdl_animating(void *state_ptr, void *userdata) {
    (void)userdata;
    struct inkcell_sdl_panel *const panel = inkcell_sdl_panel_of(state_ptr);
    return panel != NULL && inkcell_fb_state_animating(&panel->state);
}

static uint32_t inkcell_backend_sdl_page_rows(void *state_ptr, void *userdata) {
    (void)userdata;
    struct inkcell_sdl_panel *const panel = inkcell_sdl_panel_of(state_ptr);
    return panel != NULL ? panel->state.page_rows : 0U;
}

static const struct inkcell_focus_map *inkcell_backend_sdl_focus_map(void *state_ptr,
                                                                     void *userdata) {
    (void)userdata;
    struct inkcell_sdl_panel *const panel = inkcell_sdl_panel_of(state_ptr);
    return panel != NULL ? panel->state.focus : NULL;
}

static const struct inkcell_backend k_sdl_backend = {
    .name = "sdl",
    .init = inkcell_backend_sdl_init,
    .shutdown = inkcell_backend_sdl_shutdown,
    .present = inkcell_backend_sdl_present,
    .animating = inkcell_backend_sdl_animating,
    .page_rows = inkcell_backend_sdl_page_rows,
    .focus_map = inkcell_backend_sdl_focus_map,
};

const struct inkcell_backend *inkcell_backend_sdl(void) {
    return &k_sdl_backend;
}

#endif /* INKCELL_HAVE_SDL */
