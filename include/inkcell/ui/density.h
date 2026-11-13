#ifndef INKCELL_UI_DENSITY_H
#define INKCELL_UI_DENSITY_H

/*
 * Density: how many of a panel's pixels make a length a person can see.
 *
 * A scale is a pixel count in disguise. INKCELL_SCALE(4) was chosen on the Brick's 3.2" panel,
 * where it makes body text about 3 mm tall; the same scale on a 6.3" phone at 416 pixels to the
 * inch is under 2 mm, and on a 100 ppi monitor it is 8. A layout written in pixels is right on
 * exactly one panel. The fix every mobile toolkit arrived at is a unit that is a length rather
 * than a pixel - Android's dp, iOS's point - converted to pixels once, by the panel's density.
 *
 * The unit here is Android's: one dp is 1/160 of an inch. Not because inkcell is Android, but
 * because it is the unit every phone designer already measures in, so a 48 dp touch target or a
 * 16 dp margin read off any design spec means the same thing in an inkcell layout.
 *
 * A density of 0 is "unknown", and unknown is not 160: it is "keep drawing in pixels, as before".
 * The fb backend on the Brick has no millimetres to read, and a panel whose size nobody stated is
 * better drawn at the scale its theme was tuned on than at one guessed from a nominal default.
 * So at 0 a dp is a pixel and a theme's scale is the theme's - every existing panel draws
 * exactly what it did.
 *
 * Integer arithmetic throughout, rounded to nearest: a length is a design decision, and a layout
 * that drifts a pixel between two calls with the same arguments is one nobody can pin in a test.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The density at which one dp is one pixel: 160 to the inch, Android's baseline. */
#define INKCELL_DP_PPI 160U

/*
 * The densities a panel's own report is believed between.
 *
 * KMS states a connector's size in millimetres from the EDID or the panel driver, and not every
 * one tells the truth: a projector says 0, some monitors say 16x9 (the aspect ratio) or 160x90,
 * and a virtual GPU makes a size up. Outside this range the report is ignored and the density
 * stays unknown. The ceiling is past any phone shipped; the floor is below any monitor.
 */
#define INKCELL_PPI_MIN 60U
#define INKCELL_PPI_MAX 1200U

/*
 * `dp` in pixels at `ppi`, rounded to nearest. At ppi 0 a dp is a pixel.
 *
 * Never to nothing where something was asked for: a 1 dp hairline on a 120 ppi panel is still a
 * hairline, the same promise inkcell_scale_px() makes. Negative lengths - an offset up or left -
 * round the same way their magnitude would.
 */
static inline int inkcell_dp_px(int dp, uint32_t ppi) {
    if (ppi == 0U || dp == 0) {
        return dp;
    }
    const int64_t magnitude = dp < 0 ? -(int64_t)dp : (int64_t)dp;
    int64_t px = (magnitude * ppi + INKCELL_DP_PPI / 2U) / INKCELL_DP_PPI;
    if (px == 0) {
        px = 1;
    }
    return (int)(dp < 0 ? -px : px);
}

/*
 * A panel's density from its width in pixels and in millimetres, or 0 when the millimetres are
 * missing or not believable (see INKCELL_PPI_MIN). Either axis will do; KMS reports both and
 * panels have square pixels.
 */
uint32_t inkcell_ppi_from_mm(uint32_t px, uint32_t mm);

/*
 * The scale, in scale units, that draws a cell `cell_steps` whole steps tall at `dp` dp on a
 * `ppi` panel - the nearest quarter step, unclamped. 0 when the density is unknown, which a
 * caller reads as "use the theme's scale".
 *
 * Why the cell and not the line: the cell is the font's em, the size a type designer means by
 * "16 sp body", and it is the same for every font the theme might name. The line adds the
 * font's own gap, which differs between faces and is the font's business.
 */
int inkcell_scale_for_dp(int dp, uint32_t ppi, uint32_t cell_steps);

#ifdef __cplusplus
}
#endif

#endif
