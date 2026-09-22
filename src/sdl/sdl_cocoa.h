#ifndef INKCELL_SDL_COCOA_H
#define INKCELL_SDL_COCOA_H

/*
 * What the window backend asks of AppKit: that the title bar stop being a separate strip above
 * the frame.
 *
 * Not public - sdl.c is the only caller, and only on a Mac. There are two ways to ask, and the
 * application picks one through `unified_titlebar` on the SDL context:
 *
 *   - **Blended.** The title bar stays where it is and is painted the tab strip's colour, with
 *     its title and separator gone. Nothing in the frame moves.
 *
 *   - **Unified.** The frame runs up under a transparent title bar, the window's three buttons
 *     are moved down into the tab strip and centred on it, and the tabs start clear of them.
 *     This is what a native app's toolbar does, and it is the one that costs the frame
 *     something: `top_leading_inset`, which is 0 everywhere but here.
 */

#include "inkcell/ui/theme.h"

#include <SDL.h>
#include <stdbool.h>

/*
 * Paints `window`'s title bar in `bar`, hides its title and drops the separator, and picks the
 * light or dark appearance that suits `bar` - which is what the buttons' resting grey follows.
 * Called again whenever the theme changes. A window that is not a Cocoa one is left alone.
 */
void inkcell_sdl_cocoa_blend_titlebar(SDL_Window *window, struct inkcell_rgb bar);

/*
 * Once, after the window is created: the frame is extended under the title bar.
 *
 * `lock_aspect` holds the window to `panel_w`:`panel_h`, so a resize scales the frame and never
 * letterboxes it - which is what keeps the strip at the window's top edge, under the buttons.
 * That is the *fixed* frame's requirement and only its: a window that re-measures has no
 * letterbox to avoid, and locking it would leave a Mac window that cannot be dragged to any
 * shape but the handheld panel's.
 *
 * False when the window is not a Cocoa one, and then nothing was changed.
 */
bool inkcell_sdl_cocoa_unify_titlebar(SDL_Window *window, int panel_w, int panel_h,
                                      bool lock_aspect);

/*
 * The surface's dimensions have changed; the controls' arithmetic must be told.
 *
 * `inkcell_sdl_cocoa_place_controls()` converts between panel pixels and window points by the
 * ratio between the two, and it holds the panel's half of that from the unify call. Under a
 * fixed frame that half never changes and this is never needed. A re-measuring window replaces
 * its surface on every resize, and a stale panel height makes the ratio wrong by exactly the
 * factor the window has grown by - so a window dragged to twice its opening height reports half
 * the inset it should, and the tabs come back out under the buttons.
 *
 * A no-op when no window has been unified.
 */
void inkcell_sdl_cocoa_set_panel_size(int panel_w, int panel_h);

/* Where the buttons ended up, in the two units the backend needs it in. */
struct inkcell_sdl_cocoa_controls {
    /* How far the first tab has to start from the frame's left edge, in panel pixels. */
    int inset_px;
    /* How tall the draggable strip is, in window points - what SDL's hit test is asked in. */
    int strip_points;
};

/*
 * Puts the three buttons in a tab strip `strip_px` panel pixels tall, and says what that costs
 * the frame. Cheap, and safe to call on every resize: AppKit lays the title bar out again on
 * each one and puts the buttons back where it keeps them, so this is re-applied from AppKit's
 * own resize notification as well. In full screen the buttons are AppKit's to show and the
 * answer is zero on both counts.
 */
struct inkcell_sdl_cocoa_controls inkcell_sdl_cocoa_place_controls(SDL_Window *window,
                                                                   int strip_px);

#endif /* INKCELL_SDL_COCOA_H */
