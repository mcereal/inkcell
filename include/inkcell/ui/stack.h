#ifndef INKCELL_UI_STACK_H
#define INKCELL_UI_STACK_H

/*
 * How a screen spends the room it has: a stack of boxes along one axis, and the width class
 * that says how much room there is to spend.
 *
 * Both halves exist because of the same tax. A screen here is handed a body rectangle and has
 * to place things in it, and every screen paid for that by hand: a `y` cursor advanced by the
 * height of whatever was just drawn, a width divided by however many things were going across,
 * a `if (y + h > footer_y) return;` at each step, and - once a window could be any size - a
 * second copy of all of it for the case where there was room to spare. None of that arithmetic
 * is interesting and all of it is per-screen, which is the definition of a tax: it is paid
 * again on every new screen, and every screen gets a chance to round it differently.
 *
 * So the arithmetic is stated once, here, with no pixels in it. A stack is declared and then
 * resolved - the idiom `struct inkcell_fb_card` already uses, and for its reason: a caller that
 * has not measured everything yet cannot know where the second item goes, so it says what it
 * wants and asks afterwards.
 *
 *     struct inkcell_stack row;
 *     inkcell_stack_begin(&row, box, INKCELL_AXIS_X, gap);
 *     inkcell_stack_add_fixed(&row, icon_w);
 *     inkcell_stack_add_grow(&row, 0, 1);      // the label takes what is left
 *     inkcell_stack_add_fixed(&row, value_w);
 *
 *     struct inkcell_box out[3];
 *     inkcell_stack_resolve(&row, out, 3U);
 *
 * and `out` is parallel to the order things went in, so a call site reads as the row does.
 *
 * ---- what it is not ----
 *
 * **Not a layout engine.** There is no tree, no reflow and no second pass. A stack resolves one
 * run of boxes along one axis; nesting is a caller resolving a second stack inside a box the
 * first one gave it, which is an ordinary local variable rather than a graph somebody has to
 * own. That ceiling is deliberate: the thing being laid out here is a frame drawn top to bottom
 * into a buffer, and a layout pass that can revisit its own answers is a frame that cannot be
 * drawn as it is measured.
 *
 * **Not a grid, and it does not wrap.** A run that does not fit drops from its tail rather than
 * flowing onto a second line - see `inkcell_stack_resolve()`. Rows of things that wrap are
 * `struct inkcell_grid` in inkcell/ui/layout.h, which is a different question and already
 * answered.
 *
 * **Not a list.** A column of many uniform rows with a cursor in it is `struct inkcell_list`,
 * which knows about scrolling and this does not. A stack is for the handful of *unlike* things
 * a screen is made of: a header, a body, a row of buttons.
 *
 * There are no pixels below this line - only integers a caller has already measured in whatever
 * unit it draws in. That is what lets the whole file be tested without a panel
 * (tests/suites/ui_stack.c) and what makes it as useful to a second backend as to the
 * framebuffer one.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- a box ---------------------------------------------------------------------------------
 *
 * The four numbers, again.
 *
 * Deliberately not `struct inkcell_fb_rect`, for the reason `struct inkcell_focus_rect` is not
 * one either (see inkcell/ui/focus.h): that type lives in the framebuffer backend's header, and
 * a model that borrowed its shape would make every consumer of a layout - a test, a second
 * backend, a screen that has not drawn anything yet - depend on the framebuffer. The fields are
 * named and ordered to match both of the others, so converting is `{b.x, b.y, b.w, b.h}` at the
 * one seam that needs it rather than a function.
 *
 * Half-open, like the focus map's: a box at x=10 with w=20 covers 10 through 29, and the one
 * starting at 30 is beside it rather than a pixel over it. Every stack below adds up to that
 * rule, which is what makes two adjacent boxes adjacent.
 */
struct inkcell_box {
    int x, y, w, h;
};

