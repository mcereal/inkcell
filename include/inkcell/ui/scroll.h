#ifndef INKCELL_SCROLL_H
#define INKCELL_SCROLL_H

/*
 * A position in a long piece of content, measured in pixels.
 *
 * ---- what this replaces ----
 *
 * `struct inkcell_list` is a window onto *items*: a first index, a count that fits, and the
 * arithmetic that keeps the cursor inside them. It is stateless between frames on purpose -
 * the window is derived from the cursor every time - and that property is why it has been the
 * right model for every list this toolkit has drawn.
 *
 * It is also the reason four things cannot be built.
 *
 *   - **A body cannot be between two rows.** A window is an index, so a list is at row 12 or
 *     at row 13 and never at 12.4. The glide exists to hide exactly that: it draws the content
 *     displaced from where the window says it belongs and eases the displacement to nothing,
 *     which is a pixel offset bolted onto an index model rather than a position.
 *   - **Overscroll has nowhere to be.** A list at its last row that is pushed again has
 *     nothing to say, because there is no index past the last one. Rubber-banding is a
 *     *position past the end*, which an index cannot hold.
 *   - **A collapsing app bar has nothing to read.** A large title that shrinks as the body
 *     scrolls needs to know how far the body has scrolled, in pixels, before the first row
 *     has gone anywhere. A row index does not change until a whole row has left.
 *   - **Items of different heights are a second array.** A window over items whose heights
 *     vary is `heights[]`, one byte per item, borrowed for the life of the list - because the
 *     model counts steps rather than pixels and cannot ask how tall anything is. Content
 *     measured in pixels has no such array: an item is at a y and is a height tall.
 *
 * So: a scroll is an offset in pixels, and the content is drawn at its own coordinates inside
 * a view (inkcell_fb_view_push()) that moves it. A half-row at the top edge is then not a
 * special case - it is what an offset that is not a multiple of a line looks like.
 *
 * Nothing here touches a framebuffer, a snapshot or a font, for the reason nothing in
 * include/inkcell/ui/layout.h does: the arithmetic is the whole of the difficulty, and it is
 * unit tested directly (tests/suites/ui_scroll.c) with no display anywhere near it.
 *
 * ---- what is deliberately not here ----
 *
 * **There is no fling, and no velocity.** Momentum is the answer to a finger leaving a screen
 * at speed, and this device has a d-pad: a press is a discrete request to move by a row or a
 * page, and the thing that makes it feel continuous is that the offset *eases* to where the
 * press sent it rather than jumping. A velocity model here would be machinery serving a
 * gesture the hardware cannot produce.
 *
 * **Nothing is accumulated.** Where a scroll has got to is derived from the clock and a
 * target, exactly as `struct inkcell_anim` derives a knob's position - so a frame that was
 * missed does not leave the scroll behind, a capture stepping time in jumps lands exactly
 * where the arithmetic says, and a test can pin the whole curve to numbers. A scroll that
 * integrated a speed per frame would have none of those properties.
 */

#include "inkcell/ui/anim.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * How far past its end content may be dragged, as a fraction of the viewport: a third.
 *
 * The bound the rubber band approaches rather than a distance anything actually travels - see
 * inkcell_scroll_rubber(). A third of the window is enough that the overscroll is unmistakably
 * a gesture and not a rounding error, and little enough that the content never leaves.
 */
#define INKCELL_SCROLL_OVER_NUM 1
#define INKCELL_SCROLL_OVER_DEN 3

/*
 * How hard the band pulls back, in hundredths.
 *
 * 0.55 is UIScrollView's constant and it is here for the reason the easing curves carry CSS's
 * names: it is the number every reader who has felt this behaviour has felt, and a different
 * one would be a different feel with nothing to recommend it.
 */
#define INKCELL_SCROLL_RUBBER_PCT 55

/*
 * A position in a piece of content.
 *
 * Copyable, and a zeroed one is a scroll at the top of nothing. A screen keeps one per
 * scrolling region - not in the snapshot, for the reason an animation is not in the store:
 * where a body has got to is presentation, it dies with the frame buffer, and a second backend
 * is entitled to scroll differently or not at all.
 */
struct inkcell_scroll {
    /* The content's height and the window's, in pixels. Set together by
       inkcell_scroll_extent(), because the maximum offset is the difference and a pair set
       one at a time has a frame in which it is wrong. */
    int32_t content;
    int32_t viewport;
    /*
     * Where the scroll is going, in pixels from the top of the content.
     *
     * Allowed past both ends: that is what an overscroll *is*, and clamping it here is what
     * would make a rubber band impossible. What is never past the end is what
     * inkcell_scroll_offset() reports, which is this run through the band.
     */
    int32_t target;
    /* Where it is coming from, and the travel between them. Clock-derived: see the header
       note on why nothing here accumulates. */
    int32_t from;
    struct inkcell_anim travel;
};

/*
 * States how much content there is and how much of it is in view.
 *
 * Call every frame before anything else: both are facts about a layout that can change under
 * a scroll - a row that grew a line, a sheet that was resized, a filter that emptied the list
 * - and a scroll measured against last frame's content is a scroll that can be past an end
 * that has moved.
 *
 * An extent that is the same as last frame's changes nothing. That is not an optimisation: a
 * changed extent re-clamps the position, and re-clamping on every frame would take back every
 * overscroll before the frame that raised it could draw it. The band would be exactly as
 * written and the ends would simply not give.
 */
