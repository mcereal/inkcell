#ifndef INKCELL_BACKENDS_FB_WIDGETS_SCAFFOLD_H
#define INKCELL_BACKENDS_FB_WIDGETS_SCAFFOLD_H

/*
 * The scaffold: where the chrome goes, decided once per frame from how much room there is.
 *
 * inkcell/ui/widgets/chrome.h is a set of bars and inkcell/ui/stack.h is a set of width classes,
 * and until this file nothing joined them. The navigation bar is a strip across the top because
 * the first surface was a handheld panel; the width classes arrived with the window and said,
 * in so many words, that a medium surface wants a rail instead of a strip and an expanded one
 * wants two panes. Every application that took that advice would have had to write the same
 * branch - which bar, where, and what is left for the body - and a set of applications that
 * each wrote it is a set that changes shape at different widths.
 *
 * So the branch is here, and it is Material's adaptive navigation, which every platform with a
 * resizable window has converged on:
 *
 *   compact    a navigation bar across the bottom (or the tab strip across the top - see enum
 *              inkcell_fb_compact_nav for why an application may keep it)
 *   medium     a navigation rail down the leading edge
 *   expanded   the rail, and the body split into a list pane and a detail pane
 *
 * **What the application still owns** is everything that is a *fact* rather than a placement:
 * which destinations exist, what they are called, which one is active, what their badges say,
 * what a press on one does, and whether this screen has a detail worth showing beside its list.
 * The scaffold never learns what a destination is - it is handed the same `struct inkcell_fb_chip`
 * array the tab strip already takes, and the `focus_id` on each is how a press finds its way back.
 *
 * **What it owns** is where each of those lands and what is left: the destinations, the app bar,
 * the banner and the progress hairline (the client's global status), the content viewport, the
 * panes, and the action bar (the contextual actions, and the status line under them).
 *
 * ---- how a frame uses it ----
 *
 *     struct inkcell_fb_scaffold_frame frame;
 *     inkcell_fb_scaffold_begin(state, &scaffold, &frame);
 *     ... draw the list (or the only pane) against frame.layout ...
 *     if (frame.split) {
 *         struct inkcell_fb_layout detail = inkcell_fb_scaffold_detail(state, &frame, &bar);
 *         ... draw the selected item against `detail` ...
 *     }
 *     inkcell_fb_scaffold_end(state, &frame, &actions);
 *
 * Between begin and end the frame's region (inkcell_fb_region()) is narrowed to the pane being
 * drawn, so every widget that places itself against the content column - a list row, a card, the
 * FAB, an empty state - lands in the pane without being told there is one. That is the whole
 * trick, and it is why no widget in chrome.h had to learn a new parameter: a screen renderer
 * written for a phone-shaped panel draws into a pane unchanged.
 *
 * ---- what it is not ----
 *
 * **Not a router.** It does not know what "back" means, which pane has the cursor, or what the
 * detail pane should show when nothing is selected. Those are the application's nav, and a
 * toolkit that answered them would be a toolkit with an opinion about every application's tree.
 *
 * **Not a second set of bars.** The top strip, the banner, the app bar and the action bar are the
 * same calls chrome.h always made, in the same order. In the compact-top arrangement the scaffold
 * is exactly that sequence - which tests/suites/ui_scaffold.c holds to the pixel - so an
 * application can move onto it without its handheld frame changing at all.
 */

/*
 * inkcell/ui/widgets.h is the umbrella over this file and its siblings; include either.
 */

#include "inkcell/ui/fb_draw.h"
#include "inkcell/ui/widgets/button.h"
#include "inkcell/ui/widgets/chrome.h"

#include "inkcell/ui/stack.h"

#include <stdbool.h>
#include <stddef.h>

/*
 * Where the destinations went. Answered by the scaffold and read back off the frame, so a backend
 * that has to know - the window making its title-bar strip a drag handle, say - asks rather than
 * working it out again.
 */
enum inkcell_fb_nav_placement {
    /* No destinations were given: the content is the whole region. */
    INKCELL_FB_NAV_NONE = 0,
    /* The recessed tab strip across the top - inkcell_fb_draw_nav_bar(). */
    INKCELL_FB_NAV_TOP,
    /* A navigation bar across the bottom: one equal cell per destination, icon over label. */
    INKCELL_FB_NAV_BOTTOM,
    /* A navigation rail down the leading edge: the same cells, stacked. */
    INKCELL_FB_NAV_RAIL,
};

/*
 * What a compact frame does with its destinations.
 *
 * Bottom is the zero value because it is the answer for a surface that is touched: the bottom
 * edge is where a thumb already is, which is the whole of Material's argument for the bar.
 *
 * Top is here because that argument does not hold everywhere. A handheld with shoulder buttons
 * that step through the tabs has its tab controls at the *top* of the case, and a strip under
 * them is the strip the reader's eye goes to when their finger does; the action bar already
 * owns the bottom edge besides, and two bars stacked there is a frame with a heavy foot. So an
 * application whose compact surface is that kind of device keeps the strip, and gets the rail and
 * the split on every wider surface all the same.
 */
enum inkcell_fb_compact_nav {
    INKCELL_FB_COMPACT_NAV_BOTTOM = 0,
    INKCELL_FB_COMPACT_NAV_TOP,
};

