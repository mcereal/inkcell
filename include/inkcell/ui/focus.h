#ifndef INKCELL_FOCUS_H
#define INKCELL_FOCUS_H

/*
 * Where the d-pad goes: the rectangles a frame drew, and which one lies in a given direction.
 *
 * Every screen written against this toolkit so far has kept a cursor as an *index* and moved it
 * with `cursor++`. That is the right model for exactly one shape of screen - a single column of
 * rows, where the thing below is the next number - and it is the reason nothing here has a grid
 * in it. An index knows how many focusable things there are; it does not know where any of them
 * is, so it cannot answer the only question a d-pad ever asks. Two chips side by side, a card
 * with two verbs on its heading line, a keypad: each one needs "right from here lands on that",
 * and each screen that wanted it wrote its own arithmetic over its own indices, in terms of a
 * layout it had just finished forgetting.
 *
 * So the answer is the one Android's FocusFinder and the tvOS focus engine both landed on, and
 * it inverts the problem: the frame *registers the rectangle of everything it actually drew*,
 * and a press is resolved against those rectangles. The screen stops holding a map of itself.
 * It holds one id - which thing has the cursor - and asks.
 *
 *     inkcell_focus_begin(&map, storage, GRID_MAX);
 *     ... draw a button ...  inkcell_focus_add(&map, ID_SEND, rect.x, rect.y, rect.w, rect.h);
 *     ... draw a chip ...    inkcell_focus_add(&map, ID_FILTER(i), x, y, w, h);
 *
 *     enum inkcell_focus_dir dir;
 *     if (inkcell_focus_dir_for_key(key, &dir)) {
 *         const uint32_t next = inkcell_focus_find(&map, screen->focus, dir);
 *         if (next != INKCELL_FOCUS_NONE) {
 *             screen->focus = next;
 *         }
 *     }
 *
 * Registering during the draw is not an implementation detail, it is the whole safety property.
 * A card whose verb fell off its trailing edge never registers that verb; a list showing ten
 * rows of four hundred registers ten. The cursor therefore cannot land on something that is not
 * on the frame - which is a failure this toolkit has already written down twice as a thing
 * screens must remember not to do (see the reservation note in inkcell/ui/widgets/card.h). A
 * screen cannot remember it wrong here, because what was drawn and what can be reached are the
 * same list.
 *
 * Which is also why a screen drawing components does not make these calls at all. Only the
 * card knows how many of its verbs fitted, only the strip knows how far its pills got, only the
 * list knows which rows the window landed on - so the components register their own, and a
 * screen pushes a map in and names things:
 *
 *     inkcell_focus_begin(&map, storage, SCREEN_MAX);
 *     inkcell_fb_set_focus_map(state, &map);
 *     card.action_focus_id = ID_CARD_VERBS;     inkcell_fb_draw_card(...);
 *     chips[i].focus_id = ID_FILTER(i);         inkcell_fb_draw_chip_strip(...);
 *     inkcell_fb_list_focus(&list, ID_ROWS);    ... the walk ...
 *
 * See inkcell_fb_set_focus_map() in inkcell/ui/fb_draw.h for the ids each component takes.
 * inkcell_focus_add() below stays what a screen calls for something it drew itself.
 *
 * This is a model rather than a widget, for the reason inkcell/ui/actions.h and
 * inkcell/ui/keyboard.h are: where the cursor goes next is a fact about the nav, a second
 * backend would want the same answer, and it is something a test can assert about without a
 * panel anywhere near it (tests/suites/ui_focus.c). Nothing here includes a framebuffer header,
 * allocates, or keeps a pointer past the call it was made in.
 *
 * The half of a screen that is not on the panel
 * ---------------------------------------------
 * A map holds what was drawn, which is the whole of its honesty and also its one blind spot: a
 * list four hundred items long shows ten of them, and the other three hundred and ninety are
 * not boxes anywhere. Pressing down on the last visible row therefore finds nothing, and
 * "nothing" is the wrong answer - that press is a scroll.
 *
 * So a run says what the map cannot: **these ids are a sequence, and it is longer than what you
 * can see**. `inkcell_focus_step()` takes the runs with the press and answers for both halves
 * of the screen at once, which is what lets a screen hold one id whether the thing under the
 * cursor is a chip, a card's verb, or item 287 of a list that is mostly somewhere else.
 *
 * `struct inkcell_list` keeps the window arithmetic and is still what decides which rows a
 * frame draws; a run is the same list seen from the press rather than from the draw, and it is
 * three numbers because three numbers is all that is knowable between frames - where its ids
 * start, how many there are, and how many of them are side by side.
 */

