#ifndef INKCELL_BACKENDS_FB_WIDGETS_LIST_H
#define INKCELL_BACKENDS_FB_WIDGETS_LIST_H

/*
 * The list: the window onto the items, the card surfaces under them, the scroll rail beside
 * them, and the two rows that are not items - a subheader and a note.
 *
 * This is the window and its furniture; what one row *says* is inkcell/ui/widgets/item.h. The two
 * split where a screen's own reach does: every screen opens a list and walks it, and only some of
 * them fill it with the full slotted row.
 */

/*
 * inkcell/ui/widgets.h is the umbrella over this file and its siblings; include either.
 */

#include "inkcell/ui/fb_draw.h"

#include "inkcell/ui/icon.h"
#include "inkcell/ui/layout.h"
#include "inkcell/ui/theme.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * A scrolling list of rows, drawn top to bottom.
 *
 * Owns the window arithmetic (via struct inkcell_list) and the y cursor, which is the whole of
 * what the eight screen renderers used to repeat. The shape is always:
 *
 *     struct inkcell_fb_list list = inkcell_fb_list_begin(layout, count, nav->cursor[SCREEN]);
 *     uint32_t i;
 *     while (inkcell_fb_list_next(&list, &i)) {
 *         ... build the row ...
 *         inkcell_fb_list_row(state, &list, i, text, tone);
 *     }
 */
struct inkcell_fb_list {
    struct inkcell_list model;
    int y;    /* next row's baseline */
    int line; /* one step's advance - a body row */
    /* The body the list was opened against, for the scroll rail: where it starts and how tall
       it is. Taken from the layout at inkcell_fb_list_begin*() rather than accumulated as rows are
       drawn, because a rail has to be the length of the *window* whether or not the items
       filled it. */
    int track_y;
    int track_h;
    /*
     * The card ordinal per item, or NULL for a list drawn straight onto the panel. Borrowed for
     * the life of the list, on the same terms as the model's heights - see the card-list note
     * below.
     */
    const uint8_t *cards;
    /* Whether the chrome - the card surfaces, then the scroll rail - has been drawn for this
       list. It is drawn by the first row that draws, not by the screen: see
       inkcell_fb_list_chrome() in src/fb/widgets_list.c. */
    bool chrome_drawn;
    /* The cursor stands on its card rather than on its row: no row takes the highlight and the
       card draws the focus ring instead. See inkcell_fb_list_begin_focus(). */
    bool focus_card;
    /* What the d-pad calls the rows: item `index` is `focus_base + index`. Set by
       inkcell_fb_list_focus(), zero otherwise. */
    uint32_t focus_base;
    /*
     * ---- the glide ----
     *
     * How far the content is displaced from where the window says it belongs, and the state
     * that is allowed to say so. Both are zero and NULL on a list that jumps, which is every
     * list that has not called inkcell_fb_list_glide().
     *
     * The state is held here for the one thing a row draw cannot otherwise do: take the clip
     * band. A row entry point is handed the state immutably - that is its statement that a row
     * does not animate - and a gliding row does, so the handle it animates through is the
     * list's rather than the parameter's. It is borrowed for the frame, like `heights`.
     */
    struct inkcell_backend_fb_state *glide_state;
    int glide_dy;
    /*
     * How tall the window is, which is what a glide is clipped to.
     *
     * Not `track_h`, which is deliberately the *body's* height whatever the window holds - a
     * rail measures what a full window would have been. A list opened with an explicit window
     * (inkcell_fb_list_begin_visible()) has rows below it that belong to whatever the screen
     * reserved them for, and a glide painting into them would be this list drawing over
     * somebody else's content.
     */
    int band_h;
    /* The items above the window and below it that the glide draws, so it has something to
       slide in from: `*_count` is the frame's fact and `*_pending` is what the walk has left. A
       press moves a window by as many rows as the model's lookahead gives back, so this is a
       count rather than a flag. */
    uint32_t lead_count;
    uint32_t tail_count;
    uint32_t lead_pending;
    uint32_t tail_pending;
};

/* One row per item, filling the body. */
struct inkcell_fb_list inkcell_fb_list_begin(const struct inkcell_fb_layout *layout, uint32_t count,
                                             uint32_t cursor);

