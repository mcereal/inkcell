#ifndef INKCELL_BACKENDS_FB_WIDGETS_SCROLL_H
#define INKCELL_BACKENDS_FB_WIDGETS_SCROLL_H

/*
 * A region of the frame with more content than room, and the chrome that says so.
 *
 * include/inkcell/ui/scroll.h is where the content *is*, in pixels, with no framebuffer in it.
 * This is the other half: the viewport that draws it, the rail beside it, and the app bar that
 * collapses as it moves.
 *
 * The whole of the viewport is three calls to things that already exist -
 * inkcell_scroll_offset(), inkcell_fb_view_push(), inkcell_fb_view_pop() - and that is the
 * point rather than an apology. The difficulty in scrolling by pixels was never the drawing;
 * it was that the drawing layer had one transform and it belonged to the frame. Given a
 * region with coordinates of its own, a scrolled body is content drawn at its own coordinates
 * and nothing else.
 */

/*
 * Not public API. include/inkcell/ui/fb.h is; inkcell_fb_widgets.h is the umbrella over this
 * file and its siblings, and nothing outside src/ui/backends/ should include either.
 */

#include "inkcell/ui/fb_draw.h"

#include "inkcell/ui/scroll.h"
#include "inkcell/ui/theme.h"

#include <stdbool.h>
#include <stdint.h>

/* ---- the viewport ----------------------------------------------------------------------------
 *
 * What a screen wraps its content in when there is more of it than room.
 *
 *     struct inkcell_fb_viewport view;
 *     if (inkcell_fb_viewport_begin(state, &view, box, &screen->scroll, content_h)) {
 *         ... draw the whole content, at its own coordinates, from 0 to content_h ...
 *         inkcell_fb_viewport_end(state, &view);
 *     }
 *
 * **The whole content**, and that is the part that reads oddly the first time. A row-index
 * window exists so that a screen only builds the rows it can see; a viewport is handed
 * everything and cuts what does not fit. On a list of four hundred that is four hundred rows
 * built to draw ten, which is the one thing this shape costs - so `visible` on the struct
 * below is the window in *content* coordinates, and a screen with a long list skips the rows
 * outside it rather than drawing them one clipped pixel at a time.
 *
 * What is bought for that: a body can be between two rows. Every other consequence follows -
 * smooth scrolling, an overscroll that gives, a title that collapses by the pixel, and rows
 * of any height without a parallel array declaring them.
 */

struct inkcell_fb_viewport {
    /* The window on the panel. */
    struct inkcell_fb_rect box;
    /* Where the content has got to, this frame, band included: what the view was pushed by,
       negated. A screen that has to place something against the *scroll* rather than against
       the content - a section header that sticks to the top - reads this. */
    int32_t offset;
    /*
     * The part of the content in the window, in the content's own coordinates.
     *
     * What a screen walks instead of everything, on a body long enough for the difference to
     * matter. The bounds are inclusive of whatever is partly in view, so the row cut by the
     * top edge is inside them - it is on the panel, and a screen that skipped it would draw a
     * gap where the half-row should be.
     */
    struct inkcell_fb_rect visible;
    /* Whether inkcell_fb_viewport_end() has a view to pop. Set by begin(); a caller neither
       reads it nor writes it. */
    bool pushed;
};

/*
 * Opens the viewport over `box`, for `content_h` pixels of content, and answers whether there
 * is anything to draw into.
 *
 * `scroll` is the screen's own, and its extent is set here rather than by the caller: the
 * viewport is the one thing that knows both numbers at once, and a scroll measured against
 * last frame's content is a scroll that can be past an end that has moved.
 *
 * False is a window with nothing on the panel in it, and nothing has been pushed - so the pop
 * is conditional on the push, exactly as it reads.
 */
bool inkcell_fb_viewport_begin(struct inkcell_backend_fb_state *state,
                               struct inkcell_fb_viewport *view, struct inkcell_fb_rect box,
                               struct inkcell_scroll *scroll, int32_t content_h);

void inkcell_fb_viewport_end(struct inkcell_backend_fb_state *state,
                             struct inkcell_fb_viewport *view);

