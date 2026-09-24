#ifndef INKCELL_THEME_H
#define INKCELL_THEME_H

/*
 * Themes: the colours, the metrics and the font a frame is drawn with.
 *
 * A renderer never names a colour. It names a *role* - "the ground", "the fill under the
 * cursor", "text on an accent fill" - or a *tone*, which is the same idea one level up: what a
 * piece of text means, rather than what colour it is. The theme answers, so a new look is a
 * table in src/ui/theme/theme.c and nothing else, and every screen switches together because none
 * of them holds an opinion of its own.
 *
 * The same goes for geometry. The margin, the glyph multiplier, how much smaller chrome text
 * is, how wide a bubble may grow, when the label column gives way on a narrow body: those were
 * literals scattered through the drawing code, and a theme that wanted a roomier layout had to
 * find all of them. They are `struct inkcell_metrics` now.
 *
 * Nothing here knows about the framebuffer. That is deliberate: tones and metrics are the
 * vocabulary of *the UI*, and a second backend that grows colour (a colour CLI, an SDL window
 * on a desktop) should speak it too rather than inventing a parallel one.
 */

#include "inkcell/ui/font.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct inkcell_rgb {
    uint8_t r, g, b;
};

/*
 * Every colour a frame can use, named for the job it does.
 *
 * There are two kinds of entry here, and the difference is the whole shape of the palette.
 *
 * The *surfaces* and the *text* are the neutral spine: the ground, the tiers above it, the ink
 * that goes on each. There is one of each and nothing chooses between them.
 *
 * The *families* are the six colours that carry meaning - primary, secondary, tertiary,
 * success, warning, error - and each is four roles rather than one:
 *
 *   PRIMARY               the saturated colour: ink on the ground, or a fill that must be found
 *   ON_PRIMARY            ink for that fill
 *   PRIMARY_CONTAINER     the same colour held back far enough to sit behind text
 *   ON_PRIMARY_CONTAINER  ink for that
 *
 * Four rather than one because a colour used as ink and a colour used as a fill are not the
 * same colour, and a fill the size of a badge and a fill the size of a tab are not either. A
 * family that stated only its saturated value would leave every widget to work the other three
 * out - which is what a chat bubble, a snackbar and an armed row each did separately, in four
 * themes, before this. See enum inkcell_slot.
 *
 * The four slots of a family are contiguous and in slot order, so inkcell_family_role() is
 * arithmetic rather than a switch; the _Static_assert below the slot enum pins that.
 *
 * Adding a role is how a new visual element gets themed. Adding it means every theme answers
 * for it, which is the point - a role that only one theme fills is a hardcoded colour with
 * extra steps.
 */
enum inkcell_color {
    INKCELL_COLOR_BG = 0, /* the ground the whole frame is cleared to */
    /*
     * The three surface tiers, lowest first.
     *
     * A surface says how far a thing is from the ground, and the whole point of having more
     * than one is that a panel over a panel has to be tellable from it. The Brick's display
     * engine composites fb0 against its own background layer rather than against what we have
     * already drawn, so there is no alpha to shade with and no drop shadow to cast - the
     * distance is carried by the fill alone, which is how Material's tonal elevation works and
     * why it survives a light palette as well as a dark one.
     *
     * Lower is nearer the ground, so on a dark theme the tiers get lighter as they rise and on
     * a light one they get darker. A theme states all three and nothing that draws knows which
     * direction its palette went.
     */
    INKCELL_COLOR_SURFACE_LOW,    /* recessed chrome: the ground the tab strip sits on */
    INKCELL_COLOR_SURFACE,        /* a panel on the ground: a card */
    INKCELL_COLOR_SURFACE_HIGH,   /* raised over the body: the draft box, an inbound bubble */
    INKCELL_COLOR_SURFACE_SEL,    /* the fill under the cursor, and a button at rest */
    INKCELL_COLOR_SURFACE_ACTIVE, /* a pressed button */
    /*
     * The surface from the other end of the palette: a light fill on a dark theme, a dark one
     * on a light theme.
     *
     * The three tiers above say how far a thing is from the ground, which works for anything
     * that belongs to the screen it is on. A transient notice does not - it is over the whole
     * UI, it was not there a second ago and will not be there in four - and no tier can say
     * that on a panel with no shadow and no alpha to raise it with. Inverting the ground can:
     * a fill that reads as "not part of this screen" is found before it is read, which is the
     * whole job of a snackbar.
     *
     * Material calls this pair inverse-surface and inverse-on-surface, for the same reason and
     * with the same one user.
     */
    INKCELL_COLOR_SURFACE_INVERSE,
    INKCELL_COLOR_TEXT,        /* body text; also the ink on an inbound bubble */
    INKCELL_COLOR_TEXT_DIM,    /* headings, secondary lines, anything not yet loaded */
    INKCELL_COLOR_TEXT_STRONG, /* unread, unsaved: the row the eye should land on */
    INKCELL_COLOR_TEXT_ON_SEL, /* text drawn on SURFACE_SEL or SURFACE_ACTIVE */
    /* The secondary line of a selected item: a timestamp, a message preview. TEXT_DIM is
       chosen against the ground and says nothing about a fill over it, so a row that carries
       two tiers of text under the cursor - which the conversation list does - needs its own
       quiet colour rather than flattening to TEXT_ON_SEL. */
    INKCELL_COLOR_TEXT_ON_SEL_DIM,
    INKCELL_COLOR_TEXT_ON_INVERSE, /* text drawn on SURFACE_INVERSE */

    /* ---- the families -------------------------------------------------------------------
     *
     * Six colours that mean something, four roles each, in the order enum inkcell_family and
     * enum inkcell_slot name them. Keep them contiguous and keep the slots in order: the
     * lookup is arithmetic.
     */

