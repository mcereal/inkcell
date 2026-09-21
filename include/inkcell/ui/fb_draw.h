#ifndef INKCELL_UI_FB_DRAW_H
#define INKCELL_UI_FB_DRAW_H

/*
 * The drawing toolkit: pixels, glyphs, rows, the palette and the page geometry.
 *
 * The layering an inkcell application stacks on top of this is:
 *
 *   src/fb/fb_draw.c     pixels, glyphs, rows, the palette and the page geometry   (this header)
 *   widgets_*     the components screens are assembled from (inkcell/ui/widgets.h is the umbrella)
 *   <app>         one renderer per screen, drawn out of whatever the app calls a snapshot
 *   fb.c          opening /dev/fb0, the page flip, the backend vtable
 *
 * Calls only ever go downward, so this header is what the layers above draw with and
 * inkcell/ui/widgets.h is the component set above it. Both are public: an application built on
 * inkcell writes the screens and nothing else, so the toolkit they are written against is the
 * library's surface rather than a private detail of it.
 *
 * Only src/fb/fb_draw.c's own primitives live here, plus the geometry every layer above states in
 * - a rect, a row box, a layout. Anything that composes several of them into a thing with a name -
 * a button, a list, a field row - belongs in a header under inkcell/ui/widgets/ instead.
 *
 * No colour, margin or glyph size is spelled out below this comment. They come from the theme
 * on the state (inkcell/ui/theme.h) through the accessors on it, which is what lets one table
 * swap the whole look.
 *
 * Nothing here knows what the application's screens are about. The frame is drawn by a renderer
 * the app installs with inkcell_fb_set_app(), and the two facts a frame needs that no snapshot
 * carries - which way the reader just moved, and which theme they chose - are pushed in through
 * inkcell_fb_transition_begin() and inkcell_fb_state_set_theme_by_id() rather than read out of a
 * structure this library would then have to know the shape of.
 */

#include "inkcell/ui/anim.h"
#include "inkcell/ui/focus.h"
#include "inkcell/ui/icon.h"
#include "inkcell/ui/layout.h"
#include "inkcell/ui/theme.h"

#include <linux/fb.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct inkcell_fb_glyph_cache;
struct inkcell_fb_thread_cache;
struct inkcell_fb_render_cache;
struct inkcell_backend_fb_state;

/* How much of a notice fits. A snackbar is one line the reader is not expected to study. */
#define INKCELL_FB_SNACKBAR_MAX 64U

/* Bytes per pixel in the images inkcell_fb_blit_bgra() takes: B, G, R and a byte that is
   ignored. Named rather than spelled 4 because the blit reads a whole pixel as one 32-bit word
   and the two facts have to agree. */
#define INKCELL_FB_BGRA_BYTES 4U

/*
 * Which way the reader just moved, which is the one thing a frame cannot work out for itself.
 *
 * A snapshot says where the reader *is*, never that they have just arrived, so something has to
 * remember the previous frame - and what counts as "further in" is the application's question,
 * not this library's. The app answers it and calls inkcell_fb_transition_begin(); the slide
 * itself belongs here, because how a screen travels is a property of the look.
 */
enum inkcell_transition {
    INKCELL_TRANSITION_NONE = 0, /* the same place: nothing moved */
    INKCELL_TRANSITION_FORWARD,  /* further in, or rightwards along the tabs */
    INKCELL_TRANSITION_BACK,     /* back out, or leftwards along the tabs */
};

/*
 * The application behind the frame: what to draw, and whether it is owed another one.
 *
 * The backend owns the panel - the pages, the flip, the damage, the clock - and knows nothing
 * about what is on it. `render` is the one call upward, handed the state to draw with and the
 * app's own snapshot pointer, which this library never looks inside.
 *
 * `pending` is the other half, and it exists because a frame is otherwise a pure function of
 * the snapshot: nothing in a press or a packet says that a picture is still on its way. An app
 * reading a tile, a sprite sheet or a photograph one piece per frame says so here, and the
 * backend keeps waking until it stops - which is how a view fills over twenty frames with input
 * serviced between them rather than in one stall.
 *
 * All four are optional. A backend with no app installed clears the panel and presents it,
 * which is what the capture harness wants before a scene is loaded.
 */
struct inkcell_fb_app {
    void *ctx;
    /* Draws one whole frame. `snapshot` is the app's, passed through untouched. */
    void (*render)(struct inkcell_backend_fb_state *state, const void *snapshot, void *ctx);
    /* Whether the frame just drawn wanted something it did not have. */
    bool (*pending)(void *ctx);
    /* Clears that, once per frame before anything is drawn. */
    void (*frame_begin)(void *ctx);
    /*
     * Drop whatever this app has memoised on the state - `thread_cache`, `render_cache`, and
     * anything else it hung there. Called when a memo can no longer be trusted: a reference
     * render, a geometry change. The app keeps running afterwards, so this frees caches and
     * nothing else.
     */
    void (*drop_caches)(struct inkcell_backend_fb_state *state, void *ctx);
    /*
     * Released when the backend shuts down, for good. Handed the state because what an app hung
     * off it is the app's to free - which is everything drop_caches() frees, and whatever else
     * the app opened.
     */
    void (*close)(struct inkcell_backend_fb_state *state, void *ctx);
};

struct inkcell_fb_damage_rect {
    int x, y, right, bottom;
    bool valid;
};

/*
 * A list gliding from one window to the next: which list, where its window was, how far the
 * content is displaced from where it belongs, and how much of that displacement is left.
 *
 * `from` is in pixels and signed - positive is content pushed *down*, which is what scrolling
 * forward looks like on the frame the window moves. See inkcell_fb_list_glide().
 */
struct inkcell_fb_list_glide {
    uint32_t id;
    uint32_t first;
    int from;
    struct inkcell_anim travel;
};

/*
 * The focus ring's journey: the box it left, the box it is arriving at, and how far along it
 * is. Drawn by inkcell_fb_draw_focus_ring(); see include/inkcell/ui/widgets/focus.h for what
 * travels and why the ring is one thing rather than a property of each widget.
 */
struct inkcell_fb_focus_ring {
    uint32_t id; /* what it is on, or arriving at; INKCELL_FOCUS_NONE for no ring */
    struct inkcell_focus_rect from;
    struct inkcell_focus_rect to;
    int from_radius;
    int to_radius;
    struct inkcell_anim travel;
    /*
     * The box the ring actually painted last frame, padded by what it painted outside it.
     *
     * Kept because erasing a ring is not the ring's own work: what is under it belongs to
     * whatever drew there, and that drawing happens *before* the ring is asked to move. So the
     * box is carried to the next frame and declared as damage at inkcell_fb_app_frame_begin(),
     * where it is in place before anything paints - which is the only point in a frame where
     * saying "these rows changed" can still change what gets painted.
     *
     * `valid` false is a frame with no ring on it to erase.
     */
    struct inkcell_fb_damage_rect drawn;
};

/* A box in pixels. The geometry vocabulary every layer above shares: a component is handed
   one, or measures one, and src/fb/fb_draw.c's primitives take it apart again. */
