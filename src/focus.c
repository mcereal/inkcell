#define _POSIX_C_SOURCE 200809L

/*
 * Directional focus: which registered rectangle lies that way.
 *
 * The algorithm is Android's FocusFinder, kept rather than reinvented - it is fifteen years of
 * other people's d-pads, and a screen's expectation of where "right" goes is not something a
 * toolkit gets to have a fresh opinion about. What the header states as two rules (the beam
 * first, then a weighted distance) is those rules, written once in terms of a direction of
 * travel instead of four times in terms of left, right, up and down.
 *
 * No framebuffer, no allocation and nothing kept past a call, which is what lets the whole of
 * it be tested without a panel - see tests/suites/ui_focus.c.
 */

#include "inkcell/ui/focus.h"

#include <stddef.h>

/*
 * The furthest anything is allowed to be, for the purposes of comparing two distances.
 *
 * The score below squares a gap and multiplies it by thirteen, and the gaps are the difference
 * of two ints: a caller that registers a box near INT_MAX hands this arithmetic a number whose
 * square does not fit in the sixty-four bits it is computed in, and overflow is how a finder
 * that was merely wrong becomes undefined. Sixteen million pixels is four thousand panels laid
 * end to end, so clamping to it cannot change the order of two distances anything real ever
 * produces - and past it every candidate is equally, uselessly far away, which is the answer a
 * saturating compare gives.
 */
#define FOCUS_REACH ((int64_t)1 << 24)

/*
 * A rectangle seen from the direction being pressed.
 *
 * `start` and `end` run *along* the press, always increasing in the direction of travel - so a
 * left press is the mirror of a right one and everything below is written once. `lo` and `hi`
 * are the box's extent across the press, which is the beam; `center` is the middle of it.
 */
struct focus_span {
    int64_t start, end;
    int64_t lo, hi;
    int64_t center;
};

static bool focus_horizontal(enum inkcell_focus_dir dir) {
    return dir == INKCELL_FOCUS_LEFT || dir == INKCELL_FOCUS_RIGHT;
}

static enum inkcell_focus_dir focus_opposite(enum inkcell_focus_dir dir) {
    switch (dir) {
    case INKCELL_FOCUS_LEFT:
        return INKCELL_FOCUS_RIGHT;
    case INKCELL_FOCUS_RIGHT:
        return INKCELL_FOCUS_LEFT;
    case INKCELL_FOCUS_UP:
        return INKCELL_FOCUS_DOWN;
    case INKCELL_FOCUS_DOWN:
    default:
        return INKCELL_FOCUS_UP;
    }
}

/* Half-open throughout: a box at x=10 w=20 ends at 30, and the box starting at 30 is beside it
   rather than one pixel inside it. Negating the far edge is what makes a leftward press the
   same code as a rightward one. */
static struct focus_span focus_project(struct inkcell_focus_rect rect, enum inkcell_focus_dir dir) {
    const int64_t x0 = rect.x;
    const int64_t x1 = (int64_t)rect.x + rect.w;
    const int64_t y0 = rect.y;
    const int64_t y1 = (int64_t)rect.y + rect.h;
    struct focus_span span;

    switch (dir) {
    case INKCELL_FOCUS_LEFT:
        span.start = -x1;
        span.end = -x0;
        span.lo = y0;
        span.hi = y1;
        break;
    case INKCELL_FOCUS_RIGHT:
        span.start = x0;
        span.end = x1;
        span.lo = y0;
        span.hi = y1;
        break;
    case INKCELL_FOCUS_UP:
        span.start = -y1;
        span.end = -y0;
        span.lo = x0;
        span.hi = x1;
        break;
    case INKCELL_FOCUS_DOWN:
    default:
        span.start = y0;
        span.end = y1;
        span.lo = x0;
        span.hi = x1;
        break;
    }
    span.center = (span.lo + span.hi) / 2;
    return span;
}

/*
 * Whether `cand` is in the direction of the press at all.
 *
 * "Some part of it is further along, and it does not lie entirely behind us" rather than "all of
 * it is past our edge", because the second reading loses the ordinary ragged case: a tall card
 * beside two stacked buttons overlaps both of them, and a press from either button has to be
 * able to reach it.
 */
static bool focus_is_candidate(const struct focus_span *src, const struct focus_span *cand) {
    return (src->end < cand->end || src->start < cand->start) && src->start < cand->end;
}

/* In line with the source across the press: the rows a left or right press runs along. */
static bool focus_beams_overlap(const struct focus_span *src, const struct focus_span *cand) {
    return cand->lo < src->hi && src->lo < cand->hi;
}

/* Entirely past the source's leading edge - as opposed to merely overlapping it. */
static bool focus_is_ahead(const struct focus_span *src, const struct focus_span *cand) {
    return src->end <= cand->start;
}

/* The gap in the direction pressed; zero for a candidate that overlaps the source. */
static int64_t focus_along(const struct focus_span *src, const struct focus_span *cand) {
    const int64_t d = cand->start - src->end;
    return (d > 0) ? d : 0;
}

