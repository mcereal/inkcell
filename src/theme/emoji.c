#include "inkcell/ui/emoji.h"

#include "emoji_internal.h"
#include "inkcell/ui/font5x7.h"
#include "inkwell/base/file.h"
#include "inkwell/base/text.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* The walker reads this far ahead, which is as long as any sequence the pack may hold. */
#define EMOJI_MAX_LOOKAHEAD INKCELL_EMOJI_LONGEST

#define EMOJI_HEADER_BYTES 44U
#define EMOJI_VERSION 1U
#define EMOJI_SINGLE_BYTES 8U
#define EMOJI_SEQUENCE_BYTES 12U
#define EMOJI_LAYER_BYTES 4U

/*
 * The pack in use. Unset until something first asks, so that a program which hands its own
 * pack over before drawing never parses the built-in one, and one that never draws an emoji
 * never parses anything.
 */
static struct inkcell_emoji_pack g_pack;
static enum { EMOJI_UNSET, EMOJI_READY, EMOJI_NONE } g_state = EMOJI_UNSET;
/* What inkcell_emoji_load_file() read, kept for as long as it is the pack in use. */
static uint8_t *g_loaded;
static uint32_t g_generation;

/* Take `bytes` bytes off the front of what is left, or fail: every section is located this
   way, so a count that claims more than the pack holds is caught here and nowhere later. */
static const uint8_t *emoji_take(const uint8_t **at, size_t *left, uint64_t bytes) {
    if (bytes > *left) {
        return NULL;
    }
    const uint8_t *section = *at;
    *at += bytes;
    *left -= (size_t)bytes;
    return section;
}

/* Offsets that never step backwards and end inside what they index. */
static int emoji_offsets_ok(const uint8_t *offsets, uint32_t count, uint32_t limit) {
    uint32_t previous = 0U;
    for (uint32_t i = 0; i <= count; ++i) {
        const uint32_t offset = inkcell_emoji_le32(&offsets[(size_t)i * 4U]);
        if (offset < previous || offset > limit) {
            return 0;
        }
        previous = offset;
    }
    return inkcell_emoji_le32(offsets) == 0U;
}

/*
 * Parse and check a whole pack. Everything the rasteriser and the match later index by is
 * bounded here - a layer's path and colour, a glyph's layers, an entry's glyph and tail - so
 * that a pack which arrived as a file, and could be anything, cannot send either of them out
 * of the bytes. The one thing left to check as it is read is the path data itself, whose
 * varints the rasteriser decodes against its path's end.
 */
