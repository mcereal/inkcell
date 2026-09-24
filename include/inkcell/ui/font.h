#ifndef INKCELL_FONT_H
#define INKCELL_FONT_H

/*
 * The font seam.
 *
 * A renderer asks a font how wide a cell is and what a codepoint looks like; it never names
 * 5x7. That is the whole of what a second font needs from this layer - drop in another
 * `struct inkcell_font` and every measurement above it (columns per line, button widths,
 * bubble heights, the scroll window) follows, because all of them derive from
 * inkcell_font_advance()/inkcell_font_line() rather than from a constant.
 *
 * A glyph is *coverage*, not a bitmask: `alpha` holds one value per master pixel, 0 for
 * nothing and INKCELL_GLYPH_MAX_ALPHA for solid, exactly as an icon sprite does. A 1-bit
 * mask is what welds the UI to a retro look - at the body scale one source pixel is a 4x4
 * block, and no amount of Material chrome survives text made of visible squares. Coverage
 * lets a face rasterised from a real outline keep its curves, and costs the pixel-art font
 * nothing: 5x7 stores 0 or 15 and draws exactly the spans it always drew.
 *
 * The master is resampled into the cell the theme asks for, which is why a font declares
 * both its cell (`width`/`height`, in scale steps, what every measurement is derived from)
 * and its master (`master_w`/`master_h`, the resolution the coverage is actually stored at).
 * For 5x7 the two are the same and the sampling is nearest, so a glyph is block-replicated
 * the way it always was. For a rasterised face the master is bigger than the cell at most
 * scales and the sampling is bilinear, the same trade inkcell_fb_draw_icon() already makes: nearest
 * neighbour on a 2 px stroke is the difference between a smooth diagonal and a staircase.
 *
 * The overhang - `master_top` - is where a diacritic goes when the cell has no room for it.
 * Every font this small has the problem: 5x7's capitals fill all seven of its rows, and a face
 * rasterised to fill its cell puts an acute above the cap height by definition. So the master
 * is allowed to be taller than the cell, its top `master_top` rows are drawn in the gap the
 * line advance leaves above, and a font sizes that gap by its own `line_gap`. One mechanism
 * for both fonts, rather than the single hard-coded accent row this replaced.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The unit a scale is stated in: how many scale units make one whole glyph step.
 *
 * A scale was a whole multiplier over the font's cell once, and the type scale was three roles
 * because that is all a whole multiplier can say. On this panel the usable range is [2, 6], so
 * a type scale held in whole steps has five sizes in it - and a vocabulary with more roles than
 * that is a set of synonyms, which is exactly why INKCELL_TYPE_COUNT was three.
 *
 * So a scale is now counted in *quarters* of a step. Nothing about the rendering needed to
 * change for it: a glyph is coverage stored at a master four times the cell it is drawn in and
 * resampled bilinearly into whatever box is asked for, so the sizes between the old whole steps
 * were always drawable - there was simply no way to name one. INKCELL_SCALE(n) is the old
 * multiplier n, so a theme, a widget or a test that meant "four" says INKCELL_SCALE(4) and gets
 * the pixels it always got.
 *
 * Four rather than two or eight. Two is not enough to place Material's roles between the body
 * and the smallest label without two of them landing together; eight buys sizes a 3.2" panel
 * cannot tell apart, and every metric derived from a scale - a radius, a gap, a card's inset -
 * is an integer divide by this, so a needlessly fine unit is needlessly lossy arithmetic.
 *
 * Everything measured *from* a scale divides by this on the way to pixels. That is the one rule
 * to keep in mind when adding a metric: a scale is not a pixel count and never was, but the
 * factor between them used to be 1.
 */
#define INKCELL_SCALE_UNIT 4

/* A whole glyph step, in scale units - what a scale written before the unit existed meant. */
#define INKCELL_SCALE(steps) ((steps) * INKCELL_SCALE_UNIT)

/*
 * `steps` whole glyph steps, in pixels, at a scale stated in units.
 *
 * The one conversion every metric derived from a scale makes, in one place so that the divide
 * cannot be forgotten at a call site - which would read as a plausible number four times too
 * large. Rounded down, and never to nothing where something was asked for: a gap a theme stated
 * is a gap, and a hairline rounded away is a hairline somebody removed.
 */
static inline int inkcell_scale_px(int steps, int scale) {
    if (steps <= 0 || scale <= 0) {
        return 0;
    }
    const int px = steps * scale / INKCELL_SCALE_UNIT;
    return px > 0 ? px : 1;
}

/*
 * One whole step, in pixels.
 *
 * The most common conversion in the drawing code, and the one that used to need no conversion
 * at all: the hairline inset a row fill stands off by, the lift a baseline takes over a
 * highlight, the pixel a keycap is shrunk by. Every one of those was spelled `scale`, because
 * while a scale was a whole multiplier the step and the scale were the same number. They are
 * not any more, and a bare scale in a pixel expression is now four times what it used to be.
 */