struct inkcell_fb_rect {
    int x, y, w, h;
};

/*
 * ---- the view stack ----
 *
 * How deep the nesting goes.
 *
 * Four, because four is what the frame can actually stack: a scrolled body, a sheet over it
 * that scrolls too, and a menu over that is three, and the fourth is the slack that keeps a
 * component from having to know how many are already in force. A push past the end is refused
 * rather than dropped silently - see inkcell_fb_view_push().
 */
#define INKCELL_FB_VIEW_DEPTH 4U

/*
 * One entry on the view stack: where content drawn inside it lands, and what cuts it.
 *
 * Both are *absolute* rather than relative to the entry below, so the clip every pixel goes
 * through reads one slot and does no walking. Composing on the way in is the whole trick: a
 * push inside a push adds its offset to the one already in force and intersects its box with
 * the box already in force, so what a pixel is measured against is a single translate and a
 * single rectangle however deep the frame got.
 */
struct inkcell_fb_view {
    int dx, dy;              /* the accumulated translation, panel coordinates */
    int x, y, right, bottom; /* the accumulated clip, panel coordinates */
};

/*
 * No layer. What inkcell_fb_overlay_modal() answers on a frame with nothing modal on it, and
 * the id a layer may not take - for the reason INKCELL_FOCUS_NONE is reserved in the focus map:
 * an id space where "none" is a legal key is an id space where a caller that forgot to set one
 * gets a working layer instead of a diagnostic.
 */
#define INKCELL_OVERLAY_NONE 0U

/*
 * How many layers a frame can hold.
 *
 * Four, and the number is the same one the view stack takes, for the same reason: a scrimmed
 * sheet with a menu over it and a notice over that is three, and the fourth is the slack that
 * keeps a screen from having to count. A fifth is refused rather than evicting one of the four
 * - an overlay quietly dropped is a question nobody can answer - and a screen that wants five
 * modals at once has a screen problem rather than a toolkit one.
 */
#define INKCELL_OVERLAY_SLOTS 4U

/*
 * ---- the overlay stack ----
 *
 * One layer's memory between frames. See include/inkcell/ui/overlay.h, which is where the
 * whole of the reasoning is; what is here is only the shape of it, because the state struct is
 * the one place every frame-lived fact in this backend is kept.
 */
struct inkcell_fb_overlay_slot {
    uint32_t id; /* INKCELL_OVERLAY_NONE is free */
    uint64_t touched_ms;
    /* How far in it is: 0 gone, INKCELL_ANIM_ONE fully arrived. The whole of the lifetime -
       a layer with anything left in here is a layer still on the panel, whatever the app
       thinks. */
    struct inkcell_anim travel;
    /*
     * Where in the frame's draw order it came, and whether it takes the press there. Together
     * they are what inkcell_fb_overlay_modal() answers with: "drawn last is on top", read
     * backwards, which is the one stacking rule an immediate-mode frame can state without a
     * screen keeping a second list in agreement with the first.
     */
    uint32_t order;
    bool modal;
    bool up; /* what it was last aimed at, so an exit can be told from an entrance */
    /*
     * The box it painted last frame, padded by its travel.
     *
     * Carried for the reason the focus ring's is: erasing a layer is not the layer's own work,
     * because what is under it belongs to whatever drew there and that drawing happens before
     * the layer is asked to move. So it is declared as damage at the top of the next frame,
     * where it is in place before anything paints.
     */
    struct inkcell_fb_damage_rect drawn;
};

/*
 * Takes every layer off the frame at once, with no exit.
 *
 * For the change that is not a change of mind: a theme or a scale swap re-measures everything,
 * so the boxes these layers are travelling between describe a geometry that no longer exists -
 * which is inkcell_anim_table_reset()'s reason, one header over. A layer the app still wants is
 * re-adopted on the next frame at its resting place, which is right: the panel it is on has
 * just been redrawn at a different size, and a layer that animated into that would be
 * announcing the theme change rather than itself.
 */
void inkcell_fb_overlay_reset(struct inkcell_backend_fb_state *state);

/*
 * Takes one layer off the frame at once, with no exit, so the next frame sees it arrive from
 * nothing.
 *
 * For the case a re-aim cannot express: a layer whose *content* has been replaced while it was
 * up. A second notice reading the same words as the first is a second arrival, not a text
 * swap, and a layer told to go where it already is does nothing at all - deliberately, because
 * that no-op is what stops every settled widget animating on every frame. The snackbar is the
 * one caller, and it is the caller this exists for.
 *
 * A layer nothing is holding is unaffected, so this is safe to call on an id that is not up.
 */
void inkcell_fb_overlay_drop(struct inkcell_backend_fb_state *state, uint32_t id);

