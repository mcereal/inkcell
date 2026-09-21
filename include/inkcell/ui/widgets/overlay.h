#ifndef INKCELL_BACKENDS_FB_WIDGETS_OVERLAY_H
#define INKCELL_BACKENDS_FB_WIDGETS_OVERLAY_H

/*
 * What is drawn over a screen rather than in it: the dialog, the menu, the bottom sheet, the
 * snackbar, and the QR code a screen hands a link to.
 *
 * All but the last are *content for a layer*. include/inkcell/ui/overlay.h is the layer - the
 * box, the way in and out, the scrim, the stack and who owns the press - and everything here
 * is what goes in one. That split is why the menu and the sheet exist at all: each of them is
 * a few dozen lines of drawing now that none of them has to bring its own answer to "where
 * does this go and how does it get there".
 */

/*
 * Not public API. include/inkcell/ui/fb.h is; inkcell_fb_widgets.h is the umbrella over this file
 * and its siblings, and nothing outside src/ui/backends/ should include either.
 */

#include "inkcell/ui/fb_draw.h"

#include "inkcell/ui/icon.h"
#include "inkcell/ui/overlay.h"
#include "inkcell/ui/theme.h"
#include "inkcell/utils/qr.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- the dialog -------------------------------------------------------------------------------
 *
 * A raised panel that asks one question and offers two answers.
 *
 * The confirmation screen was a title, four lines of wrapped text on the bare ground, and the
 * two answers as ordinary list rows under them - which is to say it looked exactly like every
 * other list in the app, at the one moment the app is asking rather than showing. A dialog is
 * the shape that difference has everywhere else: the question lifted onto its own surface, and
 * the answers as *buttons* rather than as rows.
 *
 * It used to fill the body, and the note here said that was deliberate: a dialog elsewhere
 * dims what is behind it with a scrim, a scrim is alpha, and the Brick's display engine
 * composites fb0 against its own layer, so there was nothing to blend against. The premise was
 * right and the conclusion was not - a scrim blends against what is already *on* the page, not
 * against what is behind it, and we drew that ourselves a few microseconds earlier. See
 * INKCELL_COLOR_SCRIM.
 *
 * So it is a panel over the body now, on a layer, with the frame behind it dimmed and still
 * visible. The scrim covers the body and stops at the navigation bar: this screen is asking a
 * question, the application has not been replaced.
 *
 * The action row is the reason the two answers move off the list. Both are one press away
 * whichever is under the cursor, so the pair reads as a choice rather than as a menu; and the
 * accept is a tonal button while the cancel shows no fill at all, which is how every dialog
 * says which answer it is proposing without the words having to.
 */

struct inkcell_fb_dialog {
    /* Over the headline, drawn large in the primary - or in the error colour when
       `destructive`. INKCELL_ICON_NONE for none, and the panel closes up the room it would
       have taken. */
    enum inkcell_icon icon;
    const char *headline;
    /* The supporting paragraph, wrapped across the panel. "" for a question that needs none. */
    const char *text;
    const char *accept;
    const char *cancel;
    /* 0 is accept, 1 is cancel - the same index nav.confirm_cursor carries, which every
       direction toggles. */
    uint32_t cursor;
    /* What it goes through with cannot be undone: the whole dialog switches from the primary
       family to the error one, so the icon, the headline and the accept button's fill all
       change together rather than each being decided separately. */
    bool destructive;
    /*
     * What the d-pad calls the two answers: `action_focus_id` is the accept and
     * `action_focus_id + 1` is the cancel, which is `cursor`'s own numbering rather than a
     * second one to remember. INKCELL_FOCUS_NONE for a dialog a screen steers with its own
     * toggle, which is what every dialog did before this existed.
     *
     * Worth registering even though a dialog is two buttons a screen could toggle between
     * blind: the pair is laid out side by side or *stacked*, decided here from the words and
     * the panel's width, and left-right on a stacked pair is not the press that moves between
     * them. A screen resolving the press against the boxes gets that right without knowing
     * which way this went.
     */
    uint32_t action_focus_id;
};

