/*
 * Density: lengths in dp, panels in pixels. See include/inkcell/ui/density.h.
 */
#include "inkcell/ui/density.h"

#include "inkcell/ui/font.h"

uint32_t inkcell_ppi_from_mm(uint32_t px, uint32_t mm) {
    if (px == 0U || mm == 0U) {
        return 0U;
    }
    /* 25.4 mm to the inch, in tenths so the divide stays integral; rounded to nearest. */
    const uint64_t ppi = ((uint64_t)px * 254U + (uint64_t)mm * 5U) / ((uint64_t)mm * 10U);
    if (ppi < INKCELL_PPI_MIN || ppi > INKCELL_PPI_MAX) {
        return 0U;
    }
    return (uint32_t)ppi;
}

int inkcell_scale_for_dp(int dp, uint32_t ppi, uint32_t cell_steps) {
    if (ppi == 0U || dp <= 0 || cell_steps == 0U) {
        return 0;
    }
    /* The cell is cell_steps * scale / UNIT pixels and should be dp * ppi / 160, so
       scale = dp * ppi * UNIT / (160 * cell_steps), to the nearest unit. */
    const uint64_t num = (uint64_t)dp * ppi * INKCELL_SCALE_UNIT;
    const uint64_t den = (uint64_t)INKCELL_DP_PPI * cell_steps;
    return (int)((num + den / 2U) / den);
}