/* Whether a box has any room in it. What a caller tests before drawing into one, because a box
   that was squeezed out comes back empty rather than missing - see `inkcell_stack_resolve()`. */
static inline bool inkcell_box_is_empty(struct inkcell_box box) {
    return box.w <= 0 || box.h <= 0;
}

/*
 * `box` with `dx` taken off each vertical edge and `dy` off each horizontal one.
 *
 * Padding is a caller's inset rather than a field on the stack, because a stack that owned
 * padding would own it in two units - the outer box's and its items' - and the second is
 * already spelled as a gap. One box goes in and a smaller one comes out; nesting is what
 * composes them.
 *
 * Never inside out: an inset deeper than the box leaves a zero extent rather than a negative
 * one, so the result is empty rather than a rectangle that draws backwards.
 */
struct inkcell_box inkcell_box_inset(struct inkcell_box box, int dx, int dy);

/*
 * `box` capped at `max_w` and what is left centred in it.
 *
 * The reading measure, and the one piece of arithmetic that a window which can be any size
 * actually needs. Text set across 1600 pixels is not a wider paragraph, it is an unreadable
 * one: the eye loses the start of the next line somewhere around 90 characters, which is why
 * every reading surface from a newspaper column to a documentation site caps its measure and
 * centres it. A handheld panel never hits the cap and is unchanged by this; a maximised window
 * hits it immediately.
 *
 * `max_w` of 0 or less, or a box already narrower than it, comes back untouched.
 */
struct inkcell_box inkcell_box_measure(struct inkcell_box box, int max_w);

/* ---- a stack -------------------------------------------------------------------------------
 *
 * Which way the run goes. A column is the zero value because a screen is a column: the axis a
 * caller does not name is the one it almost always wants.
 */
enum inkcell_axis {
    INKCELL_AXIS_Y = 0, /* a column: items stack downward, `basis` is a height */
    INKCELL_AXIS_X,     /* a row: items run across, `basis` is a width */
};

/*
 * What an item does with the *cross* axis - the one the run is not along.
 *
 * Stretch is what a bare stack does, and it is what a row of buttons wants: they are the same
 * height because they are the same kind of thing, and a caller that had to say so on each one
 * would eventually forget on one.
 */
enum inkcell_align {
    /*
     * Whatever the thing above says: on an item, the stack's alignment; on the stack, stretch.
     *
     * A zero value that means "I did not say" rather than naming a behaviour, which is what
     * lets an item stay silent. Without it the item's field and the stack's default are the
     * same number, so a stack that asked for centring would be overruled by every item that
     * simply had not filled the field in - a default that only works until somebody uses it.
     */
    INKCELL_ALIGN_DEFAULT = 0,
    INKCELL_ALIGN_STRETCH, /* fill the cross extent; the item's own `cross` is ignored */
    INKCELL_ALIGN_START,   /* the top of a row, the leading edge of a column */
    INKCELL_ALIGN_CENTER,
    INKCELL_ALIGN_END,
};

/*
 * What the run does with room left over along its *own* axis - which only arises when nothing
 * in it asked to grow. A run with a growing item in it has no leftover by construction.
 */
enum inkcell_justify {
    INKCELL_JUSTIFY_START = 0, /* packed at the leading edge, leftover trailing */
    INKCELL_JUSTIFY_CENTER,
    INKCELL_JUSTIFY_END,
    /* Spread: the leftover is shared out between the items as extra gap, so the first sits at
       the leading edge and the last at the trailing one. A run of one is START - there is no
       "between" to put anything in, and stretching a lone item would be a different request. */
    INKCELL_JUSTIFY_BETWEEN,
};

/*
 * Sixteen, which is the widest *unlike* run this is for. A screen's column is a header, a body
 * and a footer; a row is an icon, a label, a value and a chevron. Anything with more parts than
 * this is a list or a grid and has its own primitive in inkcell/ui/layout.h - if a caller is
 * bumping into this number, the shape is wrong rather than the cap.
 */
