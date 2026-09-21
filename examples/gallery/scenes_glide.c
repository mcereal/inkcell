#define _POSIX_C_SOURCE 200809L

/*
 * A list caught between two windows.
 *
 * Every other list on this sheet is settled, because a settled list is what a list looks like
 * for all but a sixth of a second at a time. This one is a picture of that sixth: the window
 * has moved and the rows have not arrived yet, which is the only state in which the three
 * things a glide is made of can be seen at once.
 *
 *   - **The rows are displaced.** They are drawn below where the window says they belong and
 *     are rising into place, so the fill under the cursor is between two rows rather than on
 *     one.
 *   - **There are more rows than the window holds.** The ones above it are the rows the press
 *     is leaving, and they have to be drawn or the top of the body would be a strip of bare
 *     panel for the length of the scroll.
 *   - **The body cuts them.** The first of those rows is half outside the window and is clipped
 *     to it, which is what keeps a scroll from painting over the app bar the frame is not
 *     redrawing.
 *
 * Like the focus ring's page, the movement is scripted across the two frames the gallery
 * renders: the first settles the list at one window and the second asks for another, a fraction
 * of a motion later. The clock is what tells them apart and it is named, so the rows are the
 * same distance out on every host that renders this.
 */

#include "gallery.h"

#include <stdio.h>

/* Enough rows to be a list rather than a screenful, and a cursor far enough down the second
   frame that the window has to move to keep up with it. */
#define GLIDE_ITEMS 40U
#define GLIDE_ID 0x9100U

void gallery_scene_glide(struct inkcell_draw_state *state) {
    struct inkcell_fb_layout layout = gallery_frame(state, GALLERY_STR_HEAD_GLIDE, 1U);
    /*
     * The press this page is of happened *before* the first frame.
     *
     * A glide starts on the frame the window moves and is at its full displacement there, so a
     * sheet that rendered the move and then took the picture would photograph the moment
     * nothing has happened yet. What the first frame does instead is make the move itself - it
     * settles a window, then asks for the one after it, which is what starts the clock - and
     * the picture is the second frame, a fraction of a motion later.
     *
     * The cursor jumps a body's length rather than a row, which is a d-pad held rather than
     * pressed: one row of displacement through a column of identical rows looks exactly like a
     * column of identical rows, and a still has to be able to say which.
     *
     * A screen does none of this. It moves its own cursor and calls the same two functions; the
     * clock is this page's script and nothing else's.
     */
    if (state->now_ms <= GALLERY_CLOCK_MS) {
        struct inkcell_fb_list seed = inkcell_fb_list_begin(&layout, GLIDE_ITEMS, 0U);
        inkcell_fb_list_glide(state, &seed, GLIDE_ID);
    }

    struct inkcell_fb_list list = inkcell_fb_list_begin(&layout, GLIDE_ITEMS, layout.rows);
    inkcell_fb_list_glide(state, &list, GLIDE_ID);

    uint32_t index = 0U;
    while (inkcell_fb_list_next(&list, &index)) {
        /* The row's number, so the picture says which rows moved and by how much. A gallery
           scene is allowed a figure where a screen would carry a name. */
        char label[16];
        snprintf(label, sizeof label, "%u", (unsigned)index + 1U);
        const struct inkcell_fb_list_item item = {
            .leading = {.kind = INKCELL_FB_LEADING_ICON, .icon = INKCELL_ICON_NODES},
            .text = label,
            .tone = INKCELL_TONE_NORMAL,
            .divider = true,
        };
        inkcell_fb_list_item(state, &list, index, &item);
    }

    /*
     * No focus ring on this page, and that is not an omission. A ring has a page of its own and
     * on this frame it would be mid-journey between two rows - a second thing in motion, and a
     * still cannot say which of the two it is a picture of.
     */

    gallery_footer(state, &layout);
}
