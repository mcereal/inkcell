/*
 * The stack, the box arithmetic under it, and the width class.
 *
 * No pixels, no state and no allocation: everything here is a function of the numbers it is
 * handed, which is what lets tests/suites/ui_stack.c reach all of it without a panel.
 *
 * Two of the rules stated in inkcell/ui/stack.h are enforced in one place each, and both are
 * here rather than at the call sites for the reason the whole file is:
 *
 *   - what is placed adds up to the extent exactly, which is `share_out()`;
 *   - a run that cannot fit gives up items from its tail rather than drawing past its room,
 *     which is the first loop in `inkcell_stack_resolve()`.
 */

#include "inkcell/ui/stack.h"

#include <string.h>

/* ---- boxes ---------------------------------------------------------------------------------- */

struct inkcell_box inkcell_box_inset(struct inkcell_box box, int dx, int dy) {
    box.x += dx;
    box.y += dy;
    box.w -= 2 * dx;
    box.h -= 2 * dy;
    /* An inset deeper than the box leaves nothing, not a rectangle that draws backwards. A
       negative inset is an outset and is left alone - a card spends its hairline outward, and
       refusing that here would make it write the arithmetic out again. */
    if (box.w < 0) {
        box.w = 0;
    }
    if (box.h < 0) {
        box.h = 0;
    }
    return box;
}

struct inkcell_box inkcell_box_measure(struct inkcell_box box, int max_w) {
    if (max_w <= 0 || box.w <= max_w) {
        return box;
    }
    box.x += (box.w - max_w) / 2;
    box.w = max_w;
    return box;
}

/* ---- width classes -------------------------------------------------------------------------- */

enum inkcell_width_class inkcell_width_class_of(size_t cols) {
    if (cols >= INKCELL_WIDTH_EXPANDED_COLS) {
        return INKCELL_WIDTH_EXPANDED;
    }
    if (cols >= INKCELL_WIDTH_MEDIUM_COLS) {
        return INKCELL_WIDTH_MEDIUM;
    }
    return INKCELL_WIDTH_COMPACT;
}

/* ---- the run ---------------------------------------------------------------------------------
 *
 * The axis is spelled out through these two rather than by branching at each use. A stack is
 * the same arithmetic twice with the two dimensions swapped, and the version that branches on
 * the axis inside the placement loop is the version where the row case and the column case
 * drift apart.
 */
static int main_extent(const struct inkcell_box *box, enum inkcell_axis axis) {
    return axis == INKCELL_AXIS_X ? box->w : box->h;
}

static int cross_extent(const struct inkcell_box *box, enum inkcell_axis axis) {
    return axis == INKCELL_AXIS_X ? box->h : box->w;
}

static int main_start(const struct inkcell_box *box, enum inkcell_axis axis) {
    return axis == INKCELL_AXIS_X ? box->x : box->y;
}

static int cross_start(const struct inkcell_box *box, enum inkcell_axis axis) {
    return axis == INKCELL_AXIS_X ? box->y : box->x;
}

/*
 * Shares `total` out among `count` weights, writing one part each to `out`.
 *
 * The parts sum to `total` exactly. Each gets its floor of the proportional share first, which
 * together lose under one unit apiece, and the units that were lost go to the weights that lost
 * the most of one - the largest-remainder method, which is the split that is off by the least
 * everywhere at once, and the same one `inkcell_proportion_split()` uses for a bar's segments.
 *
 * Two things it does *not* do, both of which that function does and neither of which belongs
 * here. It does not round a zero-weight entry up to a unit: an item that did not ask to grow
 * must get nothing, where a segment of a bar that is really there must be visible. And it takes
 * no view about `total` being small - a leftover of two pixels across three items is two items
 * a pixel wider and one unchanged, which is correct and is not a picture anybody reads.
 *
 * Ties go to the lower index, so the same run resolves the same way every frame. A layout that
 * shifted a pixel between frames on a tie would flicker.
 */