/* `per_item` rows per item - the conversation list spends two, a name and a preview. */
struct inkcell_fb_list inkcell_fb_list_begin_rows(const struct inkcell_fb_layout *layout,
                                                  uint32_t count, uint32_t cursor,
                                                  uint32_t per_item);

/*
 * ---- a list drawn as a column of cards ----
 *
 * The same scrolling list, with its groups standing on card surfaces instead of on the panel.
 *
 * It exists for the reason inkcell_fb_card does one screen over: a hundred and twenty
 * label-and-value rows separated by dimmed words is a wall, and nothing in it says that Long name
 * and Short name are one subject while Temperature and Humidity are another. A dimmed heading is a
 * group distinguished from its own rows by *colour alone*, which is the one thing the type scale
 * landed to stop; a fill and an edge say it the way every phone and desktop platform says it.
 *
 * What it is not is inkcell_fb_draw_card(). That component is declared-then-drawn and measures
 * itself against the body, which is right for the Status tab's fixed column of four and impossible
 * here: this list is longer than the panel by a factor of eight, the window moves a row at a
 * time, and a card is routinely cut by both edges of it at once. So a card here is a *surface
 * behind a run of rows the list already knows how to place* - no second measure, no second
 * clip, and above all no second opinion about how tall a row is. The model stays the authority
 * on every height, exactly as it is for inkcell_fb_list_begin_heights(), and the cards are painted
 * from the window it settled.
 *
 * A screen declares the grouping the same way it declares the heights: one byte per item,
 * borrowed for the life of the list. Items carrying the same card ordinal *and lying next to
 * each other* are one card; INKCELL_FB_LIST_NO_CARD is an item standing on the bare panel, which is
 * what a list with no grouping at all passes for every row.
 *
 * Three things follow from the surface being painted before the rows rather than by them:
 *
 *   - **The cards are drawn by the first row that draws, not by the screen.** The scroll rail's
 *     rule, for the rail's reason: which card covers which rows is derived entirely from the
 *     model and the array, so a screen has nothing to say about it and a screen that had to
 *     remember the call is a screen that would forget on one list out of nine.
 *   - **A card cut by the window keeps its corners square on the cut end.** A rounded corner
 *     halfway down a scroll is a card claiming to end where the panel merely stopped, and a
 *     reader cannot tell that from a card that really did end there. The cut end keeps its
 *     inset as well as its corners, or the hairline would run across the cut and say it again -
 *     in a straight line this time. inkcell_fb_fill_round_rect_ends() is what draws it.
 *   - **Rows on a card are drawn against the card's surface**, not against the background, so
 *     the ink they blend their edges into is the colour actually under them. Every list entry
 *     point below takes that from the model, so a screen cannot get it wrong by forgetting.
 *
 * What separates two cards is a group's own heading, standing between them rather than inside
 * either: a screen gives its heading rows INKCELL_FB_LIST_NO_CARD, so the card above closes under
 * its last row and the card below opens at its first, with the label in the break naming the group
 * it opens. That is where the column gets the only air it has. A card here is a surface painted
 * round row boxes that were laid out for a flat list, so the room it can be padded with is
 * whatever a heading's step is not using - a line advance less a label's, which is a few pixels
 * at the device's scale - and split three ways between a card's bottom, the break and the next
 * card's top, none of the three was big enough to see. Spent on two edges instead of three, with
 * the heading itself standing in the break, each is the inset inkcell_fb_draw_card() uses one tab
 * over.
 *
 * The inset is at the bottom only, and the hairline is spent outward at the top. A row's box is
 * a line advance tall and a glyph's ink sits high in its cell, so the top of a card's first row
 * already carries most of a line's leading as air while the bottom of its last carries none -
 * padding both ends alike leaves the card top-heavy by exactly that leading. What the top does
 * take is the edge, for the reason the sides do: the first row of a card is a row the cursor can
 * stand on, and a hairline drawn inside the row box is a hairline the highlight paints out.
 *
 * So the grouping still costs **no rows at all**, and the nav, the row budget and every count in
 * the ui_nav_nodes suite are untouched by it.
 */

/* An item standing on the panel rather than on a card: a group's heading, in the break between
   the card that ended and the one it opens. The whole array, for a list with no grouping - which
   is every list that passes no array at all.

   The only sentinel, and that is worth stating because there used to be a second. A settings
   group that mixed fields and verbs stood its verbs on the bare panel under the card holding its
   fields, which needed a step the card above could *not* pad into; a group is one card whatever
   is in it now, so the step below a card is a heading or it is the end of the list. */
