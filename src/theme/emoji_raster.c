#include "emoji_internal.h"
#include "inkcell/ui/emoji.h"

#include <errno.h>
#include <string.h>

/*
 * Outlines to pixels: the coverage rasteriser under inkcell_emoji_render().
 *
 * The method is the one font rasterisers settled on (libart, FreeType's "gray", font-rs): walk
 * every edge of an outline once and, for each pixel it passes through, record how much of the
 * pixel lies to the edge's right - signed by which way the edge runs. A running sum along a row
 * then turns those records into how much of each pixel is inside the shape. Nothing is
 * sampled, so there is no grain of samples to see: a pixel an edge crosses a third of the way
 * along is a third covered, exactly.
 *
 * Integer throughout. A coordinate is in 1/256 px, and a record is the edge's height in the
 * pixel times twice its average distance from the pixel's right side, so every record is
 * exact and one row's records sum to exactly what the edge's height says. That exactness is the
 * whole of why no row can leak coverage across the line - the classic artefact of doing this in
 * floats is a streak to the panel's edge where rounding left one row a hair short.
 *
 * Overlapping shapes sum and are then clamped to one pixel's worth, which is the nonzero
 * winding rule's answer for shapes drawn the same way round and zero for a hole drawn the other
 * way: what an SVG's default fill asks for, and what the generator normalises every emoji to.
 */

/* Subpixel units per pixel, and what a whole covered pixel sums to: height 256 times twice the
   width 256, the factor of two being the average of the two ends taken without a division. */
#define FX 256
#define FULL (2 * FX * FX)

/* Most a flattened curve may stray from the true one: a tenth of a pixel, in FX. */
#define FLATNESS 26

/* The most lines one quadratic becomes. The flatness bound asks for about twenty across a
   whole 256-pixel emoji; this is only a ceiling on a malformed pack. */
#define MAX_STEPS 64

/*
 * The accumulator: one band of rows, each a pixel wider on the right than the box (plus one to
 * spare) so that a record at the box's right edge has somewhere to land that is never read.
 * Static because this is drawn from one thread and 33 KB is too much for some stacks.
 */
#define ACC_W (INKCELL_EMOJI_RENDER_MAX + 2)
static int32_t g_acc[INKCELL_EMOJI_BAND][ACC_W];

struct raster {
    int width;      /* pixels across */
    int rows;       /* pixels down, in this band */
    int top_row;    /* first row touched since the last composite, or rows when none */
    int bottom_row; /* last row touched, or -1 */
};

/* Floor division for a positive divisor. C's truncates toward zero, which would round a
   coordinate left of the box differently from one right of it. */
static int64_t floor_div(int64_t a, int64_t b) {
    const int64_t q = a / b;
    return (a % b != 0 && a < 0) ? q - 1 : q;
}

/* Record a piece of edge that lies inside one pixel column `cell` of `row`, from x fraction `fa`
   to `fb` (0..FX, the pixel's left to its right) over signed height `dy`. */
static void raster_piece(struct raster *r, int row, int cell, int32_t fa, int32_t fb, int32_t dy) {
    int32_t *acc = g_acc[row];
    acc[cell] += dy * (2 * FX - fa - fb);
    acc[cell + 1] += dy * (fa + fb);
    if (row < r->top_row) {
        r->top_row = row;
    }
    if (row > r->bottom_row) {
        r->bottom_row = row;
    }
}

/* x on the edge (x0, y0)-(x1, y1) at height y, rounded down - always the same way for the same
   y, so the two rows either side of a boundary agree on where the edge crosses it. */
static int32_t raster_x_at(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t y) {
    return (int32_t)(x0 + floor_div((int64_t)(x1 - x0) * (y - y0), (int64_t)(y1 - y0)));
}

/* y on the same edge at x, for splitting a row's piece at pixel boundaries. */
static int32_t raster_y_at(int32_t xa, int32_t ya, int32_t xb, int32_t yb, int32_t x) {
    return (int32_t)(ya + floor_div((int64_t)(yb - ya) * (x - xa), (int64_t)(xb - xa)));
}

/*
 * One row's share of an edge, from (xa, ya) to (xb, yb), both inside [row, row + 1) in y and
 * already clamped to [0, width] in x, with `sign` the edge's direction. Split at every pixel
 * boundary it crosses; each piece is recorded in the one column it lies in.
 */