    /* Titles, the current target, the compose destination, the active tab. The brand colour:
       what the eye is meant to follow through the app when nothing is wrong. */
    INKCELL_COLOR_PRIMARY,
    INKCELL_COLOR_ON_PRIMARY,
    INKCELL_COLOR_PRIMARY_CONTAINER,
    INKCELL_COLOR_ON_PRIMARY_CONTAINER,
    /* The second voice: our own messages, and anything that is "the other side" of a pair
       without being better or worse than it. An outbound bubble is the whole reason this
       family exists - "you are the blue one" is a convention every messenger has trained
       everybody on, and it is not the primary because a transcript full of the brand colour
       is a transcript nobody can find a title in. */
    INKCELL_COLOR_SECONDARY,
    INKCELL_COLOR_ON_SECONDARY,
    INKCELL_COLOR_SECONDARY_CONTAINER,
    INKCELL_COLOR_ON_SECONDARY_CONTAINER,
    /* In flight: a draft not sent, an edit not saved, a request the radio has not answered.
       Neither good nor bad nor the thing you are looking at - which is three meanings the
       primary used to carry at once, so a pending row and the screen's own title were the
       same colour. */
    INKCELL_COLOR_TERTIARY,
    INKCELL_COLOR_ON_TERTIARY,
    INKCELL_COLOR_TERTIARY_CONTAINER,
    INKCELL_COLOR_ON_TERTIARY_CONTAINER,
    INKCELL_COLOR_SUCCESS, /* connected, healthy, delivered */
    INKCELL_COLOR_ON_SUCCESS,
    INKCELL_COLOR_SUCCESS_CONTAINER,
    INKCELL_COLOR_ON_SUCCESS_CONTAINER,
    /* Not wrong yet: a mesh over its airtime budget, a radio low on heap, packets going
       missing. It is a family of its own and not the primary held sideways, which is what it
       was - and the airtime meter's warning band was therefore, by construction, the same
       colour as the "this is the current channel" marker beside it. */
    INKCELL_COLOR_WARNING,
    INKCELL_COLOR_ON_WARNING,
    INKCELL_COLOR_WARNING_CONTAINER,
    INKCELL_COLOR_ON_WARNING_CONTAINER,
    /* Disconnected, failed, armed to destroy something. The container is the failed bubble's
       fill, which used to be a role of its own restated in every theme. */
    INKCELL_COLOR_ERROR,
    INKCELL_COLOR_ON_ERROR,
    INKCELL_COLOR_ERROR_CONTAINER,
    INKCELL_COLOR_ON_ERROR_CONTAINER,

    /* ---- furniture ---------------------------------------------------------------------- */

    INKCELL_COLOR_RULE,        /* hairline separators */
    INKCELL_COLOR_RULE_STRONG, /* the rule under the tab strip */
    /*
     * The edge of a container, which is not the same job as a separator.
     *
     * A rule divides content that is already on one surface; an outline is what says a surface
     * is there at all. They were one role while a card was the only thing with an edge, and
     * they part company the moment a second container wants an edge that reads against a
     * different fill: a rule is tuned to disappear politely, an outline has to be found.
     */
    INKCELL_COLOR_OUTLINE,
    /*
     * The empty part of a meter: the container a fill is read against.
     *
     * A role of its own because it is the one colour with a contract in *both* directions, and
     * no existing role can hold both ends of it. It has to be findable on the two grounds a bar
     * is drawn on - the body and a card - and every fill a meter can take has to be tellable
     * from it. SURFACE_SEL was the obvious borrow and fails the first half on the light theme,
     * where the cursor fill is within 1.2:1 of a card; OUTLINE passes that and fails the second
     * half on the contrast theme, where it is the same near-white as the success colour. A
     * track borrowed from a role tuned for something else is a bar that disappears on whichever
     * theme nobody happened to open.
     *
     * Quiet is the goal, not contrast: this is the part of the widget that is meant to recede,
     * and inkcell_theme_validate() is what stops quiet becoming absent.
     */
    INKCELL_COLOR_METER_TRACK,
    /*
     * The two the QR code is drawn in, and the only pair here that is the *same on every theme*.
     *
     * Not an oversight and not a shortcut past this file's whole argument. A code on this panel
     * is not read by a person: it is read by a phone camera across the room, and what decides
     * whether that works is a dark-on-light square with a light margin round it. Several
     * scanners - the one built into iOS among them - will not read an inverted code at all, so
     * a code that followed the dark theme would be a code half the people it is shown to cannot
     * scan.
     *
     * So the choice still belongs here rather than in the renderer, which is the rule doing its
     * job: a screen names a role, this file answers, and the answer for these two happens not to
     * vary. A theme that wanted a warmer white for the margin could still have one.
     */
    INKCELL_COLOR_CODE,
    INKCELL_COLOR_CODE_GROUND,
    /*
     * The scrim: what the frame behind a modal is mixed towards.
     *
     * The surface tiers a few roles up carry the note that this panel has no alpha to raise a
     * thing with, so distance is spent on the fill instead. That is true of anything *drawn* -
     * the display engine composites fb0 against its own layer, so there is nothing behind the
     * page to blend with. It was read for years as "and therefore there is no scrim", which
     * does not follow: a scrim does not blend against what is behind the page, it blends
     * against what is already *on* it, and the frame under a dialog was drawn by us, into our
     * own buffer, a few microseconds earlier. inkcell_fb_scrim_rect() reads it back and mixes.
     *
     * A role rather than "darken by a third", because which way is *away* is a fact about the
     * palette. A dark theme scrims towards black and a light one very nearly does too - what a
     * light theme must not do is scrim towards its own ground, which would make the dimmed
     * content and the panel behind it the same colour and turn the scrim into an eraser. How
     * far it goes is `scrim_pct` on the metrics, so a palette can say both halves.
     */
    INKCELL_COLOR_SCRIM,
    INKCELL_COLOR_COUNT
};

/*
 * The six colours that mean something.
 *
 * A widget takes one of these rather than a fill and an ink, and asks the theme for the slot it
 * needs. That is what lets one `inkcell_fb_draw_button` be the plain button, the destructive
 * confirm and the "connected" pill without three branches inside it - and what stops the fourth
 * caller inventing a fill nothing has checked the label against.
 */
enum inkcell_family {
    INKCELL_FAMILY_PRIMARY = 0,
    INKCELL_FAMILY_SECONDARY,
    INKCELL_FAMILY_TERTIARY,
    INKCELL_FAMILY_SUCCESS,
    INKCELL_FAMILY_WARNING,
    INKCELL_FAMILY_ERROR,
    INKCELL_FAMILY_COUNT
};

/*
 * Which of a family's four colours is wanted.
 *
 * BASE is the saturated one: correct as ink on the ground, and as a fill only where the fill is
 * small enough to be a mark rather than a field - a badge, a meter, a switch track. CONTAINER
 * is the same colour held back until text can sit on it, which is what anything the size of a
 * tab, a chip or a chat bubble wants. Each comes with the ink that has been checked against it,
 * and the two always travel together: a fill taken from one slot and an ink from another is a
 * pair inkcell_theme_validate() never looked at.
 */
enum inkcell_slot {
    INKCELL_SLOT_BASE = 0,
    INKCELL_SLOT_ON_BASE,
    INKCELL_SLOT_CONTAINER,
    INKCELL_SLOT_ON_CONTAINER,
    INKCELL_SLOT_COUNT
};

