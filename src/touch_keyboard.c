/*
 * The keyboard a finger types on: its rows, which key a point is on, and what a press amounts to.
 * See include/inkcell/ui/touch_keyboard.h.
 */

#include "inkcell/ui/touch_keyboard.h"

#include <stdio.h>
#include <string.h>

#define CH(s) {INKCELL_TKB_CHAR, (s), 10U, 0U, 0U}
#define LEAD(s) {INKCELL_TKB_CHAR, (s), 10U, 5U, 0U}
#define SHIFT {INKCELL_TKB_SHIFT, NULL, 15U, 0U, 0U}
#define DEL {INKCELL_TKB_DELETE, NULL, 15U, 0U, 0U}
#define TO(label, layer) {INKCELL_TKB_LAYER, (label), 15U, 0U, (uint8_t)(layer)}
/* The bottom row is the same on every layer but for the key that leaves it. */
#define BOTTOM(label, layer)                                                                       \
    TO(label, layer), {INKCELL_TKB_HIDE, NULL, 10U, 0U, 0U}, CH(","),                              \
        {INKCELL_TKB_SPACE, NULL, 40U, 0U, 0U}, CH("."), {                                         \
        INKCELL_TKB_ENTER, NULL, 15U, 0U, 0U                                                       \
    }

struct tkb_row {
    struct inkcell_tkb_key keys[INKCELL_TKB_KEYS_MAX];
    size_t count;
};

#define ROW(...)                                                                                   \
    {{__VA_ARGS__},                                                                                \
     sizeof((struct inkcell_tkb_key[]){__VA_ARGS__}) / sizeof(struct inkcell_tkb_key)}

static const struct tkb_row k_rows[INKCELL_TKB_LAYER_COUNT][INKCELL_TKB_ROWS] = {
    [INKCELL_TKB_LETTERS] =
        {
            ROW(CH("q"), CH("w"), CH("e"), CH("r"), CH("t"), CH("y"), CH("u"), CH("i"), CH("o"),
                CH("p")),
            ROW(LEAD("a"), CH("s"), CH("d"), CH("f"), CH("g"), CH("h"), CH("j"), CH("k"), CH("l")),
            ROW(SHIFT, CH("z"), CH("x"), CH("c"), CH("v"), CH("b"), CH("n"), CH("m"), DEL),
            ROW(BOTTOM("?123", INKCELL_TKB_SYMBOLS)),
        },
    [INKCELL_TKB_SYMBOLS] =
        {
            ROW(CH("1"), CH("2"), CH("3"), CH("4"), CH("5"), CH("6"), CH("7"), CH("8"), CH("9"),
                CH("0")),
            ROW(CH("@"), CH("#"), CH("$"), CH("_"), CH("&"), CH("-"), CH("+"), CH("("), CH(")"),
                CH("/")),
            ROW(TO("=\\<", INKCELL_TKB_MORE), CH("*"), CH("\""), CH("'"), CH(":"), CH(";"), CH("!"),
                CH("?"), DEL),
            ROW(BOTTOM("ABC", INKCELL_TKB_LETTERS)),
        },
    [INKCELL_TKB_MORE] =
        {
            ROW(CH("~"), CH("`"), CH("|"), CH("^"), CH("="), CH("{"), CH("}"), CH("["), CH("]"),
                CH("\\")),
            ROW(LEAD("<"), CH(">"), CH("%"), CH("€"), CH("£"), CH("¥"), CH("°"), CH("×"), CH("÷")),
            ROW(TO("?123", INKCELL_TKB_SYMBOLS), CH("«"), CH("»"), CH("¿"), CH("¡"), CH("…"),
                CH("±"), CH("§"), DEL),
            ROW(BOTTOM("ABC", INKCELL_TKB_LETTERS)),
        },
};