#include "inkcell/ui/key.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * The id of nothing.
 *
 * Returned when no registered rectangle lies in the direction asked for, which is a normal
 * answer and not an error: it is what the edge of a screen is. A caller that wants a press at
 * the edge to do something else - scroll, change tab, close - is a caller that has just been
 * told the press was going spare.
 *
 * Zero is reserved for it, so `inkcell_focus_add()` refuses that id. An id space that could
 * name the thing meaning "nothing" is one where a screen's first enum entry silently becomes
 * unreachable.
 */
#define INKCELL_FOCUS_NONE 0U

/*
 * A direction of travel, and only the four.
 *
 * Not "next" and "previous": those are an index's words and they are what this file exists to
 * stop screens reasoning in. A shoulder button that steps through a strip is a different press
 * from a d-pad, and a screen that wants one already knows the order it declared things in.
 */
enum inkcell_focus_dir {
    INKCELL_FOCUS_LEFT = 0,
    INKCELL_FOCUS_RIGHT,
    INKCELL_FOCUS_UP,
    INKCELL_FOCUS_DOWN,
};

/*
 * A box in pixels, in whatever coordinates the frame was drawn in.
 *
 * Deliberately not `struct inkcell_fb_rect`, which is the same four numbers: that one lives in
 * the framebuffer backend's public header, and a model that included it to borrow a shape would
 * make every consumer of the focus map - a test, a second backend, a screen that has not drawn
 * anything yet - depend on the framebuffer. The four ints are passed to `inkcell_focus_add()`
 * individually for the same reason, so a caller holding either shape hands over `rect.x,
 * rect.y, rect.w, rect.h` and nothing needs converting.
 *
 * Half-open: a rect at x=10 with w=20 covers 10 through 29, and the one starting at x=30 is
 * beside it rather than overlapping it by a pixel. Everything below is written to that rule,
 * which is what makes two adjacent chips two chips.
 */
struct inkcell_focus_rect {
    int x, y, w, h;
};

/* One registered thing: what the screen calls it, where it came out, and how round it was. */
struct inkcell_focus_item {
    uint32_t id;
    struct inkcell_focus_rect rect;
    /*
     * The corner radius the box was drawn with, in pixels. Zero is a square corner and is what
     * inkcell_focus_add() records.
     *
     * Nothing in this file reads it: the finder is about where a box is, and how round it is
     * has no bearing on what lies to the right of it. It is carried because the one thing that
     * reads a registration back and *draws* is the focus ring, and a ring that guessed would be
     * a second opinion about a shape the widget had already stated - a rectangle travelling
     * onto a pill and stopping square on it. The widget states it once, here, in the same call
     * that says where it is.
     */
    int radius;
    /*
     * A target and not a place to stand: a pointer can press it, and the d-pad walks past it.
     *
     * A hint pill in the action bar is the case that needed this. Clicking "A Open" is pressing
     * A, so the pill is worth a box - but a d-pad that could land on it would leave the last row
     * of every list for the footer. The finder, the first and the nearest all skip these; only
     * inkcell_focus_hit() sees them. Set by inkcell_focus_add_target().
     */
    bool pointer_only;
};

/*
 * The frame's focusable rectangles.
 *
 * Storage is the caller's - an array on the stack of the render function, sized to the most
 * that screen can draw - because this is rebuilt from nothing every frame and a frame is not a
 * place to allocate. `count` and `capacity` are the whole of the bookkeeping.
 *
 * `dropped` is how many `inkcell_focus_add()` calls were refused for want of room. It is not an
 * error return nobody checks: a screen that outgrew its array loses the ability to reach
 * whatever it drew last, which on a panel looks like a button that cannot be selected rather
 * than like a bug, and is the sort of thing a test asserts is zero.
 */
