#define _POSIX_C_SOURCE 200809L

/*
 * A position in a long piece of content, in pixels.
 *
 * See include/inkcell/ui/scroll.h for what this is instead of a row-index window and what is
 * deliberately not in it. Everything below is arithmetic on two numbers - where the content is
 * going and where it came from - plus the one curve that makes the ends feel like ends.
 */

#include "inkcell/ui/scroll.h"

#include "inkcell/ui/anim.h"

#include <stddef.h>

/*
 * How long a scroll takes to reach where a press sent it.
 *
 * The token a control acknowledging a press takes, which is what this is - and the same one
 * the focus ring travels on and a list glides on, so on a frame where the cursor moves and the
 * body follows it the two arrive together. A body that settled after its own cursor would read
 * as two events.
 */
#define INKCELL_SCROLL_MOTION INKCELL_MOTION_SHORT

/*
 * How long the spring back from an overscroll takes.
 *
 * Longer than the press it undoes, which is the opposite of the exit-is-shorter rule the
 * overlays follow, and deliberately so. An overlay leaving has nothing left to say; a spring
 * back is the panel *answering* - it is the whole of the feedback that says "there is no more
 * of this list" - so it has to be slow enough to be felt as a movement rather than seen as a
 * jump.
 */
#define INKCELL_SCROLL_SPRING_MOTION INKCELL_MOTION_MEDIUM

/*
 * The durations, in milliseconds.
 *
 * Written here rather than read from the theme, and this is the one place in the toolkit that
 * does. Everything that draws is handed a state and can ask it (inkcell_fb_motion()); this
 * module is deliberately not handed one - it has no framebuffer in it, which is what lets the
 * whole of the arithmetic be tested with no display anywhere near it. Taking a theme pointer
 * to read two numbers would trade that for nothing: a scroll is not a *look*, it is where the
 * content is, and the two tokens it spends are named above so a reader can see which kinds of
 * movement they are.
 */
#define INKCELL_SCROLL_MOTION_MS 140U
#define INKCELL_SCROLL_SPRING_MS 220U

