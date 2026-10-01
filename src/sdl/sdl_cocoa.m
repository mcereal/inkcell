/*
 * The title bar, blended into the frame or folded into its tab strip, and the menu bar. See
 * sdl_cocoa.h.
 *
 * Compiled on a Mac with SDL2 and nowhere else, so there is no stub: sdl.c calls this behind
 * the same __APPLE__ that decides whether the file is built.
 */

#include "sdl_cocoa.h"

#include <SDL_syswm.h>

#include <string.h>

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

/* The least air above and below the buttons, in points: what a plain title bar leaves. */
#define INKCELL_SDL_COCOA_MARGIN_Y 7.0

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

    /*
     * Points per panel pixel, which is one number in both modes for two different reasons: a
     * fixed frame is held to the panel's aspect and scaled, and a re-measuring one *is* the
     * window, so the ratio comes out at 1. Either way it depends on the panel height being the
     * surface's current one - see inkcell_sdl_cocoa_set_panel_size().
     */
    const NSSize content = nswindow.contentView.bounds.size;
    const CGFloat points_per_px = content.height / (CGFloat)g_unified.panel_h;
    const CGFloat strip = (CGFloat)g_unified.strip_px * points_per_px;

    const NSSize button = buttons[0].frame.size;
    /* AppKit's own pitch between two buttons, read off the pair rather than assumed - it is
       not the same number on every release. Read before anything is moved, and never changed
       by moving them, since all three move together. */
    const CGFloat pitch = buttons[1].frame.origin.x - buttons[0].frame.origin.x;
    /* Never closer to the window's top edge than a plain title bar sets them - its 28 points
       around a 14-point button. The tab strip is shorter than that at a desktop scale, and
       centred on it the buttons touched the edge; the band grows instead, and the frame is
       told how tall it came out (`band_px`) so what it lays out under the buttons clears them. */
    const CGFloat least = button.height + 2.0 * INKCELL_SDL_COCOA_MARGIN_Y;
    const CGFloat height = strip > least ? strip : least;
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
    controls.band_px = (int)ceil(height / points_per_px);
    controls.strip_points = (int)height;
    return controls;
}

void inkcell_sdl_cocoa_set_panel_size(int panel_w, int panel_h) {
    if (g_unified.window == nil || panel_w <= 0 || panel_h <= 0) {
        return;
    }
    g_unified.panel_w = panel_w;
    g_unified.panel_h = panel_h;
}

