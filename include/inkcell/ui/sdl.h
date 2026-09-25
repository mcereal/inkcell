#ifndef INKCELL_UI_SDL_H
#define INKCELL_UI_SDL_H

/*
 * The window backend: the same frame, handed to a GPU instead of to /dev/fb0.
 *
 * Nothing above this line changes. The rasteriser is the one in src/fb/fb_draw.c, drawing the
 * same pixels into the same `struct inkcell_surface` it always has, and a screen cannot tell
 * which backend is presenting it - which is the whole of what the surface was separated out
 * for. What differs is only the last step: where the fb backend copies changed spans into an
 * mmap of the panel and pans, this uploads them into a texture and asks the renderer to put it
 * on screen.
 *
 * So this is *not* a GPU renderer. The triangles are one textured quad a frame; the glyphs,
 * the rounded rectangles and the anti-aliasing are still the CPU's work. What it buys is the
 * blit, a desktop window where there was only a screenshot before,
 * and - the point of the exercise - a seam that a real GPU renderer can be written behind
 * without touching a single screen.
 *
 * A window is a surface in its own right, not a magnifying glass over the device's. Dragging
 * one re-measures: the surface is reallocated at the new size and the application is asked for
 * a frame that shape, so everything a layout decides from the room it has - see the width
 * classes in inkcell/ui/stack.h - is decided from the room the *window* has.
 * <PREFIX>_SDL_FIXED asks for the other behaviour, which was this backend's only one for a
 * while: the frame stays the size it opened at and SDL scales it, letterboxing where the
 * aspect does not match. That is the right thing when the window is standing in for the
 * device, and the wrong thing when it is the surface.
 *
 * Two things about SDL do not fit an application built on one epoll loop, and neither is
 * hidden here:
 *
 *   - **SDL has no descriptor to wait on.** There is no portable way to put its event queue in
 *     an epoll set, so a host that hands this backend an `inkcell_input_host` gets a timerfd
 *     registered on it and SDL_PollEvent() drained on every tick. That is polling, and it is
 *     said out loud rather than dressed up: the rate is <PREFIX>_SDL_POLL_MS (8 by default),
 *     and the alternative - digging the X11 connection's fd out of SDL_GetWindowWMInfo - buys
 *     one video driver out of six and breaks on the rest.
 *
 *   - **Presenting can block.** With vsync on, SDL_RenderPresent() waits for the scan-out, and
 *     a single-threaded client that waits there is a client not servicing its transport for up
 *     to a frame. So vsync is off unless <PREFIX>_SDL_VSYNC asks for it, and what that costs
 *     is tearing on a frame that lands mid-scan - which is the cheaper of the two.
 *
 * Optional by presence, like Mbed TLS under inkwell: without SDL2 at build time every entry
 * point below still exists, inkcell_backend_sdl_is_available() answers false, and init()
 * refuses with -ENOTSUP. Nothing that offers this backend needs an #ifdef of its own.
 */

#include "inkcell/ui/backend.h"
#include "inkcell/ui/input.h"
#include "inkcell/ui/key.h"
#include "inkcell/ui/pointer.h"

#include <stdbool.h>
#include <stdint.h>

struct inkcell_fb_app;

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The panel this opens when nobody says otherwise: the TrimUI Brick's, so that what comes up in
 * a window is, by default, the geometry the device will actually draw, and a layout
 * that only works at desktop proportions is caught where it is written rather than after a
 * deploy. <PREFIX>_SDL_SIZE=WxH overrides it, as does naming a size in the context below.
 */
#define INKCELL_SDL_DEFAULT_WIDTH 1024U
#define INKCELL_SDL_DEFAULT_HEIGHT 768U

/*
 * What the SDL backend is opened with.
 *
 * `app` is the same one the fb backend takes - the thing that draws a frame - and is installed
 * at init() for the same reason: between opening a window and being handed a renderer there
 * would be a backend presenting a blank one. NULL draws nothing, which is what a harness
 * measuring the panel wants.
 *
 * The rest is the half the fb backend does not need. A framebuffer is a device that exists or
 * does not, and reading a button from it is somebody else's descriptor; a window is a thing
 * this library creates, and the presses that arrive in it arrive through the same queue as the
 * resize and the close box. So the window owns its input, and `host` is how the events reach
 * the loop the application already runs - see the note at the top of this file about what SDL
 * does not give us.
 *
 * `host` may be left zeroed, and then nothing is pumped and the window is a display. That is
 * the right shape for a test, and the wrong one for anything a person is meant to press.
 *
 * `width`/`height` of 0 take <PREFIX>_SDL_SIZE, then the default above. They are the size the
 * window *opens* at; unless <PREFIX>_SDL_FIXED is set, a drag changes it from there, and the
 * smallest a drag may reach is a floor this backend sets - or the opening size, where that was
 * already smaller, because a floor above an explicit request would be overruling it.
 * `title` of NULL takes the library's own.
 *
 * `unified_titlebar` is for an application whose frame opens with inkcell_fb_draw_nav_bar(),
 * and only means anything on a Mac: the frame runs up under a transparent title bar, the
 * window's three buttons sit in the tab strip, the strip drags the window, and the tabs start
 * clear of the buttons (`top_leading_inset` on the draw state). Off, the title bar stays and is
 * painted the strip's colour. Either way the frame is the panel's size and a capture of it is
 * unchanged - what moves is the first tab, on a Mac, in a window.
 *
 * The mouse needs nothing here to work. A click on a hint in the action bar is handed to
 * `on_action_key` when present, with `key_userdata`; this lets an application distinguish a
 * visible action from the same logical key on a keyboard or gamepad. NULL preserves the old
 * path through `on_key`. The wheel still arrives through `on_key` as up and down, and the thumb
 * button as B. `on_click` is for everything else the frame registered - a row, a tab - and hears
 * the id under the pointer; NULL drops those clicks. `on_context` is the secondary button on the
 * same terms - a right-click, or a control-click on a one-button Mac - and hears whatever is
 * under the pointer, a key's target included, with `click_userdata`: what a context menu is of
 * is the application's to say. NULL drops those. See inkcell/ui/pointer.h.
 *
 * `request_frame` is how the window asks for a frame it cannot draw itself, and it matters more
 * now than it did: a resize has given the application a differently shaped surface and there is
 * nothing on screen until something draws into it, and on a Mac the window's buttons moving
 * moves the tabs the last frame drew. Both need the application's snapshot. Called with
 * `frame_userdata`, and expected to end in a present() soon after - it may be NULL, and then
 * the window shows its ground until whatever frame comes next.
 */
