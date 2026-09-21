#ifndef INKCELL_BACKENDS_FB_WIDGETS_H
#define INKCELL_BACKENDS_FB_WIDGETS_H

/*
 * The components the screens are assembled from.
 *
 * The layering under src/ui/backends/ is:
 *
 *   inkcell_fb_draw.c     pixels, glyphs, the theme lookups, page geometry   "how to put ink down"
 *   inkcell_fb_widgets_*  buttons, chips, list rows, field rows, rules       "what things look
 * like" inkcell_fb_screens_*  one renderer per screen, one file each             "what is on this
 * screen"
 *
 * A screen renderer should read as a description of its content: what the list holds, what
 * each row says, which rows are actions. If it is computing a pixel coordinate, a scroll
 * offset or a padding width, that belongs down here instead - those are the three things every
 * screen used to re-derive, and the three things that were subtly wrong in a different way on
 * each of them.
 *
 * This header is the umbrella: one file per group of components, and this pulls them all in.
 * A file that wants one group can include that group's header instead, the way the UI store's
 * subjects are included - which of them a component is filed under is not part of its
 * interface, and a `struct inkcell_fb_meter` is `struct inkcell_fb_meter` from either door.
 *
 *   inkcell_fb_widgets_button.h    the button, the chip, a strip of chips, the badge
 *   inkcell_fb_widgets_chrome.h    the app bar, the navigation and action bars, the banner, the
 * rule inkcell_fb_widgets_list.h      the list window, its cards and rail, the subheader and the
 * note inkcell_fb_widgets_item.h      one list row and its slots, and the conversation cell
 *   inkcell_fb_widgets_grid.h      the grid window, and the tile it lays out
 *   inkcell_fb_widgets_bubble.h    the transcript: a message, and the separator between two of them
 *   inkcell_fb_widgets_card.h      a card, built row by row and then drawn
 *   inkcell_fb_widgets_control.h   the switch, the checkbox and radio, the segmented button, the
 * field inkcell_fb_widgets_meter.h     a quantity as a length, and a reading over time
 *   inkcell_fb_widgets_overlay.h   the dialog, the menu, the bottom sheet, the snackbar, the QR
 *   inkcell_fb_widgets_scroll.h    a viewport over content in pixels, its rail, the large title
 *   inkcell_fb_widgets_keyboard.h  the on-screen keyboard's grid, over the model in ui/keyboard.h
 *   inkcell_fb_widgets_focus.h     the focus ring, travelling between the boxes a frame drew
 *
 * The first four of those overlays are *content for a layer* rather than components that
 * place themselves: include/inkcell/ui/overlay.h is the box, the way in and out, the scrim and
 * the stack, and it is public API rather than one of these. That split is why the menu and the
 * sheet exist at all - each of them is a measure and a loop now that none of them has to bring
 * its own answer to "where does this go and how does it get there".
 *
 * Tones - what a thing *is*, rather than which colour to draw it - live in
 * include/inkcell/ui/theme.h as `enum inkcell_tone`, because they are the UI's vocabulary rather
 * than this backend's. A screen names one, the theme answers, and inkcell_fb_tone_color() on the
 * state is the only place the two meet.
 *
 * A widget takes a tone, never a colour, for the same reason a stylesheet has a token called
 * "danger" instead of the hex for red: it is what lets a theme change the answer.
 *
 * `struct inkcell_fb_rect` is not here: a box in pixels is the drawing layer's vocabulary rather
 * than any one component's, so it sits in inkcell_fb_internal.h with the rest of the geometry.
 *
 * Not public API. include/inkcell/ui/fb.h is; nothing outside src/ui/backends/ should
 * include this.
 */

#include "inkcell/ui/widgets/bubble.h"
#include "inkcell/ui/widgets/button.h"
#include "inkcell/ui/widgets/card.h"
#include "inkcell/ui/widgets/chrome.h"
#include "inkcell/ui/widgets/control.h"
#include "inkcell/ui/widgets/focus.h"
#include "inkcell/ui/widgets/grid.h"
#include "inkcell/ui/widgets/item.h"
#include "inkcell/ui/widgets/keyboard.h"
#include "inkcell/ui/widgets/list.h"
#include "inkcell/ui/widgets/meter.h"
#include "inkcell/ui/widgets/overlay.h"
#include "inkcell/ui/widgets/scroll.h"

#endif /* INKCELL_BACKENDS_FB_WIDGETS_H */
