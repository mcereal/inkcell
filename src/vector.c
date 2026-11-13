/*
 * Vector images: inkvec parsed into outlines, and outlines rasterised into coverage.
 * See include/inkcell/ui/vector.h for the format and why it is this one.
 *
 * The rasteriser is the scanline kind every font renderer has: an outline flattened into line
 * edges, each pixel row cut by VEC_SUB scanlines, the crossings on each scanline walked left to
 * right under the fill rule, and the spans that come out added to the row with exact horizontal
 * coverage at both ends. VEC_SUB scanlines give VEC_SUB+1 levels of vertical coverage and the
 * horizontal is exact, which at an icon's size is indistinguishable from an analytic coverage -
 * and every step is integer, so the same picture is the same pixels on every machine.
 */
#include "inkcell/ui/vector.h"

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define VEC_FX 8 /* fixed point: 1/256 of a unit or a pixel */
#define VEC_ONE (1 << VEC_FX)
#define VEC_SUB 16 /* scanlines a pixel row */
/* View-box coordinates are held to this, in units, so every product in the gradient arithmetic
   stays inside 64 bits. Generous: icons are drawn on boxes of 24 to 1024. */
#define VEC_COORD_MAX 8192
#define VEC_STOPS_MAX 8
#define VEC_CURVE_SEGMENTS_MIN 4
#define VEC_CURVE_SEGMENTS_MAX 64

enum vec_op_kind { VEC_MOVE, VEC_LINE, VEC_QUAD, VEC_CUBIC, VEC_CLOSE };

struct vec_op {
    uint8_t kind;
    int32_t pt[6]; /* view-box fixed point: the control points then the end */
};

struct vec_stop {
    int32_t offset; /* 0..VEC_ONE */
    uint8_t r, g, b, a;
};

struct vec_layer {
    bool linear;
    bool evenodd;
    struct vec_stop stops[VEC_STOPS_MAX]; /* a solid fill is one stop */
    size_t stop_count;
    int32_t gx0, gy0, gx1, gy1; /* the gradient's axis, view-box fixed point */
    size_t first_op, op_count;
};

struct inkcell_vector {
    int32_t width, height; /* the view box, in fixed point */
    struct vec_layer *layers;
    size_t layer_count, layer_cap;
    struct vec_op *ops;
    size_t op_count, op_cap;
};

/* ---- parsing ------------------------------------------------------------------------------ */

struct vec_cursor {
    const char *p, *end;
};

static void skip_space(struct vec_cursor *c) {
    while (c->p < c->end && (*c->p == ' ' || *c->p == '\t' || *c->p == ',' || *c->p == '\r')) {
        ++c->p;
    }
}

static bool at_line_end(struct vec_cursor *c) {
    skip_space(c);
    return c->p >= c->end || *c->p == '\n';
}

/* The next word, as a pointer and a length; false at the end of the line. */
static bool word(struct vec_cursor *c, const char **w, size_t *n) {
    if (at_line_end(c)) {
        return false;
    }
    *w = c->p;
    while (c->p < c->end && *c->p != ' ' && *c->p != '\t' && *c->p != ',' && *c->p != '\n' &&
           *c->p != '\r') {
        ++c->p;
    }
    *n = (size_t)(c->p - *w);
    return true;
}

static bool word_is(const char *w, size_t n, const char *s) {
    return strlen(s) == n && memcmp(w, s, n) == 0;
}