bool inkcell_sdl_cocoa_unify_titlebar(SDL_Window *window, int panel_w, int panel_h,
                                      bool lock_aspect) {
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
     * The panel's shape, held where the caller asked for it - see `lock_aspect`.
     *
     * The content view now takes in what was the title bar, so the window is one bar taller
     * than the panel's aspect until it is resized. It is resized here either way rather than
     * left for the first drag, because SDL learns its drawable's size from AppKit's resize
     * notification and a style change alone does not send one.
     */
    if (lock_aspect) {
        nswindow.contentAspectRatio = NSMakeSize(panel_w, panel_h);
    }
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

/*
 * The menus. One bar per process, as one window per backend, so file scope again: AppKit asks
 * the target to validate an item without saying which window it was for.
 */
static struct {
    struct inkcell_sdl_cocoa_menu spec;
    id target;
    /* What was put in the bar, to take out again: the top-level items, and what went into the
       application's menu, which stays. */
    NSMutableArray *tops;
    NSMutableArray *app_items;
    /* SDL's Preferences item, which the application's took the place of. */
    NSMenuItem *preferences;
    NSInteger preferences_at;
} g_menu;

static NSString *inkcell_sdl_cocoa_text(inkcell_str_id id) {
    const char *const text = inkcell_str(id);
    NSString *const string = text != NULL ? [NSString stringWithUTF8String:text] : nil;
    return string != nil ? string : @"";
}

@interface InkcellMenuTarget : NSObject
- (void)choose:(NSMenuItem *)item;
@end

@implementation InkcellMenuTarget

/* An event and not a call: this is inside AppKit's menu tracking, and the application's loop
   turn is where anything it does belongs. */
- (void)choose:(NSMenuItem *)item {
    SDL_Event event;
    SDL_zero(event);
    event.type = g_menu.spec.event_type;
    event.user.type = g_menu.spec.event_type;
    event.user.code = (Sint32)item.tag;
    (void)SDL_PushEvent(&event);
}

- (BOOL)validateMenuItem:(NSMenuItem *)item {
    const NSInteger index = item.tag;
    if (index < 0 || (size_t)index >= g_menu.spec.count) {
        return NO;
    }
    return g_menu.spec.enabled == NULL || g_menu.spec.enabled(g_menu.spec.ctx, (size_t)index);
}

@end

static NSMenuItem *inkcell_sdl_cocoa_item(size_t index) {
    const struct inkcell_sdl_menu_item *const spec = &g_menu.spec.items[index];
    const char key[2] = {spec->key, '\0'};
    NSMenuItem *const item =
        [[NSMenuItem alloc] initWithTitle:inkcell_sdl_cocoa_text(spec->label)
                                   action:@selector(choose:)
                            keyEquivalent:[NSString stringWithUTF8String:key]];
    item.keyEquivalentModifierMask = NSEventModifierFlagCommand;
    item.target = g_menu.target;
    item.tag = (NSInteger)index;
    return [item autorelease];
}

bool inkcell_sdl_cocoa_install_menu(SDL_Window *window, const struct inkcell_sdl_cocoa_menu *menu) {
    NSMenu *const bar = NSApp.mainMenu;
    if (inkcell_sdl_cocoa_window(window) == nil || bar == nil || menu == NULL ||
        menu->items == NULL || menu->count == 0U || menu->titles == NULL) {
        return false;
    }
    inkcell_sdl_cocoa_remove_menu();
    g_menu.spec = *menu;
    g_menu.target = [[InkcellMenuTarget alloc] init];
    g_menu.tops = [[NSMutableArray alloc] init];
    g_menu.app_items = [[NSMutableArray alloc] init];
    g_menu.preferences_at = -1;

    /* The application's menu: in the dead Preferences item's place, which is after About and
       its separator - or there, when SDL has stopped putting one up. */
    NSMenu *const app_menu = bar.numberOfItems > 0 ? [bar itemAtIndex:0].submenu : nil;
    NSInteger app_at = app_menu != nil && app_menu.numberOfItems >= 2 ? 2 : 0;
    if (app_menu != nil) {
        for (NSInteger i = 0; i < app_menu.numberOfItems; ++i) {
            NSMenuItem *const candidate = [app_menu itemAtIndex:i];
            if ([candidate.keyEquivalent isEqualToString:@","]) {
                g_menu.preferences = [candidate retain];
                g_menu.preferences_at = i;
                [app_menu removeItemAtIndex:i];
                app_at = i;
                break;
            }
        }
    }

    /* The rest before Window, which SDL put up and AppKit keeps the window list in; Help after
       it, because that is where a Mac reader looks for it. */
    NSInteger bar_at = bar.numberOfItems;
    if (NSApp.windowsMenu != nil) {
        const NSInteger window_at = [bar indexOfItemWithSubmenu:NSApp.windowsMenu];
        if (window_at >= 0) {
            bar_at = window_at;
        }
    }

    for (int which = INKCELL_SDL_MENU_APP; which < INKCELL_SDL_MENU_COUNT; ++which) {
        NSMenu *submenu = nil;
        if (which == INKCELL_SDL_MENU_APP) {
            submenu = app_menu;
        }
        bool any = false;
        for (size_t i = 0U; i < menu->count; ++i) {
            if ((int)menu->items[i].menu != which) {
                continue;
            }
            if (submenu == nil) {
                NSString *const title = inkcell_sdl_cocoa_text(menu->titles[which]);
                submenu = [[[NSMenu alloc] initWithTitle:title] autorelease];
                NSMenuItem *const top = [[[NSMenuItem alloc] initWithTitle:title
                                                                    action:nil
                                                             keyEquivalent:@""] autorelease];
                top.submenu = submenu;
                top.tag = which;
                if (which == INKCELL_SDL_MENU_HELP) {
                    [bar addItem:top];
                    NSApp.helpMenu = submenu;
                } else {
                    [bar insertItem:top atIndex:bar_at++];
                }
                [g_menu.tops addObject:top];
            }
            NSMenuItem *const item = inkcell_sdl_cocoa_item(i);
            if (which == INKCELL_SDL_MENU_APP) {
                if (menu->items[i].separated && any) {
                    NSMenuItem *const separator = [NSMenuItem separatorItem];
                    [submenu insertItem:separator atIndex:app_at++];
                    [g_menu.app_items addObject:separator];
                }
                [submenu insertItem:item atIndex:app_at++];
                [g_menu.app_items addObject:item];
            } else {
                if (menu->items[i].separated && any) {
                    [submenu addItem:[NSMenuItem separatorItem]];
                }
                [submenu addItem:item];
            }
            any = true;
        }
    }
    return true;
}

void inkcell_sdl_cocoa_retitle_menu(void) {
    if (g_menu.target == nil) {
        return;
    }
    for (NSMenuItem *top in g_menu.tops) {
        NSString *const title = inkcell_sdl_cocoa_text(g_menu.spec.titles[top.tag]);
        top.title = title;
        top.submenu.title = title;
        for (NSMenuItem *item in top.submenu.itemArray) {
            if (!item.isSeparatorItem) {
                item.title = inkcell_sdl_cocoa_text(g_menu.spec.items[item.tag].label);
            }
        }
    }
    for (NSMenuItem *item in g_menu.app_items) {
        if (!item.isSeparatorItem) {
            item.title = inkcell_sdl_cocoa_text(g_menu.spec.items[item.tag].label);
        }
    }
}

void inkcell_sdl_cocoa_remove_menu(void) {
    if (g_menu.target == nil) {
        return;
    }
    NSMenu *const bar = NSApp.mainMenu;
    for (NSMenuItem *top in g_menu.tops) {
        if (top.submenu == NSApp.helpMenu) {
            NSApp.helpMenu = nil;
        }
        if (top.menu != nil) {
            [top.menu removeItem:top];
        }
    }
    for (NSMenuItem *item in g_menu.app_items) {
        if (item.menu != nil) {
            [item.menu removeItem:item];
        }
    }
    if (g_menu.preferences != nil) {
        NSMenu *const app_menu = bar != nil && bar.numberOfItems > 0 ? [bar itemAtIndex:0].submenu
                                                                     : nil;
        if (app_menu != nil && g_menu.preferences_at <= app_menu.numberOfItems) {
            [app_menu insertItem:g_menu.preferences atIndex:g_menu.preferences_at];
        }
        [g_menu.preferences release];
    }
    [g_menu.tops release];
    [g_menu.app_items release];
    [g_menu.target release];
    memset(&g_menu, 0, sizeof g_menu);
}
