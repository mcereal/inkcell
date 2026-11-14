#include "inkcell/ui/image.h"

#include <string.h>

struct inkcell_image_fit inkcell_image_fit(uint32_t width, uint32_t height, int box_x, int box_y,
                                           int box_w, int box_h, bool cover) {
    struct inkcell_image_fit f;
    memset(&f, 0, sizeof f);
    f.x = box_x;
    f.y = box_y;
    if (width == 0U || height == 0U || box_w <= 0 || box_h <= 0) {
        return f;
    }
    /* Compared as cross products, in 64 bits: which is wider for its height, the picture or the
       box. */
    const uint64_t picture = (uint64_t)width * (uint64_t)box_h;
    const uint64_t box = (uint64_t)box_w * (uint64_t)height;
    f.src_w = width;
    f.src_h = height;
    if (cover) {
        f.w = box_w;
        f.h = box_h;
        if (picture > box) { /* wider: its sides cut */
            f.src_w = (uint32_t)(box / (uint64_t)box_h);
            f.src_w = f.src_w == 0U ? 1U : f.src_w;
            f.src_x = (width - f.src_w) / 2U;
        } else if (picture < box) { /* taller: its top and bottom cut */
            f.src_h = (uint32_t)(picture / (uint64_t)box_w);
            f.src_h = f.src_h == 0U ? 1U : f.src_h;
            f.src_y = (height - f.src_h) / 2U;
        }
        return f;
    }
    if (picture > box) { /* wider: as wide as the box */
        f.w = box_w;
        f.h = (int)(((uint64_t)height * (uint64_t)box_w) / width);
    } else {
        f.h = box_h;
        f.w = (int)(((uint64_t)width * (uint64_t)box_h) / height);
    }
    f.w = f.w < 1 ? 1 : f.w;
    f.h = f.h < 1 ? 1 : f.h;
    f.x = box_x + (box_w - f.w) / 2;
    f.y = box_y + (box_h - f.h) / 2;
    return f;
}

void inkcell_image_scale_bgra(const uint8_t *src, uint32_t src_w, uint32_t src_h, size_t src_stride,
                              uint8_t *dst, uint32_t dst_w, uint32_t dst_h, size_t dst_stride) {
    if (src == NULL || dst == NULL || src_w == 0U || src_h == 0U || dst_w == 0U || dst_h == 0U) {
        return;
    }
    for (uint32_t dy = 0U; dy < dst_h; ++dy) {
        /* The source rows under this one: [y0, y1), at least one. */
        const uint32_t y0 = (uint32_t)(((uint64_t)dy * src_h) / dst_h);
        uint32_t y1 = (uint32_t)(((uint64_t)(dy + 1U) * src_h) / dst_h);
        y1 = y1 > y0 ? y1 : y0 + 1U;
        uint8_t *out = dst + (size_t)dy * dst_stride;
        for (uint32_t dx = 0U; dx < dst_w; ++dx) {
            const uint32_t x0 = (uint32_t)(((uint64_t)dx * src_w) / dst_w);
            uint32_t x1 = (uint32_t)(((uint64_t)(dx + 1U) * src_w) / dst_w);
            x1 = x1 > x0 ? x1 : x0 + 1U;
            uint64_t sum[4] = {0U, 0U, 0U, 0U}; /* 64 bits: a huge picture to one pixel */
            for (uint32_t y = y0; y < y1; ++y) {
                const uint8_t *row = src + (size_t)y * src_stride + (size_t)x0 * 4U;
                for (uint32_t x = x0; x < x1; ++x, row += 4) {
                    sum[0] += row[0];
                    sum[1] += row[1];
                    sum[2] += row[2];
                    sum[3] += row[3];
                }
            }
            const uint64_t n = (uint64_t)(y1 - y0) * (x1 - x0);
            for (unsigned c = 0U; c < 4U; ++c) {
                out[(size_t)dx * 4U + c] = (uint8_t)((sum[c] + n / 2U) / n);
            }
        }
    }
}