static void raster_row(struct raster *r, int row, int32_t xa, int32_t ya, int32_t xb, int32_t yb,
                       int32_t sign) {
    const int32_t low = xa < xb ? xa : xb;
    const int32_t high = xa < xb ? xb : xa;
    int cell = (int)(low / FX);
    if (high <= (cell + 1) * FX) {
        if (cell >= r->width) {
            cell = r->width; /* on the right edge: lands in the column never read */
        }
        raster_piece(r, row, cell, xa - cell * FX, xb - cell * FX, sign * (yb - ya));
        return;
    }

    /* Walk from xa toward xb, one boundary at a time. */
    const int step = xb > xa ? 1 : -1;
    int32_t px = xa;
    int32_t py = ya;
    cell = (int)(xa / FX);
    if (step < 0 && xa == cell * FX) {
        cell -= 1; /* starting on a boundary going left: the first piece is in the column left */
    }
    for (;;) {
        const int32_t boundary = step > 0 ? (cell + 1) * FX : cell * FX;
        const bool last = step > 0 ? xb <= boundary : xb >= boundary;
        const int32_t nx = last ? xb : boundary;
        const int32_t ny = last ? yb : raster_y_at(xa, ya, xb, yb, boundary);
        raster_piece(r, row, cell, px - cell * FX, nx - cell * FX, sign * (ny - py));
        if (last) {
            return;
        }
        px = nx;
        py = ny;
        cell += step;
    }
}

/*
 * A row's piece of an edge, before clamping to the box. Whatever lies left of the box records
 * as if it ran down the box's left side - it covers everything to its right, which is all of
 * the row that is drawn - and whatever lies right of it records in the column past the edge,
 * which is never read. Splitting at those two boundaries first is what keeps an edge far off to
 * one side from being walked a pixel at a time across nothing.
 */
static void raster_row_clipped(struct raster *r, int row, int32_t xa, int32_t ya, int32_t xb,
                               int32_t yb, int32_t sign) {
    const int32_t right = r->width * FX;
    const int32_t bounds[2] = {0, right};
    int32_t px = xa;
    int32_t py = ya;
    /* Visit the boundaries in the order the piece crosses them. */
    for (int k = 0; k < 2; ++k) {
        const int32_t b = xb >= xa ? bounds[k] : bounds[1 - k];
        if ((px < b && xb > b) || (px > b && xb < b)) {
            const int32_t by = raster_y_at(xa, ya, xb, yb, b);
            const int32_t cx0 = px < 0 ? 0 : (px > right ? right : px);
            const int32_t cx1 = b;
            raster_row(r, row, cx0, py, cx1, by, sign);
            px = b;
            py = by;
        }
    }
    const int32_t cx0 = px < 0 ? 0 : (px > right ? right : px);
    const int32_t cx1 = xb < 0 ? 0 : (xb > right ? right : xb);
    raster_row(r, row, cx0, py, cx1, yb, sign);
}

/* One straight edge, in FX coordinates relative to the band's top-left. */
static void raster_line(struct raster *r, int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    if (y0 == y1) {
        return; /* horizontal: covers nothing a running sum along the row would see */
    }
    const int32_t sign = y1 > y0 ? 1 : -1;
    if (y0 > y1) {
        int32_t t = x0;
        x0 = x1;
        x1 = t;
        t = y0;
        y0 = y1;
        y1 = t;
    }
    const int32_t band_bottom = r->rows * FX;
    if (y1 <= 0 || y0 >= band_bottom) {
        return;
    }
    const int32_t top = y0 < 0 ? 0 : y0;
    const int32_t bottom = y1 > band_bottom ? band_bottom : y1;
    for (int row = top / FX; row * FX < bottom; ++row) {
        const int32_t ya = row * FX > top ? row * FX : top;
        const int32_t yb = (row + 1) * FX < bottom ? (row + 1) * FX : bottom;
        if (ya >= yb) {
            continue;
        }
        raster_row_clipped(r, row, raster_x_at(x0, y0, x1, y1, ya), ya,
                           raster_x_at(x0, y0, x1, y1, yb), yb, sign);
    }
}

/* Integer square root, rounded up: how many lines a curve needs is ceil(sqrt(deviation / k)). */
static int32_t isqrt_ceil(int64_t v) {
    int32_t root = 0;
    while ((int64_t)root * root < v && root < MAX_STEPS) {
        ++root;
    }
    return root;
}

static int64_t abs64(int64_t v) {
    return v < 0 ? -v : v;
}

