#define _POSIX_C_SOURCE 200809L

/*
 * The grid, twice: the home screen and the shelf.
 *
 * Two pages because a tile has two idioms and they are not the same component lightly restyled.
 * The first is the springboard - square tiles, a name under each, one colour per thing - which
 * is what a handheld's home screen is on every device that has one. The second is Material's
 * image list: a cover, portrait, with its name written on the tile itself, which is what a
 * shelf of documents or box art wants and what a caption floating underneath cannot do.
 *
 * What both pages are really pictures of is the *window*. The first is scrolled, so the rail
 * beside it is the list's rail measuring rows of tiles; the second ends on a short row, which
 * is the case a grid has and a list does not. Neither scene computes a coordinate: the tiles
 * say what they are and the grid places them, which is the same bargain every other page here
 * is drawn under.
 */

#include "gallery.h"

#include "inkcell/ui/focus.h"

/* The ids the tiles take. A base with an index folded into it, which is how a list's rows are
   named and for the same reason: a grid cursor is already an item index. */
enum {
    GRID_ID_TILE = 100,
};

/* Room for the tiles a window can hold, with slack so `dropped` stays a signal. */
#define GRID_STORAGE 32U

/* ---- the home screen --------------------------------------------------------------------------
 */

/*
 * Eleven things a radio can show, four across.
 *
 * Eleven rather than eight: a grid that fits its window is a grid with no rail, and the rail is
 * half of what a window *is*. Eleven across four is also a short last row, which is the shape a
 * list cannot have - so the page carries the two facts a grid's window adds at once.
 */
static const struct inkcell_fb_tile *gallery_grid_tiles(size_t *count) {
    static const struct inkcell_fb_tile k_tiles[] = {
        {.label = "", /* filled in below: the catalog is not a compile-time constant */
         .art = INKCELL_FB_TILE_ART_ICON,
         .icon = INKCELL_ICON_MESSAGES,
         .tone = INKCELL_TONE_PRIMARY,
         .badge = "3",
         .badge_family = INKCELL_FAMILY_ERROR},
        {.art = INKCELL_FB_TILE_ART_ICON,
         .icon = INKCELL_ICON_NODES,
         .tone = INKCELL_TONE_SECONDARY},
        {.art = INKCELL_FB_TILE_ART_ICON, .icon = INKCELL_ICON_MAP, .tone = INKCELL_TONE_TERTIARY},
        {.art = INKCELL_FB_TILE_ART_ICON,
         .icon = INKCELL_ICON_CHANNEL,
         .tone = INKCELL_TONE_SUCCESS},
        {.art = INKCELL_FB_TILE_ART_ICON,
         .icon = INKCELL_ICON_POSITION,
         .tone = INKCELL_TONE_WARNING},
        /* The name that does not fit on one line, which is what the second label line is for. */
        {.art = INKCELL_FB_TILE_ART_ICON,
         .icon = INKCELL_ICON_TELEMETRY,
         .tone = INKCELL_TONE_NORMAL},
        {.art = INKCELL_FB_TILE_ART_ICON, .icon = INKCELL_ICON_ABOUT, .tone = INKCELL_TONE_DIM},
        /* A tile whose face is a sprite rather than a symbol, and two whose faces are the two
           letters of a name - the three kinds of art, on one row. */
        {.art = INKCELL_FB_TILE_ART_EMOJI, .text = "\xf0\x9f\x93\xa1", .tone = INKCELL_TONE_NORMAL},
        {.art = INKCELL_FB_TILE_ART_INITIALS, .text = "FS", .tone = INKCELL_TONE_TERTIARY},
        {.art = INKCELL_FB_TILE_ART_INITIALS, .text = "RG", .tone = INKCELL_TONE_SUCCESS},
        /* The container alone: a tile with nothing to show yet. */
        {.art = INKCELL_FB_TILE_ART_NONE, .tone = INKCELL_TONE_DIM},
    };
    *count = sizeof k_tiles / sizeof k_tiles[0];
    return k_tiles;
}

/* The names, in the order above. Looked up rather than listed in the table because the catalog
   is read at run time and a static initialiser cannot call into it. */
static const char *gallery_grid_name(size_t index) {
    static const enum gallery_str_id k_names[] = {
        GALLERY_STR_TILE_MESSAGES, GALLERY_STR_TILE_NODES,    GALLERY_STR_TILE_MAP,
        GALLERY_STR_TILE_CHANNELS, GALLERY_STR_TILE_POSITION, GALLERY_STR_TILE_TELEMETRY,
        GALLERY_STR_TILE_ABOUT,    GALLERY_STR_COVER_WEATHER, GALLERY_STR_COVER_FIELD,
        GALLERY_STR_COVER_ROUTES,  GALLERY_STR_COVER_SPARES,
    };
    return gallery_text(k_names[index]);
}

