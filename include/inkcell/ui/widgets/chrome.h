#ifndef INKCELL_BACKENDS_FB_WIDGETS_CHROME_H
#define INKCELL_BACKENDS_FB_WIDGETS_CHROME_H

/*
 * The frame around a screen's content: the bar over it, the two bars under it, the notices that
 * drop in between, and the hairline that separates any of them from the body.
 *
 * None of it is the screen. A screen names a title, a trail, a set of tabs and a notice, and
 * what each costs in pixels is decided here - which is why the empty state and the rule live
 * with the app bar rather than with the list they sit above.
 */

/*
 * inkcell/ui/widgets.h is the umbrella over this file and its siblings; include either.
 */

#include "inkcell/ui/fb_draw.h"
#include "inkcell/ui/widgets/button.h"
#include "inkcell/ui/widgets/overlay.h"

#include "inkcell/ui/actions.h"
#include "inkcell/ui/icon.h"
#include "inkcell/ui/theme.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- the frame ------------------------------------------------------------------------------
 *
 * Where the chrome ends and the body begins, before any of it has been drawn.
 *
 * Every screen needs this and none of them should derive it. The numbers are the panel's
 * geometry crossed with the theme's spacing - the edge the frame stands off, the line advance
 * the body counts in, the room the action bar will want at the bottom - and a screen that works
 * them out for itself is a screen that will disagree with the next one by a gutter.
 *
 * What it hands back is the *empty* frame: the body is the whole panel less its edges, and
 * `nav_y` and `body_y` are the same row because nothing has been drawn above them yet. Each
 * piece of chrome then takes its room as it draws - inkcell_fb_draw_nav_bar(), then
 * inkcell_fb_draw_banner(), then inkcell_fb_draw_app_bar() - and moves `body_y` down past
 * itself. That is why those three take `layout` mutably and everything else takes it const.
 *
 * `footer` is whether an action bar will be drawn at the bottom. It has to be known now rather
 * than when the bar is drawn, because the bar is drawn *last* and the body above it has already
 * been laid out by then - a list that counted rows into the footer's room would have its last
 * row painted over by the keycaps. The bar itself is then drawn at `footer_y`.
 *
 * `back` is the application's answer to whether its bar offers a way out, carried on the
 * layout because the top app bar's leading slot and the bottom action bar must not form two
 * opinions about whether B leaves - see the field's own note on struct inkcell_fb_layout, and
 * inkcell/ui/actions.h for why the question is the application's.
 *
 * `rows` is recomputed by each piece of chrome that consumes body rows, so it is always the
 * count against the body's *real* top, never a deduction from this one.
 */
struct inkcell_fb_layout inkcell_fb_layout_begin(const struct inkcell_draw_state *state,
                                                 bool footer, bool back);

/*
 * The same, naming which action bar the foot of the frame is keeping room for.
 *
 * inkcell_fb_layout_begin() is this with INKCELL_FB_FOOTER_FULL for `true` - the only bar
 * there was. A compact bar is one row rather than two, so a screen that asks for one gets that
 * row back as body, and the action bar reads `layout->footer` to draw the shape it was given
 * room for.
 */
struct inkcell_fb_layout inkcell_fb_layout_begin_footer(const struct inkcell_draw_state *state,
                                                        enum inkcell_fb_footer footer, bool back);

/*
 * How many body rows are left between `body_y` and the footer, at the body's line advance.
 *
 * The one arithmetic behind `layout->rows`, stated once. Chrome calls it after moving `body_y`;
 * an application calls it after making room of its own - a text field above a list, a chart the
 * screen reserved - so that the list below counts the rows that are actually there.
 */
uint32_t inkcell_fb_layout_rows(const struct inkcell_draw_state *state,
                                const struct inkcell_fb_layout *layout);

/* ---- the navigation bar ---------------------------------------------------------------------
 *
 * The chrome across the top: a recessed bar, one chip per tab, and the rule that closes it off.
 *
 * The bar is the point. The strip used to float on the body's own ground, which left the tabs
 * reading as the first row of content rather than as the frame around it; a recessed tier
 * behind them says "this is chrome" before a word of it is read, which is what every phone's
 * navigation bar is doing. It is the theme's lowest surface, so a palette decides how far from
 * the ground that is - on the high-contrast theme it is barely anywhere, which is correct.
 *
 * Consumes the room it occupies: `layout->body_y` comes back pointing at the first body row.
 */
void inkcell_fb_draw_nav_bar(const struct inkcell_draw_state *state,
                             struct inkcell_fb_layout *layout, const struct inkcell_fb_chip *tabs,
                             size_t count, size_t active);

/*
 * How tall that bar is, down to its closing rule, at chrome scale `small` - the height
 * inkcell_fb_draw_nav_bar() fills. For a backend that has to know where the strip is without
 * drawing it: the window that puts its title-bar buttons on the strip, and makes it the handle
 * the window is dragged by.
 */
int inkcell_fb_nav_bar_height(const struct inkcell_draw_state *state, int small);

