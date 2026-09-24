#define _POSIX_C_SOURCE 200809L

/*
 * The grid of tiles: the window's geometry, and what one tile puts on its surface.
 *
 * The window arithmetic is not here - it is `struct inkcell_grid` in src/layout.c, which is
 * `struct inkcell_list` over the grid's rows. What is here is the pixels between that and the
 * panel: how wide a tile comes out, how many of them fit across before one is too small to be
 * worth drawing, how tall the art is for the shape the caller asked for, and where a name
 * stands relative to the picture it names.
 *
 * Every one of those was going to be written into the first screen that wanted a home page, and
 * then into the second one differently.
 */

#include "inkcell/ui/widgets/grid.h"
#include "list_internal.h"

#include "inkcell/ui/emoji.h"
#include "inkcell/ui/widgets/button.h"

#include <string.h>

/* ---- the geometry ---------------------------------------------------------------------------- */

/*
 * The narrowest tile worth drawing, which is what a column request is narrowed against.
 *
 * Two floors and the larger wins, because a tile too small fails in two different ways. It has
 * to hold its picture: an icon at the body scale with the padding either side, below which the
 * art is a speck in a box rather than a thing anybody recognises. And it has to hold a *word*:
 * four nominal cells, below which every name on the grid is one syllable and an ellipsis, and a
 * reader who cannot recognise the picture has nothing left to read.
 *
 * The cells are nominal - inkcell_fb_char_adv() - and that is the right measure here for once:
 * what is being reserved is room for text in general rather than for a particular string, which
 * is exactly the case the note in the README leaves the nominal advance for.
 */
static int inkcell_fb_tile_floor(const struct inkcell_draw_state *state, int label_scale) {
    const int pad = inkcell_fb_space(state, INKCELL_SPACE_MD);
    const int art = inkcell_fb_icon_drawn(state, state->scale) + 2 * pad;
    const int words = 4 * inkcell_fb_char_adv(state, label_scale) + 2 * pad;
    return art > words ? art : words;
}

/* How wide a tile is whose art is `art_h` tall: the inverse of the function below, for the
   caller that stated rows as well as columns and so constrained the tile from both sides. Both
   are integer divisions of the same pair of numbers, so the round trip loses at most a pixel -
   and it loses it *inward*, which is the direction a fit has to err in. */
static int inkcell_fb_tile_art_w(int art_h, enum inkcell_fb_tile_ratio ratio) {
    switch (ratio) {
    case INKCELL_FB_TILE_PORTRAIT:
        return art_h * 3 / 4;
    case INKCELL_FB_TILE_LANDSCAPE:
        return art_h * 16 / 9;
    case INKCELL_FB_TILE_SQUARE:
    default:
        return art_h;
    }
}

/* How tall the art is on a tile `width` across. The ratios are the ones the enum names, done in
   integers so a tile is the same tile on every host that draws it. */
static int inkcell_fb_tile_art_h(int width, enum inkcell_fb_tile_ratio ratio) {
    switch (ratio) {
    case INKCELL_FB_TILE_PORTRAIT:
        return width * 4 / 3;
    case INKCELL_FB_TILE_LANDSCAPE:
        return width * 9 / 16;
    case INKCELL_FB_TILE_SQUARE:
    default:
        return width;
    }
}