/* To its far edge, and never zero: it is a divisor of attention rather than of arithmetic, and
   a candidate straddling the source would otherwise beat everything by being at distance 0. */
static int64_t focus_along_far(const struct focus_span *src, const struct focus_span *cand) {
    const int64_t d = cand->end - src->end;
    return (d > 1) ? d : 1;
}

/* How far off the line it sits: centre to centre, across the press. */
static int64_t focus_across(const struct focus_span *src, const struct focus_span *cand) {
    const int64_t d = src->center - cand->center;
    return (d < 0) ? -d : d;
}

static int64_t focus_clamp(int64_t v) {
    return (v > FOCUS_REACH) ? FOCUS_REACH : v;
}

/*
 * The score two in-line candidates are compared by: 13 * along² + across².
 *
 * Thirteen is Android's weight and the number itself is not the point - what it says is that
 * being *in line* is worth about three and a half times being *close*, so a cell slightly
 * further ahead beats a nearer one off to the side. An unweighted centre-to-centre distance is
 * what makes a cursor drift diagonally across a grid.
 */
static int64_t focus_weight(int64_t along, int64_t across) {
    const int64_t a = focus_clamp(along);
    const int64_t b = focus_clamp(across);
    return 13 * a * a + b * b;
}

/*
 * Whether `a` beats `b` on the beam alone, before any distance is measured.
 *
 * In line beats out of line, however much closer the out-of-line one is: that is what keeps a
 * press travelling along a chip strip instead of diving into the card below it at the first
 * ragged gap. The last clause is the one exception, and it is a vertical one - a candidate out
 * of the beam but *overlapping* the source's own row can still win a left or right press,
 * because it is beside us in every sense a reader has.
 */
static bool focus_beam_beats(const struct focus_span *src, const struct focus_span *a,
                             const struct focus_span *b, bool horizontal) {
    const bool a_in_beam = focus_beams_overlap(src, a);
    const bool b_in_beam = focus_beams_overlap(src, b);

    if (b_in_beam || !a_in_beam) {
        return false;
    }
    if (!focus_is_ahead(src, b)) {
        return true;
    }
    if (horizontal) {
        return true;
    }
    return focus_along(src, a) < focus_along_far(src, b);
}

static const struct inkcell_focus_item *focus_item(const struct inkcell_focus_map *map,
                                                   uint32_t id) {
    if (map == NULL || map->items == NULL || id == INKCELL_FOCUS_NONE) {
        return NULL;
    }
    for (uint32_t i = 0U; i < map->count; ++i) {
        if (map->items[i].id == id) {
            return &map->items[i];
        }
    }
    return NULL;
}

void inkcell_focus_begin(struct inkcell_focus_map *map, struct inkcell_focus_item *storage,
                         uint32_t capacity) {
    if (map == NULL) {
        return;
    }
    map->items = storage;
    map->count = 0U;
    map->capacity = (storage != NULL) ? capacity : 0U;
    map->dropped = 0U;
}

bool inkcell_focus_add(struct inkcell_focus_map *map, uint32_t id, int x, int y, int w, int h) {
    if (map == NULL || id == INKCELL_FOCUS_NONE || w <= 0 || h <= 0) {
        return false;
    }
    if (map->items == NULL || map->count >= map->capacity) {
        /* Counted rather than ignored: a screen that outgrew its array looks, on the panel,
           like a button that cannot be selected. */
        map->dropped += 1U;
        return false;
    }
    if (focus_item(map, id) != NULL) {
        return false;
    }
    map->items[map->count].id = id;
    map->items[map->count].rect.x = x;
    map->items[map->count].rect.y = y;
    map->items[map->count].rect.w = w;
    map->items[map->count].rect.h = h;
    map->count += 1U;
    return true;
}

bool inkcell_focus_has(const struct inkcell_focus_map *map, uint32_t id) {
    return focus_item(map, id) != NULL;
}

bool inkcell_focus_rect_of(const struct inkcell_focus_map *map, uint32_t id,
                           struct inkcell_focus_rect *out) {
    const struct inkcell_focus_item *item = focus_item(map, id);
    if (item == NULL) {
        return false;
    }
    if (out != NULL) {
        *out = item->rect;
    }
    return true;
}

uint32_t inkcell_focus_find(const struct inkcell_focus_map *map, uint32_t id,
                            enum inkcell_focus_dir dir) {
    const struct inkcell_focus_item *from = focus_item(map, id);
    if (from == NULL) {
        return INKCELL_FOCUS_NONE;
    }

    const bool horizontal = focus_horizontal(dir);
    const struct focus_span src = focus_project(from->rect, dir);
    uint32_t best_id = INKCELL_FOCUS_NONE;
    struct focus_span best = {0, 0, 0, 0, 0};
    int64_t best_weight = 0;

    for (uint32_t i = 0U; i < map->count; ++i) {
        if (map->items[i].id == id) {
            continue;
        }
        const struct focus_span cand = focus_project(map->items[i].rect, dir);
        if (!focus_is_candidate(&src, &cand)) {
            continue;
        }
        const int64_t weight = focus_weight(focus_along(&src, &cand), focus_across(&src, &cand));
        if (best_id == INKCELL_FOCUS_NONE) {
            best_id = map->items[i].id;
            best = cand;
            best_weight = weight;
            continue;
        }
        /* The beam decides first and the distance only breaks what it leaves level. A tie goes
           to whichever was registered earlier, so one layout always resolves one way. */
        if (focus_beam_beats(&src, &cand, &best, horizontal)) {
            best_id = map->items[i].id;
            best = cand;
            best_weight = weight;
        } else if (!focus_beam_beats(&src, &best, &cand, horizontal) && weight < best_weight) {
            best_id = map->items[i].id;
            best = cand;
            best_weight = weight;
        }
    }
    return best_id;
}