struct inkcell_focus_map {
    struct inkcell_focus_item *items;
    uint32_t count;
    uint32_t capacity;
    uint32_t dropped;
    /*
     * The id the frame drew as *focused*, or INKCELL_FOCUS_NONE - see inkcell_focus_mark().
     *
     * Kept beside the boxes because it is the same kind of fact: something the frame found out
     * while drawing, not something the application decided before it. The application knows a
     * list's cursor index and a dialog's cursor answer; only the frame knows which of those
     * came out on top, and so which one the ring belongs on.
     */
    uint32_t focused;
};

/* Empties the map onto `storage`. Call it at the top of the draw, once, before anything is
   registered: a map carried between frames is a map naming rectangles from the layout before
   the last one. `storage` may be NULL with a capacity of 0, which is a map that refuses
   everything - and answers INKCELL_FOCUS_NONE to everything, which is what an empty screen
   is. */
void inkcell_focus_begin(struct inkcell_focus_map *map, struct inkcell_focus_item *storage,
                         uint32_t capacity);

/*
 * Registers a rectangle under `id`. Returns false when it was refused, which is one of four
 * things and all of them are the caller's mistake rather than a state to handle:
 *
 *   - the map is full (`dropped` counts these),
 *   - `id` is INKCELL_FOCUS_NONE,
 *   - `id` is already registered - one name, two boxes, and no answer to which one "right"
 *     means. A grid's ids are usually an index folded into the enum for exactly this reason,
 *   - the box has no area. A zero-width control is one the eye cannot find and the cursor
 *     should not be able to either; this is the case that turns up when a widget is asked to
 *     draw itself into a gutter.
 */
bool inkcell_focus_add(struct inkcell_focus_map *map, uint32_t id, int x, int y, int w, int h);

/*
 * The same, for a box with rounded corners: `radius` is what it was drawn with, in pixels.
 *
 * A second entry point rather than a sixth parameter on the one above, because a square box is
 * the ordinary case and a caller that has nothing to say about its corners should not have to
 * say nothing. Both record the same thing; this one records it with a number in it.
 */
bool inkcell_focus_add_round(struct inkcell_focus_map *map, uint32_t id, int x, int y, int w, int h,
                             int radius);

/*
 * The same, for a target a pointer may press and the d-pad may not reach - see `pointer_only`
 * above. The rules for refusing are the same four, and it shares the id space, so a target and
 * a focusable box cannot both be called the same thing.
 */
bool inkcell_focus_add_target(struct inkcell_focus_map *map, uint32_t id, int x, int y, int w,
                              int h, int radius);

/*
 * Says that `id` - already registered, or about to be - is the thing drawn as focused.
 *
 * Called by the component that drew the cursor, at the moment it drew it: the list for the row
 * under its cursor, a button whose `focused` is set. The *last* call wins, for
 * inkcell_focus_hit()'s reason: a sheet's rows are drawn after the list beneath them, and the sheet
 * is where the d-pad is. INKCELL_FOCUS_NONE is ignored rather than clearing, so a component with no
 * id cannot take the mark away from one that had one.
 *
 * Selection never marks. The current tab is drawn as selected on every frame, and a ring that
 * followed it would be a cursor that never leaves the tab strip.
 */
void inkcell_focus_mark(struct inkcell_focus_map *map, uint32_t id);

/* The last id marked this frame, or INKCELL_FOCUS_NONE. What a frame hands the focus ring. */
uint32_t inkcell_focus_marked(const struct inkcell_focus_map *map);

/*
 * What is under the point (x, y), or INKCELL_FOCUS_NONE.
 *
 * The pointer's question, and the reason the map is a map of boxes rather than of ids: a frame
 * that recorded what it drew and where can answer a click with no second description of itself.
 * Targets count, as does everything the d-pad can reach.
 *
 * Where two boxes overlap, the one registered *last* wins, because it is the one drawn last and
 * therefore the one on top - an overlay's buttons over the rows beneath it.
 */
uint32_t inkcell_focus_hit(const struct inkcell_focus_map *map, int x, int y);

/*
 * Two blocks of ids the toolkit reserves for "this box is a key": pressing it with a pointer
 * is pressing that key, and nothing about the application has to be asked.
 *
 * INKCELL_FOCUS_ACTION_KEY is specifically a visible action-bar hint. Keeping it apart lets an
 * application route that click through the semantic action the hint names, while an app-bar
 * back arrow or a keycap drawn on a help screen remains an ordinary logical key. Both blocks
 * sit at the top of the id space so that no application enum counting up from 1 reaches them.
 */
