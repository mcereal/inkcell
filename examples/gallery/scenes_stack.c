#define _POSIX_C_SOURCE 200809L

/*
 * The stack, and the width class - the two things a screen no longer works out for itself.
 *
 * Every other page here is a picture of a component. This one is a picture of *arithmetic*,
 * which is a harder thing to photograph and the reason the page exists: a stack has no
 * appearance, so a change to how it divides a row is invisible in every other scene until some
 * screen somewhere comes out a pixel wrong. Drawing the boxes it resolves puts the division on
 * the golden sheet, where a change to it has to be looked at.
 *
 * It borrows the trick the typography page uses for the same reason. Where a stack's answer
 * differs from what was asked for - an item that shrank, an item that was dropped - both are
 * drawn: the resolved box as a fill, and what it asked for as an outline behind it. The gap
 * between the two is the arithmetic, at the size it actually happens.
 *
 * The page is also the width class's only picture, and it gets one free: the manifest renders
 * every scene at two glyph scales, and on a 1024-pixel panel those two land either side of a
 * threshold. The bottom section reports which class the frame is in, so the sheet carries the
 * compact layout and the roomier one side by side without the harness needing to know that is
 * what it is doing.
 */

#include "gallery.h"

#include "inkcell/ui/stack.h"

/* How tall a specimen box is drawn: two body lines, which is enough to hold a label and to read
   as a box rather than as a rule. */
static int specimen_height(const struct inkcell_draw_state *state) {
    return inkcell_fb_line_adv(state, inkcell_fb_type_scale(state, INKCELL_TYPE_BODY)) * 2;
}

/* One resolved box, named. The label is drawn only when it measures narrower than the box it
   would sit in - a specimen whose caption overflows it is a picture of the caption. */
static void specimen(const struct inkcell_draw_state *state, struct inkcell_box box,
                     enum gallery_str_id label, enum inkcell_tone tone) {
    if (inkcell_box_is_empty(box)) {
        return;
    }
    const int radius = inkcell_fb_radius(state, INKCELL_SHAPE_SM);
    const struct inkcell_rgb fill = inkcell_fb_color(state, INKCELL_COLOR_SURFACE_HIGH);
    inkcell_fb_fill_round_rect(state, box.x, box.y, box.w, box.h, radius, fill);
    inkcell_fb_stroke_round_rect(state, box.x, box.y, box.w, box.h, radius, inkcell_fb_edge(state),
                                 inkcell_fb_tone_color(state, tone));

    const int small = inkcell_fb_type_scale(state, INKCELL_TYPE_LABEL);
    const char *text = gallery_text(label);
    const int width = inkcell_fb_text_width(state, text, small);
    if (width >= box.w) {
        return;
    }
    inkcell_fb_draw_text(state, box.x + (box.w - width) / 2,
                         box.y + (box.h - inkcell_fb_line_adv(state, small)) / 2, text, small,
                         inkcell_fb_tone_color(state, tone), fill);
}

/*
 * What an item asked for, over what it got: an outline as wide as its basis.
 *
 * Drawn *last*, over the specimens rather than behind them, and that is not a detail. The room
 * an item gave up is the room its neighbour is now standing in, so an outline drawn first is an
 * outline the neighbour paints over - which is to say the one part of the picture worth having
 * is the part that disappears. Crossing the neighbour is the point: it is where the two would
 * have overlapped if nothing had given way.
 */
static void asked_for(const struct inkcell_draw_state *state, struct inkcell_box box, int basis) {
    if (basis <= box.w) {
        return;
    }
    inkcell_fb_stroke_round_rect(state, box.x, box.y, basis, box.h,
                                 inkcell_fb_radius(state, INKCELL_SHAPE_SM), inkcell_fb_edge(state),
                                 inkcell_fb_tone_color(state, INKCELL_TONE_WARNING));
}

/* A bar as long as `w`, with a caption beside it: how the last section draws a width. */
static int rule_of(const struct inkcell_draw_state *state, int x, int y, int w,
                   enum gallery_str_id label, enum inkcell_tone tone) {
    const int small = inkcell_fb_type_scale(state, INKCELL_TYPE_LABEL);
    const int thick = inkcell_fb_rule_height(state, small) * 2;
    inkcell_fb_fill_rect(state, x, y, w, thick, inkcell_fb_tone_color(state, tone));
    inkcell_fb_draw_text(state, x, y + thick + inkcell_fb_space(state, INKCELL_SPACE_XS),
                         gallery_text(label), small, inkcell_fb_tone_color(state, tone),
                         inkcell_fb_color(state, INKCELL_COLOR_BG));
    return y + thick + inkcell_fb_space(state, INKCELL_SPACE_XS) +
           inkcell_fb_line_adv(state, small) + inkcell_fb_space(state, INKCELL_SPACE_SM);
}

/* What this frame's width class is called. A lookup rather than three branches at the call
   site, so the page names every class the enum has. */
static enum gallery_str_id class_name(enum inkcell_width_class width) {
    switch (width) {
    case INKCELL_WIDTH_EXPANDED:
        return GALLERY_STR_WIDTH_EXPANDED;
    case INKCELL_WIDTH_MEDIUM:
        return GALLERY_STR_WIDTH_MEDIUM;
    case INKCELL_WIDTH_COMPACT:
    default:
        return GALLERY_STR_WIDTH_COMPACT;
    }
}