struct inkcell_backend_fb_state {
    /* The body rows the last paged list was laid out in - what the app pages its content by,
       read back through the backend's page_rows() vtable entry. */
    uint32_t page_rows;
    struct inkcell_fb_glyph_cache *glyph_cache;
    /* Two slots an application may memo into - laid-out text, a rendered screen. inkcell
       allocates neither and frees neither; they are here because the state is what a renderer
       is handed, and the app drops them from its close() hook. */
    struct inkcell_fb_thread_cache *thread_cache;
    struct inkcell_fb_render_cache *render_cache;
    /* What draws the frame. Zeroed until inkcell_fb_set_app(). */
    struct inkcell_fb_app app;
    /*
     * Where this frame's pressable things are being collected, or NULL.
     *
     * Pushed in by whoever is drawing (inkcell_fb_set_focus_map()) rather than owned here, on
     * the same terms as the app and the theme: it is rebuilt from nothing every frame, it
     * belongs to the screen that is going to ask it questions, and a toolkit that allocated one
     * would be a toolkit deciding how long a screen's map lives.
     *
     * NULL is the whole of the opt-out, and it is what an application that has not heard of
     * this has. A widget handed no map registers nothing and draws exactly what it drew before.
     */
    struct inkcell_focus_map *focus;
    bool partial_disabled;
    bool clip_active;
    struct inkcell_fb_damage_rect clip;
    struct inkcell_fb_damage_rect animation_damage;
    bool thread_cache_disabled;
    uint8_t *draw_buffer;
    uint8_t *previous_frame;
    bool frame_valid;
    int inkcell_fb_fd;
    uint8_t *inkcell_fb_ptr;
    size_t inkcell_fb_size;
    struct fb_fix_screeninfo fix;
    struct fb_var_screeninfo var;
    uint32_t line_bytes;
    uint32_t bytes_per_pixel;
    bool pan_failed_logged;
    /* What this frame is drawn with: the palette, the metrics and the font. Never NULL once
       inkcell_fb_state_set_theme() has run, and every accessor below falls back to the default
       anyway, so no drawing function guards it. */
    const struct inkcell_theme *theme;
    /* Glyph multiplier for body text; the tab bar and footer use one step smaller. The Brick's
       3.2" panel is 1024 px wide, so 4 gives ~41 columns of legible text. It starts at the
       theme's own and is overridden by <PREFIX>_FB_SCALE. */
    int scale;
    /* Somebody named this multiplier outright - <PREFIX>_FB_SCALE on the device, an explicit
       scale through the capture API - so a theme arriving in a snapshot keeps it rather than
       swapping in that theme's default. */
    bool scale_pinned;
    /*
     * The clock this frame is drawn against, and what is still moving in it.
     *
     * A frame is otherwise a function of the snapshot alone, and a snapshot has no notion of
     * "was": it says a switch is on, never that it has just become on. These two are what a
     * widget that animates asks instead - the time, and where it had got to last frame, keyed
     * by an id the screen supplies. See include/inkcell/ui/anim.h for why that lives here rather
     * than in the store.
     *
     * `now_ms` is set once per frame by whoever is driving: the monotonic clock on the device,
     * a number the scene script names in a capture. Nothing below reads a clock of its own, so
     * a capture renders the same frame every time it is asked for.
     */
    uint64_t now_ms;
    struct inkcell_anim_table anim;
    /*
     * The notice the snackbar is showing.
     *
     * The animation table remembers where the container has slid to; this remembers *what is
     * written on it*, and it is here for the second half of the same reason. A snackbar leaves
     * by sliding out, and by the time it does the store has already forgotten the text - the
     * nav clears an expired toast, which is what makes the next frame happen at all. Without a
     * copy the container would slide out blank, or more likely vanish on the frame it expired
     * and never slide at all.
     *
     * It is also what tells one notice from the next: a toast arriving while another is up is
     * a second arrival, not a text swap, and comparing against this is how the widget knows.
     */
    char snackbar[INKCELL_FB_SNACKBAR_MAX];
    uint64_t
        snackbar_until_ms; /* the deadline that identifies it; see struct inkcell_fb_snackbar */
    /*
     * The place the last frame was drawn for, and how far the current one has slid into view.
     *
     * Which place that was is the app's to remember - what counts as further in is its
     * question - so it reports the move through inkcell_fb_transition_begin() and what is kept
     * here is only the travel.
     *
     * `slide` runs 0 -> ONE as the arriving screen travels the last of the panel's width;
     * `slide_dir` is the side it came from, +1 for the right and -1 for the left, and 0 when
     * nothing is travelling. Not a slot in the animation table because that table is for
     * widgets with nowhere of their own to keep a position, and the frame is not one of those.
     */
    struct inkcell_anim slide;
    int slide_dir;
    /*
     * Where the focus ring is, and where it is going.
     *
     * Kept here rather than in the animation table for `slide`'s reason, one line up: that
     * table is for widgets with nowhere of their own to keep a position, and a screen has one
     * cursor. It also could not be kept there - a slot holds one scalar, and what travels here
     * is a whole rectangle, from a box the cursor has *left* (so the id that would key it is
     * already gone) to the one it is arriving at.
     *
     * `id` of INKCELL_FOCUS_NONE is a frame with no ring on it. See
     * include/inkcell/ui/widgets/focus.h.
     */
    struct inkcell_fb_focus_ring focus_ring;
    /*
     * Where the body list has got to between two windows, while it is gliding from one to the
     * other.
     *
     * One slot rather than a table, for `slide`'s reason and for a second: a frame has one
     * scrolling body - `page_rows` a few lines up has assumed so since before any of this - and
     * what is remembered is a window position, which is a fact about a list rather than about a
     * widget with nowhere to keep one. `id` is what a second list on the same frame would
     * collide on, so it is compared rather than assumed: a list that finds another list's
     * window in the slot takes it over and does not glide on that frame.
     */
    struct inkcell_fb_list_glide list_glide;
    /*
     * The frame's content transform: what inkcell_fb_shift_begin() has moved the body by, and the
     * band it is confined to while it is moved. See inkcell_fb_shift_begin().
     */
    int shift_x;
    int shift_top;
    int shift_bottom;
    bool shift_active;
    /*
     * The view stack: the nested translations and clips pushed by
     * inkcell_fb_view_push(). `views` is how many are in force, and slot `views - 1` is the
     * one every pixel is measured against.
     */
    struct inkcell_fb_view views_stack[INKCELL_FB_VIEW_DEPTH];
    uint32_t views;
    /*
     * The layers over this frame, and the counter that orders them.
     *
     * Kept here rather than in the animation table for the reason `slide`, `focus_ring` and
     * `list_glide` are: that table is for widgets with nowhere of their own to keep a
     * position, and a layer is not a widget - it outlives the app's interest in it, which is
     * the one thing a keyed scalar cannot express. `overlay_order` is reset at the top of
     * each frame and handed out as layers open, so the last one drawn has the highest.
     */
    struct inkcell_fb_overlay_slot overlays[INKCELL_OVERLAY_SLOTS];
    uint32_t overlay_order;
};

/*
 * How wide and how tall the surface this frame is being drawn into is, in pixels.
 *
 * Every layer above this one places its content against these two, and every one of them wants
 * a signed number: a margin subtracted from a width is arithmetic that goes negative on a panel
 * narrower than the margin, and an unsigned width would wrap it into an enormous positive
 * instead of the negative a clip then throws away. So the cast happens once, here, rather than
 * at the ~70 call sites that used to spell it out.
 *
 * They are accessors rather than fields a caller reads because the panel's geometry is the last
 * thing a component should have to know the provenance of. A widget asking how wide the surface
 * is has no business seeing that the answer arrived in an FBIOGET_VSCREENINFO - that is what
 * this backend happens to be, not what a width *is*.
 */
static inline int inkcell_fb_panel_width(const struct inkcell_backend_fb_state *state) {
    return state == NULL ? 0 : (int)state->var.xres;
}

static inline int inkcell_fb_panel_height(const struct inkcell_backend_fb_state *state) {
    return state == NULL ? 0 : (int)state->var.yres;
}

/*
 * The clock for the next frame. Call before drawing it.
 *
 * Time never goes backwards here: a caller that hands over an earlier reading than the last is
 * ignored, because an animation window that starts in the future never finishes and the knob
 * would stick.
 */
void inkcell_fb_state_set_now(struct inkcell_backend_fb_state *state, uint64_t now_ms);

/* Whether anything on the last frame is still moving, and so whether another frame is owed.
   What the controller's repaint timer asks. */
bool inkcell_fb_state_animating(const struct inkcell_backend_fb_state *state);

/*
 * Starts a screen travelling. The app calls this when the reader has moved somewhere new, which
 * is a comparison only it can make - see enum inkcell_transition.
 *
 * From the far end every time, including when a move interrupts one already running: a second
 * press is a second screen arriving, not the first one changing its mind about where it was
 * going. INKCELL_TRANSITION_NONE does nothing, so an app may call this unconditionally.
 */
void inkcell_fb_transition_begin(struct inkcell_backend_fb_state *state,
                                 enum inkcell_transition move);

/*
 * How far the arriving screen still has to travel: a positive offset for one coming in from the
 * right, negative from the left, 0 for a frame that is not moving.
 *
 * Call once per frame, before inkcell_fb_shift_begin().
 */