/* ---- the screen progress bar -----------------------------------------------------------------
 *
 * A hairline across the panel, under the navigation bar's rule: the client is waiting on
 * something it has already asked for.
 *
 * Not a new drawing - it is inkcell_fb_draw_meter() at INKCELL_FB_METER_INDETERMINATE, full bleed
 * and one hairline tall - and that is deliberate rather than lazy. There is exactly one "a thing is
 * working" motion in this UI, and a second implementation of a travelling pill would be a
 * second one to keep in step with the theme's timings.
 *
 * What it is for: open Settings before the radio has answered and eight sections say "not
 * loaded", which reads identically whether a request is on its way back or nothing was ever
 * sent. This is the difference, and because it is chrome it answers for every screen at once
 * rather than for the one that happened to be waiting.
 *
 * **It never moves the body.** The navigation bar already leaves a gap between its rule and the
 * first body row, and the bar hangs in that gap - so `layout` is const here, the rows a list
 * gets are the same rows whether or not anything is in flight, and a save going out does not
 * reflow the screen it was saved from. It is the same rule the card's focus ring is drawn by:
 * an indicator that changes the layout is an indicator that moves what it is pointing at.
 *
 * Which states count is not this file's business: the application answers it once, for
 * every backend, and passes the answer in.
 */
void inkcell_fb_draw_progress(struct inkcell_draw_state *state,
                              const struct inkcell_fb_layout *layout, bool busy);

/* ---- the banner ------------------------------------------------------------------------------
 *
 * The persistent notice: something is true of the whole client and stays true until it is
 * resolved.
 *
 * The snackbar is the transient half of this and is correctly transient - it is for what just
 * happened. What it cannot say is what is *still the case*: a release waiting to be installed
 * was visible only inside Settings > About, so the one screen that already knew was the only
 * screen that said so. A banner is the other half: it costs body rows, it does not go away on a
 * timer, and it sits in the chrome under the tab strip where a statement about the client
 * belongs.
 *
 * Why it is above the screen's own app bar rather than below it, which is where Material puts
 * one. The navigation bar is this client's app-level chrome and the top app bar is the
 * *screen's* heading; a banner is a statement about the client, so it goes with the first. The
 * practical half of the same answer: drawn below the app bar it would have to be called by
 * every screen renderer, and the four overlays would each need their own copy - which is the
 * duplication drawing it here, once per frame, exists to prevent.
 *
 * Nothing here animates, and that is a decision rather than an omission. The container consumes
 * body rows, so a height that eased open would reflow the list underneath it for the length of
 * the animation - and unlike the snackbar, which arrives over the UI and has to be *noticed*,
 * a banner is read whenever the eye next reaches the top of the panel.
 *
 * Which banner, if any, is the application's answer, for the reason above.
 */
struct inkcell_fb_banner {
    /* The leading symbol, in the container's own ink. */
    enum inkcell_icon icon;
    /* The headline: what is true. NULL or empty draws nothing at all, which is what makes "no
       banner" a struct rather than a branch at the call site. */
    const char *text;
    /* What to do about it, on a second line at the label scale. NULL for none - and dropped
       before the headline is when the body cannot spare the row for it. */
    const char *supporting;
    /* A fact stated in its own units against the trailing edge of the headline: a version
       number. Untranslated by design, so it is a string rather than an id. NULL for none. */
    const char *detail;
    /* The container's fill and the ink on it, taken together from one theme call. A family
       rather than a tone for the reason a badge takes one: this thing fills something. */
    enum inkcell_family family;
};

/*
 * Draws it at the top of the body and consumes the rows it took, so a screen renderer that
 * follows lays out against a shorter body without knowing this happened.
 *
 * Recomputes `rows` from the body's real bottom rather than deducting a row count, for the
 * reason inkcell_fb_draw_app_bar() does - see the comment on its tail.
 */
void inkcell_fb_draw_banner(const struct inkcell_draw_state *state,
                            struct inkcell_fb_layout *layout,
                            const struct inkcell_fb_banner *banner);

/* ---- the action bar -------------------------------------------------------------------------
 *
 * The chrome across the bottom: what the buttons do here, as keycaps, over the line that says
 * what the radio is doing.
 *
 * This was two lines of plain text, and it was the last screen-level renderer laying out its
 * own pixels - which is also why it was the piece of chrome that most made the UI read as a
 * terminal rather than as a handheld OS. A hint sentence is a row of controls written down as
 * words: the letters in it are things on the case, and the verbs after them are what those
 * things do. Drawing it as keycaps says both without the eye having to parse a sentence to
 * find the one letter it was looking for.
 *
 * The keycap is INKCELL_FB_BUTTON_FILLED at INKCELL_SHAPE_SM, which is the component set's existing
 * answer for "a place to press" - the on-screen keyboard's keys are the same button - so a
 * keycap here and a key there cannot drift apart.
 *
 * **What it draws is `struct inkcell_button_action`, never a sentence.** The bar has to iterate the
 * pairs, so the pairs have to exist before the drawing does; that is why the hint catalog
 * entries were retired in favour of the table in src/ui/tables/actions.c. See
 * include/inkcell/ui/actions.h.
 *
 * It owns the whole bottom bar - the surface, the rule above it, the keycaps and the status
 * line - for the same reason the card owns its own inset: a screen that placed the status line
 * itself would be back to computing a y coordinate in a screen renderer.
 */