#define INKCELL_FB_LIST_NO_CARD 0xFFU

/*
 * Rows that are not all the same height: `heights` is one row count per item, and it is
 * borrowed for the life of the list.
 *
 * The measure is the caller's because the caller is the only thing that knows: whether a row
 * carries a bar under its words is a fact about that row's content, and the screen has already
 * walked its items to build them. What must not happen is the screen measuring one way and the
 * component drawing another, so the *list* is the authority once it has been told - every entry
 * point below advances by the height the model holds for that index, never by what the item it
 * was handed looks like. A row whose height the screen forgot to declare therefore draws short
 * rather than over the row beneath it.
 *
 * inkcell_transcript_window() takes the same shape, for the same reason - see include/inkcell/ui/
 * layout.h, where the window arithmetic lives and is unit tested.
 */
struct inkcell_fb_list inkcell_fb_list_begin_heights(const struct inkcell_fb_layout *layout,
                                                     uint32_t count, uint32_t cursor,
                                                     const uint8_t *heights);

/*
 * The same, with the items grouped onto card surfaces: `cards` is one ordinal per item and is
 * borrowed for the life of the list, exactly as `heights` is. See the card-list note above.
 *
 * `heights` may be NULL for a list whose rows are all one row tall, and `cards` may be NULL for
 * no grouping - in which case this is inkcell_fb_list_begin_heights() and nothing is painted.
 */
struct inkcell_fb_list inkcell_fb_list_begin_cards(const struct inkcell_fb_layout *layout,
                                                   uint32_t count, uint32_t cursor,
                                                   const uint8_t *heights, const uint8_t *cards);

/*
 * A card list whose window keeps items [first, last] in view around the cursor
 * (inkcell_list_begin_span()), and which, when `card` is set, focuses the cursor's card as a
 * whole: the card draws inkcell_fb_draw_card()'s focus ring and no row under it draws the
 * highlight.
 *
 * For a list whose cursor can stand on a group of facts rather than on a row - a fact is not a
 * control, and a row highlight over one promises a press that does nothing.
 */
struct inkcell_fb_list inkcell_fb_list_begin_focus(const struct inkcell_fb_layout *layout,
                                                   uint32_t count, uint32_t cursor,
                                                   const uint8_t *heights, const uint8_t *cards,
                                                   uint32_t first, uint32_t last, bool card);

/*
 * The colour item `index` is standing on: a card's surface, or the panel's background.
 *
 * Every row entry point below already asks this for itself, so a screen needs it only when it
 * draws something of its own beside a row - and when it does, it must ask rather than assume,
 * because a glyph blended against the wrong ground keeps its shape and gains a halo.
 */
enum inkcell_color inkcell_fb_list_ground(const struct inkcell_fb_list *list, uint32_t index);

/* An explicit window, for a screen that reserves body rows for something else. */
struct inkcell_fb_list inkcell_fb_list_begin_visible(const struct inkcell_fb_layout *layout,
                                                     uint32_t count, uint32_t cursor,
                                                     uint32_t visible);

/*
 * Register each row this list draws, under `base + index`.
 *
 * Called after one of the inkcell_fb_list_begin*() calls above and before the walk, on a frame
 * whose state carries a focus map (inkcell_fb_set_focus_map()). An index rather than an id per
 * row because only the list has a number of pressable things it does not know in advance -
 * everything else in this toolkit takes an id per thing - and because a screen's list cursor is
 * already an item index, so `focus_base + cursor` is the id it was going to hold anyway.
 *
 * **Only the rows that were drawn.** A window shows what fits and the rest of the list is
 * somewhere else; an item scrolled past, or clipped by the bottom of the body, registers
 * nothing. That is the point of registering from the draw, and it has a consequence worth
 * stating plainly: pressing down on the last visible row finds *nothing*, because there is
 * nothing drawn down there.
 *
 * That press is a scroll, and what answers it is `inkcell_focus_step()` rather than
 * `inkcell_focus_find()`: the screen hands the press a `struct inkcell_focus_run` - this base
 * and how many items there are in total - and the run answers for the part of the list that is
 * not on the panel while the map answers for the part that is. At the true ends of the list the
 * run has nowhere to go and the geometry decides, which is where leaving the list is what the
 * press means.
 *
 * The run is two numbers rather than this struct because a press happens between frames, when
 * no `struct inkcell_fb_list` exists - and because those two numbers are the whole of what the
 * press needs to know. See include/inkcell/ui/focus.h.
 *
 * The subheaders and notes between rows register nothing: they are labels, not places to
 * stand, so their item indices are simply absent from the map and a press cannot land on one -
 * on this frame. They are still item indices, though, so a run over a list that has them must
 * say which of its items are labels (`focusable` on struct inkcell_focus_run), or a press will
 * step onto a heading and the reader will have to press again to get past it. Not being
 * registered is what a label and a row scrolled off the panel have in common, and only one of
 * them is somewhere to put a cursor.
 */