const struct inkcell_tkb_key *inkcell_touch_keyboard_row(unsigned layer, unsigned row,
                                                         size_t *count) {
    if (layer >= INKCELL_TKB_LAYER_COUNT || row >= INKCELL_TKB_ROWS) {
        if (count != NULL) {
            *count = 0U;
        }
        return NULL;
    }
    if (count != NULL) {
        *count = k_rows[layer][row].count;
    }
    return k_rows[layer][row].keys;
}

void inkcell_touch_keyboard_reset(struct inkcell_touch_keyboard *kb) {
    if (kb == NULL) {
        return;
    }
    memset(kb, 0, sizeof *kb);
    kb->row = -1;
    kb->key = -1;
}

/* Units to pixels along a row `w` wide: rounded per edge, so adjacent keys share an edge. */
static int32_t tkb_px(int32_t w, unsigned units) {
    return (int32_t)(((int64_t)w * units + INKCELL_TKB_UNITS / 2U) / INKCELL_TKB_UNITS);
}

static int32_t tkb_row_y(struct inkcell_tkb_box box, unsigned row) {
    return box.y + (int32_t)(((int64_t)box.h * row) / INKCELL_TKB_ROWS);
}

struct inkcell_tkb_box inkcell_touch_keyboard_key_box(unsigned layer, unsigned row, unsigned key,
                                                      struct inkcell_tkb_box box) {
    struct inkcell_tkb_box out = {0, 0, 0, 0};
    size_t count = 0U;
    const struct inkcell_tkb_key *keys = inkcell_touch_keyboard_row(layer, row, &count);
    if (keys == NULL || key >= count) {
        return out;
    }
    unsigned at = 0U;
    for (unsigned i = 0U; i < key; ++i) {
        at += keys[i].lead + keys[i].units;
    }
    at += keys[key].lead;
    out.x = box.x + tkb_px(box.w, at);
    out.w = box.x + tkb_px(box.w, at + keys[key].units) - out.x;
    out.y = tkb_row_y(box, row);
    out.h = tkb_row_y(box, row + 1U) - out.y;
    return out;
}

bool inkcell_touch_keyboard_hit(unsigned layer, struct inkcell_tkb_box box, int32_t x, int32_t y,
                                unsigned *row, unsigned *key) {
    if (layer >= INKCELL_TKB_LAYER_COUNT || box.w <= 0 || box.h <= 0 || x < box.x ||
        x >= box.x + box.w || y < box.y || y >= box.y + box.h) {
        return false;
    }
    unsigned r = (unsigned)(((int64_t)(y - box.y) * INKCELL_TKB_ROWS) / box.h);
    if (r >= INKCELL_TKB_ROWS) {
        r = INKCELL_TKB_ROWS - 1U;
    }
    size_t count = 0U;
    (void)inkcell_touch_keyboard_row(layer, r, &count);
    /* The nearest key by its middle: a point in the stagger, or between two keys, is the key it
       is closest to. */
    unsigned best = 0U;
    int64_t best_d = INT64_MAX;
    for (unsigned k = 0U; k < count; ++k) {
        const struct inkcell_tkb_box b = inkcell_touch_keyboard_key_box(layer, r, k, box);
        int64_t d = 0;
        if (x < b.x) {
            d = b.x - x;
        } else if (x >= b.x + b.w) {
            d = x - (b.x + b.w - 1);
        }
        if (d < best_d) {
            best_d = d;
            best = k;
        }
    }
    if (row != NULL) {
        *row = r;
    }
    if (key != NULL) {
        *key = best;
    }
    return count > 0U;
}

static const struct inkcell_tkb_key *tkb_lit(const struct inkcell_touch_keyboard *kb) {
    if (kb->row < 0 || kb->key < 0) {
        return NULL;
    }
    size_t count = 0U;
    const struct inkcell_tkb_key *keys =
        inkcell_touch_keyboard_row(kb->layer, (unsigned)kb->row, &count);
    return (keys != NULL && (size_t)kb->key < count) ? &keys[kb->key] : NULL;
}

