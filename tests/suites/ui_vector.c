#define _POSIX_C_SOURCE 200809L

/*
 * Vector images: the format refused when it is not version 1, and the rasteriser pinned to exact
 * pixels - a square on whole pixels is whole pixels and nothing else, a half pixel is half, a
 * circle has a circle's area, and the fill rules differ where they should.
 */

#include "framework/inkcell_test.h"

#include "inkcell/ui/fb_capture.h"
#include "inkcell/ui/fb_draw.h"
#include "inkcell/ui/vector.h"

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define GRID 64

struct painted {
    int coverage[GRID][GRID];
    struct inkcell_rgb color[GRID][GRID];
    int calls;
};

static void paint(void *ctx, int x, int y, struct inkcell_rgb color, int coverage) {
    struct painted *p = ctx;
    p->calls++;
    if (x >= 0 && y >= 0 && x < GRID && y < GRID) {
        p->coverage[y][x] += coverage;
        p->color[y][x] = color;
    }
}

static struct inkcell_vector *parse(const char *text) {
    struct inkcell_vector *v = NULL;
    return inkcell_vector_parse(text, strlen(text), &v) == 0 ? v : NULL;
}

/* Renders `text` at `w` x `h` into a fresh `p`; false when it would not parse or draw. */
static bool render(const char *text, int w, int h, struct painted *p) {
    memset(p, 0, sizeof *p);
    struct inkcell_vector *v = parse(text);
    const bool drawn = v != NULL && inkcell_vector_render(v, w, h, paint, p) == 0;
    inkcell_vector_free(v);
    return drawn;
}

static long total(const struct painted *p) {
    long sum = 0;
    for (int y = 0; y < GRID; ++y) {
        for (int x = 0; x < GRID; ++x) {
            sum += p->coverage[y][x];
        }
    }
    return sum;
}

#define SQUARE "inkvec 1 10 10\nfill #ff0000 M 2 2 L 8 2 L 8 8 L 2 8 Z\n"

INKCELL_TEST_CASE(ui_vector_refuses_what_is_not_version_1, unit) {
    static const char *const bad[] = {
        "",
        "fill #ff0000 M 0 0 L 1 1 Z\n",                  /* no header */
        "inkvec 2 10 10\n",                              /* a later version */
        "inkvec 1 0 10\n",                               /* no view box */
        "inkvec 1 0.5 10\n",                             /* under a unit */
        "inkvec 1 10 10\nstroke #ff0000 M 0 0 L 1 1\n",  /* a word version 1 does not have */
        "inkvec 1 10 10\nfill red M 0 0 L 1 1\n",        /* a colour by name */
        "inkvec 1 10 10\nfill #ff00 M 0 0 L 1 1\n",      /* a colour short a channel */
        "inkvec 1 10 10\nfill #ff0000 L 1 1 Z\n",        /* a path that starts nowhere */
        "inkvec 1 10 10\nfill #ff0000 M 0 0 L 1\n",      /* a point short a coordinate */
        "inkvec 1 10 10\nfill #ff0000 M 0 0 l 1 1\n",    /* a relative command */
        "inkvec 1 10 10\nfill #ff0000\n",                /* a paint with no path */
        "inkvec 1 10 10\nlinear 0 0 10 0 M 0 0 L 1 1\n", /* a gradient, no stops */
        "inkvec 1 10 10\nlinear 0 0 10 0 1 #000000 0 #ffffff M 0 0\n", /* stops out of order */
        "inkvec 1 10 10\nfill #ff0000 M 99999 0 L 1 1\n",              /* past the coordinates */
    };
    for (size_t i = 0U; i < sizeof bad / sizeof bad[0]; ++i) {
        struct inkcell_vector *v = parse(bad[i]);
        inkcell_vector_free(v);
        INKCELL_TEST_FAIL_IF(v != NULL, bad[i][0] != '\0' ? bad[i] : "empty text parsed");
    }
    struct inkcell_vector *v = parse("; a comment\n\ninkvec 1 24 12\n; another\n");
    int w = 0, h = 0;
    inkcell_vector_view_box(v, &w, &h);
    inkcell_vector_free(v);
    INKCELL_TEST_FAIL_IF(v == NULL || w != 24 || h != 12, "comments and an empty picture parsed");
    record_success(test_name);
}

INKCELL_TEST_CASE(ui_vector_square_on_whole_pixels, unit) {
    static struct painted p;
    INKCELL_TEST_FAIL_IF(!render(SQUARE, 10, 10, &p), "the square did not draw");
    for (int y = 0; y < 10; ++y) {
        for (int x = 0; x < 10; ++x) {
            const bool inside = x >= 2 && x < 8 && y >= 2 && y < 8;
            INKCELL_TEST_FAIL_IF(p.coverage[y][x] != (inside ? INKCELL_VECTOR_COVERAGE_ONE : 0),
                                 "a square on pixel edges is not exactly its pixels");
        }
    }
    INKCELL_TEST_FAIL_IF(p.color[4][4].r != 0xFF || p.color[4][4].g != 0U, "not its colour");

    /* Twice the size is four times the pixels, still exact. */
    static struct painted big;
    INKCELL_TEST_FAIL_IF(!render(SQUARE, 20, 20, &big), "the square did not draw at 20");
    INKCELL_TEST_FAIL_IF(total(&big) != 144L * INKCELL_VECTOR_COVERAGE_ONE,
                         "the square at twice the size is not 12x12 pixels");
    record_success(test_name);
}