void inkcell_scroll_extent(struct inkcell_scroll *scroll, int32_t content, int32_t viewport);

/* The furthest the content can honestly go: 0 when it all fits. */
int32_t inkcell_scroll_max(const struct inkcell_scroll *scroll);

/*
 * Where the content is at `now_ms`, in pixels, with the rubber band applied.
 *
 * This is what a viewport pushes as its offset, and it is never more than
 * INKCELL_SCROLL_OVER_NUM/DEN of a viewport past either end however hard the reader pushes.
 * See inkcell_scroll_rubber().
 */
int32_t inkcell_scroll_offset(const struct inkcell_scroll *scroll, uint64_t now_ms);

/*
 * How far past an end the content is being held, in pixels: negative above the top, positive
 * below the bottom, 0 in the ordinary middle.
 *
 * What a widget drawing the stretch asks - a rail that shortens as the content is pulled, a
 * "release to refresh" that is not in this toolkit yet and will want it when it is. Already
 * through the band, so it is a distance on the panel rather than the raw push behind it.
 */
int32_t inkcell_scroll_overscroll(const struct inkcell_scroll *scroll, uint64_t now_ms);

/* Whether the scroll still has somewhere to be. What a backend adds to the animations when the
   event loop asks whether another frame is owed. */
bool inkcell_scroll_active(const struct inkcell_scroll *scroll, uint64_t now_ms);

/*
 * Moves by `dy` pixels from wherever it is now, easing there.
 *
 * What a d-pad press is: a row down is `inkcell_scroll_by(s, line, now)` and a page is the
 * viewport.
 *
 * Past an end the *target* keeps going and the offset does not: the band is applied on the way
 * out, so a reader holding down at the bottom of a list feels the content give less and less
 * rather than feeling the press stop working. Past the bound the target is clamped, so a press
 * repeated for a second does not build up a debt that takes a second to unwind.
 *
 * Whether the content *stays* stretched is inkcell_scroll_release()'s question rather than
 * this one's. A screen that releases on every frame gets a bounce - one frame out, then the
 * spring - and a screen that holds off while a press is repeating gets the stretch. Neither is
 * a flag here, because whether a key is still down is the input layer's fact and this module
 * has no business being told it twice.
 */
void inkcell_scroll_by(struct inkcell_scroll *scroll, int32_t dy, uint64_t now_ms);

/*
 * Eases to an absolute offset. Clamped to the content, so this cannot be used to overscroll -
 * a jump to a position is not a gesture and has nothing to rubber-band against.
 */
void inkcell_scroll_to(struct inkcell_scroll *scroll, int32_t offset, uint64_t now_ms);

/*
 * Puts it at an offset with nothing in flight.
 *
 * What a first paint does, and what a screen re-entered from somewhere else does: a body that
 * scrolls into position on the frame it opens is a body announcing itself rather than moving.
 * inkcell_anim_set()'s rule, for its reason.
 */
void inkcell_scroll_place(struct inkcell_scroll *scroll, int32_t offset);

/*
 * Lets go: anything past an end springs back to it.
 *
 * Called on the frame after the last press of a repeat - or whenever a screen knows the reader
 * has stopped pushing. A no-op when the content is not past an end, so a screen may call it
 * unconditionally, which is what it should do: "nothing is being held" is the state a frame is
 * in almost always, and a screen that only called this when it thought it mattered is a screen
 * that will one day leave a list stretched.
 */
void inkcell_scroll_release(struct inkcell_scroll *scroll, uint64_t now_ms);

/*
 * Scrolls the least it can to bring [top, top + height) into the window, and answers whether
 * it had to move.
 *
 * What the focus ring needs: the cursor moved onto a row, and the row may be off the panel.
 * The *least* it can, because a reveal that centred what it was revealing would move
 * everything else too, and a reader pressing down a list expects the list to come up by a row
 * rather than to jump.
 *
 * `pad` is how much clear space to leave beyond the box - a row revealed flush against the
 * bottom edge of a window looks like the last row of the list whether or not it is, and a
 * line's worth of the next row is what says there is more.
 */
bool inkcell_scroll_reveal(struct inkcell_scroll *scroll, int32_t top, int32_t height,
                           int32_t pad, uint64_t now_ms);

/*
 * The rubber band: `over` pixels of push, as pixels of give.
 *
 * Exposed because it is the whole of the feel and the one thing here worth testing in
 * isolation. It is UIScrollView's curve:
 *
 *     give = (c * over * dim) / (c * over + dim)
 *
 * with `c` = INKCELL_SCROLL_RUBBER_PCT and `dim` the room the band may use. Read it as a
 * hyperbola: it is linear for small pushes, so the first pixel of overscroll moves the content
 * by very nearly a pixel and the gesture does not feel dead - and it approaches `dim` and
 * never reaches it, so no amount of pushing takes the content away. The reader feels
 * resistance building rather than a wall.
 *
 * Integer throughout, so an overscroll is exactly reproducible and a capture of one renders
 * the same picture twice. Negative `over` gives negative give, so the two ends are one
 * function rather than two.
 */
int32_t inkcell_scroll_rubber(int32_t over, int32_t dim);

#ifdef __cplusplus
}
#endif

#endif /* INKCELL_SCROLL_H */
