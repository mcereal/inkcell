#pragma once

#include "inkcell/ui/backend.h"

#include <stdbool.h>

/*
 * What the fb backend is opened with. Nothing today - the panel is a file and an ioctl - but the
 * vtable's init() takes a userdata and an application with something to hand it puts it here.
 */
struct inkcell_backend_fb_context {
    void *app;
};

const struct inkcell_backend *inkcell_backend_fb(void);
bool inkcell_backend_fb_is_available(void);
