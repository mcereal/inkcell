/*
 * The UI face's descriptor: a lookup into the generated table, and the geometry above it.
 *
 * There is almost nothing here because there is almost nothing to decide - the shapes were
 * decided by scripts/gen-font.py, and the metrics are a table entry. That is the point of the
 * font seam: a second face is data plus a descriptor, not a renderer.
 */

#include "inkcell/ui/font_ui.h"

#include <string.h>

/* The glyph `codepoint` is at, or the table's count when it has none. Bisection rather than a
   scan: a full screen of body text is several hundred lookups a frame. */
static uint32_t font_ui_find_in(const struct inkcell_font_ui_table *table, uint32_t codepoint) {
    uint32_t low = 0U;
    uint32_t high = table->count;
    while (low < high) {
        const uint32_t mid = low + (high - low) / 2U;
        if (table->glyphs[mid].codepoint < codepoint) {
            low = mid + 1U;
        } else {
            high = mid;
        }
    }
    return (low < table->count && table->glyphs[low].codepoint == codepoint) ? low : table->count;
}

/* One glyph's advance, in master columns - the lookup without the unpack. A string is measured
   far more often than it is drawn, so this must not touch the coverage. */
static uint8_t font_ui_advance_in(const struct inkcell_font_ui_table *table, uint32_t codepoint) {
    const uint32_t index = font_ui_find_in(table, codepoint);
    return index < table->count ? table->glyphs[index].advance : 0U;
}

/*
 * The replacement box: a hollow rectangle over the cap height, the "tofu" a reader already
 * knows means "a character this font does not have".
 *
 * Drawn rather than stored, because it is a rectangle and storing it would be a glyph in the
 * table that no codepoint maps to. The proportions are the 5x7 tofu's, scaled: a box inset from
 * the cell's sides, standing on the baseline and as tall as a capital.
 */
static void font_ui_tofu(const struct inkcell_font_ui_table *table, uint8_t *out) {
    const int master_w = (int)table->master_w;
    const int inset = (int)table->nominal / 8;
    const int left = inset;
    /* Bounded by the *nominal* advance rather than the master, because the master of a
       proportional face is as wide as its widest glyph and a box drawn to that would be a
       tofu the width of a 'W' wherever a character is missing. */
    const int right = (int)table->nominal - inset - 1;
    const int top = INKCELL_FONT_UI_MASTER_TOP + 4;
    const int bottom = INKCELL_FONT_UI_MASTER_H - 5;
    if (right <= left) {
        return;
    }
    const int stroke = 2;
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            const bool edge =
                y < top + stroke || y > bottom - stroke || x < left + stroke || x > right - stroke;
            if (edge) {
                out[(size_t)y * (size_t)master_w + (size_t)x] = INKCELL_GLYPH_MAX_ALPHA;
            }
        }
    }
}

/* Unpack one glyph's ink box into its place in the master, which inkcell_font_glyph() has
   already cleared. A glyph with no ink - a space - writes nothing and needs no test for it. */
static bool font_ui_glyph_in(const struct inkcell_font_ui_table *table, uint32_t codepoint,
                             struct inkcell_glyph *out) {
    const uint32_t index = font_ui_find_in(table, codepoint);
    if (index >= table->count) {
        font_ui_tofu(table, out->alpha);
        return false;
    }

    const struct inkcell_font_ui_glyph *glyph = &table->glyphs[index];
    for (uint32_t row = 0U; row < glyph->h; ++row) {
        uint8_t *dst = &out->alpha[((size_t)glyph->y + row) * table->master_w + glyph->x];
        for (uint32_t col = 0U; col < glyph->w; ++col) {
            const size_t at = (size_t)row * glyph->w + col;
            const uint8_t packed = table->pixels[glyph->offset + at / 2U];
            dst[col] = (at & 1U) != 0U ? (uint8_t)(packed >> 4) : (uint8_t)(packed & 0x0FU);
        }
    }
    return true;
}

/* The hooks, one pair per weight: the glyph hook carries no context, so the table a face reads
   from is which function it points at. */
static bool font_ui_glyph(uint32_t codepoint, struct inkcell_glyph *out) {
    return font_ui_glyph_in(&inkcell_font_ui_table, codepoint, out);
}
static bool font_ui_has_glyph(uint32_t codepoint) {
    return font_ui_find_in(&inkcell_font_ui_table, codepoint) < inkcell_font_ui_table.count;
}
static uint8_t font_ui_advance(uint32_t codepoint) {
    return font_ui_advance_in(&inkcell_font_ui_table, codepoint);
}