struct inkcell_fb_action_bar {
    const struct inkcell_button_action *items;
    size_t count;
    /*
     * The line under the keycaps: the transport state, and either the radio it is attached to
     * or how to quit.
     *
     * It used to share a row with the transient notice, which took it whenever there was one -
     * so every action blanked the answer to "is there a radio attached?" for four seconds. The
     * notice has somewhere of its own now (inkcell_fb_draw_snackbar), and this row says one thing,
     * always. Clipped to the panel rather than wrapped: the bar is a fixed height.
     */
    const char *status;
    enum inkcell_tone status_tone;
    /*
     * How many of `items` the bar shows before the rest go behind `more`. 0 shows every one of
     * them, which is the bar as it always was.
     *
     * The persistent footer is the right answer on a device with no pointer - it is the one
     * place a d-pad reader can find out what the buttons do - and the wrong one when it prints
     * every press on every frame: seven keycaps in a row is a handheld emulator's HUD, and the
     * press the screen is *for* is one of seven things at the smallest size on the panel. Two
     * or three is what a reader takes in without reading, and `items` is already in priority
     * order, so the first `limit` of them are the ones worth the room.
     */
    size_t limit;
    /*
     * The keycap that opens the rest: the application's own button and its own word for it,
     * because which key is free to mean "more" is a question about the device and the app. The
     * TrimUI's MENU is its quit key, and an app there would name START or SELECT here.
     *
     * Drawn after the limited items whenever anything is behind it, and never dropped for room
     * while it is - a bar that elided the one cap leading to everything else would have hidden
     * the whole set. NULL for none, in which case `limit` simply stops early.
     */
    const struct inkcell_button_action *more;
    /*
     * Draw the first item's cap as the screen's primary press: a tonal pill in the primary
     * family, where the others are the neutral keycap.
     *
     * The action bar's half of the "one emphasized action" rule the top app bar keeps. A row of
     * keycaps of equal weight has no way to say "this is what you came here to do", and in a
     * bar of two or three the one that says so is found without reading.
     */
    bool emphasize_first;
};

/* The room the bar wants at the foot of the panel - what a caller subtracts from the panel
   height to find where the body ends. One row for a layout opened with a compact footer, two for
   anything else. */
int inkcell_fb_action_bar_height(const struct inkcell_draw_state *state,
                                 const struct inkcell_fb_layout *layout);

/*
 * Draws it, with its top edge at `layout->footer_y`, and returns the index of the first item
 * that is *not* on it - `count` when every one of them is.
 *
 * Actions that do not fit are dropped from the *end*, which is why struct inkcell_action_bar
 * is documented as being in priority order: on a narrow panel or in a long translation, the
 * press the screen is for survives and "L/R tabs" - true everywhere, and therefore the least
 * worth the room - is what goes.
 *
 * The return is what makes that honest once there is a `more` cap. What the bar dropped for
 * room and what it held back for `limit` are both the tail of the array, so one index says
 * where the menu behind `more` starts - see inkcell_fb_action_bar_menu(). A screen keeps it from
 * the frame it drew, the way it keeps a FAB's box.
 */
size_t inkcell_fb_draw_action_bar(const struct inkcell_draw_state *state,
                                  const struct inkcell_fb_layout *layout,
                                  const struct inkcell_fb_action_bar *bar);

/*
 * The menu behind the `more` cap: `bar->items[from..count)` as menu rows, in the bar's own
 * order. Returns how many were written, at most `max`.
 *
 * Row `i` is `bar->items[from + i]`, so the menu's `focus_base + i` numbering maps a chosen row
 * straight back to the press it stands for. The row says the verb and not the cap: a reader
 * who opened a menu to find a press is going to choose it from the menu, not go looking for
 * the key.
 */
size_t inkcell_fb_action_bar_menu(const struct inkcell_fb_action_bar *bar, size_t from,
                                  struct inkcell_fb_menu_item *items, size_t max);

/* ---- the first-use tip ----------------------------------------------------------------------
 *
 * A press worth learning, said once, beside the bar it lives in - and then gone.
 *
 * The other half of a compact footer. A bar that shows three presses and hides the rest behind
 * `more` has to tell a new reader that the rest exist, and the full footer did that by printing
 * all of them, forever. This says it the first time and gets out of the way, which is what
 * every platform's coach mark is for: a hint that is always there stops being read by the
 * third frame, and a hint that arrives is read because it moved.
 *
 * It rises out of the action bar's top edge and sinks back behind it, rather than fading. A
 * container cannot fade on a panel with no alpha to composite, and moving is the honest
 * version: the bar is where the tip came from, so it is where it goes back to.
 *
 * Whether it has been seen is the application's business and has to outlive the run, so the
 * toolkit holds none of it. The app decides a tip is due, stamps `since_ms` once, and keeps
 * handing the same tip in until inkcell_fb_action_tip_live() says it has finished - then
 * records it as seen, in whatever it persists.
 */

