#define _POSIX_C_SOURCE 200809L

/*
 * The floating action button: the one verb a screen is *for*, as a shape rather than as a row.
 *
 * A FAB is placed by the frame rather than by whoever draws it - it anchors to the body's
 * trailing bottom corner and keeps clear of the action bar - so a sheet of specimens cannot lay
 * them out in a grid the way the buttons page does. What it can do is hand each one a frame of
 * its own: a layout is a few numbers saying where the chrome ended and where the footer starts,
 * and a copy with a raised floor is the same frame with a shorter body. So the column up the
 * trailing edge is six real FABs at six real anchors, each measured and drawn exactly as the one
 * at the bottom of a screen is, rather than six circles at coordinates this file made up.
 *
 * What the page has to show, and why each is here:
 *
 *   - **the three sizes**, because the axis they differ on is the room around the symbol rather
 *     than the symbol itself, and that is only legible with all three in one column;
 *   - **a family that is not the primary**, because a FAB takes a family and not a colour, and
 *     the whole of what that buys is a destructive one being unmistakable;
 *   - **resting and under the cursor**, which is the tonal pair the theme was validated on;
 *   - **extended, and half way back to a disc**, because the collapse is the shape's one
 *     behaviour and a picture of both ends says nothing about the journey between them.
 *
 * The rows behind them are not decoration. A FAB floats over content, and a picture of one on an
 * empty panel is a picture of a circle - what has to be checked is that it is still found, and
 * still readable, over the list it is standing on.
 */

#include "gallery.h"

/* Identities for the collapse. One each, or the two extended specimens would share a slot in
   the animation table and report each other's position. */
enum {
    FAB_ANIM_EXTENDED = 7100,
    FAB_ANIM_COLLAPSING,
};

/* One entry of the column: the FAB, what to call it, and whether the page has to *start* its
   collapse - see fab_column() for why that cannot simply be a field on the FAB. */
struct fab_row {
    struct inkcell_fb_fab fab;
    enum gallery_str_id caption;
    bool collapsing;
};

/*
 * A frame whose floor is `bottom`: the same layout, with the footer moved up.
 *
 * This is the whole of how one page shows six FABs. A FAB is anchored against `footer_y`, so a
 * copy with a raised floor is an honest frame rather than a coordinate invented here, and every
 * number the component derives from it is derived the way it would be on a screen.
 */
static struct inkcell_fb_layout fab_floor(const struct inkcell_fb_layout *layout, int bottom) {
    struct inkcell_fb_layout shelf = *layout;
    shelf.footer_y = bottom;
    return shelf;
}

/* The caption, right-aligned into the gap the column leaves beside itself. Beside the FAB rather
   than in the body's own text column, because the body is a list: a label in that column would
   be a second sentence written across the rows this page is standing the FAB on. */
static void fab_caption(const struct inkcell_draw_state *state,
                        const struct inkcell_fb_layout *layout, const struct inkcell_fb_rect *box,
                        enum gallery_str_id caption) {
    const int scale = layout->small;
    const char *text = gallery_text(caption);
    const int text_h = inkcell_scale_px((int)inkcell_fb_font(state)->height, scale);
    inkcell_fb_draw_text(
        state,
        box->x - inkcell_fb_char_adv(state, scale) - inkcell_fb_text_width(state, text, scale),
        box->y + (box->h - text_h) / 2, text, scale, inkcell_fb_tone_color(state, INKCELL_TONE_DIM),
        inkcell_fb_color(state, INKCELL_COLOR_BG));
}

/*
 * The column, from the floor upwards, stopping when the next one would reach the chrome.
 *
 * Each shelf is stepped by the box the last one actually came out as rather than by a number
 * this file picked: the sizes differ, and a fixed step would either overlap the large one or
 * leave a hole under the small one.
 */
