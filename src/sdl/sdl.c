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
#include "inkwell/runtime/timer.h"

#include "inkcell/ui/input_codes.h"
#include "inkcell/ui/pointer.h"

#if defined(__APPLE__)
#include "inkcell/ui/widgets/chrome.h"

#include "sdl_cocoa.h"
#endif

#include <SDL.h>
#include <stdlib.h>
#include <string.h>
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
   to wait on, so this is a timer on the loop and a poll, said out loud. */
#define INKCELL_SDL_POLL_MS 8

/* How many of the last frame's boxes the backend keeps for the mouse. A panel's worth of rows
   and chrome is a few dozen; past this the boxes drawn *first* are the ones let go, because a
   click takes the box drawn last and those are the ones on top. */
#define INKCELL_SDL_POINTER_BOXES 512U

/*
 * The smallest frame a window will be asked to draw.
 *
 * Set on the window, so the compositor stops the drag rather than this clamping a size the
 * reader can see is wrong, and clamped here as well because a video driver is not obliged to
 * honour a minimum and a zero-width surface is a division by zero two layers up.
 *
 * Small enough to be a deliberate choice by whoever dragged it there and large enough that what
 * is left is still a frame: chrome at both ends and rows between them. Below this the answer
 * is not a cleverer layout, it is a bigger window.
 *
 * It never rises above the size the window was *opened* at, though - see `min_width` on the
 * panel. A caller that asked for a 64-pixel surface has said what it wants, and a default that
 * overruled it would be this backend deciding it knows better than an explicit request.
 */
#define INKCELL_SDL_MIN_WIDTH 320
#define INKCELL_SDL_MIN_HEIGHT 240

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
    inkcell_key_handler on_action_key;
    void (*on_shortcut)(void *userdata, char letter);
    void *key_userdata;
    /* The mouse: what it pressed, where a scroll has got to, and which cursor it is showing.
       See include/inkcell/ui/pointer.h. The cursors are NULL where the video driver has none -
       the dummy one a test runs under - and then the pointer simply never changes shape. */
    struct inkcell_pointer pointer;
    /* The secondary button's press, kept apart from the primary's so a right-press released
       over a row is not a left-click on it. */
    struct inkcell_pointer context;
    /* Which button that press was made with, so only its own release ends it. */
    Uint8 context_button;
    /*
     * The last frame's boxes, copied out when it was presented.
     *
     * A mouse event arrives between frames, and the map the frame filled belongs to the
     * renderer - which may have kept it on its own stack, since nothing in
     * inkcell_fb_set_focus_map() says it may not. Reading it after the render returned would
     * be reading whatever the stack holds now. So the backend owns what it answers clicks
     * against, and it is exactly the frame the reader is looking at.
     */
    struct inkcell_focus_item pointer_items[INKCELL_SDL_POINTER_BOXES];
    struct inkcell_focus_map pointer_map;
    inkcell_click_handler on_click;
    inkcell_click_handler on_context;
    void *click_userdata;
    SDL_Cursor *arrow;
    SDL_Cursor *hand;
    bool pointing;
    /*
     * The title bar, on a Mac - see src/sdl/sdl_cocoa.h.
     *
     * `titlebar_theme` and `strip_px` are what it was last arranged for, so an ordinary frame
     * asks AppKit nothing and a theme or scale change arranges it again. `strip_points` is how
     * much of the window's top drags it, in the window points SDL's hit test is asked in; 0
     * when nothing does.
     */
    bool unified_titlebar;
    const struct inkcell_theme *titlebar_theme;
    int strip_px;
    int strip_points;
    void (*request_frame)(void *userdata);
    void *frame_userdata;
    /* A frame was asked for and has not been drawn. animating() says so, because a host that
       asks it after every present - mesh-client's controller does - would otherwise hear "at
       rest" and cancel the very frame requested during that present. */
    bool frame_requested;
    /* Whether the surface holds a frame yet, for frame(). */
    bool presented;
    /*
     * Whether the frame keeps the size it opened at and is scaled to the window, which is what
     * this backend used to do always - see the note in init().
     *
     * <PREFIX>_SDL_FIXED asks for it. Off, a resize re-measures: the surface is reallocated at
     * the window's new size and the application draws a frame that shape.
     */
    bool fixed_frame;
    /* The floor a drag stops at: INKCELL_SDL_MIN_* , or the opening size where that was
       smaller. Held rather than recomputed so the window and the clamp cannot disagree. */
    int min_width;
    int min_height;
};