/* A letter's capital: the letters here are ASCII, and every other key types itself. */
const char *inkcell_touch_keyboard_label(const struct inkcell_touch_keyboard *kb,
                                         const struct inkcell_tkb_key *k,
                                         char scratch[INKCELL_TKB_TEXT_MAX]) {
    if (k == NULL || k->text == NULL) {
        return "";
    }
    if (kb != NULL && k->kind == INKCELL_TKB_CHAR && kb->layer == INKCELL_TKB_LETTERS &&
        kb->shift != INKCELL_TKB_SHIFT_OFF && k->text[0] >= 'a' && k->text[0] <= 'z' &&
        k->text[1] == '\0') {
        scratch[0] = (char)(k->text[0] - 'a' + 'A');
        scratch[1] = '\0';
        return scratch;
    }
    return k->text;
}

static struct inkcell_tkb_result tkb_result(enum inkcell_tkb_result_kind kind) {
    struct inkcell_tkb_result r;
    memset(&r, 0, sizeof r);
    r.kind = kind;
    return r;
}

static bool tkb_light(struct inkcell_touch_keyboard *kb, struct inkcell_tkb_box box, int32_t x,
                      int32_t y) {
    unsigned row = 0U;
    unsigned key = 0U;
    int8_t r = -1;
    int8_t k = -1;
    if (inkcell_touch_keyboard_hit(kb->layer, box, x, y, &row, &key)) {
        r = (int8_t)row;
        k = (int8_t)key;
    }
    const bool changed = r != kb->row || k != kb->key;
    kb->row = r;
    kb->key = k;
    return changed;
}

/* The press the lit key makes, the finger having lifted on it. */
static struct inkcell_tkb_result tkb_press(struct inkcell_touch_keyboard *kb,
                                           const struct inkcell_tkb_key *k, uint64_t now_ms) {
    struct inkcell_tkb_result r = tkb_result(INKCELL_TKB_REDRAW);
    switch (k->kind) {
    case INKCELL_TKB_CHAR: {
        char scratch[INKCELL_TKB_TEXT_MAX];
        r.kind = INKCELL_TKB_TEXT;
        (void)snprintf(r.text, sizeof r.text, "%s", inkcell_touch_keyboard_label(kb, k, scratch));
        if (kb->shift == INKCELL_TKB_SHIFT_ONCE) {
            kb->shift = INKCELL_TKB_SHIFT_OFF;
        }
        kb->auto_shift = false;
        break;
    }
    case INKCELL_TKB_SHIFT:
        if (kb->shift == INKCELL_TKB_SHIFT_ONCE && !kb->auto_shift && kb->shift_ms != 0U &&
            now_ms - kb->shift_ms <= INKCELL_TKB_DOUBLE_MS) {
            kb->shift = INKCELL_TKB_SHIFT_LOCKED;
        } else if (kb->shift == INKCELL_TKB_SHIFT_OFF) {
            kb->shift = INKCELL_TKB_SHIFT_ONCE;
        } else {
            kb->shift = INKCELL_TKB_SHIFT_OFF;
        }
        kb->auto_shift = false;
        kb->shift_ms = now_ms;
        break;
    case INKCELL_TKB_LAYER:
        kb->layer = k->to;
        kb->shift = INKCELL_TKB_SHIFT_OFF;
        kb->auto_shift = false;
        break;
    case INKCELL_TKB_SPACE:
        r.kind = INKCELL_TKB_TEXT;
        r.text[0] = ' ';
        break;
    case INKCELL_TKB_ENTER:
        r.kind = INKCELL_TKB_SUBMIT;
        break;
    case INKCELL_TKB_HIDE:
        r.kind = INKCELL_TKB_AWAY;
        break;
    case INKCELL_TKB_DELETE:
    default:
        break; /* deleted going down */
    }
    return r;
}

