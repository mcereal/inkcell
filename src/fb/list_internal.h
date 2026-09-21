#ifndef INKCELL_BACKENDS_FB_WIDGETS_LIST_INTERNAL_H
#define INKCELL_BACKENDS_FB_WIDGETS_LIST_INTERNAL_H

/*
 * The handful of answers the list window and the row it draws both need.
 *
 * inkcell_fb_widgets_list.c owns the window - where a row lands, whether it stands on a card, what
 * the cursor is on - and inkcell_fb_widgets_item.c draws into it. Everything here would still be
 * `static` if the two were one file; it is declared only because splitting them is what keeps the
 * window's arithmetic and the row's slots readable apart.
 *
 * The rail at the end is the one entry here a third file reads: a grid scrolls a window of items
 * too, and a second derivation of the same mark in the same gutter is how two scrollbars on one
 * toolkit come to sit a pixel apart.
 *
 * Not public API, and not part of inkcell_fb_widgets.h: nothing outside src/fb/ should include
 * it.
 */

/* Self-contained rather than leaning on the includer having got there first: the rail below is
   read by a file that draws no list rows at all, and a header whose types only resolve in the
   two files it grew up in is a header the third one includes in the wrong order. */
#include "inkcell/ui/widgets/list.h"

/*
 * The disc and what is in it: a node's initials, or an icon for the rows that are not a person.
 *
 * Shared by two components and therefore neither's: a list row's leading slot draws one, and so
 * does a card heading over rows that carry them. See inkcell_fb_widgets_list.c for what it measures
 * and why the fit is measured rather than assumed.
 */
void inkcell_fb_draw_avatar(const struct inkcell_backend_fb_state *state, int x, int y, int size,
                            const char *label, enum inkcell_icon icon, struct inkcell_paint paint);

/* Whether item `index` draws the cursor's highlight. Never on a list whose card is focused
   instead, which is what INKCELL_FB_LIST_FOCUS_CARD means. */
bool inkcell_fb_list_is_cursor(const struct inkcell_fb_list *list, uint32_t index);

/* Whether item `index` stands on a card at all. */
bool inkcell_fb_list_on_card(const struct inkcell_fb_list *list, uint32_t index);

/* Whether this list draws its groups as cards at all - a question about the list, not the row. */
bool inkcell_fb_list_has_cards(const struct inkcell_fb_list *list);

/* The card surfaces and the scroll rail, drawn once before the first row. Every entry point
   that draws a row calls it, and it does its work only on the first. */
void inkcell_fb_list_chrome(const struct inkcell_backend_fb_state *state,
                            struct inkcell_fb_list *list);

/*
 * Registers the box item `index` was drawn in, under the list's focus base.
 *
 * Takes the box rather than deriving it: a plain row fills the height the model gave it and a
 * slotted item fills the box it measured for itself, and the rectangle the cursor can reach has
 * to be the one that was actually filled. Does nothing on a list with no base, which is every
 * list drawn by a screen that has not asked for this.
 */
void inkcell_fb_list_focus_row(const struct inkcell_backend_fb_state *state,
                               const struct inkcell_fb_list *list, uint32_t index, int y, int h);

/*
 * The clip a gliding list draws its rows inside: the body it was opened against.
 *
 * Taken and released around each row rather than around the walk, so a screen that stops
 * drawing early cannot leave the rest of its frame clipped - and so no list entry point needs a
 * closing call that every existing screen would have to learn. Returns whether a band was taken;
 * hand that back to the end. Both are no-ops on a list that is not gliding, which is every list
 * that did not ask to.
 */
bool inkcell_fb_list_band_begin(const struct inkcell_fb_list *list);
void inkcell_fb_list_band_end(const struct inkcell_fb_list *list, bool began);

/*
 * The scroll rail beside a window of items: a track `track_h` tall at `track_y`, with a thumb
 * whose length is the fraction of `window` on screen.
 *
 * Drawn by the component rather than asked for by a screen - a rail is derived entirely from the
 * window, so a screen has nothing to say about it and one that had to remember the call would
 * forget on one list out of nine. Shared by the list and the grid; see the note above its
 * definition in widgets_list.c for why the window and the track are two arguments.
 */
void inkcell_fb_draw_list_rail(const struct inkcell_backend_fb_state *state,
                               const struct inkcell_list *window, int track_y, int track_h);

/* The ink a row's text takes: its tone, or the cursor's, or the quiet pairing for the slots
   that are deliberately secondary. */
struct inkcell_rgb inkcell_fb_item_ink(const struct inkcell_backend_fb_state *state,
                                       enum inkcell_tone tone, bool selected, bool quiet);

#endif /* INKCELL_BACKENDS_FB_WIDGETS_LIST_INTERNAL_H */
