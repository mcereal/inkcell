#ifndef INKCELL_BACKENDS_FB_WIDGETS_GRID_H
#define INKCELL_BACKENDS_FB_WIDGETS_GRID_H

/*
 * The grid: a window onto tiles laid out in rows of more than one, and what one tile says.
 *
 * This is the shape a list cannot express, and it is not a variation on one. A row is a line of
 * text with slots at its ends - it is read, left to right, and the thing that makes it findable
 * is its words. A tile is a *picture with a name under it*: it is recognised rather than read,
 * from across the room and at a glance, which is why every home screen on every platform is a
 * grid of them and none of them is a list. A handheld makes the difference sharper still.
 * Eleven games as eleven rows of text is a directory; the same eleven as tiles is a shelf, and
 * a reader finds the one they want by its cover before they have read a single word.
 *
 * What it is made of is what everything else here is made of:
 *
 *   - **The window is `struct inkcell_grid`** (include/inkcell/ui/layout.h), which is a
 *     `struct inkcell_list` over the grid's *rows*. So the first visible row, the scroll thumb
 *     and the clamped cursor are the arithmetic that was already written and already tested,
 *     and this file adds pixels to it rather than a second opinion about where a window starts.
 *   - **The rail is the list's rail**, in the same gutter and at the same width, because a
 *     screen with a list on one tab and a grid on the next should not appear to have two
 *     different scrollbars.
 *   - **The tile registers the box it drew** (inkcell_fb_grid_focus()), so the cursor cannot
 *     land on a tile that the window left off the panel - the rule every component in this
 *     toolkit follows and the reason there is no index arithmetic in a screen.
 *
 * The one thing it adds to the press is an axis. A list's cursor moves by one and a grid's
 * moves by one sideways and by a row's width downward, so a screen hands the press a run whose
 * `stride` is the grid's column count - and it gets that number from inkcell_fb_grid_run()
 * rather than from the constant it asked for, because a grid too narrow for the columns it was
 * asked for draws fewer of them.
 *
 * inkcell/ui/widgets.h is the umbrella over this file and its siblings; include either.
 */

#include "inkcell/ui/fb_draw.h"

#include "inkcell/ui/focus.h"
#include "inkcell/ui/icon.h"
#include "inkcell/ui/layout.h"
#include "inkcell/ui/theme.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * How tall a tile's art is, given how wide the tile came out.
 *
 * A ratio rather than a height, for the reason a shape is a token rather than a radius: how
 * wide a tile is, is the panel's answer and not the screen's, so a screen that stated a height
 * would be stating half of a proportion whose other half it does not know. Three, because
 * three is what the things a tile can be a picture *of* come in.
 */
enum inkcell_fb_tile_ratio {
    /* As tall as it is wide: an app, a setting, a shortcut. The springboard's tile. */
    INKCELL_FB_TILE_SQUARE = 0,
    /* Three wide to four tall: a cover. What box art, a poster and a book spine all are, and
       the one to reach for when the tiles are things somebody else designed the front of. */
    INKCELL_FB_TILE_PORTRAIT,
    /* Sixteen to nine: a frame of what is inside. A screenshot, a preview, a map. */
    INKCELL_FB_TILE_LANDSCAPE,
};

/*
 * Where a tile's name goes, which is a statement about what the tile is for.
 *
 * Below the surface is the springboard: the picture is the thing, and the name is a caption
 * under it that the eye reaches only after it has failed to recognise the picture. Inside is
 * Material's image list: the name is *part of* the tile, which is what a grid of things that do
 * not all have a recognisable face wants - twelve tiles of the same icon in the same colour are
 * twelve tiles that have to be read, and a caption floating under each one has left them
 * looking identical for a beat longer than the reader had.
 *
 * NONE is a grid of pictures and nothing else: a palette, a gallery, a keypad of faces.
 */
enum inkcell_fb_tile_label {
    INKCELL_FB_TILE_LABEL_BELOW = 0,
    INKCELL_FB_TILE_LABEL_INSIDE,
    INKCELL_FB_TILE_LABEL_NONE,
};

