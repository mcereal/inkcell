/*
 * The font registry and the metrics every measurement in the UI derives from.
 *
 * Two fonts: a rasterised monospace face and the 5x7 pixel one. A font is the half of "themes"
 * that a palette does not cover - a 1-bit 5x7 cell is an aesthetic that survives any number of
 * Material components laid over it - so which face a theme draws in is a property of the theme,
 * resolved here.
 *
 * Index order is menu order and slot 0 is the default, which is why the rasterised face is
 * first. The registry is a switch rather than a table because a descriptor comes from a
 * function call, which C will not let a static table hold.
 */

#include "inkcell/ui/font.h"

#include "inkcell/ui/font5x7.h"
#include "inkcell/ui/font_ui.h"

#include <string.h>

/* Every accessor goes through this, so "there is always a font" lives in one place. */
static const struct inkcell_font *font_slot(size_t index) {
    switch (index) {
    case 0:
        return inkcell_font_ui();
    case 1:
        return inkcell_font5x7();
    default:
        return NULL;
    }
}

size_t inkcell_font_count(void) {
    return 2U;
}

const struct inkcell_font *inkcell_font_at(size_t index) {
    return font_slot(index);
}

const struct inkcell_font *inkcell_font_default(void) {
    return font_slot(0U);
}

const struct inkcell_font *inkcell_font_by_id(const char *id) {
    if (id == NULL || id[0] == '\0') {
        return NULL;
    }
    for (size_t i = 0; i < inkcell_font_count(); ++i) {
        const struct inkcell_font *font = font_slot(i);
        if (font != NULL && font->id != NULL && strcmp(font->id, id) == 0) {
            return font;
        }
    }
    return NULL;
}

const struct inkcell_font *inkcell_font_at_weight(const struct inkcell_font *font,
                                                  enum inkcell_weight weight) {
    if (font == NULL) {
        font = inkcell_font_default();
    }
    if (weight != INKCELL_WEIGHT_STRONG || font == NULL || font->id == NULL) {
        return font;
    }
    /* By identity rather than by a pointer on the descriptor: a face's heavier cut is a fact
       about the family, and the registry is the one place that knows which files are one. */
    if (strcmp(font->id, "ui") == 0) {
        return inkcell_font_ui_bold();
    }
    return font;
}

static const struct inkcell_font *font_or_default(const struct inkcell_font *font) {
    return font != NULL ? font : inkcell_font_default();
}

int inkcell_font_advance(const struct inkcell_font *font, int scale) {
    font = font_or_default(font);
    if (font == NULL || scale <= 0) {
        return 1;
    }
    /* Through the master where the face states one, so the divide happens after the multiply -
       see `nominal`. `width` is the answer for a face that has not got one. */
    if (font->nominal > 0U && font->master_scale > 0U) {
        const int advance =
            (int)font->nominal * scale / ((int)font->master_scale * INKCELL_SCALE_UNIT) +
            inkcell_scale_px((int)font->advance_gap, scale);
        return advance > 0 ? advance : 1;
    }
    return inkcell_scale_px((int)font->width + (int)font->advance_gap, scale);
}

/*
 * One glyph's advance, in pixels.
 *
 * The master-to-pixel conversion is the whole of it: a face stores its advances at the
 * resolution it rasterised at (`master_scale` units to the pixel), so the multiply happens
 * before the divide and a half-pixel advance survives into the sum of a whole string rather
 * than being rounded away per character. That is what keeps a measured line the same width as
 * the drawn one - the two walk the same arithmetic.
 *
 * The letter-spacing gap is added after, unscaled by the master, because it is a property of
 * how the face is *set* rather than of the glyph.
 */
int inkcell_font_advance_cp(const struct inkcell_font *font, uint32_t codepoint, int scale) {
    font = font_or_default(font);
    if (font == NULL || scale <= 0) {
        return 1;
    }
    if (!font->proportional || font->advance == NULL) {
        return inkcell_font_advance(font, scale);
    }
    const int units = (int)font->advance(codepoint);
    if (units <= 0) {
        /* No advance of its own - a codepoint the face has no glyph for, drawn as the
           replacement box, which is sized to the nominal cell. */
        return inkcell_font_advance(font, scale);
    }
    const int per_px = font->master_scale > 0U ? (int)font->master_scale : 1;
    const int advance = units * scale / (per_px * INKCELL_SCALE_UNIT) +
                        inkcell_scale_px((int)font->advance_gap, scale);
    return advance > 0 ? advance : 1;
}

int inkcell_font_line(const struct inkcell_font *font, int scale) {
    font = font_or_default(font);
    if (font == NULL || scale <= 0) {
        return 1;
    }
    return inkcell_scale_px((int)font->height + (int)font->line_gap, scale);
}

/*
 * Clearing only the master the font actually uses, rather than the whole struct.
 *
 * `struct inkcell_glyph` is sized for the largest master any font may declare, and a full
 * frame of body text is several hundred of these calls - zeroing the unused tail of every one
 * would be most of the work of drawing a character. The font fills the rest.
 */
/*
 * The capitals' height in pixels, from the master rows they occupy.
 *
 * The cell is `master_h - master_top` master rows and is drawn inkcell_scale_px(height, scale)
 * pixels tall, so a cap is that many pixels per master row times the rows it stands. For 5x7 the
 * arithmetic cancels back to the cell height, which is what it always was.
 */
int inkcell_font_cap(const struct inkcell_font *font, int scale) {
    font = font_or_default(font);
    if (font == NULL || scale <= 0) {
        return 1;
    }
    const int cell_rows = (int)font->master_h - (int)font->master_top;
    if (cell_rows <= 0 || font->cap_rows == 0U) {
        return inkcell_scale_px((int)font->height, scale);
    }
    const int cap =
        (int)font->cap_rows * (int)font->height * scale / (cell_rows * INKCELL_SCALE_UNIT);
    return cap > 0 ? cap : 1;
}

bool inkcell_font_glyph(const struct inkcell_font *font, uint32_t codepoint,
                        struct inkcell_glyph *out) {
    if (out == NULL) {
        return false;
    }
    font = font_or_default(font);
    if (font == NULL || font->glyph == NULL) {
        memset(out, 0, sizeof *out);
        return false;
    }
    memset(out->alpha, 0, (size_t)font->master_w * (size_t)font->master_h);
    return font->glyph(codepoint, out);
}

bool inkcell_font_has_glyph(const struct inkcell_font *font, uint32_t codepoint) {
    font = font_or_default(font);
    if (font == NULL || font->has_glyph == NULL) {
        return false;
    }
    return font->has_glyph(codepoint);
}