void gallery_scene_grid(struct inkcell_draw_state *state) {
    struct inkcell_focus_item storage[GRID_STORAGE];
    struct inkcell_focus_map map;
    struct inkcell_fb_layout layout = gallery_frame(state, GALLERY_STR_HEAD_TILES, 1U);

    /*
     * The map goes in before anything draws, and nothing in this scene adds to it. The grid
     * registers the tiles its window landed on - which on a page that is scrolled is the whole
     * point, because the four tiles above the window are not on the panel and the cursor must
     * not be able to reach them.
     */
    inkcell_focus_begin(&map, storage, GRID_STORAGE);
    inkcell_fb_set_focus_map(state, &map);

    size_t count = 0U;
    const struct inkcell_fb_tile *tiles = gallery_grid_tiles(&count);

    /* Four across and two down, which is what a home screen is: the tiles take whichever of the
       two constraints binds, so the page fills the body at the large glyph scale as well as the
       small one rather than leaving a row of nothing under itself. */
    const struct inkcell_fb_grid_style style = {
        .cols = 4U,
        .rows = 2U,
        .ratio = INKCELL_FB_TILE_SQUARE,
        .label = INKCELL_FB_TILE_LABEL_BELOW,
        .label_lines = 2U,
    };
    /* The cursor is in the last row, so the window has scrolled off the first one: the rail
       beside the tiles is measuring rows of them, and the four tiles above are not registered
       because they are not on the panel. */
    struct inkcell_fb_grid grid =
        inkcell_fb_grid_begin(state, &layout, (uint32_t)count, 9U, &style);
    inkcell_fb_grid_focus(&grid, GRID_ID_TILE);

    uint32_t index = 0U;
    while (inkcell_fb_grid_next(&grid, &index)) {
        struct inkcell_fb_tile tile = tiles[index];
        tile.label = gallery_grid_name(index);
        inkcell_fb_grid_tile(state, &grid, index, &tile);
    }

    /* Last, over the body: the ring is one object travelling between boxes rather than a
       property of each of them - see include/inkcell/ui/widgets/focus.h. */
    inkcell_fb_draw_focus_ring(state, &map, GRID_ID_TILE + grid.model.cursor);
    gallery_footer(state, &layout);
}

/* ---- the shelf ---------------------------------------------------------------------------------
 */

void gallery_scene_grid_covers(struct inkcell_draw_state *state) {
    struct inkcell_focus_item storage[GRID_STORAGE];
    struct inkcell_focus_map map;
    struct inkcell_fb_layout layout = gallery_frame(state, GALLERY_STR_HEAD_COVERS, 1U);

    inkcell_focus_begin(&map, storage, GRID_STORAGE);
    inkcell_fb_set_focus_map(state, &map);

    /*
     * Seven covers five across: a full row and a short one.
     *
     * The short row is the case a grid has and a list does not, and it is worth a page of its
     * own because two things have to be right about it at once - the tiles in it stop where the
     * items stop rather than filling the row with empty containers, and a press down from the
     * row above lands on the last of them rather than on nothing (see `stride` on struct
     * inkcell_focus_run).
     */
    const struct inkcell_fb_tile covers[] = {
        {.label = gallery_text(GALLERY_STR_COVER_FIELD),
         .supporting = gallery_text(GALLERY_STR_COVER_AGO_TODAY),
         .art = INKCELL_FB_TILE_ART_INITIALS,
         .text = "FN",
         .tone = INKCELL_TONE_PRIMARY},
        {.label = gallery_text(GALLERY_STR_COVER_ROUTES),
         .supporting = gallery_text(GALLERY_STR_COVER_PAGES),
         .art = INKCELL_FB_TILE_ART_ICON,
         .icon = INKCELL_ICON_MAP,
         .tone = INKCELL_TONE_TERTIARY},
        {.label = gallery_text(GALLERY_STR_COVER_WEATHER),
         .supporting = gallery_text(GALLERY_STR_COVER_AGO_WEEK),
         .art = INKCELL_FB_TILE_ART_EMOJI,
         .text = "\xf0\x9f\x8c\xa7",
         .tone = INKCELL_TONE_NORMAL},
        {.label = gallery_text(GALLERY_STR_COVER_SPARES),
         .art = INKCELL_FB_TILE_ART_ICON,
         .icon = INKCELL_ICON_MODULES,
         .tone = INKCELL_TONE_SECONDARY},
        /* The one whose name is longer than its cover is wide: a label inside a tile is clipped
           by the tile rather than by the panel, and this is the tile that shows it. */
        {.label = gallery_text(GALLERY_STR_TILE_TELEMETRY),
         .art = INKCELL_FB_TILE_ART_ICON,
         .icon = INKCELL_ICON_TELEMETRY,
         .tone = INKCELL_TONE_WARNING},
        {.label = gallery_text(GALLERY_STR_TILE_NODES),
         .supporting = gallery_text(GALLERY_STR_SHEET_DETAIL),
         .art = INKCELL_FB_TILE_ART_ICON,
         .icon = INKCELL_ICON_NODES,
         .tone = INKCELL_TONE_SUCCESS},
        {.label = gallery_text(GALLERY_STR_TILE_ABOUT),
         .art = INKCELL_FB_TILE_ART_ICON,
         .icon = INKCELL_ICON_ABOUT,
         .tone = INKCELL_TONE_DIM},
    };
    const uint32_t count = (uint32_t)(sizeof covers / sizeof covers[0]);

    /* Five across and two down. A portrait tile is half again as tall as it is wide, so this is
       the page where the row count is the constraint that binds at the larger glyph scale and
       the column count is the one that binds at the smaller - the same style, laid out from
       opposite sides on the two sheets. */
    const struct inkcell_fb_grid_style style = {
        .cols = 5U,
        .rows = 2U,
        .ratio = INKCELL_FB_TILE_PORTRAIT,
        .label = INKCELL_FB_TILE_LABEL_INSIDE,
        .label_lines = 2U,
    };
    struct inkcell_fb_grid grid = inkcell_fb_grid_begin(state, &layout, count, 6U, &style);
    inkcell_fb_grid_focus(&grid, GRID_ID_TILE);

    uint32_t index = 0U;
    while (inkcell_fb_grid_next(&grid, &index)) {
        inkcell_fb_grid_tile(state, &grid, index, &covers[index]);
    }

    inkcell_fb_draw_focus_ring(state, &map, GRID_ID_TILE + grid.model.cursor);
    gallery_footer(state, &layout);
}
