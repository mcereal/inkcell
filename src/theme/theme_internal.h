#ifndef INKCELL_THEME_INTERNAL_H
#define INKCELL_THEME_INTERNAL_H

/*
 * What theme.c measures colour with, shared with tint.c so a derived palette is held to the same
 * luminance the validator reads - one table, so the two can never disagree by a rounding.
 */

#include "inkcell/ui/theme.h"

#include <stdint.h>

/* An sRGB channel in linear light, 0..65535. */
uint16_t inkcell_theme_linear16(uint8_t channel);

/* Relative luminance (the WCAG definition), 0..65535. */
uint32_t inkcell_theme_luminance16(struct inkcell_rgb color);

#endif /* INKCELL_THEME_INTERNAL_H */