int inkcell_fb_transition_offset(struct inkcell_backend_fb_state *state);

/*
 * Slides everything drawn until inkcell_fb_shift_end() by `dx` pixels, clipped to rows [top,
 * bottom).
 *
 * The one transform in the drawing layer, and the only thing here that knows content can come
 * from off the panel. It is a single call rather than an offset threaded through the widgets
 * because a component that took one would be a component with a pixel coordinate in it: a
 * screen renderer describes its content and a widget places it against `struct inkcell_fb_layout`,
 * and neither has any business knowing the frame is mid-transition. Every pixel this backend writes
 * goes through inkcell_fb_fill_packed(), so putting it there covers glyphs, icons, emoji, fills and
 * rounded corners at once - and covers anything added later without being told to.
 *
 * The band is what keeps a transition to the part of the frame that changed: the navigation bar
 * and the action bar are the same on both sides of a move, and chrome that slid with the body
 * would be the client claiming the whole application had been replaced. It is stated in rows
 * rather than derived from the layout because the caller is the only thing that knows where the
 * body it is about to draw begins and ends.
 *
 * Not nestable, deliberately: there is one transform per frame and a second would be a second
 * opinion about where the body is.
 */
void inkcell_fb_shift_begin(struct inkcell_backend_fb_state *state, int dx, int top, int bottom);
void inkcell_fb_shift_end(struct inkcell_backend_fb_state *state);

/*
 * ---- the view stack ------------------------------------------------------------------------
 *
 * A box on the panel, and an offset the content inside it is drawn at.
 *
 * inkcell_fb_shift_begin() one comment up is the *frame's* transform and there is deliberately
 * one of them. This is the other kind: a region of the frame that has content of its own, laid
 * out in coordinates that are not the panel's, and cut where the region ends. Two things in this
 * toolkit need one and neither could be built without it.
 *
 *   - **A scrolled body.** A list windowed by row index can only ever be at a row boundary,
 *     which is why the glide had to be a displacement bolted onto a window rather than a
 *     position. Push a view with `dy` of minus the scroll offset and the body is simply drawn
 *     at its own coordinates - the whole content, from the first item to the last - and what
 *     lands on the panel is the part the offset put there. A half-row at the top edge is then
 *     not a special case; it is what a pixel offset that is not a multiple of a line looks
 *     like.
 *   - **An overlay.** A menu, a sheet or a dialog is a panel that arrives, and what stops it
 *     painting over the chrome while it is half way in is a clip - not a check inside every
 *     widget it happens to contain. See include/inkcell/ui/overlay.h.
 *
 * Three properties, and each of them is why this is a stack rather than a second `shift`:
 *
 *   - **It nests.** A sheet that scrolls is a view inside a view, and the offsets add while the
 *     boxes intersect. A component inside one does not know how many are in force.
 *   - **It is composed on the way in**, so the clip every pixel goes through reads one entry.
 *     See struct inkcell_fb_view.
 *   - **The focus map goes through it too**, which the frame's own transform deliberately does
 *     not. That is not an inconsistency: a slide moves the whole frame by one dx and so changes
 *     no answer the map is asked for, while a view can move part of it *out of sight*. A row
 *     scrolled past the top of its viewport is not a place to stand, and a map that held it
 *     would let the cursor walk onto something nobody can see. So a box registered inside a
 *     view is registered where the view put it, and one the view cut away entirely is not
 *     registered at all. That is the same rule the list has always followed by only registering
 *     the rows it drew - stated once, for everything.
 *
 * Returns false when the box has nothing on the panel in it, or when the stack is full, and
 * *nothing is pushed* in either case - so the pop is conditional on the push, exactly as it
 * reads:
 *
 *     if (inkcell_fb_view_push(state, box, 0, -offset)) {
 *         ... draw the content at its own coordinates ...
 *         inkcell_fb_view_pop(state);
 *     }
 *
 * A false is a region with nothing visible in it, so the draw inside it had nothing to
 * contribute anyway. The one thing a caller must not do is pop a push that was refused, which
 * is what the `if` is for.
 */
bool inkcell_fb_view_push(struct inkcell_backend_fb_state *state, struct inkcell_fb_rect box,
                          int dx, int dy);
void inkcell_fb_view_pop(struct inkcell_backend_fb_state *state);

/*
 * The box the innermost view is cutting to, in panel coordinates, or the whole panel when none
 * is pushed.
 *
 * What a component inside a view asks when it has to know whether it is worth drawing something
 * - a list skipping the rows above the viewport rather than clipping several hundred of them
 * one pixel at a time. It is the clip, not the content: a view translated by an offset reports
 * where its window is on the panel, not where its content thinks it is.
 */
struct inkcell_fb_rect inkcell_fb_view_box(const struct inkcell_backend_fb_state *state);

/*
 * The same box in the *content's* coordinates: what a caller inside a view has to draw to fill
 * it, with the translation taken back off.
 *
 * A list walking its items needs this rather than the panel box. It places row four hundred at
 * its own y, and what it wants to know is which of its rows land in the window - a question
 * asked in the coordinates the rows are in.
 */
struct inkcell_fb_rect inkcell_fb_view_content_box(const struct inkcell_backend_fb_state *state);

/*
 * Adopts the theme named by `id`, when it is one this build knows and is not already drawing.
 * Returns true when the frame's look changed. An empty or NULL id keeps the current theme, which
 * is what a harness with no app behind it wants.
 *
 * This is how a theme switch reaches the panel: the app reads the reader's choice out of
 * whatever it keeps settings in and pushes the id here on the next frame. The new theme brings
 * its own glyph scale unless the environment pinned one.
 */
bool inkcell_fb_state_set_theme_by_id(struct inkcell_backend_fb_state *state, const char *id);

/* Sets the theme and takes the scale from it. Pass 0 for `scale` to accept the theme's. */
void inkcell_fb_state_set_theme(struct inkcell_backend_fb_state *state,
                                const struct inkcell_theme *theme, int scale);

/* ---- the theme, as the drawing layers ask for it ------------------------------------------ */

/* A colour by role. This is the only way a colour enters the framebuffer layers. */
struct inkcell_rgb inkcell_fb_color(const struct inkcell_backend_fb_state *state,
                                    enum inkcell_color role);
/* A colour by what the content means. What screens use; see enum inkcell_tone. */
struct inkcell_rgb inkcell_fb_tone_color(const struct inkcell_backend_fb_state *state,
                                         enum inkcell_tone tone);
/*
 * A fill and the ink that goes on it, for one family, one slot and one interaction state.
 *
 * This is what a widget that *fills* something asks for, and inkcell_fb_color() is what one that
 * only writes ink asks for. The difference matters: a fill and its label are a pair the theme was
 * validated as a pair, and every component that picked them up separately - the button, the
 * switch, the chat bubble - is a component that could be handed a combination nothing checked.
 */
struct inkcell_paint inkcell_fb_paint(const struct inkcell_backend_fb_state *state,
                                      enum inkcell_family family, enum inkcell_slot slot,
                                      enum inkcell_state ui_state);