static int32_t inkcell_scroll_clamp(int32_t value, int32_t low, int32_t high) {
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

int32_t inkcell_scroll_max(const struct inkcell_scroll *scroll) {
    if (scroll == NULL) {
        return 0;
    }
    const int32_t max = scroll->content - scroll->viewport;
    return max > 0 ? max : 0;
}

/*
 * The room the band may use: a third of the window.
 *
 * Of the *window* rather than of the content, because what the reader is looking at is the
 * window - a third of a window is the same give on a list of ten rows and a list of four
 * hundred, which is what makes the two feel like the same list.
 */
static int32_t inkcell_scroll_give(const struct inkcell_scroll *scroll) {
    const int32_t dim =
        scroll->viewport * INKCELL_SCROLL_OVER_NUM / INKCELL_SCROLL_OVER_DEN;
    return dim > 0 ? dim : 0;
}

int32_t inkcell_scroll_rubber(int32_t over, int32_t dim) {
    if (dim <= 0 || over == 0) {
        return 0;
    }
    const int32_t sign = over < 0 ? -1 : 1;
    const int64_t x = (int64_t)(over < 0 ? -over : over);
    const int64_t c = INKCELL_SCROLL_RUBBER_PCT;
    /*
     * give = (c * x * dim) / (c * x + dim), with c in hundredths - so both halves are
     * multiplied by 100 and the hundredths cancel. In 64 bits because c * x * dim is three
     * panel-sized quantities multiplied together and an int32 has nowhere near the room.
     */
    const int64_t give = (c * x * (int64_t)dim) / (c * x + 100 * (int64_t)dim);
    return sign * (int32_t)give;
}

/* Where the raw position is at `now_ms`, before the band: may be past either end. */
static int32_t inkcell_scroll_raw(const struct inkcell_scroll *scroll, uint64_t now_ms) {
    const int32_t at = inkcell_anim_value(&scroll->travel, now_ms);
    const int64_t span = (int64_t)scroll->target - (int64_t)scroll->from;
    return (int32_t)((int64_t)scroll->from + (span * at) / INKCELL_ANIM_ONE);
}

int32_t inkcell_scroll_offset(const struct inkcell_scroll *scroll, uint64_t now_ms) {
    if (scroll == NULL) {
        return 0;
    }
    const int32_t raw = inkcell_scroll_raw(scroll, now_ms);
    const int32_t max = inkcell_scroll_max(scroll);
    if (raw < 0) {
        return inkcell_scroll_rubber(raw, inkcell_scroll_give(scroll));
    }
    if (raw > max) {
        return max + inkcell_scroll_rubber(raw - max, inkcell_scroll_give(scroll));
    }
    return raw;
}

int32_t inkcell_scroll_overscroll(const struct inkcell_scroll *scroll, uint64_t now_ms) {
    if (scroll == NULL) {
        return 0;
    }
    const int32_t offset = inkcell_scroll_offset(scroll, now_ms);
    const int32_t max = inkcell_scroll_max(scroll);
    if (offset < 0) {
        return offset;
    }
    if (offset > max) {
        return offset - max;
    }
    return 0;
}

bool inkcell_scroll_active(const struct inkcell_scroll *scroll, uint64_t now_ms) {
    return scroll != NULL && inkcell_anim_active(&scroll->travel, now_ms);
}

/* Aims at `target` from wherever the content is right now, over `duration_ms`. The one place
   the travel is started, so re-aiming mid-flight always takes the current position as the new
   origin - which is what stops a second press snapping back to where the first one began. */
static void inkcell_scroll_aim(struct inkcell_scroll *scroll, int32_t target, uint64_t now_ms,
                               uint32_t duration_ms) {
    scroll->from = inkcell_scroll_raw(scroll, now_ms);
    scroll->target = target;
    inkcell_anim_set(&scroll->travel, 0);
    inkcell_anim_to(&scroll->travel, now_ms, INKCELL_ANIM_ONE, duration_ms, INKCELL_EASE_OUT);
    if (duration_ms == 0U) {
        scroll->from = target;
    }
}

void inkcell_scroll_extent(struct inkcell_scroll *scroll, int32_t content, int32_t viewport) {
    if (scroll == NULL) {
        return;
    }
    const int32_t content_now = content > 0 ? content : 0;
    const int32_t viewport_now = viewport > 0 ? viewport : 0;
    /*
     * Restating the same extent changes nothing at all.
     *
     * The check is the whole of this function's correctness, not a saving. A viewport sets the
     * extent every frame - it is the one thing that holds both numbers at once - so a clamp
     * applied unconditionally here is a clamp applied to every frame of every scroll, and an
     * overscroll would be taken back before the frame that raised it could draw. It would look
     * exactly like a list whose ends do not give, and nothing about the band would be wrong.
     */
    if (content_now == scroll->content && viewport_now == scroll->viewport) {
        return;
    }
    scroll->content = content_now;
    scroll->viewport = viewport_now;
    /*
     * Content that changed shape pulls the position back inside it.
     *
     * Not an edge case: a filter emptying is exactly this, and a scroll left where it was
     * would be a window onto rows that are not there any more. Both ends of the travel are
     * clamped rather than only the target, or the content would ease *from* somewhere outside
     * the content it is now in.
     *
     * The clamp is to the honest range, which also means a change of extent cancels an
     * overscroll. That is right: an overscroll is the reader pushing at an end, and an end
     * that has moved is not the end they were pushing at.
     */
    const int32_t max = inkcell_scroll_max(scroll);
    scroll->target = inkcell_scroll_clamp(scroll->target, 0, max);
    scroll->from = inkcell_scroll_clamp(scroll->from, 0, max);
}

void inkcell_scroll_by(struct inkcell_scroll *scroll, int32_t dy, uint64_t now_ms) {
    if (scroll == NULL || dy == 0) {
        return;
    }
    const int32_t max = inkcell_scroll_max(scroll);
    const int32_t give = inkcell_scroll_give(scroll);
    /*
     * The push accumulates past the end, and is capped there.
     *
     * Capped rather than left to run, because the band is asymptotic: a target a thousand
     * pixels past the end and one two thousand past it look identical on the panel, and the
     * difference is a debt the spring has to unwind before anything moves. A reader holding
     * down for a second at the bottom of a list would then press up and watch nothing happen.
     * Past `give` the extra push has nowhere to go, so it is not taken.
     */
    const int32_t wanted = scroll->target + dy;
    const int32_t target = inkcell_scroll_clamp(wanted, -give, max + give);
    inkcell_scroll_aim(scroll, target, now_ms, INKCELL_SCROLL_MOTION_MS);
}

void inkcell_scroll_to(struct inkcell_scroll *scroll, int32_t offset, uint64_t now_ms) {
    if (scroll == NULL) {
        return;
    }
    inkcell_scroll_aim(scroll, inkcell_scroll_clamp(offset, 0, inkcell_scroll_max(scroll)),
                       now_ms, INKCELL_SCROLL_MOTION_MS);
}

void inkcell_scroll_place(struct inkcell_scroll *scroll, int32_t offset) {
    if (scroll == NULL) {
        return;
    }
    const int32_t at = inkcell_scroll_clamp(offset, 0, inkcell_scroll_max(scroll));
    scroll->from = at;
    scroll->target = at;
    inkcell_anim_set(&scroll->travel, INKCELL_ANIM_ONE);
}

void inkcell_scroll_release(struct inkcell_scroll *scroll, uint64_t now_ms) {
    if (scroll == NULL) {
        return;
    }
    const int32_t max = inkcell_scroll_max(scroll);
    if (scroll->target >= 0 && scroll->target <= max) {
        return; /* nothing is being held past an end, which is almost every frame */
    }
    inkcell_scroll_aim(scroll, inkcell_scroll_clamp(scroll->target, 0, max), now_ms,
                       INKCELL_SCROLL_SPRING_MS);
}

bool inkcell_scroll_reveal(struct inkcell_scroll *scroll, int32_t top, int32_t height,
                           int32_t pad, uint64_t now_ms) {
    if (scroll == NULL || scroll->viewport <= 0) {
        return false;
    }
    const int32_t max = inkcell_scroll_max(scroll);
    /*
     * Measured against where the scroll is *going* rather than where it is.
     *
     * A cursor pressed twice quickly is two reveals, and the second one asked against the
     * position the first is still travelling through would decide the row it wants is already
     * in view - it will be, in a hundred milliseconds. Reading the target instead makes a
     * repeated press add up the way a repeated press should.
     */
    const int32_t at = inkcell_scroll_clamp(scroll->target, 0, max);
    const int32_t bottom = top + height;

    int32_t want = at;
    if (top - pad < at) {
        want = top - pad;
    } else if (bottom + pad > at + scroll->viewport) {
        want = bottom + pad - scroll->viewport;
    }
    want = inkcell_scroll_clamp(want, 0, max);
    if (want == at) {
        return false;
    }
    inkcell_scroll_aim(scroll, want, now_ms, INKCELL_SCROLL_MOTION_MS);
    return true;
}