INKCELL_TEST_CASE(ui_vector_half_pixels_are_half, unit) {
    static struct painted p;
    INKCELL_TEST_FAIL_IF(
        !render("inkvec 1 10 10\nfill #ffffff M 2.5 2 L 8 2 L 8 8 L 2.5 8 Z\n", 10, 10, &p),
        "did not draw");
    INKCELL_TEST_FAIL_IF(p.coverage[4][2] != INKCELL_VECTOR_COVERAGE_ONE / 2,
                         "a pixel half inside the edge is not half covered");
    INKCELL_TEST_FAIL_IF(p.coverage[4][3] != INKCELL_VECTOR_COVERAGE_ONE, "inside is not whole");
    /* Translucent paint is coverage too: half of a whole pixel, rounded. */
    INKCELL_TEST_FAIL_IF(
        !render("inkvec 1 10 10\nfill #ffffff80 M 2 2 L 8 2 L 8 8 L 2 8 Z\n", 10, 10, &p),
        "did not draw");
    INKCELL_TEST_FAIL_IF(p.coverage[4][4] < 128 || p.coverage[4][4] > 129,
                         "half an alpha is not half the coverage");
    record_success(test_name);
}

INKCELL_TEST_CASE(ui_vector_circle_has_a_circles_area, unit) {
    /* A radius-20 circle as four cubics (the 0.5523 approximation), drawn 1:1. */
    static const char circle[] =
        "inkvec 1 64 64\nfill #000000 M 52 32 C 52 43.046 43.046 52 32 52 "
        "C 20.954 52 12 43.046 12 32 C 12 20.954 20.954 12 32 12 C 43.046 12 52 20.954 52 32 Z\n";
    static struct painted p;
    INKCELL_TEST_FAIL_IF(!render(circle, 64, 64, &p), "the circle did not draw");
    const long area = total(&p) / INKCELL_VECTOR_COVERAGE_ONE; /* pi * 400 = 1256.6 */
    INKCELL_TEST_FAIL_IF(area < 1244 || area > 1270, "the circle's area is not pi r squared");
    INKCELL_TEST_FAIL_IF(p.coverage[32][32] != INKCELL_VECTOR_COVERAGE_ONE || p.coverage[2][2] != 0,
                         "the circle's middle or its outside is wrong");
    /* Its edge is antialiased: some pixel there is neither in nor out. */
    bool partial = false;
    for (int x = 0; x < GRID; ++x) {
        partial = partial || (p.coverage[14][x] > 0 && p.coverage[14][x] < 256);
    }
    INKCELL_TEST_FAIL_IF(!partial, "the circle's edge is not antialiased");
    record_success(test_name);
}

INKCELL_TEST_CASE(ui_vector_fill_rules, unit) {
    /* An outer and an inner square drawn the same way round: nonzero fills the inside of both,
       evenodd leaves the inner one a hole. */
    static const char nonzero[] = "inkvec 1 10 10\nfill #000000 M 1 1 L 9 1 L 9 9 L 1 9 Z "
                                  "M 3 3 L 7 3 L 7 7 L 3 7 Z\n";
    static const char evenodd[] = "inkvec 1 10 10\nfill #000000 evenodd M 1 1 L 9 1 L 9 9 L 1 9 Z "
                                  "M 3 3 L 7 3 L 7 7 L 3 7 Z\n";
    static struct painted p;
    INKCELL_TEST_FAIL_IF(!render(nonzero, 10, 10, &p) || p.coverage[5][5] != 256,
                         "nonzero left a hole");
    INKCELL_TEST_FAIL_IF(!render(evenodd, 10, 10, &p) || p.coverage[5][5] != 0 ||
                             p.coverage[2][2] != 256,
                         "evenodd did not leave the hole");
    /* An open subpath is closed, as a fill closes it. */
    INKCELL_TEST_FAIL_IF(
        !render("inkvec 1 10 10\nfill #000000 M 2 2 L 8 2 L 8 8 L 2 8\n", 10, 10, &p) ||
            total(&p) != 36L * 256,
        "an open path was not closed");
    record_success(test_name);
}