void inkcell_fb_list_focus(struct inkcell_fb_list *list, uint32_t base);

/* How long a glide takes: the token a control acknowledging a press takes, because that is what
   a list moving a row is. The same one the focus ring travels on, so the two agree on a frame
   where both are moving. */
#define INKCELL_FB_LIST_GLIDE_MOTION INKCELL_MOTION_SHORT

/*
 * Glide this list between windows instead of jumping between them.
 *
 * A window that moves a row moves every row in it by a row's height at once, which on a panel
 * is the whole body flicking. What a reader is doing is travelling *through* a list, and a list
 * that jumps makes them find their place again at every step. So the content is drawn displaced
 * from where the window says it belongs, and the displacement eases to nothing.
 *
 * Called after one of the inkcell_fb_list_begin*() calls and before the walk. `id` names the
 * list, because what has to be remembered between frames is where its window was, and a second
 * list on the same frame must not inherit the first one's. There is one slot: a list that finds
 * another's window in it takes it over and does not glide on that frame, which is the right
 * answer for the frame a screen changes what its body is.
 *
 * Three things follow, and they are the reason this is opt-in rather than what a list does:
 *
 *   - **The walk yields one extra item**, above or below the window depending on which way the
 *     content is moving. There has to be something to slide in from, and a screen builds its
 *     rows by index, so the index is handed to it like any other. An item outside the list is
 *     never yielded, so the ends of a list glide against nothing, which is what the ends of a
 *     list look like.
 *   - **The rows are clipped to the body.** The extra one is half outside it by construction,
 *     and so is whichever row is leaving. Rows drawn past the body would paint over the app bar
 *     the frame is not redrawing.
 *   - **A leap is not a glide.** A window that moved further than a few rows - a filter
 *     emptying, a jump to the end - is a new place rather than a step, and gliding there would
 *     be a blur nobody can read. Past the cap the content is simply where it belongs.
 *
 * A frame already travelling does not glide: a screen arriving from the side has a transform of
 * its own, there is one per frame, and a body sliding two ways at once is not a thing to look
 * at. The list jumps that frame and glides from the next.
 */
void inkcell_fb_list_glide(struct inkcell_backend_fb_state *state, struct inkcell_fb_list *list,
                           uint32_t id);

bool inkcell_fb_list_next(struct inkcell_fb_list *list, uint32_t *index);

/* Rows item `index` occupies, from the list model. What a screen measuring something of its own
   against a row - a divider, a second column - has to advance by. */
uint32_t inkcell_fb_list_row_height(const struct inkcell_fb_list *list, uint32_t index);

/*
 * ---- the scroll rail ----
 *
 * A list longer than its window draws a rail in the right-hand gutter: a track the height of
 * the body, with a thumb whose length is the fraction of the list on screen and whose position
 * is how far down it is. Nothing else in the frame says either of those - a screen title that
 * carries a count is answering a different question, and on the Nodes tab a very different one
 * (how much of the radio's NodeDB we hold, which is not a scroll position and is not this).
 * A window that gives no sign there is more of the list is the one thing every list UI on every
 * platform has an answer for.
 *
 * **No screen asks for it.** It is drawn by the first row that draws, from the list model,
 * because a rail is derived entirely from `count`, `first` and `visible` - a screen has nothing
 * to say about it and a screen that had to remember the call is a screen that would forget on
 * one list out of nine. That is the same reasoning the list item's clipping follows: geometry
 * belongs down here, and the screen describes content.
 *
 * It lives in the half-margin *outside* the row fill, so it overlaps nothing: rows clip their
 * text at `xres - margin` and the cursor fill stops at `xres - margin / 2`. It is therefore
 * invisible to every measurement a row already makes, which is why adding it changed no row.
 *
 * It does not animate. The row entry points that draw it take the state immutably - unlike the
 * switch and the meter, whose sliding is the reason they take it mutably - and a rail that
 * eased would need a keyed slot per list. A thumb that jumps a row when the cursor moves a row
 * is not the thing motion was for.
 */