/*
 * The families are laid out family-major, slot-minor, so the lookup is a multiply. Pinned here
 * rather than trusted: reordering the enum above would otherwise silently repaint the UI in the
 * wrong colours rather than failing to build.
 *
 * Spelled through a macro because of the extern "C" block above. That block says this header may
 * be included from C++, and `_Static_assert` is a C keyword g++ rejects outright - C++ has
 * spelled it `static_assert` since C++11 and C only since C23, so there is no one name that
 * works in both without the bridge. The comparisons cast to int for the same reason: arithmetic
 * between two unscoped enums is deprecated in C++20.
 */
#ifdef __cplusplus
#define INKCELL_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#else
#define INKCELL_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#endif

INKCELL_STATIC_ASSERT((int)INKCELL_COLOR_ON_PRIMARY ==
                          (int)INKCELL_COLOR_PRIMARY + (int)INKCELL_SLOT_ON_BASE,
                      "family slots must be contiguous and in slot order");
INKCELL_STATIC_ASSERT((int)INKCELL_COLOR_SECONDARY ==
                          (int)INKCELL_COLOR_PRIMARY + (int)INKCELL_SLOT_COUNT,
                      "families must be contiguous");
INKCELL_STATIC_ASSERT((int)INKCELL_COLOR_ON_ERROR_CONTAINER ==
                          (int)INKCELL_COLOR_PRIMARY +
                              ((int)INKCELL_FAMILY_COUNT * (int)INKCELL_SLOT_COUNT) - 1,
                      "every family must state all four slots");

/*
 * How a control is being interacted with, which is a *modifier* on a colour rather than a
 * colour of its own.
 *
 * Material calls this a state layer: the resting fill with its own ink mixed into it a little,
 * so "the cursor is on this" is one operation applied to whatever the thing is already painted
 * in. Before this, every element that could be selected stated a second colour - the chat
 * bubbles alone cost two extra roles in four themes - and each of those was a value somebody
 * had matched by eye to the one above it.
 *
 * Mixing the *ink* in rather than white or black is what makes one rule work on a dark ground
 * and a light one: the layer always moves the fill towards the thing written on it, so it
 * lightens on dark and darkens on light without either being spelled out.
 */
enum inkcell_state {
    INKCELL_STATE_REST = 0,
    INKCELL_STATE_SELECTED, /* the cursor is on it */
    INKCELL_STATE_ACTIVE,   /* pressed */
    INKCELL_STATE_COUNT
};

/*
 * What a piece of content *is*, rather than which colour to draw it.
 *
 * This is the vocabulary screens speak. It is a level above the roles above: a screen says
 * "this row is bad news" and the theme decides both which role that maps to and what colour
 * the role holds. Same reason a stylesheet has a token called `danger` instead of a hex.
 *
 * A tone is always the colour a piece of *text* takes. The six that name a family resolve to
 * that family's BASE, because ink on the ground is exactly what BASE is for; a screen that
 * wants the family as a fill hands the family itself to a widget and lets the widget ask for
 * the container and the ink that goes with it. Splitting it that way is what keeps a fill and
 * the label on it from being chosen in two different places.
 */
enum inkcell_tone {
    /* The neutral three: what a line is, when what it is has nothing to do with meaning. */
    INKCELL_TONE_NORMAL = 0,
    INKCELL_TONE_DIM,
    INKCELL_TONE_STRONG,
    /* One per family, in the same order, so inkcell_tone_family() is a subtraction. A screen
       that wants a family's colour as *ink* names the tone; one that wants it as a *fill* names
       the family and the slot. Keep these contiguous - the _Static_assert below says so. */
    INKCELL_TONE_PRIMARY,
    INKCELL_TONE_SECONDARY,
    INKCELL_TONE_TERTIARY,
    INKCELL_TONE_SUCCESS,
    INKCELL_TONE_WARNING,
    INKCELL_TONE_ERROR,
    INKCELL_TONE_COUNT
};

INKCELL_STATIC_ASSERT((int)INKCELL_TONE_COUNT - (int)INKCELL_TONE_PRIMARY ==
                          (int)INKCELL_FAMILY_COUNT,
                      "one tone per family, contiguous, in family order");

/* The glyph multipliers the UI will accept, whatever a theme asks for. The lower bound is
   legibility on the Brick's 3.2" panel; the upper is the buffers sized off it. */
#define INKCELL_SCALE_MIN INKCELL_SCALE(2)
#define INKCELL_SCALE_MAX INKCELL_SCALE(6)

/*
 * Avatar tints: the fills behind a conversation's initials.
 *
 * A palette rather than a role because the whole point of an avatar colour is that two of them
 * differ - it is what lets the eye find a thread in a list without reading a word of it, the
 * way every messenger's coloured discs do. Which tint a conversation gets is a hash of its
 * identity, so it is stable across restarts and across a rename.
 *
 * A theme may fill fewer than the maximum (`avatar_count`); the high-contrast one deliberately
 * offers two, because a palette of six hues is the opposite of what that theme is for.
 *
 * The initials are drawn in INKCELL_COLOR_BG - a tint is a fill punched out of the ground - so
 * every tint owes the ground the body-text contrast, and inkcell_theme_validate() holds it to
 * that. Same contract a family's BASE/ON_BASE pair has, one level up.
 */
#define INKCELL_AVATAR_TINTS 6U

/*
 * The series palette: fills whose whole meaning is that they are not each other.
 *
 * Every other colour in this file answers "what kind of thing is this" - a warning, a heading,
 * the edge of a container. A chart with three parts in it needs the opposite: colours that
 * carry no meaning at all beyond *which part*, so that the parts can be told apart without any
 * of them being read as good news or bad. The families cannot do that job. Three of them mean
 * a status outright, and the three that do not are not reliably distinct: on the high-contrast
 * theme the primary, the secondary and the tertiary are all the same yellow, so a bar drawn in
 * the three of them there is one undivided yellow block claiming the mesh is made of one thing.
 * A picture cannot be wrong quietly, and that is the quietest a picture gets.
 *
 * The avatar palette next door is the near relative and is still not this. A tint is picked by
 * a *hash*, so what it owes is variety and nothing else - two conversations landing on one
 * colour costs the eye a moment. A series colour is picked by *position*, so the sequence is
 * the contract: slice 0 is the same colour on every frame, on every theme and in the legend,
 * and a theme that offered fewer of them would leave a chart with a part it could not draw.
 * Which is why every theme states all four and there is no `series_count` to match
 * `avatar_count`.
 *
 * Ordered by prominence, not by hue or by lightness: the first is the one to reach for when
 * only one is needed and the one the largest part of a whole should take. Which end of the
 * lightness range that is, is the theme's business - the darkest ink on paper and the palest
 * tint on a dark ground are the same decision read twice.
 *
 * Four. It is what three-part composition leaves room to grow into once, and it is as far as
 * the contract below can be carried on this panel: the entries must be separable from each
 * other *and* from the two grounds a chart is drawn on, and separability here is measured in
 * luminance rather than in hue, because hue is the cue that goes first in sunlight and the one
 * the colour-blind theme exists because some readers do not have. A fifth would have to be
 * squeezed between two rungs of a ladder that is already as long as the panel's range allows.
 *
 * A series colour is only ever a *fill* - never an ink, never a text colour. A legend names its
 * part in the row's own words beside a swatch, so nothing is ever written in one of these and
 * none of them owes anybody the 4.5:1 that text does. inkcell_theme_validate() holds them to
 * the meter's 1.4:1 instead, in both directions: against the grounds, and against each other.
 */
