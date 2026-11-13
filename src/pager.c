/*
 * Pages a finger drags between. See include/inkcell/ui/pager.h.
 */
#include "inkcell/ui/pager.h"

#include "inkcell/ui/scroll.h"

#include <string.h>

void inkcell_pager_init(struct inkcell_pager *pager, int32_t slop, int32_t flick,
                        uint32_t duration_ms) {
    memset(pager, 0, sizeof *pager);
    pager->slop = slop > 0 ? slop : 1;
    pager->flick = flick > 0 ? flick : 1;
    pager->count = 1U;
    pager->duration_ms = duration_ms;
}

static uint32_t last_page(const struct inkcell_pager *pager) {
    return pager->count > 0U ? pager->count - 1U : 0U;
}

static int32_t page_x(const struct inkcell_pager *pager, uint32_t page) {
    return (int32_t)page * pager->page_w;
}

void inkcell_pager_extent(struct inkcell_pager *pager, int32_t page_w, uint32_t count) {
    pager->page_w = page_w > 0 ? page_w : 1;
    pager->count = count > 0U ? count : 1U;
    if (pager->page > last_page(pager)) {
        pager->page = last_page(pager);
    }
}

/* Where a settle has the pages: from where they were let go to the page, eased. */
static int32_t settled(const struct inkcell_pager *pager, uint64_t now_ms) {
    const int32_t to = page_x(pager, pager->page);
    if (pager->travel.duration_ms == 0U) {
        return to;
    }
    const int32_t progress = inkcell_anim_value(&pager->travel, now_ms);
    return pager->from + (int32_t)((int64_t)(to - pager->from) * progress / INKCELL_ANIM_ONE);
}

/* A drag's raw offset through the rubber band past either end. */
static int32_t banded(const struct inkcell_pager *pager, int32_t raw) {
    const int32_t end = page_x(pager, last_page(pager));
    if (raw < 0) {
        return inkcell_scroll_rubber(raw, pager->page_w);
    }
    if (raw > end) {
        return end + inkcell_scroll_rubber(raw - end, pager->page_w);
    }
    return raw;
}

int32_t inkcell_pager_offset(const struct inkcell_pager *pager, uint64_t now_ms) {
    if (pager->down) {
        /* A finger holds the pages: where it caught them, less the drag. */
        return pager->claimed ? banded(pager, pager->from - pager->drag) : pager->from;
    }
    return settled(pager, now_ms);
}

bool inkcell_pager_active(const struct inkcell_pager *pager, uint64_t now_ms) {
    return (pager->down && pager->claimed) || inkcell_anim_active(&pager->travel, now_ms);
}

static void sample(struct inkcell_pager *pager, int32_t x, uint64_t now_ms) {
    if (pager->sample_count == INKCELL_PAGER_SAMPLES) {
        memmove(&pager->samples[0], &pager->samples[1],
                (INKCELL_PAGER_SAMPLES - 1U) * sizeof pager->samples[0]);
        pager->sample_count--;
    }
    pager->samples[pager->sample_count++] = (struct inkcell_pager_sample){x, now_ms};
}

void inkcell_pager_down(struct inkcell_pager *pager, int32_t x, int32_t y, uint64_t now_ms) {
    /* Catch the pages where they are: a finger on a settle stops it, as on every phone. */
    pager->from = inkcell_pager_offset(pager, now_ms);
    pager->travel = (struct inkcell_anim){0};
    pager->down = true;
    pager->decided = false;
    pager->claimed = false;
    pager->x0 = x;
    pager->y0 = y;
    pager->drag = 0;
    pager->sample_count = 0U;
    sample(pager, x, now_ms);
}

static int32_t magnitude(int32_t v) {
    return v < 0 ? -v : v;
}

bool inkcell_pager_move(struct inkcell_pager *pager, int32_t x, int32_t y, uint64_t now_ms) {
    if (!pager->down) {
        return false;
    }
    sample(pager, x, now_ms);
    const int32_t dx = x - pager->x0;
    const int32_t dy = y - pager->y0;
    if (!pager->decided) {
        if (magnitude(dy) >= pager->slop && magnitude(dy) > magnitude(dx)) {
            pager->decided = true; /* down first: somebody else's gesture */
        } else if (magnitude(dx) >= pager->slop && magnitude(dx) > magnitude(dy)) {
            pager->decided = true;
            pager->claimed = true;
            /* The drag starts where the slop ended, so the pages do not jump by it. */
            pager->x0 += dx > 0 ? pager->slop : -pager->slop;
        }
    }
    if (pager->claimed) {
        pager->drag = x - pager->x0;
    }
    return pager->claimed;
}

/* The finger's speed as it let go, pixels a second, + to the right: over the samples no older
   than INKCELL_PAGER_VELOCITY_MS before the last. */
static int32_t release_speed(const struct inkcell_pager *pager) {
    if (pager->sample_count < 2U) {
        return 0;
    }
    const struct inkcell_pager_sample *last = &pager->samples[pager->sample_count - 1U];
    const struct inkcell_pager_sample *first = last;
    for (uint32_t i = pager->sample_count - 1U; i-- > 0U;) {
        if (last->at_ms - pager->samples[i].at_ms > INKCELL_PAGER_VELOCITY_MS) {
            break;
        }
        first = &pager->samples[i];
    }
    const uint64_t dt = last->at_ms - first->at_ms;
    return dt > 0U ? (int32_t)((int64_t)(last->x - first->x) * 1000 / (int64_t)dt) : 0;
}

bool inkcell_pager_up(struct inkcell_pager *pager, uint64_t now_ms, bool cancelled) {
    if (!pager->down) {
        return false;
    }
    const bool claimed = pager->claimed;
    const int32_t shown = inkcell_pager_offset(pager, now_ms);
    if (claimed && !cancelled) {
        const int32_t raw = pager->from - pager->drag;
        const int32_t speed = release_speed(pager);
        /* The page the finger caught, rounded: where a flick counts one on from. */
        const int32_t caught = (pager->from + pager->page_w / 2) / pager->page_w;
        int32_t page;
        if (magnitude(speed) >= pager->flick && magnitude(pager->drag) >= pager->slop) {
            page = speed < 0 ? caught + 1 : caught - 1; /* a finger going left turns to the next */
        } else {
            page = raw >= 0 ? (raw + pager->page_w / 2) / pager->page_w : -1;
        }
        if (page < 0) {
            page = 0;
        } else if ((uint32_t)page > last_page(pager)) {
            page = (int32_t)last_page(pager);
        }
        pager->page = (uint32_t)page;
    }
    pager->down = false;
    pager->claimed = false;
    pager->from = shown;
    inkcell_anim_set(&pager->travel, 0);
    inkcell_anim_to(&pager->travel, now_ms, INKCELL_ANIM_ONE,
                    shown == page_x(pager, pager->page) ? 0U : pager->duration_ms,
                    INKCELL_EASE_OUT);
    return claimed;
}

void inkcell_pager_to(struct inkcell_pager *pager, uint32_t page, uint64_t now_ms) {
    if (pager->down) {
        return; /* a finger has the pages */
    }
    const int32_t shown = inkcell_pager_offset(pager, now_ms);
    pager->page = page > last_page(pager) ? last_page(pager) : page;
    pager->from = shown;
    inkcell_anim_set(&pager->travel, 0);
    inkcell_anim_to(&pager->travel, now_ms, INKCELL_ANIM_ONE,
                    shown == page_x(pager, pager->page) ? 0U : pager->duration_ms,
                    INKCELL_EASE_OUT);
}