/*
 * A note on who asks for the next frame.
 *
 * A scroll lives on the *screen*, not on the state, so inkcell_fb_state_animating() cannot see
 * it - which is right, and is the same reason the animation table does not hold one: where a
 * body has got to is the screen's, it is not keyed by anything the toolkit issued, and a
 * screen may have three of them. So a screen with a scrolling body adds
 * inkcell_scroll_active() to whatever it already answers its own "is a frame owed" with, in
 * the same place it answers for a picture it is still loading (`pending` on struct
 * inkcell_fb_app). A viewport that quietly registered its scroll somewhere would be a toolkit
 * deciding how long a screen's scroll lives.
 */

/*
 * The rail beside a scrolled viewport: a track the height of the window, and a thumb that is
 * the fraction of the content in view.
 *
 * The list draws its own from `struct inkcell_list` and does not ask a screen to (see
 * inkcell_fb_widgets_list.h). This one is for a viewport, whose content is not a list of items
 * and has no window of items to measure - and it *is* asked for, because a viewport does not
 * know whether the thing inside it already drew one.
 *
 * It does what the list's rail deliberately does not: it shortens as the content is pulled
 * past an end. A thumb that stayed its own length while the content stretched would be the one
 * thing on the panel disagreeing that anything had happened - which is exactly what an
 * overscroll is *for*, since the stretch is the only thing saying the list has ended.
 */
void inkcell_fb_draw_scroll_rail(const struct inkcell_backend_fb_state *state,
                                 const struct inkcell_fb_viewport *view,
                                 const struct inkcell_scroll *scroll);

/* ---- the collapsing app bar ------------------------------------------------------------------
 *
 * A screen's heading, drawn large when the body is at the top and shrunk into the bar once it
 * is not.
 *
 * The shape every phone platform opens a list screen with, and the one piece of chrome here
 * that a row-index window made impossible: it is a continuous function of how far the body has
 * scrolled, and a window that moves a whole row at a time has nothing continuous to give it.
 * Pixels are what make it buildable at all.
 *
 * What collapses is the *room*, not the glyph. A glyph scale here is an integer multiplier
 * over a cell (see enum inkcell_type), so there is no such thing as a title at 1.4x - a title
 * shrinking smoothly would have to be a bitmap scaled by a fraction, which is exactly the
 * resampling this toolkit's font machinery exists to do once, at load, rather than per frame.
 *
 * So the two titles cross-fade and the bar's height eases between them. A fade is honest here
 * where it is not for a panel: text is drawn as coverage blended against a stated ground, so
 * ink mixed a fraction of the way towards that ground *is* a partly faded glyph, drawn by the
 * same blend that anti-aliases its edges. Nothing about it is a trick.
 *
 * The bar consumes the room it is occupying *now*, so the body under it reflows as it
 * collapses. That is the shape working rather than a flaw in it - the whole point is that the
 * heading gives its room back to the content - and it is why this takes the layout mutably,
 * like every other piece of chrome that costs body rows.
 */

struct inkcell_fb_large_title {
    /* What the screen is. Drawn twice: large on its own line at the top, and small in the bar
       once it has collapsed. */
    const char *title;
    /* A fact about the screen, beside the large title only: a count, a unit. It is the first
       thing to go, because it is the least of the three things on the bar and the collapsed
       bar has room for a title and a badge rather than for all three. NULL for none. */
    const char *detail;
    /* The trailing capsule, on both sizes of the bar - a badge is a state rather than a
       decoration, so it is the thing that must not disappear when the heading shrinks. */
    const char *badge;
    enum inkcell_family badge_family;
};

/*
 * How far the body has to scroll for the heading to be fully collapsed, in pixels.
 *
 * The difference between the two bars' heights: the large title has exactly its own extra row
 * to give back, so the collapse finishes at the moment the room it was using is gone. A
 * constant here would be a number that stops matching the type scale the first time a theme
 * changes it.
 */
int inkcell_fb_large_title_travel(const struct inkcell_backend_fb_state *state);

/*
 * Draws the bar at whatever `offset` says it has collapsed to, and consumes the room it took.
 *
 * `offset` is inkcell_scroll_offset()'s answer for the body underneath. Negative - the body
 * pulled past its top - is not clamped away: the heading grows, which is the other half of
 * what an overscroll is for and is what every platform does with a large title being pulled
 * down.
 */
void inkcell_fb_draw_large_title(const struct inkcell_backend_fb_state *state,
                                 struct inkcell_fb_layout *layout,
                                 const struct inkcell_fb_large_title *bar, int32_t offset);

#endif /* INKCELL_BACKENDS_FB_WIDGETS_SCROLL_H */
