/*
 * A theme's text at another size, or heavier: inkcell_theme_text() (inkcell/ui/theme.h says why).
 */

#include "inkcell/ui/theme.h"

#include <errno.h>

/* `value` at `percent`, to the nearest whole unit. */
static unsigned text_at(unsigned value, unsigned percent) {
    return (value * percent + 50U) / 100U;
}

int inkcell_theme_text(struct inkcell_theme *theme, unsigned percent, bool bold) {
    if (theme == NULL || percent < INKCELL_TEXT_PERCENT_MIN || percent > INKCELL_TEXT_PERCENT_MAX) {
        return -EINVAL;
    }
    struct inkcell_metrics *m = &theme->metrics;
    const unsigned body = text_at(m->body_dp, percent);
    m->body_dp = (uint8_t)(body > 255U ? 255U : (body == 0U ? 1U : body));
    unsigned scale = text_at(m->scale, percent);
    if (scale < (unsigned)INKCELL_SCALE_MIN) {
        scale = (unsigned)INKCELL_SCALE_MIN;
    } else if (scale > (unsigned)INKCELL_SCALE_MAX) {
        scale = (unsigned)INKCELL_SCALE_MAX;
    }
    m->scale = (uint8_t)scale;
    if (bold) {
        for (int i = 0; i < (int)INKCELL_TYPE_COUNT; ++i) {
            m->type[i].weight = (uint8_t)INKCELL_WEIGHT_STRONG;
        }
    }
    return 0;
}
