/*
 * A theme re-hued around one colour: inkcell_theme_tint() (inkcell/ui/theme.h says why).
 *
 * The work is done in OKLab, the perceptual space where "the same colour, another hue" is a
 * rotation of (a, b) and nothing else, so a chroma and a lightness a palette was picked at carry
 * across hues as the eye reads them. Each colour the tint touches keeps its *luminance* exactly -
 * the lightness is solved for, not copied - because luminance is what every contrast ratio the
 * base theme was validated at is made of, and a tint that kept the ratios approximately would
 * be a tint that fails the validator on some hue nobody tried. What a hue cannot have at that
 * luminance (a saturated yellow is light, a saturated blue is dark) comes out of the chroma,
 * never out of the lightness.
 *
 * No libm, for the reason theme.c gives for its table: the roots are Newton's method and the
 * one rotation is by a constant angle, so this is arithmetic and nothing else.
 */

#include "inkcell/ui/theme.h"

#include "theme_internal.h"

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>

/* A seed at least TINT_VIVID chromatic gives the full chroma of the palette it tints, and one
   at most TINT_DULL none at all, with a ramp between. The floor is what makes a graphite accent
   grey: the greys people pick lean a little cool or warm, and a seventh of a hue carried onto
   every family reads as a mistake, not as a choice. */
#define TINT_VIVID 0.10
#define TINT_DULL 0.02
/* The secondary family is the seed held back, so that the two families still read as two: the
   primary is what the eye follows, and the secondary is "the other side" without competing. */
#define TINT_SECONDARY_SHARE 0.6
/* A neutral picks up this much of the seed - enough that the surfaces belong to the accent, few
   enough that they still read as grey. A neutral the base theme stated as pure grey stays one:
   that theme decided its surfaces carry no hue at all, and the tint does not overrule it. */
#define TINT_NEUTRAL_CHROMA 0.010
#define TINT_GREY_CHROMA 0.0005
/* Above this a role on the neutral list is an accent the theme spent on furniture (the contrast
   theme's yellow rule), and it is tinted as one. */
#define TINT_ACCENT_CHROMA 0.04

struct tint_lab {
    double l, a, b;
};

struct tint_linear {
    double r, g, b;
};

static double tint_cbrt(double x) {
    if (x == 0.0) {
        return 0.0;
    }
    const bool negative = x < 0.0;
    if (negative) {
        x = -x;
    }
    /* From above the root, Newton's method on y^3 - x falls monotonically onto it, so it stops
       the step it no longer falls. max(x, 1) is above the cube root of every positive x. */
    double y = x > 1.0 ? x : 1.0;
    for (int i = 0; i < 200; ++i) {
        const double next = (2.0 * y + x / (y * y)) / 3.0;
        if (!(next < y)) {
            break;
        }
        y = next;
    }
    return negative ? -y : y;
}

static double tint_sqrt(double x) {
    if (x <= 0.0) {
        return 0.0;
    }
    double y = x > 1.0 ? x : 1.0; /* above the root, as for the cube */
    for (int i = 0; i < 200; ++i) {
        const double next = (y + x / y) / 2.0;
        if (!(next < y)) {
            break;
        }
        y = next;
    }
    return y;
}

/* Björn Ottosson's OKLab, from and to linear sRGB. */
static struct tint_lab tint_lab_of(struct tint_linear c) {
    const double l = tint_cbrt(0.4122214708 * c.r + 0.5363325363 * c.g + 0.0514459929 * c.b);
    const double m = tint_cbrt(0.2119034982 * c.r + 0.6806995451 * c.g + 0.1073969566 * c.b);
    const double s = tint_cbrt(0.0883024619 * c.r + 0.2817188376 * c.g + 0.6299787005 * c.b);
    return (struct tint_lab){
        .l = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
        .a = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
        .b = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s,
    };
}

static struct tint_linear tint_linear_of(struct tint_lab c) {
    const double l1 = c.l + 0.3963377774 * c.a + 0.2158037573 * c.b;
    const double m1 = c.l - 0.1055613458 * c.a - 0.0638541728 * c.b;
    const double s1 = c.l - 0.0894841775 * c.a - 1.2914855480 * c.b;
    const double l = l1 * l1 * l1;
    const double m = m1 * m1 * m1;
    const double s = s1 * s1 * s1;
    return (struct tint_linear){
        .r = 4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
        .g = -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
        .b = -0.0041960863 * l - 0.7034186147 * m + 1.7076956961 * s,
    };
}