#define INKCELL_FOCUS_ACTION_KEY_BASE 0xFFFFFE00U
#define INKCELL_FOCUS_ACTION_KEY(key) (INKCELL_FOCUS_ACTION_KEY_BASE + (uint32_t)(key))
#define INKCELL_FOCUS_KEY_BASE 0xFFFFFF00U
#define INKCELL_FOCUS_KEY(key) (INKCELL_FOCUS_KEY_BASE + (uint32_t)(key))

/* The key a reserved id stands for, or INKCELL_KEY_NONE for an id outside the block. */
enum inkcell_key inkcell_focus_key_of(uint32_t id);
enum inkcell_key inkcell_focus_action_key_of(uint32_t id);

/* The radius `id` was registered with, and 0 for a square one or for an id that is not here.
   What the focus ring asks, and the reason `radius` is on the item at all. */
int inkcell_focus_radius_of(const struct inkcell_focus_map *map, uint32_t id);

/* Whether `id` was registered this frame. The question a screen asks *after* laying out, about
   the id it was holding: a card that lost a row, a chip that was elided and a list that scrolled
   are all "the thing the cursor was on is not on this frame", and they are all this. */
bool inkcell_focus_has(const struct inkcell_focus_map *map, uint32_t id);

/* Where `id` came out. False, and `out` untouched, when it was not registered. Worth keeping
   beside the focused id on a screen that reflows: it is the remembered rectangle
   `inkcell_focus_nearest()` takes, and the only thing that survives the layout it describes. */
bool inkcell_focus_rect_of(const struct inkcell_focus_map *map, uint32_t id,
                           struct inkcell_focus_rect *out);

/*
 * What lies `dir` of `id`, or INKCELL_FOCUS_NONE.
 *
 * INKCELL_FOCUS_NONE is also the answer when `id` itself was not registered, rather than a
 * jump to somewhere plausible. A cursor on something that is no longer drawn is a screen with a
 * question to answer - `inkcell_focus_nearest()` is usually the answer - and a finder that
 * quietly picked for it would turn a reflow into the cursor teleporting.
 *
 * How it chooses, because a screen is entitled to predict it. Two rules, in order:
 *
 *   1. **The beam**: whether a candidate is in line with the source across the direction of
 *      travel - on the same row, for a left or right press, or in the same column for an up or
 *      down one.
 *
 *      Sideways this is absolute. An in-line candidate beats one that is not, however much
 *      closer the out-of-line one is, which is what keeps a press travelling along a chip strip
 *      instead of diving into the card underneath at the first ragged gap.
 *
 *      Up and down keep the same preference with one release valve, and it is Android's rather
 *      than an oversight: an in-line candidate a long way down does not beat one sitting
 *      diagonally nearer than that reach. A column is normally tight and the in-line one wins
 *      on distance anyway; where it does not, the choice is between stepping to the control
 *      beside you and leaping the height of the panel to stay in a column the reader is not
 *      thinking in. A screen that needs the sideways guarantee vertically is a screen whose
 *      columns should say so by being columns - see `inkcell_focus_find_wrapping()`, which is
 *      in-line only, both ways.
 *   2. **Weighted distance**, among equals: `13 * along² + across²`, where `along` is the gap in
 *      the direction pressed and `across` is how far the two centres are offset from each other.
 *      The 13 is Android's, kept rather than re-derived because the number is not the point -
 *      what it says is that being *in line* is worth about three and a half times being *close*,
 *      so a slightly further cell straight ahead beats a nearer one off to the side. A plain
 *      centre-to-centre distance is what gives a grid its diagonal drift.
 *
 * Things that overlap the source are candidates too - a wide card beside two stacked buttons is
 * the ordinary case - which is why the test is "is any part of it further along" rather than
 * "is all of it past the edge".
 */
uint32_t inkcell_focus_find(const struct inkcell_focus_map *map, uint32_t id,
                            enum inkcell_focus_dir dir);