/* How long a tip rests where it can be read, between rising and sinking. Four seconds is a
   sentence of a dozen words read twice, which is the snackbar's own measure. */
#define INKCELL_FB_ACTION_TIP_HOLD_MS 4000U

struct inkcell_fb_action_tip {
    /* The press it is about, drawn as the bar draws it - a keycap and nothing else - at the
       tip's head. NULL for a tip about no one key. */
    const struct inkcell_button_action *action;
    /* The sentence: "More actions are under START". Already translated. NULL or empty draws
       nothing, so "no tip" is a struct rather than a branch. */
    const char *text;
    /* When it was first shown, on the clock `state->now_ms` runs on. The tip is a pure function
       of this and the time: no animation table, no identity, and a picture of it lands in the
       same place on every host. */
    uint64_t since_ms;
};

/*
 * Whether the tip is still on its way in, resting, or on its way out at `state->now_ms`.
 *
 * What an application asks after drawing a frame: while it answers true, the frame after this
 * one has something to move, and once it answers false the tip has sunk behind the bar and the
 * app can record it as seen and stop handing it in.
 */
bool inkcell_fb_action_tip_live(const struct inkcell_draw_state *state,
                                const struct inkcell_fb_action_tip *tip);

/*
 * Draws it at wherever its journey has got to: against the leading edge of the body, standing
 * on the action bar's top edge.
 *
 * Clipped to the body, so the part of it still behind the bar is not painted over the keycaps
 * - which means it can be drawn before or after the bar and comes out the same. Mutable state
 * for the reason the FAB's is: it says where it is moving, so a frame drawn under a clip band
 * still copies the rows it vacated.
 */
void inkcell_fb_draw_action_tip(struct inkcell_draw_state *state,
                                const struct inkcell_fb_layout *layout,
                                const struct inkcell_fb_action_tip *tip);

/* ---- the floating action button ---------------------------------------------------------------
 *
 * The one thing on a screen worth doing, as a shape rather than as a row: compose a message,
 * add a node, start a scan.
 *
 * Every verb here has been a keycap or a list row, and both of those are the *same weight* as
 * everything beside them. A keycap row says what the four buttons do, in priority order, and
 * the screen's own reason for existing is one of four things printed at the smallest size on
 * the panel; a list row that starts something looks exactly like the rows that merely report
 * something. Neither has a way to say "this one". That is the whole of what this shape is for,
 * and it is why Material gives it a container of its own and a corner of the frame to stand in.
 *
 * Why it is chrome rather than a button. include/inkcell/ui/widgets/button.h opens by saying
 * that nothing in it knows where on the panel it is being drawn, and that is exactly what a FAB
 * has to know: it is anchored to the body's trailing bottom corner, it keeps the panel's own
 * margin from the edge, and it has to clear an action bar that is drawn after it. A component
 * placed by the frame belongs with the frame - which is the same argument the banner and the
 * progress bar are here under.
 *
 * It does not consume body rows, so `layout` is const. That is the progress bar's rule for the
 * same reason: a screen whose content reflowed when its primary action appeared would be moving
 * the thing the action is about. What a screen owes it instead is *clearance* - see
 * inkcell_fb_fab_clearance() - so that the last row of a list can still be scrolled out from
 * under it.
 *
 * What it is filled with. A FAB floats on a shadow, and this one casts the floating level's
 * (INKCELL_ELEVATION_FLOATING) where the theme states one - but on a dark palette a shadow is a
 * few levels of the ground, so the fill is still the main cue, which is the snackbar's answer
 * one component over: it is the only saturated container on the body, and it takes a *family*
 * rather than a tone because it fills something and a fill travels with the ink the theme was
 * validated against. INKCELL_SHAPE_FULL rather than a rounded square, because
 * the circle is what makes the extended form read as the same object grown sideways rather than
 * as a second control.
 */

/*
 * How much of the frame it takes.
 *
 * Three, and the axis that changes is the *room around the symbol* rather than the symbol
 * itself at the first step - which is Material's own scale: a small and a regular FAB carry the
 * same icon in different amounts of container, and only the large one draws a bigger symbol.
 * Each size names one entry of the spacing scale, so a theme that asks for a roomier layout
 * gets a roomier FAB without any of these being a number.
 */
enum inkcell_fb_fab_size {
    /* What a screen with one obvious verb wants, and the zero value for the reason
       INKCELL_FAMILY_PRIMARY is: a FAB declared with nothing said about its size is the
       ordinary one, not the smallest one. */
    INKCELL_FB_FAB_MD = 0,
    /* Beside content that is already busy: a mark rather than a target. */
    INKCELL_FB_FAB_SM,
    /* A screen whose whole purpose is the one action - an empty state with a way out of it.
       The one size that draws its symbol bigger as well as its container. */
    INKCELL_FB_FAB_LG,
};

