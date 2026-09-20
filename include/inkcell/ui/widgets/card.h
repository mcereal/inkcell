#ifndef INKCELL_BACKENDS_FB_WIDGETS_CARD_H
#define INKCELL_BACKENDS_FB_WIDGETS_CARD_H

/*
 * A card: a heading and a handful of rows, declared and then drawn.
 *
 * Unlike everything else here a card is *built* - a screen adds rows to it and asks afterwards
 * whether anything went in - because a row that only exists when the radio said something would
 * otherwise have the screen counting rows before it knew what they were.
 */

/*
 * Not public API. include/inkcell/ui/fb.h is; inkcell_fb_widgets.h is the umbrella over this file
 * and its siblings, and nothing outside src/ui/backends/ should include either.
 */

#include "inkcell/ui/fb_draw.h"

#include "inkcell/i18n/strings.h"
#include "inkcell/ui/icon.h"
#include "inkcell/ui/layout.h"
#include "inkcell/ui/theme.h"
#include "inkcell/ui/trend.h"

#include <stdbool.h>
#include <stdint.h>

/* ---- cards --------------------------------------------------------------------------------
 *
 * A card: a titled panel that groups rows which belong together.
 *
 * Every screen that is not a list is a column of label/value lines on the bare ground, and a
 * column of eighteen of them is a wall - nothing in it says that Transport, Radio and Sync are
 * one subject and Packets and Dropped are another. A card says it with a fill and a heading,
 * which is what the phone and desktop platforms settled on for the same reason.
 *
 * It is *declared, then drawn*, unlike every other component here, and that is forced by the
 * framebuffer: a card's fill has to go down before its text or it paints over it, and its
 * height is not known until the last row is in. So a screen fills a struct and hands it over:
 *
 *     struct inkcell_fb_card card;
 *     inkcell_fb_card_begin(&card, APP_STR_CARD_HEADING, INKCELL_TONE_PRIMARY);
 *     inkcell_fb_card_row_text(&card, INKCELL_TONE_NORMAL, APP_STR_LABEL_FIRST, status);
 *     if (connected) {
 *         inkcell_fb_card_row_text(&card, INKCELL_TONE_SUCCESS, APP_STR_LABEL_SECOND, name);
 *     }
 *     inkcell_fb_draw_card(state, layout, &y, &card);
 *
 * which is worth more than the drawing it saves: a conditional row is an `if` around one call
 * rather than a branch that has to remember to advance a y cursor by the right amount.
 *
 * The card owns its own geometry - the inset, the corner radius, the label column, where the
 * next card starts - and it owns the footer. A screen cannot lay a card over the footer: rows
 * that do not fit are dropped and the card says how many, rather than the screen re-deriving
 * the "does another row fit" test that was written out by hand on every dense screen.
 *
 * Colours are the theme's: the fill is a surface role picked by the card's variant, which every
 * theme already owes body text 4.5:1 and the four card tones 3:1, and the edge is
 * INKCELL_COLOR_OUTLINE, which has to be visible against all of them. Its corners are
 * INKCELL_SHAPE_MD, so how round a card is belongs to the theme like everything else about it.
 * The heading takes the card's own tone, so a card reports the state of what it holds - the
 * Link card goes bad when the radio is gone - without a second cue to invent.
 */

/*
 * How much weight a card is asking for.
 *
 * A column of cards drawn at one weight has no shape: Link and Mesh sat side by side on the
 * Status tab with nothing to say which one to read first, which is the same complaint the
 * button variants answer for a screen full of controls. Material has the same three and uses
 * them for the same thing.
 *
 * Here they are three *surface tiers* rather than three shadows. The Brick's display engine
 * composites fb0 against its own background layer, so there is no alpha and nothing to cast a
 * shadow into - the distance a card is off the ground is carried by its fill alone, which is
 * how Material's tonal elevation works and why it survives a light palette as well as a dark
 * one (see the tier comment in include/inkcell/ui/theme.h).
 *
 * All three keep the hairline. The edge is not decoration on a theme whose surface is a step
 * off the ground - it is the whole of what says a card is there - and an outlined card, whose
 * fill *is* the ground, would otherwise not be a card at all.
 */
