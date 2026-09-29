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
 *   medium     a navigation rail down the leading edge, collapsed to its icons - and, once the
 *              detail would hold a whole measure beside the list, the split below
 *   expanded   the rail expanded into rows of icon and word, and the body split into a list
 *              pane and a detail pane
 *
 * The rail's two widths are also the reader's to choose between: with a toggle id the rail
 * draws the press at its head that folds and unfolds it (enum inkcell_fb_rail), and the
 * application keeps the answer.
 *
 * The split follows the detail rather than the class. The detail is running text and is held to
 * a measure; the list is short rows read by their leading edge and reads the same at two fifths
 * of the width. So a medium window with room for a measured detail splits too, and one without
 * it keeps one pane.
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
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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
    /* A navigation rail down the leading edge: icons alone, or rows of icon and word - see
       enum inkcell_fb_rail. */
    INKCELL_FB_NAV_RAIL,
};

/*
 * Which of its two widths a rail is drawn at.
 *
 * Collapsed is a column of icons; expanded is a row per destination with the icon and its word
 * side by side, which is the sidebar every desktop application draws. The words are the
 * difference between a rail a newcomer can read and one a regular can ignore, and which of the
 * two a reader is changes with the reader rather than with the window - so an application that
 * offers the toggle keeps the choice and hands it back here every frame.
 *
 * AUTO is the zero value and is the class's answer for an application that has not been told:
 * expanded where the window has room to spare (the expanded class), collapsed on a medium one
 * where the body wants every column. An expanded rail whose words would not fit the width a rail
 * may take (twenty columns of body text) is drawn collapsed whatever was asked, and
 * `frame.rail_expanded` says which it came out.
 */
enum inkcell_fb_rail {
    INKCELL_FB_RAIL_AUTO = 0,
    INKCELL_FB_RAIL_COLLAPSED,
    INKCELL_FB_RAIL_EXPANDED,
};

/*
 * What a press on the rail's toggle does, as an offset from `rail_toggle_id`.
 *
 * Two ids rather than one, because the press means opposite things depending on which rail was
 * drawn, and the application answering a click has only the id to go on: by the time it arrives
 * the frame it landed on is gone, and on AUTO the application never knew which width it was.
 * The id says what the reader saw - three bars asking to expand, or the folding arrow asking to
 * collapse - so the answer is a store, not a guess.
 */
enum inkcell_fb_rail_toggle {
    INKCELL_FB_RAIL_TOGGLE_EXPAND = 0,
    INKCELL_FB_RAIL_TOGGLE_COLLAPSE = 1,
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
    /* Which width the rail is drawn at, on a frame that has one. See enum inkcell_fb_rail. */
    enum inkcell_fb_rail rail;
    /*
     * The toggle at the rail's head, registered as `rail_toggle_id + INKCELL_FB_RAIL_TOGGLE_*`
     * (so two consecutive ids are taken). INKCELL_FOCUS_NONE - zero - draws no toggle, and a
     * rail without one is whatever `rail` says, which is what an application that has not
     * wired up the click gets.
     */
    uint32_t rail_toggle_id;
    /* Whether an action bar will be passed to inkcell_fb_scaffold_end(). Needed now rather than
       then for the reason inkcell_fb_layout_begin() needs it: the body is laid out long before
       the bar is drawn. */
    bool footer;
    /*
     * Which action bar that is, when it is not the full one: INKCELL_FB_FOOTER_COMPACT keeps one
     * row at the foot rather than two, and the body gets the other back. See
     * inkcell_fb_layout_begin_footer().
     *
     * A second field rather than `footer` changing type, so that a scaffold declared before a
     * compact bar existed still says what it said. Zero (NONE) defers to `footer`; anything else
     * wins over it, since a frame that names a bar has plainly asked for one.
     */
    enum inkcell_fb_footer footer_kind;
    /* Whether B leaves - struct inkcell_fb_layout's `back`, asked once for the whole frame. */
    bool back;
    /*
     * Whether this screen has a list and a detail that could stand side by side.
     *
     * A statement about the screen, not a request for two panes: the scaffold splits only when
     * the frame is wider than compact and the detail would hold a whole measure, and on anything
     * narrower the application shows one or the other as it always has. That is why `frame.split`
     * is read back rather than assumed - the same screen is one pane in a phone-sized window and
     * two in a maximised one.
     */
    bool split;
    /* The progress hairline: the client is waiting on something. */
    bool busy;
    /*
     * The body is not running text - a map, a canvas - so the frame is laid out without the
     * reading measure (inkcell_fb_set_measured()). Applied at the top of begin, before the app
     * bar and the layout are measured, which is the only point it can reach them: set before
     * begin it would be a leftover, and after it the heading is already drawn in the measure.
     * Every frame states it, so a screen's choice never outlives its own frame.
     */
    bool unmeasured;
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
    /* Whether the rail came out expanded. False for every placement but the rail. */
    bool rail_expanded;
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
 * Whether a frame drawn now with `scaffold` would have room for a list and a detail side by side,
 * whether or not it asks for the split - without drawing anything, and leaving the region and the
 * measure as they were.
 *
 * For an application that has to decide something about a screen that is *not* a list and a
 * detail by whether one would stand beside the other - a keyboard it could dock under a detail
 * instead of giving it the body, say - and has to decide it before the frame, because the answer
 * picks the route the frame draws. `frame.split` comes too late for that, and last frame's answer
 * is wrong for exactly one frame: the first after the window changes size.
 *
 * Only the fields that decide the width are read - the destinations, the rail and the measure -
 * so a caller may pass a scaffold with nothing but those set.
 */
bool inkcell_fb_scaffold_splittable(struct inkcell_draw_state *state,
                                    const struct inkcell_fb_scaffold *scaffold);

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
 * width - collapsed, and expanded. For a backend or a test that has to know before a frame is
 * drawn. 0 for the top strip, whose height is inkcell_fb_nav_bar_height().
 *
 * The expanded width is 0 when the words would not fit the rail's cap, which is the case in which
 * the scaffold draws the rail collapsed however it was asked.
 */
int inkcell_fb_scaffold_bar_height(const struct inkcell_draw_state *state);
int inkcell_fb_scaffold_rail_width(const struct inkcell_draw_state *state,
                                   const struct inkcell_fb_chip *destinations, size_t count);
int inkcell_fb_scaffold_rail_expanded_width(const struct inkcell_draw_state *state,
                                            const struct inkcell_fb_chip *destinations,
                                            size_t count);

#ifdef __cplusplus
}
#endif

#endif /* INKCELL_BACKENDS_FB_WIDGETS_SCAFFOLD_H */