struct inkcell_fb_fab {
    /*
     * The symbol. Required: a FAB is a picture first - it is the one control on the frame that
     * has no room for a word at rest - so one with nothing to put in its disc draws nothing at
     * all rather than an empty circle.
     */
    enum inkcell_icon icon;
    /*
     * The verb beside it, for the extended form. NULL for the plain disc.
     *
     * It stays here while the FAB collapses, and that is deliberate rather than an oversight:
     * `extended` going false is the *start* of a journey the widget then has to finish, and a
     * caller that took the words away on the same frame would leave it collapsing around a
     * label that is no longer there. It is the layer's rule in miniature - the app keeps
     * describing the content until the travel has finished - see include/inkcell/ui/overlay.h.
     */
    const char *label;
    /*
     * Whether the verb is showing *now*.
     *
     * The extended FAB's one behaviour everywhere it exists: it carries its word while the body
     * is at the top and shrinks to its symbol once the reader has started scrolling, because by
     * then they have read it. A screen sets this from its own scroll - `inkcell_scroll_offset()
     * <= 0` is the usual answer - and the widget eases the container between the two widths and
     * cross-fades the label, exactly as the collapsing app bar eases between its two headings.
     *
     * Ignored when there is no label, and overruled when the body cannot spare the width: a
     * verb that does not fit is a FAB without a verb, not a FAB running off the panel. That is
     * the chip strip's elision rule applied to a component with one label instead of five.
     */
    bool extended;
    /* What it is tinted with. Zero is INKCELL_FAMILY_PRIMARY, which is what a primary action
       wants; a FAB that destroys something names the error family and gets the pair the theme
       checked against it. */
    enum inkcell_family family;
    enum inkcell_fb_fab_size size;
    bool focused; /* the d-pad will act on it */
    /*
     * Identity for the collapse, in the animation table on the state - the switch's `id` and
     * not a focus id, because the two spaces are unrelated. 0 means "no identity": the FAB
     * draws correctly at whichever width `extended` asks for, and simply never eases between
     * them.
     */
    uint32_t id;
    /* What the d-pad calls it, or INKCELL_FOCUS_NONE. Registered as the pill it was actually
       filled as, so a ring that lands here takes the FAB's own curve. */
    uint32_t focus_id;
};

/*
 * Where it rests: the box it is drawn in once it has finished extending or collapsing.
 *
 * A *resting* box rather than this frame's, because the width is mid-flight for as long as the
 * collapse lasts and nothing a screen does with this wants a number that moves - a first paint
 * adopts its target without animating, so on the frame that matters the two are the same.
 *
 * Stated here rather than derived by whoever wants it, which is inkcell_fb_chip_box()'s rule one
 * component over: a box worked out twice is a box that will one day be two boxes, and the second
 * of them would be an invisible rectangle the cursor sits on beside the disc it is supposed to
 * be on.
 *
 * A zero-width box is a frame with no room for one - a panel too short between its chrome and
 * its footer - and is what inkcell_fb_draw_fab() draws and registers nothing for.
 */
struct inkcell_fb_rect inkcell_fb_fab_box(const struct inkcell_draw_state *state,
                                          const struct inkcell_fb_layout *layout,
                                          const struct inkcell_fb_fab *fab);

/*
 * The room at the foot of the body a FAB is standing in: what a scrolling body adds to its
 * content height so its last row can be scrolled out from under it.
 *
 * The other half of "it does not consume body rows". Nothing reflows when a FAB appears, so the
 * cost is paid at the *end* of the content instead - which is what every phone list does under
 * one, and the only arrangement in which the last item is reachable.
 *
 * Zero when there is no room for a FAB at all, so a caller adds it without testing first.
 */
int inkcell_fb_fab_clearance(const struct inkcell_draw_state *state,
                             const struct inkcell_fb_layout *layout,
                             const struct inkcell_fb_fab *fab);

/*
 * Draws it, easing the container towards the width `extended` asks for, and hands back the box
 * it came out as - a zero-width one when there was no room and nothing was drawn.
 *
 * *This* box rather than the resting one above, and the difference is the whole reason both
 * exist: what a screen wants before it draws is where the FAB will settle, and what it wants
 * afterwards is where the FAB is *now*. A menu hung off a FAB is the case that cannot use the
 * other one - INKCELL_OVERLAY_ANCHOR lines a panel up with the control it came out of, and a
 * panel anchored to a resting box would sit beside a container that is still half extended.
 *
 * Mutable state, like every animated component here: where the collapse has got to lives in the
 * animation table, keyed by `fab->id`.
 *
 * Drawn after the body and before the action bar - it floats over the screen's content and
 * under the frame's own chrome, which is the order those two sentences are true in.
 */
struct inkcell_fb_rect inkcell_fb_draw_fab(struct inkcell_draw_state *state,
                                           const struct inkcell_fb_layout *layout,
                                           const struct inkcell_fb_fab *fab);