static bool font_ui_bold_glyph(uint32_t codepoint, struct inkcell_glyph *out) {
    return font_ui_glyph_in(&inkcell_font_ui_bold_table, codepoint, out);
}
static bool font_ui_bold_has_glyph(uint32_t codepoint) {
    return font_ui_find_in(&inkcell_font_ui_bold_table, codepoint) <
           inkcell_font_ui_bold_table.count;
}
static uint8_t font_ui_bold_advance(uint32_t codepoint) {
    return font_ui_advance_in(&inkcell_font_ui_bold_table, codepoint);
}

/*
 * Fill in everything the generator measured, which is everything that differs between two
 * weights of the same family.
 *
 * The metrics a face does *not* get to choose are the cell and the line: both weights are
 * rasterised into the same box on the same baseline, so a heading set in one and a body set in
 * the other sit on the same grid and a row does not change height when its title goes bold.
 */
static void font_ui_fill(struct inkcell_font *font, const struct inkcell_font_ui_table *table) {
    font->cap_rows = table->cap_rows;
    font->master_w = table->master_w;
    font->master_left = table->master_left;
    font->proportional = table->proportional;
    font->name = table->name;
    /* Both forms of the nominal advance: master columns, which is what every measurement goes
       through, and the rounded pixel fallback. */
    font->nominal = table->nominal;
    font->width = (uint8_t)((table->nominal + INKCELL_FONT_UI_MASTER_SCALE / 2) /
                            INKCELL_FONT_UI_MASTER_SCALE);
}

const struct inkcell_font *inkcell_font_ui(void) {
    /*
     * Not `static const`, unlike 5x7's, for one field: the cap height is a property of the face
     * the generator rasterised, so it travels with the generated data rather than being a
     * constant here that a regeneration could silently disagree with. Copying it on every call
     * is cheaper than the branch an initialised flag would need.
     */
    /*
     * No gap between cells, one row between lines.
     *
     * 5x7 asks for a column of gap because its glyphs fill their cell edge to edge; this face
     * carries its own sidebearings, and a 20 px cell holds 17 px of ink, so a gap on top of
     * that spaces the letters like a ransom note. The cell is the advance instead, which is
     * what a monospace face means by one.
     *
     * The line is 36 px at the body scale either way - the same rows per screen as 5x7 - because
     * the cell took the row the accent gap used to need: the diacritics live in the overhang
     * now rather than in the space between lines.
     */
    static struct inkcell_font font = {
        .id = "ui",
        .height = INKCELL_FONT_UI_CELL_H,
        .advance_gap = 0U,
        .line_gap = 1U,
        .master_h = INKCELL_FONT_UI_MASTER_H,
        .master_top = INKCELL_FONT_UI_MASTER_TOP,
        /* Four master units to the pixel: the generator rasterises at the size the body scale
           draws, so on the device a glyph is blitted 1:1. */
        .master_scale = INKCELL_FONT_UI_MASTER_SCALE,
        .sampling = INKCELL_FONT_SMOOTH,
        .glyph = font_ui_glyph,
        .has_glyph = font_ui_has_glyph,
        .advance = font_ui_advance,
    };
    /* Everything the generator measured off the face travels with the generated data rather
       than being restated here, where a regeneration could silently disagree with it. */
    font_ui_fill(&font, &inkcell_font_ui_table);
    return &font;
}

/*
 * The same family, heavier.
 *
 * A second weight rather than a second family, and the reason it earns its 57 KB is that it is
 * the only way this UI has to say "this line matters more" without saying it in colour. Size
 * alone is a coarse instrument here - the scale steps in whole glyph multipliers, so the next
 * one up is a quarter bigger - and colour was carrying the whole job, which is why every screen
 * used to read as though it were shouting. A title in the heavier weight at the same size is
 * the ordinary answer, and the one an iOS or Material screen gives.
 *
 * Not in the font registry: the registry is the menu a theme picks a *typeface* from, and a
 * weight is not a typeface. It is reached through inkcell_font_ui_bold().
 */
const struct inkcell_font *inkcell_font_ui_bold(void) {
    static struct inkcell_font font = {
        .id = "ui-bold",
        .height = INKCELL_FONT_UI_CELL_H,
        .advance_gap = 0U,
        .line_gap = 1U,
        .master_h = INKCELL_FONT_UI_MASTER_H,
        .master_top = INKCELL_FONT_UI_MASTER_TOP,
        .master_scale = INKCELL_FONT_UI_MASTER_SCALE,
        .sampling = INKCELL_FONT_SMOOTH,
        .glyph = font_ui_bold_glyph,
        .has_glyph = font_ui_bold_has_glyph,
        .advance = font_ui_bold_advance,
    };
    font_ui_fill(&font, &inkcell_font_ui_bold_table);
    return &font;
}