/*
 * How tall this dialog needs to be in a panel `width` wide, given `room` to grow into.
 *
 * Asked before the layer is opened, because a layer is handed a size rather than deciding one:
 * a dialog's height depends on how far its paragraph wraps, which is a fact about the words
 * and not about where the panel ends up. The paragraph is the only part that gives way - the
 * buttons, the headline and the icon are reserved first, because a dialog that dropped one of
 * its buttons to fit its explanation would be unanswerable.
 */
int inkcell_fb_dialog_height(const struct inkcell_backend_fb_state *state,
                             const struct inkcell_fb_dialog *dialog, int width, int room);

/* Draws it into `box`, for a screen that has opened a layer of its own. */
void inkcell_fb_draw_dialog_at(const struct inkcell_backend_fb_state *state,
                               struct inkcell_fb_rect box, const struct inkcell_fb_dialog *dialog);

/*
 * The whole thing: measures the dialog, opens a centred and scrimmed layer over the body, and
 * draws into it.
 *
 * `id` names the layer and `up` is whether the app still wants it. Returns false once it has
 * finished leaving, so a screen that has other things to draw over the dialog can tell - and
 * so the call reads the way every layer does:
 *
 *     (void)inkcell_fb_draw_dialog(state, &layout, &dialog, SCREEN_CONFIRM, nav->confirming);
 *
 * The scrim stops at `layout->nav_y`, not at the top of the panel. See the note above.
 *
 * Mutable state, unlike the const it used to take: a dialog arrives and leaves now, and where
 * it has got to is the one thing about it the frame itself remembers.
 */
bool inkcell_fb_draw_dialog(struct inkcell_backend_fb_state *state,
                            const struct inkcell_fb_layout *layout,
                            const struct inkcell_fb_dialog *dialog, uint32_t id, bool up);

/* ---- the snackbar -------------------------------------------------------------------------
 *
 * The transient notice: "Sent to BRVO", "Not connected", "Rebooting".
 *
 * It used to be the footer's second line, in the accent, sharing that row with the link
 * summary - which meant the two competed for one line and the notice won, so for four seconds
 * after every action the frame stopped saying whether there was a radio attached. Neither is
 * secondary to the other; they were only sharing a row because a row was what a notice had.
 *
 * So it is a container of its own, over the body rather than in the chrome, and it arrives by
 * sliding up from below the panel and leaves the same way. That is the whole point of the
 * shape: a notice that appears in place has to be *noticed* to be read, and on a handheld the
 * eye is usually somewhere else at the moment it appears. Movement is what gets it back.
 *
 * Everything about it that cannot come from the snapshot - where it has slid to, what it says
 * while it slides back out after the store has forgotten it - is remembered on the state, next
 * to the animation table and for the same reason. See struct inkcell_backend_fb_state.
 *
 * Drawn last of everything on the frame, because it is over the UI rather than in it.
 */
struct inkcell_fb_snackbar {
    /* The notice; NULL or empty means there is none, which is also what makes one already on
       screen start sliding back out. */
    const char *text;
    /*
     * When the store means to take it away, which the widget uses as the notice's *identity*
     * rather than as a deadline - expiry is the nav's business and it has already done it by
     * the time the text arrives empty.
     *
     * It is here because two notices can read the same: pressing send twice with no radio
     * attached raises "Not connected" twice, and the second one has to arrive rather than sit
     * there looking like the first never left. A deadline moves every time a notice is raised,
     * so it tells them apart when the words cannot.
     */
    uint64_t until_ms;
};

/*
 * Draws the notice, advancing it towards its resting place - or off the bottom of the panel
 * when there is none left to show. Nothing is drawn once it has gone.
 *
 * Mutable state, like every animated component here: the position it is coming from and the
 * words it is still carrying both live on the state.
 */
