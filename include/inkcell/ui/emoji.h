#ifndef INKCELL_EMOJI_H
#define INKCELL_EMOJI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Colour emoji, drawn from outlines at whatever size they are asked for.
 *
 * Names and messages are written by people, and a good share of them are written partly or
 * entirely in emoji. Without these a name like that draws as a row of replacement boxes and
 * cannot be told apart from any other.
 *
 * What is stored is drawing instructions, not pictures. An emoji is a stack of layers, each one
 * colour and one outline, and an outline is closed contours of quadratic curves - the shape of
 * a COLRv0 font. It used to be a 32-pixel bitmap, and a bitmap is right at exactly one size:
 * scaled down into a line of text it is fine, scaled up into a keycap it is a blur, because no
 * filter recovers the detail that was never stored. An outline has no size until it is drawn,
 * so the keycap and the text cell are both drawn fresh, each as sharp as its own pixels allow.
 * It is also a third of the bytes of the one bitmap size it replaced, and every colour is the
 * artwork's own rather than the nearest of a palette shared by four thousand pictures.
 *
 * The set is Twemoji (CC-BY 4.0), which is drawn in flat colour with no gradients - the one
 * property that keeps the rasteriser below a few hundred lines - and drawn to read at the
 * sixteen to twenty pixels a line of text gives it.
 *
 * ## Where the pack comes from
 *
 * scripts/gen-emoji.py writes the pack, and the build compiles it in unless INKCELL_EMOJI is
 * OFF. Off, there is no built-in pack and every lookup misses: an emoji draws as the replacement
 * glyph, exactly as an unknown character does, and nothing else changes. A target short of flash
 * can still have emoji without compiling them in, by handing a pack over at run time -
 * inkcell_emoji_load_file() from a file, or inkcell_emoji_use_pack() over bytes it already has
 * mapped, such as a flash partition. The pack is read in place and never copied.
 *
 * ## The pack
 *
 * Little-endian throughout, four-byte aligned sections, and checked whole when it is handed
 * over, so that nothing after that trusts a count it has not bounded:
 *
 *   header (44 bytes)   "ICEM", u16 version, u16 grid (pack units across the em square),
 *                       u32 colours, paths, path_bytes, glyphs, layers, singles, sequences,
 *                       tail, then u8 longest sequence and three bytes of padding
 *   colours             RGBA, four bytes each, straight rather than premultiplied
 *   path offsets        u32 per path plus one, into the path data
 *   path data           per path: varint contours; per contour a varint point count, the
 *                       on-curve flags as bits (point i is bit i % 8 of byte i / 8), then each
 *                       point as zigzag varint (dx, dy) from the point before it - carried
 *                       across contours, from the origin. y grows downward. Padded to four.
 *   layer offsets       u32 per glyph plus one, into the layers
 *   layers              u16 path, u16 colour: bottom layer first
 *   singles             u32 codepoint, u16 glyph, u16 zero - sorted by codepoint
 *   sequences           u32 first, u32 offset into tail, u16 glyph, u8 length, u8 zero - sorted
 *                       by first, longest first within it
 *   tail                u32 codepoints: the rest of each sequence after its first
 *
 * An outline is TrueType's: two off-curve points in a row imply an on-curve point halfway
 * between them, and every contour starts on the curve. Paths are shared between glyphs, which
 * is most of why the pack is small: a skin-tone variant is the same outlines in other colours.
 */

/* The most codepoints one table entry spells; the walker reads no further ahead than this. */
#define INKCELL_EMOJI_LONGEST 16

/* The largest square inkcell_emoji_render() draws, and the most rows one call draws. A caller
   draws a larger emoji band by band - see inkcell_emoji_render(). */
#define INKCELL_EMOJI_RENDER_MAX 256
#define INKCELL_EMOJI_BAND 32

/*
 * Use `size` bytes at `bytes` as the emoji pack from here on. The bytes are read in place for as
 * long as they are in use, so they must outlive that - a mapping, a static array, a buffer the
 * caller keeps.
 *
 * 0, or -EINVAL when the bytes are not a pack this build can read, which leaves the pack that
 * was in use in use. NULL means no emoji at all.
 */
int inkcell_emoji_use_pack(const void *bytes, size_t size);

/* Back to the pack compiled in: 0, or -ENOENT when INKCELL_EMOJI was OFF and there is none -
   which leaves no emoji in use either way. For a program whose own pack failed to load. */
int inkcell_emoji_use_builtin(void);