/* Draws the row - highlighted when `index` is the cursor - and advances. */
void inkcell_fb_list_row(const struct inkcell_backend_fb_state *state, struct inkcell_fb_list *list,
                         uint32_t index, const char *text, enum inkcell_tone tone);

/* Same, taking the line builder directly, which is how most rows are assembled. */
void inkcell_fb_list_row_line(const struct inkcell_backend_fb_state *state,
                              struct inkcell_fb_list *list, uint32_t index,
                              struct inkcell_line *line, enum inkcell_tone tone);

/* What sits at the row's leading edge. */
enum inkcell_fb_leading_kind {
    INKCELL_FB_LEADING_NONE = 0,
    /* A tinted disc with one or two cells - or one icon - in it. What lets the eye find a row
       by colour and two letters long before it has read a name. */
    INKCELL_FB_LEADING_AVATAR,
    /*
     * One icon in the gutter before the words: the star on a pinned node, the broken link on a
     * node the radio has forgotten, the bluetooth rune on a device row.
     *
     * The slot is reserved whenever the kind is set, `icon` of INKCELL_ICON_NONE included -
     * which is the point. A list where some rows have an icon and some do not is a list whose
     * text starts in two different columns, so a screen declares the slot for the whole list
     * and the rows with nothing to say leave it empty.
     */
    INKCELL_FB_LEADING_ICON,
    /*
     * The same icon, in a filled disc the width of an avatar: what Material puts at the leading
     * edge of a list item that is a *verb* rather than a fact.
     *
     * The disc is the row's own family held back until a symbol can sit on it - the container
     * slot, the pair a tonal button takes - and which family it is, is not a second thing the
     * caller says. It is `tone`'s, and the primary where the tone names none, which is word for
     * word the rule `accent_edge` follows a few fields down. That is the whole reason this is a
     * kind rather than a colour: the row states once what it means, and the disc, the words and
     * the marker bar are three renderings of that one statement.
     *
     * What it buys is the screen the node detail was: eleven verbs whose ink was the accent, so
     * a card of actions read as a wall of one colour and the row that deletes something had to
     * shout over ten rows already shouting. With the colour in the disc the words go back to the
     * ordinary ink, the eye runs down a column of *labels*, and the two rows that cost something
     * are the only two coloured words on the card.
     */
    INKCELL_FB_LEADING_TONAL,
    /*
     * The tonal gutter, reserved and left empty.
     *
     * INKCELL_FB_LEADING_ICON has always reserved its slot on every row of a list whether or not
     * the row filled it, because a list that indents only the rows with something to show is a list
     * whose text starts in two columns. A disc is wider than an icon, so a list mixing the two
     * needs the same promise kept at the disc's width - and until now there was no way to say
     * it: a row with nothing to put in a disc had to pick between drawing an empty circle and
     * starting its words a gutter to the left of every other row.
     *
     * A settings section that mixes verbs with fields is exactly that list. About is four rows,
     * two of them verbs, and it drew "Language" and "Theme" past a disc while "Version" and
     * "Updates" began at the panel's own padding. The cards were hiding it rather than fixing
     * it - each run got a surface and the mismatch moved to the boundary between them - which is
     * why removing a card that was saying nothing is what made it visible.
     *
     * A kind rather than TONAL with an empty icon, so a row that means "nothing here" cannot be
     * confused with one that forgot its symbol, and so inkcell_fb_draw_avatar() is never asked to
     * draw a disc with nothing in it.
     */
    INKCELL_FB_LEADING_TONAL_SLOT,
};

struct inkcell_fb_leading {
    enum inkcell_fb_leading_kind kind;
    const char *label; /* AVATAR: initials */
    /* ICON and TONAL, and AVATAR when a disc holds a symbol rather than letters. */
    enum inkcell_icon icon;
    uint32_t tint; /* AVATAR: seeds the disc's colour through the theme's avatar palette */
    /* AVATAR: a stated fill instead of a tint - the primary for "all traffic", the error
       colour for a row armed to be deleted. INKCELL_COLOR_COUNT means "use the tint". */
    enum inkcell_color role;
};

