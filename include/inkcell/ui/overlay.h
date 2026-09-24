#ifndef INKCELL_UI_OVERLAY_H
#define INKCELL_UI_OVERLAY_H

/*
 * The layer: a panel that arrives over the frame, stays while it is wanted, and leaves.
 *
 * ---- what this replaces ----
 *
 * There were four things drawn over a screen and no two of them agreed about anything.
 *
 * The dialog filled the body and called it a modal - it is a screen with a panel on it, not a
 * thing over a screen, and it says so in its own header: "it fills the body rather than
 * floating over it, and that is deliberate rather than a shortcut", because there was no scrim
 * to draw. The snackbar animated, remembered its own words on the state so it could finish
 * leaving after the store had forgotten them, and keyed a constant at the far end of the
 * animation table to do it. The banner consumed body rows and did not animate at all. The QR
 * code was handed a box by whoever drew it and had no notion of arriving or leaving.
 *
 * Four shapes, four answers to "where does this go", three answers to "how does it get there",
 * one answer to "what is under it" and none at all to "who owns the press while it is up". And
 * because each of them was its own thing rather than an instance of something, the shapes that
 * were *missing* had nowhere to be written: a menu, a bottom sheet, a tooltip, a popover are
 * each three lines of content and all of the machinery above, and every one of them would have
 * had to bring its own copy.
 *
 * This is the machinery, once. A layer is:
 *
 *   - **a box**, derived from a placement and a size rather than from a y coordinate a screen
 *     worked out;
 *   - **a way in and a way out**, so the two are one decision and the exit is shorter than the
 *     entrance the way every platform makes it;
 *   - **a scrim, or not**, drawn by the layer rather than by what is in it;
 *   - **a lifetime that outlives the app's interest in it**, which is the hard part - see
 *     below;
 *   - **a place in the stack**, so a menu over a sheet over a scrimmed body is three calls
 *     rather than a question;
 *   - **an answer to who owns the press**, which is the one thing none of the four had.
 *
 * What a layer is *not* is what is in it. inkcell_fb_draw_dialog(), the menu, the sheet and the
 * snackbar are content drawn into the box a layer hands back, and a screen with a shape none of
 * them covers draws its own. That split is the whole point: the next overlay this toolkit grows
 * should be its content and nothing else.
 *
 * ---- the lifetime, which is the hard part ----
 *
 * A frame here is a function of a snapshot, and a snapshot says what is true *now*. An overlay
 * that is leaving is not true now: the app closed the menu, the nav cleared the expired toast,
 * and the thing is still on the panel for another two hundred milliseconds.
 *
 * The snackbar solved this by copying its text onto the state, which works exactly once,
 * because it works for a widget whose entire content is one short string. Nothing can copy a
 * menu.
 *
 * So the layer remembers the *travel* and the app keeps describing the content until the travel
 * has finished. That is what inkcell_fb_overlay_begin() returning true means - not "the app
 * wants this up", but "there is still some of it on the panel" - and it is why the call is
 * written as the condition of the `if` rather than inside one:
 *
 *     struct inkcell_overlay_frame frame;
 *     if (inkcell_fb_overlay_begin(state, &(struct inkcell_overlay){
 *             .id = SCREEN_MENU,
 *             .up = snapshot->menu_open,
 *             .placement = INKCELL_OVERLAY_ANCHOR,
 *             .anchor = button_box,
 *             .w = menu_w, .h = menu_h,
 *             .scrim = true,
 *             .modal = true,
 *         }, &frame)) {
 *         inkcell_fb_draw_menu(state, &frame, &menu);
 *         inkcell_fb_overlay_end(state, &frame);
 *     }
 *
 * `up` false with the layer still on the panel is the exit, and the app's branch runs anyway -
 * so the content it describes on the way out is the content that was there, which is the only
 * honest thing it could be. Once the travel reaches nothing the call returns false, the slot is
 * released, and the branch stops running.
 *
 * ---- the stack ----
 *
 * There is no z field. The order overlays are drawn in is the order they stack, which is the
 * one rule an immediate-mode frame can state without a screen having to keep a second list in
 * agreement with the first. What the stack is *for* is the press: a menu over a sheet is two
 * modals, and only one of them owns B. inkcell_fb_overlay_modal() answers with the last modal
 * layer drawn that is not on its way out, which is the same "drawn last is on top" rule read
 * backwards.
 */

/*
 * Public API, unlike the widget headers under inkcell/ui/widgets/. A layer is the frame's own
 * machinery rather than a component: a screen asks for one, and the thing it puts in it may be
 * one of inkcell's components or may be the screen's own drawing.
 */