#define INKCELL_SERIES_COLORS 4U

/*
 * The shape scale: how round a container's corners are, by what kind of container it is.
 *
 * A renderer no more names a radius than it names a colour. It names a shape - "this is a
 * pill", "this is a panel" - and the theme answers, which is what makes a squarer or a rounder
 * look one table entry instead of a hunt through every fill in the backend. It is the same
 * move as `enum inkcell_tone`, one axis over.
 *
 * The steps are in *glyph-scale multiples*, not pixels, for the reason `card_pad` is: a theme
 * asking for bigger text gets proportionally rounder corners, so the whole frame stays in
 * proportion rather than the corners staying put while everything around them grows.
 */
enum inkcell_shape {
    INKCELL_SHAPE_NONE = 0, /* square: a rule, a bar, anything that meets an edge */
    INKCELL_SHAPE_SM,       /* a row highlight, a keycap - a shape the eye reads as a rectangle */
    INKCELL_SHAPE_MD,       /* a panel: a card, a text field */
    INKCELL_SHAPE_LG,       /* a surface over the body: a dialog, a sheet */
    /* As round as the shorter side allows - a capsule, or a circle when it is square. A badge,
       a chip, an avatar. Not a step count, so it has no entry in the table below: "half of
       whatever this turns out to be" is not a length a theme can state in advance. */
    INKCELL_SHAPE_FULL,
    INKCELL_SHAPE_COUNT
};

/*
 * How long a thing takes to move.
 *
 * Four rather than a number at each call site, for the reason the colour families are six roles
 * rather than a hex per widget: what a duration means is "this kind of movement", and a theme
 * that wants a calmer or a snappier feel should be able to say so once. The curve is still named
 * at the call site (enum inkcell_ease) because every animation here currently wants the same one
 * - a token pairing duration with a curve is the right shape the first time two of them differ.
 */
enum inkcell_motion {
    /* A control acknowledging a press, and anything leaving. Exits are shorter than entrances
       because a thing on its way out has nothing left to say. The switch knob, the snackbar
       going away. */
    INKCELL_MOTION_SHORT = 0,
    /* Something arriving that was not there: the snackbar coming up. */
    INKCELL_MOTION_MEDIUM,
    /* A value easing to a new reading rather than jumping to it - long enough that a bar
       sampled once a second reads as movement instead of as a series of positions. */
    INKCELL_MOTION_LONG,
    /* One pass of something that has no end: the pill travelling an indeterminate meter. Not a
       transition at all, which is why it is an order of magnitude longer than the rest. */
    INKCELL_MOTION_LOOP,
    INKCELL_MOTION_COUNT
};

/*
 * The spacing scale: the gaps between things, in half-steps of the glyph scale.
 *
 * These were literals - `scale / 2` for the inset above a row fill, `2 * scale` for a button's
 * padding, `line / 2` for the gap under a dialog's heading. Each was right; collectively they
 * were the same thing the colour literals were before theme.c, and a theme that wanted a denser
 * or a roomier layout could move `margin` and watch half the spacing on screen stay where it
 * was, because it had been derived from the glyph scale instead.
 *
 * Half-steps rather than whole ones because half a step is a real and much-used gap here: at
 * the device's scale a step is four pixels, and the hairline inset that stops a row fill from
 * touching the text above it is two. A table in whole steps could not say that without either
 * rounding it away or doubling every other entry.
 *
 * This scale is glyph-relative on purpose, and so is *not* where a panel inset belongs. The
 * half-margin the row fill and the dialog panel sit in tracks the body margin, not the text
 * size - see inkcell_fb_gutter().
 */
enum inkcell_space {
    INKCELL_SPACE_NONE = 0,
    INKCELL_SPACE_XS, /* half a step: the inset above a fill, a hairline's thickness */
    INKCELL_SPACE_SM, /* one step: the gap under a title, a meter's clearance */
    INKCELL_SPACE_MD, /* two steps: a button's padding, a keyboard cell's */
    INKCELL_SPACE_LG, /* three steps: the room a dialog leaves around its actions */
    INKCELL_SPACE_COUNT
};

/*
 * The type scale: how big a thing is drawn, named for what it *is*.
 *
 * The theme answered for colour, for shape and for spacing, and for type it had two numbers -
 * the body scale and how many steps smaller chrome was. Those two split *content from chrome*,
 * which is not a hierarchy: a screen title was drawn at the body scale, exactly the size of the
 * list rows beneath it, and told apart from them by colour alone. There was no size a renderer
 * could reach for that meant "more important", so colour was carrying the whole job.
 *
 * Held as an offset from the body scale rather than as an absolute multiplier, because the body
 * scale is not the theme's alone: a preference and <PREFIX>_SCALE both override it at
 * runtime, and a table of absolutes would silently stop being a scale the moment somebody asked
 * for larger text. An offset keeps the *relationship*, which is what a type scale is.
 *
 * Seven roles, where Material names fifteen and iOS eleven - and the reason there were three
 * is no longer arithmetic. It was: a scale was a whole multiplier over the font's cell, so the
 * range [2, 6] held five sizes and a vocabulary with more roles than that would have been a set
 * of synonyms. A scale counts quarter steps now (see INKCELL_SCALE_UNIT), which puts seventeen
 * sizes in the same range and about a pixel of cap height between neighbours - enough to place
 * a display, a headline, a title, a body, a supporting body, a label and a caption without two
 * of them landing together.
 *
 * Seven rather than fifteen because a role nobody can name is a role nobody reaches for. Each
 * of these is a sentence a screen can actually say about a line - "this is the reading", "this
 * is the line under the line", "this is a timestamp" - and a vocabulary is only worth having
 * while every word in it is one somebody would choose.
 *
 * A role is four facets, not one: how big, how heavy, how far apart the lines sit and how far
 * apart the letters do. Size alone is what a "type scale" usually means and it is not enough -
 * text set two steps up at the body's own letter-spacing reads loose, a caption set a step and
 * a half down at the body's reads cramped, and a wrapping paragraph at a heading's line height
 * reads as a list. Those are the corrections a designer makes by hand at each size, which is
 * exactly the kind of decision this table exists to hold once. See struct inkcell_type_role.
 *
 * At the top of the range the roles still collapse towards each other, which is correct and is
 * what inkcell_theme_type_scale() clamping guarantees. They collapse in order: a display never
 * lands below a headline, and a caption never above a label.
 */