void gallery_scene_stack(struct inkcell_draw_state *state) {
    struct inkcell_fb_layout layout = gallery_frame(state, GALLERY_STR_HEAD_STACK, 4U);
    const struct inkcell_fb_row_box row_box = inkcell_fb_row_box(state);
    const int gap = inkcell_fb_space(state, INKCELL_SPACE_SM);
    const int height = specimen_height(state);
    const int span = row_box.text_right - row_box.text_x;
    const int small = inkcell_fb_type_scale(state, INKCELL_TYPE_LABEL);
    int y = layout.body_y;

    struct inkcell_stack stack;
    struct inkcell_box out[5];

    /* ---- a row: what a list row's three parts are, and which of them takes the slack ---- */
    y = gallery_section(state, &layout, y, GALLERY_STR_STACK_ROW);
    struct inkcell_box room = {.x = row_box.text_x, .y = y, .w = span, .h = height};
    inkcell_stack_begin(&stack, room, INKCELL_AXIS_X, gap);
    inkcell_stack_add_fixed(&stack, height);
    inkcell_stack_add_grow(&stack, 0, 1U);
    inkcell_stack_add_fixed(&stack, height * 2);
    inkcell_stack_resolve(&stack, out, 5U);
    specimen(state, out[0], GALLERY_STR_STACK_ICON, INKCELL_TONE_DIM);
    specimen(state, out[1], GALLERY_STR_STACK_LABEL, INKCELL_TONE_PRIMARY);
    specimen(state, out[2], GALLERY_STR_STACK_VALUE, INKCELL_TONE_DIM);
    y += height + inkcell_fb_space(state, INKCELL_SPACE_MD);

    /* ---- shrink: the outline is the width it asked for, the fill is what it kept ---- */
    y = gallery_section(state, &layout, y, GALLERY_STR_STACK_SHRINK);
    room.y = y;
    inkcell_stack_begin(&stack, room, INKCELL_AXIS_X, gap);
    const int wants = (span * 3) / 4;
    const struct inkcell_stack_item label = {.basis = wants, .min = span / 4, .shrink = 1U};
    inkcell_stack_add(&stack, label);
    inkcell_stack_add_fixed(&stack, span / 2);
    inkcell_stack_resolve(&stack, out, 5U);
    specimen(state, out[0], GALLERY_STR_STACK_LABEL, INKCELL_TONE_PRIMARY);
    specimen(state, out[1], GALLERY_STR_STACK_VALUE, INKCELL_TONE_DIM);
    asked_for(state, out[0], wants);
    y += height + inkcell_fb_space(state, INKCELL_SPACE_MD);

    /* ---- and what happens when shrinking cannot save it ---- */
    y = gallery_section(state, &layout, y, GALLERY_STR_STACK_DROP);
    room.y = y;
    inkcell_stack_begin(&stack, room, INKCELL_AXIS_X, gap);
    for (uint32_t i = 0U; i < 5U; ++i) {
        inkcell_stack_add_fixed(&stack, span / 4);
    }
    const uint32_t placed = inkcell_stack_resolve(&stack, out, 5U);
    for (uint32_t i = 0U; i < 5U; ++i) {
        specimen(state, out[i], GALLERY_STR_STACK_KEPT, INKCELL_TONE_PRIMARY);
    }
    /* The two that were given up have no box to draw, so the count is what says they were
       there. A run that silently placed three would look exactly like a run of three. */
    if (placed < 5U) {
        inkcell_fb_draw_text(state, row_box.text_x,
                             y + height + inkcell_fb_space(state, INKCELL_SPACE_XS),
                             gallery_text(GALLERY_STR_STACK_DROPPED), small,
                             inkcell_fb_tone_color(state, INKCELL_TONE_WARNING),
                             inkcell_fb_color(state, INKCELL_COLOR_BG));
    }
    y += height + inkcell_fb_space(state, INKCELL_SPACE_XS) + inkcell_fb_line_adv(state, small) +
         inkcell_fb_space(state, INKCELL_SPACE_MD);

    /* ---- spread: the leftover shared out between the items rather than after them ---- */
    y = gallery_section(state, &layout, y, GALLERY_STR_STACK_SPREAD);
    room.y = y;
    inkcell_stack_begin(&stack, room, INKCELL_AXIS_X, 0);
    stack.justify = INKCELL_JUSTIFY_BETWEEN;
    for (uint32_t i = 0U; i < 4U; ++i) {
        inkcell_stack_add_fixed(&stack, height * 2);
    }
    inkcell_stack_resolve(&stack, out, 5U);
    for (uint32_t i = 0U; i < 4U; ++i) {
        specimen(state, out[i], GALLERY_STR_STACK_KEPT, INKCELL_TONE_DIM);
    }
    y += height + inkcell_fb_space(state, INKCELL_SPACE_MD);

    /* ---- the measure, and the class this frame is in ---- */
    y = gallery_section(state, &layout, y, class_name(layout.width));
    /* Each bar at its own leading edge, so what the picture shows is where the two boxes
       actually are - a pair drawn from one origin would say the column is narrower and hide
       that it is also centred. */
    const struct inkcell_box full = inkcell_fb_full_box(state, &layout);
    const struct inkcell_box body = inkcell_fb_body_box(state, &layout);
    y = rule_of(state, full.x, y, full.w, GALLERY_STR_STACK_BODY, INKCELL_TONE_DIM);
    (void)rule_of(state, body.x, y, body.w, GALLERY_STR_STACK_MEASURE, INKCELL_TONE_PRIMARY);

    gallery_footer(state, &layout);
}
