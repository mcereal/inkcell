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
 * In line beats out of line, and sideways that is the whole rule: it is what keeps a press
 * travelling along a chip strip instead of diving into the card below it at the first ragged
 * gap. Two things stop it being absolute, and both are Android's.
 *
 * An out-of-beam candidate *overlapping* the source's leading edge is never beaten on the beam,
 * whichever way the press went: it is beside us in every sense a reader has, and the overlap is
 * how a tall card beside two short buttons presents itself.
 *
 * And a vertical press releases the rule by distance - an in-line candidate wins only while it
 * is nearer than the far edge of the out-of-line one. A column is normally tight enough that it
 * wins on distance regardless; where it is not, this is the cursor stepping to the control
 * beside it rather than leaping the height of the panel to stay in line. The contract states
 * this, and focus_beam_is_absolute_sideways_and_not_vertically holds it.
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
    map->focused = INKCELL_FOCUS_NONE;
    map->cued = false;
}

void inkcell_focus_mark(struct inkcell_focus_map *map, uint32_t id) {
    if (map == NULL || id == INKCELL_FOCUS_NONE) {
        return;
    }
    map->focused = id;
    map->cued = false;
}

void inkcell_focus_mark_cued(struct inkcell_focus_map *map, uint32_t id) {
    if (map == NULL || id == INKCELL_FOCUS_NONE) {
        return;
    }
    map->focused = id;
    map->cued = true;
}

uint32_t inkcell_focus_marked(const struct inkcell_focus_map *map) {
    return (map != NULL && !map->cued) ? map->focused : INKCELL_FOCUS_NONE;
}

bool inkcell_focus_add(struct inkcell_focus_map *map, uint32_t id, int x, int y, int w, int h) {
    return inkcell_focus_add_round(map, id, x, y, w, h, 0);
}

static bool focus_put(struct inkcell_focus_map *map, uint32_t id, int x, int y, int w, int h,
                      int radius, bool pointer_only) {
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
    /* Clamped the way a fill clamps it, so what is recorded is the curve that was drawn rather
       than the one that was asked for: "as round as it goes" is a request, and half the shorter
       side is what it comes out as. */
    const int shorter = (w < h) ? w : h;
    map->items[map->count].radius =
        (radius < 0) ? 0 : ((radius > shorter / 2) ? shorter / 2 : radius);
    map->items[map->count].pointer_only = pointer_only;
    map->count += 1U;
    return true;
}

bool inkcell_focus_add_round(struct inkcell_focus_map *map, uint32_t id, int x, int y, int w, int h,
                             int radius) {
    return focus_put(map, id, x, y, w, h, radius, false);
}

bool inkcell_focus_add_target(struct inkcell_focus_map *map, uint32_t id, int x, int y, int w,
                              int h, int radius) {
    return focus_put(map, id, x, y, w, h, radius, true);
}

uint32_t inkcell_focus_hit(const struct inkcell_focus_map *map, int x, int y) {
    if (map == NULL || map->items == NULL) {
        return INKCELL_FOCUS_NONE;
    }
    /* Backwards, so the first box found is the one drawn last - the one on top. */
    for (uint32_t i = map->count; i-- > 0U;) {
        const struct inkcell_focus_rect r = map->items[i].rect;
        if ((int64_t)x >= r.x && (int64_t)x < (int64_t)r.x + r.w && (int64_t)y >= r.y &&
            (int64_t)y < (int64_t)r.y + r.h) {
            return map->items[i].id;
        }
    }
    return INKCELL_FOCUS_NONE;
}

enum inkcell_key inkcell_focus_key_of(uint32_t id) {
    if (id <= INKCELL_FOCUS_KEY_BASE) {
        return INKCELL_KEY_NONE;
    }
    const uint32_t key = id - INKCELL_FOCUS_KEY_BASE;
    return key <= (uint32_t)INKCELL_KEY_SELECT ? (enum inkcell_key)key : INKCELL_KEY_NONE;
}

enum inkcell_key inkcell_focus_action_key_of(uint32_t id) {
    if (id <= INKCELL_FOCUS_ACTION_KEY_BASE) {
        return INKCELL_KEY_NONE;
    }
    const uint32_t key = id - INKCELL_FOCUS_ACTION_KEY_BASE;
    return key <= (uint32_t)INKCELL_KEY_SELECT ? (enum inkcell_key)key : INKCELL_KEY_NONE;
}

