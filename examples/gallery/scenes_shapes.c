#define _POSIX_C_SOURCE 200809L

/*
 * The drawing primitives themselves, with no component in the way.
 *
 * Every other sheet here shows a component, which is the right way round: what a reviewer
 * usually wants to know is whether a button still looks like a button. This one is the
 * exception, because a curve is the one thing a component cannot show clearly - a card's corner
 * is twelve pixels of a thousand-pixel panel, and a staircase on it is invisible in a
 * screenshot right up until it is on a device in somebody's hands.
 *
 * So the shapes are drawn large, at radii from one pixel to a disc, and at the two thicknesses
 * a hairline and a focus ring take. What to look for is the corner where the arc meets the
 * straight edge: that is where a rasteriser that is nearly right stops being right, and where a
 * ring drawn as two fills used to show the seam between them.
 */

#include "gallery.h"

void gallery_scene_shapes(struct inkcell_backend_fb_state *state) {
    struct inkcell_fb_layout layout = gallery_frame(state, GALLERY_STR_HEAD_SHAPES, 0U);
    const struct inkcell_fb_row_box box = inkcell_fb_row_box(state);
    const int gap = inkcell_fb_space(state, INKCELL_SPACE_SM);
    const struct inkcell_rgb ink = inkcell_fb_tone_color(state, INKCELL_TONE_PRIMARY);
    const struct inkcell_rgb second = inkcell_fb_tone_color(state, INKCELL_TONE_TERTIARY);
    int y = layout.body_y;

    y = gallery_section(state, &layout, y, GALLERY_STR_HEAD_CORNERS);

    /*
     * The radius scale, run past its own ends. A theme states four steps; these are those and
     * then the two that matter most for a modern look - a capsule, and a disc, where the arc is
     * long enough that a stepped edge is a staircase rather than a rounding.
     */
    const int side = inkcell_fb_line_adv(state, state->scale) * 2;
    static const enum inkcell_shape k_shapes[] = {INKCELL_SHAPE_NONE, INKCELL_SHAPE_SM,
                                                  INKCELL_SHAPE_MD, INKCELL_SHAPE_LG,
                                                  INKCELL_SHAPE_FULL};
    int x = box.text_x;
    for (size_t i = 0U; i < sizeof k_shapes / sizeof k_shapes[0]; ++i) {
        inkcell_fb_fill_round_rect(state, x, y, side, side, inkcell_fb_radius(state, k_shapes[i]),
                                   ink);
        x += side + gap;
    }
    /* A capsule and a disc, which is the same call with the radius left to find its own limit. */
    const int wide = side * 2;
    inkcell_fb_fill_round_rect(state, x, y, wide, side,
                               inkcell_fb_radius(state, INKCELL_SHAPE_FULL), ink);
    x += wide + gap;
    inkcell_fb_fill_round_rect(state, x, y, side, side, side / 2, second);
    y += side + gap;

    y = gallery_section(state, &layout, y, GALLERY_STR_HEAD_OUTLINES);

    /*
     * The same shapes as rings. A hairline first, then the focus ring's doubled thickness, then
     * a band thick enough that the inner and outer arcs are plainly two different curves - which
     * is the case that shows whether the ring is one shape or two fills with a seam.
     */
    const int hairline = inkcell_fb_rule_height(state, state->scale);
    const int thicknesses[] = {hairline, hairline * 2, side / 6};
    for (size_t t = 0U; t < sizeof thicknesses / sizeof thicknesses[0]; ++t) {
        x = box.text_x;
        for (size_t i = 0U; i < sizeof k_shapes / sizeof k_shapes[0]; ++i) {
            inkcell_fb_stroke_round_rect(state, x, y, side, side,
                                         inkcell_fb_radius(state, k_shapes[i]), thicknesses[t],
                                         ink);
            x += side + gap;
        }
        inkcell_fb_stroke_round_rect(state, x, y, wide, side,
                                     inkcell_fb_radius(state, INKCELL_SHAPE_FULL), thicknesses[t],
                                     ink);
        x += wide + gap;
        inkcell_fb_stroke_round_rect(state, x, y, side, side, side / 2, thicknesses[t], second);
        y += side + gap;
    }

    y = gallery_section(state, &layout, y, GALLERY_STR_HEAD_ARCS);

    /*
     * Sweeps, from a sliver to the whole ring. Zero is twelve o'clock and they run clockwise,
     * so this row reads as a progress indicator filling - which is the component the arc exists
     * for, and the counterpart of the bar that inkcell_fb_draw_meter() draws.
     */
    const int ring = side / 2;
    const int band = inkcell_fb_meter_thickness(state, state->scale);
    static const int32_t k_sweeps[] = {80, 250, 500, 750, 1000};
    x = box.text_x;
    for (size_t i = 0U; i < sizeof k_sweeps / sizeof k_sweeps[0]; ++i) {
        /* The track under it, which is what makes a ring a gauge rather than a mark: the same
           pairing a meter draws, so the two read as one family. */
        inkcell_fb_stroke_arc(state, x + ring, y + ring, ring, band, 0, 1000,
                              inkcell_fb_color(state, INKCELL_COLOR_METER_TRACK));
        inkcell_fb_stroke_arc(state, x + ring, y + ring, ring, band, 0, k_sweeps[i], ink);
        x += 2 * ring + gap;
    }

    /* And the arcs that are not gauges: a sweep that does not start at the top, which is what a
       spinner is at any instant, and a thick band where the two ends are plainly cut square. */
    inkcell_fb_stroke_arc(state, x + ring, y + ring, ring, band, 300, 400, second);
    x += 2 * ring + gap;
    inkcell_fb_stroke_arc(state, x + ring, y + ring, ring, ring / 2, 620, 600, second);

    gallery_footer(state, &layout);
}