#define INKCELL_STACK_MAX 16U

/*
 * One thing in the run.
 *
 * The zero value is a rigid item of no size: `{.basis = h}` is the common case and reads as
 * one. Two of the fields are worth stating outright because they are where CSS's defaults are
 * not the right ones here.
 *
 * **`shrink` is 0 by default, so an item does not shrink.** CSS shrinks everything by default,
 * which is right for a page that must never overflow its viewport and wrong for a frame: a row
 * of keycaps squeezed by two pixels each is a row of keycaps that is subtly wrong everywhere,
 * where a row that drops its last item is one obvious thing the author can see and fix. So
 * shrinking is asked for, not assumed, and what a run that still does not fit does instead is
 * stated in `inkcell_stack_resolve()`.
 *
 * **`min` is a floor, not a hint.** An item with `shrink` set will be taken down to `min` and
 * no further, which is how a label gives up room to a value without disappearing. `min` of 0
 * means it may be squeezed to nothing, and an item squeezed to nothing comes back empty.
 */
struct inkcell_stack_item {
    /* What it asks for along the axis: a height in a column, a width in a row. Usually
       something the caller measured - a line advance, `inkcell_fb_text_width()`. */
    int basis;
    /* The floor `shrink` may take `basis` down to. Ignored when `shrink` is 0. */
    int min;
    /*
     * Its size across the axis, when it is not stretched: a fixed-width column in a row, a
     * button's own width in a column of them. 0 means the whole cross extent, which is what an
     * item that is aligned but has no width of its own wants.
     *
     * Ignored entirely under INKCELL_ALIGN_STRETCH, because stretching *is* the answer to this
     * question and an item that stated both would be saying two things.
     */
    int cross;
    /* Its share of any room left over. 0 takes none. The shares are relative, so two items
       weighted 1 and 2 split the leftover one-third/two-thirds. */
    uint8_t grow;
    /* Its share of any overflow, weighted by `basis` as well - so a wide item gives up more
       than a narrow one, which is what keeps a row from squeezing its smallest part to nothing
       first. 0 never shrinks. */
    uint8_t shrink;
    /* Its own cross-axis alignment, overriding the stack's. */
    enum inkcell_align align;
};

/*
 * The run, declared then resolved.
 *
 * Held by value and filled in place, so a stack is a local and nothing here allocates. The
 * fields are public because a caller that wants to set `justify` after the fact should not need
 * a setter for a plain assignment; the invariants that matter are all inside
 * `inkcell_stack_resolve()`.
 */
struct inkcell_stack {
    struct inkcell_box box; /* the room the run is laid out in */
    enum inkcell_axis axis;
    enum inkcell_align align;     /* the default for items that do not state one */
    enum inkcell_justify justify; /* what to do with room nothing asked for */
    int gap;                      /* between neighbours, and only between them */
    uint32_t count;
    struct inkcell_stack_item items[INKCELL_STACK_MAX];
};

/* Opens a run over `box`. `gap` below 0 is clamped to 0: a negative gap is items on top of each
   other, which is never what a layout meant. */
void inkcell_stack_begin(struct inkcell_stack *stack, struct inkcell_box box,
                         enum inkcell_axis axis, int gap);

/* Adds one, in order. False once INKCELL_STACK_MAX is reached, and nothing is added - a caller
   that ignores the answer gets a run without its last item rather than a corrupted one. */
bool inkcell_stack_add(struct inkcell_stack *stack, struct inkcell_stack_item item);

/* The two common cases, so that the usual call site is one line and states only what it means.
   Anything past these - a floor, a cross size, its own alignment - is a designated initializer
   through `inkcell_stack_add()` above. */
bool inkcell_stack_add_fixed(struct inkcell_stack *stack, int basis);
bool inkcell_stack_add_grow(struct inkcell_stack *stack, int basis, uint8_t grow);

