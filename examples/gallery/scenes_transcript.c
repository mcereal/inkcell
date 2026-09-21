#define _POSIX_C_SOURCE 200809L

/*
 * The transcript: a column of messages, which is the one screen in a handheld client that is
 * not a list of settings.
 *
 * A bubble is the component with the most states that are not visual decoration: inbound and
 * outbound sit on opposite sides and take opposite fills, a failed one has to say so without
 * being mistaken for an alert, a reply quotes the message above it, and the meta line under it
 * carries the clock, the lock and the delivery mark. All of those are here, because the way a
 * transcript goes wrong is that one of them reads as another.
 *
 * Rows are measured before they are drawn, for the reason a card's height is: a transcript
 * scrolls from the bottom, so the window has to know how tall each message is before it can
 * decide which of them fit.
 */

#include "gallery.h"

void gallery_scene_transcript(struct inkcell_draw_state *state) {
    struct inkcell_fb_layout layout = gallery_frame(state, GALLERY_STR_HEAD_TRANSCRIPT, 0U);

    const struct inkcell_fb_bubble bubbles[] = {
        /* The day separator, which the first message of a day carries rather than being a row
           of its own - a separator is a property of the message under it. */
        {.separator = gallery_text(GALLERY_STR_CHAT_TODAY),
         .separator_tone = INKCELL_TONE_DIM,
         .name = gallery_text(GALLERY_STR_CHAT_NAME),
         .text = gallery_text(GALLERY_STR_CHAT_INBOUND),
         .meta = {.clock = "09:14", .lock = INKCELL_ICON_ENCRYPTED}},
        /* Outbound and delivered: the other side, the other fill, and a mark that says it
           arrived. */
        {.text = gallery_text(GALLERY_STR_CHAT_OUTBOUND),
         .outbound = true,
         .meta = {.clock = "09:21", .state = INKCELL_ICON_DELIVERED}},
        /* A reply, quoting what it answers. */
        {.name = gallery_text(GALLERY_STR_CHAT_NAME),
         .quote = gallery_text(GALLERY_STR_CHAT_OUTBOUND),
         .text = gallery_text(GALLERY_STR_NOTE_WRAPPED),
         .meta = {.clock = "09:30", .relay = "2 hops", .reactions = "\xF0\x9F\x91\x8D 2"}},
        /* Under the cursor, so the selection fill is visible against a bubble rather than
           against the ground. */
        {.text = gallery_text(GALLERY_STR_CHAT_OUTBOUND),
         .outbound = true,
         .selected = true,
         .meta = {.clock = "09:44", .state = INKCELL_ICON_SENDING}},
        /* And one that did not go. A failure has to read as a failure without reading as an
           alert: the client is not warning about anything, it is reporting. */
        {.text = gallery_text(GALLERY_STR_CHAT_OUTBOUND),
         .note = gallery_text(GALLERY_STR_CHAT_FAILED),
         .outbound = true,
         .failed = true,
         .meta = {.clock = "09:47", .state = INKCELL_ICON_UNDELIVERED}},
    };

    int y = layout.body_y;
    const int floor_y = layout.footer_y - inkcell_fb_gutter(state);
    for (size_t i = 0U; i < sizeof bubbles / sizeof bubbles[0]; ++i) {
        /* Measured first: a bubble drawn past the bottom of the body is a bubble painted over
           the keycap row, and the rows it wants are not a function of anything the loop knows. */
        const uint32_t rows = inkcell_fb_bubble_rows(state, &layout, &bubbles[i]);
        const int height = (int)rows * layout.line;
        if (y + height > floor_y) {
            break;
        }
        inkcell_fb_draw_bubble(state, &layout, y, &bubbles[i]);
        y += height;
    }

    gallery_footer(state, &layout);
}