static void share_out(const uint64_t *weights, uint32_t count, int total, int *out) {
    uint64_t sum = 0U;
    for (uint32_t i = 0U; i < count; ++i) {
        out[i] = 0;
        sum += weights[i];
    }
    if (sum == 0U || total <= 0) {
        return;
    }

    int given = 0;
    for (uint32_t i = 0U; i < count; ++i) {
        out[i] = (int)(((uint64_t)total * weights[i]) / sum);
        given += out[i];
    }

    /* Each floor lost under a unit, so what is left is fewer units than there are weights and
       the loop below runs at most `count` times. */
    bool topped[INKCELL_STACK_MAX] = {false};
    for (int left = total - given; left > 0; --left) {
        uint32_t best = count;
        uint64_t best_remainder = 0U;
        for (uint32_t i = 0U; i < count; ++i) {
            if (weights[i] == 0U || topped[i]) {
                continue;
            }
            const uint64_t remainder = ((uint64_t)total * weights[i]) % sum;
            if (best == count || remainder > best_remainder) {
                best = i;
                best_remainder = remainder;
            }
        }
        if (best == count) {
            break;
        }
        out[best] += 1;
        topped[best] = true;
    }
}

void inkcell_stack_begin(struct inkcell_stack *stack, struct inkcell_box box,
                         enum inkcell_axis axis, int gap) {
    if (stack == NULL) {
        return;
    }
    memset(stack, 0, sizeof(*stack));
    stack->box = box;
    stack->axis = axis;
    /* A negative gap is items drawn on top of each other, which no layout ever meant. */
    stack->gap = gap > 0 ? gap : 0;
}

bool inkcell_stack_add(struct inkcell_stack *stack, struct inkcell_stack_item item) {
    if (stack == NULL || stack->count >= INKCELL_STACK_MAX) {
        return false;
    }
    if (item.basis < 0) {
        item.basis = 0;
    }
    if (item.cross < 0) {
        item.cross = 0;
    }
    /* A floor above what the item asked for is a caller contradicting itself. The floor gives
       way, because `min` only ever constrains shrinking and an item that is not shrinking has
       no use for one - taking `basis` up to it instead would grow an item that asked for a
       size, which is the more surprising of the two readings. */
    if (item.min < 0) {
        item.min = 0;
    }
    if (item.min > item.basis) {
        item.min = item.basis;
    }
    stack->items[stack->count] = item;
    stack->count += 1U;
    return true;
}

bool inkcell_stack_add_fixed(struct inkcell_stack *stack, int basis) {
    const struct inkcell_stack_item item = {.basis = basis};
    return inkcell_stack_add(stack, item);
}

bool inkcell_stack_add_grow(struct inkcell_stack *stack, int basis, uint8_t grow) {
    const struct inkcell_stack_item item = {.basis = basis, .grow = grow};
    return inkcell_stack_add(stack, item);
}

