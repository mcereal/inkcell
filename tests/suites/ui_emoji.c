/*
 * The emoji pack and the rasteriser under it, over packs built here by hand.
 *
 * A hand-built pack is one square in one colour, which is what makes exact answers possible: a
 * square on half-pixel boundaries covers its edge pixels by exactly a half and its corners by
 * exactly a quarter, and a rasteriser that is off by a rounding anywhere says so here rather
 * than as a shade nobody can name in a golden picture.
 */

#include "framework/inkcell_test.h"

#include "inkcell/ui/emoji.h"
#include "support/emoji_support.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define EMOJI_GRID 16U
#define EMOJI_SQUARE 0x1F7E5U /* the red square, which is what the one glyph is filed under */

struct emoji_pack_bytes {
    _Alignas(4) uint8_t bytes[256];
    size_t size;
};

static void put16(uint8_t *at, uint16_t v) {
    at[0] = (uint8_t)v;
    at[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *at, uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        at[i] = (uint8_t)(v >> (8 * i));
    }
}

static size_t put_varint(uint8_t *at, uint32_t v) {
    size_t n = 0;
    do {
        uint8_t byte = (uint8_t)(v & 0x7FU);
        v >>= 7;
        at[n++] = (uint8_t)(byte | (v != 0U ? 0x80U : 0U));
    } while (v != 0U);
    return n;
}

static uint32_t zigzag(int32_t v) {
    return v >= 0 ? (uint32_t)v << 1 : (((uint32_t)-v) << 1) - 1U;
}

/*
 * A pack of one glyph: a rectangle from (x0, y0) to (x1, y1) in sixteenths of the em, filled
 * red, filed under U+1F7E5. `contours` is what the path claims to hold, so a caller can claim
 * more than it does.
 */
static void emoji_build_path(struct emoji_pack_bytes *pack, const uint8_t *path, size_t n);

static void emoji_build(struct emoji_pack_bytes *pack, int32_t x0, int32_t y0, int32_t x1,
                        int32_t y1, uint32_t contours) {
    uint8_t path[64];
    size_t n = put_varint(path, contours);
    n += put_varint(&path[n], 4U);
    path[n++] = 0x0FU; /* all four on the curve */
    const int32_t corners[4][2] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    int32_t x = 0;
    int32_t y = 0;
    for (int i = 0; i < 4; ++i) {
        n += put_varint(&path[n], zigzag(corners[i][0] - x));
        n += put_varint(&path[n], zigzag(corners[i][1] - y));
        x = corners[i][0];
        y = corners[i][1];
    }
    emoji_build_path(pack, path, n);
}

/* The same pack around path bytes the caller wrote, for a path no rectangle spells. */
static void emoji_build_path(struct emoji_pack_bytes *pack, const uint8_t *path, size_t n) {
    const size_t path_bytes = (n + 3U) & ~(size_t)3U;

    uint8_t *b = pack->bytes;
    memset(b, 0, sizeof pack->bytes);
    memcpy(b, "ICEM", 4);
    put16(&b[4], 1U);
    put16(&b[6], EMOJI_GRID);
    put32(&b[8], 1U);                    /* colours */
    put32(&b[12], 1U);                   /* paths */
    put32(&b[16], (uint32_t)path_bytes); /* path bytes */
    put32(&b[20], 1U);                   /* glyphs */
    put32(&b[24], 1U);                   /* layers */
    put32(&b[28], 1U);                   /* singles */
    put32(&b[32], 0U);                   /* sequences */
    put32(&b[36], 0U);                   /* tail */
    b[40] = 1U;                          /* longest */

    size_t at = 44U;
    const uint8_t red[4] = {255U, 0U, 0U, 255U};
    memcpy(&b[at], red, 4);
    at += 4U;
    put32(&b[at], 0U);
    put32(&b[at + 4U], (uint32_t)n);
    at += 8U;
    memcpy(&b[at], path, n);
    at += path_bytes;
    put32(&b[at], 0U);
    put32(&b[at + 4U], 1U);
    at += 8U;
    put16(&b[at], 0U);      /* layer: path 0 */
    put16(&b[at + 2U], 0U); /* colour 0 */
    at += 4U;
    put32(&b[at], EMOJI_SQUARE);
    put16(&b[at + 4U], 0U);
    at += 8U;
    pack->size = at;
}

