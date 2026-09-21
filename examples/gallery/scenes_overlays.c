#define _POSIX_C_SOURCE 200809L

/*
 * The overlays: what arrives over a screen rather than on it.
 *
 * Drawn in one picture, over a body, because the thing to check about an overlay is not the
 * overlay - it is the stacking. A dialog has to be legible over whatever it landed on, a
 * snackbar has to clear the keycap row it hangs above, and neither may move the screen
 * underneath: a modal that reflows the thing it is asking about is a modal answering a
 * different question by the time it is read.
 *
 * So the body under them is deliberately busy. If the dialog's scrim is too weak or the
 * snackbar's tier too close to the ground, this is the picture that says so.
 */

#include "gallery.h"

#include "inkcell/ui/overlay.h"

#include "inkcell/utils/qr.h"

#include <string.h>

/* What the QR encodes. A URL rather than a sentence: not read, not translated, and long enough
   to need a version with real structure in it rather than a toy grid. */
#define GALLERY_QR_PAYLOAD "https://github.com/mcereal/inkcell"

void gallery_scene_overlays(struct inkcell_backend_fb_state *state) {
    struct inkcell_fb_layout layout = gallery_frame(state, GALLERY_STR_HEAD_OVERLAYS, 4U);
    const struct inkcell_fb_row_box box = inkcell_fb_row_box(state);

    /* A body to land on: a short column of rows, so the dialog has something to cover and the
       scrim has something to dim. */
    static const enum gallery_str_id k_rows[] = {
        GALLERY_STR_ROW_DISPLAY, GALLERY_STR_ROW_SOUND,   GALLERY_STR_ROW_STORAGE,
        GALLERY_STR_ROW_NETWORK, GALLERY_STR_ROW_BATTERY, GALLERY_STR_ROW_SECURITY,
        GALLERY_STR_ROW_UPDATES, GALLERY_STR_ROW_THEME,
    };
    const uint32_t row_count = (uint32_t)(sizeof k_rows / sizeof k_rows[0]);
    struct inkcell_fb_list list = inkcell_fb_list_begin(&layout, row_count, 1U);
    uint32_t index = 0U;
    while (inkcell_fb_list_next(&list, &index)) {
        const struct inkcell_fb_list_item item = {
            .text = gallery_text(k_rows[index]),
            .leading = {.kind = INKCELL_FB_LEADING_ICON, .icon = INKCELL_ICON_CHEVRON},
            .trailing = {.kind = INKCELL_FB_TRAILING_ICON, .icon = INKCELL_ICON_CHEVRON},
        };
        inkcell_fb_list_item(state, &list, index, &item);
    }

    const int body_bottom = layout.body_y + (int)row_count * layout.line;

    /*
     * The QR, in the body's lower half. It is here rather than in a sheet of its own because
     * it is the one thing this toolkit draws that has to be *read by another device*: a module
     * that is a pixel out is a code a phone cannot resolve, and that is a failure no other
     * component in the library can have.
     */
    static struct inkcell_qr code;
    if (inkcell_qr_encode((const uint8_t *)GALLERY_QR_PAYLOAD, strlen(GALLERY_QR_PAYLOAD),
                          INKCELL_QR_ECC_MEDIUM, &code)) {
        /*
         * The box is the room *offered*, not the size drawn: a module has to be a whole number
         * of pixels or a reader photographing it finds edges the code does not have, so the
         * component takes the largest whole-module square that fits and centres it. Which means
         * the box comes first and inkcell_fb_qr_side() answers afterwards - asking for the side
         * of a code whose box is still zero gets zero, which is a QR that does not appear.
         */
        const int room = (layout.footer_y - inkcell_fb_gutter(state) - body_bottom);
        struct inkcell_fb_qr qr = {
            .code = &code,
            .box = {.x = box.text_right - room, .y = body_bottom, .w = room, .h = room},
        };
        if (inkcell_fb_qr_side(&qr) > 0) {
            inkcell_fb_draw_qr(state, &qr);
        }
    }

    /* The transient half of "something happened", above the keycap row. `until_ms` is when it
       stops being shown; the clock this scene is drawn against is well before it. */
    const struct inkcell_fb_snackbar snack = {
        .text = gallery_text(GALLERY_STR_SNACK_SENT),
        .until_ms = 1000000U,
    };
    inkcell_fb_draw_snackbar(state, &layout, &snack);

    /* And the modal, last, because it is over everything. Destructive, so the accepting verb
       takes the error family rather than the accent - a key that deletes something should not
       look like the key that saves it.

       It is a layer now rather than a panel filling the body, so what this picture is really
       of is the *scrim*: the rows above are still there, dimmed, and the keycap row at the
       bottom is not - because the question is this screen's and the keycaps are how it gets
       answered. See include/inkcell/ui/overlay.h. */
    const struct inkcell_fb_dialog dialog = {
        .icon = INKCELL_ICON_DELETE,
        .headline = gallery_text(GALLERY_STR_DIALOG_HEADLINE),
        .text = gallery_text(GALLERY_STR_DIALOG_BODY),
        .accept = gallery_text(GALLERY_STR_ACT_DELETE),
        .cancel = gallery_text(GALLERY_STR_ACT_CANCEL),
        .cursor = 1U,
        .destructive = true,
    };
    (void)inkcell_fb_draw_dialog(state, &layout, &dialog, GALLERY_OVERLAY_DIALOG, true);

    gallery_footer(state, &layout);
}