static inline int inkcell_step_px(int scale) {
    return inkcell_scale_px(1, scale);
}

/* The largest *cell* this layer will render, in pixels per whole step. Buffers and bounds sized
   off these are the reason a font cannot simply declare any size it likes; raise them when a
   font needs it. */
#define INKCELL_GLYPH_MAX_WIDTH 8
#define INKCELL_GLYPH_MAX_HEIGHT 16

/* The largest coverage *master* a font may store a glyph at. Independent of the cell: a face
   is rasterised once at a resolution that survives being drawn small, and the cell it lands
   in is whatever the theme's scale works out to.

   Wide enough for a proportional face's widest advance, not just a monospace cell: where a
   mono master is the cell and nothing exceeds it, 'W' and 'm' in a proportional face are half
   again the nominal advance, and the master has to hold the widest of them. */
#define INKCELL_GLYPH_MASTER_MAX_WIDTH 40
#define INKCELL_GLYPH_MASTER_MAX_HEIGHT 40

/* Solid. Coverage runs 0..this inclusive, 4 bits, the same range an icon sprite carries. */
#define INKCELL_GLYPH_MAX_ALPHA 15

/*
 * How a master is resampled into the cell.
 *
 * Not a preference: pixel art scaled up bilinearly reads as a smudge, and an outline scaled
 * down with nearest neighbour drops every fourth row. A font knows which of the two it is,
 * so it says so rather than leaving the renderer to guess from its dimensions.
 */
enum inkcell_font_sampling {
    INKCELL_FONT_PIXEL = 0, /* nearest neighbour: block-replicated, edges stay hard */
    INKCELL_FONT_SMOOTH,    /* bilinear: an outline keeps its curves at any cell size */
};

/* One character's coverage, row-major over the font's master. */
struct inkcell_glyph {
    uint8_t alpha[INKCELL_GLYPH_MASTER_MAX_WIDTH * INKCELL_GLYPH_MASTER_MAX_HEIGHT];
};

/*
 * A bitmap font as the UI sees it.
 *
 * `advance_gap` and `line_gap` are in pixels *per whole step*, so a font keeps its proportions
 * when the glyph multiplier changes: the 5x7 font asks for one column of gap and two rows,
 * which at INKCELL_SCALE(4) is the 4 px and 8 px the Brick's panel has always drawn. Every
 * field here counting pixels counts them per whole step, not per scale unit - the conversion
 * is inkcell_scale_px() and it happens once, in the accessors below.
 */
struct inkcell_font {
    const char *id;   /* what a theme names it by */
    const char *name; /* what a menu would show */
    /*
     * The *nominal* advance, in pixels per whole step.
     *
     * For a monospace face this is the advance, full stop: every cell is this wide and
     * inkcell_font_advance() is exact. For a proportional one it is the width the layout
     * *estimates* with - a column count, a wrap guess, the fallback when no codepoint is in
     * hand - and the real advance comes per glyph from inkcell_font_advance_cp(). Sized to the
     * face's average lowercase advance rather than its widest, so an estimate is wrong in both
     * directions rather than always short.
     */
    uint8_t width;
    /*
     * The same nominal advance, in master columns - which is where the precision is.
     *
     * `width` is pixels per whole step, and at that size a proportional face's average advance is
     * about four pixels: rounding it there throws away a quarter of it before the scale is
     * applied, which at the body scale is three pixels of every cell and at an icon's scale is
     * twenty. Stating it in master units and dividing at the end keeps it exact.
     *
     * Zero means the face has not got one and `width` is the answer, which is every monospace
     * font and anything written before this field.
     */
    uint8_t nominal;
    uint8_t height; /* cell height in pixels per whole step */
    uint8_t advance_gap;
    uint8_t line_gap;
    uint8_t master_w; /* coverage master width; the cell width when the font is pixel art */
    uint8_t master_h; /* coverage master height, overhang rows included */
    /*
     * Master units to one pixel per whole step - the resolution the coverage is stored at, relative
     * to the size the cell is drawn.
     *
     * A per-glyph advance is stored in master columns, and this is what turns one into pixels:
     * at scale S a glyph steps `advance * S / master_scale`. 5x7 stores its master at the cell,
     * so it is 1 and the arithmetic cancels; the rasterised faces store four master units to
     * the pixel, which is what lets a proportional advance land on a quarter of a pixel rather
     * than being rounded to a whole one before the scale is applied.
     *
     * Zero is read as 1, so a font that predates this field keeps its old measurements.
     */
    uint8_t master_scale;
    /*
     * Whether the advance varies per glyph.
     *
     * Not cosmetic: it is the difference between a layout that may multiply a column count by
     * the nominal advance and one that has to measure the string. Anything that lays out against
     * a grid asks this before assuming one.
     */
    bool proportional;
    /* How many of the master's top rows hang *above* the cell rather than inside it.
       See the note on the overhang below. */
    uint8_t master_top;
    /*
     * How many of the master's left columns sit *before* the pen.
     *
     * The horizontal mirror of the overhang, and there for the same reason: the accent on a
     * narrow letter is centred on its stem and wider than it, so the circumflex of an 'i'
     * reaches left of the origin its advance is measured from. The renderer draws the master
     * this far before the pen, so that ink lands where the face put it and the advance stays
     * the advance. Zero for a face whose glyphs all start at the origin, which is every
     * monospace one.
     */
    uint8_t master_left;
    /*
     * How many master rows the capitals stand on the baseline, which is not the cell.
     *
     * Anything sized to match the text - an icon in a row slot, above all - wants the height of
     * a capital beside it, and for 5x7 that is the cell, because its capitals fill it. A face
     * with real ascenders and descenders keeps both inside the cell, so its capitals are a good
     * bit shorter than it - and an icon sized off the cell there comes out overbearing.
     */
    uint8_t cap_rows;
    enum inkcell_font_sampling sampling;
    /* Fills `out` with the glyph for `codepoint`. Returns false when it fell back to the
       replacement box, which is still a drawable glyph. */
    bool (*glyph)(uint32_t codepoint, struct inkcell_glyph *out);
    /* Whether the font has a real glyph for `codepoint`, without building it. */
    bool (*has_glyph)(uint32_t codepoint);
    /*
     * How far `codepoint` steps, in master columns. NULL means every glyph steps the nominal
     * advance, which is what a monospace face is.
     *
     * Separate from `glyph` rather than a field on the coverage it returns, because measuring
     * a string must not decode one: a line of body text is measured several times for every
     * time it is drawn - to fit it, to centre it, to right-align what follows - and unpacking
     * three hundred ink boxes to add up their widths would cost more than the frame.
     */
    uint8_t (*advance)(uint32_t codepoint);
};

