#ifndef INKCELL_BACKENDS_FB_WIDGETS_FOCUS_H
#define INKCELL_BACKENDS_FB_WIDGETS_FOCUS_H

/*
 * The focus ring: one outline that travels between the boxes a frame registered.
 *
 * Every other cue in this toolkit says *which* thing has the cursor - a row's fill, a button's
 * state layer, a card's accent edge. Each of them is drawn by the thing it is about, and
 * because of that none of them can say the one thing a handheld most needs said: that the
 * cursor **moved, and which way**. A fill that disappears from one row and appears on another
 * is two events a reader has to join up; on a grid, where the next box may be in any of four
 * directions, joining them up is the whole difficulty. A ring that slides from the one to the
 * other is the same information with the work already done.
 *
 * It is one object rather than a property of each component, and that is what makes it
 * possible at all. The boxes are already collected - see include/inkcell/ui/focus.h - so the
 * ring is handed the map and an id and works out the rest: what it is on now, what it was on
 * before, and where between them it has got to. Nothing that draws a chip, a row or a verb
 * knows this exists.
 *
 *     inkcell_focus_begin(&map, storage, SCREEN_MAX);
 *     inkcell_fb_set_focus_map(state, &map);
 *     ... draw the screen ...
 *     inkcell_fb_draw_focus_ring(state, &map, screen->focus);   // last: it sits over the body
 *
 * Drawn last, because it is over everything and because it can only be drawn once everything
 * has said where it is. That is also why it is not a widget in the usual sense - it takes no
 * rect, no tone and no label. What it looks like is decided by the box it is on, which is what
 * `radius` on a registration is for: a ring landing on a pill is a pill and a ring landing on
 * a keycap is a rounded square, without the screen saying a word about either.
 */

/*
 * Not public API. include/inkcell/ui/fb.h is; inkcell_fb_widgets.h is the umbrella over this
 * file and its siblings, and nothing outside src/ui/backends/ should include either.
 */

#include "inkcell/ui/fb_draw.h"

#include "inkcell/ui/anim.h"
#include "inkcell/ui/focus.h"

#include <stdbool.h>

/*
 * How long a move takes, as a token rather than a number: the same one a control acknowledging
 * a press takes, because that is what this is. The curve is INKCELL_EASE_OUT for the same
 * reason the switch's knob takes it - the reader caused this, so it should leave quickly and
 * settle rather than ease in as though it had decided to go on its own.
 */
#define INKCELL_FB_FOCUS_RING_MOTION INKCELL_MOTION_SHORT

/*
 * Draws the ring on whatever `id` is registered as, travelling from wherever it was.
 *
 * Three things happen here and only the first is visible:
 *
 *   - **First sight adopts.** A screen opening puts the ring where the cursor already is
 *     rather than flying it in from nothing, which is `inkcell_anim_track()`'s rule and is
 *     right for the same reason: a frame that animates on arrival is a frame announcing itself.
 *   - **A different thing travels; the same thing is followed.** A journey is for a cursor that
 *     moved, and a box that moves under a ring is not that: a list gliding between two windows
 *     shifts its rows a few pixels per frame, and a ring that started a transition at each of
 *     them would trail its own row for the length of the scroll. So the ring goes exactly where
 *     its box went, which is also the right answer for a reflow - a row that grew a line moves
 *     without the reader having pressed anything. A move still in flight only has its
 *     destination corrected, so a real journey is never broken off half way.
 *   - **A box that is gone clears it.** An id no longer in the map is a thing no longer on the
 *     panel, and a ring left where it used to be is the exact lie this whole mechanism exists
 *     to prevent. Nothing is drawn, and the next id seen is adopted rather than travelled to,
 *     because there is no longer anywhere honest to travel from.
 *
 * `id` of INKCELL_FOCUS_NONE clears it too, which is what a screen with nothing focused passes.
 *
 * Mutable state, unlike most of what draws: the ring's position is the one thing on the frame
 * that the frame itself remembers.
 *
 * A note for a screen that repaints part of a frame rather than all of it. Erasing a ring is
 * not the ring's own work - what is under it belongs to whatever drew there - so the box it
 * painted is carried into the next frame and declared as damage before anything paints (see
 * inkcell_fb_app_frame_begin()). That is enough on a frame drawn whole, which is every frame
 * inkcell itself produces. It is *not* enough under a clip band that excludes where the ring
 * has been: the rows are let through to the panel, but nothing in them is redrawn, so the band
 * a screen declares has to take in the ring's path. A screen that cannot promise that should
 * place the ring rather than move it - a cursor that jumps is a worse cue than one that
 * travels, and both are better than a trail of outline left on the panel.
 */
void inkcell_fb_draw_focus_ring(struct inkcell_backend_fb_state *state,
                                const struct inkcell_focus_map *map, uint32_t id);

/*
 * Puts the ring on `id` without travelling to it, as though it had been there all along.
 *
 * For the move that is not a move. A screen that reflowed under the cursor - a row deleted, a
 * card that shed a verb, a filter that emptied the list - answers with inkcell_focus_nearest()
 * and lands somewhere new *without the reader having pressed anything*. A ring that flew across
 * the panel there would be reporting a press that never happened, and on a list that reflows
 * while it is being read it would fly on every frame.
 *
 * Call it before the draw below, on the frames where the cursor was moved by the layout rather
 * than by a press; the next ordinary press then travels from where this put it.
 */
void inkcell_fb_focus_ring_place(struct inkcell_backend_fb_state *state,
                                 const struct inkcell_focus_map *map, uint32_t id);

/*
 * Where the ring is at this instant, mid-travel included, and how round it is there.
 *
 * False when no ring is up. `rect` and `radius` may each be NULL.
 *
 * It is here because the ring's position is a fact about the frame that something other than
 * the ring can want: a screen deciding whether the cursor is still on the panel, a test
 * asserting that a move actually moved. Reading it back beats every alternative to testing this
 * at all - the ring is a few pixels of outline, and a golden picture of one halfway through a
 * journey pins the whole curve to a digest that any change to the easing would break.
 */
bool inkcell_fb_focus_ring_rect(const struct inkcell_backend_fb_state *state,
                                struct inkcell_focus_rect *rect, int *radius);

#endif /* INKCELL_BACKENDS_FB_WIDGETS_FOCUS_H */