struct inkcell_backend_sdl_context {
    const struct inkcell_fb_app *app;
    struct inkcell_input_host host;
    inkcell_key_handler on_key;
    inkcell_key_handler on_action_key;
    /* Called for a primary-modifier letter chord (Command on macOS, Control elsewhere).
       The letter is lowercase ASCII. Such chords never fall through to on_key. */
    void (*on_shortcut)(void *userdata, char letter);
    /* The application says when its visible context accepts text. Committed UTF-8 (including
       paste) reaches on_text_input; preedit composition is not drawn in the application frame.
       Both use key_userdata. When active, Backspace deletes, Enter submits, and Escape leaves. */
    bool (*text_input_active)(void *userdata);
    void (*on_text_input)(void *userdata, const char *text);
    void *key_userdata;
    inkcell_click_handler on_click;
    inkcell_click_handler on_context;
    void *click_userdata;
    const char *title;
    uint32_t width;
    uint32_t height;
    bool unified_titlebar;
    /*
     * Size the UI for the display the window is on, not for the handheld panel.
     *
     * A theme's scale is chosen for a 3.2" panel read at arm's length, and on a desktop that is
     * text twice the size of every other window's. Set, the body scale becomes
     * inkcell_sdl_display_scale() of the theme's at the window's pixel density, and is worked
     * out again when the window moves to a display of another density. <PREFIX>_FB_SCALE still
     * wins, and a fixed frame (<PREFIX>_SDL_FIXED) is a picture of the panel and keeps the
     * panel's scale. Off, the theme's scale is used as it stands.
     *
     * On Windows the window is only drawn at the display's density when SDL video is started
     * under SDL_HINT_WINDOWS_DPI_SCALING, which this backend sets before it starts video. A
     * host that starts video itself should set it first; otherwise the host's DPI mode stands
     * and only the scale follows the display, from its DPI.
     */
    bool display_scale;
    void (*request_frame)(void *userdata);
    void *frame_userdata;
};

const struct inkcell_backend *inkcell_backend_sdl(void);

/*
 * Whether this build has SDL2 in it *and* a video driver that will start.
 *
 * Both halves matter and the second is why this is a probe rather than a compile-time answer:
 * an SDL linked against X11 on a machine with no display refuses exactly as a missing
 * /dev/fb0 does, and an application choosing a backend wants the two to be the same kind of
 * no. Cheap enough to call once at startup - it starts the video subsystem and stops it again -
 * and not cheap enough to call per frame.
 */
bool inkcell_backend_sdl_is_available(void);

/*
 * An SDL scancode as the evdev code that means the same key, or 0 for one this client has no
 * use for.
 *
 * The seam that keeps there from being two keyboard conventions. inkcell already answers "which
 * cap is this key?" for evdev - in src/input/input.c, over a device profile, under
 * <PREFIX>_QUIT_KEYS - and a second table mapping SDL's scancodes straight to `enum
 * inkcell_key` would be a second set of answers to drift away from the first. So this translates
 * only as far as the code the existing table already understands, and inkcell_input_map_key()
 * and inkcell_input_is_quit_key() take it from there.
 *
 * Takes an int rather than an SDL_Scancode so that this header stays free of SDL's, the way
 * inkcell/ui/fb.h stays free of <linux/fb.h> - see scripts/check-platform.py. Pure, and public
 * so the translation is testable without a window.
 */
uint16_t inkcell_sdl_evdev_code(int scancode);

/*
 * The body scale, in units, that `theme_scale` becomes on a display of `density` pixels per
 * window point.
 *
 * A theme's scale is stated for the panel, and the panel is a dense one: a desktop point at the
 * size a laptop is read from is two of its pixels. So a Retina display (density 2) keeps the
 * theme's scale and draws it at the same size in points as every other application does, and a
 * display of one pixel per point halves it. Rounded to half a step, which is the finest a type
 * role is ever offset by, and clamped to what a theme may ask for. A density that is not a
 * positive number is the theme's scale unchanged.
 *
 * Pure, and public so the arithmetic is testable without a display to measure.
 */
int inkcell_sdl_display_scale(int theme_scale, float density);

#ifdef __cplusplus
}
#endif

#endif /* INKCELL_UI_SDL_H */