uint32_t inkcell_stack_resolve(const struct inkcell_stack *stack, struct inkcell_box *out,
                               uint32_t max) {
    if (stack == NULL || out == NULL) {
        return 0U;
    }
    const uint32_t count = stack->count;
    /* Refused rather than truncated: a run laid out against a total the caller cannot see is
       worse than no answer, for inkcell_fb_damage_rects()'s reason. */
    if (max < count) {
        return 0U;
    }
    for (uint32_t i = 0U; i < count; ++i) {
        out[i] = (struct inkcell_box){0};
    }
    if (count == 0U) {
        return 0U;
    }

    const int extent = main_extent(&stack->box, stack->axis);
    const int cross = cross_extent(&stack->box, stack->axis);
    if (extent <= 0 || cross <= 0) {
        return 0U;
    }

    /*
     * How many of the leading items the room can hold at all, which is asked before anything is
     * sized because the answer changes the gaps.
     *
     * The test is against what the run costs with everything squeezed as far as it is allowed -
     * `min` where an item may shrink, `basis` where it may not - so an item is only given up
     * when shrinking genuinely cannot save it.
     */
    uint32_t placed = count;
    while (placed > 0U) {
        int floor_cost = (int)(placed - 1U) * stack->gap;
        for (uint32_t i = 0U; i < placed; ++i) {
            const struct inkcell_stack_item *item = &stack->items[i];
            floor_cost += item->shrink > 0U ? item->min : item->basis;
        }
        if (floor_cost <= extent) {
            break;
        }
        placed -= 1U;
    }
    if (placed == 0U) {
        return 0U;
    }

    int sizes[INKCELL_STACK_MAX];
    int cost = (int)(placed - 1U) * stack->gap;
    for (uint32_t i = 0U; i < placed; ++i) {
        sizes[i] = stack->items[i].basis;
        cost += sizes[i];
    }

    uint64_t weights[INKCELL_STACK_MAX];
    int parts[INKCELL_STACK_MAX];
    int leftover = extent - cost;

    if (leftover > 0) {
        uint64_t total = 0U;
        for (uint32_t i = 0U; i < placed; ++i) {
            weights[i] = stack->items[i].grow;
            total += weights[i];
        }
        if (total > 0U) {
            share_out(weights, placed, leftover, parts);
            for (uint32_t i = 0U; i < placed; ++i) {
                sizes[i] += parts[i];
            }
            leftover = 0; /* anything that grows takes all of it, so there is none to justify */
        }
    } else if (leftover < 0) {
        /*
         * Shrinking, weighted by how much an item has as well as by how much it offered - so a
         * wide item gives up more than a narrow one, and a row does not squeeze its smallest
         * part away first.
         *
         * A pass at a time, because an item that hits its floor has to hand the rest of its
         * share back to the others. Each pass either clears the deficit or puts at least one
         * item on its floor, so it runs at most once per item; and the loop above has already
         * established that the floors add up to something the room can hold, which is what
         * guarantees the deficit is gone by the end.
         */
        int deficit = -leftover;
        bool floored[INKCELL_STACK_MAX] = {false};
        for (uint32_t pass = 0U; pass < placed && deficit > 0; ++pass) {
            uint64_t total = 0U;
            for (uint32_t i = 0U; i < placed; ++i) {
                weights[i] =
                    floored[i] ? 0U : (uint64_t)stack->items[i].shrink * (uint64_t)sizes[i];
                total += weights[i];
            }
            if (total == 0U) {
                break;
            }
            share_out(weights, placed, deficit, parts);
            for (uint32_t i = 0U; i < placed; ++i) {
                if (weights[i] == 0U) {
                    continue;
                }
                int take = parts[i];
                if (sizes[i] - take <= stack->items[i].min) {
                    take = sizes[i] - stack->items[i].min;
                    floored[i] = true;
                }
                sizes[i] -= take;
                deficit -= take;
            }
        }
        leftover = 0;
    }

    /*
     * What to do with room nothing asked for. Only reachable when the run has no growing item
     * in it - one that does absorbs the lot above.
     */
    int offset = 0;
    int spread[INKCELL_STACK_MAX] = {0};
    if (leftover > 0) {
        switch (stack->justify) {
        case INKCELL_JUSTIFY_CENTER:
            offset = leftover / 2;
            break;
        case INKCELL_JUSTIFY_END:
            offset = leftover;
            break;
        case INKCELL_JUSTIFY_BETWEEN:
            /* Shared out over the gaps rather than the items, so the first sits at the leading
               edge and the last at the trailing one. A run of one has no gap to put it in and
               stays where it is - stretching it would be a different request. */
            if (placed > 1U) {
                for (uint32_t i = 0U; i + 1U < placed; ++i) {
                    weights[i] = 1U;
                }
                share_out(weights, placed - 1U, leftover, spread);
            }
            break;
        case INKCELL_JUSTIFY_START:
        default:
            break;
        }
    }

    const int main_at = main_start(&stack->box, stack->axis);
    const int cross_at = cross_start(&stack->box, stack->axis);
    int at = main_at + offset;

    for (uint32_t i = 0U; i < placed; ++i) {
        const struct inkcell_stack_item *item = &stack->items[i];
        enum inkcell_align align = item->align;
        if (align == INKCELL_ALIGN_DEFAULT) {
            align = stack->align == INKCELL_ALIGN_DEFAULT ? INKCELL_ALIGN_STRETCH : stack->align;
        }

        int size = sizes[i];
        if (size < 0) {
            size = 0;
        }

        /* Stretch ignores the item's own cross size: stretching *is* the answer to that
           question, and an item stating both would be saying two things. */
        int span = align == INKCELL_ALIGN_STRETCH || item->cross <= 0 ? cross : item->cross;
        if (span > cross) {
            span = cross;
        }
        int span_at = 0;
        if (align == INKCELL_ALIGN_CENTER) {
            span_at = (cross - span) / 2;
        } else if (align == INKCELL_ALIGN_END) {
            span_at = cross - span;
        }

        if (stack->axis == INKCELL_AXIS_X) {
            out[i].x = at;
            out[i].w = size;
            out[i].y = cross_at + span_at;
            out[i].h = span;
        } else {
            out[i].y = at;
            out[i].h = size;
            out[i].x = cross_at + span_at;
            out[i].w = span;
        }

        at += size;
        if (i + 1U < placed) {
            at += stack->gap + spread[i];
        }
    }

    return placed;
}