struct inkcell_fb_grid inkcell_fb_grid_begin(const struct inkcell_draw_state *state,
                                             const struct inkcell_fb_layout *layout, uint32_t count,
                                             uint32_t cursor,
                                             const struct inkcell_fb_grid_style *style) {
    struct inkcell_fb_grid grid;
    memset(&grid, 0, sizeof grid);
    if (state == NULL || layout == NULL) {
        return grid;
    }
    static const struct inkcell_fb_grid_style k_default = {0};
    if (style == NULL) {
        style = &k_default;
    }

    grid.label_place = style->label;
    grid.label_lines = style->label_lines > 0U ? style->label_lines : 1U;
    grid.label_scale = layout->small;
    grid.gap = inkcell_fb_space(state, INKCELL_SPACE_MD);

    /*
     * The row box rather than the margins, so a grid's tiles start and end exactly where a
     * list's rows do and the rail's strip is already clear. A screen with a list on one tab and
     * a grid on the next has one content column, not two that nearly agree.
     */
    const struct inkcell_fb_row_box box = inkcell_fb_row_box(state);

    /*
     * The columns, narrowed until a tile is at least the floor wide. A request this cannot
     * honour is narrowed rather than refused, because what a screen asked for is an intention
     * about density and the panel is the thing that knows whether it can be met - the keyboard
     * strikes the same bargain over its key height, and a card over the verbs it has room for.
     */
    /*
     * The name's block: the lines it was opened for, plus the air between it and the picture.
     *
     * Measured before the tile because a tile told how many rows to fit into is a tile whose art
     * gets what the names do not, and charged to every tile whether or not its own name used
     * both lines. A grid whose rows were each as tall as their longest name is a grid that
     * shuffles when one name changes, and a reader whose eye has learned where the next row
     * starts is a reader that grid has lied to.
     */
    const int label_line = inkcell_fb_line_adv(state, grid.label_scale);
    grid.label_h =
        grid.label_place == INKCELL_FB_TILE_LABEL_NONE
            ? 0
            : (int)grid.label_lines * label_line + inkcell_fb_space(state, INKCELL_SPACE_SM);
    /* What a tile costs beyond its art, whichever side of the surface the name is on: the label
       block and the gap to the row below. */
    const int spent = grid.label_h + grid.gap;

    grid.track_y = layout->body_y;
    grid.track_h = (int)layout->rows * layout->line;

    const int floor_w = inkcell_fb_tile_floor(state, grid.label_scale);
    /* The most columns that can each keep the floor: `c` tiles and the `c - 1` gaps between
       them have to fit the box, which rearranges to this. Derived rather than searched for, so
       a request of four thousand columns costs the same as a request of four - and so the
       multiplication below is over a number this panel could actually hold. */
    const int fits = (box.w + grid.gap) / (floor_w + grid.gap);
    uint32_t cols = style->cols > 0U ? style->cols : 1U;
    if (fits > 0 && cols > (uint32_t)fits) {
        cols = (uint32_t)fits;
    } else if (fits <= 0) {
        /* Not even one tile at the floor. One column anyway: a grid one tile wide is a list,
           and a list is what a panel this narrow can show. */
        cols = 1U;
    }
    grid.tile_w = (box.w - (int)(cols - 1U) * grid.gap) / (int)cols;

    /*
     * A stated row count is the second constraint, and the tile takes whichever of the two
     * binds. Keeping the shape is what makes that safe to do in one direction as well as the
     * other: a tile narrowed to fit a row count is still the tile the caller asked for, one
     * stretched to fill the body would not be.
     */
    if (style->rows > 0U && grid.track_h > 0) {
        const int by_height = inkcell_fb_tile_art_w(
            (grid.track_h + grid.gap) / (int)style->rows - spent, style->ratio);
        if (by_height < grid.tile_w) {
            grid.tile_w = by_height;
        }
    }
    if (grid.tile_w < 1) {
        grid.tile_w = 1;
    }

    grid.art_h = inkcell_fb_tile_art_h(grid.tile_w, style->ratio);
    grid.tile_h =
        grid.art_h + (grid.label_place == INKCELL_FB_TILE_LABEL_INSIDE ? grid.label_h : 0);
    grid.cell_w = grid.tile_w + grid.gap;
    grid.cell_h = grid.tile_h + grid.gap +
                  (grid.label_place == INKCELL_FB_TILE_LABEL_BELOW ? grid.label_h : 0);

    /*
     * Where the tiles stand. Centred in the row box across, which is the leading margin until a
     * row count has left the columns narrower than the body - and then it is half the slack
     * either side, because a row of tiles hugging one margin with the rest of the panel empty
     * after it reads as a layout that ran out.
     *
     * Down, the first tile starts a hairline above the body's first baseline: that is where a
     * list's row fill starts, and a grid a hairline lower than the list on the tab beside it is
     * a toolkit disagreeing with itself about where a body begins.
     */
    const int used = (int)cols * grid.tile_w + (int)(cols - 1U) * grid.gap;
    grid.origin_x = box.x + (box.w > used ? (box.w - used) / 2 : 0);
    grid.origin_y = layout->body_y - inkcell_step_px(state->scale);
    grid.x = grid.origin_x;
    grid.y = grid.origin_y;

    /*
     * Rows that fit. `n` rows occupy `n * cell_h - gap`, because the gap after the last one is
     * outside the body rather than inside it - so the body's height is measured with one gap
     * added back rather than with a row's worth of slack left unspent.
     */
    uint32_t visible_rows = 0U;
    if (grid.cell_h > 0 && grid.track_h > 0) {
        visible_rows = (uint32_t)((grid.track_h + grid.gap) / grid.cell_h);
    }
    grid.model = inkcell_grid_begin(count, cursor, cols, visible_rows);
    return grid;
}

