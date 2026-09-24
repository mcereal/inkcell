#ifndef INKCELL_GALLERY_H
#define INKCELL_GALLERY_H

/*
 * The gallery: every component inkcell ships, drawn once, so that a change to any of them is
 * reviewable as a picture rather than as a diff.
 *
 * It is three things at once and each one pays for the other two:
 *
 *   - **A specimen sheet.** What a button, a row, a meter and a dialog actually look like, in
 *     every theme and at more than one scale. A toolkit whose components can only be seen by
 *     building an application on it is a toolkit nobody evaluates.
 *   - **The widget test suite.** The ten files under src/fb/widgets_*.c are about five thousand
 *     lines and had no tests, because a unit test for "the button looks right" is a unit test
 *     nobody can write. A digest of the pixels is one anybody can: see tests/golden/.
 *   - **The worked integration example.** The README describes four things an application
 *     pushes into inkcell - a prefix, a catalog, an icon table, a render callback. This is a
 *     program that does them, which is the version that cannot go out of date.
 *
 * How a scene works
 * -----------------
 * A scene is a function that draws one whole frame, handed the same `state` a device renderer
 * gets. It is an ordinary inkcell screen: it opens a layout, draws chrome into it, and fills
 * the body. Nothing here is a special path through the library - that is the point, because a
 * picture taken through a special path is a picture of the special path.
 *
 * Scenes are listed in one table in scenes.c rather than registered from constructors, and the
 * order of that table is part of the manifest: a golden run names its pictures, so a scene that
 * moved would otherwise read as every scene after it having changed.
 */

#include "inkcell/i18n/strings.h"
#include "inkcell/ui/fb_draw.h"
#include "inkcell/ui/widgets.h"

#include <stddef.h>
#include <stdint.h>

/* ---- the gallery's words ---------------------------------------------------------------------
 *
 * Ids continuing inkcell's, built from examples/gallery/catalog.def exactly as
 * include/inkcell/i18n/strings.h describes. _BASE is INKCELL_STR_COUNT - 1 so that the first
 * entry below lands on INKCELL_STR_COUNT and the two halves index one table straight through.
 */
enum gallery_str_id {
    GALLERY_STR__BASE = INKCELL_STR_COUNT - 1,
#define INKCELL_STR_ENTRY(id, text) GALLERY_STR_##id,
#define INKCELL_STR_PLURAL_ENTRY(id, one, other) GALLERY_STR_##id##_ONE, GALLERY_STR_##id##_OTHER,
#include "catalog.def"
#undef INKCELL_STR_ENTRY
#undef INKCELL_STR_PLURAL_ENTRY
    GALLERY_STR_COUNT
};

/* Installs the catalog. Called once, before inkcell_i18n_init(). */
void gallery_i18n_install(void);

/*
 * A gallery id as the id a widget takes.
 *
 * The two enums share one numbering space by construction, so this is a cast and not a lookup.
 * It is a function rather than a cast at every call site because -Wconversion is right to want
 * to be told, and being told once is better than being told sixty times.
 */
static inline inkcell_str_id gallery_id(enum gallery_str_id id) {
    return (inkcell_str_id)id;
}

/* The text for a gallery id in the locale in force. Never NULL. */
static inline const char *gallery_text(enum gallery_str_id id) {
    return inkcell_str(gallery_id(id));
}

/* ---- scenes --------------------------------------------------------------------------- */

/*
 * The clock every page is drawn against, in milliseconds.
 *
 * Named rather than read, so a frame lands in the same place on every host - and named *here*
 * rather than in the renderer, because a scene scripting an interaction across the two frames
 * the harness renders (see `settle_ms`) needs to know which of them it is on. The focus ring's
 * page is the one that does: the first frame puts the cursor somewhere and the second is the
 * picture of it moving.
 */
#define GALLERY_CLOCK_MS 1000U

/*
 * The layers this gallery opens, as the ids a screen would keep in an enum of its own.
 *
 * They start high enough to be obviously not a focus id: the two spaces are unrelated, and a
 * reader looking at `.id = 3` in a scene has no way to tell which of them it is in.
 */
enum gallery_overlay_id {
    GALLERY_OVERLAY_DIALOG = 9000,
    GALLERY_OVERLAY_MENU,
    GALLERY_OVERLAY_SHEET,
    GALLERY_OVERLAY_TOOLTIP,
};