/* A decimal - sign, digits, a point, digits - to fixed point, rounded; within VEC_COORD_MAX. */
static bool number(struct vec_cursor *c, int32_t *out) {
    const char *w;
    size_t n;
    if (!word(c, &w, &n)) {
        return false;
    }
    size_t i = 0U;
    bool negative = false;
    if (i < n && (w[i] == '-' || w[i] == '+')) {
        negative = w[i] == '-';
        ++i;
    }
    int64_t whole = 0;
    bool digits = false;
    for (; i < n && w[i] >= '0' && w[i] <= '9'; ++i) {
        whole = whole * 10 + (w[i] - '0');
        digits = true;
        if (whole > VEC_COORD_MAX) {
            return false;
        }
    }
    int64_t frac = 0;
    int64_t scale = 1;
    if (i < n && w[i] == '.') {
        for (++i; i < n && w[i] >= '0' && w[i] <= '9'; ++i) {
            if (scale < 1000000) {
                frac = frac * 10 + (w[i] - '0');
                scale *= 10;
            }
            digits = true;
        }
    }
    if (!digits || i != n) {
        return false;
    }
    const int64_t fx = whole * VEC_ONE + (frac * VEC_ONE + scale / 2) / scale;
    *out = (int32_t)(negative ? -fx : fx);
    return true;
}