void inkcell_fb_draw_snackbar(struct inkcell_backend_fb_state *state,
                              const struct inkcell_fb_layout *layout,
                              const struct inkcell_fb_snackbar *bar);

/* ---- the menu --------------------------------------------------------------------------------
 *
 * A short column of verbs on a raised panel, hanging off the thing that opened it.
 *
 * The shape that had nowhere to live. A screen here that wanted one had to become a screen
 * instead - a whole page with an app bar and a back affordance, for four verbs - because the
 * alternative was a widget answering "where does this go", "how does it arrive", "what is
 * under it" and "who owns B" entirely by itself. On a layer it is a measure and a loop.
 *
 * What makes it a menu rather than a short list is the *anchor*. A list is the screen's
 * content; a menu is about the one control it came out of, so the layer lines its leading edge
 * up with that control's and flips it above when there is no room below - see
 * INKCELL_OVERLAY_ANCHOR. The reader's eye is already on the button they pressed, which is
 * where the panel appears.
 *
 * Rows rather than buttons, and a check rather than a fill on the chosen one: a menu is a list
 * of things that can be done, and a row of pills would be a toolbar. Each item registers a
 * focus box, so the d-pad walks it and inkcell_focus_step() answers at its ends - the same
 * mechanism a list uses, for the same reason.
 */

struct inkcell_fb_menu_item {
    /* The verb. An item with no words is skipped, which is what lets a screen build a fixed
       array and leave the entries it has nothing for empty. */
    const char *label;
    /* The leading symbol. INKCELL_ICON_NONE leaves the slot empty rather than closing it up: a
       menu where some rows have an icon and some do not is a menu whose words start in two
       columns, which is the list's own rule (INKCELL_FB_LEADING_ICON) applied here. */
    enum inkcell_icon icon;
    /* A trailing check: this is the option currently in force. What a menu that is a *choice*
       draws, as against one that is a set of verbs. */
    bool checked;
    /* What the row means. A tone, never a colour - and it colours the words rather than the
       row, so the one verb that destroys something is findable and a menu of them is not a
       wall. That is the argument INKCELL_FB_LEADING_TONAL makes one header over. */
    enum inkcell_tone tone;
    /* Greyed, and registered nowhere - so the cursor steps over it rather than landing on a
       verb that does nothing. */
    bool disabled;
};

struct inkcell_fb_menu {
    const struct inkcell_fb_menu_item *items;
    size_t count;
    /* An optional heading over the first item, at the label scale: what this menu is *of*.
       NULL for a menu whose verbs speak for themselves, which is most of them. */
    const char *heading;
    /* Which item the cursor is on. Out of range highlights nothing, which is what a menu
       steered entirely by the focus map passes. */
    uint32_t cursor;
    /* What the d-pad calls the rows: item `i` is `focus_base + i`, the numbering
       inkcell_fb_list_focus() uses and for its reason - a screen's menu cursor is already an
       item index. INKCELL_FOCUS_NONE registers nothing. */
    uint32_t focus_base;
};

/*
 * The box this menu wants: wide enough for its longest row, tall enough for all of them.
 *
 * Measured rather than stated, because a menu is a handful of verbs and their lengths are the
 * only thing that decides how wide it should be. Asked before the layer is opened, like the
 * dialog's height and for the same reason: a layer is handed a size, it does not invent one.
 * `max_w` is what it may not exceed.
 */
struct inkcell_fb_rect inkcell_fb_menu_box(const struct inkcell_backend_fb_state *state,
                                           const struct inkcell_fb_menu *menu, int max_w);

/* Draws it into `box` - the one a layer handed back. */
void inkcell_fb_draw_menu(const struct inkcell_backend_fb_state *state, struct inkcell_fb_rect box,
                          const struct inkcell_fb_menu *menu);