/*
 * Lays the run out and writes one box per item into `out`, in the order they were added.
 *
 * Returns how many of them the run had room for, counted from the front - because a run that
 * does not fit gives up from its tail. It is *not* how many boxes were written: `out` always
 * receives `stack->count` of them, and an item that got no room comes back empty rather than
 * missing. That is the whole of why a call site can index `out` by the order it built the run
 * in, and it is why `inkcell_box_is_empty()` exists: the test a caller makes before drawing is
 * "is there room for this", not "did the indices shift". An item inside the run that asked for
 * no size of its own is empty too, so the test is worth making on all of them rather than on
 * the ones past the return value.
 *
 * `max` below `stack->count` is refused: nothing is written and 0 comes back. A partial answer
 * would be a run laid out against a total the caller cannot see, which is worse than no answer
 * - the same reason `inkcell_fb_damage_rects()` refuses a cap it cannot report inside.
 *
 * **Everything placed adds up to the extent exactly**, gaps included, whenever anything grows.
 * Rounding each share on its own leaves a stray pixel at the trailing edge on most inputs, and
 * a stray pixel is a card that does not line up with the one above it. The leftover units go to
 * the items that lost most to rounding - the largest-remainder method, which is the split that
 * is off by the least everywhere at once, and the one `inkcell_proportion_split()` already uses
 * a few hundred lines away in inkcell/ui/layout.h. Two answers to one question would be two
 * roundings.
 *
 * **A run that does not fit drops from its tail**, after shrinking has done what it can. That
 * is `inkcell_fb_draw_card()`'s rule - rows that do not fit are dropped and the card says how
 * many - rather than a new one, and it is the honest failure: the alternative is drawing past
 * the edge of the room the caller was given, which on a framebuffer means over whatever is
 * there. What drops is the *tail* because a run is built in reading order, so its tail is the
 * part the reader misses least.
 */
uint32_t inkcell_stack_resolve(const struct inkcell_stack *stack, struct inkcell_box *out,
                               uint32_t max);

/* ---- width classes -------------------------------------------------------------------------
 *
 * How much room there is, as a *class* rather than a number.
 *
 * This is the other half of the tax, and it arrived with the window. While the only surface was
 * a 1024-pixel handheld panel, "how wide is it" had one answer and no screen had to ask. A
 * window can be any size, so every screen that wanted to use a wider one would otherwise carry
 * its own breakpoint - and a set of screens that each picked its own is a UI that changes shape
 * at four different widths on the way out of a drag.
 *
 * ---- why columns, and not pixels ----
 *
 * The class is taken from how many *columns of body text* fit, not from the pixel width, and
 * that is the one decision in here worth arguing about.
 *
 * A pixel count cannot answer this question on its own. The Brick's panel is 1024 pixels across
 * and 3.2 inches wide; a laptop window 1024 pixels across is a foot of glass. Material solves
 * it with density-independent pixels and iOS with points, both of which are a pixel count
 * divided by how big a pixel is - a fact this toolkit does not have and should not invent,
 * because it draws into a buffer and the buffer does not know what it will be presented on.
 *
 * What it does have is the glyph scale, which is the same fact from the other end: the scale is
 * already chosen so that body text is legible on whatever this is, so a column of body text is
 * already a density-independent unit. Counting them is measuring the panel in the only unit
 * that is about the reader.
 *
 * It falls out of that - rather than being a second feature - that a reader who turns the text
 * up gets a simpler layout. Fewer columns fit, the class drops, and a two-pane screen becomes
 * one pane. That is exactly what iOS does when Dynamic Type is turned up, and it is right for
 * the same reason: at that size the second pane was never going to be readable, and the
 * alternative is a layout that keeps its shape and loses its content.
 *
 * It runs the other way too, and that is worth saying outright because it looks like a bug the
 * first time it is met: a reader who turns the text *down* on the handheld gets the roomier
 * layout, because at that size the panel really does hold a column and a rail. The class is
 * about how much the surface can say, not about how big the surface is, and those two stop
 * being the same question the moment the reader has a say in it.
 *
 * The count is an estimate on a proportional face, as `struct inkcell_fb_layout`'s `cols` says.
 * That is fine here and would not be fine in a measurement: this is a threshold crossed once on
 * the way through a resize, not a width anything is drawn to. It is also deterministic - the
 * same panel and scale give the same count every frame - so there is nothing for hysteresis to
 * damp.
 */