static struct inkcell_sdl_panel *inkcell_sdl_panel_of(void *state_ptr) {
    return (struct inkcell_sdl_panel *)state_ptr;
}

/*
 * A frame the window needs and cannot draw itself.
 *
 * Two things ask for one, and neither is a snapshot changing: a resize, which gives the
 * application a differently shaped surface to lay out in, and - on a Mac - the window's buttons
 * moving, which moves the tabs the frame already drew. Both need what only the application has,
 * so the flag is set either way and `request_frame` is called where one was given.
 *
 * The flag outlives the call because animating() is asked after every present, and a host that
 * heard "at rest" there would cancel the very frame requested during it.
 */
static void inkcell_sdl_request_frame(struct inkcell_sdl_panel *panel) {
    panel->frame_requested = true;
    if (panel->request_frame != NULL) {
        panel->request_frame(panel->frame_userdata);
    }
}

/* ---- the title bar --------------------------------------------------------------------- */

#if defined(__APPLE__)
/*
 * The title bar brought into line with the frame: its colour with the theme, and - unified -
 * the window's buttons with the tab strip and the tabs with the buttons.
 *
 * `force` is a resize, which moves the buttons without changing anything this can compare.
 * Returns whether the tabs have to move, which the frame already drawn does not know: the
 * caller either has not drawn yet, or asks the application for another.
 */
static bool inkcell_sdl_titlebar_sync(struct inkcell_sdl_panel *panel, bool force) {
    struct inkcell_draw_state *const state = &panel->state;
    const bool restyle = panel->titlebar_theme != state->theme;
    if (restyle) {
        inkcell_sdl_cocoa_blend_titlebar(panel->window,
                                         inkcell_fb_color(state, INKCELL_COLOR_SURFACE_LOW));
        panel->titlebar_theme = state->theme;
    }
    if (!panel->unified_titlebar) {
        return false;
    }
    /* The strip's height is the theme's and the scale's, not the frame's, so it is known
       before anything is drawn - which is what lets the first frame already be clear of the
       buttons. */
    const int strip_px =
        inkcell_fb_nav_bar_height(state, inkcell_fb_type_scale(state, INKCELL_TYPE_LABEL));
    if (!force && !restyle && strip_px == panel->strip_px) {
        return false;
    }
    panel->strip_px = strip_px;
    const struct inkcell_sdl_cocoa_controls controls =
        inkcell_sdl_cocoa_place_controls(panel->window, strip_px);
    panel->strip_points = controls.strip_points;
    const bool moved = controls.inset_px != state->top_leading_inset;
    state->top_leading_inset = controls.inset_px;
    return moved;
}

/*
 * The strip is the window's handle, as a title bar is - except where the frame drew something a
 * click is for. A tab is clicked, not dragged, and AppKit takes a draggable point for the window
 * before SDL ever sees a button go down, so the tabs have to be carved out here or they cannot be
 * clicked at all. Asked of the last frame's boxes, the same ones a click is answered against; a
 * strip with no handler for clicks gives nothing up to a tab it could not deliver. The buttons are
 * AppKit's views and answer before this is asked.
 *
 * `point` is in window points and the boxes are in the frame's pixels. A re-measuring window is
 * both at once; a fixed one is scaled, which is what SDL_RenderWindowToLogical() undoes - and
 * before SDL 2.0.18, which has no such call, a fixed window's strip does not drag at all.
 */