static uint16_t emoji_square_glyph(void) {
    const uint32_t cp = EMOJI_SQUARE;
    uint16_t glyph = UINT16_MAX;
    return inkcell_emoji_match(&cp, 1U, &glyph) == 1U ? glyph : UINT16_MAX;
}

/* The one pack every case leaves behind it, whatever it swapped in: the rest of the suite
   draws with the suite's own. */
static void emoji_restore(void) {
    (void)inkcell_test_emoji_restore();
}

/* A square on half-pixel boundaries: whole pixels inside, halves along each side, quarters at
   the corners, nothing outside - and the colour premultiplied by exactly that. */
INKCELL_TEST_CASE(emoji_raster_square_is_exact, unit) {
    static struct emoji_pack_bytes pack;
    /* Sixteenths of an 8-pixel box are half pixels: (1, 1) to (9, 9) is 0.5 px to 4.5 px. */
    emoji_build(&pack, 1, 1, 9, 9, 1U);
    INKCELL_TEST_FAIL_IF(inkcell_emoji_use_pack(pack.bytes, pack.size) != 0,
                         "a hand-built pack should be accepted");
    const uint16_t glyph = emoji_square_glyph();
    INKCELL_TEST_FAIL_IF(glyph != 0U, "the square should be glyph 0");

    uint8_t out[8 * 8][4];
    INKCELL_TEST_FAIL_IF(inkcell_emoji_render(glyph, 8, 0, 8, out) != 0, "the square renders");
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            const bool edge_x = x == 0 || x == 4;
            const bool edge_y = y == 0 || y == 4;
            const bool inside = x <= 4 && y <= 4;
            uint8_t want = 0U;
            if (inside) {
                want = edge_x && edge_y ? 64U : (edge_x || edge_y ? 128U : 255U);
            }
            const uint8_t *px = out[y * 8 + x];
            INKCELL_TEST_FAIL_IF(px[3] != want, "a pixel's coverage should be its exact area");
            INKCELL_TEST_FAIL_IF(px[0] != want || px[1] != 0U || px[2] != 0U,
                                 "red premultiplied by its coverage");
        }
    }

    emoji_restore();
    record_success(test_name);
}

/* An outline reaching far past the box on any side draws what lies inside and nothing else -
   and covers the box to its edge, rather than stopping a pixel short where it was clipped. */
INKCELL_TEST_CASE(emoji_raster_clips_to_the_box, unit) {
    static struct emoji_pack_bytes pack;
    const struct {
        int32_t x0, y0, x1, y1;
        int left, top, right, bottom; /* the pixels expected solid, in an 8-pixel box */
    } cases[] = {
        {-4000, -4000, 4, 4, 0, 0, 2, 2},  /* off the top-left */
        {12, -3000, 3000, 16, 6, 0, 8, 8}, /* off the right, top and bottom */
        {-50, 6, 50, 10, 0, 3, 8, 5},      /* a bar wider than the box */
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        emoji_build(&pack, cases[i].x0, cases[i].y0, cases[i].x1, cases[i].y1, 1U);
        INKCELL_TEST_FAIL_IF(inkcell_emoji_use_pack(pack.bytes, pack.size) != 0,
                             "a hand-built pack should be accepted");
        uint8_t out[8 * 8][4];
        INKCELL_TEST_FAIL_IF(inkcell_emoji_render(0U, 8, 0, 8, out) != 0, "the shape renders");
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                const bool solid = x >= cases[i].left && x < cases[i].right && y >= cases[i].top &&
                                   y < cases[i].bottom;
                INKCELL_TEST_FAIL_IF(out[y * 8 + x][3] != (solid ? 255U : 0U),
                                     "clipped coverage should be whole inside and none outside");
            }
        }
    }

    emoji_restore();
    record_success(test_name);
}

/* A pack is checked whole when it is handed over. One that is not a pack, or whose counts
   point past its own bytes, is refused - and the pack that was in use stays in use. */
