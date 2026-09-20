#pragma once

#include "inkcell/ui/backend.h"

#include <stdbool.h>

struct inkcell_fb_app;

/*
 * What the fb backend is opened with: the thing that draws the frame.
 *
 * Installed at init() rather than pushed in afterwards, because between the two there is a
 * backend that would present a blank panel - and because an application that can forget to
 * install a renderer is one that has a way to come up with no UI. The structure is copied, so it
 * may be a temporary; what it points at must outlive the backend.
 *
 * NULL is legal and draws nothing, which is what a harness measuring the panel wants.
 */
struct inkcell_backend_fb_context {
    const struct inkcell_fb_app *app;
};

const struct inkcell_backend *inkcell_backend_fb(void);
bool inkcell_backend_fb_is_available(void);