/* What stands in the middle of a tile. */
enum inkcell_fb_tile_art {
    /* The container and nothing on it: a tile whose meaning is its colour and its name. */
    INKCELL_FB_TILE_ART_NONE = 0,
    /* One icon, at the largest multiplier the art box has room for - which on a tile is several
       times the body scale, and is the whole reason an icon in a grid reads at arm's length
       when the same icon in a row does not. */
    INKCELL_FB_TILE_ART_ICON,
    /* One emoji, drawn as a sprite at the size of the box rather than at the size of a text
       cell. The button's `emoji_face` one component over, for the same reason: a sprite is not
       a letter, and a picture drawn at a text cell is a thumbnail of a picture. */
    INKCELL_FB_TILE_ART_EMOJI,
    /* One or two cells of text, at the largest multiplier that fits: the avatar's trick at the
       size of a tile, for the things that have no face of their own. */
    INKCELL_FB_TILE_ART_INITIALS,
};

/*
 * One tile: what it is a picture of, what it is called, and what it means.
 *
 * `tone` is the one statement that decides its colour, exactly as a row's does. A tone that
 * names a family fills the art container with that family's held-back half and writes on it in
 * the ink the theme checked against that fill; a neutral tone - NORMAL, DIM, STRONG - fills the
 * surface a card takes and writes in ordinary text ink. So a launcher of coloured tiles and a
 * gallery of quiet ones are one component with one field set differently, and neither of them
 * is a screen choosing a colour.
 */
struct inkcell_fb_tile {
    /* The name. Wrapped to the tile's width over as many lines as the grid was opened for, and
       what does not fit is what does not fit - a tile's name is a label, not a paragraph. */
    const char *label;
    /* A second, quieter line under the name: a count, a date, a size. Costs a line of the
       label block, so a grid that wants one opens with two label lines. */
    const char *supporting;
    enum inkcell_fb_tile_art art;
    enum inkcell_icon icon; /* INKCELL_FB_TILE_ART_ICON */
    /* The emoji cell, or the one or two characters of an initials tile. Ignored by the other
       two kinds of art. */
    const char *text;
    /* What the tile is: its container's family, and the ink of its name when the name stands on
       the panel rather than on the tile. See the note above. */
    enum inkcell_tone tone;
    /*
     * A count in the tile's top trailing corner - unread, waiting, new.
     *
     * The badge a navigation bar's tab takes, at the one other place on a panel where something
     * is recognised rather than read. It fills with `badge_family`'s BASE, which is a
     * deliberate shout: a badge every tile carries is a column of colour that says nothing, so
     * anything a badge does not need to interrupt for is a badge that should not be there.
     */
    const char *badge;
    enum inkcell_family badge_family;
};

/*
 * How the grid is laid out: how many across, what shape the tiles are, and where their names
 * go. Every field has a zero that is the ordinary answer, so a grid of square tiles with a
 * one-line name under each is `{.cols = 3}`.
 */
struct inkcell_fb_grid_style {
    /*
     * Tiles across, as a *request*. 0 is read as 1.
     *
     * The grid answers with what it drew (inkcell_fb_grid_cols()), because a request it cannot
     * honour is one it has to narrow: below a floor of a few cells a tile is too small for its
     * own name and an icon in it is a speck, and four columns of that is worse than two columns
     * of something legible. Which is the same bargain the keyboard's grid strikes with its key
     * height, and the same one a card strikes with the verbs it has room for - what came out is
     * what the reader gets, and the component is the only thing that knows.
     */
    uint32_t cols;
    /*
     * Rows to fit in the body, and 0 is "however many the tiles' own size allows".
     *
     * The field a home screen needs and a shelf does not. Left at 0 the tile is as wide as a
     * column gets and as tall as its shape then makes it, and how many rows that leaves is
     * arithmetic - which is right for a grid the reader scrolls through, and wrong for the one
     * screen that is not scrolled at all: a launcher with four tiles across and most of the
     * panel empty under them is a launcher that has misjudged its own device.
     *
     * Set, it is the *other* half of the same bargain the columns strike. The tile takes
     * whichever of the two constraints binds - the width a column allows, or the height that
     * many rows allow - and keeps the shape it was asked for either way, so a grid told four by
     * two fills the body without a tile anywhere being stretched to do it. What the shorter of
     * the two leaves over horizontally is spent evenly either side, because a row of tiles
     * hugging the leading margin with a hand's width of nothing after it reads as a layout that
     * ran out rather than as one that was placed.
     *
     * More rows than fit is still what it says: the tiles shrink until they do.
     */
    uint32_t rows;
    enum inkcell_fb_tile_ratio ratio;
    enum inkcell_fb_tile_label label;
    /* Lines the name may take: 1 or 2, and 0 is read as 1. A second line is what a grid of
       real names needs - "Settings" fits one and "Bluetooth devices" does not - and it costs
       every tile that height whether or not its own name used it, because a grid whose rows
       are each as tall as their longest name is a grid that shuffles when a name changes. */
    uint32_t label_lines;
};