INKCELL_TEST_CASE(emoji_pack_rejects_what_is_not_one, unit) {
    static struct emoji_pack_bytes good;
    static struct emoji_pack_bytes bad;
    emoji_build(&good, 0, 0, 16, 16, 1U);
    INKCELL_TEST_FAIL_IF(inkcell_emoji_use_pack(good.bytes, good.size) != 0,
                         "a hand-built pack should be accepted");

    INKCELL_TEST_FAIL_IF(inkcell_emoji_use_pack(good.bytes, good.size - 1U) != -EINVAL,
                         "a truncated pack is refused");
    INKCELL_TEST_FAIL_IF(inkcell_emoji_use_pack(good.bytes, 10U) != -EINVAL,
                         "a pack shorter than its header is refused");

    bad = good;
    bad.bytes[0] = 'X';
    INKCELL_TEST_FAIL_IF(inkcell_emoji_use_pack(bad.bytes, bad.size) != -EINVAL,
                         "the wrong magic is refused");
    bad = good;
    put16(&bad.bytes[4], 2U);
    INKCELL_TEST_FAIL_IF(inkcell_emoji_use_pack(bad.bytes, bad.size) != -EINVAL,
                         "a version this build cannot read is refused");
    bad = good;
    put32(&bad.bytes[20], 5000U); /* glyphs: offsets that would run past the bytes */
    INKCELL_TEST_FAIL_IF(inkcell_emoji_use_pack(bad.bytes, bad.size) != -EINVAL,
                         "a count past the pack's own bytes is refused");
    /* The single's glyph is the last u16 but one: point it past the only glyph. */
    bad = good;
    put16(&bad.bytes[bad.size - 4U], 1U);
    INKCELL_TEST_FAIL_IF(inkcell_emoji_use_pack(bad.bytes, bad.size) != -EINVAL,
                         "an entry naming a glyph that is not there is refused");

    INKCELL_TEST_FAIL_IF(emoji_square_glyph() != 0U,
                         "after every refusal the good pack is still the one in use");

    emoji_restore();
    record_success(test_name);
}

/* An outline whose bytes stop short draws what it had, and says so. */
INKCELL_TEST_CASE(emoji_raster_short_path_draws_what_it_can, unit) {
    static struct emoji_pack_bytes pack;
    emoji_build(&pack, 0, 0, 8, 8, 2U); /* claims a second contour it does not have */
    INKCELL_TEST_FAIL_IF(inkcell_emoji_use_pack(pack.bytes, pack.size) != 0,
                         "the pack's layout is sound even though a path is not");
    uint8_t out[8 * 8][4];
    INKCELL_TEST_FAIL_IF(inkcell_emoji_render(0U, 8, 0, 8, out) != -EBADMSG,
                         "a path that stops short is reported");
    INKCELL_TEST_FAIL_IF(out[0][3] != 255U || out[7 * 8 + 7][3] != 0U,
                         "the contour before the short one still draws");

    emoji_restore();
    record_success(test_name);
}

/* No pack: nothing is an emoji, so an emoji is one cell of replacement like any character the
   fonts lack, and a render draws nothing - and then the suite's pack comes back. */
INKCELL_TEST_CASE(emoji_without_a_pack, unit) {
    INKCELL_TEST_FAIL_IF(inkcell_emoji_use_pack(NULL, 0U) != 0, "no pack is a pack");
    INKCELL_TEST_FAIL_IF(inkcell_emoji_available(), "nothing is in use");
    const char *grin = "\xF0\x9F\x98\x80";
    const struct inkcell_text_cell cell = inkcell_text_cell_next(grin);
    INKCELL_TEST_FAIL_IF(cell.is_emoji || cell.bytes != 4U,
                         "an emoji with no pack is one plain cell of its four bytes");
    uint8_t out[4 * 4][4];
    INKCELL_TEST_FAIL_IF(inkcell_emoji_render(0U, 4, 0, 4, out) != -ENOENT,
                         "a render with no pack reports it");
    struct inkcell_emoji_summary summary;
    INKCELL_TEST_FAIL_IF(inkcell_emoji_summary(&summary) != -ENOENT, "no summary without one");

    INKCELL_TEST_FAIL_IF(inkcell_test_emoji_restore() != 0, "the suite's pack comes back");
    INKCELL_TEST_FAIL_IF(!inkcell_text_cell_next(grin).is_emoji, "and draws the grin again");
    record_success(test_name);
}

