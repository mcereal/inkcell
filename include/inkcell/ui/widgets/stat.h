#ifndef INKCELL_BACKENDS_FB_WIDGETS_STAT_H
#define INKCELL_BACKENDS_FB_WIDGETS_STAT_H

/*
 * The stat tile: one figure, large, with what it is a figure of.
 *
 * A card row is a label and a value at the body size, read a line at a time, and that is the
 * right shape for a handful of facts about one thing. It is the wrong shape for the four or five
 * numbers a dashboard opens on - how many are online, how busy the air is, how long the link has
 * been up - because those are not read, they are *glanced at*: from across a desk, between two
 * other things, by somebody who wants to know whether to look closer. Every dashboard on every
 * platform answers that the same way, with the figure set two sizes up and the question in small
 * type over it, and this is that.
 *
 * Four decisions in it:
 *
 *   - **The figure is the display role, in tabular figures.** It is the one thing on the tile
 *     and it is redrawn as it changes, so a proportional "18%" becoming "19%" would shuffle the
 *     whole figure sideways by a pixel. When it does not fit the tile's width it steps down a
 *     role at a time - headline, title, body - rather than being cut: half a number is not a
 *     number. The dial makes the same walk for the same reason.
 *   - **The tone is the figure's, and the label stays quiet.** The tile reports a judgement the
 *     way a card heading does - a figure in the warning tone is a figure past its band - and the
 *     label is the question, which repeats across the row of tiles and recedes so the answers
 *     do not.
 *   - **One picture under it, at most, and only a row-height one.** A meter for a level against
 *     its band, or a sparkline for the way it has been going. The tile is a glance; a chart with
 *     axes is a screen (or a card drawn into a box, inkcell_fb_draw_card_in()).
 *   - **It is read, not pressed.** No verbs and no focus: a tile summarises something, and the
 *     verbs belong to whatever it summarises. A tile that took the cursor would be a second way to
 *     press the same thing, and a d-pad walking a row of five of them to reach one button is a
 *     row of five presses spent on nothing.
 *
 * The surface is a card's, by the card's variants, so a board of tiles and cards reads as one set
 * of panels on one ground.
 *
 * inkcell/ui/widgets.h is the umbrella over this file and its siblings; include either.
 */

#include "inkcell/ui/fb_draw.h"

#include "inkcell/i18n/strings.h"
#include "inkcell/ui/icon.h"
#include "inkcell/ui/layout.h"
#include "inkcell/ui/theme.h"
#include "inkcell/ui/widgets/card.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* What, if anything, is drawn under the figure. */
enum inkcell_fb_stat_picture {
    INKCELL_FB_STAT_NONE = 0,
    /* A level on its domain, against its band: a meter across the tile. */
    INKCELL_FB_STAT_METER,
    /* Which way it has been going: a sparkline across the tile. */
    INKCELL_FB_STAT_TREND,
};

struct inkcell_fb_stat {
    /* The whole tile. Its height is the caller's - usually the row's, so a row of tiles lines up -
       and inkcell_fb_stat_height() is what the content wants. */
    struct inkcell_fb_rect rect;
    enum inkcell_fb_card_variant variant;
    /* Beside the label, at its size; INKCELL_ICON_NONE for none. */
    enum inkcell_icon icon;
    /* The question: "Channel use", "Online". A string id, like a card heading. */
    inkcell_str_id label;
    /* The answer, already formatted in its own units - "18%", "9 / 42", "9d 8h". */
    const char *value;
    /* A line under the figure saying what qualifies it - "4% ours", "of 42 known" - or NULL. */
    const char *caption;
    /* The figure's tone. NORMAL draws it in body text; a status tone is the tile's judgement. */
    enum inkcell_tone tone;

    enum inkcell_fb_stat_picture picture;
    /* METER: the reading, its domain and its band - the card meter's fields, for its reasons. A
       zeroed scale is permille. `meter_id` keys the easing, 0 for none. */
    int32_t meter_value;
    struct inkcell_scale meter_scale;
    const struct inkcell_band *meter_band;
    uint32_t meter_id;
    /* TREND: the samples, projected as the sparkline takes them. Borrowed for the call. */
    const struct inkcell_polyline *trend;
};

/* The height the tile's content wants at `rect.w`: its padding, the label, the figure at the size
   it will actually be drawn at, the caption and the picture. A caller sizing a row of tiles takes
   the tallest of these. */
int inkcell_fb_stat_height(const struct inkcell_draw_state *state,
                           const struct inkcell_fb_layout *layout,
                           const struct inkcell_fb_stat *stat);

/* Draws it, filling `rect`. The content is set from the top; the picture sits on the bottom
   padding, so a tile taller than it needs to be keeps its bar on its floor. Mutable state for the
   meter's easing. */
void inkcell_fb_draw_stat(struct inkcell_draw_state *state, const struct inkcell_fb_layout *layout,
                          const struct inkcell_fb_stat *stat);

#ifdef __cplusplus
}
#endif

#endif /* INKCELL_BACKENDS_FB_WIDGETS_STAT_H */