/* A stated fill with a state layer over it, for the neutral surfaces - which have no family to
   ask, but are still drawn under a cursor. `ink` is what the layer mixes in. */
struct inkcell_rgb inkcell_fb_state_layer(const struct inkcell_backend_fb_state *state,
                                          enum inkcell_color fill, enum inkcell_color ink,
                                          enum inkcell_state ui_state);

/*
 * `ink` mixed `progress` of the way towards `ground`, in permille: a cross-fade, on a panel
 * that has no alpha to do one with.
 *
 * Text here is drawn as *coverage* blended against a stated ground, so ink moved towards that
 * ground is a partly faded glyph - drawn by the same blend that anti-aliases its edges, which
 * is why nothing about this is a trick. INKCELL_ANIM_ONE is gone entirely and 0 is untouched.
 *
 * It is a lookup rather than a widget's own arithmetic for the reason the colour roles are:
 * the collapsing app bar cross-fades its two titles, a FAB cross-fades the label it is
 * collapsing away from, and two components mixing their own channels are two components that
 * will one day round a fade differently. `ground` is what is actually behind the glyph - a fade
 * towards a colour that is not what is there is a glyph with a halo.
 */
struct inkcell_rgb inkcell_fb_fade(struct inkcell_rgb ink, struct inkcell_rgb ground,
                                   int32_t progress);
/* Pixels between the panel edge and the body. */
int inkcell_fb_margin(const struct inkcell_backend_fb_state *state);

/* How far towards INKCELL_COLOR_SCRIM a modal takes the frame behind it, as a percentage: the
   theme's metrics.scrim_pct. What inkcell_fb_scrim_rect() is handed by a layer easing one in. */
int inkcell_fb_scrim_depth(const struct inkcell_backend_fb_state *state);
/* The corner radius for a kind of container, at the frame's own glyph scale. The only way a
   radius enters the framebuffer layers, for the reason inkcell_fb_color() is the only way a colour
   does; see enum inkcell_shape. */
int inkcell_fb_radius(const struct inkcell_backend_fb_state *state, enum inkcell_shape shape);

/* The gap `space` asks for, in pixels, at the body scale. */
int inkcell_fb_space(const struct inkcell_backend_fb_state *state, enum inkcell_space space);

/* The same gap at an explicit glyph multiplier, for the widgets that are drawn at one that is
   not the body's - a rule under the tab strip, a switch on a chrome-scale row. A gap beside
   smaller text has to be smaller too, or the scale stops being a scale. */
int inkcell_fb_space_at(const struct inkcell_backend_fb_state *state, enum inkcell_space space,
                        int scale);

/* The glyph multiplier `type` is drawn at, given this state's body scale. */
int inkcell_fb_type_scale(const struct inkcell_backend_fb_state *state, enum inkcell_type type);
/* The weight that role is set in, the theme's answer. A widget names the role; it never picks
   a weight. */
enum inkcell_weight inkcell_fb_type_weight(const struct inkcell_backend_fb_state *state,
                                           enum inkcell_type type);

/*
 * The half-margin: the inset a panel sits in, and the row's own padding either side of it.
 *
 * Not part of the spacing scale, deliberately. The spacing scale is glyph-relative - it is the
 * room around *text* - and this tracks the body margin instead, because what it measures is how
 * far a panel is from the edge of the screen. A theme that asks for bigger text wants roomier
 * padding and the same inset; one that asks for a roomier margin wants the opposite.
 */
int inkcell_fb_gutter(const struct inkcell_backend_fb_state *state);

/* How long `motion` lasts on this state's theme, in milliseconds. The duration half of an
   animation; the curve is still named at the call site. */
uint32_t inkcell_fb_motion(const struct inkcell_backend_fb_state *state,
                           enum inkcell_motion motion);
/* The hairline thickness an edge is drawn at - a card's, a field's. One place, because an
   outline is two fills and both have to agree about how thick it is. */
int inkcell_fb_edge(const struct inkcell_backend_fb_state *state);

/*
 * The strip kept clear at a list's trailing edge for its scroll rail.
 *
 * A gutter rather than an overlay, and reserved on every list rather than on the ones that
 * happen to scroll. Both halves are the same decision: a rail drawn over the content it
 * measures sits on top of whatever is widest there - which on a column of cards is the card -
 * and a gutter taken only when a list outgrows its window is a layout that reflows the moment a
 * node reports one more reading. So the room is spent whether or not the rail is drawn, and
 * nothing a list puts down ever reaches into it.
 *
 * The half-margin, for inkcell_fb_gutter()'s reason: what it measures is clearance from the panel
 * edge, not room around text, so it tracks the body margin rather than the glyph scale.
 */
int inkcell_fb_rail_gutter(const struct inkcell_backend_fb_state *state);

/*
 * Where a list's rows stand, horizontally. The one answer, asked by everything that draws one.
 *
 * This was three separate derivations of the same rectangle - the cursor's highlight in
 * inkcell_fb_draw_row_fill_on(), the list item's own copy of it, and the card surfaces a grouped
 * list paints under its rows - plus a fourth opinion in the scroll rail about how much room was
 * left over beside them. They agreed until the cards started spending their hairline outward into
 * the gutter, at which point the rail was flush against the card edge on the one screen that is a
 * column of cards: the node detail, where it read as part of the card rather than as a control
 * beside it.
 *
 * So the box is stated once and derived from nowhere else. `x`/`w` is the row fill - the
 * cursor's highlight, and a card's interior - and the text span is that inset by the row's own
 * padding. A card is this rectangle with its hairline spent outward (inkcell_fb_list_cards()),
 * which makes the card the widest thing a list draws and therefore what the rail has to clear.
 */
struct inkcell_fb_row_box {
    int x, w;               /* the row fill */
    int text_x, text_right; /* where a row's content starts, and where it stops */
};

struct inkcell_fb_row_box inkcell_fb_row_box(const struct inkcell_backend_fb_state *state);

/* Columns of a list row's text at `scale`: the box above, measured in cells. inkcell_fb_cols() is
   the panel's answer and is what a dialog or a wrapped empty state wants; a row inside a list has
   the rail's gutter taken off it, and measuring one with the other is how a value column comes
   to sit a cell wider than the row it is drawn in. */
size_t inkcell_fb_row_cols(const struct inkcell_backend_fb_state *state, int scale);

const struct inkcell_metrics *inkcell_fb_metrics(const struct inkcell_backend_fb_state *state);
const struct inkcell_font *inkcell_fb_font(const struct inkcell_backend_fb_state *state);

/* Where the chrome ends and the body begins. Opened by inkcell_fb_layout_begin(), and moved
   down by each piece of chrome as it takes its room - see include/inkcell/ui/widgets/chrome.h. */