static int emoji_parse(const uint8_t *bytes, size_t size, struct inkcell_emoji_pack *pack) {
    if (bytes == NULL || size < EMOJI_HEADER_BYTES || memcmp(bytes, "ICEM", 4U) != 0 ||
        inkcell_emoji_le16(&bytes[4]) != EMOJI_VERSION) {
        return -EINVAL;
    }
    struct inkcell_emoji_pack p = {0};
    p.grid = inkcell_emoji_le16(&bytes[6]);
    p.colour_count = inkcell_emoji_le32(&bytes[8]);
    p.path_count = inkcell_emoji_le32(&bytes[12]);
    const uint32_t path_bytes = inkcell_emoji_le32(&bytes[16]);
    p.glyph_count = inkcell_emoji_le32(&bytes[20]);
    const uint32_t layer_count = inkcell_emoji_le32(&bytes[24]);
    p.single_count = inkcell_emoji_le32(&bytes[28]);
    p.sequence_count = inkcell_emoji_le32(&bytes[32]);
    p.tail_count = inkcell_emoji_le32(&bytes[36]);
    p.longest = bytes[40];
    if (p.grid == 0U || p.longest > INKCELL_EMOJI_LONGEST || p.glyph_count > UINT16_MAX + 1U) {
        return -EINVAL;
    }

    const uint8_t *at = bytes + EMOJI_HEADER_BYTES;
    size_t left = size - EMOJI_HEADER_BYTES;
    p.colours = emoji_take(&at, &left, (uint64_t)p.colour_count * 4U);
    p.path_offsets = emoji_take(&at, &left, ((uint64_t)p.path_count + 1U) * 4U);
    p.paths = emoji_take(&at, &left, path_bytes);
    p.layer_offsets = emoji_take(&at, &left, ((uint64_t)p.glyph_count + 1U) * 4U);
    p.layers = emoji_take(&at, &left, (uint64_t)layer_count * EMOJI_LAYER_BYTES);
    p.singles = emoji_take(&at, &left, (uint64_t)p.single_count * EMOJI_SINGLE_BYTES);
    p.sequences = emoji_take(&at, &left, (uint64_t)p.sequence_count * EMOJI_SEQUENCE_BYTES);
    p.tail = emoji_take(&at, &left, (uint64_t)p.tail_count * 4U);
    if (p.colours == NULL || p.path_offsets == NULL || p.paths == NULL || p.layer_offsets == NULL ||
        p.layers == NULL || p.singles == NULL || p.sequences == NULL || p.tail == NULL ||
        left != 0U) {
        return -EINVAL;
    }

    if (!emoji_offsets_ok(p.path_offsets, p.path_count, path_bytes) ||
        !emoji_offsets_ok(p.layer_offsets, p.glyph_count, layer_count)) {
        return -EINVAL;
    }
    for (uint32_t i = 0; i < layer_count; ++i) {
        const uint8_t *layer = &p.layers[(size_t)i * EMOJI_LAYER_BYTES];
        if (inkcell_emoji_le16(layer) >= p.path_count ||
            inkcell_emoji_le16(&layer[2]) >= p.colour_count) {
            return -EINVAL;
        }
    }
    for (uint32_t i = 0; i < p.single_count; ++i) {
        if (inkcell_emoji_le16(&p.singles[(size_t)i * EMOJI_SINGLE_BYTES + 4U]) >= p.glyph_count) {
            return -EINVAL;
        }
    }
    for (uint32_t i = 0; i < p.sequence_count; ++i) {
        const uint8_t *entry = &p.sequences[(size_t)i * EMOJI_SEQUENCE_BYTES];
        const uint32_t tail = inkcell_emoji_le32(&entry[4]);
        const uint8_t length = entry[10];
        if (inkcell_emoji_le16(&entry[8]) >= p.glyph_count || length < 2U || length > p.longest ||
            (uint64_t)tail + length - 1U > p.tail_count) {
            return -EINVAL;
        }
    }

    *pack = p;
    return 0;
}

uint32_t inkcell_emoji_generation(void) {
    return g_generation;
}

const struct inkcell_emoji_pack *inkcell_emoji_pack(void) {
    if (g_state == EMOJI_UNSET) {
        size_t size = 0U;
        const uint8_t *builtin = inkcell_emoji_builtin(&size);
        g_state =
            builtin != NULL && emoji_parse(builtin, size, &g_pack) == 0 ? EMOJI_READY : EMOJI_NONE;
    }
    return g_state == EMOJI_READY ? &g_pack : NULL;
}

int inkcell_emoji_use_pack(const void *bytes, size_t size) {
    if (bytes == NULL) {
        g_generation++;
        g_state = EMOJI_NONE;
        free(g_loaded);
        g_loaded = NULL;
        return 0;
    }
    struct inkcell_emoji_pack parsed;
    const int rc = emoji_parse(bytes, size, &parsed);
    if (rc != 0) {
        return rc;
    }
    g_generation++;
    g_pack = parsed;
    g_state = EMOJI_READY;
    if ((const uint8_t *)bytes != g_loaded) {
        free(g_loaded);
        g_loaded = NULL;
    }
    return 0;
}

int inkcell_emoji_use_builtin(void) {
    free(g_loaded);
    g_loaded = NULL;
    g_generation++;
    g_state = EMOJI_UNSET;
    return inkcell_emoji_pack() != NULL ? 0 : -ENOENT;
}