static void fab_column(struct inkcell_draw_state *state, const struct inkcell_fb_layout *layout,
                       const struct fab_row *rows, size_t count) {
    const int gap = inkcell_fb_space(state, INKCELL_SPACE_MD);
    int floor = layout->footer_y;

    for (size_t i = 0U; i < count; ++i) {
        const struct inkcell_fb_layout shelf = fab_floor(layout, floor);
        if (inkcell_fb_fab_box(state, &shelf, &rows[i].fab).w <= 0) {
            return; /* the column has reached the chrome; the page is short a row, not wrong */
        }
        /*
         * A transition is started by the frame that *observes* the change, and the harness takes
         * its picture on the second of two frames - so a specimen whose `extended` differed
         * between them would be photographed at the start of its collapse rather than during it.
         * The change therefore has to happen inside frame one: the slot is seeded extended, and
         * then the real specimen asks for the collapse, so by the time frame two is taken the
         * container is already on its way. Frame one is not the picture, which is what makes
         * drawing this one twice legitimate rather than a trick.
         */
        if (rows[i].collapsing && state->now_ms <= GALLERY_CLOCK_MS) {
            struct inkcell_fb_fab seed = rows[i].fab;
            seed.extended = true;
            inkcell_fb_draw_fab(state, &shelf, &seed);
        }
        /* The caption goes beside the box the FAB *came out as*, not the one it is heading for:
           the one specimen mid-collapse is wider than it will rest at, and a label placed
           against its resting edge would be written over the pill it is naming. */
        const struct inkcell_fb_rect box = inkcell_fb_draw_fab(state, &shelf, &rows[i].fab);
        fab_caption(state, layout, &box, rows[i].caption);
        floor -= box.h + gap;
    }
}

void gallery_scene_fab(struct inkcell_draw_state *state) {
    struct inkcell_fb_layout layout = gallery_frame(state, GALLERY_STR_HEAD_FAB, 0U);

    /*
     * The body it floats over. Plain rows rather than anything clever: what this page asks is
     * whether the disc is still found over ordinary content, and ordinary content is the only
     * honest thing to ask it over.
     */
    static const enum gallery_str_id k_rows[] = {
        GALLERY_STR_ROW_DISPLAY,  GALLERY_STR_ROW_BRIGHTNESS, GALLERY_STR_ROW_THEME,
        GALLERY_STR_ROW_LANGUAGE, GALLERY_STR_ROW_SOUND,      GALLERY_STR_ROW_NOTIFICATIONS,
        GALLERY_STR_ROW_STORAGE,  GALLERY_STR_ROW_BATTERY,    GALLERY_STR_ROW_NETWORK,
        GALLERY_STR_ROW_SECURITY,
    };
    int y = gallery_section(state, &layout, layout.body_y, GALLERY_STR_HEAD_FAB_SIZES);
    for (size_t row = 0U; y + layout.line <= layout.footer_y; ++row) {
        inkcell_fb_draw_row(state, y,
                            gallery_text(k_rows[row % (sizeof k_rows / sizeof k_rows[0])]),
                            inkcell_fb_tone_color(state, INKCELL_TONE_NORMAL), false);
        y += layout.line;
    }

    /* Bottom of the column first, because it is built upwards from the floor - which is where a
       FAB is anchored, so it is also the order the frame itself would answer in. */
    const struct fab_row column[] = {
        {{.icon = INKCELL_ICON_COMPOSE,
          .label = gallery_text(GALLERY_STR_ACT_COMPOSE),
          .extended = true,
          .size = INKCELL_FB_FAB_MD,
          .id = FAB_ANIM_EXTENDED},
         GALLERY_STR_ACT_COMPOSE,
         false},
        {{.icon = INKCELL_ICON_COMPOSE,
          .label = gallery_text(GALLERY_STR_ACT_COMPOSE),
          .size = INKCELL_FB_FAB_MD,
          .id = FAB_ANIM_COLLAPSING},
         GALLERY_STR_HEAD_FAB_COLLAPSE,
         true},
        /* Under the cursor: the family at full strength, with the ink the theme checked against
           it. The pair, not the fill - which is what a FAB taking a family is for. */
        {{.icon = INKCELL_ICON_COMPOSE, .size = INKCELL_FB_FAB_LG, .focused = true},
         GALLERY_STR_ACT_SELECT,
         false},
        {{.icon = INKCELL_ICON_COMPOSE, .size = INKCELL_FB_FAB_LG}, GALLERY_STR_HEAD_FAB, false},
        {{.icon = INKCELL_ICON_COMPOSE, .size = INKCELL_FB_FAB_MD},
         GALLERY_STR_ACT_ADD_NODE,
         false},
        /* The one that means something different rather than looking different: a FAB that
           destroys is filled from the error family. */
        {{.icon = INKCELL_ICON_DELETE, .size = INKCELL_FB_FAB_SM, .family = INKCELL_FAMILY_ERROR},
         GALLERY_STR_ACT_DELETE,
         false},
    };
    fab_column(state, &layout, column, sizeof column / sizeof column[0]);

    gallery_footer(state, &layout);
}