struct inkcell_tkb_result inkcell_touch_keyboard_touch(struct inkcell_touch_keyboard *kb,
                                                       struct inkcell_tkb_box box,
                                                       enum inkcell_touch_phase phase, int32_t x,
                                                       int32_t y, uint64_t now_ms) {
    if (kb == NULL) {
        return tkb_result(INKCELL_TKB_NOTHING);
    }
    switch (phase) {
    case INKCELL_TOUCH_DOWN: {
        kb->down = true;
        (void)tkb_light(kb, box, x, y);
        const struct inkcell_tkb_key *k = tkb_lit(kb);
        if (k != NULL && k->kind == INKCELL_TKB_DELETE) {
            kb->due_ms = now_ms + INKCELL_TKB_REPEAT_DELAY_MS;
            return tkb_result(INKCELL_TKB_BACK);
        }
        return tkb_result(INKCELL_TKB_REDRAW);
    }
    case INKCELL_TOUCH_MOVE: {
        if (!kb->down) {
            return tkb_result(INKCELL_TKB_NOTHING);
        }
        /* A finger that has started deleting stays deleting: sliding off the key is the end of
           the repeat, not a slide onto a letter. */
        const struct inkcell_tkb_key *was = tkb_lit(kb);
        if (was != NULL && was->kind == INKCELL_TKB_DELETE) {
            if (tkb_light(kb, box, x, y)) {
                kb->down = false; /* the rest of this contact is nothing's */
                kb->due_ms = 0U;
                kb->row = -1;
                kb->key = -1;
                return tkb_result(INKCELL_TKB_REDRAW);
            }
            return tkb_result(INKCELL_TKB_NOTHING);
        }
        return tkb_result(tkb_light(kb, box, x, y) ? INKCELL_TKB_REDRAW : INKCELL_TKB_NOTHING);
    }
    case INKCELL_TOUCH_UP: {
        if (!kb->down) {
            return tkb_result(INKCELL_TKB_NOTHING);
        }
        (void)tkb_light(kb, box, x, y);
        const struct inkcell_tkb_key *k = tkb_lit(kb);
        const bool deleting = k != NULL && k->kind == INKCELL_TKB_DELETE;
        kb->down = false;
        kb->due_ms = 0U;
        kb->row = -1;
        kb->key = -1;
        if (k == NULL || deleting) { /* a delete was done going down */
            return tkb_result(INKCELL_TKB_REDRAW);
        }
        return tkb_press(kb, k, now_ms);
    }
    case INKCELL_TOUCH_CANCEL:
    default:
        kb->down = false;
        kb->due_ms = 0U;
        kb->row = -1;
        kb->key = -1;
        return tkb_result(INKCELL_TKB_REDRAW);
    }
}

uint64_t inkcell_touch_keyboard_due(const struct inkcell_touch_keyboard *kb) {
    return kb != NULL ? kb->due_ms : 0U;
}

struct inkcell_tkb_result inkcell_touch_keyboard_tick(struct inkcell_touch_keyboard *kb,
                                                      uint64_t now_ms) {
    if (kb == NULL || kb->due_ms == 0U || now_ms < kb->due_ms) {
        return tkb_result(INKCELL_TKB_NOTHING);
    }
    kb->due_ms = now_ms + INKCELL_TKB_REPEAT_MS;
    return tkb_result(INKCELL_TKB_BACK);
}

bool inkcell_touch_keyboard_capitalize(struct inkcell_touch_keyboard *kb, bool on) {
    if (kb == NULL || kb->layer != INKCELL_TKB_LETTERS) {
        return false;
    }
    if (on && kb->shift == INKCELL_TKB_SHIFT_OFF) {
        kb->shift = INKCELL_TKB_SHIFT_ONCE;
        kb->auto_shift = true;
        return true;
    }
    if (!on && kb->auto_shift && kb->shift == INKCELL_TKB_SHIFT_ONCE) {
        kb->shift = INKCELL_TKB_SHIFT_OFF;
        kb->auto_shift = false;
        return true;
    }
    return false;
}