struct gallery_scene {
    /* What the picture is of, and the stem its file takes: lower case, no spaces. */
    const char *name;
    /* Draws one whole frame. Mutable because the components that animate - the switch, the
       meter, the slider - keep their position on the state, and a scene is entitled to the
       same components a screen gets rather than to a frozen copy of them. */
    void (*render)(struct inkcell_draw_state *state);
    /*
     * How far to step the clock before taking the picture, in milliseconds. 0 means the first
     * frame is the picture.
     *
     * Some components are invisible on their first frame *by design*, and a sheet that showed
     * them as they are at time zero would be a sheet of empty space. The snackbar is the clear
     * case: it arrives from below the panel, so the frame that introduces it draws it entirely
     * off-screen, and only a later frame has it anywhere a person could see. A switch that has
     * just been flicked is the same story with a smaller journey.
     *
     * So a scene may say "draw me, then let this much time pass, then draw me again" - which
     * is what a device does at sixty frames a second and what a still picture has to ask for.
     * The clock is named rather than read (see GALLERY_CLOCK_MS), so a settled frame lands in
     * exactly the same place on every host.
     */
    uint32_t settle_ms;
    /* The page's size, when it is not the handheld panel's - 0 for INKCELL_CAPTURE_WIDTH by
       INKCELL_CAPTURE_HEIGHT. For the scenes whose subject is what a wider surface does, which
       the panel's two scales cannot reach on their own: expanded needs about 120 columns. */
    uint32_t width;
    uint32_t height;
};

/* The table, in manifest order. */
const struct gallery_scene *gallery_scenes(size_t *count);

/* One scene per file under scenes_*.c, in the order scenes.c lists them. */
void gallery_scene_buttons(struct inkcell_draw_state *state);
void gallery_scene_chrome(struct inkcell_draw_state *state);
void gallery_scene_fab(struct inkcell_draw_state *state);
void gallery_scene_controls(struct inkcell_draw_state *state);
void gallery_scene_list(struct inkcell_draw_state *state);
void gallery_scene_grid(struct inkcell_draw_state *state);
void gallery_scene_grid_covers(struct inkcell_draw_state *state);
void gallery_scene_cards(struct inkcell_draw_state *state);
void gallery_scene_meters(struct inkcell_draw_state *state);
void gallery_scene_transcript(struct inkcell_draw_state *state);
void gallery_scene_overlays(struct inkcell_draw_state *state);
void gallery_scene_typography(struct inkcell_draw_state *state);
void gallery_scene_stack(struct inkcell_draw_state *state);
void gallery_scene_palette(struct inkcell_draw_state *state);
void gallery_scene_shapes(struct inkcell_draw_state *state);
void gallery_scene_keyboard(struct inkcell_draw_state *state);
void gallery_scene_keyboard_emoji(struct inkcell_draw_state *state);
void gallery_scene_focus(struct inkcell_draw_state *state);
void gallery_scene_focus_ring(struct inkcell_draw_state *state);
void gallery_scene_glide(struct inkcell_draw_state *state);
void gallery_scene_layers(struct inkcell_draw_state *state);
void gallery_scene_scroll(struct inkcell_draw_state *state);
void gallery_scene_scroll_overscroll(struct inkcell_draw_state *state);
void gallery_scene_scroll_title(struct inkcell_draw_state *state);
void gallery_scene_scaffold(struct inkcell_draw_state *state);
void gallery_scene_scaffold_top(struct inkcell_draw_state *state);
void gallery_scene_list_grouped(struct inkcell_draw_state *state);
void gallery_scene_list_sections(struct inkcell_draw_state *state);
void gallery_scene_list_plain(struct inkcell_draw_state *state);
void gallery_scene_app_bar(struct inkcell_draw_state *state);

/* ---- shared scene furniture --------------------------------------------------------------------
 *
 * The frame every scene stands in: the tab strip across the top and the keycap row along the
 * bottom, so that a specimen is shown where it would really sit rather than floating on a blank
 * panel. Returns the layout with the chrome's room already taken out of it.
 */
struct inkcell_fb_layout gallery_frame(struct inkcell_draw_state *state, enum gallery_str_id title,
                                       size_t tab);

/* The keycap row, drawn last because it sits over a body that was laid out before it. */
void gallery_footer(const struct inkcell_draw_state *state, const struct inkcell_fb_layout *layout);

/* A section heading inside a scene's body, and the row it leaves the next thing standing on. */
int gallery_section(const struct inkcell_draw_state *state, const struct inkcell_fb_layout *layout,
                    int y, enum gallery_str_id title);

#endif /* INKCELL_GALLERY_H */
