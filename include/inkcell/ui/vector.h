#ifndef INKCELL_UI_VECTOR_H
#define INKCELL_UI_VECTOR_H

/*
 * Vector images: a picture as shapes rather than pixels, drawn sharp at whatever size it is asked
 * for.
 *
 * What an app icon, an illustration or a logo needs once one panel is 3.2" and the next is a
 * 416 ppi phone: a bitmap is right at one size and soft or jagged at every other, and shipping a
 * bitmap per density is a packaging problem that grows with every panel. A vector is drawn from
 * its shapes at the size the layout asked for, in dp, on that panel.
 *
 * The format is inkcell's own, "inkvec", and small on purpose. SVG is the format artwork comes
 * in, and parsing it - XML, CSS, inheritance, a dozen shape elements, arcs, relative commands -
 * is a build machine's job, done once by scripts/svg2inkvec.py. What reaches a device is what is
 * left when all of that has been resolved: filled outlines in absolute coordinates, each with its
 * paint. A line-based text format rather than a binary one, because an icon is a few kilobytes
 * either way and a person can read this one in a diff.
 *
 *     inkvec 1 W H                        the view box: (0,0) to (W,H), in its own units, each
 *                                         at least 1 and at most 8192
 *     fill COLOR [evenodd] PATH           an outline filled with one colour
 *     linear X0 Y0 X1 Y1 STOPS [evenodd] PATH
 *                                         ... with a linear gradient from (X0,Y0) to (X1,Y1), in
 *                                         view-box units; STOPS is OFFSET COLOR pairs, offsets
 *                                         from 0 to 1 in order
 *     ; anything                          a comment
 *
 * COLOR is #rrggbb or #rrggbbaa. PATH is absolute commands only - M x y, L x y, Q x1 y1 x y,
 * C x1 y1 x2 y2 x y, Z - which is all an outline needs once arcs, shapes and relative steps are
 * resolved. An open subpath is closed, as a fill would close it. Lines are painted in order,
 * each over the last. The fill rule is nonzero unless the line says evenodd. Numbers are decimal,
 * resolved to 1/256 of a unit.
 *
 * Changes by appending: a reader of version 1 refuses a later version rather than drawing half
 * of it, and a new paint or keyword is a new version.
 *
 * Drawing is integer arithmetic throughout - outlines flattened in fixed point, sixteen scanlines
 * a pixel, exact coverage across each - so a vector draws the same pixels on every machine, which
 * is what lets a golden sheet hold one.
 */

#include "inkcell/ui/theme.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct inkcell_vector;

/* The largest picture inkcell_vector_render() draws, in pixels on a side: an icon or an
   illustration, not a wallpaper. Bigger requests are refused rather than allocated. */
#define INKCELL_VECTOR_MAX_PX 2048

/*
 * Parses `size` bytes of inkvec text into a picture. 0, or -EINVAL for text that is not inkvec
 * version 1 (nothing is drawn from a file half understood), -ENOMEM.
 */
int inkcell_vector_parse(const char *text, size_t size, struct inkcell_vector **out);
void inkcell_vector_free(struct inkcell_vector *vector);

/* The view box, in its own units - what a caller keeps the aspect ratio of. */
void inkcell_vector_view_box(const struct inkcell_vector *vector, int *width, int *height);

/*
 * Draws `vector` scaled to `width` x `height` pixels, telling `emit` the colour and coverage of
 * every pixel a shape touches, in paint order: coverage runs 0..INKCELL_VECTOR_COVERAGE_ONE and
 * already includes the colour's own alpha. Pixels are relative to the picture's corner. What the
 * coverage is blended onto is the caller's business, which is what keeps this testable without
 * a panel; inkcell_fb_draw_vector() blends it onto a frame. 0, -EINVAL (no picture, or a size
 * outside 1..INKCELL_VECTOR_MAX_PX), -ENOMEM.
 */
#define INKCELL_VECTOR_COVERAGE_ONE 256
typedef void (*inkcell_vector_emit)(void *ctx, int x, int y, struct inkcell_rgb color,
                                    int coverage);
int inkcell_vector_render(const struct inkcell_vector *vector, int width, int height,
                          inkcell_vector_emit emit, void *ctx);

#ifdef __cplusplus
}
#endif

#endif
