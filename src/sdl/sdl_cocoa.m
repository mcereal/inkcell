/*
 * The title bar, blended into the frame or folded into its tab strip. See sdl_cocoa.h.
 *
 * Compiled on a Mac with SDL2 and nowhere else, so there is no stub: sdl.c calls this behind
 * the same __APPLE__ that decides whether the file is built.
 */

#include "sdl_cocoa.h"

#include <SDL_syswm.h>

#import <Cocoa/Cocoa.h>

/*
 * The one window this is doing it to.
 *
 * File scope because AppKit is the second caller: it lays the title bar out again on every
 * resize - including every step of a live one, which runs inside AppKit's own loop and not
 * this client's - and puts the buttons back where it keeps them. The observer that moves them
 * back again needs the geometry without being handed it, and the backend opens one window.
 */
static struct {
    NSWindow *window;
    id observers[3];
    int panel_w;
    int panel_h;
    int strip_px;
} g_unified;

static NSWindow *inkcell_sdl_cocoa_window(SDL_Window *window) {
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (window == NULL || !SDL_GetWindowWMInfo(window, &info) ||
        info.subsystem != SDL_SYSWM_COCOA) {
        return nil;
    }
    return info.info.cocoa.window;
}

void inkcell_sdl_cocoa_blend_titlebar(SDL_Window *window, struct inkcell_rgb bar) {
    NSWindow *const nswindow = inkcell_sdl_cocoa_window(window);
    if (nswindow == nil) {
        return;
    }

    /*
     * Device RGB rather than sRGB, because that is how the frame's own pixels reach the
     * screen: SDL's renderer hands the texture over untagged, so a colour-managed sRGB value
     * here would be converted into the display's space and land a shade off the strip it is
     * meant to continue. Unified, this is only ever seen for the moment a resize outruns the
     * frame - which is exactly when a second shade would show.
     */
    nswindow.titlebarAppearsTransparent = YES;
    nswindow.titleVisibility = NSWindowTitleHidden;
    nswindow.backgroundColor = [NSColor colorWithDeviceRed:bar.r / 255.0
                                                     green:bar.g / 255.0
                                                      blue:bar.b / 255.0
                                                     alpha:1.0];
    if (@available(macOS 11.0, *)) {
        nswindow.titlebarSeparatorStyle = NSTitlebarSeparatorStyleNone;
    }

    /* The buttons' resting grey follows the appearance, not the bar - so a light theme on a
       dark desktop would put dark-mode controls on a pale bar. Rec. 601 luma is plenty to pick
       a side. */
    const unsigned luma = (299U * bar.r + 587U * bar.g + 114U * bar.b) / 1000U;
    nswindow.appearance = [NSAppearance
        appearanceNamed:luma < 128U ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua];
}

static bool inkcell_sdl_cocoa_fullscreen(NSWindow *nswindow) {
    return (nswindow.styleMask & NSWindowStyleMaskFullScreen) != 0;
}

/*
 * The buttons, centred on the strip. Returns what they cost the frame.
 *
 * The same move Electron's trafficLightPosition makes, for the same reason: AppKit has no
 * setting for where the buttons go, only a title bar view it sizes itself. So the view that
 * holds them is made as tall as the strip and pinned to the window's top, and each button is
 * set down in it - centred vertically, and as far in from the left edge as that leaves above
 * and below, which is how a toolbar's buttons sit in a native window.
 */
static struct inkcell_sdl_cocoa_controls inkcell_sdl_cocoa_apply(void) {
    struct inkcell_sdl_cocoa_controls controls = {0};
    NSWindow *const nswindow = g_unified.window;
    if (nswindow == nil || g_unified.panel_w <= 0 || g_unified.panel_h <= 0 ||
        inkcell_sdl_cocoa_fullscreen(nswindow)) {
        return controls;
    }

    NSButton *const buttons[3] = {
        [nswindow standardWindowButton:NSWindowCloseButton],
        [nswindow standardWindowButton:NSWindowMiniaturizeButton],
        [nswindow standardWindowButton:NSWindowZoomButton],
    };
    NSView *const titlebar = buttons[0].superview.superview;
    if (buttons[0] == nil || buttons[1] == nil || buttons[2] == nil || titlebar == nil) {
        return controls;
    }