/*
 * A quadratic from (x0, y0) through control (cx, cy) to (x2, y2), as straight lines.
 *
 * Flattened into n equal steps of the parameter, n from the curve's deviation: a quadratic
 * strays from a chord over a step of 1/n by at most |p0 - 2c + p2| / (4 n^2), so n is chosen to
 * keep that under FLATNESS. Every point is worked out from the ends and the control directly
 * rather than by stepping, so no error builds up along the curve.
 */
static void raster_quad(struct raster *r, int32_t x0, int32_t y0, int32_t cx, int32_t cy,
                        int32_t x2, int32_t y2) {
    const int64_t ddx = abs64((int64_t)x0 - 2 * (int64_t)cx + x2);
    const int64_t ddy = abs64((int64_t)y0 - 2 * (int64_t)cy + y2);
    const int64_t deviation = ddx > ddy ? ddx : ddy;
    int32_t n = isqrt_ceil((deviation + 4 * FLATNESS - 1) / (4 * FLATNESS));
    if (n < 1) {
        n = 1;
    }
    const int64_t nn = (int64_t)n * n;
    int32_t px = x0;
    int32_t py = y0;
    for (int32_t i = 1; i <= n; ++i) {
        const int64_t a = (int64_t)(n - i) * (n - i);
        const int64_t b = 2 * (int64_t)i * (n - i);
        const int64_t c = (int64_t)i * i;
        const int32_t qx = (int32_t)floor_div(a * x0 + b * cx + c * x2 + nn / 2, nn);
        const int32_t qy = (int32_t)floor_div(a * y0 + b * cy + c * y2 + nn / 2, nn);
        raster_line(r, px, py, qx, qy);
        px = qx;
        py = qy;
    }
}

/* Reads a varint, or fails at the end of the path's bytes. */
static bool read_varint(const uint8_t **at, const uint8_t *end, uint32_t *out) {
    uint32_t value = 0;
    for (unsigned shift = 0; shift < 35U; shift += 7U) {
        if (*at >= end) {
            return false;
        }
        const uint8_t byte = *(*at)++;
        value |= (uint32_t)(byte & 0x7FU) << shift;
        if ((byte & 0x80U) == 0U) {
            *out = value;
            return true;
        }
    }
    return false;
}

static int32_t unzigzag(uint32_t v) {
    return (v & 1U) ? -(int32_t)(v >> 1) - 1 : (int32_t)(v >> 1);
}

/* Where a pack coordinate lands, in FX relative to the band: scaled from the grid to the box
   and rounded once, here, so that a point shared by two edges is one point to both. */
struct raster_map {
    int64_t scale; /* box * FX */
    int64_t grid;
    int32_t dy; /* the band's top, in FX */
};

static int32_t map_coord(const struct raster_map *m, int32_t v) {
    return (int32_t)floor_div((int64_t)v * m->scale + m->grid / 2, m->grid);
}

/*
 * Walk one path - its contours, TrueType's on- and off-curve points - as edges.
 *
 * Two off-curve points in a row imply an on-curve one halfway between them, which is how a run
 * of curves is stored with one point per curve rather than two. A contour closes back to its
 * first point, which the generator guarantees is on the curve.
 */
static bool raster_path(struct raster *r, const uint8_t *at, const uint8_t *end,
                        const struct raster_map *m) {
    uint32_t contours;
    if (!read_varint(&at, end, &contours)) {
        return false;
    }
    int32_t ux = 0;
    int32_t uy = 0;
    for (uint32_t k = 0; k < contours; ++k) {
        uint32_t points;
        if (!read_varint(&at, end, &points) || points == 0U) {
            return false;
        }
        const uint8_t *flags = at;
        const size_t flag_bytes = (points + 7U) / 8U;
        if ((size_t)(end - at) < flag_bytes) {
            return false;
        }
        at += flag_bytes;

        int32_t first_x = 0;
        int32_t first_y = 0;
        int32_t cur_x = 0;
        int32_t cur_y = 0;
        int32_t ctl_x = 0;
        int32_t ctl_y = 0;
        bool pending = false;
        for (uint32_t i = 0; i <= points; ++i) {
            int32_t x;
            int32_t y;
            bool on;
            if (i < points) {
                uint32_t zx;
                uint32_t zy;
                if (!read_varint(&at, end, &zx) || !read_varint(&at, end, &zy)) {
                    return false;
                }
                ux += unzigzag(zx);
                uy += unzigzag(zy);
                x = map_coord(m, ux);
                y = map_coord(m, uy) - m->dy;
                on = (flags[i / 8U] >> (i % 8U)) & 1U;
            } else {
                x = first_x; /* close the contour */
                y = first_y;
                on = true;
            }
            if (i == 0U) {
                if (!on) {
                    return false;
                }
                first_x = cur_x = x;
                first_y = cur_y = y;
                continue;
            }
            if (on) {
                if (pending) {
                    raster_quad(r, cur_x, cur_y, ctl_x, ctl_y, x, y);
                    pending = false;
                } else {
                    raster_line(r, cur_x, cur_y, x, y);
                }
                cur_x = x;
                cur_y = y;
            } else {
                if (pending) {
                    const int32_t mx = (int32_t)floor_div((int64_t)ctl_x + x, 2);
                    const int32_t my = (int32_t)floor_div((int64_t)ctl_y + y, 2);
                    raster_quad(r, cur_x, cur_y, ctl_x, ctl_y, mx, my);
                    cur_x = mx;
                    cur_y = my;
                }
                ctl_x = x;
                ctl_y = y;
                pending = true;
            }
        }
    }
    return true;
}