static int hex_digit(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

/* #rrggbb or #rrggbbaa. */
static bool color(struct vec_cursor *c, struct vec_stop *stop) {
    const char *w;
    size_t n;
    if (!word(c, &w, &n) || (n != 7U && n != 9U) || w[0] != '#') {
        return false;
    }
    uint8_t channel[4] = {0U, 0U, 0U, 0xFFU};
    for (size_t k = 0U; k < (n - 1U) / 2U; ++k) {
        const int hi = hex_digit(w[1U + 2U * k]);
        const int lo = hex_digit(w[2U + 2U * k]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        channel[k] = (uint8_t)(hi * 16 + lo);
    }
    stop->r = channel[0];
    stop->g = channel[1];
    stop->b = channel[2];
    stop->a = channel[3];
    return true;
}

static int push_op(struct inkcell_vector *v, const struct vec_op *op) {
    if (v->op_count == v->op_cap) {
        const size_t cap = v->op_cap != 0U ? v->op_cap * 2U : 64U;
        struct vec_op *ops = realloc(v->ops, cap * sizeof *ops);
        if (ops == NULL) {
            return -ENOMEM;
        }
        v->ops = ops;
        v->op_cap = cap;
    }
    v->ops[v->op_count++] = *op;
    return 0;
}

/* The path that ends a paint line: absolute M L Q C Z. */
static int path(struct vec_cursor *c, struct inkcell_vector *v, struct vec_layer *layer) {
    layer->first_op = v->op_count;
    const char *w;
    size_t n;
    bool moved = false;
    while (word(c, &w, &n)) {
        struct vec_op op = {0};
        int points = 0;
        if (word_is(w, n, "M")) {
            op.kind = VEC_MOVE;
            points = 1;
        } else if (word_is(w, n, "L")) {
            op.kind = VEC_LINE;
            points = 1;
        } else if (word_is(w, n, "Q")) {
            op.kind = VEC_QUAD;
            points = 2;
        } else if (word_is(w, n, "C")) {
            op.kind = VEC_CUBIC;
            points = 3;
        } else if (word_is(w, n, "Z")) {
            op.kind = VEC_CLOSE;
        } else {
            return -EINVAL;
        }
        if (op.kind != VEC_MOVE && !moved) {
            return -EINVAL; /* a path starts somewhere */
        }
        moved = true;
        for (int k = 0; k < 2 * points; ++k) {
            if (!number(c, &op.pt[k])) {
                return -EINVAL;
            }
        }
        const int pushed = push_op(v, &op);
        if (pushed < 0) {
            return pushed;
        }
    }
    layer->op_count = v->op_count - layer->first_op;
    return layer->op_count > 0U ? 0 : -EINVAL;
}

static int paint_line(struct vec_cursor *c, struct inkcell_vector *v, bool linear) {
    struct vec_layer layer = {.linear = linear};
    if (linear) {
        if (!number(c, &layer.gx0) || !number(c, &layer.gy0) || !number(c, &layer.gx1) ||
            !number(c, &layer.gy1)) {
            return -EINVAL;
        }
        /* OFFSET COLOR pairs, until the path or the fill rule begins. */
        for (;;) {
            skip_space(c);
            if (c->p >= c->end || !((*c->p >= '0' && *c->p <= '9') || *c->p == '.')) {
                break;
            }
            if (layer.stop_count == VEC_STOPS_MAX) {
                return -EINVAL;
            }
            struct vec_stop *stop = &layer.stops[layer.stop_count];
            if (!number(c, &stop->offset) || stop->offset > VEC_ONE || !color(c, stop) ||
                (layer.stop_count > 0U &&
                 stop->offset < layer.stops[layer.stop_count - 1U].offset)) {
                return -EINVAL;
            }
            ++layer.stop_count;
        }
        if (layer.stop_count == 0U) {
            return -EINVAL;
        }
    } else {
        if (!color(c, &layer.stops[0])) {
            return -EINVAL;
        }
        layer.stop_count = 1U;
    }
    skip_space(c);
    if (c->end - c->p >= 7 && memcmp(c->p, "evenodd", 7U) == 0) {
        layer.evenodd = true;
        c->p += 7;
    }
    const int parsed = path(c, v, &layer);
    if (parsed < 0) {
        return parsed;
    }
    if (v->layer_count == v->layer_cap) {
        const size_t cap = v->layer_cap != 0U ? v->layer_cap * 2U : 8U;
        struct vec_layer *layers = realloc(v->layers, cap * sizeof *layers);
        if (layers == NULL) {
            return -ENOMEM;
        }
        v->layers = layers;
        v->layer_cap = cap;
    }
    v->layers[v->layer_count++] = layer;
    return 0;
}

int inkcell_vector_parse(const char *text, size_t size, struct inkcell_vector **out) {
    if (out == NULL) {
        return -EINVAL;
    }
    *out = NULL;
    if (text == NULL) {
        return -EINVAL;
    }
    struct inkcell_vector *v = calloc(1U, sizeof *v);
    if (v == NULL) {
        return -ENOMEM;
    }
    struct vec_cursor c = {.p = text, .end = text + size};
    bool header = false;
    int rc = 0;
    while (rc == 0 && c.p < c.end) {
        const char *w;
        size_t n;
        if (!word(&c, &w, &n) || w[0] == ';') {
            /* a blank line or a comment */
        } else if (!header) {
            int32_t version = 0;
            header = word_is(w, n, "inkvec") && number(&c, &version) && version == (1 << VEC_FX) &&
                     number(&c, &v->width) && number(&c, &v->height) && v->width >= VEC_ONE &&
                     v->height >= VEC_ONE && at_line_end(&c);
            rc = header ? 0 : -EINVAL;
        } else if (word_is(w, n, "fill")) {
            rc = paint_line(&c, v, false);
        } else if (word_is(w, n, "linear")) {
            rc = paint_line(&c, v, true);
        } else {
            rc = -EINVAL; /* a later version's word: refused, not skipped */
        }
        while (c.p < c.end && *c.p != '\n') {
            ++c.p;
        }
        if (c.p < c.end) {
            ++c.p;
        }
    }
    if (rc == 0 && !header) {
        rc = -EINVAL;
    }
    if (rc < 0) {
        inkcell_vector_free(v);
        return rc;
    }
    *out = v;
    return 0;
}

void inkcell_vector_free(struct inkcell_vector *vector) {
    if (vector != NULL) {
        free(vector->layers);
        free(vector->ops);
        free(vector);
    }
}

void inkcell_vector_view_box(const struct inkcell_vector *vector, int *width, int *height) {
    *width = vector != NULL ? vector->width >> VEC_FX : 0;
    *height = vector != NULL ? vector->height >> VEC_FX : 0;
}

/* ---- rasterising -------------------------------------------------------------------------- */

struct vec_edge {
    int32_t x0, y0, x1, y1; /* pixel fixed point, y0 < y1 */
    int dir;                /* +1 drawn downwards, -1 upwards */
};

struct vec_raster {
    const struct inkcell_vector *v;
    int width, height;
    struct vec_edge *edges;
    size_t edge_count, edge_cap;
    bool failed;
};

static int32_t to_px(int32_t vb, int32_t box, int size) {
    return (int32_t)((int64_t)vb * size / (box >> VEC_FX));
}

static void add_edge(struct vec_raster *r, int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    if (y0 == y1 || r->failed) {
        return; /* a horizontal edge crosses no scanline */
    }
    if (r->edge_count == r->edge_cap) {
        const size_t cap = r->edge_cap != 0U ? r->edge_cap * 2U : 256U;
        struct vec_edge *edges = realloc(r->edges, cap * sizeof *edges);
        if (edges == NULL) {
            r->failed = true;
            return;
        }
        r->edges = edges;
        r->edge_cap = cap;
    }
    r->edges[r->edge_count++] =
        y0 < y1 ? (struct vec_edge){x0, y0, x1, y1, 1} : (struct vec_edge){x1, y1, x0, y0, -1};
}

static int32_t span_px(const int32_t *p, int count) {
    int64_t length = 0;
    for (int k = 1; k < count; ++k) {
        const int64_t dx = p[2 * k] - p[2 * k - 2];
        const int64_t dy = p[2 * k + 1] - p[2 * k - 1];
        length += (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
    }
    return (int32_t)(length >> VEC_FX);
}

/* A Bezier of `count` points (3 or 4), as straight segments enough that none strays a visible
   fraction of a pixel: about one per four pixels of the control polygon. */
static void add_curve(struct vec_raster *r, const int32_t *p, int count) {
    int segments = span_px(p, count) / 4;
    if (segments < VEC_CURVE_SEGMENTS_MIN) {
        segments = VEC_CURVE_SEGMENTS_MIN;
    } else if (segments > VEC_CURVE_SEGMENTS_MAX) {
        segments = VEC_CURVE_SEGMENTS_MAX;
    }
    const int64_t n = segments;
    int32_t px = p[0], py = p[1];
    for (int64_t i = 1; i <= n; ++i) {
        const int64_t u = n - i;
        int64_t x, y;
        if (count == 3) {
            const int64_t d = n * n;
            x = (u * u * p[0] + 2 * u * i * p[2] + i * i * p[4]) / d;
            y = (u * u * p[1] + 2 * u * i * p[3] + i * i * p[5]) / d;
        } else {
            const int64_t d = n * n * n;
            x = (u * u * u * p[0] + 3 * u * u * i * p[2] + 3 * u * i * i * p[4] +
                 i * i * i * p[6]) /
                d;
            y = (u * u * u * p[1] + 3 * u * u * i * p[3] + 3 * u * i * i * p[5] +
                 i * i * i * p[7]) /
                d;
        }
        add_edge(r, px, py, (int32_t)x, (int32_t)y);
        px = (int32_t)x;
        py = (int32_t)y;
    }
}

/* One layer's outline as edges, in pixels. Every subpath is closed, as a fill closes it. */
static void flatten(struct vec_raster *r, const struct vec_layer *layer) {
    r->edge_count = 0U;
    int32_t sx = 0, sy = 0, cx = 0, cy = 0;
    for (size_t k = 0U; k < layer->op_count; ++k) {
        const struct vec_op *op = &r->v->ops[layer->first_op + k];
        int32_t pt[8] = {cx, cy};
        for (int j = 0; j < 6; ++j) {
            pt[2 + j] = to_px(op->pt[j], (j % 2 == 0) ? r->v->width : r->v->height,
                              (j % 2 == 0) ? r->width : r->height);
        }
        switch (op->kind) {
        case VEC_MOVE:
            add_edge(r, cx, cy, sx, sy);
            sx = cx = pt[2];
            sy = cy = pt[3];
            break;
        case VEC_LINE:
            add_edge(r, cx, cy, pt[2], pt[3]);
            cx = pt[2];
            cy = pt[3];
            break;
        case VEC_QUAD:
            add_curve(r, pt, 3);
            cx = pt[4];
            cy = pt[5];
            break;
        case VEC_CUBIC:
            add_curve(r, pt, 4);
            cx = pt[6];
            cy = pt[7];
            break;
        default: /* VEC_CLOSE */
            add_edge(r, cx, cy, sx, sy);
            cx = sx;
            cy = sy;
            break;
        }
    }
    add_edge(r, cx, cy, sx, sy);
}

/* The colour at a pixel's centre: a gradient's position projected onto its axis. */
static struct vec_stop paint_at(const struct vec_raster *r, const struct vec_layer *layer, int x,
                                int y) {
    if (!layer->linear || layer->stop_count == 1U) {
        return layer->stops[0];
    }
    /* The centre in view-box fixed point. */
    const int64_t px = ((int64_t)x * VEC_ONE + VEC_ONE / 2) * (r->v->width >> VEC_FX) / r->width;
    const int64_t py = ((int64_t)y * VEC_ONE + VEC_ONE / 2) * (r->v->height >> VEC_FX) / r->height;
    const int64_t dx = layer->gx1 - layer->gx0;
    const int64_t dy = layer->gy1 - layer->gy0;
    const int64_t dd = dx * dx + dy * dy;
    int64_t t = 0;
    if (dd > 0) {
        t = ((px - layer->gx0) * dx + (py - layer->gy0) * dy) * VEC_ONE / dd;
    }
    const struct vec_stop *first = &layer->stops[0];
    const struct vec_stop *last = &layer->stops[layer->stop_count - 1U];
    if (t <= first->offset) {
        return *first;
    }
    if (t >= last->offset) {
        return *last;
    }
    size_t i = 1U;
    while (layer->stops[i].offset < t) {
        ++i;
    }
    const struct vec_stop *a = &layer->stops[i - 1U];
    const struct vec_stop *b = &layer->stops[i];
    const int64_t span = b->offset - a->offset;
    const int64_t f = span > 0 ? (t - a->offset) * 256 / span : 256;
#define VEC_MIX(ch) (uint8_t)((a->ch * (256 - f) + b->ch * f + 128) / 256)
    return (struct vec_stop){.r = VEC_MIX(r), .g = VEC_MIX(g), .b = VEC_MIX(b), .a = VEC_MIX(a)};
#undef VEC_MIX
}

/* Adds the span [xa, xb), in pixel fixed point, to a row: whole pixels a full step, the two ends
   by how much of them it covers. */
static void add_span(int32_t *acc, int width, int32_t xa, int32_t xb, int *lo, int *hi) {
    if (xa < 0) {
        xa = 0;
    }
    if (xb > width * VEC_ONE) {
        xb = width * VEC_ONE;
    }
    if (xa >= xb) {
        return;
    }
    const int pa = xa >> VEC_FX;
    const int pb = xb >> VEC_FX;
    if (pa == pb) {
        acc[pa] += xb - xa;
    } else {
        acc[pa] += VEC_ONE - (xa & (VEC_ONE - 1));
        for (int p = pa + 1; p < pb; ++p) {
            acc[p] += VEC_ONE;
        }
        if (pb < width) {
            acc[pb] += xb & (VEC_ONE - 1);
        }
    }
    if (pa < *lo) {
        *lo = pa;
    }
    if (pb > *hi) {
        *hi = pb < width ? pb : width - 1;
    }
}

struct vec_crossing {
    int32_t x;
    int dir;
};

static void fill_layer(struct vec_raster *r, const struct vec_layer *layer, int32_t *acc,
                       struct vec_crossing *cross, inkcell_vector_emit emit, void *ctx) {
    int32_t top = INT32_MAX, bottom = INT32_MIN;
    for (size_t e = 0U; e < r->edge_count; ++e) {
        top = r->edges[e].y0 < top ? r->edges[e].y0 : top;
        bottom = r->edges[e].y1 > bottom ? r->edges[e].y1 : bottom;
    }
    int first = top >> VEC_FX;
    int last = bottom >> VEC_FX;
    first = first < 0 ? 0 : first;
    last = last >= r->height ? r->height - 1 : last;
    for (int y = first; y <= last; ++y) {
        int lo = r->width, hi = -1;
        for (int s = 0; s < VEC_SUB; ++s) {
            const int32_t ys = (int32_t)(y * VEC_ONE + (2 * s + 1) * VEC_ONE / (2 * VEC_SUB));
            size_t count = 0U;
            for (size_t e = 0U; e < r->edge_count; ++e) {
                const struct vec_edge *edge = &r->edges[e];
                if (ys < edge->y0 || ys >= edge->y1) {
                    continue;
                }
                const int64_t x = edge->x0 + ((int64_t)(ys - edge->y0) * (edge->x1 - edge->x0)) /
                                                 (edge->y1 - edge->y0);
                /* Insertion into order: a scanline crosses a handful of edges. */
                size_t at = count++;
                while (at > 0U && cross[at - 1U].x > x) {
                    cross[at] = cross[at - 1U];
                    --at;
                }
                cross[at] = (struct vec_crossing){(int32_t)x, edge->dir};
            }
            int winding = 0;
            int32_t start = 0;
            for (size_t k = 0U; k < count; ++k) {
                const bool was = layer->evenodd ? (winding & 1) != 0 : winding != 0;
                winding += cross[k].dir;
                const bool is = layer->evenodd ? (winding & 1) != 0 : winding != 0;
                if (!was && is) {
                    start = cross[k].x;
                } else if (was && !is) {
                    add_span(acc, r->width, start, cross[k].x, &lo, &hi);
                }
            }
        }
        for (int x = lo; x <= hi; ++x) {
            const int32_t sum = acc[x];
            acc[x] = 0;
            if (sum <= 0) {
                continue;
            }
            const struct vec_stop paint = paint_at(r, layer, x, y);
            int coverage = (sum + VEC_SUB / 2) / VEC_SUB;
            coverage = (coverage * paint.a + 127) / 255;
            if (coverage > 0) {
                emit(ctx, x, y, (struct inkcell_rgb){paint.r, paint.g, paint.b},
                     coverage > INKCELL_VECTOR_COVERAGE_ONE ? INKCELL_VECTOR_COVERAGE_ONE
                                                            : coverage);
            }
        }
    }
}

int inkcell_vector_render(const struct inkcell_vector *vector, int width, int height,
                          inkcell_vector_emit emit, void *ctx) {
    if (vector == NULL || emit == NULL || width < 1 || height < 1 ||
        width > INKCELL_VECTOR_MAX_PX || height > INKCELL_VECTOR_MAX_PX) {
        return -EINVAL;
    }
    struct vec_raster r = {.v = vector, .width = width, .height = height};
    int32_t *acc = calloc((size_t)width + 1U, sizeof *acc);
    struct vec_crossing *cross = NULL;
    int rc = acc != NULL ? 0 : -ENOMEM;
    for (size_t l = 0U; rc == 0 && l < vector->layer_count; ++l) {
        flatten(&r, &vector->layers[l]);
        if (r.failed) {
            rc = -ENOMEM;
            break;
        }
        free(cross);
        cross = malloc((r.edge_count + 1U) * sizeof *cross);
        if (cross == NULL) {
            rc = -ENOMEM;
            break;
        }
        fill_layer(&r, &vector->layers[l], acc, cross, emit, ctx);
    }
    free(cross);
    free(r.edges);
    free(acc);
    return rc;
}