int inkcell_focus_radius_of(const struct inkcell_focus_map *map, uint32_t id) {
    const struct inkcell_focus_item *item = focus_item(map, id);
    return (item != NULL) ? item->radius : 0;
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
        if (map->items[i].id == id || map->items[i].pointer_only) {
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
    const struct inkcell_focus_item *from = focus_item(map, id);
    if (from == NULL) {
        return INKCELL_FOCUS_NONE;
    }

    /*
     * The furthest one back that is still in line: the other end of the row, and nothing that
     * is not on it.
     *
     * The beam is checked here and not left to fall out of walking the other direction one
     * press at a time. That walk is what this used to do, and it leaked: a press with nothing
     * in line ahead of it is *entitled* to leave the row (see the note on the beam above), so
     * the last step of a walk along a strip could step off it onto whatever row below happened
     * to extend further back, and the wrap answered with that. Asking the question this file
     * exists to answer - what is in line with me - is also cheaper than n presses, and it
     * cannot loop on a layout where two boxes each contain the other.
     */
    const struct focus_span src = focus_project(from->rect, dir);
    uint32_t best_id = INKCELL_FOCUS_NONE;
    struct focus_span best = {0, 0, 0, 0, 0};

    for (uint32_t i = 0U; i < map->count; ++i) {
        if (map->items[i].id == id || map->items[i].pointer_only) {
            continue;
        }
        const struct focus_span cand = focus_project(map->items[i].rect, dir);
        if (!focus_beams_overlap(&src, &cand)) {
            continue;
        }
        if (best_id == INKCELL_FOCUS_NONE || cand.start < best.start) {
            best_id = map->items[i].id;
            best = cand;
        }
    }
    return best_id;
}

/*
 * The item `id` is, within a run that holds it - or false when no run does.
 *
 * A run is a half-open range of ids and nothing else, so this is a bounds check rather than a
 * lookup: `base` is item 0 and the last item is `base + count - 1`. A run with no items holds
 * nothing, and one based at INKCELL_FOCUS_NONE is a run whose first item is the id meaning
 * "nothing", which is a caller mistake rather than a range.
 */
static bool focus_run_index(const struct inkcell_focus_run *run, uint32_t id, uint32_t *index) {
    if (run == NULL || run->count == 0U || run->base == INKCELL_FOCUS_NONE || id < run->base) {
        return false;
    }
    const uint32_t offset = id - run->base;
    if (offset >= run->count) {
        return false;
    }
    *index = offset;
    return true;
}

/* Whether item `index` of this run is somewhere a cursor can be. Every item, unless the run
   named the ones that are not. */
static bool focus_run_stands(const struct inkcell_focus_run *run, uint32_t index) {
    return run->focusable == NULL || run->focusable[index] != 0U;
}

/* Items per row, with the two values that mean a column folded into one. */
static uint32_t focus_run_stride(const struct inkcell_focus_run *run) {
    return run->stride > 1U ? run->stride : 1U;
}

/*
 * The next place to stand in a column, or INKCELL_FOCUS_NONE at its end.
 *
 * The walk is past the labels rather than one step, which is what makes this one press rather
 * than two: a list's subheaders take an item index and are not places to stand, so the next
 * *item* and the next thing a cursor can be on are not always the same number.
 */
static uint32_t focus_run_step_column(const struct inkcell_focus_run *run, uint32_t index,
                                      enum inkcell_focus_dir dir) {
    uint32_t step = index;
    while (dir == INKCELL_FOCUS_DOWN && step + 1U < run->count) {
        step += 1U;
        if (focus_run_stands(run, step)) {
            return run->base + step;
        }
    }
    while (dir == INKCELL_FOCUS_UP && step > 0U) {
        step -= 1U;
        if (focus_run_stands(run, step)) {
            return run->base + step;
        }
    }
    return INKCELL_FOCUS_NONE;
}

/*
 * The same in a grid, where the press decides which of the two axes the run is answering on.
 *
 * Sideways stops at the end of its row rather than wrapping into the next one, and downward
 * lands on the last tile of a short last row rather than on nothing - both for the reasons set
 * out beside `stride` in include/inkcell/ui/focus.h. The label walk of a column is kept on both
 * axes: a grid with gaps in it is the same problem as a list with headings in it, and the run
 * is told about either the same way.
 */
static uint32_t focus_run_step_grid(const struct inkcell_focus_run *run, uint32_t index,
                                    enum inkcell_focus_dir dir) {
    const uint32_t stride = focus_run_stride(run);
    const uint32_t last_row = (run->count - 1U) / stride;
    uint32_t step = index;
    uint32_t col = index % stride;

    switch (dir) {
    case INKCELL_FOCUS_LEFT:
        while (col > 0U) {
            col -= 1U;
            step -= 1U;
            if (focus_run_stands(run, step)) {
                return run->base + step;
            }
        }
        return INKCELL_FOCUS_NONE;
    case INKCELL_FOCUS_RIGHT:
        while (col + 1U < stride && step + 1U < run->count) {
            col += 1U;
            step += 1U;
            if (focus_run_stands(run, step)) {
                return run->base + step;
            }
        }
        return INKCELL_FOCUS_NONE;
    case INKCELL_FOCUS_UP:
        while (step >= stride) {
            step -= stride;
            if (focus_run_stands(run, step)) {
                return run->base + step;
            }
        }
        return INKCELL_FOCUS_NONE;
    case INKCELL_FOCUS_DOWN:
    default:
        /* `count - step` rather than `step + stride`, because the sum is the one form of this
           that can wrap - and a run is handed counts a caller owns. */
        while (run->count - step > stride) {
            step += stride;
            if (focus_run_stands(run, step)) {
                return run->base + step;
            }
        }
        if (step / stride < last_row) {
            /*
             * A row below, and no tile directly under this one: the short last row, where the
             * press means its last tile.
             *
             * Walked back along that row rather than taken as `count - 1`, because the last
             * tile of a row is not always a place to stand - and stopping there because of a
             * tile the reader cannot reach anyway would leave the grid while the row still has
             * something in it. The walk is the row's own, so it stops at the column it starts
             * in and never steps up into the row above.
             */
            uint32_t tail = run->count - 1U;
            for (;;) {
                if (focus_run_stands(run, tail)) {
                    return run->base + tail;
                }
                if (tail % stride == 0U) {
                    break;
                }
                tail -= 1U;
            }
        }
        return INKCELL_FOCUS_NONE;
    }
}

uint32_t inkcell_focus_step(const struct inkcell_focus_map *map, uint32_t id,
                            enum inkcell_focus_dir dir, const struct inkcell_focus_run *runs,
                            size_t run_count) {
    /*
     * A run that can still move wins, and only along the axes it runs on: a column answers up
     * and down, and a grid answers all four.
     *
     * Before the geometry rather than after it, which is the whole of what this function adds:
     * at the last *visible* row of a long list the geometry has an answer - whatever is drawn
     * below the list - and taking it would walk the cursor out at item ten of four hundred.
     * The run knows the list goes on; the map cannot.
     *
     * A grid's window always holds whole rows, so sideways the map usually has the answer too -
     * and the run gives it anyway, because "right is the next tile and never the next row's
     * first" is then true by construction rather than true because the finder's beam happened
     * to agree with the layout that frame.
     */
    if (runs != NULL) {
        for (size_t i = 0U; i < run_count; ++i) {
            uint32_t index = 0U;
            if (!focus_run_index(&runs[i], id, &index)) {
                continue;
            }
            uint32_t next = INKCELL_FOCUS_NONE;
            if (focus_run_stride(&runs[i]) > 1U) {
                next = focus_run_step_grid(&runs[i], index, dir);
            } else if (dir == INKCELL_FOCUS_UP || dir == INKCELL_FOCUS_DOWN) {
                next = focus_run_step_column(&runs[i], index, dir);
            }
            if (next != INKCELL_FOCUS_NONE) {
                return next;
            }
            /* Nothing left in the run that way - the end of it, the edge of a grid's row, or
               nothing but labels between here and the end. The press means leaving, which is a
               question about what is drawn and therefore the finder's. Stop looking through the
               runs: an id is in one of them at most. */
            break;
        }
    }
    return inkcell_focus_find(map, id, dir);
}

uint32_t inkcell_focus_first(const struct inkcell_focus_map *map) {
    if (map == NULL || map->items == NULL) {
        return INKCELL_FOCUS_NONE;
    }

    uint32_t best_id = INKCELL_FOCUS_NONE;
    struct inkcell_focus_rect best = {0, 0, 0, 0};
    for (uint32_t i = 0U; i < map->count; ++i) {
        if (map->items[i].pointer_only) {
            continue;
        }
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
        if (map->items[i].pointer_only) {
            continue;
        }
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