enum inkcell_fb_card_variant {
    /* A panel on the ground: INKCELL_COLOR_SURFACE. The ordinary weight, and the zero value. */
    INKCELL_FB_CARD_FILLED = 0,
    /* A step further up: INKCELL_COLOR_SURFACE_HIGH. The card to read first. */
    INKCELL_FB_CARD_ELEVATED,
    /* The ground itself, held by its edge. The card that is on screen because the set would be
       incomplete without it, not because it has something to say. */
    INKCELL_FB_CARD_OUTLINED,
};

#define INKCELL_FB_CARD_ROWS_MAX 12U
#define INKCELL_FB_CARD_LABEL_MAX 24U
/* Wide enough for the longest thing a row carries whole, which is a radio notice
   (INKCELL_RADIO_NOTICE_TEXT_MAX). A value longer than this is clipped on a cell boundary. */
#define INKCELL_FB_CARD_VALUE_MAX 132U
/* A note is a sentence the radio wrote, not a value, and three lines is where it stops being
   worth the rows it costs on a 3.2" panel. */
#define INKCELL_FB_CARD_NOTE_LINES 3U

enum inkcell_fb_card_row_kind {
    INKCELL_FB_CARD_ROW_FIELD = 0, /* a label column and a value, as the field rows have */
    INKCELL_FB_CARD_ROW_NOTE,      /* a wrapped paragraph across the card's full width, no label */
    /* A label column and a meter across the rest of the row, in place of the value. One line
       like a field row, so a card with one costs no more room and the clip arithmetic above is
       unchanged - a bar is thinner than the text it sits among, not taller. */
    INKCELL_FB_CARD_ROW_METER,
    /* And the same shape again for a whole divided into its parts. One line, like the meter and
       for the meter's reason: a composition is read as lengths, and lengths are as thin as the
       theme's bar. It is the trend that needs two rows, because a shape needs a second
       dimension and a length does not. */
    INKCELL_FB_CARD_ROW_PROPORTION,
};

struct inkcell_fb_card_row {
    enum inkcell_fb_card_row_kind kind;
    enum inkcell_tone tone;
    char label[INKCELL_FB_CARD_LABEL_MAX];
    char value[INKCELL_FB_CARD_VALUE_MAX];
    /* METER: what the bar reads, the domain it reads it on, and what it is keyed on. Held by
       value rather than by pointer because a card is built, handed over and drawn - there is no
       caller-owned control to point at, unlike a list row's switch. The band goes the same way
       and for the same reason, with `meter_banded` standing in for the NULL a pointer would
       have carried. */
    int32_t meter_value;
    struct inkcell_scale meter_scale;
    struct inkcell_band meter_band;
    bool meter_banded;
    uint32_t meter_id;
    /* PROPORTION: the parts, held by value on the terms the band and the polyline are - a card
       is built, handed over and drawn, so a row pointing at a caller's array would be a card
       that only works while the frame that built it is still on the stack. */
    uint32_t parts[INKCELL_PROPORTION_PARTS];
    uint32_t part_count;
};

/*
 * A verb the card offers, drawn as a button against the far edge of its heading's line.
 *
 * "Radio actions" was a settings row that opened a screen because a card could not offer a
 * verb, and disconnecting meant walking to the Devices tab to press X on a card that was
 * already naming the radio. A card that reports on something is the place to act on it.
 *
 * The heading's line rather than a row under the content, which is where a phone puts card
 * actions and where this was first written. That cost a row per card carrying a verb, and the
 * screen it cost them on is the one that can outgrow the panel - so the two rows the Status
 * tab lost were a refused packet and a reboot count, which are the rows that card exists to
 * show. A heading is three or four cells of an otherwise empty line; the verbs go in the rest
 * of it and cost nothing.
 *
 * They are drawn at the chrome scale for the same reason the action bar's verbs are: a verb is
 * read beside the content rather than as part of it. A body-scale button here would also have
 * grown the header line by the difference and given a third of a row back.
 *
 * A button carries a word and no icon. The verbs the Status tab spends are the same ones the
 * action bar names for the same press, and the bar draws a keycap beside each - a symbol here
 * as well would be a third thing on the frame saying one press.
 */