/* A pack from a file is the same pack, and a file that is missing or is not one is refused. */
INKCELL_TEST_CASE(emoji_pack_loads_from_a_file, unit) {
    static struct emoji_pack_bytes pack;
    emoji_build(&pack, 0, 0, 16, 16, 1U);

    char path[] = "/tmp/inkcell-emoji-XXXXXX";
    const int fd = mkstemp(path);
    INKCELL_TEST_FAIL_IF(fd < 0, "a temporary file should open");
    INKCELL_TEST_FAIL_IF(write(fd, pack.bytes, pack.size) != (ssize_t)pack.size,
                         "the pack should write");
    close(fd);

    INKCELL_TEST_FAIL_IF(inkcell_emoji_load_file(path) != 0, "the file should load");
    INKCELL_TEST_FAIL_IF(emoji_square_glyph() != 0U, "and be the pack in use");
    uint8_t out[4 * 4][4];
    INKCELL_TEST_FAIL_IF(inkcell_emoji_render(0U, 4, 0, 4, out) != 0 || out[5][3] != 255U,
                         "and draw");

    FILE *truncate = fopen(path, "wb");
    INKCELL_TEST_FAIL_IF(truncate == NULL, "the file should reopen");
    fwrite(pack.bytes, 1U, 20U, truncate);
    fclose(truncate);
    INKCELL_TEST_FAIL_IF(inkcell_emoji_load_file(path) != -EINVAL, "half a pack is refused");
    INKCELL_TEST_FAIL_IF(emoji_square_glyph() != 0U, "leaving the loaded one in use");
    unlink(path);
    INKCELL_TEST_FAIL_IF(inkcell_emoji_load_file(path) != -ENOENT, "a missing file is refused");

    emoji_restore();
    record_success(test_name);
}

/* A pack whose deltas carry a coordinate past what an int32 holds - which parses, because the
   layout is sound - is an outline that stops short, not arithmetic that overflows. */
INKCELL_TEST_CASE(emoji_raster_refuses_a_coordinate_past_the_range, unit) {
    static struct emoji_pack_bytes pack;
    uint8_t path[64];
    size_t n = put_varint(path, 1U);
    n += put_varint(&path[n], 3U);
    path[n++] = 0x07U;
    n += put_varint(&path[n], zigzag(INT32_MAX)); /* x at the top of the range */
    n += put_varint(&path[n], 0U);
    n += put_varint(&path[n], zigzag(1)); /* and one past it */
    n += put_varint(&path[n], 0U);
    n += put_varint(&path[n], 0U);
    n += put_varint(&path[n], zigzag(8));
    emoji_build_path(&pack, path, n);
    INKCELL_TEST_FAIL_IF(inkcell_emoji_use_pack(pack.bytes, pack.size) != 0,
                         "the pack's layout is sound even though a path is not");

    uint8_t out[8 * 8][4];
    INKCELL_TEST_FAIL_IF(inkcell_emoji_render(0U, 8, 0, 8, out) != -EBADMSG,
                         "a coordinate past the range is reported");

    /* Inside an int32 but past any panel: scaled to pixels, still refused rather than wrapped. */
    n = put_varint(path, 1U);
    n += put_varint(&path[n], 3U);
    path[n++] = 0x07U;
    n += put_varint(&path[n], zigzag(INT32_MAX - 1));
    n += put_varint(&path[n], 0U);
    n += put_varint(&path[n], 0U);
    n += put_varint(&path[n], zigzag(8));
    n += put_varint(&path[n], zigzag(-(INT32_MAX - 1)));
    n += put_varint(&path[n], 0U);
    emoji_build_path(&pack, path, n);
    INKCELL_TEST_FAIL_IF(inkcell_emoji_use_pack(pack.bytes, pack.size) != 0,
                         "the pack's layout is sound");
    INKCELL_TEST_FAIL_IF(inkcell_emoji_render(0U, 8, 0, 8, out) != -EBADMSG,
                         "a coordinate no panel could hold is reported");

    emoji_restore();
    record_success(test_name);
}