/*
 * A scrolling grid of tiles, filling the body.
 *
 *     struct inkcell_fb_grid grid = inkcell_fb_grid_begin(
 *         state, &layout, count, screen->cursor, &(struct inkcell_fb_grid_style){.cols = 3});
 *     inkcell_fb_grid_focus(&grid, ID_TILES);
 *     uint32_t i;
 *     while (inkcell_fb_grid_next(&grid, &i)) {
 *         inkcell_fb_grid_tile(state, &grid, i, &(struct inkcell_fb_tile){ ... });
 *     }
 *     screen->run = inkcell_fb_grid_run(&grid);   // what the next press steps by
 *
 * The fields are here because a screen is entitled to measure something of its own against the
 * tiles - a heading over a row of them, a tile it draws the face of itself. Nothing here is a
 * knob: every one of them is settled by inkcell_fb_grid_begin() from the layout and the style,
 * and writing to one afterwards is a grid disagreeing with the window it already opened.
 */
struct inkcell_fb_grid {
    struct inkcell_grid model;
    /* Where the window's first tile stands: the top-left of column 0 of the first visible row.
       Every tile's position is derived from this and its own index, so a screen that skips one
       does not shift the rest of the grid - see inkcell_fb_grid_next(). */
    int origin_x, origin_y;
    int x, y;           /* the tile the walk is on: its top-left corner */
    int tile_w, tile_h; /* the surface: the box the cursor's ring lands on */
    int art_h;          /* the part of the surface the picture has */
    int label_h;        /* the name's block, wherever it stands */
    int gap;            /* between one tile and the next, across and down */
    int cell_w, cell_h; /* one tile and the gap after it: what the walk advances by */
    int label_scale;    /* the multiplier a name is set at */
    uint32_t label_lines;
    enum inkcell_fb_tile_label label_place;
    /* The body the grid was opened against, for the rail: where it starts and how tall it is.
       Taken from the layout rather than accumulated as tiles are drawn, because a rail is the
       length of the *window* whether or not the tiles filled it. */
    int track_y, track_h;
    /* Whether the rail has been drawn for this grid. It is drawn by the first tile that draws,
       not by the screen - the list's rule, for the list's reason. */
    bool chrome_drawn;
    /* What the d-pad calls the tiles: item `index` is `focus_base + index`. Set by
       inkcell_fb_grid_focus(), zero otherwise. */
    uint32_t focus_base;
};

/*
 * Opens the grid over the body `layout` has left, with `cursor` taken raw from the nav state.
 *
 * Takes the state as well as the layout, which the list's entry points do not: a list's window
 * is a count of body rows and the layout knows it, while a grid's depends on how wide a tile
 * came out - which is a margin, a gutter and a glyph scale away. Settling that at the first
 * tile instead would mean the window was decided after the walk had started.
 *
 * `style` may be NULL, which is one column of square tiles with a one-line name under each -
 * in other words a list, drawn the long way round. The interesting default is `{.cols = 3}`.
 */
struct inkcell_fb_grid inkcell_fb_grid_begin(const struct inkcell_backend_fb_state *state,
                                             const struct inkcell_fb_layout *layout, uint32_t count,
                                             uint32_t cursor,
                                             const struct inkcell_fb_grid_style *style);

/* Hands back each visible index in turn, false when the window is exhausted. Row by row, left
   to right - which is also the path the x/y cursor travels. */
bool inkcell_fb_grid_next(struct inkcell_fb_grid *grid, uint32_t *index);