/*
 * The *nominal* advance, and the line height, in pixels.
 *
 * For a proportional font this is an estimate - see `width`. Anything laying out real text
 * measures it with inkcell_font_advance_cp() or, above this layer, inkcell_fb_text_width().
 */
int inkcell_font_advance(const struct inkcell_font *font, int scale);
int inkcell_font_line(const struct inkcell_font *font, int scale);

/*
 * What `codepoint` actually steps, in pixels.
 *
 * The monospace answer for a monospace face, and for a proportional one the face's own
 * advance for that character. A codepoint the font has no glyph for steps the nominal advance,
 * which is what the replacement box is drawn at.
 */
int inkcell_font_advance_cp(const struct inkcell_font *font, uint32_t codepoint, int scale);

/* How tall a capital is drawn, in pixels - what anything standing beside the text matches. */
int inkcell_font_cap(const struct inkcell_font *font, int scale);

/*
 * What one digit steps when the digits are being set in a column - the widest of the ten.
 *
 * A proportional face draws its figures to fit: in the UI face a '1' is two thirds the width of
 * a '4'. Inside a word that is right and invisible; in a reading that updates, or in a column
 * of them, it means the number moves sideways as its digits change and two rows do not line up.
 * A style that says so (struct inkcell_type_style's `tabular`) steps every digit this far and
 * centres the glyph in it, which is what a face's own tabular figures would do if this one had
 * a second set of them.
 */
int inkcell_font_digit_advance(const struct inkcell_font *font, int scale);

/* Safe wrappers: a NULL font, or one missing a hook, resolves to the default. */
bool inkcell_font_glyph(const struct inkcell_font *font, uint32_t codepoint,
                        struct inkcell_glyph *out);
bool inkcell_font_has_glyph(const struct inkcell_font *font, uint32_t codepoint);

/*
 * How heavily text is set.
 *
 * Two, because two is what this panel can tell apart: a face rasterised into a 20-pixel cell
 * has room for one clear step of weight and no more, and a scale of four that a reader cannot
 * distinguish is a scale of one with extra data. REGULAR is body text and everything that has
 * not said otherwise; STRONG is the line the eye should land on first.
 *
 * A weight, not a colour and not a size. That is the point of having it: hierarchy was being
 * carried entirely by palette here, which is why a screen of headings, values and states read
 * as though every line were competing with its neighbours.
 */
enum inkcell_weight {
    INKCELL_WEIGHT_REGULAR = 0,
    INKCELL_WEIGHT_STRONG,
};

/*
 * `font` at `weight` - the same family, set heavier.
 *
 * A face with no heavier cut answers with itself, which is what the pixel font does: 5x7 has
 * one weight and emboldening it by smearing a column would turn its letters into blocks.
 */
const struct inkcell_font *inkcell_font_at_weight(const struct inkcell_font *font,
                                                  enum inkcell_weight weight);

/* The registry a theme's `font_id` is resolved against. */
size_t inkcell_font_count(void);
const struct inkcell_font *inkcell_font_at(size_t index);
const struct inkcell_font *inkcell_font_by_id(const char *id); /* NULL when unknown */
const struct inkcell_font *inkcell_font_default(void);

#ifdef __cplusplus
}
#endif

#endif /* INKCELL_FONT_H */
