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
 * blit, a window on a development host where there was only a screenshot before,
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
 * a window on a development host is the geometry the device will actually draw, and a layout
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
 * The mouse needs nothing here to work. A click on a hint in the action bar is a press of that
 * hint's key and arrives through `on_key`; so does the wheel, as up and down, and the thumb
 * button, as B. `on_click` is for everything else the frame registered - a row, a tab - and
 * hears the id under the pointer; NULL drops those clicks. See inkcell/ui/pointer.h.
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
    void *key_userdata;
    inkcell_click_handler on_click;
    void *click_userdata;
    const char *title;
    uint32_t width;
    uint32_t height;
    bool unified_titlebar;
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

#ifdef __cplusplus
}
#endif

#endif /* INKCELL_UI_SDL_H */