static SDL_HitTestResult inkcell_sdl_hit_test(SDL_Window *window, const SDL_Point *point,
                                              void *data) {
    (void)window;
    const struct inkcell_sdl_panel *const panel = (const struct inkcell_sdl_panel *)data;
    if (point->y >= panel->strip_points) {
        return SDL_HITTEST_NORMAL;
    }
    int x = point->x;
    int y = point->y;
#if SDL_VERSION_ATLEAST(2, 0, 18)
    if (panel->renderer != NULL) {
        float lx = 0.0f;
        float ly = 0.0f;
        SDL_RenderWindowToLogical(panel->renderer, point->x, point->y, &lx, &ly);
        x = (int)lx;
        y = (int)ly;
    }
#else
    /* Nothing to undo a fixed frame's scaling with, so the boxes cannot be found under the
       point. A strip that never drags is the smaller loss than tabs that sometimes do. */
    if (panel->fixed_frame) {
        return SDL_HITTEST_NORMAL;
    }
#endif
    return inkcell_pointer_over_target(&panel->pointer_map, x, y, panel->on_click != NULL)
               ? SDL_HITTEST_NORMAL
               : SDL_HITTEST_DRAGGABLE;
}
#endif

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
    /* The clear is for the letterbox rather than for the frame, and only a fixed frame has one:
       with a logical size set, a window whose aspect does not match the frame's has bars down
       the sides and they are the only pixels the texture does not cover. A re-measuring window
       has no bars - the texture is the window - so the clear costs a fill nothing will show,
       which is cheap enough not to be worth a branch. */
    SDL_RenderClear(panel->renderer);
    SDL_RenderCopy(panel->renderer, panel->texture, NULL, NULL);
    SDL_RenderPresent(panel->renderer);
}

/*
 * The window's new size, taken on: a surface that shape, and a frame drawn into it.
 *
 * This is the whole of what a window is for on a development host now. A frame pinned at the
 * device's geometry and scaled up is a picture of the device - which is a useful thing and is
 * what <PREFIX>_SDL_FIXED still asks for - but it is not an application: the layout never sees
 * the room it was given, so a window dragged to twice the width is the handheld layout at twice
 * the size rather than a layout that used the width. Everything the width classes decide
 * (inkcell/ui/stack.h) is decided from the surface's own geometry, so a surface that never
 * changes is a width class that never changes either.
 *
 * Allocating before freeing is not tidiness. A resize that cannot be honoured has to leave the
 * window exactly as it was - the old surface whole, the old texture still holding the frame the
 * reader is looking at - because the alternative is a window that went blank because a drag
 * asked for more memory than there was. So everything new is obtained first and nothing old is
 * released until all of it succeeded.
 *
 * Answers whether the geometry actually changed, which is not the same as whether it was
 * asked to: SDL sends SDL_WINDOWEVENT_SIZE_CHANGED for a move between displays and for its own
 * programmatic changes, and re-measuring a frame that is already the right shape would be a
 * full upload and a reallocation for nothing.
 */