    /* The window is held to the panel's aspect, so points per panel pixel are one number. */
    const NSSize content = nswindow.contentView.bounds.size;
    const CGFloat points_per_px = content.height / (CGFloat)g_unified.panel_h;
    const CGFloat strip = (CGFloat)g_unified.strip_px * points_per_px;

    const NSSize button = buttons[0].frame.size;
    /* AppKit's own pitch between two buttons, read off the pair rather than assumed - it is
       not the same number on every release. Read before anything is moved, and never changed
       by moving them, since all three move together. */
    const CGFloat pitch = buttons[1].frame.origin.x - buttons[0].frame.origin.x;
    const CGFloat height = strip > button.height ? strip : button.height;
    const CGFloat margin_y = (height - button.height) / 2.0;
    /* Square to the strip's air above and below, within what still reads as a title bar: at
       a small window the buttons would otherwise touch the edge, and at a large one drift off
       towards the first tab. */
    const CGFloat margin_x = margin_y < 8.0 ? 8.0 : (margin_y > 20.0 ? 20.0 : margin_y);

    NSRect frame = titlebar.frame;
    frame.size.height = height;
    frame.origin.y = nswindow.frame.size.height - height;
    titlebar.frame = frame;
    for (size_t i = 0U; i < 3U; ++i) {
        [buttons[i] setFrameOrigin:NSMakePoint(margin_x + (CGFloat)i * pitch, margin_y)];
    }

    /* The zoom button's far edge and the same margin again, so the first tab sits as far from
       the buttons as the buttons sit from the window's edge. */
    const CGFloat clear = margin_x + 2.0 * pitch + button.width + margin_x;
    controls.inset_px = (int)ceil(clear / points_per_px);
    controls.strip_points = (int)strip;
    return controls;
}

bool inkcell_sdl_cocoa_unify_titlebar(SDL_Window *window, int panel_w, int panel_h) {
    NSWindow *const nswindow = inkcell_sdl_cocoa_window(window);
    if (nswindow == nil || panel_w <= 0 || panel_h <= 0) {
        return false;
    }
    g_unified.window = nswindow;
    g_unified.panel_w = panel_w;
    g_unified.panel_h = panel_h;

    nswindow.styleMask |= NSWindowStyleMaskFullSizeContentView;
    nswindow.titlebarAppearsTransparent = YES;
    nswindow.titleVisibility = NSWindowTitleHidden;

    /*
     * The panel's shape, held. The content view now takes in what was the title bar, so the
     * window is one bar taller than the panel's aspect until it is resized - and it is resized
     * here rather than left for the first drag, because SDL learns its drawable's size from
     * AppKit's resize notification and a style change alone does not send one.
     */
    nswindow.contentAspectRatio = NSMakeSize(panel_w, panel_h);
    const CGFloat width = nswindow.contentView.bounds.size.width;
    [nswindow setContentSize:NSMakeSize(width, width * (CGFloat)panel_h / (CGFloat)panel_w)];

    /* AppKit puts the buttons back on every layout of the title bar; these put them where they
       go again. Leaving full screen is a layout that is not a resize. */
    NSNotificationCenter *const center = NSNotificationCenter.defaultCenter;
    NSString *const names[3] = {NSWindowDidResizeNotification,
                                NSWindowDidExitFullScreenNotification,
                                NSWindowDidBecomeKeyNotification};
    for (size_t i = 0U; i < 3U; ++i) {
        if (g_unified.observers[i] != nil) {
            [center removeObserver:g_unified.observers[i]];
        }
        g_unified.observers[i] = [center addObserverForName:names[i]
                                                     object:nswindow
                                                      queue:nil
                                                 usingBlock:^(NSNotification *note) {
                                                   (void)note;
                                                   (void)inkcell_sdl_cocoa_apply();
                                                 }];
    }
    return true;
}

struct inkcell_sdl_cocoa_controls inkcell_sdl_cocoa_place_controls(SDL_Window *window,
                                                                   int strip_px) {
    if (inkcell_sdl_cocoa_window(window) != g_unified.window) {
        return (struct inkcell_sdl_cocoa_controls){0};
    }
    g_unified.strip_px = strip_px;
    return inkcell_sdl_cocoa_apply();
}
