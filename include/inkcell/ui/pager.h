#ifndef INKCELL_PAGER_H
#define INKCELL_PAGER_H

/*
 * Pages a finger drags between: a home screen's pages, a photo viewer's, onboarding cards.
 *
 * The scroll model (inkcell/ui/scroll.h) has no velocity on purpose - a d-pad presses, it does
 * not fling. A touchscreen does, and a pager is the gesture that most needs it: a short, fast
 * flick means "next page" though the finger travelled a fraction of one, and a long, slow drag
 * that stops just short of half way means "stay". So this holds the drag and the speed it was
 * let go at, and decides the page from both, the way every phone's launcher does.
 *
 * What it does, and the order a screen calls it in:
 *
 *   inkcell_pager_extent()   every frame: how wide a page is, how many there are
 *   inkcell_pager_down()     a finger lands
 *   inkcell_pager_move()     it moves - answers whether the pager has the gesture: a drag goes
 *                            to the pager once it has gone `slop` pixels more across than down,
 *                            and is never the pager's once it went `slop` down first, so a
 *                            vertical scroll or a tap underneath is left alone
 *   inkcell_pager_up()       it lifts - the page is chosen and the pages ease to it
 *   inkcell_pager_offset()   where the pages are now, in pixels from page 0's left edge
 *
 * Past the first or last page the drag gives through the scroll model's rubber band
 * (inkcell_scroll_rubber()), so the end of the pages is felt rather than a wall.
 *
 * As in the scroll model, nothing is integrated per frame: the settle is a clock-derived ease
 * from where the drag was let go to the chosen page, so a missed frame does not leave the pages
 * behind and a test pins the curve to numbers. Pixels in, pixels out; the caller turns its dp
 * into them (inkcell_fb_dp()).
 */

#include "inkcell/ui/anim.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The finger's recent positions the speed is measured over. Older samples than
   INKCELL_PAGER_VELOCITY_MS are not the speed it was let go at. */
#define INKCELL_PAGER_SAMPLES 8
#define INKCELL_PAGER_VELOCITY_MS 100

struct inkcell_pager_sample {
    int32_t x;
    uint64_t at_ms;
};

struct inkcell_pager {
    /* Set once: how far a finger goes before the gesture is decided, and how fast a flick is,
       in pixels and pixels a second. */
    int32_t slop;
    int32_t flick;
    /* Every frame: the page's width and the number of pages. */
    int32_t page_w;
    uint32_t count;
    /* The page it is on, or easing to. */
    uint32_t page;
    /* A finger: where it landed, and whether the gesture is decided - and whose. */
    bool down, decided, claimed;
    int32_t x0, y0;
    int32_t drag; /* how far it has been dragged, pixels, + to the right */
    struct inkcell_pager_sample samples[INKCELL_PAGER_SAMPLES];
    uint32_t sample_count;
    /* The settle: from an offset to the page's, eased. */
    int32_t from;
    struct inkcell_anim travel;
    uint32_t duration_ms;
};

/* A pager on page 0 that decides a gesture after `slop` pixels and takes `flick` pixels a
   second as a flick; settles take `duration_ms` (a theme motion, inkcell_fb_motion()). */
void inkcell_pager_init(struct inkcell_pager *pager, int32_t slop, int32_t flick,
                        uint32_t duration_ms);

/* The page's width and how many there are. A page past the new end is brought back to it. */
void inkcell_pager_extent(struct inkcell_pager *pager, int32_t page_w, uint32_t count);

void inkcell_pager_down(struct inkcell_pager *pager, int32_t x, int32_t y, uint64_t now_ms);

/* The finger at (x, y). True while the gesture is the pager's: the caller then cancels whatever
   the touch had started underneath - a pressed icon - and redraws. */
bool inkcell_pager_move(struct inkcell_pager *pager, int32_t x, int32_t y, uint64_t now_ms);

/*
 * The finger lifts (or the touch is cancelled: pass `cancelled`, and the pages go back to the
 * page they were on). Answers whether the gesture was the pager's - a lift that was not is a tap
 * for the caller.
 *
 * The page: one on in the direction of a flick (faster than `flick`, past the slop), else the
 * page the pages are mostly showing, so a drag past half way turns it and one short of half way
 * does not.
 */
bool inkcell_pager_up(struct inkcell_pager *pager, uint64_t now_ms, bool cancelled);

/* Goes to `page` (clamped), easing - a page dot tapped, a launcher sent home. */
void inkcell_pager_to(struct inkcell_pager *pager, uint32_t page, uint64_t now_ms);

/* Where the pages are, in pixels from page 0's left edge: 0 on page 0, page_w on page 1. What a
   screen subtracts from each page's x. */
int32_t inkcell_pager_offset(const struct inkcell_pager *pager, uint64_t now_ms);

/* Whether the pages are moving - a finger on them or a settle in flight. What asks for the next
   frame. */
bool inkcell_pager_active(const struct inkcell_pager *pager, uint64_t now_ms);

#ifdef __cplusplus
}
#endif

#endif