static struct tint_linear tint_linear_of_rgb(struct inkcell_rgb c) {
    return (struct tint_linear){
        .r = (double)inkcell_theme_linear16(c.r) / 65535.0,
        .g = (double)inkcell_theme_linear16(c.g) / 65535.0,
        .b = (double)inkcell_theme_linear16(c.b) / 65535.0,
    };
}

static double tint_luminance(struct tint_linear c) {
    return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b;
}

static bool tint_in_gamut(struct tint_linear c) {
    const double slack = 1e-9;
    return c.r >= -slack && c.r <= 1.0 + slack && c.g >= -slack && c.g <= 1.0 + slack &&
           c.b >= -slack && c.b <= 1.0 + slack;
}

/* The colour at (a, b) whose luminance is `y`: the lightness found by bisection, which works
   because at a fixed (a, b) luminance only rises with lightness. */
static struct tint_linear tint_at_luminance(double y, double a, double b) {
    double lo = 0.0;
    double hi = 1.0;
    for (int i = 0; i < 48; ++i) {
        const double mid = (lo + hi) / 2.0;
        if (tint_luminance(tint_linear_of((struct tint_lab){mid, a, b})) < y) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return tint_linear_of((struct tint_lab){(lo + hi) / 2.0, a, b});
}

/* The colour of luminance `y` in the direction (ha, hb), as near `chroma` as sRGB allows. Grey
   at that luminance always fits, so the search has a floor to fall back to. */
static struct tint_linear tint_place(double y, double ha, double hb, double chroma) {
    struct tint_linear best = tint_at_luminance(y, ha * chroma, hb * chroma);
    if (tint_in_gamut(best)) {
        return best;
    }
    double lo = 0.0;
    double hi = 1.0;
    best = tint_at_luminance(y, 0.0, 0.0);
    for (int i = 0; i < 24; ++i) {
        const double mid = (lo + hi) / 2.0;
        const struct tint_linear c = tint_at_luminance(y, ha * chroma * mid, hb * chroma * mid);
        if (tint_in_gamut(c)) {
            lo = mid;
            best = c;
        } else {
            hi = mid;
        }
    }
    return best;
}

/* The largest 8-bit channel whose linear value is at most `linear`. */
static uint8_t tint_floor8(double linear) {
    const double want = linear * 65535.0;
    int lo = 0;
    int hi = 255;
    while (lo < hi) {
        const int mid = (lo + hi + 1) / 2;
        if ((double)inkcell_theme_linear16((uint8_t)mid) <= want) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }
    return (uint8_t)lo;
}

/*
 * Down to 8 bits, keeping the luminance. Each channel rounds one of two ways, and of the eight
 * colours that makes, the one whose luminance - measured exactly as the validator measures it -
 * is nearest the original's wins. Rounding each channel to its nearest instead can move the
 * luminance by more than a contrast pair has to spare.
 */
static struct inkcell_rgb tint_rgb(struct tint_linear c, uint32_t target) {
    const uint8_t floor[3] = {tint_floor8(c.r), tint_floor8(c.g), tint_floor8(c.b)};
    struct inkcell_rgb best = {floor[0], floor[1], floor[2]};
    uint32_t best_error = UINT32_MAX;
    for (unsigned pick = 0U; pick < 8U; ++pick) {
        uint8_t v[3];
        for (unsigned i = 0U; i < 3U; ++i) {
            const bool up = (pick >> i) & 1U;
            v[i] = up && floor[i] < 255U ? (uint8_t)(floor[i] + 1U) : floor[i];
        }
        const struct inkcell_rgb candidate = {v[0], v[1], v[2]};
        const uint32_t y = inkcell_theme_luminance16(candidate);
        const uint32_t error = y > target ? y - target : target - y;
        if (error < best_error) {
            best_error = error;
            best = candidate;
        }
    }
    return best;
}

enum tint_kind {
    TINT_KEEP = 0, /* not the tint's: text, the verdicts, the code, the scrim */
    TINT_ACCENT,   /* the seed's hue */
    TINT_MUTED,    /* the seed's hue, held back */
    TINT_TURNED,   /* the seed's hue turned a sixth of the way round */
    TINT_NEUTRAL,  /* a surface or a line: a breath of the seed */
};

static enum tint_kind tint_kind_of(enum inkcell_color role) {
    switch (role) {
    case INKCELL_COLOR_BG:
    case INKCELL_COLOR_SURFACE_LOW:
    case INKCELL_COLOR_SURFACE:
    case INKCELL_COLOR_SURFACE_HIGH:
    case INKCELL_COLOR_SURFACE_SEL:
    case INKCELL_COLOR_SURFACE_ACTIVE:
    case INKCELL_COLOR_RULE:
    case INKCELL_COLOR_RULE_STRONG:
    case INKCELL_COLOR_OUTLINE:
    case INKCELL_COLOR_METER_TRACK:
        return TINT_NEUTRAL;
    default:
        break;
    }
    for (int slot = 0; slot < (int)INKCELL_SLOT_COUNT; ++slot) {
        if (role == inkcell_family_role(INKCELL_FAMILY_PRIMARY, (enum inkcell_slot)slot)) {
            return TINT_ACCENT;
        }
        if (role == inkcell_family_role(INKCELL_FAMILY_SECONDARY, (enum inkcell_slot)slot)) {
            return TINT_MUTED;
        }
        if (role == inkcell_family_role(INKCELL_FAMILY_TERTIARY, (enum inkcell_slot)slot)) {
            return TINT_TURNED;
        }
    }
    return TINT_KEEP;
}

int inkcell_theme_tint(const struct inkcell_theme *base, struct inkcell_rgb seed,
                       struct inkcell_theme *out) {
    if (out == NULL) {
        return -EINVAL;
    }
    if (base == NULL) {
        base = inkcell_theme_default();
    }
    *out = *base;