struct inkcell_fb_card_action {
    char label[INKCELL_FB_CARD_LABEL_MAX];
    bool selected; /* the screen's cursor is on this button */
};

/* What fits beside a heading at the largest glyph scale with room for the words. A card wanting
   a fourth verb is a card that wants a screen. */
#define INKCELL_FB_CARD_ACTIONS_MAX 3U

struct inkcell_fb_card {
    enum inkcell_fb_card_variant variant;
    char heading[INKCELL_FB_CARD_LABEL_MAX];
    /* Beside the heading: what the card is about, said in one cell. A column of cards is a
       column of headings otherwise, and the icon is what the eye finds first when it is looking
       for the radio rather than the mesh. */
    enum inkcell_icon icon;
    enum inkcell_tone tone; /* the heading's, and so the card's own report on itself */
    /* Rows offered past the last are dropped. The cap is well above what any screen here fills
       - the densest is the Status tab's Radio card at six - so reaching it means a screen has
       outgrown one card rather than that the panel ran out, and wants two. */
    struct inkcell_fb_card_row rows[INKCELL_FB_CARD_ROWS_MAX];
    uint32_t count;
    /* The verbs. They are on the header line, so they are never among the rows dropped to make
       a card fit - a card that shed its buttons would leave the action bar naming a press with
       nothing behind it. A card whose heading and verbs together overrun its width keeps the
       verbs and cuts the heading, for the same reason. */
    struct inkcell_fb_card_action actions[INKCELL_FB_CARD_ACTIONS_MAX];
    uint32_t action_count;
    /*
     * What the d-pad calls the verbs: the first is this id, the second this id + 1, and so on
     * up to INKCELL_FB_CARD_ACTIONS_MAX. INKCELL_FOCUS_NONE for a card nothing navigates into.
     *
     * A base and an offset rather than an id per verb, because the verbs are declared one call
     * at a time and a screen would otherwise be naming them in two places - here and at
     * inkcell_fb_card_action(). The block is three wide and the cap says so, which is small
     * enough for a screen to reserve by hand and the reason the cap is a constant rather than a
     * number this file happens to stop at.
     *
     * The registration is the part worth having. A card is drawn against the room it has, and a
     * card too narrow for its verbs drops them **from the end** - so a screen that had reserved
     * a cursor position per declared verb would walk onto one that is not on the panel, which
     * is the failure the reservation note below describes from the other side. What goes into
     * the map is what was drawn: two verbs on a card that fitted two, however many were
     * declared.
     */
    uint32_t action_focus_id;
};

/* Starts a card. `heading` of INKCELL_STR_NONE is a card with no heading - a panel, not a
   section - and takes INKCELL_ICON_NONE with it. Always call this first: it is what clears the
   row list - and the focus base with it, so `action_focus_id` is set on the card after this
   call rather than before. The variant is stated here rather than defaulted, because which of three
   weights a card is asking for is a decision about the column it sits in and not a property of the
   card on its own. */
void inkcell_fb_card_begin(struct inkcell_fb_card *card, enum inkcell_fb_card_variant variant,
                           enum inkcell_icon icon, enum inkcell_str_id heading,
                           enum inkcell_tone tone);

/* A label and a value formatted from the catalog, which is the shape most rows have. */
void inkcell_fb_card_row(struct inkcell_fb_card *card, enum inkcell_tone tone,
                         enum inkcell_str_id label, enum inkcell_str_id value, ...);

/* The same row for a value that is already text - a device name, an age, a percentage a caller
   has formatted. It exists so no "%s" pass-through ends up in the catalog, where it would be a
   line for a translator to wonder about. Same split as inkcell_fb_draw_status_row/_text. */