uint32_t inkcell_fb_grid_cols(const struct inkcell_fb_grid *grid) {
    return grid != NULL ? grid->model.cols : 0U;
}

bool inkcell_fb_grid_is_cursor(const struct inkcell_fb_grid *grid, uint32_t index) {
    return grid != NULL && inkcell_grid_is_cursor(&grid->model, index);
}

struct inkcell_fb_rect inkcell_fb_grid_tile_box(const struct inkcell_fb_grid *grid) {
    if (grid == NULL) {
        return (struct inkcell_fb_rect){0, 0, 0, 0};
    }
    return (struct inkcell_fb_rect){
        .x = grid->x, .y = grid->y, .w = grid->tile_w, .h = grid->tile_h};
}

bool inkcell_fb_grid_next(struct inkcell_fb_grid *grid, uint32_t *index) {
    if (grid == NULL || !inkcell_grid_next(&grid->model, index)) {
        return false;
    }
    /*
     * The position is computed from the index rather than accumulated, so a screen that skips a
     * tile - draws its own face for one, or draws nothing at all for a slot it has no data for
     * yet - still finds the next one where the reader expects it. An accumulated cursor would
     * pull the whole rest of the grid one place left.
     */
    const uint32_t col = *index % grid->model.cols;
    const uint32_t row = *index / grid->model.cols - grid->model.rows.first;
    grid->x = grid->origin_x + (int)col * grid->cell_w;
    grid->y = grid->origin_y + (int)row * grid->cell_h;
    return true;
}

void inkcell_fb_grid_focus(struct inkcell_fb_grid *grid, uint32_t base) {
    if (grid == NULL) {
        return;
    }
    grid->focus_base = base;
}

/*
 * The box the walk is on, registered under the grid's base.
 *
 * The surface, and the shape it was actually filled with - so the ring that lands here next
 * frame takes this tile's corners rather than a guess at them. The caption below a tile is not
 * part of this box: it stands on the panel, and a ring drawn round it would claim an edge
 * nothing painted.
 *
 * Called by inkcell_fb_grid_tile() for the tiles it draws, and by a screen for the ones it drew
 * itself. A screen has no other way to say it: the ids are the grid's base plus an index, and a
 * face drawn through inkcell_fb_grid_tile_box() would otherwise be a tile on the panel that the
 * cursor cannot reach.
 */
void inkcell_fb_grid_focus_tile(const struct inkcell_draw_state *state,
                                const struct inkcell_fb_grid *grid, uint32_t index) {
    if (state == NULL || grid == NULL || grid->focus_base == INKCELL_FOCUS_NONE) {
        return;
    }
    const struct inkcell_fb_rect rect = {
        .x = grid->x, .y = grid->y, .w = grid->tile_w, .h = grid->tile_h};
    inkcell_fb_focus_register_shaped(state, grid->focus_base + index, &rect, INKCELL_SHAPE_MD);
    if (inkcell_fb_grid_is_cursor(grid, index)) {
        inkcell_fb_focus_mark(state, grid->focus_base + index);
    }
}

