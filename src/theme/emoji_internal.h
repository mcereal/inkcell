#ifndef INKCELL_EMOJI_INTERNAL_H
#define INKCELL_EMOJI_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

/*
 * The seam between the emoji code and the pack compiled into it.
 *
 * src/generated/emoji_pack.c defines this over the pack scripts/gen-emoji.py wrote, and
 * emoji_builtin_none.c defines it over nothing when INKCELL_EMOJI is OFF: CMake picks one, so
 * the choice is made once, where the build is described, rather than behind an #ifdef in every
 * file that asks. NULL, with *size 0, is "no pack compiled in".
 */
const uint8_t *inkcell_emoji_builtin(size_t *size);

/*
 * The pack in use, parsed: every section located and every count bounded, once, when it was
 * handed over. The rasteriser reads through this rather than the bytes so that it can trust an
 * index it has been given.
 */
struct inkcell_emoji_pack {
    uint16_t grid;
    uint32_t colour_count;
    uint32_t path_count;
    uint32_t glyph_count;
    uint32_t single_count;
    uint32_t sequence_count;
    uint32_t tail_count;
    uint8_t longest;
    const uint8_t *colours;       /* colour_count * 4 */
    const uint8_t *path_offsets;  /* (path_count + 1) u32 */
    const uint8_t *paths;         /* path data */
    const uint8_t *layer_offsets; /* (glyph_count + 1) u32 */
    const uint8_t *layers;        /* u16 path, u16 colour */
    const uint8_t *singles;       /* 8 bytes each */
    const uint8_t *sequences;     /* 12 bytes each */
    const uint8_t *tail;          /* u32 each */
};

/* The pack in use, or NULL when there is none. */
const struct inkcell_emoji_pack *inkcell_emoji_pack(void);

/* A number that changes whenever the pack in use does. Glyph ids are only ids within one pack,
   so anything that keeps a drawn glyph by its id keeps this beside it and throws the drawing
   away when the two no longer agree. */
uint32_t inkcell_emoji_generation(void);

static inline uint16_t inkcell_emoji_le16(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

static inline uint32_t inkcell_emoji_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

#endif /* INKCELL_EMOJI_INTERNAL_H */