/* ---- the top app bar ------------------------------------------------------------------------
 *
 * The heading a screen opens with: where you are, how to get out, and one fact about the whole
 * screen. Consumes the body rows it occupies, so a screen calls this and then lays its list out
 * against the layout it hands back.
 *
 * It used to take a `const char *`, which meant a screen's heading was a *string* and
 * everything a heading had to carry got glued into it. Settings built "Settings > %s%s%s" out
 * of two catalog entries and an unsaved marker, and that is a whole-sentence string id doing
 * structural work in the same way the button hints were: the `>` separators handed a translator
 * the breadcrumb's grammar along with its words, and a badge glued into a title with %s cannot
 * be a badge. What is left in the catalog is one word per level.
 *
 * The slots, and what each is for:
 *
 *   leading    the back affordance. Not a field - it is layout->back, from the action bar's own
 *              table, so the arrow and the B keycap cannot disagree about whether B leaves.
 *   overline   the trail of levels above this one, separated by a drawn chevron. Two levels is
 *              the deepest anything here goes ("Settings > Modules" over "Telemetry").
 *   title      what this screen is. One line, at INKCELL_TYPE_TITLE.
 *   badge      a fact about the screen rather than about any row of it.
 *   status     one compact mark about the whole client - the link, the battery - in a tone.
 *   actions    the screen's own verbs as icon buttons, one of them optionally emphasized, and
 *              an overflow button holding whatever did not fit.
 *
 * Read from the trailing edge in: overflow, actions, status, badge, then the title in whatever
 * room is left. Every one of those after the title is optional and zero is "none", so a bar
 * declared with a title and nothing else is the bar it always was, pixel for pixel.
 *
 * And three modes that replace the heading rather than add to it - a search field, a
 * selection's count - and a large title that collapses into the bar as the body scrolls. See
 * enum inkcell_fb_app_bar_mode and `large`.
 */

/* ---- the app bar's actions -------------------------------------------------------------------
 *
 * The screen's verbs, as icon buttons against the bar's trailing edge.
 *
 * Every verb on a screen here has been a keycap, and a keycap is a fine answer to "what do the
 * buttons do" and a poor one to "what can I do here": it is bound to a button, so a screen has
 * as many verbs as the case has buttons, and it is printed at the foot of the panel far from
 * the heading it acts on. An icon in the bar is the answer every platform gives to the second
 * question - search, share, filter - and it is reached by moving *up* into the chrome, which a
 * d-pad does as well as a finger.
 *
 * The array is in priority order, the action bar's rule: the bar keeps as many as fit from the
 * front and the rest go behind the overflow button, so the first entry survives a narrow panel
 * and a long title. Laid out left to right in that same order, so a screen's verbs do not
 * rearrange themselves as the panel narrows - they fall off the end, into the menu.
 */

/* The most actions a bar reads. A bar with more verbs than this is a screen that wants a menu of
   its own rather than a heading, and it is also the width of the mask a fit is reported in. */
#define INKCELL_FB_APP_BAR_ACTIONS_MAX 8U

struct inkcell_fb_bar_action {
    /*
     * The symbol on the bar. INKCELL_ICON_NONE makes the action *menu only*: it is never drawn
     * on the bar and is always a row of the overflow menu, which is how a screen puts "Settings"
     * or "Help" behind the dots without a second array to keep in step with the first.
     */
    enum inkcell_icon icon;
    /*
     * The verb. Required - an action with no words is skipped - because it is what the overflow
     * menu says when the icon did not fit, what the emphasized action says beside its icon, and
     * the only thing about a bare symbol that can be translated.
     */
    const char *label;
    /*
     * The one verb the screen is for: drawn as a tonal pill with its label beside the icon,
     * where the rest are bare symbols. The first action with this set is the emphasized one and
     * the flag is ignored on any after it - two emphasized verbs is none.
     *
     * It is also the last to leave. When the bar runs short it gives up its label before it
     * gives up its place, and gives up its place only after every plain action has gone - the
     * FAB's elision, applied to a verb in the heading.
     */
    bool emphasized;
    /* What the emphasized pill is tinted with. Zero is INKCELL_FAMILY_PRIMARY; a verb that
       destroys something names the error family. Ignored on a plain action. */
    enum inkcell_family family;
    /* Greyed, and registered nowhere - the menu item's rule: the cursor steps over a verb that
       does nothing rather than landing on it. It stays on the bar, because a verb that is
       sometimes missing is a bar that rearranges itself under the reader. */
    bool disabled;
    bool focused; /* the d-pad will act on it */
    /* What the d-pad calls it, or INKCELL_FOCUS_NONE. The same id is the overflow menu row's,
       so a screen answers a press the same way wherever the verb was when it was chosen. */
    uint32_t focus_id;
};

/*
 * A compact mark about the whole client, in the bar: the link is up, the battery is low.
 *
 * The action bar's status line said this, on a line of its own under the keycaps, on every
 * frame. That is a whole row of the panel for a fact that is usually "fine" - and a compact
 * footer has no second line to put it on. A phone says the same thing with one symbol in the
 * corner, coloured when it matters, and this is that symbol.
 */