struct inkcell_focus_run inkcell_fb_grid_run(const struct inkcell_fb_grid *grid) {
    struct inkcell_focus_run run;
    memset(&run, 0, sizeof run);
    if (grid == NULL) {
        return run;
    }
    run.base = grid->focus_base;
    run.count = grid->model.count;
    run.stride = grid->model.cols;
    return run;
}

/* The rail, drawn once per grid by the first tile that draws. The list's rail, in the list's
   gutter, measured over the grid's window of rows - see inkcell_fb_draw_list_rail(). */
static void inkcell_fb_grid_chrome(const struct inkcell_draw_state *state,
                                   struct inkcell_fb_grid *grid) {
    if (grid->chrome_drawn) {
        return;
    }
    grid->chrome_drawn = true;
    inkcell_fb_draw_list_rail(state, &grid->model.rows, grid->track_y, grid->track_h);
}

/* ---- the tile --------------------------------------------------------------------------------
 */

/*
 * The picture, centred in the art box at the largest size that box has room for.
 *
 * "The largest that fits" is measured rather than assumed, for the avatar's reason one file
 * over: which multiplier fits is a fact about this geometry, and a screen that worked it out
 * would be computing a glyph size. The three kinds differ only in what they measure - an icon
 * by its drawn width and the glyph body's height, initials by the width of those particular
 * letters, a sprite by the whole multiple of its own grid that fits - and all three are
 * centred on the glyph body rather than on the line advance, because the advance carries the
 * gap accents hang in and counting it sits the picture low in the tile.
 */
static void inkcell_fb_tile_art(const struct inkcell_draw_state *state,
                                const struct inkcell_fb_tile *tile, int x, int y, int w, int h,
                                struct inkcell_paint paint) {
    const int pad = inkcell_fb_space(state, INKCELL_SPACE_MD);
    const int room_w = w - 2 * pad;
    const int room_h = h - 2 * pad;
    if (room_w <= 0 || room_h <= 0) {
        return;
    }
    const int glyph_h = (int)inkcell_fb_font(state)->height;

    switch (tile->art) {
    case INKCELL_FB_TILE_ART_ICON: {
        if (!inkcell_icon_is_valid(tile->icon)) {
            return;
        }
        int scale = INKCELL_FB_ICON_SCALE_MAX;
        /* A whole step at a time. The scale has quarters in it now, and this loop could fit the
           art a good deal closer by walking them - but a tile that shrinks by a quarter step is
           a different tile, so stepping stays what it was and the finer fit is a change of its
           own. */
        while (scale > INKCELL_SCALE(1) && (inkcell_fb_icon_drawn(state, scale) > room_w ||
                                            inkcell_scale_px(glyph_h, scale) > room_h)) {
            scale -= INKCELL_SCALE(1);
        }
        inkcell_fb_draw_icon(state, x + (w - inkcell_fb_icon_box(state, scale)) / 2,
                             y + (h - inkcell_scale_px(glyph_h, scale)) / 2, tile->icon, scale,
                             paint.ink, paint.fill);
        return;
    }
    case INKCELL_FB_TILE_ART_EMOJI: {
        if (tile->text == NULL) {
            return;
        }
        const struct inkcell_text_cell cell = inkcell_text_cell_next(tile->text);
        if (!cell.is_emoji) {
            return;
        }
        int box = room_w < room_h ? room_w : room_h;
        if (box > INKCELL_FB_EMOJI_BOX_MAX) {
            box = INKCELL_FB_EMOJI_BOX_MAX;
        }
        box = inkcell_fb_emoji_box_fit(box);
        if (box <= 0) {
            return;
        }
        inkcell_fb_draw_emoji_box(state, x + (w - box) / 2, y + (h - box) / 2, box, cell.sprite);
        return;
    }
    case INKCELL_FB_TILE_ART_INITIALS: {
        if (tile->text == NULL || tile->text[0] == '\0') {
            return;
        }
        int scale = INKCELL_SCALE_MAX;
        /* A whole step at a time, and never past one - the same floor the icon above stops at.
           Walking raw units instead would let a cramped tile take its initials down to a quarter
           step, which is below anything the type scale can name and is not a smaller label but
           an unreadable one. */
        while (scale > INKCELL_SCALE(1) &&
               (inkcell_fb_text_width(state, tile->text, scale) > room_w ||
                inkcell_scale_px(glyph_h, scale) > room_h)) {
            scale -= INKCELL_SCALE(1);
        }
        inkcell_fb_draw_text(state, x + (w - inkcell_fb_text_width(state, tile->text, scale)) / 2,
                             y + (h - inkcell_scale_px(glyph_h, scale)) / 2, tile->text, scale,
                             paint.ink, paint.fill);
        return;
    }
    case INKCELL_FB_TILE_ART_NONE:
    default:
        return;
    }
}