/*
 * The same, except that running out of screen comes back round.
 *
 * Where it wraps to: **the furthest one back that is still in line with the source** - the other
 * end of the row a left or right press was travelling along, the top of the column an up or down
 * press was. In line is the beam of rule 1 above, and it is the whole of the rule here, with no
 * release valve in either direction: the strip's own chips are the candidates and the row under
 * it is not, whatever that row is doing with its own edges.
 *
 * Absolute here and not when a press is merely moving, because the two answer different
 * questions. A press asks what is over there and should reach a near thing off to one side
 * rather than nothing at all; a wrap asks for the other end of *this* row, and a wrap that
 * answered with another row has not brought the reader round, it has lost their place.
 *
 * Which of the two a screen calls is an editorial decision, and the toolkit has no opinion. A
 * strip of four filters wants to wrap; a column of settings does not, because a reader holding
 * down and finding themselves back at the top has lost their place rather than been helped.
 */
uint32_t inkcell_focus_find_wrapping(const struct inkcell_focus_map *map, uint32_t id,
                                     enum inkcell_focus_dir dir);

/*
 * A sequence of ids that is longer than the part of it on the panel: a scrolling list.
 *
 * `base` is item 0's id and item `n` is `base + n`, which is what inkcell_fb_list_focus()
 * registers its rows as. `count` is every item, drawn or not - the number the screen has and
 * the map does not.
 *
 * Vertical by default, because that is what a window in this toolkit scrolls: a `struct
 * inkcell_list` is a column, so a run with no `stride` steps on up and down and says nothing
 * about left and right. A grid is the case that needed the other axis, and it got it here
 * rather than in a second function - see `stride` below.
 */
struct inkcell_focus_run {
    uint32_t base;
    uint32_t count;
    /*
     * Items per row: the sequence is a grid `stride` wide rather than a column.
     *
     * 0 and 1 both mean a column, which is every list and is what a run declared before this
     * field existed has. Past that, the run steps by `stride` on up and down and by one on
     * left and right - and the difference between those two is the whole reason a grid cannot
     * be expressed as a run of one column: item 7 of a three-wide grid is below item 4 and
     * beside item 8, and an index alone cannot tell those apart.
     *
     * Two rules follow, and both are what a reader of a tile grid expects:
     *
     *   - **Sideways stays in its row.** A press right at the end of a row is not the start of
     *     the next one; it is the edge of the grid, and the geometry answers it - which on a
     *     home screen means nothing, and on a screen with a rail beside the tiles means the
     *     rail. Wrapping is an editorial decision this file leaves to the screen, exactly as
     *     `inkcell_focus_find_wrapping()` does.
     *   - **Down from a short last row lands on its last tile.** Seven tiles three across leave
     *     a row of one, and a press down from the tile above the gap has somewhere to go: the
     *     grid goes on, and stopping there would be the cursor refusing to move for want of a
     *     tile directly underneath. Down from the last row itself is still the edge.
     *
     * Which way `count` runs is unchanged: it is every item, drawn or not, and the grid's own
     * column count is `stride` - inkcell_fb_grid_run() hands back both, because a grid that
     * narrowed itself to fit the panel is the only thing that knows what it drew.
     */
    uint32_t stride;
    /*
     * One byte per item, non-zero where the item is a place to stand. NULL is a run whose every
     * item is one, which is what a plain list of rows passes.
     *
     * It is here because an id in a run is not always a box. A list's subheaders and notes take
     * an item index like any other row and register nothing - they are labels - so the
     * sequence a run describes has gaps in it, and a step that walked into one would put the
     * cursor on a heading: the ring would vanish for a frame and the reader would have to press
     * again to get past it. The map cannot see the difference, because a label and a row that
     * has scrolled off are both simply not registered.
     *
     * A borrowed array of one byte per item is the shape this toolkit already asks for when a
     * screen has something to say per item - see `heights` and `cards` on
     * inkcell_fb_list_begin_cards(). It is borrowed for the call and not retained.
     *
     * **A list with a subheader or a note in it needs this.** Leaving it NULL there is the one
     * way to use a run wrongly, and what it costs is a press at every group boundary.
     */
    const uint8_t *focusable;
};