struct inkcell_fb_layout {
    int body_y;    /* first body row */
    int footer_y;  /* top of the two footer lines */
    int line;      /* body line advance */
    uint32_t rows; /* body rows available */
    size_t cols;   /* body columns, at the nominal advance - an estimate on a proportional face */
    /*
     * The body's width in pixels, which is what anything wrapping or fitting real text measures
     * against.
     *
     * `cols` is a character count and was the same fact while every face was monospace. It is
     * not one now: a line of `cols` characters set in a proportional face is as wide as those
     * particular characters happen to be, so a screen that wrapped to `cols` and drew the
     * result would run off the panel for some strings and stop short for others. Wrap against
     * this and the two agree again.
     */
    int body_w;
    /* The glyph multiplier chrome is drawn at: INKCELL_TYPE_LABEL, resolved once in
       inkcell_fb_layout_begin() and carried here so every piece of chrome in the frame agrees.
     */
    int small;
    /*
     * The first pixel below the navigation bar, its closing rule included.
     *
     * Not the same number as `body_y`, and that is the point: between the two there is the gap
     * the bar leaves before the body starts, and the screen progress bar hangs in it. A caller
     * that derived it from `body_y` would be subtracting a gap the navigation bar chose, which
     * is the navigation bar's arithmetic written out a second time - and `body_y` has moved on
     * by then anyway, because the app bar and the banner both advance it.
     */
    int nav_y;
    /*
     * Whether there is a screen behind this one to go back to - the top app bar's leading slot.
     * Read off the action bar by the application; see inkcell/ui/actions.h for why the question
     * is not one inkcell can answer.
     *
     * Here rather than on `struct inkcell_fb_app_bar` because a screen renderer is the wrong place
     * to be asked: it is a fact about the nav, the tables in src/ui/tables/actions.c already decide
     * it for the action bar at the bottom, and the two pieces of chrome disagreeing about whether
     * B leaves is exactly the drift a second opinion would introduce. It is asked once, at
     * inkcell_fb_layout_begin(), and both bars read the same answer.
     */
    bool back;
};

/* Copies changed row spans from ordinary RAM into page 0 and its display mirror.
   Returns bytes written across both pages; force initializes pages owned by the launcher. */
size_t inkcell_fb_copy_damage(struct inkcell_backend_fb_state *state, const uint8_t *frame,
                              uint8_t *previous, bool force);

void inkcell_fb_animation_damage(struct inkcell_backend_fb_state *state, int x, int y, int w,
                                 int h);

void inkcell_fb_glyph_cache_free(struct inkcell_backend_fb_state *state);

/* ---- src/fb/fb_draw.c: the drawing toolkit
 * ------------------------------------------------------ */

/*
 * Glyph metrics for a multiplier, from the theme's font.
 *
 * `char_adv` is the *nominal* advance - the width a column of a grid is worth, and the exact
 * advance only while the theme's face is monospace. `cell_adv` is what one character actually
 * steps, and `text_width` is what a whole string actually draws, which is the one to measure a
 * coordinate with. See the note on inkcell_font's `width`.
 */
int inkcell_fb_char_adv(const struct inkcell_backend_fb_state *state, int scale);
int inkcell_fb_cell_adv(const struct inkcell_backend_fb_state *state, uint32_t codepoint,
                        int scale);
int inkcell_fb_text_width(const struct inkcell_backend_fb_state *state, const char *text,
                          int scale);
/* The same at a weight, because a heavier cut sets a little wider and a heading measured in the
   regular one would be a heading its own box clips. */
int inkcell_fb_text_width_weight(const struct inkcell_backend_fb_state *state, const char *text,
                                 int scale, enum inkcell_weight weight);
/* The same width, rounded up to whole nominal cells - for the layouts that still reserve
   space in columns. See the note on the implementation. */
size_t inkcell_fb_text_cols(const struct inkcell_backend_fb_state *state, const char *text,
                            int scale);

/*
 * A wrap metric over this state's font, so a wrap budget can be stated in pixels.
 *
 * The context is the caller's and must outlive the walk - it is two words, so it goes beside
 * the `struct inkcell_wrap` on the stack:
 *
 *     struct inkcell_fb_wrap_ctx wctx;
 *     const struct inkcell_wrap_metric metric = inkcell_fb_wrap_metric(&wctx, state, scale);
 *     inkcell_wrap_begin_measured(&wrap, text, width_in_pixels, &metric);
 */
struct inkcell_fb_wrap_ctx {
    const struct inkcell_backend_fb_state *state;
    int scale;
};
struct inkcell_wrap_metric inkcell_fb_wrap_metric(struct inkcell_fb_wrap_ctx *ctx,
                                                  const struct inkcell_backend_fb_state *state,
                                                  int scale);
int inkcell_fb_line_adv(const struct inkcell_backend_fb_state *state, int scale);
void inkcell_fb_clear(const struct inkcell_backend_fb_state *state, struct inkcell_rgb color);

/*
 * Mixes everything already drawn inside `box` `percent` of the way towards `color`.
 *
 * The scrim, and the one drawing call here that *reads* the panel over a whole region rather
 * than writing to it. Every other primitive composes a colour and puts it down; this one takes
 * what is there and moves it, which is the only way to dim a frame on a panel with no alpha
 * layer to lay over it. See INKCELL_COLOR_SCRIM.
 *
 * What it buys is what a scrim is for: the body under a modal stays *itself*, dimmed - the
 * columns and the shapes are still there and the reader can still see what the question is
 * about. A flat fill over the same region would be a second screen, and a modal that replaced
 * the screen it was asked about is a modal answering a different question by the time it is
 * read. That is the compromise the dialog has been making since it was written, and it is why
 * it fills the body rather than floating over it.
 *
 * `percent` of 0 draws nothing at all, so a scrim easing in from nothing costs nothing on the
 * frame before it starts. 100 is a flat fill and is reached by arithmetic rather than by a
 * special case.
 *
 * It goes through the view stack and the clip like everything else, so a scrim inside a view
 * dims that view's window and nothing outside it.
 */
void inkcell_fb_scrim_rect(const struct inkcell_backend_fb_state *state, struct inkcell_fb_rect box,
                           struct inkcell_rgb color, int percent);
size_t inkcell_fb_cols(const struct inkcell_backend_fb_state *state, int scale);
/*
 * Text and one glyph of it, in `ink` over `ground`.
 *
 * `ground` is the colour the caller has just filled behind the text, and it is a parameter for
 * the same reason inkcell_fb_draw_icon()'s is: a glyph carries coverage, not a mask, and blending
 * its edges needs to know what they are blending into. What is already on the panel is not readable
 * from here, and a caller that has just filled a row is the only thing that knows what colour it
 * filled it with. Getting it wrong does not lose the text - it puts a faint halo of the wrong
 * colour around it.
 */
void inkcell_fb_draw_glyph(const struct inkcell_backend_fb_state *state, int x, int y,
                           uint32_t codepoint, int scale, struct inkcell_rgb ink,
                           struct inkcell_rgb ground);
/* A whole row from the body margin, drawing its own cursor fill - so it knows its own ground
   and does not take one. */