/*
 * A name, wrapped to the tile's width and set over at most `lines` of them. Returns the lines
 * it used, so whatever follows knows what is left.
 *
 * Wrapped against pixels rather than against a column count, because a tile is narrow and a
 * proportional face is exactly where counting cells comes apart: "Illustrations" and "WWW Radio"
 * are thirteen and nine cells and very nearly the same width. What does not fit is dropped -
 * a tile's name is a label, and a label that spilled onto a third line would be a tile drawing
 * over the one below it.
 */
static uint32_t inkcell_fb_tile_text(const struct inkcell_draw_state *state, int x, int y,
                                     int width, const char *text, int scale,
                                     enum inkcell_weight weight, uint32_t lines, bool centred,
                                     struct inkcell_rgb ink, struct inkcell_rgb ground) {
    if (text == NULL || text[0] == '\0' || width <= 0 || lines == 0U) {
        return 0U;
    }
    struct inkcell_fb_wrap_ctx wctx;
    const struct inkcell_wrap_metric metric = inkcell_fb_wrap_metric(&wctx, state, scale);
    struct inkcell_wrap wrap;
    inkcell_wrap_begin_measured(&wrap, text, (size_t)width, &metric);

    uint32_t drawn = 0U;
    while (drawn < lines && inkcell_wrap_next(&wrap)) {
        const int line_x =
            centred
                ? x + (width - inkcell_fb_text_width_weight(state, wrap.line, scale, weight)) / 2
                : x;
        inkcell_fb_draw_text_weight(state, line_x, y, wrap.line, scale, weight, ink, ground);
        y += inkcell_fb_line_adv(state, scale);
        drawn += 1U;
    }
    return drawn;
}

/*
 * The name and the line under it, wherever the grid was opened to put them.
 *
 * The two are told apart by *weight* rather than by a second colour, and that is deliberate on
 * the half of this that stands on the tile: the fill and the ink on it are a pair the theme was
 * validated as a pair, and a quieter version of that ink is a combination nothing checked. A
 * name below the tile stands on the body's ground instead, where INKCELL_TONE_DIM is a role
 * with a contract of its own - so that half says it the way every list row says it.
 */
static void inkcell_fb_tile_label(const struct inkcell_draw_state *state,
                                  const struct inkcell_fb_grid *grid,
                                  const struct inkcell_fb_tile *tile, struct inkcell_paint paint) {
    if (grid->label_place == INKCELL_FB_TILE_LABEL_NONE || grid->label_h <= 0) {
        return;
    }
    const bool inside = grid->label_place == INKCELL_FB_TILE_LABEL_INSIDE;
    const int pad = inkcell_fb_space(state, INKCELL_SPACE_MD);
    const int x = inside ? grid->x + pad : grid->x;
    const int width = inside ? grid->tile_w - 2 * pad : grid->tile_w;
    /* Under the art either way: inside the surface it is the band the tile's own height already
       reserved, and below it the caption starts a step under the corner. */
    int y = grid->y + grid->art_h + inkcell_fb_space(state, INKCELL_SPACE_SM);

    const struct inkcell_rgb ground =
        inside ? paint.fill : inkcell_fb_color(state, INKCELL_COLOR_BG);
    const struct inkcell_rgb ink = inside ? paint.ink : inkcell_fb_tone_color(state, tile->tone);
    const uint32_t used = inkcell_fb_tile_text(state, x, y, width, tile->label, grid->label_scale,
                                               inkcell_fb_type_weight(state, INKCELL_TYPE_LABEL),
                                               grid->label_lines, !inside, ink, ground);
    if (used >= grid->label_lines) {
        return;
    }
    y += (int)used * inkcell_fb_line_adv(state, grid->label_scale);
    (void)inkcell_fb_tile_text(
        state, x, y, width, tile->supporting, grid->label_scale,
        inkcell_fb_type_weight(state, INKCELL_TYPE_BODY), grid->label_lines - used, !inside,
        inside ? paint.ink : inkcell_fb_tone_color(state, INKCELL_TONE_DIM), ground);
}