void inkcell_fb_card_row_text(struct inkcell_fb_card *card, enum inkcell_tone tone,
                              enum inkcell_str_id label, const char *value);

/* A sentence, wrapped across the card's whole width with no label column. What the radio said
   about itself goes here: a firmware sentence in the value gutter is three words and a cut. */
void inkcell_fb_card_note(struct inkcell_fb_card *card, enum inkcell_tone tone, const char *text);

/*
 * A bar: the row for a number whose *level* is the point.
 *
 * `value` is a reading on `scale` - a zeroed scale meaning it is already permille - and `id`
 * keys the animation, on the same terms as a switch's: stable while the row is on screen,
 * unique within the frame, 0 for a bar that never moves of its own accord.
 *
 * `band` may be NULL for a plain bar. When it is not, the bar marks the boundaries on its track
 * and takes its fill from where the reading falls, resting in `tone` - so a card that colours
 * its heading by the same band is stating one threshold rather than agreeing with itself by
 * hand. It is copied, not retained.
 *
 * `label` of INKCELL_STR_NONE gives the bar the card's whole content width instead of a label
 * column, and that is the shape to reach for when the bar is *about the row above it* - which
 * is what the airtime pair on the Status card is. The words there already say what the number
 * is and how large it is; a label on the bar would be the third time, and it would cost the
 * track the third of its length that makes a fill readable as a proportion. A label is for a
 * bar that stands alone in a card of unrelated rows.
 *
 * It never carries the figure either way. A row that drew both would spend the card's width
 * saying one thing twice.
 */
void inkcell_fb_card_meter(struct inkcell_fb_card *card, enum inkcell_tone tone,
                           enum inkcell_str_id label, int32_t value, struct inkcell_scale scale,
                           const struct inkcell_band *band, uint32_t id);

/*
 * A divided bar: the row for what a reading is *made of*.
 *
 * `values` are the parts in the order they are drawn, and `tone` colours the label rather than
 * the bar - the parts take the theme's series palette, because which part a slice is is not a
 * judgement about it. See struct inkcell_fb_proportion for what a caller is promising by calling
 * this: that the parts are disjoint, and that whatever row names them names them in this order.
 *
 * Fewer than two parts, or parts summing to zero, adds no row at all - the sparkline's rule, for
 * the sparkline's reason. A bar with nothing in it says the radio heard nothing; a radio that
 * has not reported yet has said nothing, and those are different.
 *
 * `label` of INKCELL_STR_NONE gives the bar the card's whole content width, and that is the shape
 * to reach for: this row goes under the row that names its parts, exactly as the airtime meter
 * goes under the airtime figures, and a label here would be naming the subject a third time.
 */
void inkcell_fb_card_proportion(struct inkcell_fb_card *card, enum inkcell_tone tone,
                                enum inkcell_str_id label, const uint32_t *values, uint32_t count);

/*
 * A verb, as a button on the card's heading line. Declared left to right: the first call is the
 * leftmost button, which is also the first the screen cursor reaches.
 *
 * `selected` says the cursor is on it, which is also what makes the card itself read as
 * focused - see inkcell_fb_draw_card().
 *
 * A card carries the whole verb and nothing about the press: which button runs it is the action
 * bar's business, and a keycap drawn twice on one frame is a screen disagreeing with itself.
 */
void inkcell_fb_card_action(struct inkcell_fb_card *card, enum inkcell_str_id label, bool selected);

/* Whether anything was added. A card with no rows is not drawn, so a screen can build one
   unconditionally and let it disappear when the radio has reported nothing. A card with verbs
   and no rows is still empty: an action row alone is a button strip, not a card. */
bool inkcell_fb_card_is_empty(const struct inkcell_fb_card *card);

/*
 * The room a card needs to say *everything* it holds: its heading, every row, and its padding.
 * The other half of the reservation pair below - and on the same terms, so a screen can hand
 * either to inkcell_fb_draw_card_reserving() and get what it asked for. Neither carries the gap
 * between the two cards, because that gap belongs to the card doing the reserving.
 *
 * Which of the two a screen reserves is an editorial decision and is allowed to be a *reading*
 * rather than a constant. The Status tab is the worked example: its Radio card is a heading over
 * a battery figure while the radio is well and five rows of explanation when it is not, so the
 * card above it promises the minimum in the first case and the whole in the second. See
 * inkcell_fb_render_status().
 */