struct inkcell_fb_bar_status {
    /* The symbol. INKCELL_ICON_NONE draws no indicator. */
    enum inkcell_icon icon;
    /* What it means. INKCELL_TONE_DIM for "fine", which is what it should be most of the time
       - a status mark that is always bright is a status mark that is never read. */
    enum inkcell_tone tone;
    /* A word beside it - the radio's name, "offline" - at the label scale. The first thing on
       the bar to go when it runs short. NULL for the symbol alone. */
    const char *text;
};

/*
 * What the heading is doing.
 *
 * Modes rather than flags, because each one *replaces* the title slot rather than adding to
 * it, and two of them at once is not a bar anything draws. None of them changes the bar's
 * height - the trail is dropped in both, since a search field and a selection count are the
 * screen's whole heading - so entering or leaving one never reflows the body under it.
 */
enum inkcell_fb_app_bar_mode {
    /* The title, and everything above. */
    INKCELL_FB_APP_BAR_NORMAL = 0,
    /*
     * The title slot is a search field: `query` as typed, or `placeholder` dimmed when it is
     * empty, a caret while `editing`, and a clear mark once there is something to clear.
     *
     * The field is drawn, not edited. What a key press does to the query is the application's
     * own - on this device it is inkcell's on-screen keyboard - and the field is where the
     * result is shown, so a filter narrowing the list below is readable without the keyboard
     * in the way. The actions stay: a search screen's filter button is a verb like any other.
     */
    INKCELL_FB_APP_BAR_SEARCH,
    /*
     * Contextual selection: the bar fills with the secondary family's container, the leading
     * mark becomes a close, and the title is what is selected ("3 selected", already
     * translated). The actions are the verbs that apply to the selection.
     *
     * The fill is what makes it a mode rather than a relabelled title - the reader has to be
     * able to tell at a glance that B now clears a selection instead of leaving the screen. That
     * is also why the close is drawn whether or not `layout->back` is set: in this mode B has
     * one meaning, and the mark is how the bar says it.
     */
    INKCELL_FB_APP_BAR_SELECTION,
};

/* Settings is the deepest trail in the tree and it is two levels; three is one level of slack
   so that a screen growing one is a call-site change rather than a component change. */
#define INKCELL_FB_APP_BAR_TRAIL_MAX 3U

struct inkcell_fb_app_bar {
    /* Outermost first: {"Settings", "Modules"} above a title of "Telemetry". Each entry is one
       level's own name - the separators belong to the component. */
    const char *trail[INKCELL_FB_APP_BAR_TRAIL_MAX];
    size_t trail_count;
    const char *title;
    /* The trailing capsule, empty for most screens. `badge_family` is what it is filled with:
       the warning family for edits the radio has not been told about, which is the one this
       exists for. */
    const char *badge;
    enum inkcell_family badge_family;

    /* The verbs, in priority order - see struct inkcell_fb_bar_action. At most
       INKCELL_FB_APP_BAR_ACTIONS_MAX are read. */
    const struct inkcell_fb_bar_action *actions;
    size_t action_count;
    /* The overflow button's focus id and state. It is drawn only when something is behind it,
       and a screen that leaves the id at INKCELL_FOCUS_NONE has a menu the d-pad cannot reach -
       which is a bug in the screen, not a choice. */
    uint32_t overflow_focus_id;
    bool overflow_focused;

    struct inkcell_fb_bar_status status;

    enum inkcell_fb_app_bar_mode mode;
    /* INKCELL_FB_APP_BAR_SEARCH only. `query` is the text as typed, NULL or empty for none;
       `placeholder` is what an empty field says. */
    const char *query;
    const char *placeholder;
    /* The field has the keyboard: draw a caret. */
    bool editing;
    /*
     * Where that caret is, as bytes of `query` after it - struct inkcell_keyboard's own count,
     * handed straight over, and the text field's `caret_back` on the same terms. 0 is the end,
     * and so is a count longer than the query: a stale caret is the end rather than a mark
     * before the first letter.
     *
     * The keyboard's triggers move the caret through what was typed, and a field that drew it
     * after the last letter wherever it really was would be telling the reader the next letter
     * lands somewhere it does not. So the window follows the caret rather than the tail: a long
     * query with the caret near its start shows its start.
     */
    size_t caret_back;
    /* The field itself, for the d-pad to land on, and the clear mark inside it. The mark is
       drawn only when there is a query *and* an id to press it by - a clear that cannot be
       pressed is a mark that promises something the screen does not do. */
    uint32_t field_focus_id;
    bool field_focused;
    uint32_t clear_focus_id;
    bool clear_focused;

