#define _POSIX_C_SOURCE 200809L

/*
 * The card: a panel that holds a few related readings and, sometimes, the verbs that act on
 * them.
 *
 * Three variants, and the difference between them is a claim about depth rather than about
 * decoration. Filled sits on the ground, elevated stands over it, outlined *is* the ground with
 * an edge drawn round it - which is why the outlined one is the variant that shows whether a
 * theme's tiers are far enough apart to tell, and why all three are in one picture.
 *
 * A card is built row by row and then drawn, rather than drawn piece by piece. That is what
 * lets it be measured before it is placed: a column of cards has to know how tall each one is
 * to decide how many fit, and a card that reported one height and drew another would overrun
 * the one below it.
 */

#include "gallery.h"

enum { GALLERY_ANIM_CARD_METER = 0x0C00 };

void gallery_scene_cards(struct inkcell_backend_fb_state *state) {
    struct inkcell_fb_layout layout = gallery_frame(state, GALLERY_STR_HEAD_CARDS, 1U);
    static const struct inkcell_scale k_permille = {.min = 0, .max = 1000};
    static const struct inkcell_band k_band = {.warn = 700, .bad = 880};
    int y = layout.body_y;

    /* Filled, with a reading of each kind a card row can hold: a field, a meter against a band,
       a proportion, and a note under them. */
    struct inkcell_fb_card filled;
    inkcell_fb_card_begin(&filled, INKCELL_FB_CARD_FILLED, INKCELL_ICON_POWER,
                          gallery_id(GALLERY_STR_READ_CHARGE), INKCELL_TONE_NORMAL);
    inkcell_fb_card_row_text(&filled, INKCELL_TONE_NORMAL, gallery_id(GALLERY_STR_ROW_BATTERY),
                             "64%");
    inkcell_fb_card_meter(&filled, INKCELL_TONE_PRIMARY, gallery_id(GALLERY_STR_READ_UTILISATION),
                          780, k_permille, &k_band, GALLERY_ANIM_CARD_METER);
    static const uint32_t k_parts[] = {45U, 25U, 20U, 10U};
    inkcell_fb_card_proportion(&filled, INKCELL_TONE_NORMAL, gallery_id(GALLERY_STR_READ_MIXTURE),
                               k_parts, (uint32_t)(sizeof k_parts / sizeof k_parts[0]));
    inkcell_fb_card_note(&filled, INKCELL_TONE_DIM, gallery_text(GALLERY_STR_NOTE_WRAPPED));
    (void)inkcell_fb_draw_card(state, &layout, &y, &filled);
    y += inkcell_fb_space(state, INKCELL_SPACE_SM);

    /* Elevated, carrying the verbs that act on it - one of them under the cursor, which is what
       the focus ring is drawn for. */
    struct inkcell_fb_card elevated;
    inkcell_fb_card_begin(&elevated, INKCELL_FB_CARD_ELEVATED, INKCELL_ICON_NETWORK,
                          gallery_id(GALLERY_STR_ROW_NETWORK), INKCELL_TONE_SUCCESS);
    inkcell_fb_card_row_text(&elevated, INKCELL_TONE_NORMAL, gallery_id(GALLERY_STR_READ_SIGNAL),
                             "-71 dBm");
    inkcell_fb_card_row_text(&elevated, INKCELL_TONE_SUCCESS, gallery_id(GALLERY_STR_ROW_SECURITY),
                             gallery_text(GALLERY_STR_VAL_ON));
    inkcell_fb_card_action(&elevated, gallery_id(GALLERY_STR_ACT_OPEN), true);
    inkcell_fb_card_action(&elevated, gallery_id(GALLERY_STR_ACT_RETRY), false);
    (void)inkcell_fb_draw_card(state, &layout, &y, &elevated);
    y += inkcell_fb_space(state, INKCELL_SPACE_SM);

    /* Outlined, in the tone that says something is wrong. A card's tone colours its heading and
       its edge, not its fill: the fill is a tier, and a tier that changed colour with the news
       would be a panel that moved when the news did. */
    struct inkcell_fb_card outlined;
    inkcell_fb_card_begin(&outlined, INKCELL_FB_CARD_OUTLINED, INKCELL_ICON_WARNING,
                          gallery_id(GALLERY_STR_ROW_STORAGE), INKCELL_TONE_ERROR);
    inkcell_fb_card_row_text(&outlined, INKCELL_TONE_ERROR, gallery_id(GALLERY_STR_ROW_STORAGE),
                             "12.4 GB");
    inkcell_fb_card_note(&outlined, INKCELL_TONE_DIM, gallery_text(GALLERY_STR_BANNER_SUPPORTING));
    (void)inkcell_fb_draw_card(state, &layout, &y, &outlined);

    gallery_footer(state, &layout);
}