int inkcell_fb_card_height(const struct inkcell_backend_fb_state *state,
                           const struct inkcell_fb_layout *layout,
                           const struct inkcell_fb_card *card);

/*
 * Draws the card with its top edge at `*y` and advances `*y` past it.
 *
 * Rows that would fall past the body's bottom are dropped from the end and the card shrinks to
 * what is left, so a screen may hand over more cards than the panel holds and get the ones that
 * fit. A *note* is the exception: when it is the row that did not fit, it keeps as many of its
 * lines as the leftover room takes, because a note is a sentence explaining something no other
 * row can and half of it beats none of it. A field row is never halved - a label and half a
 * value is not half a fact - and nothing is drawn below a clipped note, since a truncated
 * paragraph with rows under it reads as a complete one.
 *
 * Returns false when not even the heading and one row - or one line of a note - fit, in which
 * case nothing is drawn and `*y` is untouched. That is also the answer for every card after it,
 * so a screen can stop.
 *
 * A card holding the selected action draws its edge in the primary instead of in
 * INKCELL_COLOR_OUTLINE, and draws it thicker: that is the focus ring, and it is the one cue
 * here that is not a state layer. A layer mixed into a fill this large is a change nobody
 * notices from across a table, and every tone written on the card would owe the result its own
 * contrast contract; the accent edge is the indicator Material uses for focus, it is read at a
 * glance, and PRIMARY already owes both grounds 3:1.
 */
/*
 * The least a card can be drawn as and still be one: its heading, its first row and its padding.
 * What a screen reserves for a card that must not disappear but has nothing urgent to say.
 *
 * The first *row* rather than a line, because a card is refused outright at the point it has
 * nothing but a heading - so this is the smallest height that actually draws something.
 */
int inkcell_fb_card_min_height(const struct inkcell_backend_fb_state *state,
                               const struct inkcell_fb_layout *layout,
                               const struct inkcell_fb_card *card);

/*
 * inkcell_fb_draw_card(), keeping `reserve` pixels of the body free below this card.
 *
 * Why a card needs to be told this at all. A column of cards is drawn in order and each takes
 * what it wants, so the *last* card pays for everything above it - and paying means not being
 * drawn, because inkcell_fb_draw_card() refuses a card it cannot fit rather than drawing an empty
 * box. Losing a card is worse than losing a row, and not only because it is more content: a card
 * carries **verbs**, and which verbs a screen offers is a table (src/ui/tables/status.c) with no
 * idea how tall anything came out. The cursor therefore keeps walking onto a button that is not on
 * the frame - which is exactly the failure "a card that can end up with no rows must not be
 * given a verb" names, reached from the layout side instead of the row-count side.
 *
 * A reservation turns it around: the card that can afford to drop a row drops one, and the card
 * that would have vanished survives. It is also the right order editorially, because rows are
 * clipped from the end and a screen declares its least important rows last.
 *
 * Pair it with inkcell_fb_card_min_height() of whatever comes next rather than with that card's
 * full height: the promise worth making is *that the card exists*, and a card given more room than
 * its minimum will use it.
 *
 * A reservation that cannot be afforded is dropped rather than honoured - two cards missing is
 * not an improvement on one - so this never draws less than inkcell_fb_draw_card() would have.
 */
bool inkcell_fb_draw_card_reserving(struct inkcell_backend_fb_state *state,
                                    const struct inkcell_fb_layout *layout, int *y,
                                    const struct inkcell_fb_card *card, int reserve);

bool inkcell_fb_draw_card(struct inkcell_backend_fb_state *state,
                          const struct inkcell_fb_layout *layout, int *y,
                          const struct inkcell_fb_card *card);

#endif /* INKCELL_BACKENDS_FB_WIDGETS_CARD_H */