    const struct tint_lab s = tint_lab_of(tint_linear_of_rgb(seed));
    const double seed_chroma = tint_sqrt(s.a * s.a + s.b * s.b);
    /* The seed's hue as a unit direction in (a, b), and that direction turned 60 degrees for the
       tertiary - Material's figure: near enough to belong, far enough to be a second voice. */
    const double ha = seed_chroma > 0.0 ? s.a / seed_chroma : 1.0;
    const double hb = seed_chroma > 0.0 ? s.b / seed_chroma : 0.0;
    const double cos60 = 0.5;
    const double sin60 = 0.86602540378443864676;
    const double ta = ha * cos60 - hb * sin60;
    const double tb = ha * sin60 + hb * cos60;
    const double vivid = seed_chroma >= TINT_VIVID ? 1.0
                         : seed_chroma <= TINT_DULL
                             ? 0.0
                             : (seed_chroma - TINT_DULL) / (TINT_VIVID - TINT_DULL);

    for (int i = 0; i < (int)INKCELL_COLOR_COUNT; ++i) {
        const enum inkcell_color role = (enum inkcell_color)i;
        enum tint_kind kind = tint_kind_of(role);
        if (kind == TINT_KEEP) {
            continue;
        }
        const struct inkcell_rgb was = base->colors[i];
        const struct tint_linear linear = tint_linear_of_rgb(was);
        const struct tint_lab lab = tint_lab_of(linear);
        const double chroma = tint_sqrt(lab.a * lab.a + lab.b * lab.b);
        double want;
        if (kind == TINT_NEUTRAL) {
            if (chroma >= TINT_ACCENT_CHROMA) {
                kind = TINT_ACCENT;
                want = chroma * vivid;
            } else if (chroma <= TINT_GREY_CHROMA) {
                continue;
            } else {
                want = (chroma > TINT_NEUTRAL_CHROMA ? chroma : TINT_NEUTRAL_CHROMA) * vivid;
            }
        } else {
            want = chroma * vivid * (kind == TINT_MUTED ? TINT_SECONDARY_SHARE : 1.0);
        }
        const uint32_t target = inkcell_theme_luminance16(was);
        const bool turned = kind == TINT_TURNED;
        const struct tint_linear placed =
            tint_place((double)target / 65535.0, turned ? ta : ha, turned ? tb : hb, want);
        out->colors[i] = tint_rgb(placed, target);
    }

    if (!inkcell_theme_validate(out, NULL, 0U)) {
        *out = *base;
        return -ERANGE;
    }
    return 0;
}