/* Generous for a pack of every emoji there is, and a refusal rather than an allocation of
   whatever a stray path names. */
#define EMOJI_FILE_MAX (16U * 1024U * 1024U)

int inkcell_emoji_load_file(const char *path) {
    size_t size = 0U;
    uint8_t *bytes = inkwell_file_read(path, EMOJI_FILE_MAX, &size);
    if (bytes == NULL) {
        return -ENOENT;
    }
    struct inkcell_emoji_pack parsed;
    const int rc = emoji_parse(bytes, size, &parsed);
    if (rc != 0) {
        free(bytes);
        return rc;
    }
    free(g_loaded);
    g_loaded = bytes;
    g_generation++;
    g_pack = parsed;
    g_state = EMOJI_READY;
    return 0;
}

bool inkcell_emoji_available(void) {
    return inkcell_emoji_pack() != NULL;
}

int inkcell_emoji_summary(struct inkcell_emoji_summary *out) {
    const struct inkcell_emoji_pack *pack = inkcell_emoji_pack();
    if (pack == NULL || out == NULL) {
        return -ENOENT;
    }
    *out = (struct inkcell_emoji_summary){.glyphs = pack->glyph_count,
                                          .singles = pack->single_count,
                                          .sequences = pack->sequence_count,
                                          .longest = pack->longest};
    return 0;
}

static uint32_t emoji_single_codepoint(const struct inkcell_emoji_pack *pack, uint32_t i) {
    return inkcell_emoji_le32(&pack->singles[(size_t)i * EMOJI_SINGLE_BYTES]);
}

static uint32_t emoji_sequence_first(const struct inkcell_emoji_pack *pack, uint32_t i) {
    return inkcell_emoji_le32(&pack->sequences[(size_t)i * EMOJI_SEQUENCE_BYTES]);
}

size_t inkcell_emoji_entry(uint32_t index, uint32_t codepoints[INKCELL_EMOJI_LONGEST],
                           uint16_t *sprite) {
    const struct inkcell_emoji_pack *pack = inkcell_emoji_pack();
    if (pack == NULL || codepoints == NULL) {
        return 0U;
    }
    if (index < pack->single_count) {
        codepoints[0] = emoji_single_codepoint(pack, index);
        if (sprite != NULL) {
            *sprite = inkcell_emoji_le16(&pack->singles[(size_t)index * EMOJI_SINGLE_BYTES + 4U]);
        }
        return 1U;
    }
    index -= pack->single_count;
    if (index >= pack->sequence_count) {
        return 0U;
    }
    const uint8_t *entry = &pack->sequences[(size_t)index * EMOJI_SEQUENCE_BYTES];
    const uint8_t *tail = &pack->tail[(size_t)inkcell_emoji_le32(&entry[4]) * 4U];
    const uint8_t length = entry[10];
    codepoints[0] = inkcell_emoji_le32(entry);
    for (uint8_t part = 1; part < length; ++part) {
        codepoints[part] = inkcell_emoji_le32(&tail[(size_t)(part - 1U) * 4U]);
    }
    if (sprite != NULL) {
        *sprite = inkcell_emoji_le16(&entry[8]);
    }
    return length;
}

/* First index in [0, count) whose key is not below `codepoint`, over a table of `width`-byte
   rows keyed by the u32 at their front: both lookup tables are bisected this way. */
static uint32_t emoji_lower_bound(const uint8_t *rows, uint32_t count, size_t width,
                                  uint32_t codepoint) {
    uint32_t low = 0;
    uint32_t high = count;
    while (low < high) {
        const uint32_t mid = low + (high - low) / 2U;
        if (inkcell_emoji_le32(&rows[(size_t)mid * width]) < codepoint) {
            low = mid + 1U;
        } else {
            high = mid;
        }
    }
    return low;
}