/* The count in the top trailing corner, in the space the tile's own padding already leaves. A
   badge that reached the corner would sit on the container's curve, which is where the corner
   has already begun to turn away underneath it. */
static void inkcell_fb_tile_badge(const struct inkcell_draw_state *state,
                                  const struct inkcell_fb_grid *grid,
                                  const struct inkcell_fb_tile *tile) {
    if (tile->badge == NULL || tile->badge[0] == '\0') {
        return;
    }
    const int scale = grid->label_scale;
    const int width = inkcell_fb_badge_width(state, tile->badge, scale);
    const int height = inkcell_fb_line_adv(state, scale);
    const int pad = inkcell_fb_space(state, INKCELL_SPACE_SM);
    if (width <= 0 || width + 2 * pad > grid->tile_w) {
        return;
    }
    const struct inkcell_fb_rect box = {
        .x = grid->x + grid->tile_w - width - pad, .y = grid->y + pad, .w = width, .h = height};
    const int text_h = inkcell_scale_px((int)inkcell_fb_font(state)->height, scale);
    inkcell_fb_draw_badge(state, &box, box.y + (height - text_h) / 2, tile->badge,
                          tile->badge_family, scale);
}

void inkcell_fb_grid_tile(const struct inkcell_draw_state *state, struct inkcell_fb_grid *grid,
                          uint32_t index, const struct inkcell_fb_tile *tile) {
    if (state == NULL || grid == NULL || tile == NULL) {
        return;
    }
    inkcell_fb_grid_chrome(state, grid);

    const bool focused = inkcell_fb_grid_is_cursor(grid, index);
    const enum inkcell_state ui_state = focused ? INKCELL_STATE_FOCUSED : INKCELL_STATE_REST;
    /*
     * One statement decides the colour, and it is the tile's tone.
     *
     * A tone that names a family fills with that family's container and writes in the ink the
     * theme checked against it; a neutral tone has no family to ask, so it takes the surface a
     * card takes with the same state layer over it. Both arrive as a *pair*, which is the whole
     * point of asking this way: a widget that took its fill from one place and its ink from
     * another is a widget drawing a combination nothing validated.
     */
    const enum inkcell_family family = inkcell_tone_family(tile->tone);
    struct inkcell_paint paint;
    if (family != INKCELL_FAMILY_COUNT) {
        paint = inkcell_fb_paint(state, family, INKCELL_SLOT_CONTAINER, ui_state);
    } else {
        paint.fill =
            inkcell_fb_state_layer(state, INKCELL_COLOR_SURFACE, INKCELL_COLOR_TEXT, ui_state);
        paint.ink = inkcell_fb_color(state, INKCELL_COLOR_TEXT);
    }

    const int radius = inkcell_fb_radius(state, INKCELL_SHAPE_MD);
    inkcell_fb_fill_round_rect(state, grid->x, grid->y, grid->tile_w, grid->tile_h, radius,
                               paint.fill);
    inkcell_fb_tile_art(state, tile, grid->x, grid->y, grid->tile_w, grid->art_h, paint);
    inkcell_fb_tile_label(state, grid, tile, paint);
    inkcell_fb_tile_badge(state, grid, tile);

    inkcell_fb_grid_focus_tile(state, grid, index);
}