/*
 * The rows a layer touched, summed into coverage and laid over `out` in the layer's colour -
 * source over, premultiplied - and the accumulator left clear behind them for the next layer.
 */
static void raster_composite(struct raster *r, const uint8_t rgba[4], uint8_t (*out)[4]) {
    for (int row = r->top_row; row <= r->bottom_row; ++row) {
        int32_t *acc = g_acc[row];
        uint8_t(*px)[4] = &out[(size_t)row * (size_t)r->width];
        int32_t sum = 0;
        for (int x = 0; x < r->width; ++x) {
            sum += acc[x];
            acc[x] = 0;
            int32_t cover = sum < 0 ? -sum : sum;
            if (cover == 0) {
                continue;
            }
            if (cover > FULL) {
                cover = FULL;
            }
            const uint32_t coverage = (uint32_t)(((int64_t)cover * 255 + FULL / 2) / FULL);
            const uint32_t alpha = (rgba[3] * coverage + 127U) / 255U;
            if (alpha == 0U) {
                continue;
            }
            const uint32_t keep = 255U - alpha;
            for (size_t c = 0; c < 3U; ++c) {
                const uint32_t src = (rgba[c] * alpha + 127U) / 255U;
                px[x][c] = (uint8_t)(src + (px[x][c] * keep + 127U) / 255U);
            }
            px[x][3] = (uint8_t)(alpha + (px[x][3] * keep + 127U) / 255U);
        }
        acc[r->width] = 0;
        acc[r->width + 1] = 0;
    }
    r->top_row = r->rows;
    r->bottom_row = -1;
}

int inkcell_emoji_render(uint16_t sprite, int box, int first_row, int rows, uint8_t (*out)[4]) {
    if (out == NULL || box <= 0 || box > INKCELL_EMOJI_RENDER_MAX || rows <= 0 ||
        rows > INKCELL_EMOJI_BAND || first_row < 0 || first_row + rows > box) {
        return -EINVAL;
    }
    memset(out, 0, (size_t)rows * (size_t)box * 4U);

    const struct inkcell_emoji_pack *pack = inkcell_emoji_pack();
    if (pack == NULL || sprite >= pack->glyph_count) {
        return -ENOENT;
    }

    int rc = 0;
    struct raster r = {.width = box, .rows = rows, .top_row = rows, .bottom_row = -1};
    const struct raster_map map = {
        .scale = (int64_t)box * FX, .grid = pack->grid, .dy = (int32_t)first_row * FX};

    const uint32_t layer_first = inkcell_emoji_le32(&pack->layer_offsets[(size_t)sprite * 4U]);
    const uint32_t layer_end = inkcell_emoji_le32(&pack->layer_offsets[(size_t)sprite * 4U + 4U]);
    for (uint32_t layer = layer_first; layer < layer_end; ++layer) {
        const uint8_t *entry = &pack->layers[(size_t)layer * 4U];
        const uint16_t path = inkcell_emoji_le16(entry);
        const uint8_t *colour = &pack->colours[(size_t)inkcell_emoji_le16(&entry[2]) * 4U];
        const uint8_t *start =
            &pack->paths[inkcell_emoji_le32(&pack->path_offsets[(size_t)path * 4U])];
        const uint8_t *end =
            &pack->paths[inkcell_emoji_le32(&pack->path_offsets[(size_t)path * 4U + 4U])];
        /* A path that does not decode draws what it had drawn before it stopped, and the
           layers above it still draw: one bad outline is not a reason to lose the emoji. */
        if (!raster_path(&r, start, end, &map)) {
            rc = -EBADMSG;
        }
        raster_composite(&r, colour, out);
    }
    return rc;
}