size_t inkcell_emoji_match(const uint32_t *codepoints, size_t count, uint16_t *sprite) {
    const struct inkcell_emoji_pack *pack = inkcell_emoji_pack();
    if (pack == NULL || codepoints == NULL || count == 0U) {
        return 0U;
    }

    /* Sequences are stored longest first within a leading codepoint, so the first one that
       fits is the greediest match: a regional-indicator pair is a flag, not two letters. */
    for (uint32_t i = emoji_lower_bound(pack->sequences, pack->sequence_count, EMOJI_SEQUENCE_BYTES,
                                        codepoints[0]);
         i < pack->sequence_count && emoji_sequence_first(pack, i) == codepoints[0]; ++i) {
        const uint8_t *entry = &pack->sequences[(size_t)i * EMOJI_SEQUENCE_BYTES];
        const uint8_t length = entry[10];
        if (length > count) {
            continue;
        }
        const uint8_t *tail = &pack->tail[(size_t)inkcell_emoji_le32(&entry[4]) * 4U];
        bool matched = true;
        for (uint8_t part = 1; part < length && matched; ++part) {
            matched = codepoints[part] == inkcell_emoji_le32(&tail[(size_t)(part - 1U) * 4U]);
        }
        if (matched) {
            if (sprite != NULL) {
                *sprite = inkcell_emoji_le16(&entry[8]);
            }
            return length;
        }
    }

    const uint32_t single =
        emoji_lower_bound(pack->singles, pack->single_count, EMOJI_SINGLE_BYTES, codepoints[0]);
    if (single < pack->single_count && emoji_single_codepoint(pack, single) == codepoints[0]) {
        if (sprite != NULL) {
            *sprite = inkcell_emoji_le16(&pack->singles[(size_t)single * EMOJI_SINGLE_BYTES + 4U]);
        }
        return 1U;
    }
    return 0U;
}

bool inkcell_emoji_is_zero_width(uint32_t codepoint) {
    return codepoint == 0x200DU ||                             /* zero-width joiner */
           (codepoint >= 0x200BU && codepoint <= 0x200FU) ||   /* zero-width space, marks */
           (codepoint >= 0xFE00U && codepoint <= 0xFE0FU) ||   /* variation selectors */
           (codepoint >= 0x1F3FBU && codepoint <= 0x1F3FFU) || /* skin tone modifiers */
           (codepoint >= 0x0300U && codepoint <= 0x036FU) ||   /* combining diacritics */
           (codepoint >= 0x20D0U && codepoint <= 0x20F0U);     /* combining symbols, keycap */
}

/*
 * Is this byte a plain printable ASCII character that cannot begin an emoji?
 *
 * '#', '*' and the digits are excluded because they lead the keycap sequences, which is the
 * only way ASCII appears at the head of the emoji table. Everything else in 0x20-0x7E is a
 * character the 5x7 font draws, so the full walk below would match nothing and hand back
 * exactly this - one codepoint, one byte.
 */
static inline bool emoji_plain_ascii(unsigned char byte) {
    return byte >= 0x20U && byte <= 0x7EU && (byte < '0' || byte > '9') && byte != '#' &&
           byte != '*';
}

struct inkcell_text_cell inkcell_text_cell_next(const char *text) {
    struct inkcell_text_cell cell = {0};
    if (text == NULL || *text == '\0') {
        return cell;
    }

    /*
     * Fast path for plain ASCII, which is nearly every character on screen.
     *
     * The general walk below decodes sixteen codepoints of lookahead and binary-searches two
     * tables for every cell, and it does that per character on every draw, measure, fit and
     * truncate. When this character cannot start an emoji and the next byte is also ASCII -
     * so no combining mark, variation selector, ZWJ or skin tone can attach to it, all of
     * which live at U+0300 and above - the answer is settled without any of that.
     */
    if (emoji_plain_ascii((unsigned char)text[0]) && (unsigned char)text[1] < 0x80U) {
        cell.codepoint = (uint32_t)(unsigned char)text[0];
        cell.bytes = 1U;
        return cell;
    }

