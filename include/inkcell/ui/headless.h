#ifndef INKCELL_UI_HEADLESS_H
#define INKCELL_UI_HEADLESS_H

/*
 * The panel with nothing behind it: every frame drawn, into memory, and shown to nobody.
 *
 * The fb backend needs /dev/fb0 and the window backend needs a display, and a cloud session or
 * a CI runner has neither - which left an application there with a terminal backend that draws
 * no pixels at all. This one draws exactly what the device would, through the same
 * inkcell_fb_render() into the same kind of page the off-screen capture uses, against the real
 * clock rather than a scripted one. So a client run here animates, pages and resolves presses
 * the way it does on a Brick, and its `frame` hook (inkcell/ui/backend.h) hands back the picture -
 * which is the whole point: something driving the application by its keys can look at what it
 * pressed.
 *
 * Unlike inkcell/ui/fb_capture.h, which is a harness that names its theme, its scale and its
 * time, this is a backend an application runs: the theme and the scale come from the
 * environment as the fb backend's do, and the clock is the monotonic one.
 *
 * Always available. Reads no input: whatever drives it presses keys through the application.
 */

#include "inkcell/ui/backend.h"

#include <stdbool.h>
#include <stdint.h>

struct inkcell_fb_app;

#ifdef __cplusplus
extern "C" {
#endif

/*
 * What the headless backend is opened with. `app` is the fb backend's - the thing that draws a
 * frame - and NULL draws nothing. `width`/`height` of 0 take the Brick's panel.
 */
struct inkcell_backend_headless_context {
    const struct inkcell_fb_app *app;
    uint32_t width;
    uint32_t height;
};

const struct inkcell_backend *inkcell_backend_headless(void);

#ifdef __cplusplus
}
#endif

#endif /* INKCELL_UI_HEADLESS_H */