INKCELL_TEST_CASE(ui_vector_linear_gradient, unit) {
    static struct painted p;
    INKCELL_TEST_FAIL_IF(!render("inkvec 1 10 10\nlinear 0 0 10 0 0 #000000 1 #ffffff "
                                 "M 0 0 L 10 0 L 10 10 L 0 10 Z\n",
                                 10, 10, &p),
                         "the gradient did not draw");
    INKCELL_TEST_FAIL_IF(p.color[5][0].r > 20 || p.color[5][9].r < 235,
                         "the gradient does not run dark to light along its axis");
    INKCELL_TEST_FAIL_IF(p.color[5][4].r < 100 || p.color[5][4].r > 128,
                         "the gradient's middle is not its middle");
    INKCELL_TEST_FAIL_IF(p.color[0][4].r != p.color[9][4].r,
                         "a horizontal gradient changes vertically");
    /* Three stops: the middle one is reached at its offset. */
    INKCELL_TEST_FAIL_IF(!render("inkvec 1 10 10\nlinear 0 0 10 0 0 #000000 0.5 #ff0000 1 #000000 "
                                 "M 0 0 L 10 0 L 10 10 L 0 10 Z\n",
                                 10, 10, &p),
                         "three stops did not draw");
    INKCELL_TEST_FAIL_IF(p.color[5][5].r < 220 || p.color[5][0].r > 40 || p.color[5][9].r > 60,
                         "the middle stop is not in the middle");
    record_success(test_name);
}

INKCELL_TEST_CASE(ui_vector_render_refuses, unit) {
    struct inkcell_vector *v = parse(SQUARE);
    static struct painted p;
    INKCELL_TEST_FAIL_IF_CLEANUP(
        inkcell_vector_render(v, 0, 10, paint, &p) != -EINVAL ||
            inkcell_vector_render(v, INKCELL_VECTOR_MAX_PX + 1, 10, paint, &p) != -EINVAL ||
            inkcell_vector_render(NULL, 10, 10, paint, &p) != -EINVAL ||
            inkcell_vector_render(v, 10, 10, NULL, &p) != -EINVAL,
        inkcell_vector_free(v), "a size it cannot draw was not refused");
    inkcell_vector_free(v);
    record_success(test_name);
}

INKCELL_TEST_CASE(ui_vector_on_a_frame, unit) {
    struct inkcell_capture *capture = NULL;
    INKCELL_TEST_FAIL_IF(inkcell_capture_open(&capture, 40U, 40U, 0) != 0, "no capture");
    struct inkcell_draw_state *state = inkcell_capture_state(capture);
    const struct inkcell_rgb ground = {0U, 0U, 0U};
    inkcell_fb_clear(state, ground);
    struct inkcell_vector *v = parse(SQUARE);
    const int drawn = inkcell_fb_draw_vector(state, v, (struct inkcell_fb_rect){10, 10, 20, 20}, 0);
    inkcell_vector_free(v);
    const uint8_t *px = state->surface.pixels;
    const size_t stride = state->surface.stride;
    uint32_t inside = 0U, outside = 0U, corner = 0U;
    memcpy(&inside, px + 20U * stride + 20U * 4U, 4U);
    memcpy(&outside, px + 12U * stride + 12U * 4U, 4U); /* in the box, outside the square */
    memcpy(&corner, px, 4U);
    inkcell_capture_close(capture);
    INKCELL_TEST_FAIL_IF(drawn != 0, "the frame refused the vector");
    INKCELL_TEST_FAIL_IF(inside == outside, "the square is not on the frame");
    INKCELL_TEST_FAIL_IF(outside != corner, "the vector drew outside its shape");
    record_success(test_name);
}

/* A full-bleed picture cut to rounded corners: the system's shape, not the artwork's. */
INKCELL_TEST_CASE(ui_vector_cut_to_a_rounded_box, unit) {
    struct inkcell_capture *capture = NULL;
    INKCELL_TEST_FAIL_IF(inkcell_capture_open(&capture, 40U, 40U, 0) != 0, "no capture");
    struct inkcell_draw_state *state = inkcell_capture_state(capture);
    inkcell_fb_clear(state, (struct inkcell_rgb){0U, 0U, 0U});
    struct inkcell_vector *v =
        parse("inkvec 1 10 10\nfill #ffffff M 0 0 L 10 0 L 10 10 L 0 10 Z\n");
    const int drawn = inkcell_fb_draw_vector(state, v, (struct inkcell_fb_rect){0, 0, 40, 40}, 12);
    inkcell_vector_free(v);
    const uint8_t *px = state->surface.pixels;
    const size_t stride = state->surface.stride;
    uint32_t corner = 0U, middle = 0U, edge = 0U, bg = 0U;
    memcpy(&corner, px + 1U * stride + 1U * 4U, 4U);
    memcpy(&middle, px + 20U * stride + 20U * 4U, 4U);
    memcpy(&edge, px + 20U * stride + 0U * 4U, 4U); /* the straight side: inside */
    inkcell_fb_clear(state, (struct inkcell_rgb){0U, 0U, 0U});
    memcpy(&bg, px, 4U);
    inkcell_capture_close(capture);
    INKCELL_TEST_FAIL_IF(drawn != 0, "the frame refused the vector");
    INKCELL_TEST_FAIL_IF(corner != bg, "the picture was not cut at the corner");
    INKCELL_TEST_FAIL_IF(middle == bg || edge != middle, "the picture is not whole inside");
    record_success(test_name);
}