enum inkcell_type {
    /* The one thing a screen exists to show: the reading on a status panel, the figure in an
       empty state, the count something is about. Two whole steps up, and the one role set
       *tight* - a face's letter-spacing is drawn for text at reading size, and at twice that
       the same gaps read as a word coming apart. */
    INKCELL_TYPE_DISPLAY = 0,
    /* A heading with a screen behind it rather than a bar: the question a dialog asks, the name
       of the thing a detail screen is about. A step and a half, so it stands over a title
       without reaching for a display's presence. */
    INKCELL_TYPE_HEADLINE,
    /* A screen's own heading, and a card's. A whole step up, so a title is a title before it is
       read. */
    INKCELL_TYPE_TITLE,
    /* The default: list rows, card values, chat bubbles, anything read rather than glanced at. */
    INKCELL_TYPE_BODY,
    /* The line under the line: a row's second line, a field's current value, the sentence that
       explains a setting. Half a step down with a looser line, because supporting text is the
       part that wraps - and because the alternative, the body size in a dimmer colour, is
       exactly the hierarchy-by-palette this scale exists to replace. */
    INKCELL_TYPE_BODY_SOFT,
    /* Chrome and section labels: the tab strip, the footer, a card's heading, a counter. One
       step down - these are found, not read, and at the body scale each costs a whole row on a
       panel that has fifteen. */
    INKCELL_TYPE_LABEL,
    /* Metadata beside something else: a timestamp, a unit, a signal reading, "3 of 12". A step
       and a half down, set loose so that it survives being that small, and in tabular figures
       because what is written in this role is mostly numbers that change while the reader is
       looking at them. */
    INKCELL_TYPE_CAPTION,
    INKCELL_TYPE_COUNT
};

/*
 * How one role is set: the row of the type scale, as a theme states it.
 *
 * Four facets rather than a size, because the four are one decision. How far a title stands
 * above the body is a question about size *and* weight; whether a caption is legible at a step
 * and a half down is a question about its letter-spacing; whether a paragraph of supporting
 * text reads as prose or as a list is a question about its line height. A theme that answered
 * one of them here and left the rest in the renderers would be a theme that cannot actually
 * restyle its own typography - which is the shape the two-array version had.
 *
 * Every facet is stated *relative* to something the theme does not own, so that the table stays
 * true when the thing underneath it moves. The size is an offset from the body scale, which a
 * preference and <PREFIX>_SCALE both override at runtime. The tracking is in scale units, so it
 * grows with the text. The line height is a percentage of the font's own line, so a face with
 * different proportions brings them with it rather than being overridden by a pixel count
 * written for a different face. A table of absolutes would silently stop being a scale the
 * moment somebody asked for larger text or another font.
 */
struct inkcell_type_role {
    /* Size: scale units from the body scale. Signed - a title is above the body and a label
       below it. INKCELL_SCALE(1) is a whole step, so a half-step role is INKCELL_SCALE(1) / 2
       and not a rounding accident. */
    int8_t offset;
    /* Weight, as an enum inkcell_weight. A theme wanting a flat look sets every role REGULAR. */
    uint8_t weight;
    /*
     * Tracking: extra space after every cell, in scale units, added to the face's own advance.
     *
     * Signed, and the sign is the whole point: large text wants less air between letters than
     * the face was drawn with and small text wants more, which is why a display is negative
     * here and a caption positive. Zero is the face exactly as it was drawn, which is what
     * every role had before this field existed.
     *
     * In scale units rather than pixels so that it tracks the text: a quarter step at the body
     * scale is one pixel on the Brick's panel and two at the top of the range, which is the
     * same *proportion* of the letter it is spacing.
     */
    int8_t tracking;
    /*
     * Line height, as a percentage of the font's own line advance. 0 reads as 100, so a theme
     * that states nothing draws exactly the lines it drew before.
     *
     * A percentage rather than pixels because the font already answers this question once - it
     * declares a cell and a line gap - and a theme restating it in pixels would be a theme that
     * silently overrides the next font it is pointed at. What a theme legitimately has an
     * opinion about is the *ratio*: a heading set at the body's leading looks lost in its own
     * row, and a wrapping paragraph set at a heading's looks like a list of lines.
     */
    uint8_t line_pct;
    /*
     * Whether digits step a common advance - tabular figures.
     *
     * The UI face is proportional and its digits are drawn to fit: '1' steps twelve master
     * columns where '4' steps nineteen. That is correct inside a word and wrong in a column of
     * readings, where it means a value re-rendered once a second moves sideways under the eye,
     * and two numbers on consecutive rows do not line up at the decimal point. A role that is
     * mostly numbers asks for the widest digit advance for all ten and centres each glyph in
     * it, which is what a face's own tabular figures would do if this one had a second set.
     */
    bool tabular;
};

/*
 * A role resolved against a body scale: what a renderer actually draws with.
 *
 * The role table is relative and the scale is not known until a frame is being drawn, so a
 * screen asks inkcell_theme_type_style() once and hands the answer to every measurement and
 * every draw of that run. Passing the struct rather than the four numbers is what keeps them
 * together: a line measured at a role's size but drawn at the body's tracking is a line that
 * does not fit the box its own layout reserved for it, and that is a bug no signature taking
 * `int scale` can refuse to express.
 */
struct inkcell_type_style {
    /* The glyph multiplier, in scale units, already clamped into the drawable range. */
    int scale;
    enum inkcell_weight weight;
    /* As the theme stated it - scale units per cell, not pixels. inkcell_type_tracking_px()
       does the conversion, and the fb layer does it once per run. */
    int tracking;
    uint8_t line_pct;
    bool tabular;
};