/*
 * The press, answered against the whole screen: what is drawn, and what is only known about.
 *
 * This is the one call a screen makes on a d-pad press. It resolves in two moves, and the order
 * is the point:
 *
 *   1. **A run that can still move wins.** The cursor is inside a list and there is another
 *      item that way, so the answer is that item - whether or not it is on the panel. A screen
 *      that took the geometry first would walk out of a four-hundred-item list onto the button
 *      below it at item ten, because the button is the only thing drawn down there.
 *   2. **Otherwise the geometry answers**, exactly as `inkcell_focus_find()` does. At the true
 *      ends of a list that is where leaving it is what the press means, and everywhere else it
 *      is the whole of what a press means.
 *
 * An id in a run that is *not* registered still steps, which is the one place this parts
 * company with the finder: a cursor whose row has scrolled out of the window is still a cursor
 * in that list, and the press that brings it back is the same press as any other. Which is also
 * why a run has to say where its *labels* are - see `focusable` above: not being registered is
 * what a row off the panel and a subheader have in common, and only one of them is somewhere to
 * put a cursor.
 *
 * A run that is a grid answers sideways as well. Not because the tiles across are off the
 * panel - a grid's window holds whole rows, so they are exactly the ones that are on it - but
 * because the two rules beside `stride` above are then the run's statement rather than
 * something the finder's beam agrees with while the layout happens to suit it. See
 * inkcell_fb_grid_run() for where the number comes from.
 *
 * `runs` may be NULL with `run_count` 0, which makes this `inkcell_focus_find()` with more
 * words. A screen with one list passes one run:
 *
 *     const struct inkcell_focus_run runs[] = {{.base = ID_ROWS, .count = screen->count}};
 *     enum inkcell_focus_dir dir;
 *     if (inkcell_focus_dir_for_key(key, &dir)) {
 *         const uint32_t next = inkcell_focus_step(&map, screen->focus, dir, runs, 1U);
 *         if (next != INKCELL_FOCUS_NONE) {
 *             screen->focus = next;
 *         }
 *     }
 *
 * The map it is asked about is the one the *last frame* filled, which is the frame the reader
 * is looking at when they press. That is also why a run is two numbers rather than a list: no
 * `struct inkcell_fb_list` exists between frames, and the two things a press needs to know
 * about a list - where its ids start and how many there are - are both facts the screen has.
 */
uint32_t inkcell_focus_step(const struct inkcell_focus_map *map, uint32_t id,
                            enum inkcell_focus_dir dir, const struct inkcell_focus_run *runs,
                            size_t run_count);

/*
 * Where a screen with no cursor yet starts: the top-most registered rectangle, and the
 * left-most of those level with it.
 *
 * Reading order rather than registration order, which are usually the same list and sometimes
 * not: chrome is drawn before the body it sits above, and a widget is free to draw its own
 * parts in whatever order suits it. The one that the eye lands on first is the one a geometry
 * answers for, and the order a frame happened to be assembled in is not that.
 */
uint32_t inkcell_focus_first(const struct inkcell_focus_map *map);

/*
 * The registered rectangle closest to `rect`, or INKCELL_FOCUS_NONE when nothing was
 * registered at all.
 *
 * This is how a cursor survives the screen changing under it - the remembered rectangle tvOS
 * keeps, and the pair to `inkcell_focus_rect_of()`. A row was deleted, a card shed a verb, a
 * filter emptied the list; the id the screen was holding is gone, and the honest answer to
 * "where was the user looking" is not the first thing on the panel, it is whatever is now
 * nearest to where they were looking.
 *
 *     if (!inkcell_focus_has(&map, screen->focus)) {
 *         screen->focus = inkcell_focus_nearest(&map, screen->focus_rect);
 *     }
 *     (void)inkcell_focus_rect_of(&map, screen->focus, &screen->focus_rect);
 *
 * Distance is measured between the boxes and not between their centres, so a rectangle
 * overlapping `rect` is at zero however large it is: a row that grew a second line is still the
 * row the cursor was on, and a centre that moved half a line down should not hand the cursor to
 * its neighbour.
 */
uint32_t inkcell_focus_nearest(const struct inkcell_focus_map *map, struct inkcell_focus_rect rect);

/*
 * The d-pad key as a direction. False for every other key, which is the screen's to deal with.
 *
 * One line, and it is here so that it is one line *once*. Every screen that resolves a press
 * writes this switch otherwise, and a switch that turns four enum entries into four enum
 * entries is a place for a typo to live where no test will ever see it.
 */
bool inkcell_focus_dir_for_key(enum inkcell_key key, enum inkcell_focus_dir *dir);

#endif /* INKCELL_FOCUS_H */