/*
 * A section header inside a list: the row that names the group under it.
 *
 * It was a dimmed row of body text, which is a heading distinguished from the rows it heads by
 * colour alone - the one thing the type scale landed to stop, and the half of it that could not
 * be done at the time because the list model counted rows of one height. It is drawn at
 * INKCELL_TYPE_LABEL and sat on the *bottom* of its step, so the space the smaller glyphs free
 * becomes air above it: the gap is what separates one group from the last one's rows, and it
 * costs nothing because the row was already that tall.
 *
 * Still a row of the list, and still highlightable - the cursor walks onto these on both
 * screens that draw them - so the fill is the step, whatever size the words in it are.
 *
 * On a list drawn as a column of cards this is the card's label, and it stands in the gap
 * between that card and the one that ended rather than on either - which is what pays for both
 * cards' insets and why the grouping costs no rows. It is centred in that gap instead of sitting
 * on the bottom of its step, because there it is not a break between two runs of rows: it names
 * the card under it, and a label seated against the card above would be naming the wrong one.
 * Its leading slot carries a symbol there and nowhere else: a heading over rows already carrying
 * icons of their own would be a second thing saying what the words under it say, but a *card*
 * heading is the one cell the eye finds when it is looking for Signal rather than Identity,
 * which is the same argument struct inkcell_fb_card's own icon is there for.
 */
void inkcell_fb_list_subheader(const struct inkcell_backend_fb_state *state,
                               struct inkcell_fb_list *list, uint32_t index, const char *text);

/*
 * The same heading, indented to a list that declares a leading slot.
 *
 * `leading` is the row's gutter and, on a card, its symbol. The gutter half is why it exists at
 * all: a heading over rows whose words begin an icon-box in would otherwise name a column
 * nothing is in.
 *
 * Whether the slot is *filled* is the card distinction. On a flat list it stays empty, because a
 * group there is a break between runs of rows and a symbol on it would repeat the words beside
 * it - the icons on such a list say what each row is about, and a group has no single answer to
 * that. On a column of cards the heading is the card's own, and there the symbol is what the eye
 * finds first when it is looking for Signal rather than Identity: the argument struct
 * inkcell_fb_card states for the icon beside *its* heading, which this is. The list knows which it
 * is drawing, so a caller passes the icon either way and nothing has to decide twice.
 *
 * inkcell_fb_list_subheader() is this with no slot, which is every list that has no icons in it.
 */
void inkcell_fb_list_subheader_icon(const struct inkcell_backend_fb_state *state,
                                    struct inkcell_fb_list *list, uint32_t index, const char *text,
                                    struct inkcell_fb_leading leading);

/*
 * ---- the note row ----
 *
 * A paragraph as a list row: a heading line at the label scale and the sentences under it,
 * wrapped across the list's whole width. What the help screen is made of, and the one row shape
 * here whose height is not a property of the row's *kind* but of the words in it.
 *
 * It exists because the two shapes that already wrap were both the wrong one.
 * inkcell_fb_card_note() stops at INKCELL_FB_CARD_NOTE_LINES, which is right for a sentence the
 * radio wrote into a card of other rows and wrong for the only content on a screen; and a list
 * item's supporting line is one line, elided, which is the shape for a reminder rather than for an
 * explanation.
 *
 * inkcell_fb_list_note_steps() is the measure and inkcell_fb_list_note() is the draw, and the
 * screen must ask the first before it opens the list - a note whose height the model was not told
 * about draws over the row beneath it. Two calls rather than one for the reason the settings slider
 * has two: the model is the authority on every height, and it can only be if it is told before the
 * first row is placed.
 *
 * `heading` may be NULL for a paragraph that names nothing, which is what the topic's own
 * opening note is.
 */
uint32_t inkcell_fb_list_note_steps(const struct inkcell_backend_fb_state *state,
                                    const char *heading, const char *body);

void inkcell_fb_list_note(const struct inkcell_backend_fb_state *state,
                          struct inkcell_fb_list *list, uint32_t index, const char *heading,
                          const char *body);

#endif /* INKCELL_BACKENDS_FB_WIDGETS_LIST_H */