#include "inkcell/ui/anim.h"
#include "inkcell/ui/fb_draw.h"
#include "inkcell/ui/theme.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Where the panel sits.
 *
 * Four, and each of them is a place a real overlay goes rather than a direction on a compass.
 * A placement decides the resting box *and* which way the layer travels to reach it, because
 * those are one decision: a sheet that came in from the side would not be a sheet.
 */
enum inkcell_overlay_placement {
    /* Centred in the bounds: a dialog, an alert. Travels upward a short way, which is the
       movement every platform gives a modal that is not sliding in from an edge - enough to
       say it arrived, not enough to read as a thing sliding past. */
    INKCELL_OVERLAY_CENTER = 0,
    /* Against the bottom edge: a sheet, a notice. Travels up from below it. */
    INKCELL_OVERLAY_BOTTOM,
    /* Against the top edge: a notice dropped over the chrome. Travels down from above it. */
    INKCELL_OVERLAY_TOP,
    /* Beside `anchor`: a menu, a tooltip, a popover. Placed below the anchor when there is room
       under it and above it when there is not, and travelling away from whichever edge it came
       off - so a menu opening upward rises and one opening downward drops, and neither is a
       second thing for the caller to say. */
    INKCELL_OVERLAY_ANCHOR,
};

/*
 * How far it travels on its way in.
 *
 * The direction is the placement's; this is the distance, and there are three because there are
 * three honest answers rather than because a scale wanted filling.
 */
enum inkcell_overlay_travel {
    /* It is simply there. What a tooltip wants - a label that explains the thing under the
       cursor is not an event, and one that slid in would be drawing attention to itself instead
       of to what it names. Also what a theme with no motion in it gets, without any of this
       having to check. */
    INKCELL_OVERLAY_TRAVEL_NONE = 0,
    /* A short way, from the edge it is nearest: a dialog, a menu. The distance is a fraction of
       the panel rather than of the layer, so a tall sheet and a short menu move at the same
       speed and read as the same system. */
    INKCELL_OVERLAY_TRAVEL_NEAR,
    /* The whole way, from off the panel: a sheet, a snackbar. A container that slid in from
       just off its resting place reads as a nudge; one that comes up from off-screen reads as
       something arriving, which is what these two shapes are for. */
    INKCELL_OVERLAY_TRAVEL_OFF_PANEL,
};

/*
 * What a screen asks for. Everything here is a fact about *this frame* - the layer's own
 * memory is on the state, keyed by `id`.
 */
struct inkcell_overlay {
    /*
     * What this layer is, across frames. Not INKCELL_OVERLAY_NONE.
     *
     * A screen's own enumerator, exactly as a focus id is, and for the same reason: the thing
     * that must be remembered between two frames is where the travel has got to, and the only
     * thing that can name the layer it belongs to is the screen that opened it.
     */
    uint32_t id;
    /* Whether the app still wants it up. False on a layer already on the panel is the exit, and
       the call still returns true until the exit has finished - see the lifetime note above. */
    bool up;
    enum inkcell_overlay_placement placement;
    enum inkcell_overlay_travel travel;
    /* The resting size. A layer wider or taller than its bounds is fitted to them rather than
       clipped by them, because a panel cut off at the edge of the screen is a panel that looks
       broken rather than one that looks large. */
    int w, h;
    /* INKCELL_OVERLAY_ANCHOR: the box it hangs off, in panel coordinates. Ignored otherwise. */
    struct inkcell_fb_rect anchor;
    /*
     * Where it is allowed to be, and what the scrim covers. A zeroed rect means the whole
     * panel.
     *
     * Two callers want less than the whole panel and they want it for opposite reasons. A
     * dialog is about the *screen*, so its scrim should dim the body and leave the navigation
     * bar alone - the app has not been replaced. A sheet inside a pane is confined to the pane.
     * Both are "the region this layer belongs to", which is why it is one field.
     */
    struct inkcell_fb_rect bounds;
    /*
     * Whether the frame behind it is dimmed. The depth is the theme's (INKCELL_OPACITY_SCRIM)
     * and it eases in with the layer, so a scrim never appears before the panel it belongs to.
     */
    bool scrim;
    /*
     * How far off the page the panel stands, and so what shadow it casts: after the scrim,
     * before the panel, eased in with the layer exactly as the scrim is. FLAT - the zero value -
     * casts none, which is what every layer declared before this field existed still does.
     *
     * The layer draws it rather than the panel, because the layer is what knows where the panel
     * *was*: a shadow is a read-modify-write like the scrim, and the region it covers goes into
     * the span carried to the next frame for the scrim's reason. `shape` is the corner the panel
     * is filled with, so the shadow's corner is the panel's; a panel with only its top corners
     * rounded (a sheet) states its top corner, and the square bottom is off the edge anyway.
     */
    enum inkcell_elevation elevation;
    enum inkcell_shape shape;
    /*
     * Whether the press is the layer's while it is up.
     *
     * The layer does not route anything - what a press *means* is the application's, and always
     * was. What it can answer, and what nothing before it could, is *which* overlay a press
     * belongs to when three are stacked: inkcell_fb_overlay_modal() reports the last modal one
     * drawn that is not on its way out. A screen holds one `if` against that instead of a
     * priority order it maintains by hand.
     *
     * A layer on its way out is never the answer, whatever this says. A reader pressing B as a
     * menu closes means the screen behind it, not the menu they have already dismissed.
     */
    bool modal;
};