static bool inkcell_sdl_resize(struct inkcell_sdl_panel *panel, int width, int height) {
    struct inkcell_draw_state *const state = &panel->state;
    if (panel->fixed_frame || panel->renderer == NULL) {
        return false;
    }
    if (width < panel->min_width) {
        width = panel->min_width;
    }
    if (height < panel->min_height) {
        height = panel->min_height;
    }
    if ((uint32_t)width == state->surface.width && (uint32_t)height == state->surface.height) {
        return false;
    }

    const size_t stride = (size_t)width * 4U;
    const size_t page_bytes = stride * (size_t)height;
    uint8_t *const pixels = calloc(1U, page_bytes);
    uint8_t *const previous = calloc(1U, page_bytes);
    SDL_Texture *const texture = SDL_CreateTexture(panel->renderer, SDL_PIXELFORMAT_ARGB8888,
                                                   SDL_TEXTUREACCESS_STREAMING, width, height);
    if (pixels == NULL || previous == NULL || texture == NULL) {
        free(pixels);
        free(previous);
        if (texture != NULL) {
            SDL_DestroyTexture(texture);
        }
        inkwell_log_warn("ui", "resize to %dx%d refused; keeping %ux%u", width, height,
                         state->surface.width, state->surface.height);
        return false;
    }

    free(state->surface.pixels);
    free(panel->previous_frame);
    SDL_DestroyTexture(panel->texture);

    state->surface.pixels = pixels;
    state->surface.size = page_bytes;
    state->surface.width = (uint32_t)width;
    state->surface.height = (uint32_t)height;
    state->surface.stride = (uint32_t)stride;
    panel->previous_frame = previous;
    panel->texture = texture;

    /* Nothing has been drawn at this size, so there is nothing for the damage comparison to be
       a comparison *with*: the next frame goes up whole. `presented` says the same thing to
       frame(), which would otherwise hand out a buffer of zeroes as a picture. */
    panel->frame_valid = false;
    panel->presented = false;
    /*
     * And whatever the application memoised against the old geometry.
     *
     * This is exactly the case struct inkcell_fb_app's `drop_caches` names - "a memo can no
     * longer be trusted: a reference render, a geometry change" - and a resize is the geometry
     * change. Without it the frame requested below may lay itself out from measurements taken
     * for a surface that no longer exists, which is a frame drawn to the old width on a window
     * that is now a different one. It is called here rather than at the call site so that every
     * path that changes the geometry drops them, including any that arrives later.
     */
    inkcell_fb_app_drop_caches(state);
#if defined(__APPLE__)
    /*
     * And the title bar's arithmetic, which converts between panel pixels and window points by
     * the ratio between them and holds the panel's half of it. Stale, that ratio is wrong by
     * exactly the factor the window has grown by, so the forced sync below would report an
     * inset the tabs then fail to clear the buttons by. Told before anything asks it.
     */
    inkcell_sdl_cocoa_set_panel_size(width, height);
#endif
    /*
     * And the mouse forgets what it was pointing at. The boxes are the last frame's, measured
     * against a surface that no longer exists, so a click landing between the resize and the
     * next frame would be answered against a layout the reader cannot see - which is the one
     * thing a pointer must never do. Empty until a frame refills it.
     */
    panel->pointer_map.count = 0U;
    panel->pointer_map.dropped = 0U;
    return true;
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

/* The frame's boxes, into the backend's own map: see `pointer_map`. The last ones when there
   are too many, keeping their order, so what is on top is still found first. */
static void inkcell_sdl_keep_boxes(struct inkcell_sdl_panel *panel) {
    const struct inkcell_focus_map *const drawn = panel->state.focus;
    const uint32_t count = drawn != NULL && drawn->items != NULL ? drawn->count : 0U;
    const uint32_t kept = count < INKCELL_SDL_POINTER_BOXES ? count : INKCELL_SDL_POINTER_BOXES;
    if (kept > 0U) {
        memcpy(panel->pointer_items, drawn->items + (count - kept),
               (size_t)kept * sizeof panel->pointer_items[0]);
    }
    panel->pointer_map.items = panel->pointer_items;
    panel->pointer_map.capacity = INKCELL_SDL_POINTER_BOXES;
    panel->pointer_map.count = kept;
    panel->pointer_map.dropped = count - kept;
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

    panel->frame_requested = false;
#if defined(__APPLE__)
    /* Before the render, so the tabs are drawn clear of the buttons where they are now... */
    (void)inkcell_sdl_titlebar_sync(panel, false);
#endif
    inkcell_fb_render(state, snapshot);
    panel->presented = true;
    inkcell_sdl_keep_boxes(panel);
#if defined(__APPLE__)
    /* ...and after it, because a frame is where the application gets to change the theme. The
       bar turns with the strip under it rather than a frame later, and a strip that changed
       height moves the tabs, which this frame drew where they were. */
    if (inkcell_sdl_titlebar_sync(panel, false)) {
        inkcell_sdl_request_frame(panel);
    }
#endif
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

/*
 * A logical key, to the application. Every source in this file ends here - a key on the keyboard,
 * a click on a hint, a notch of the wheel - so that the application hears one kind of press.
 * `counted` is whether the latency probe should take it as a press; see below.
 */
static void inkcell_sdl_deliver(struct inkcell_sdl_panel *panel, inkcell_key_handler handler,
                                enum inkcell_key mapped, bool counted) {
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
    if (counted) {
        inkcell_latency_event(inkcell_latency_now_us());
        inkcell_latency_press();
    }
    if (handler != NULL) {
        handler(panel->key_userdata, mapped);
    }
}

static void inkcell_sdl_handle_key(struct inkcell_sdl_panel *panel, const SDL_KeyboardEvent *key) {
    /* A desktop shortcut must not also become a controller button (notably Ctrl+X).
       Use SDL's key symbol so the letter follows the active keyboard layout. */
#if defined(__APPLE__)
    const SDL_Keymod primary = KMOD_GUI;
#else
    const SDL_Keymod primary = KMOD_CTRL;
#endif
    if ((key->keysym.mod & (KMOD_CTRL | KMOD_GUI)) != 0) {
        const SDL_Keycode symbol = key->keysym.sym;
        if (panel->on_shortcut != NULL && (key->keysym.mod & primary) != 0 && key->repeat == 0U &&
            (key->keysym.mod & (KMOD_ALT | KMOD_SHIFT)) == 0 && symbol >= SDLK_a &&
            symbol <= SDLK_z) {
            inkcell_latency_event(inkcell_latency_now_us());
            inkcell_latency_press();
            panel->on_shortcut(panel->key_userdata, (char)symbol);
        }
        return;
    }
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
    inkcell_sdl_deliver(panel, panel->on_key, mapped, key->repeat == 0U);
}

/* ---- the mouse ------------------------------------------------------------------------- */

/*
 * The coordinates here are already the frame's, under either of the two ways a window can be
 * sized. A re-measuring window is the easy case: the surface *is* the window, so a mouse
 * position needs no translation at all. A fixed one relies on SDL_RenderSetLogicalSize(), which
 * makes the renderer rewrite every mouse event into logical pixels before it is queued, so a
 * window scaled to twice the frame reports a click on a hint where the hint was drawn; a click
 * in the letterbox comes out past the frame's edge and hits nothing, which is what it is.
 *
 * The map is the backend's copy of the last frame's - the frame the reader was looking at when
 * they clicked, which is the same argument the controller makes for a key.
 */
static void inkcell_sdl_handle_button(struct inkcell_sdl_panel *panel,
                                      const SDL_MouseButtonEvent *button) {
    if (button->button == SDL_BUTTON_X1) {
        /* The thumb button a browser goes back with. Back here is B, on the release so a
           held button is one press. */
        if (button->type == SDL_MOUSEBUTTONUP) {
            inkcell_sdl_deliver(panel, panel->on_key, INKCELL_KEY_B, true);
        }
        return;
    }
    /*
     * The secondary button: the right one, or a control-click on a Mac with only one - which
     * SDL reports as the left, and which is decided on the press. The release that ends it is
     * the *same* button's, so a left click made while the right is held is still a click, and
     * a control-click let go after control was released is still a context.
     */
    if (button->type == SDL_MOUSEBUTTONDOWN &&
        (button->button == SDL_BUTTON_RIGHT ||
         (button->button == SDL_BUTTON_LEFT && (SDL_GetModState() & KMOD_CTRL) != 0))) {
        inkcell_pointer_down(&panel->context, &panel->pointer_map, button->x, button->y);
        panel->context_button = button->button;
        return;
    }
    if (button->type == SDL_MOUSEBUTTONUP && panel->context.down &&
        button->button == panel->context_button) {
        const struct inkcell_pointer_result result =
            inkcell_pointer_up(&panel->context, &panel->pointer_map, button->x, button->y);
        if (result.kind != INKCELL_POINTER_NONE && panel->on_context != NULL) {
            inkcell_latency_event(inkcell_latency_now_us());
            inkcell_latency_press();
            panel->on_context(panel->click_userdata, result.target, result.x, result.y);
        }
        return;
    }
    if (button->button != SDL_BUTTON_LEFT) {
        return;
    }
    if (button->type == SDL_MOUSEBUTTONDOWN) {
        inkcell_pointer_down(&panel->pointer, &panel->pointer_map, button->x, button->y);
        return;
    }
    const struct inkcell_pointer_result result =
        inkcell_pointer_up(&panel->pointer, &panel->pointer_map, button->x, button->y);
    if (result.kind == INKCELL_POINTER_ACTION_KEY) {
        inkcell_key_handler handler =
            panel->on_action_key != NULL ? panel->on_action_key : panel->on_key;
        inkcell_sdl_deliver(panel, handler, result.key, true);
    } else if (result.kind == INKCELL_POINTER_KEY) {
        inkcell_sdl_deliver(panel, panel->on_key, result.key, true);
    } else if (result.kind == INKCELL_POINTER_CLICK && panel->on_click != NULL) {
        inkcell_latency_event(inkcell_latency_now_us());
        inkcell_latency_press();
        panel->on_click(panel->click_userdata, result.target, result.x, result.y);
    }
}

/*
 * A scroll, as rows: up and down, one press per notch.
 *
 * Moving the cursor rather than the view is the honest version of a wheel for a UI whose lists
 * are windowed around a cursor - there is no view here that could move without it. Not counted
 * as presses by the probe: a fling is dozens of steps in one gesture, and charging each one as a
 * press would weigh every screen by how hard somebody flicked.
 */
static void inkcell_sdl_handle_wheel(struct inkcell_sdl_panel *panel,
                                     const SDL_MouseWheelEvent *wheel) {
#if SDL_VERSION_ATLEAST(2, 0, 18)
    const float dy = wheel->preciseY;
#else
    const float dy = (float)wheel->y;
#endif
    /* Not negated for SDL_MOUSEWHEEL_FLIPPED. The number is already the one the system's own
       scrolling setting produced, and what `direction` adds is only which setting that was - a
       reader who chose natural scrolling expects this list to scroll the way their others do. */
    const int steps = inkcell_pointer_wheel(&panel->pointer, dy);
    const enum inkcell_key key = steps > 0 ? INKCELL_KEY_UP : INKCELL_KEY_DOWN;
    for (int i = 0; i < (steps > 0 ? steps : -steps); ++i) {
        inkcell_sdl_deliver(panel, panel->on_key, key, false);
    }
}

/* The hand over something a click would do, and the arrow everywhere else. Only on a change:
   SDL_SetCursor() redraws the cursor, and a mouse moving across a row is dozens of events. */
static void inkcell_sdl_handle_motion(struct inkcell_sdl_panel *panel,
                                      const SDL_MouseMotionEvent *motion) {
    if (panel->arrow == NULL || panel->hand == NULL) {
        return;
    }
    const bool pointing = inkcell_pointer_over_target(&panel->pointer_map, motion->x, motion->y,
                                                      panel->on_click != NULL);
    if (pointing != panel->pointing) {
        panel->pointing = pointing;
        SDL_SetCursor(pointing ? panel->hand : panel->arrow);
    }
}

static int inkcell_sdl_pump(int fd, uint32_t events, void *userdata) {
    struct inkcell_sdl_panel *const panel = (struct inkcell_sdl_panel *)userdata;
    (void)events;
    if (panel == NULL) {
        return 0;
    }
    /* The timer is level-triggered and counts what it owes us; not reading it is a loop that
       never sleeps again. The count itself is of no interest - several ticks that arrived while
       something else was running are still one drain of the queue. */
    (void)inkwell_timer_read(fd);

    SDL_Event event;
    while (SDL_PollEvent(&event) != 0) {
        switch (event.type) {
        case SDL_QUIT:
            inkcell_sdl_request_stop(panel);
            return 0;
        case SDL_KEYDOWN:
            inkcell_sdl_handle_key(panel, &event.key);
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            inkcell_sdl_handle_button(panel, &event.button);
            break;
        case SDL_MOUSEWHEEL:
            inkcell_sdl_handle_wheel(panel, &event.wheel);
            break;
        case SDL_MOUSEMOTION:
            inkcell_sdl_handle_motion(panel, &event.motion);
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
                /*
                 * A re-measuring window takes the new geometry here, which leaves a gap of at
                 * most one frame where the application has a `request_frame` and until the next
                 * ordinary present where it does not. The alternative is holding the old
                 * texture to scale into the gap, which is a second copy of the frame kept alive
                 * for a few milliseconds of a drag.
                 */
                /*
                 * One event, two reasons a frame might be owed, and *one* request.
                 *
                 * A macOS size change can be both at once: the surface is a new shape, and the
                 * window's buttons have moved so the tabs must start somewhere else.
                 * inkcell_sdl_request_frame() calls the application's callback every time
                 * rather than only setting a flag, so asking twice for the same event is two
                 * frames for one drag - and a drag is a great many events. The reasons are
                 * collected and the ask is made once.
                 */
                const bool remeasured =
                    event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED &&
                    inkcell_sdl_resize(panel, event.window.data1, event.window.data2);
                bool owed = remeasured;
#if defined(__APPLE__)
                /*
                 * The buttons do not scale with the frame, so a resize changes how far in the
                 * tabs have to start, and that *is* a frame.
                 *
                 * Forced, and ahead of the two paths below rather than inside one of them. It
                 * was briefly reachable only on the path that did *not* re-measure, which is
                 * backwards: a window that just changed size is the case where AppKit has
                 * certainly moved the controls. The unforced call in present() returns early
                 * when the theme and the strip height are unchanged, so nothing else would have
                 * recomputed the inset and the tabs would draw under the buttons.
                 */
                if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED &&
                    inkcell_sdl_titlebar_sync(panel, true)) {
                    owed = true;
                }
#endif
                if (remeasured) {
                    /* There is nothing to blit: the texture that held the last frame has just
                       been destroyed and its replacement has never been drawn into. So the
                       window is cleared to the ground the next frame will stand on - not to
                       black, which reads as a flash. */
                    const struct inkcell_rgb ground =
                        inkcell_fb_color(&panel->state, INKCELL_COLOR_BG);
                    SDL_SetRenderDrawColor(panel->renderer, ground.r, ground.g, ground.b,
                                           SDL_ALPHA_OPAQUE);
                    SDL_RenderClear(panel->renderer);
                    SDL_RenderPresent(panel->renderer);
                }
                if (owed) {
                    inkcell_sdl_request_frame(panel);
                }
                if (!remeasured) {
                    inkcell_sdl_blit(panel);
                }
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
    const int fd = inkwell_timer_open();
    if (fd < 0) {
        inkwell_log_warn("ui", "the timer for the SDL event pump failed: %s", strerror(-fd));
        return;
    }
    const int armed = inkwell_timer_arm_every(fd, (uint32_t)interval_ms);
    const int added =
        armed < 0 ? armed : panel->host.add_fd(panel->host.ctx, fd, inkcell_sdl_pump, panel);
    if (added < 0) {
        inkwell_log_warn("ui", "the SDL event pump could not be put on the loop: %s",
                         strerror(-added));
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
    /*
     * Whether a drag re-measures the UI or scales it.
     *
     * A logical size is what scales it: the frame stays the size it opened at and SDL stretches
     * it, letterboxing where the aspect does not match. That was this backend's only behaviour
     * for as long as the only surface it stood in for was a handheld panel, and as a way of
     * *looking at* that panel it is still the right one - which is why <PREFIX>_SDL_FIXED keeps
     * it.
     *
     * It is no longer the right default. A window is a surface in its own right now, with a
     * width class taken from its own geometry (inkcell/ui/stack.h), and a frame that never
     * changes shape is a width class that never changes either - so a window dragged across a
     * breakpoint would show the handheld layout, larger. Re-measuring is one reallocation per
     * drag-end and is what makes the window an application rather than a picture of one.
     *
     * The size it *opens* at is unchanged either way, and is still the device's unless
     * something says otherwise. A layout that only works at desktop proportions is still
     * caught the moment the window comes up, which is what that default was for.
     */
    panel->fixed_frame = inkwell_env_bool("SDL_FIXED", "SDL fixed frame", false);
    panel->min_width = (int)width < INKCELL_SDL_MIN_WIDTH ? (int)width : INKCELL_SDL_MIN_WIDTH;
    panel->min_height = (int)height < INKCELL_SDL_MIN_HEIGHT ? (int)height : INKCELL_SDL_MIN_HEIGHT;
    if (panel->fixed_frame) {
        SDL_RenderSetLogicalSize(panel->renderer, (int)width, (int)height);
    } else {
        /* Asked of the window rather than only enforced in the resize, so a drag stops at the
           edge instead of the frame quietly disagreeing with the window about its own size. */
        SDL_SetWindowMinimumSize(panel->window, panel->min_width, panel->min_height);
    }

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
    state->pointer = true;

    panel->host = context->host;
    panel->on_key = context->on_key;
    panel->on_action_key = context->on_action_key;
    panel->on_shortcut = context->on_shortcut;
    panel->key_userdata = context->key_userdata;
    panel->on_click = context->on_click;
    panel->on_context = context->on_context;
    panel->click_userdata = context->click_userdata;
    inkcell_pointer_reset(&panel->pointer);
    inkcell_pointer_reset(&panel->context);
    panel->arrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    panel->hand = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
    panel->request_frame = context->request_frame;
    panel->frame_userdata = context->frame_userdata;
#if defined(__APPLE__)
    if (context->unified_titlebar) {
        /* The aspect is held only for a fixed frame, which is the mode that letterboxes and so
           the only one with a reason to. A re-measuring window locked to the handheld panel's
           4:3 would be a Mac window that cannot be dragged to any shape at all. */
        panel->unified_titlebar = inkcell_sdl_cocoa_unify_titlebar(panel->window, (int)width,
                                                                   (int)height, panel->fixed_frame);
    }
    if (panel->unified_titlebar) {
        /* Half the panel is where the buttons start to crowd the first tab off the strip. */
        SDL_SetWindowMinimumSize(panel->window, (int)width / 2, (int)height / 2);
        if (SDL_SetWindowHitTest(panel->window, inkcell_sdl_hit_test, panel) != 0) {
            inkwell_log_warn("ui", "SDL_SetWindowHitTest failed: %s", SDL_GetError());
        }
    }
    (void)inkcell_sdl_titlebar_sync(panel, true);
#endif
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
    if (panel->hand != NULL) {
        SDL_FreeCursor(panel->hand);
        panel->hand = NULL;
    }
    if (panel->arrow != NULL) {
        SDL_FreeCursor(panel->arrow);
        panel->arrow = NULL;
    }
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
    return panel != NULL && (panel->frame_requested || inkcell_fb_state_animating(&panel->state));
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

/* The surface is ordinary RAM holding the whole of the last frame - the texture is filled from
   it - so it is the answer as it stands. */
static bool inkcell_backend_sdl_frame(void *state_ptr, void *userdata,
                                      struct inkcell_surface *out) {
    (void)userdata;
    struct inkcell_sdl_panel *const panel = inkcell_sdl_panel_of(state_ptr);
    if (panel == NULL || out == NULL || !panel->presented) {
        return false;
    }
    *out = panel->state.surface;
    return true;
}

static const struct inkcell_backend k_sdl_backend = {
    .name = "sdl",
    .init = inkcell_backend_sdl_init,
    .shutdown = inkcell_backend_sdl_shutdown,
    .present = inkcell_backend_sdl_present,
    .animating = inkcell_backend_sdl_animating,
    .page_rows = inkcell_backend_sdl_page_rows,
    .focus_map = inkcell_backend_sdl_focus_map,
    .frame = inkcell_backend_sdl_frame,
};

const struct inkcell_backend *inkcell_backend_sdl(void) {
    return &k_sdl_backend;
}

#endif /* INKCELL_HAVE_SDL */
