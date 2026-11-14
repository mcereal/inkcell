#ifndef INKCELL_IMAGE_H
#define INKCELL_IMAGE_H

/*
 * A picture made to fit somewhere: where it goes in a box, and its pixels at that size.
 *
 * A photo is far larger than the place it is shown - a 1080 x 2400 screenshot in a grid cell a
 * tenth that wide - and a blit draws pixels one for one (inkcell_fb_blit_bgra). So a viewer asks
 * inkcell_image_fit() where the picture lands and which part of it shows, scales that part once
 * with inkcell_image_scale_bgra() into a buffer of its own, and blits the buffer each frame. Pure:
 * no panel, no allocation, the same on every backend.
 *
 * Pixels are four bytes, blue, green, red, alpha - what inkwell's PNG decoder hands back.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define INKCELL_IMAGE_PIXEL_BYTES 4U

/* A picture fitted into a box: the part of it shown, and where that lands. */
struct inkcell_image_fit {
    int x, y, w, h;                      /* on the page: inside the box */
    uint32_t src_x, src_y, src_w, src_h; /* of the picture */
};

/*
 * A `width` x `height` picture in the box at `box_x`, `box_y`, `box_w` x `box_h`, its shape kept.
 *
 * - **contain** (`cover` false): all of it, as large as fits, centred; the box may show around it
 *   on two sides. A viewer's page.
 * - **cover** (`cover` true): the box filled, the picture's middle cut to the box's shape. A grid's
 *   cell.
 *
 * A side of 0 - the picture's or the box's - fits to nothing: every field 0 but the box's corner.
 */
struct inkcell_image_fit inkcell_image_fit(uint32_t width, uint32_t height, int box_x, int box_y,
                                           int box_w, int box_h, bool cover);

/*
 * The `src_w` x `src_h` pixels at `src` (rows `src_stride` bytes apart) into `dst_w` x `dst_h` at
 * `dst` (rows `dst_stride` apart).
 *
 * Smaller, each pixel is the mean of the source pixels under it (a box filter): a screenshot's
 * text turns to an even grey in a thumbnail, rather than the shimmer of picking one pixel in ten.
 * Larger, each is its nearest source pixel. Alpha is averaged like the rest. The cost is one read
 * of each source pixel and one write of each destination pixel. Nothing happens for a side of 0.
 */
void inkcell_image_scale_bgra(const uint8_t *src, uint32_t src_w, uint32_t src_h, size_t src_stride,
                              uint8_t *dst, uint32_t dst_w, uint32_t dst_h, size_t dst_stride);

#ifdef __cplusplus
}
#endif

#endif /* INKCELL_IMAGE_H */