/*
 * Register each tile this grid draws, under `base + index`.
 *
 * Called after inkcell_fb_grid_begin() and before the walk, on a frame whose state carries a
 * focus map (inkcell_fb_set_focus_map()). An index folded into a base rather than an id per
 * tile, for the reason a list's rows take one: a screen's grid cursor is already an item index.
 *
 * **Only the tiles that were drawn**, which on a grid is the same promise a list makes and a
 * sharper one: a grid's window ends on a row boundary, so the tiles below it are not boxes
 * anywhere and a press down from the last visible row finds nothing. That press is a scroll,
 * and what answers it is `inkcell_focus_step()` with the run below.
 *
 * The box registered is the *surface* - the rounded rectangle the tile filled - and not the
 * cell it stood in. A name below a tile is a caption on the panel rather than part of the
 * container, and a ring drawn round both would be claiming a box the tile never painted.
 */
void inkcell_fb_grid_focus(struct inkcell_fb_grid *grid, uint32_t base);

/*
 * The run a press is answered against: this grid's base, its item count, and the number of
 * columns it settled on.
 *
 * Kept by the screen and handed to `inkcell_focus_step()` on the next press, exactly as a
 * list's two numbers are. The third one is why this is a function rather than something a
 * screen assembles: the columns a grid *drew* are not always the columns it was asked for, and
 * a press that stepped by the request would walk diagonally across a grid that had narrowed
 * itself.
 *
 * INKCELL_FOCUS_NONE for a base on a grid that was never given one, which is a run that matches
 * nothing and therefore steps nothing.
 *
 * **It says every tile is a place to stand**, because from here every one of them is: the grid
 * draws what it is handed, and which slots a screen chose to skip is the screen's own fact and
 * not something the window can see. A grid that skips one - draws nothing for a slot it has no
 * data for yet - must therefore fill in `focusable` on the run it keeps, exactly as a list with
 * subheaders in it must (see `focusable` on struct inkcell_focus_run). Leaving it NULL there
 * steps the cursor onto a tile that is not on the panel, and the ring vanishes for a frame.
 *
 * The array is the screen's and the run outlives the frame, so it has to be storage that
 * outlives the frame too - a field on the screen's own state, not a local in the render. A
 * screen that draws a *face* of its own for a slot rather than skipping it has nothing to
 * declare: it calls inkcell_fb_grid_focus_tile() as it draws, the tile is registered like any
 * other, and the run's default is the truth again.
 */
struct inkcell_focus_run inkcell_fb_grid_run(const struct inkcell_fb_grid *grid);

/* Tiles across, after the narrowing described on `cols` above. */
uint32_t inkcell_fb_grid_cols(const struct inkcell_fb_grid *grid);

/* Where the tile the walk is on will be drawn: the surface, not the cell it stands in. For a
   screen that draws a tile's face itself - a thumbnail it decoded, a chart - and wants the grid
   to do the placing. Nothing is advanced by drawing, so a screen may draw its own face for one
   tile and hand the next straight back to inkcell_fb_grid_tile(). */
struct inkcell_fb_rect inkcell_fb_grid_tile_box(const struct inkcell_fb_grid *grid);

/* Whether `index` is the tile the cursor is on: the one drawn with the container's selected
   fill, and the one the ring is over. */
bool inkcell_fb_grid_is_cursor(const struct inkcell_fb_grid *grid, uint32_t index);

/*
 * Registers the box the walk is on under the grid's base, and nothing else.
 *
 * inkcell_fb_grid_tile() ends with this, so a grid drawing its own tiles never needs it. What
 * needs it is the other half of inkcell_fb_grid_tile_box(): a screen that drew a face of its
 * own into that box has put a tile on the panel the grid knows nothing about, and *what is
 * drawn is what can be reached* is a promise the screen then has to keep for it.
 */
void inkcell_fb_grid_focus_tile(const struct inkcell_backend_fb_state *state,
                                const struct inkcell_fb_grid *grid, uint32_t index);

/* Draws the tile at the position the walk is on. The position came from the index rather than
   from an accumulated cursor, so a tile the screen chose not to draw costs the grid nothing -
   see inkcell_fb_grid_next(), and inkcell_fb_grid_run() for what a skipped slot owes the
   press. */
void inkcell_fb_grid_tile(const struct inkcell_backend_fb_state *state,
                          struct inkcell_fb_grid *grid, uint32_t index,
                          const struct inkcell_fb_tile *tile);

#endif /* INKCELL_BACKENDS_FB_WIDGETS_GRID_H */