    /* Read ahead far enough for the longest sequence the table holds. */
    uint32_t codepoints[EMOJI_MAX_LOOKAHEAD];
    size_t widths[EMOJI_MAX_LOOKAHEAD];
    size_t count = 0;
    size_t offset = 0;
    while (count < EMOJI_MAX_LOOKAHEAD) {
        const size_t step = inkwell_text_utf8_next(&text[offset], &codepoints[count]);
        if (step == 0U) {
            break;
        }
        widths[count++] = step;
        offset += step;
    }

    /*
     * Match against the codepoints with the variation selectors taken out, and remember where
     * each survivor came from.
     *
     * The font spells its keycap ligature U+0039 U+20E3, but the keycap as people actually
     * type it is U+0039 U+FE0F U+20E3 - the selector in the middle is what makes it emoji
     * rather than a digit. Matching the raw sequence misses every one of them.
     */
    uint32_t filtered[EMOJI_MAX_LOOKAHEAD];
    size_t source[EMOJI_MAX_LOOKAHEAD];
    size_t filtered_count = 0;
    for (size_t i = 0; i < count; ++i) {
        if (codepoints[i] >= 0xFE00U && codepoints[i] <= 0xFE0FU) {
            continue;
        }
        filtered[filtered_count] = codepoints[i];
        source[filtered_count] = i;
        filtered_count++;
    }

    /* A stray selector with nothing to attach to: hand back a blank cell rather than a box. */
    if (filtered_count == 0U) {
        cell.codepoint = (uint32_t)' ';
        for (size_t i = 0; i < count; ++i) {
            cell.bytes += widths[i];
        }
        return cell;
    }

    uint16_t sprite = 0;
    size_t matched = inkcell_emoji_match(filtered, filtered_count, &sprite);
    /*
     * The emoji font claims '#', '*' and the ten digits, because those lead the keycap
     * sequences. Letting it have them turns the 9 of "Dog Tracker K9" into a tiny grey keycap
     * digit, so a single codepoint the text font can draw is drawn by the text font.
     *
     * A sequence always wins: the extra codepoints are precisely what says "this is the
     * emoji, not the character".
     */
    if (matched == 1U && inkcell_font5x7_has_glyph(filtered[0])) {
        matched = 0U;
    }

    /* Consume through the last codepoint the match covered, selectors between them included. */
    size_t consumed;
    if (matched > 0U) {
        cell.is_emoji = true;
        cell.sprite = sprite;
        consumed = source[matched - 1U] + 1U;
    } else {
        cell.codepoint = filtered[0];
        consumed = source[0] + 1U;
    }

    /* Whatever follows and has no width of its own belongs to this cell too: a skin tone or
       selector left over from a sequence the table does not have, or a combining mark. */
    while (consumed < count && inkcell_emoji_is_zero_width(codepoints[consumed])) {
        consumed++;
    }
    for (size_t i = 0; i < consumed; ++i) {
        cell.bytes += widths[i];
    }

    return cell;
}

size_t inkcell_text_cells(const char *text) {
    if (text == NULL) {
        return 0U;
    }
    size_t cells = 0;
    size_t offset = 0;
    for (;;) {
        const struct inkcell_text_cell cell = inkcell_text_cell_next(&text[offset]);
        if (cell.bytes == 0U) {
            break;
        }
        offset += cell.bytes;
        cells++;
    }
    return cells;
}

size_t inkcell_text_cell_offset(const char *text, size_t index) {
    if (text == NULL) {
        return 0U;
    }
    size_t offset = 0;
    for (size_t i = 0; i < index; ++i) {
        const struct inkcell_text_cell cell = inkcell_text_cell_next(&text[offset]);
        if (cell.bytes == 0U) {
            break;
        }
        offset += cell.bytes;
    }
    return offset;
}

void inkcell_text_cell_truncate(char *text, size_t cells) {
    if (text == NULL) {
        return;
    }
    text[inkcell_text_cell_offset(text, cells)] = '\0';
}