/*
 * What a layer hands back: where to draw, and how far in it is.
 *
 * `box` is the only field most content reads. It is where the panel is *this frame*, travel
 * included, so a sheet half way in is a box half off the bottom of the panel and the content
 * drawn into it needs to know nothing about that.
 */
struct inkcell_overlay_frame {
    uint32_t id;
    /* Where the panel is on this frame. Content is drawn against this. */
    struct inkcell_fb_rect box;
    /* Where it will be when it has arrived. What a caller measuring something against the
       settled layout wants - and what the box is, on every frame that is not moving. */
    struct inkcell_fb_rect rest;
    /* 0 at the start of the entrance, INKCELL_ANIM_ONE once it is fully in, and back down
       again on the way out. Eased, so it is a position rather than a clock reading. */
    int32_t progress;
    /* Whether this is the way in. False is a layer the app has let go of. */
    bool arriving;
    /* Whether inkcell_fb_overlay_end() has a view to pop. Set by begin(); a caller neither
       reads it nor writes it. */
    bool clipped;
};

/*
 * Opens the layer, and answers whether there is any of it on the panel.
 *
 * On true: the scrim is down, the box is in `frame`, and the drawing that follows is clipped to
 * the layer's bounds - so content that overhangs while it is arriving is cut at the region the
 * layer belongs to rather than painted over the chrome above it.
 *
 * On false: nothing has been drawn, nothing has been pushed, and `frame` is zeroed. The caller
 * must not call inkcell_fb_overlay_end(), which is what the `if` in the example at the top of
 * this header is for.
 *
 * False is also the answer when the view stack has no room for this layer's clip, which is a
 * frame with four regions already nested inside each other. Not drawing is the only honest
 * alternative: unclipped is not a degraded version of clipped, because a sheet arriving would
 * paint its way up across the app bar, and the promise this call makes is that what it draws
 * stays inside its region.
 *
 * Mutable state, like everything here that animates: where a layer has got to is the one thing
 * about it the frame itself remembers.
 */
bool inkcell_fb_overlay_begin(struct inkcell_draw_state *state,
                              const struct inkcell_overlay *overlay,
                              struct inkcell_overlay_frame *frame);

/* Closes it. Pairs with a inkcell_fb_overlay_begin() that returned true. */
void inkcell_fb_overlay_end(struct inkcell_draw_state *state, struct inkcell_overlay_frame *frame);

/*
 * The layer that owns the press, or INKCELL_OVERLAY_NONE.
 *
 * The last modal layer drawn on the previous frame that was not on its way out - "drawn last is
 * on top", read backwards. A screen asks this before it interprets a press, so a B that closes
 * a menu over a sheet closes the menu, and the same B one frame later closes the sheet, without
 * the screen keeping a stack of its own in agreement with the one it is drawing.
 *
 * Read between frames, so it answers from the frame just drawn rather than the one being drawn.
 * That is the frame the reader was looking at when they pressed.
 */
uint32_t inkcell_fb_overlay_modal(const struct inkcell_draw_state *state);

/*
 * Whether `id` has any of itself on the panel - arriving, arrived or leaving.
 *
 * What a screen asks when it has to know whether an overlay is still costing it something: a
 * body that stops registering its own focus ids while a modal is up, a press that should do
 * nothing at all until a sheet has finished going away.
 */
bool inkcell_fb_overlay_showing(const struct inkcell_draw_state *state, uint32_t id);
#ifdef __cplusplus
}
#endif

#endif /* INKCELL_UI_OVERLAY_H */