/*
 * The fill a selected row lays down, and the ground everything on that row is then drawn
 * against - the background colour when the row is not the cursor's.
 *
 * Shared rather than written out per row shape because a list mixes them: a plain row and a
 * section heading in the same list highlighting to two slightly different rectangles is a
 * cursor that changes shape as it walks, which is exactly what a duplicated `y - scale` and a
 * duplicated height produced. `rows` is how many body rows the row occupies.
 *
 * `ground` is what the row is standing on when it is *not* selected, which is the background on
 * every list that draws straight onto the panel and a card's surface on one whose groups are
 * drawn as cards. It is asked for rather than assumed because a glyph carries coverage rather
 * than a mask: text blended against the wrong ground keeps its shape and gains a faint halo of
 * the colour it was told about, which is the same fact inkcell_fb_draw_glyph() is documented on.
 */
struct inkcell_rgb inkcell_fb_draw_row_fill_on(const struct inkcell_backend_fb_state *state, int y,
                                               uint32_t rows, bool selected,
                                               enum inkcell_color ground);

/* The same on the panel's own background, which is every list that is not a column of cards. */
struct inkcell_rgb inkcell_fb_draw_row_fill(const struct inkcell_backend_fb_state *state, int y,
                                            uint32_t rows, bool selected);

void inkcell_fb_draw_row(const struct inkcell_backend_fb_state *state, int y, const char *text,
                         struct inkcell_rgb color, bool selected);
void inkcell_fb_draw_text(const struct inkcell_backend_fb_state *state, int x, int y,
                          const char *text, int scale, struct inkcell_rgb ink,
                          struct inkcell_rgb ground);
/* The same at a weight. The plain form is INKCELL_WEIGHT_REGULAR, which is what body text and
   anything that has not asked for emphasis is set in. */
void inkcell_fb_draw_text_weight(const struct inkcell_backend_fb_state *state, int x, int y,
                                 const char *text, int scale, enum inkcell_weight weight,
                                 struct inkcell_rgb ink, struct inkcell_rgb ground);
/* The box an icon is drawn in: one text cell, so a row that puts one in front of its words is
   still measured in columns like every other row. */
int inkcell_fb_icon_box(const struct inkcell_backend_fb_state *state, int scale);
/* What an icon is actually *drawn* at, which is a little wider than the cell it occupies - a
   symbol has to stand as tall as the capitals beside it, and the advance is narrower than the
   glyph body is tall. Beside inkcell_fb_icon_box() because it answers the other half of "how big is
   an icon": the cell is what the column arithmetic counts, this is what a component fitting one
   inside a box of its own - a checkbox's tick - has to measure against. */
int inkcell_fb_icon_drawn(const struct inkcell_backend_fb_state *state, int scale);
/* The largest multiplier inkcell_fb_draw_icon() will draw at: everything in a row is drawn at the
   text's scale, and the empty state's symbol is the one thing bigger than that. */
#define INKCELL_FB_ICON_SCALE_MAX (INKCELL_SCALE_MAX * 3)

/*
 * One icon, in `ink`, blended against `ground` - the colour the caller has just filled behind
 * it. Placed like a glyph: `x` is the cell's left edge and `y` the text baseline it lines up
 * with. INKCELL_ICON_NONE draws nothing, so a slot that is empty needs no test.
 */
void inkcell_fb_draw_icon(const struct inkcell_backend_fb_state *state, int x, int y,
                          enum inkcell_icon icon, int scale, struct inkcell_rgb ink,
                          struct inkcell_rgb ground);

/*
 * The largest square inkcell_fb_draw_emoji_box() will draw a sprite in.
 *
 * A sprite inside a line of text is one cell and is bounded by the glyph scale; a keycap on
 * the keyboard's emoji layer is sized to the *key* instead, which on the Brick's panel is four
 * times that and on a wider one more. The bound is what the per-column map inside is sized
 * for, and it is generous rather than tight - the cost of the slack is a few hundred bytes of
 * stack for the length of one call.
 */
#define INKCELL_FB_EMOJI_BOX_MAX 192

/*
 * One emoji sprite, filling a square `box` pixels on a side with its top-left at (x, top).
 *
 * The primitive under the emoji cells inkcell_fb_draw_text() lays into a line, exposed because a
 * keycap is the one place a sprite is sized to the box it sits in rather than to the text
 * around it. An emoji takes no ink and no ground: it carries its own colours and draws only
 * its opaque pixels, so whatever the caller has already filled shows through the margin.
 */
void inkcell_fb_draw_emoji_box(const struct inkcell_backend_fb_state *state, int x, int top,
                               int box, uint16_t sprite);

/* The box to ask inkcell_fb_draw_emoji_box() for when there is `box` pixels of room: the whole
   multiple of the sprite's own grid that fits, so every source pixel lands on a square of the same
   size rather than on a mix of two. Returns small boxes - a text cell - unchanged. */
int inkcell_fb_emoji_box_fit(int box);
/*
 * Wrapped body text from the leading margin, and from an explicit left edge.
 *
 * `width` is pixels, not columns - see `body_w` on struct inkcell_fb_layout. Both walk
 * inkcell_wrap() with the font's own advances, so what they measure is what they draw; there is
 * no second wrapper in this file any more.
 */
int inkcell_fb_draw_wrapped(const struct inkcell_backend_fb_state *state, int y, const char *text,
                            size_t width, int max_lines, struct inkcell_rgb color,
                            struct inkcell_rgb ground);
/*
 * The same, with every line centred in `width` about the leading margin.
 *
 * For the one shape that is centred rather than set: a symbol with a sentence under it. An
 * empty state centres its icon on the panel, and a caption left-aligned under a centred symbol
 * reads as a mistake rather than as a choice.
 *
 * Each line is measured and placed on its own, not the block on its widest line. A ragged
 * paragraph centred as a block is a block with one straight edge, and on a proportional face
 * two lines of the same character count are two different widths anyway.
 */
int inkcell_fb_draw_wrapped_centered(const struct inkcell_backend_fb_state *state, int y,
                                     const char *text, size_t width, int max_lines,
                                     struct inkcell_rgb color, struct inkcell_rgb ground);
int inkcell_fb_draw_wrapped_at(const struct inkcell_backend_fb_state *state, int x, int y,
                               const char *text, size_t width, int max_lines,
                               struct inkcell_rgb color, struct inkcell_rgb ground);

/* How many lines `text` wraps to inside `width` pixels, at `scale`. */
uint32_t inkcell_fb_wrapped_lines(const struct inkcell_backend_fb_state *state, const char *text,
                                  size_t width, int scale);
void inkcell_fb_fill_rect(const struct inkcell_backend_fb_state *state, int x, int y, int w, int h,
                          struct inkcell_rgb color);
/*
 * A rectangle of BGRA pixels - what an image decoder produces - drawn at `x`, `y`.
 *
 * `stride` is the source's row length in bytes, so a caller may hand over part of a larger
 * image. Clipped by the same rules every fill in this backend is, which is what puts a map tile
 * inside the map's own body rather than over the app bar above it.
 */
void inkcell_fb_blit_bgra(const struct inkcell_backend_fb_state *state, int x, int y, int w, int h,
                          const uint8_t *pixels, size_t stride);