/*
 * `style`'s tracking in pixels, which may legitimately be none.
 *
 * Scale units in and pixels out, so the divide is by INKCELL_SCALE_UNIT twice: once because the
 * tracking is stated in quarter steps like everything else on the role, and once because the
 * scale is. INKCELL_SCALE(1) of tracking is a whole step of air after every letter, which is
 * about a fifth of a cell and far more than any real face wants - the useful range is the
 * quarter and half steps the table actually uses.
 *
 * Not inkcell_scale_px(): that one never rounds a request away to nothing, because a hairline a
 * theme asked for is a hairline. Tracking is the opposite case - a quarter step of air at the
 * smallest drawable size *is* nothing, and rounding it up to a whole pixel would space a
 * caption further apart than a body. It truncates towards zero in both directions, so
 * tightening and loosening stay symmetric.
 */
static inline int inkcell_type_tracking_px(const struct inkcell_type_style *style) {
    if (style == NULL || style->tracking == 0 || style->scale <= 0) {
        return 0;
    }
    const int magnitude = (style->tracking < 0 ? -style->tracking : style->tracking) *
                          style->scale / (INKCELL_SCALE_UNIT * INKCELL_SCALE_UNIT);
    return style->tracking < 0 ? -magnitude : magnitude;
}

/*
 * `line` - a font's own line advance, in pixels - at `style`'s line height.
 *
 * Never nothing where the font asked for something: a line height of zero is a row of text
 * drawn on top of the one before it, and a theme that states an absurd percentage should get an
 * ugly frame rather than an unreadable one.
 */
static inline int inkcell_type_line_px(int line, const struct inkcell_type_style *style) {
    const int pct = (style == NULL || style->line_pct == 0U) ? 100 : (int)style->line_pct;
    if (line <= 0) {
        return line;
    }
    const int scaled = line * pct / 100;
    return scaled > 0 ? scaled : 1;
}

/*
 * The geometry a theme owns.
 *
 * Everything here was a literal in a drawing function once. They are theme data because a
 * "large text" theme is exactly this struct with a different `scale`, and a roomier one is a
 * different `margin` - neither should need a renderer to be touched.
 */
struct inkcell_metrics {
    uint8_t margin; /* pixels between the panel edge and the body */
    uint8_t scale;  /* glyph multiplier for body text */
    /*
     * The type scale: one row per role, indexed by enum inkcell_type.
     *
     * Read through inkcell_theme_type_style(), which resolves a row against a body scale, or
     * through inkcell_theme_type_scale() and inkcell_theme_type_weight() where only one facet
     * is wanted. What each field means and why it is stated the way it is, is on struct
     * inkcell_type_role.
     *
     * One array of rows rather than an array per facet, because a role is the unit a theme
     * restyles: the size, the weight, the tracking and the line height of a caption are one
     * paragraph of a design, and a theme that had to state them in four places four rows apart
     * is a theme whose caption is four edits away from being one.
     */
    struct inkcell_type_role type[INKCELL_TYPE_COUNT];
    uint8_t bubble_width_pct; /* how much of the body a chat bubble may fill */
    uint8_t field_label_cols; /* preferred label column, in cells */
    uint8_t narrow_cols;      /* a body narrower than this halves the label column */
    /* A card's inset, in glyph-scale steps rather than in pixels: a theme that asks for bigger
       text gets a proportionally roomier card, the same way the switch and the chat bubble
       already grow with the scale. */
    uint8_t card_pad;
    /*
     * How thick a meter's track is, in glyph-scale steps.
     *
     * A bar is the one widget here whose whole job is to be read without being looked at, so
     * its thickness is the difference between "a line the eye finds in a column of text" and
     * "an underline somebody forgot to remove". One step - four pixels at the device's scale -
     * is Material's 4dp track at the size this panel actually is, and it grows with the glyph
     * scale like everything else so a theme asking for bigger text gets a bar to match.
     */
    uint8_t meter_thickness;
    /*
     * How long a transition takes, in milliseconds, indexed by enum inkcell_motion. Read it
     * through inkcell_theme_motion().
     *
     * Here rather than beside each widget because a set of controls that each picked its own
     * duration is a set that moves as five things rather than as one system - which is what
     * these were before they were tokens: a switch at 140ms, a snackbar at 220 in and 150 out,
     * a meter at 320. Those numbers were independent guesses that happened to agree, and the
     * next widget would have been a sixth.
     *
     * Milliseconds rather than steps, unlike the shape scale: a duration is not a proportion of
     * anything on the panel, and a theme that draws bigger does not want to animate slower.
     */
    uint16_t motion_ms[INKCELL_MOTION_COUNT];
    /* The spacing scale, in *half* steps of the glyph scale, indexed by enum inkcell_space.
       Read through inkcell_theme_space(). All zeroes is a legal, entirely flush theme. */
    uint8_t space[INKCELL_SPACE_COUNT];
    /* The shape scale, in glyph-scale steps, indexed by enum inkcell_shape. Sized to stop
       before INKCELL_SHAPE_FULL because that one is not a step count - see the enum. Read it
       through inkcell_theme_radius(), which does the multiply and handles the pill. All zeroes
       is a legal, entirely square theme. */
    uint8_t shape[INKCELL_SHAPE_FULL];
    /*
     * How far towards INKCELL_COLOR_SCRIM the frame behind a modal is taken, as a percentage.
     *
     * A percentage rather than a colour because the scrim is a *mix* and the thing it is mixed
     * with is whatever happened to be on the panel - which is the point of it: the shapes and
     * the columns under a dialog stay legible as shapes and columns, dimmed, so the reader can
     * still see what the question is about. A flat fill over them would be a second screen.
     *
     * Material asks for 32%; a theme built for legibility rather than for looks wants more,
     * because on that palette the content under the scrim and the panel over it are both
     * near-maximum contrast and a gentle dim does not separate them. 0 is a legal theme with
     * no scrim at all, and the overlays still work - what is lost is the dimming, not the
     * modal.
     */
    uint8_t scrim_pct;
};

struct inkcell_theme {
    const char *id;      /* what <PREFIX>_THEME and a saved preference name it by */
    const char *name;    /* what a menu would show */
    const char *font_id; /* resolved against the font registry; NULL means the default */
    bool dark;           /* whether the ground is darker than the text; nothing draws
                            differently for it, but a caller choosing a default cares */
    struct inkcell_rgb colors[INKCELL_COLOR_COUNT];
    /* Avatar fills, read through inkcell_theme_avatar(). Only the first `avatar_count` are
       used, so a theme states its palette and leaves the rest zeroed. */
    struct inkcell_rgb avatars[INKCELL_AVATAR_TINTS];
    uint8_t avatar_count;
    /* The categorical fills a chart divides a whole with, read through inkcell_theme_series().
       All of them, in prominence order - unlike the avatars above, a theme does not get to
       state fewer, because a chart cannot draw fewer parts than it has. */
    struct inkcell_rgb series[INKCELL_SERIES_COLORS];
    struct inkcell_metrics metrics;
};