uint32_t inkcell_focus_find_wrapping(const struct inkcell_focus_map *map, uint32_t id,
                                     enum inkcell_focus_dir dir) {
    const uint32_t next = inkcell_focus_find(map, id, dir);
    if (next != INKCELL_FOCUS_NONE) {
        return next;
    }
    if (map == NULL) {
        return INKCELL_FOCUS_NONE;
    }

    /*
     * Walk the other way until the screen runs out, and answer with where that stopped - which
     * is the header's promise stated as code: wrapping lands where holding the opposite
     * direction would have.
     *
     * Bounded by the number of registered rectangles rather than by "until it stops moving". A
     * box that entirely contains another can be a candidate of it in the same direction that it
     * is a candidate of the box - two rectangles that each think the other is to their left -
     * and a walk with no bound on it would sit between them forever on a layout nobody
     * inspected.
     */
    const enum inkcell_focus_dir back = focus_opposite(dir);
    uint32_t far = id;
    for (uint32_t step = 0U; step < map->count; ++step) {
        const uint32_t prev = inkcell_focus_find(map, far, back);
        if (prev == INKCELL_FOCUS_NONE) {
            break;
        }
        far = prev;
    }
    return (far == id) ? INKCELL_FOCUS_NONE : far;
}

uint32_t inkcell_focus_first(const struct inkcell_focus_map *map) {
    if (map == NULL || map->items == NULL) {
        return INKCELL_FOCUS_NONE;
    }

    uint32_t best_id = INKCELL_FOCUS_NONE;
    struct inkcell_focus_rect best = {0, 0, 0, 0};
    for (uint32_t i = 0U; i < map->count; ++i) {
        const struct inkcell_focus_rect rect = map->items[i].rect;
        if (best_id == INKCELL_FOCUS_NONE || rect.y < best.y ||
            (rect.y == best.y && rect.x < best.x)) {
            best_id = map->items[i].id;
            best = rect;
        }
    }
    return best_id;
}

/* The gap between two extents on one axis, and zero where they overlap. */
static int64_t focus_gap(int64_t lo, int64_t hi, int64_t other_lo, int64_t other_hi) {
    if (hi <= other_lo) {
        return other_lo - hi;
    }
    if (other_hi <= lo) {
        return lo - other_hi;
    }
    return 0;
}

uint32_t inkcell_focus_nearest(const struct inkcell_focus_map *map,
                               struct inkcell_focus_rect rect) {
    if (map == NULL || map->items == NULL) {
        return INKCELL_FOCUS_NONE;
    }

    const int64_t x0 = rect.x;
    const int64_t x1 = (int64_t)rect.x + rect.w;
    const int64_t y0 = rect.y;
    const int64_t y1 = (int64_t)rect.y + rect.h;

    uint32_t best_id = INKCELL_FOCUS_NONE;
    int64_t best = 0;
    for (uint32_t i = 0U; i < map->count; ++i) {
        const struct inkcell_focus_rect other = map->items[i].rect;
        /* Box to box rather than centre to centre: a row that grew a second line is still the
           row the cursor was on, and a centre that moved half a line should not hand the cursor
           to its neighbour. */
        const int64_t dx = focus_clamp(focus_gap(x0, x1, other.x, (int64_t)other.x + other.w));
        const int64_t dy = focus_clamp(focus_gap(y0, y1, other.y, (int64_t)other.y + other.h));
        const int64_t distance = dx * dx + dy * dy;
        if (best_id == INKCELL_FOCUS_NONE || distance < best) {
            best_id = map->items[i].id;
            best = distance;
        }
    }
    return best_id;
}

bool inkcell_focus_dir_for_key(enum inkcell_key key, enum inkcell_focus_dir *dir) {
    enum inkcell_focus_dir resolved;

    switch (key) {
    case INKCELL_KEY_LEFT:
        resolved = INKCELL_FOCUS_LEFT;
        break;
    case INKCELL_KEY_RIGHT:
        resolved = INKCELL_FOCUS_RIGHT;
        break;
    case INKCELL_KEY_UP:
        resolved = INKCELL_FOCUS_UP;
        break;
    case INKCELL_KEY_DOWN:
        resolved = INKCELL_FOCUS_DOWN;
        break;
    default:
        return false;
    }
    if (dir != NULL) {
        *dir = resolved;
    }
    return true;
}