enum inkcell_width_class {
    /*
     * One thing at a time. The handheld panel, and any window not much bigger: a screen is a
     * single column, chrome is a bar at each end, and anything secondary is somewhere else.
     * The zero value, so a surface that has not been measured behaves like the narrow one -
     * which is the safe way round, because a compact layout fits an expanded panel and the
     * reverse does not fit anything.
     */
    INKCELL_WIDTH_COMPACT = 0,
    /*
     * Room beside the content. Still one column of it - capping the measure and centring it is
     * `inkcell_box_measure()` - but enough left over for something permanent at one edge: a
     * navigation rail instead of a tab strip, a list's metadata pulled out into its own column.
     */
    INKCELL_WIDTH_MEDIUM,
    /* Two panes. A list and the thing it selects, side by side, which is the shape every
       desktop mail client and every tablet has converged on. */
    INKCELL_WIDTH_EXPANDED,
};

/*
 * How wide one column of running text is allowed to get, in columns of body text: the cap
 * `inkcell_box_measure()` is handed, and the unit the two thresholds below are counted in.
 *
 * Typography has argued about the number for a century and keeps landing between 45 and 75
 * *characters*, for a reason that is about eyes rather than taste: the return sweep to the
 * start of the next line is made without reading, and past about 75 characters it lands on the
 * wrong line often enough to be felt.
 *
 * A column here is a nominal advance rather than a character, and on this toolkit's own UI face
 * ordinary prose measures about four fifths of it - 58 columns is a line of around 71
 * characters. So 58 is the top of that band, and it is a cap rather than a target: a column
 * only reaches this width when there is room to spare, and erring wide costs less than leaving
 * a hand's width of surface empty either side of a narrow ribbon.
 *
 * It falls out of the arithmetic, rather than being arranged, that the handheld panel is almost
 * exactly one measure: 1024 pixels at the body scale is 58 columns. That is a useful thing to
 * know when reading the two thresholds below - they are counted from a width the device
 * already is - but it is not a fact to build on, because the next panel will be a different
 * one.
 *
 * Stated here so that a screen does not pick its own. Two screens with different caps are two
 * columns that do not line up when the reader moves between them, which is the same drift the
 * width classes exist to stop.
 */
#define INKCELL_WIDTH_MEASURE_COLS 58U

/*
 * Where the classes divide, in those same columns.
 *
 * 76 is the first width with room for something *beside* a full measure: a measure is 58, a
 * navigation rail or a metadata column is worth having at around a dozen more, and the few on
 * top of that keep a layout from reshaping itself to gain a rail too narrow to use. Below it
 * the only thing extra room can buy is a wider margin, and changing shape for a wider margin
 * is changing shape for nothing.
 *
 * 120 is where two measures fit at once, which is what two panes are. Two of 58 is 116 and the
 * gap between them is the rest. There is no separate minimum pane width: a pane narrower than
 * a measure is a pane that cannot be read, so the measure is the minimum by construction.
 */
#define INKCELL_WIDTH_MEDIUM_COLS 76U
#define INKCELL_WIDTH_EXPANDED_COLS 120U

/* The class `cols` columns of body text fall in. A backend answers this from its own geometry -
   inkcell_fb_width_class() in inkcell/ui/fb_draw.h - and the arithmetic is here so that every
   one of them answers it the same way. */
enum inkcell_width_class inkcell_width_class_of(size_t cols);

#ifdef __cplusplus
}
#endif

#endif /* INKCELL_UI_STACK_H */