/* The registry. Index order is menu order, and `dark` is index 0. */
size_t inkcell_theme_count(void);
const struct inkcell_theme *inkcell_theme_at(size_t index);
const struct inkcell_theme *inkcell_theme_by_id(const char *id); /* NULL when unknown */
const struct inkcell_theme *inkcell_theme_default(void);

/*
 * The theme <PREFIX>_THEME names, or the default when it is unset or names nothing.
 *
 * An unknown name warns rather than failing: a typo in an environment variable should not
 * leave a handheld with no UI.
 */
const struct inkcell_theme *inkcell_theme_from_env(void);

/*
 * The same, but NULL when the environment did not name a theme this layer knows.
 *
 * The difference matters to whoever owns the choice: an environment variable is a deliberate
 * override, so when it names a theme the Settings row shows it as a fact rather than as a
 * switch that would spring back. Same shape as the updater's <PREFIX>_UPDATE_ALLOW_DEV.
 */
const struct inkcell_theme *inkcell_theme_env(void);

/* The theme `id` names, or the default. What a saved preference is read back through. */
const struct inkcell_theme *inkcell_theme_resolve(const char *id);

/* The next theme in registry order, wrapping - the Settings row steps through with this. */
const struct inkcell_theme *inkcell_theme_next(const struct inkcell_theme *theme);

/* Lookups. A NULL theme resolves to the default, so no caller has to guard. */
struct inkcell_rgb inkcell_theme_color(const struct inkcell_theme *theme, enum inkcell_color role);
struct inkcell_rgb inkcell_theme_tone(const struct inkcell_theme *theme, enum inkcell_tone tone);
enum inkcell_color inkcell_tone_role(enum inkcell_tone tone);

/* The role holding one slot of one family. Out-of-range arguments answer with the primary's
   equivalent rather than with nothing, for the reason an unknown <PREFIX>_THEME warns rather
   than failing: a bad enum should not leave a handheld with an unpainted widget. */
enum inkcell_color inkcell_family_role(enum inkcell_family family, enum inkcell_slot slot);
struct inkcell_rgb inkcell_theme_family(const struct inkcell_theme *theme,
                                        enum inkcell_family family, enum inkcell_slot slot);

/* The family a tone names, or INKCELL_FAMILY_COUNT for the three neutral tones - which is the
   answer a widget wants when it has to decide whether a tone can fill something at all. */
enum inkcell_family inkcell_tone_family(enum inkcell_tone tone);

/* The tone that draws in a family's BASE. The inverse of inkcell_tone_family(). */
enum inkcell_tone inkcell_family_tone(enum inkcell_family family);

/*
 * `fill` with `ink` mixed into it by however much `state` calls for: the state layer.
 *
 * REST returns `fill` untouched, so a caller can hand the state straight through rather than
 * branching on it. The percentages are Material's, near enough - a selected element is a
 * visible step and a pressed one is a step further - and they are small on purpose: the layer
 * has to be findable without taking the fill far enough that the ink checked against it stops
 * being readable, which inkcell_theme_validate() then confirms for every pair that is drawn
 * this way.
 */
struct inkcell_rgb inkcell_theme_state_layer(struct inkcell_rgb fill, struct inkcell_rgb ink,
                                             enum inkcell_state state);

/*
 * The fill and the ink for one family, one slot and one state, resolved together.
 *
 * The pair is the unit because splitting it is the bug: a widget that took its fill from here
 * and its label colour from somewhere else is a widget drawing a combination no theme was ever
 * checked against. Every component that can be filled goes through this.
 */
struct inkcell_paint {
    struct inkcell_rgb fill;
    struct inkcell_rgb ink;
};

struct inkcell_paint inkcell_theme_paint(const struct inkcell_theme *theme,
                                         enum inkcell_family family, enum inkcell_slot slot,
                                         enum inkcell_state state);

/*
 * The tone a fraction of some budget has earned: SUCCESS below `warn`, WARNING from there up
 * to `bad`, and ERROR at or above it. All three arguments are permille, the same scale a
 * meter's value is on (INKCELL_ANIM_ONE).
 *
 * The middle band was the primary until the warning family existed, which made the airtime
 * meter's caution colour the same one the screen drew its title in.
 *
 * It is here, in the UI's vocabulary, rather than in whichever screen first needed it, because
 * two things now say the same sentence about one number: the airtime figure is coloured by it
 * and the meter beside the figure is filled by it. A screen that worked its own thresholds out
 * for the text and handed a bar something else would be drawing a number and a picture that
 * disagree - and the picture is the one people will believe.
 *
 * Thresholds are the caller's because they are domain facts, not palette ones: 25% of the air
 * is a busy mesh, 25% of a download is a slow start.
 */
enum inkcell_tone inkcell_tone_for_load(int32_t permille, int32_t warn, int32_t bad);

/*
 * Where a reading changes meaning, on whatever scale the reading is stated in.
 *
 * The generalisation of the two thresholds above, and it is here for the same reason they are
 * answered here: what a number *means* is the UI's vocabulary, in the way a colour role and a
 * shape are. Its counterpart, `struct inkcell_scale` in layout.h, answers the other half - how
 * far along a track the number sits - and the split is the one this file already draws: the
 * theme says what a thing is, layout says where it goes.
 *
 * Two things beyond the tone now read a band: a bar fills to a boundary and *marks* it on its
 * own track, which is what turns "31%" into "past the first mark" for a reader who does not
 * carry the threshold around. A screen that stated the thresholds twice would be marking one
 * boundary and colouring another.
 *
 * Order is meaning. `bad` above `warn` is a figure that gets worse as it climbs - airtime,
 * channel utilization; `bad` below `warn` is one that gets worse as it falls - a battery, a
 * signal-to-noise ratio. There is no third case, and the reversed pair inkcell_tone_for_load()
 * defends against by swapping is here a sentence rather than a typo.
 */
struct inkcell_band {
    int32_t warn;
    int32_t bad;
};