/* What the application says about this frame. Every pointer may be NULL, and a zeroed struct is a
   frame with nothing in it but a body. */
struct inkcell_fb_scaffold {
    /* The destinations, in order, and which one is active. The same array the tab strip takes,
       badges and focus ids included. */
    const struct inkcell_fb_chip *destinations;
    size_t count;
    size_t active;
    enum inkcell_fb_compact_nav compact_nav;
    /* Whether an action bar will be passed to inkcell_fb_scaffold_end(). Needed now rather than
       then for the reason inkcell_fb_layout_begin() needs it: the body is laid out long before
       the bar is drawn. */
    bool footer;
    /* Whether B leaves - struct inkcell_fb_layout's `back`, asked once for the whole frame. */
    bool back;
    /*
     * Whether this screen has a list and a detail that could stand side by side.
     *
     * A statement about the screen, not a request for two panes: the scaffold splits only when
     * the frame is expanded, and on anything narrower the application shows one or the other as
     * it always has. That is why `frame.split` is read back rather than assumed - the same screen
     * is one pane in a phone-sized window and two in a maximised one.
     */
    bool split;
    /* The progress hairline: the client is waiting on something. */
    bool busy;
    /* The persistent notice, across the whole content viewport. NULL, or one whose text is empty,
       draws nothing. */
    const struct inkcell_fb_banner *banner;
    /* The screen's heading, over the list pane (or the only one). NULL for a screen that draws its
       own - a collapsing title, say - into `frame.layout` after begin. */
    const struct inkcell_fb_app_bar *app_bar;
};

/* What the scaffold made of it. Filled by inkcell_fb_scaffold_begin(); read-only after that. */
struct inkcell_fb_scaffold_frame {
    /* The frame's own class, from the whole region - not a pane's. */
    enum inkcell_width_class width;
    enum inkcell_fb_nav_placement nav;
    /* Where the destinations are drawn: the strip, the bar or the rail. Empty for none. */
    struct inkcell_box nav_box;
    /* The viewport: the region less the destinations. Everything the screen and the action bar
       are drawn in, and the band a transition slides within. */
    struct inkcell_box content;
    /* The two panes, top to bottom of the content. With no split `list` is the content and
       `detail` is empty. */
    struct inkcell_box list;
    struct inkcell_box detail;
    bool split;
    /* The layout for the list pane (or the only one): chrome already drawn, `body_y` below it. */
    struct inkcell_fb_layout layout;
    /* Where the panes start: below the banner, which spans both. */
    int panes_y;
    /* The region as it was before begin, which end puts back. */
    struct inkcell_box saved_region;
};

/*
 * Draws the destinations, the progress hairline, the banner and the app bar, and narrows the
 * region to the list pane (or the whole content) for the screen to draw into.
 *
 * Clears nothing: the caller has painted the ground, for the reason every other chrome call
 * leaves it - a frame is cleared once, by whoever owns the frame.
 */
void inkcell_fb_scaffold_begin(struct inkcell_draw_state *state,
                               const struct inkcell_fb_scaffold *scaffold,
                               struct inkcell_fb_scaffold_frame *frame);

/*
 * Moves the region to the detail pane and hands back a layout for it, with `bar` (NULL for none)
 * already drawn at its top.
 *
 * Only meaningful when `frame->split`; otherwise it returns an empty layout and moves nothing,
 * so a caller that forgot to test draws nothing rather than drawing over the list.
 */
struct inkcell_fb_layout inkcell_fb_scaffold_detail(struct inkcell_draw_state *state,
                                                    const struct inkcell_fb_scaffold_frame *frame,
                                                    const struct inkcell_fb_app_bar *bar);

/*
 * Starts the frame's transition across the whole content viewport, both panes of a split
 * included, and returns the offset it applied - 0 when nothing is moving, in which case nothing
 * was started. Pair with inkcell_fb_shift_end().
 *
 * Here rather than left to inkcell_fb_shift_begin() because the band across is the region at the
 * moment of the call, and between begin and end the region is one *pane*: a screen that slid its
 * list pane alone would leave the detail standing still beside it. The rows are the panes' - the
 * destinations, the banner and the action bar are the same on both sides of a move.
 */
int inkcell_fb_scaffold_transition(struct inkcell_draw_state *state,
                                   const struct inkcell_fb_scaffold_frame *frame);

/*
 * Draws the action bar (NULL for none) across the whole content viewport, under both panes, and
 * puts the region back as it was before begin.
 *
 * Last, like the bar has always been: it is drawn over whatever a list scrolled under it.
 */
void inkcell_fb_scaffold_end(struct inkcell_draw_state *state,
                             const struct inkcell_fb_scaffold_frame *frame,
                             const struct inkcell_fb_action_bar *actions);

/*
 * The room the destinations take, without drawing them: the bottom bar's height, or the rail's
 * width. For a backend or a test that has to know before a frame is drawn. 0 for the top strip,
 * whose height is inkcell_fb_nav_bar_height().
 */
int inkcell_fb_scaffold_bar_height(const struct inkcell_draw_state *state);
int inkcell_fb_scaffold_rail_width(const struct inkcell_draw_state *state,
                                   const struct inkcell_fb_chip *destinations, size_t count);

#endif /* INKCELL_BACKENDS_FB_WIDGETS_SCAFFOLD_H */