/* ---- the bottom sheet ------------------------------------------------------------------------
 *
 * A panel that comes up from the bottom edge and holds whatever the screen puts in it.
 *
 * The other shape that had nowhere to live, and the one the dialog kept being used for. A
 * dialog asks a question with two answers; a sheet is for everything else that should not cost
 * a screen - a filter, a picker, the details of the row under the cursor. It comes up from the
 * edge rather than appearing in the middle because that is what says it is *over* this screen
 * rather than a new one: what it came from is still there behind it, dimmed, and going back
 * means putting the sheet down rather than navigating.
 *
 * The grabber at the top is the one piece of furniture it draws that is not content. It is not
 * pressable - there is no touch on this device - and it is drawn anyway, because it is the
 * mark every platform has trained readers to read as "this came up, and it can go back down".
 * A sheet without it is a panel that looks as though it had always been there.
 *
 * The content is the screen's. inkcell_fb_draw_sheet() paints the surface, the grabber and the
 * title and hands back the rectangle underneath them - so a sheet holding a list is a list
 * drawn into that rectangle, and a sheet holding a form is a form.
 */
struct inkcell_fb_sheet {
    /* The heading, at INKCELL_TYPE_TITLE. NULL for a sheet whose content names itself. */
    const char *title;
    /* A fact about the sheet against its trailing edge, in the dim ink: a count, a unit. NULL
       for none. */
    const char *detail;
};

/*
 * How tall a sheet holding `content_h` pixels of content has to be: the content plus the
 * grabber, the title and the insets.
 *
 * Asked before the layer is opened, because the layer is handed a size. The arithmetic is here
 * rather than at the call site for the reason every measure in this toolkit is - a screen that
 * worked out a sheet's chrome would disagree with the thing drawing it the moment either
 * changed.
 */
int inkcell_fb_sheet_height(const struct inkcell_backend_fb_state *state,
                            const struct inkcell_fb_sheet *sheet, int content_h);

/*
 * Draws the surface, the grabber and the title into `box`, and hands back the rectangle left
 * for the content.
 *
 * A zero-height answer is a sheet with no room in it, which a caller should read as nothing to
 * draw rather than as a rectangle to draw into.
 */
struct inkcell_fb_rect inkcell_fb_draw_sheet(const struct inkcell_backend_fb_state *state,
                                             struct inkcell_fb_rect box,
                                             const struct inkcell_fb_sheet *sheet);

/* ---- the QR code ----------------------------------------------------------------------------
 *
 * A matrix of modules, drawn as squares in the one colour pair that does not follow the theme.
 *
 * It is the only widget here whose audience is not a person: what reads it is a phone camera
 * held by somebody standing next to the Brick, so every decision about it is about scanning
 * rather than about looking. Two of them are worth stating.
 *
 * **The module size is a whole number of pixels.** A code scaled to fill the room available
 * would put module boundaries between pixels, and a reader thresholding a photograph of that
 * finds edges where the code has none. So the scale is the largest integer that fits and the
 * code is centred in whatever is left over, which is why a smaller code may not fill its box.
 *
 * **The quiet zone is part of the code.** The standard asks for four modules of clear margin,
 * and a reader that cannot find it will not lock on however sharp the modules are - so the
 * margin is drawn in the code's own ground rather than left to whatever the screen behind it
 * happens to be.
 */
struct inkcell_fb_qr {
    /* The matrix. NULL, or one that failed to encode, draws nothing at all. */
    const struct inkcell_qr *code;
    /* The box to fit it in. The code is centred inside it and never drawn larger. */
    struct inkcell_fb_rect box;
};

/* The side of the square this code would actually occupy inside `box`, quiet zone included, or
   0 when there is no room for even one pixel per module. Asked before drawing by a screen that
   has to put something underneath it. */
int inkcell_fb_qr_side(const struct inkcell_fb_qr *qr);

/* Draws it centred in its box. Nothing is drawn when inkcell_fb_qr_side() is 0. */
void inkcell_fb_draw_qr(const struct inkcell_backend_fb_state *state,
                        const struct inkcell_fb_qr *qr);

#endif /* INKCELL_BACKENDS_FB_WIDGETS_OVERLAY_H */