    /*
     * A large title: the screen's name on a line of its own under the bar, collapsing into the
     * bar as the body scrolls - see include/inkcell/ui/widgets/scroll.h for the shape and why
     * it is a cross-fade. `offset` is inkcell_scroll_offset() for the body under it, and
     * `detail` is the caption beside the large heading only.
     *
     * The trail is not drawn - a large title is a tab's root, and a screen with somewhere to go
     * back up to has an overline instead - and the mode must be NORMAL: a search field or a
     * selection is the collapsed bar's business, and a heading that stayed large while it was
     * one would be two headings.
     */
    bool large;
    int32_t offset;
    const char *detail;
};

/*
 * Which of a bar's actions it had room for, and where the overflow button came out - what a
 * screen keeps from the frame it drew, the way it keeps a FAB's box.
 */
struct inkcell_fb_app_bar_fit {
    /* Bit `i` set: `actions[i]` is on the bar. */
    uint32_t shown;
    /* How many are behind the overflow button, menu-only ones included. */
    size_t hidden;
    /* The overflow button's box, for INKCELL_OVERLAY_ANCHOR to hang the menu off. Zero-width
       when there is nothing behind it and no button was drawn. */
    struct inkcell_fb_rect overflow;
};

/*
 * Draws it and consumes the rows it took - and says which verbs made it onto the bar.
 *
 * The fit is decided here rather than by the caller because it is a function of pixels the
 * caller does not have: the title's measured width, the badge's, the status mark's, all at the
 * glyph scale the theme chose. The screen gets the answer back rather than working it out, which
 * is inkcell_fb_draw_fab()'s arrangement: a box worked out twice is a box that will one day be
 * two boxes.
 */
struct inkcell_fb_app_bar_fit inkcell_fb_draw_app_bar(const struct inkcell_draw_state *state,
                                                      struct inkcell_fb_layout *layout,
                                                      const struct inkcell_fb_app_bar *bar);

/*
 * The overflow menu for a bar as it was last drawn: every action `fit` left off the bar, in the
 * bar's own order, menu-only ones included. Returns how many rows were written, at most `max`.
 *
 * `ids`, when not NULL, is filled in parallel with each action's own focus id, ready to hand the
 * menu as `focus_ids` - so the id a screen gets back from a menu row is the id it would have got
 * from the icon on the bar, and one switch answers both.
 */
size_t inkcell_fb_app_bar_menu(const struct inkcell_fb_app_bar *bar,
                               const struct inkcell_fb_app_bar_fit *fit,
                               struct inkcell_fb_menu_item *items, uint32_t *ids, size_t max);

/*
 * How far down the body the bar pushes it, without drawing anything.
 *
 * Beside inkcell_fb_action_bar_height() and for the same reason: a screen that has to know how big
 * its content area will be *before* it has something to put in the bar cannot get there by drawing
 * the bar first. The map is the one such screen - its badge counts the markers on the panel,
 * which is not answerable until the panel has been measured - and the alternative is drawing the
 * bar twice, once with a wrong number and once over the top of it.
 *
 * A trail is one extra line, which is the only thing about a bar that changes its height. What
 * the title says, and whether there is a badge, do not.
 */
int inkcell_fb_app_bar_height(const struct inkcell_draw_state *state,
                              const struct inkcell_fb_layout *layout, size_t trail_count);

/*
 * The same answer for a whole bar: the trail it will actually draw given its mode, and a large
 * title at the `offset` it will be drawn at.
 *
 * What a screen with a search mode or a large title asks instead of the count above, because
 * both change which of the bar's lines are drawn and the count alone cannot say so.
 */
int inkcell_fb_app_bar_measure(const struct inkcell_draw_state *state,
                               const struct inkcell_fb_layout *layout,
                               const struct inkcell_fb_app_bar *bar);

/*
 * What a screen says instead of a list when it has nothing to show, under the icon of whatever
 * the list would have held.
 *
 * The icon is drawn large and dim above the words, which is the shape an empty state has
 * everywhere: the screen is blank, so the one thing on it can afford to be the size that says
 * "this is empty on purpose" rather than "this failed to load".
 */
void inkcell_fb_draw_empty(const struct inkcell_draw_state *state,
                           const struct inkcell_fb_layout *layout, enum inkcell_icon icon,
                           const char *text);

/* How tall a hairline is at `scale` - what a caller stacking something under one has to clear.
   Beside the call that draws one because two expressions for one thickness is how a bar ends up
   overlapping the rule above it. */
int inkcell_fb_rule_height(const struct inkcell_draw_state *state, int scale);

/* A hairline separator - under the tab strip, above a detail pane. The role says which of the
   theme's two rule colours it is: INKCELL_COLOR_RULE for a separator inside the body,
   INKCELL_COLOR_RULE_STRONG for the one that closes the chrome off. */
void inkcell_fb_draw_rule(const struct inkcell_draw_state *state, int x, int y, int w, int scale,
                          enum inkcell_color role);

/* "Messages (12)", or "Messages (12, +40 older)" when a ring has dropped some. */
void inkcell_fb_title_count(char *out, size_t out_len, const char *name, uint32_t count,
                            uint32_t dropped);

#endif /* INKCELL_BACKENDS_FB_WIDGETS_CHROME_H */