/*
 * The tone `value` has earned against `band`, or `resting` when it has earned none. A NULL band
 * earns nothing, so a caller with no thresholds need not branch.
 *
 * `resting` rather than a stated SUCCESS, because "below the first boundary" is not always good
 * news to report: an airtime bar resting green is a mesh with room, while a download resting
 * green would be claiming something about a job that has merely not gone wrong yet.
 */
enum inkcell_tone inkcell_band_tone(const struct inkcell_band *band, int32_t value,
                                    enum inkcell_tone resting);
const struct inkcell_font *inkcell_theme_font(const struct inkcell_theme *theme);

/*
 * The avatar fill for `seed`, which is whatever identifies the conversation - a node number, a
 * channel index. Any seed answers: it is reduced into the theme's palette here, so no caller
 * has to know how many tints a theme offers.
 */
struct inkcell_rgb inkcell_theme_avatar(const struct inkcell_theme *theme, uint32_t seed);

/*
 * The `index`th categorical fill, wrapping - so a chart with more parts than the palette has
 * draws every one of them rather than leaving the overflow unpainted.
 *
 * Wrapping is the same refusal to fail that an unknown <PREFIX>_THEME and an out-of-range
 * family make, and it is not a licence: two parts sharing a colour is a chart that cannot be
 * read, so what wraps here is a bug upstream in whoever built a chart wider than
 * INKCELL_SERIES_COLORS. Drawing it is how that bug is visible rather than invisible.
 */
struct inkcell_rgb inkcell_theme_series(const struct inkcell_theme *theme, uint32_t index);
const struct inkcell_metrics *inkcell_theme_metrics(const struct inkcell_theme *theme);

/* How heavily `type` is set. REGULAR for a theme that states nothing, which is what a table
   written before the weight existed does. */
enum inkcell_weight inkcell_theme_type_weight(const struct inkcell_theme *theme,
                                              enum inkcell_type type);

/* The theme's glyph multiplier, clamped into [INKCELL_SCALE_MIN, INKCELL_SCALE_MAX]. */
int inkcell_theme_scale(const struct inkcell_theme *theme);
/* The multiplier chrome is drawn at, given the body's. Never below the minimum. */
/*
 * The glyph multiplier `type` wants, given the body's.
 *
 * The body scale plus the role's offset, clamped into the accepted range - so a theme already
 * drawing at the maximum gets a title the same size as its body rather than one the font
 * registry cannot rasterise, and the vocabulary degrades instead of breaking.
 */
int inkcell_theme_type_scale(const struct inkcell_theme *theme, enum inkcell_type type, int scale);

/*
 * The whole of `type`, resolved against a body scale - the one to reach for.
 *
 * A style rather than four calls because the facets only mean anything together: what is drawn
 * has to be measured with the same tracking and laid out at the same line height, and a
 * renderer that fetched the size here and forgot the tracking there would draw a line wider
 * than the box it reserved. Ask once, at the top of a run, and hand the answer down.
 *
 * `scale` is the body scale - 0 or less means the theme's own, as everywhere else here.
 */
struct inkcell_type_style inkcell_theme_type_style(const struct inkcell_theme *theme,
                                                   enum inkcell_type type, int scale);

/*
 * A style that is a size and a weight and nothing else: the face exactly as it was drawn, at
 * the font's own line, with proportional figures.
 *
 * What every call site that names a scale rather than a role is doing, said out loud - the
 * measurement primitives are written against a style now, and this is the style the plain
 * forms of them build. A caller that has a role should not be using it.
 */
struct inkcell_type_style inkcell_type_style_plain(int scale, enum inkcell_weight weight);

/*
 * `style` with tabular figures, whatever its role says.
 *
 * A role answers for the *kind* of line - a caption is mostly numbers, so the table says so
 * once - but some call sites know something the role cannot: that this particular string is a
 * count, a reading, a clock, and that it is going to be redrawn while somebody is looking at
 * it. A badge and a dial's label are both that, at whatever size their container asked for.
 *
 * There is deliberately no opposite. Turning a role's tabular figures back off is a call site
 * overruling the theme about what a caption *is*, and if that is ever right the answer is a
 * different role rather than a flag.
 */
struct inkcell_type_style inkcell_type_style_tabular(struct inkcell_type_style style);
/* Clamps any multiplier into the accepted range; 0 or less means "the theme's own". */
int inkcell_theme_clamp_scale(const struct inkcell_theme *theme, int scale);

/*
 * The corner radius `shape` wants, in pixels, at glyph multiplier `scale`.
 *
 * INKCELL_SHAPE_FULL answers with a number larger than any panel, because the fill primitive
 * clamps a radius to half the shorter side anyway: "as round as it goes" is the one shape that
 * cannot be measured until the box it is applied to is known, and the clamp is where that is
 * already known. Every other shape is its step count times the scale.
 */
int inkcell_theme_radius(const struct inkcell_theme *theme, enum inkcell_shape shape, int scale);

/* The gap `space` asks for, in pixels, at glyph multiplier `scale`. Half-steps times the scale,
   halved - so INKCELL_SPACE_SM is exactly one step and INKCELL_SPACE_XS is half of one. Never
   negative, and never rounded away to nothing when the theme asked for something. */
int inkcell_theme_space(const struct inkcell_theme *theme, enum inkcell_space space, int scale);

/* How long `motion` lasts, in milliseconds. 0 for a token a theme left unset, which every
   animation here reads as "already there" - so an incomplete theme is still a drawable one. */
uint32_t inkcell_theme_motion(const struct inkcell_theme *theme, enum inkcell_motion motion);

/*
 * The WCAG contrast ratio between two colours, from 1.0 (identical) to 21.0 (black on white).
 *
 * The arithmetic is the standard one: undo the display's gamma on each channel, weight them by
 * how much the eye gets from each (green most, blue least) to get a relative luminance, then
 * compare the lighter against the darker. It is here rather than in a test because it is what
 * makes "is this theme readable?" a question with an answer - see inkcell_theme_validate().
 */
double inkcell_theme_contrast(struct inkcell_rgb a, struct inkcell_rgb b);

/*
 * Whether a theme's text is readable on the grounds it is drawn on.
 *
 * Body text needs 4.5:1 and secondary text 3.0:1, which are the WCAG AA thresholds; a hairline
 * only has to be visible at all. On failure `reason` (when given) names the pair that failed,
 * so a theme added later fails with an explanation rather than by looking wrong on a handheld
 * somebody has taken outdoors.
 */
bool inkcell_theme_validate(const struct inkcell_theme *theme, char *reason, size_t reason_len);

#ifdef __cplusplus
}
#endif

#endif /* INKCELL_THEME_H */
