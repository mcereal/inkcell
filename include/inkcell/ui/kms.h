#ifndef INKCELL_UI_KMS_H
#define INKCELL_UI_KMS_H

/*
 * The panel through DRM/KMS: /dev/dri/cardN, dumb buffers and a page flip.
 *
 * fbdev is the old way to a panel, and a phone's kernel may not offer it at all - Android's
 * display stack talks to the DRM driver, so the emulated /dev/fb0 is often compiled out. This
 * backend talks to that driver the same way: it picks a connected connector and its preferred
 * mode, finds a CRTC and the primary plane that can drive it, and puts the frame up with an
 * atomic commit. A driver that refuses atomic gets the legacy SETCRTC and PAGE_FLIP calls
 * instead, and <PREFIX>_KMS_LEGACY=1 asks for those regardless.
 *
 * The frame is drawn on the CPU into ordinary RAM, exactly as the fb backend draws it, and only
 * the rows that changed are copied into a dumb buffer. There are two of those: the panel shows
 * one while the next frame is copied into the other, and a flip swaps them on the vertical
 * blank, so a frame is never seen half-written. A buffer is two frames behind when its turn
 * comes round, so it is owed the rows of both.
 *
 * A present() that arrives while the last flip is still pending waits for it, for at most a
 * tenth of a second. That is the only blocking this backend does, and it is what holds an
 * animation to the panel's refresh rather than to however fast the loop can draw.
 *
 * It needs DRM master, which the first process to open the card gets while nobody else holds
 * it. A desktop with a compositor running already has it, and init() then fails at the first
 * modeset rather than fighting over the panel.
 *
 * No libdrm: the ioctls are the kernel's own, from its uapi headers, so the backend links into
 * a static binary with nothing beside it. On a host without DRM - macOS, or a Linux build
 * without the headers - the entry points exist and refuse.
 */

#include "inkcell/ui/backend.h"

#include <stdbool.h>

struct inkcell_fb_app;

#ifdef __cplusplus
extern "C" {
#endif

/*
 * What the KMS backend is opened with. `app` draws the frame, as it does for the fb backend,
 * and NULL draws nothing. `device` is the card to open; NULL takes <PREFIX>_KMS_DEVICE, and
 * failing that the first /dev/dri/cardN with a connected connector.
 */
struct inkcell_backend_kms_context {
    const struct inkcell_fb_app *app;
    const char *device;
};

const struct inkcell_backend *inkcell_backend_kms(void);

/* Whether there is a card to try: a cheap check, not a promise that init() will succeed. */
bool inkcell_backend_kms_is_available(void);

#ifdef __cplusplus
}
#endif

#endif /* INKCELL_UI_KMS_H */