/*
 * The same box with its corners taken off, `radius` pixels each - the shape an avatar disc, a
 * count pill and a selected row are. A radius of half the shorter side is a circle (or a
 * capsule); anything larger is clamped to that, so a caller can ask for "as round as it goes"
 * without measuring first.
 *
 * The edge is anti-aliased, which it did not used to be. The old note here said blending would
 * need a background this function cannot see; it can see it, because this backend draws into
 * ordinary RAM and copies the page to the panel afterwards, so the ground is simply the pixel
 * about to be written over. Only edge pixels are read back and blended - the interior is one
 * bulk fill - so what the curve costs is its boundary rather than its area.
 */
void inkcell_fb_fill_round_rect(const struct inkcell_backend_fb_state *state, int x, int y, int w,
                                int h, int radius, struct inkcell_rgb color);

/*
 * The same, with either end left square.
 *
 * It is here for the one shape that is honestly open at an end: a card in a scrolling list, cut
 * by the window rather than finished. A rounded corner halfway down a scroll is a card claiming
 * to end where the panel merely stopped, and a reader has no way to tell that from a card that
 * really did end - so the cut end keeps square corners and reads as continuing.
 *
 * A square end is not a patch over a rounded one: the corner band it would have spent is given
 * back to the straight middle, so the two are one fill and there is no seam where they met.
 * Both ends square is inkcell_fb_fill_rect(), which is what it calls.
 */
void inkcell_fb_fill_round_rect_ends(const struct inkcell_backend_fb_state *state, int x, int y,
                                     int w, int h, int radius, struct inkcell_rgb color,
                                     bool round_top, bool round_bottom);
/*
 * The same shape as an outline of `thickness` pixels, drawn as one ring.
 *
 * What an outline used to be here was the larger shape filled and the smaller one laid over it,
 * which needs to know what is behind the hole and steps twice - once on each edge. A ring is
 * the difference between the two coverages, and because the inner shape lies wholly inside the
 * outer one that difference is exact rather than approximate: every sub-sample the inner claims
 * is one the outer claims too.
 *
 * This is what a focus ring, an outlined card, a checkbox and a segmented divider all want, and
 * it is the one of them that stays a hairline at any radius.
 */
void inkcell_fb_stroke_round_rect(const struct inkcell_backend_fb_state *state, int x, int y, int w,
                                  int h, int radius, int thickness, struct inkcell_rgb color);

/*
 * Part of a ring: a band `thickness` wide at `radius` from (`cx`, `cy`), from `start` and
 * running `sweep`.
 *
 * Both are in permille of a whole circle, zero is twelve o'clock and a sweep runs clockwise -
 * the way a clock face and every progress ring on every phone run. A sweep of 1000 or more is
 * the whole ring and skips the angular test altogether.
 *
 * The one thing this backend could not draw at all: a dial, a ring gauge, and the circular
 * progress indicator that is the counterpart of inkcell_fb_draw_meter()'s bar. The angles come
 * off a committed sine table rather than libm, so a picture of an arc is the same picture on
 * every host that renders it.
 */
void inkcell_fb_stroke_arc(const struct inkcell_backend_fb_state *state, int cx, int cy, int radius,
                           int thickness, int32_t start, int32_t sweep, struct inkcell_rgb color);

void inkcell_fb_fit(char *line, size_t cols);
void inkcell_fb_format_age(uint32_t last_heard, char *out, size_t out_len);
void inkcell_fb_format_clock(uint32_t rx_time, char *out, size_t out_len);
size_t inkcell_fb_width(const char *line);

/* ---- the application behind the frame ---------------------------------------------------- */

/* Installs what draws the frame. Pass NULL to remove one; the outgoing app's close() runs. */
void inkcell_fb_set_app(struct inkcell_backend_fb_state *state, const struct inkcell_fb_app *app);

/* ---- focus ------------------------------------------------------------------------------- */

/*
 * Collect this frame's pressable rectangles into `map`, or NULL to collect none.
 *
 * The other half of include/inkcell/ui/focus.h. That header says a frame registers the box of
 * everything focusable *as it draws it*, and left it to the screen to make the calls - which
 * works, and asks every screen to know where a widget put things the widget laid out itself. A
 * card drops the verb it had no room for; a chip strip elides the tab that did not fit; a list
 * clips the row that fell past the body. Only the widget knows which of those happened, so only
 * the widget can honestly say what is on the frame.
 *
 * So the widgets say it. Set a map here, give the pressable things ids (`focus_id` on a button
 * or a chip, `action_focus_id` on a card or a dialog, inkcell_fb_list_focus() on a list), and
 * what was drawn and what the cursor can reach are one list rather than two that have to agree.
 *
 * The map is borrowed until the next call, so it must outlive the frame. Open it with
 * inkcell_focus_begin() at the top of the render and read it after - anything registered against
 * the *previous* layout is a box that is no longer on the panel.
 */
void inkcell_fb_set_focus_map(struct inkcell_backend_fb_state *state,
                              struct inkcell_focus_map *map);

/*
 * Registers `rect` under `id` in whatever map is set, and does nothing when none is or when the
 * id is INKCELL_FOCUS_NONE.
 *
 * What the widgets below call, and what a screen drawing something of its own calls so that it
 * goes in by the same path rather than by reaching for the map it happens to hold. The state is
 * what a renderer is handed; this keeps that true for the one fact about a frame that is not a
 * pixel.
 */
void inkcell_fb_focus_register(const struct inkcell_backend_fb_state *state, uint32_t id,
                               const struct inkcell_fb_rect *rect);

/*
 * The same, for a box drawn with corners: `shape` is the one it was filled with.
 *
 * What the components call, because a component has a shape token in its hand at the moment it
 * draws - a chip is a pill, a key is a rounded square - and the ring that lands on the box
 * later has no way to find that out again. INKCELL_SHAPE_FULL is resolved here to the radius a
 * fill would actually produce, so what is recorded is the curve on the panel rather than the
 * request that made it.
 */
void inkcell_fb_focus_register_shaped(const struct inkcell_backend_fb_state *state, uint32_t id,
                                      const struct inkcell_fb_rect *rect, enum inkcell_shape shape);

/* Whether the app says it is owed another frame - what inkcell_fb_state_animating() adds to the
   animations when it decides whether one is due. */
bool inkcell_fb_app_pending(const struct inkcell_backend_fb_state *state);

/* Asks the app to drop whatever it has memoised on this state - what a geometry or mode change
   invalidates. A no-op when nothing installed an app, or when it keeps no caches. */
void inkcell_fb_app_drop_caches(struct inkcell_backend_fb_state *state);

/* Clears that, before anything is drawn. Called once per frame by inkcell_fb_render(). */
void inkcell_fb_app_frame_begin(struct inkcell_backend_fb_state *state);

/* Draws one whole frame, by handing `snapshot` to the installed app. A state with no app
   installed draws nothing, which leaves the cleared panel the present() is about to show. */
void inkcell_fb_render(struct inkcell_backend_fb_state *state, const void *snapshot);

#endif /* INKCELL_UI_FB_DRAW_H */