/* A pack from a file, read whole into memory that inkcell then keeps. 0, -ENOENT when the file
   cannot be read, or -EINVAL when it is not a pack. */
int inkcell_emoji_load_file(const char *path);

/* Whether any pack is in use: the one compiled in, or one handed over. */
bool inkcell_emoji_available(void);

/*
 * Longest emoji starting at codepoints[0]. Returns how many codepoints it consumed - 0 when
 * none of them start an emoji - and stores the glyph id in *sprite.
 *
 * Greedy on purpose: a regional-indicator pair is a flag rather than two letters, and a ZWJ
 * sequence is one glyph rather than its parts, so the longest match is the right one. The
 * table holds no U+FE0F, so a caller matches with the variation selectors taken out - which
 * inkcell_text_cell_next() does.
 */
size_t inkcell_emoji_match(const uint32_t *codepoints, size_t count, uint16_t *sprite);

/* How many glyphs, singles and sequences the pack in use holds, and its longest sequence.
   -ENOENT with no pack. */
struct inkcell_emoji_summary {
    uint32_t glyphs;
    uint32_t singles;
    uint32_t sequences;
    uint8_t longest;
};

int inkcell_emoji_summary(struct inkcell_emoji_summary *out);

/*
 * Entry `index` of the lookup: the singles in codepoint order, then the sequences in the order
 * a match tries them. Writes its codepoints to `codepoints` (INKCELL_EMOJI_LONGEST of room) and
 * its glyph to *sprite, and returns how many codepoints it spells - 0 past the last entry.
 *
 * For a test that the table is ordered the way the match assumes, and for a screen that shows
 * them all.
 */
size_t inkcell_emoji_entry(uint32_t index, uint32_t codepoints[INKCELL_EMOJI_LONGEST],
                           uint16_t *sprite);

/*
 * Draw rows [first_row, first_row + rows) of glyph `sprite` filling a `box`-pixel square, into
 * `out`: `rows` rows of `box` pixels, each RGBA premultiplied by its own opacity.
 *
 * Premultiplied because that is the form a caller blends without a division, and the form in
 * which a transparent pixel is all zero whatever colour it nominally has.
 *
 * In bands, so that what a large emoji costs in memory is a band rather than the square: the
 * scratch behind this is INKCELL_EMOJI_BAND rows of INKCELL_EMOJI_RENDER_MAX, however large the
 * box. A caller wanting the whole square asks for it band by band.
 *
 * Integer arithmetic throughout, as every anti-aliased shape here is: a pixel that depended on a
 * float's rounding would be a golden test that passes on one compiler and not the next.
 *
 * 0; -EINVAL for a box or band out of range, which draws nothing; -ENOENT for a glyph the pack
 * does not have or no pack at all, which leaves `out` transparent; -EBADMSG when an outline
 * stopped short of its end, in which case `out` holds every layer drawn as far as it could be -
 * one bad outline is not a reason to lose the rest of the emoji.
 */
int inkcell_emoji_render(uint16_t sprite, int box, int first_row, int rows, uint8_t (*out)[4]);

/* Codepoints that attach to the character before them and never take a cell of their own:
   variation selectors, skin-tone modifiers, the zero-width joiner, combining marks. Absorbing
   these is what stops an emoji written as base plus modifier - the common spelling, and the
   only spelling for things like U+26F0 U+FE0F - from drawing as two cells. */
bool inkcell_emoji_is_zero_width(uint32_t codepoint);

/*
 * One drawable cell of a UTF-8 string.
 *
 * A cell is not a byte and not always a codepoint: a flag is a pair of regional indicators, a
 * family is a ZWJ sequence, and a modifier attaches to whatever came before it. Layout and
 * drawing both walk cells, through the same function, so a line always measures as wide as it
 * draws.
 */
struct inkcell_text_cell {
    size_t bytes; /* source bytes consumed; 0 at the end of the string */
    bool is_emoji;
    uint16_t sprite;    /* the glyph, when is_emoji */
    uint32_t codepoint; /* when not */
};

struct inkcell_text_cell inkcell_text_cell_next(const char *text);

/* Cells a string occupies once drawn. */
size_t inkcell_text_cells(const char *text);

/* Byte offset of cell `index`, or of the NUL when the string is shorter. */
size_t inkcell_text_cell_offset(const char *text, size_t index);

/* Truncate in place to at most `cells` cells, never inside one. */
void inkcell_text_cell_truncate(char *text, size_t cells);

#ifdef __cplusplus
}
#endif

#endif /* INKCELL_EMOJI_H */
