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

#include "inkcell/ui/sdl.h"
#include "inkcell/ui/theme.h"

#include <SDL.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
    /* How tall the band the buttons stand in is, in panel pixels: the strip, or taller where the
       strip would have put them against the window's top edge. */
    int band_px;
    /* How tall the draggable strip is, in window points - what SDL's hit test is asked in. The
       band, so the whole of what reads as the title bar drags the window. */
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

/*
 * The application's menus, in the menu bar SDL put up.
 *
 * SDL builds a bar of its own when it starts AppKit - the application menu (About, a
 * Preferences item that does nothing, Hide, Quit) and Window - and this adds to it rather than
 * replacing it: File, Edit, View and Go go in front of Window, Help after it, and the
 * application's own items take the dead Preferences item's place. Choosing one pushes an SDL
 * event of `event_type` whose `user.code` is the item's index in `items`, so the application
 * hears it from its own loop turn and not from inside AppKit's menu tracking. `enabled` is
 * asked, with that index, whenever AppKit validates the item - as a menu opens, and before a
 * chord is let through.
 */
struct inkcell_sdl_cocoa_menu {
    const struct inkcell_sdl_menu_item *items;
    size_t count;
    const inkcell_str_id *titles;
    bool (*enabled)(void *ctx, size_t index);
    void *ctx;
    uint32_t event_type;
};

/* False when `window` is not a Cocoa one or SDL put up no bar, and then nothing was changed.
   `menu` is copied; what it points at is read until inkcell_sdl_cocoa_remove_menu(). */
bool inkcell_sdl_cocoa_install_menu(SDL_Window *window, const struct inkcell_sdl_cocoa_menu *menu);

/* Every title read again from the catalog: the locale has changed. */
void inkcell_sdl_cocoa_retitle_menu(void);

/* The bar back as SDL left it. A no-op when nothing was installed. */
void inkcell_sdl_cocoa_remove_menu(void);

#endif /* INKCELL_SDL_COCOA_H */
