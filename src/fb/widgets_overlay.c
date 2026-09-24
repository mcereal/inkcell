#define _POSIX_C_SOURCE 200809L

/*
 * The overlays. The snackbar and the dialog each take the whole of something - the footer, or
 * the body - which is why neither advances a `y` the way a component inside a screen does.
 */

#include "inkcell/ui/widgets/button.h"
#include "inkcell/ui/widgets/overlay.h"

#include "inkcell/i18n/strings.h"
#include "inkcell/ui/anim.h"
#include "inkcell/ui/emoji.h"
#include "inkcell/ui/layout.h"
#include "inkwell/base/text.h"

#include <string.h>

/* ---- the snackbar ------------------------------------------------------------------------ */

/*
 * The snackbar's layer id.
 *
 * Every other id in the overlay stack is a screen's own enumerator. There is exactly one
 * snackbar and no screen owns it, so it takes a constant from the far end of the range, where
 * nothing a screen can pass will ever land - which is where it took its animation-table key
 * from, for the same reason.
 */
#define INKCELL_FB_OVERLAY_ID_SNACKBAR 0xFFFFFF01U

/* Material allows one line or two, and two is where a notice stops being one on a 3.2" panel.
   Anything longer is clipped rather than allowed to grow into the body. */
#define INKCELL_FB_SNACKBAR_LINES_MAX 2U

void inkcell_fb_draw_snackbar(struct inkcell_draw_state *state,
                              const struct inkcell_fb_layout *layout,
                              const struct inkcell_fb_snackbar *bar) {
    if (state == NULL || layout == NULL) {
        return;
    }

    const char *const text = (bar != NULL && bar->text != NULL) ? bar->text : "";
    const bool showing = (text[0] != '\0');

    if (showing &&
        (bar->until_ms != state->snackbar_until_ms || strcmp(text, state->snackbar) != 0)) {
        /*
         * A different notice, so it *arrives* rather than swapping its words.
         *
         * The layer is taken down with no exit first, so the next frame sees it come up from
         * nothing - which is what makes a second "Not connected" arrive rather than sit there
         * looking like the first one never left. A notice replacing another is the one case
         * where a layer should be interrupted rather than re-aimed.
         */
        (void)inkwell_str_copy(state->snackbar, sizeof state->snackbar, text);
        state->snackbar_until_ms = bar->until_ms;
        inkcell_fb_overlay_drop(state, INKCELL_FB_OVERLAY_ID_SNACKBAR);
    }
    if (state->snackbar[0] == '\0') {
        return;
    }

    const int scale = state->scale;
    const int adv = inkcell_fb_char_adv(state, scale);
    const int line = inkcell_fb_line_adv(state, scale);
    /*
     * How far the bar floats above the footer - a *vertical* gap, and so the theme's own margin
     * rather than anything the content column has a say in.
     *
     * Worth naming for what it is, because it was briefly spelled as the column's leading edge.
     * Those were one number for as long as the column and the panel were the same place; where
     * the column is capped and centred the leading edge is hundreds of pixels in, and spending
     * that on a vertical gap lifts the snackbar most of the way up the frame - or, on a wide
     * enough window, leaves it no band at all and it is never drawn.
     *
     * How wide the bar is *is* the column's business, and that is `room` above.
     */
    const int clearance = inkcell_fb_margin(state);
    /* A cell in from each edge and a proportional band above and below: a container's padding
       is measured in the same units as what it holds, so a theme asking for bigger text gets a
       proportionally roomier bar rather than a tighter one. */
    const int pad_x = adv;
    const int pad_y = inkcell_scale_px(2, scale);

    const int room = inkcell_fb_content_w(state) - 2 * pad_x;
    if (room < adv || line <= 0) {
        return; /* a geometry too small to hold one cell of it; nothing to say here */
    }
    /* The budget is the room itself, in pixels, rather than a count of cells that room would
       hold: what has to fit is the drawn line, and on a proportional face those are two
       different questions. */
    struct inkcell_fb_wrap_ctx wctx;
    const struct inkcell_wrap_metric metric = inkcell_fb_wrap_metric(&wctx, state, scale);
    const size_t budget = (size_t)room;

    uint32_t lines = inkcell_wrap_lines_measured(state->snackbar, budget, &metric);
    if (lines == 0U) {
        lines = 1U;
    }
    if (lines > INKCELL_FB_SNACKBAR_LINES_MAX) {
        lines = INKCELL_FB_SNACKBAR_LINES_MAX;
    }
    /* Sized to its own words rather than to the panel, the way a bubble is: a bar the full
       width of the screen is a region of the chrome, and a notice is one thing that arrived. */
    size_t words = inkcell_wrap_widest_measured(state->snackbar, budget, &metric);
    if (words == 0U) {
        words = (size_t)adv;
    }

    /*
     * The band it comes up into: the body, ending a full margin clear of the footer.
     *
     * A layer rests against the edge of its region, so a region that stops short is how a
     * floating notice says it floats - which is what this is. A card stops half a margin short
     * of the footer because a card is *in* the body and the body's own bottom edge is where it
     * belongs; this one is over everything, so it keeps the distance the panel edge keeps
     * rather than the one the body does.
     */
    const struct inkcell_fb_rect bounds = {
        .x = 0,
        .y = layout->nav_y,
        .w = inkcell_fb_panel_width(state),
        .h = layout->footer_y - clearance - layout->nav_y,
    };

    struct inkcell_overlay_frame frame;
    if (!inkcell_fb_overlay_begin(state,
                                  &(struct inkcell_overlay){
                                      .id = INKCELL_FB_OVERLAY_ID_SNACKBAR,
                                      .up = showing,
                                      .placement = INKCELL_OVERLAY_BOTTOM,
                                      /* From off the panel, not from just under its resting
                                         place: a container that slid in a few pixels reads as
                                         a nudge, and one that comes up from off-screen reads
                                         as something arriving. That is the whole of what this
                                         shape is for. */
                                      .travel = INKCELL_OVERLAY_TRAVEL_OFF_PANEL,
                                      .w = (int)words + 2 * pad_x,
                                      .h = (int)lines * line + 2 * pad_y,
                                      .bounds = bounds,
                                      /* No scrim and not modal. A notice is not a question -
                                         nothing about it is waiting for a press, and dimming
                                         the screen to say "Sent" would be the loudest thing
                                         on the panel saying the quietest thing. */
                                  },
                                  &frame)) {
        /* All the way out. The store forgot the words several frames ago; now so does this,
           and the next notice starts from nothing rather than from this one's position. */
        state->snackbar[0] = '\0';
        state->snackbar_until_ms = 0U;
        return;
    }

    /*
     * The inverted surface, and no edge on it.
     *
     * A card needs a hairline because its fill is one step off the ground and the step alone is
     * not findable in daylight. This one is the other end of the palette - the furthest from
     * the ground the theme has - so the fill is the whole cue, and an outline over it would be
     * drawing a border around the most obvious thing on the panel.
     */
    inkcell_fb_fill_round_rect(state, frame.box.x, frame.box.y, frame.box.w, frame.box.h,
                               inkcell_fb_radius(state, INKCELL_SHAPE_SM),
                               inkcell_fb_color(state, INKCELL_COLOR_SURFACE_INVERSE));

    const struct inkcell_rgb ink = inkcell_fb_color(state, INKCELL_COLOR_TEXT_ON_INVERSE);
    struct inkcell_wrap wrap;
    inkcell_wrap_begin_measured(&wrap, state->snackbar, budget, &metric);
    int text_y = frame.box.y + pad_y;
    for (uint32_t drawn = 0U; drawn < lines && inkcell_wrap_next(&wrap); ++drawn) {
        inkcell_fb_draw_text(state, frame.box.x + pad_x, text_y, wrap.line, scale, ink,
                             inkcell_fb_color(state, INKCELL_COLOR_SURFACE_INVERSE));
        text_y += line;
    }
    inkcell_fb_overlay_end(state, &frame);
}

/* ---- the dialog ---------------------------------------------------------------------------- */

/* How large the dialog's icon is drawn, in glyph-scale steps. Big enough to be the first thing
   read - it is the only thing on the panel that says what *kind* of question this is before a
   word of it has been - and clamped by inkcell_fb_draw_icon() to what the sprite can carry. */
#define INKCELL_FB_DIALOG_ICON_SCALE 3

/*
 * `src` shortened until what it draws fits `max_w` pixels.
 *
 * Measured, never counted. A width divided by inkcell_fb_char_adv() is a *nominal* column
 * count, and on the proportional face the advance is an average rather than a width - so a
 * label of narrow letters is cut several cells early and one of wide letters still overhangs.
 * The menu found this the moment it was drawn: "Mute notifications" came out as "Mute
 * notificatio" inside a panel measured to hold the whole of it.
 *
 * Cut on cell boundaries, so a label with an emoji or an accented character in it loses a
 * whole glyph rather than half of a UTF-8 sequence. A label that cannot be made to fit at all
 * keeps its first cell: something is drawn, and the row still marks where the press lands.
 */
static void inkcell_fb_fit_text(const struct inkcell_draw_state *state, const char *src, int scale,
                                int max_w, char *out, size_t out_len) {
    inkwell_str_copy(out, out_len, src != NULL ? src : "");
    while (inkcell_fb_text_width(state, out, scale) > max_w) {
        const size_t cells = inkcell_text_cells(out);
        if (cells <= 1U) {
            return;
        }
        inkcell_text_cell_truncate(out, cells - 1U);
    }
}

/*
 * `src` shortened until the button holding it fits `max_w`.
 *
 * Measured with inkcell_fb_button_width() rather than by dividing the width by a cell, because a
 * button's padding and the gap after its icon are the button's business - a caller that did the
 * arithmetic itself would disagree with the thing it is sizing the moment either changes.
 *
 * Cut on cell boundaries, so a label with an emoji or an accented character in it loses a whole
 * glyph rather than half of a UTF-8 sequence. A label that cannot be made to fit at all keeps
 * its first cell: something is drawn, and the button still marks where the press lands.
 */
static void inkcell_fb_fit_button_label(const struct inkcell_draw_state *state,
                                        enum inkcell_icon icon, const char *src, int scale,
                                        int max_w, char *out, size_t out_len) {
    inkwell_str_copy(out, out_len, src != NULL ? src : "");
    while (inkcell_fb_button_width(state, icon, out, scale) > max_w) {
        const size_t cells = inkcell_text_cells(out);
        if (cells <= 1U) {
            return;
        }
        inkcell_text_cell_truncate(out, cells - 1U);
    }
}

/*
 * The dialog, measured.
 *
 * Split out of the draw because a layer is *handed* a size rather than deciding one, and a
 * dialog's height depends on how far its paragraph wraps. Two calls rather than one, which is
 * the shape every measure-then-draw component here takes - the card, the list note, the
 * settings slider - and for the same reason: whoever places the thing has to know how big it
 * is before the first pixel goes down.
 */
struct inkcell_fb_dialog_metrics {
    int scale, line, pad, edge, adv, gap;
    int text_w;
    size_t text_budget, text_cols;
    /* The two roles the panel is set in: the question, and the explanation under it. Held as
       styles rather than as scales because both are measured and drawn several times below,
       and a paragraph wrapped in one style and drawn in another is a paragraph that overruns
       the panel it was sized to fit. */
    struct inkcell_type_style head_style, text_style;
    /* The explanation's own line advance - the supporting role's, which is not the body's. */
    int text_line;
    size_t head_cols;
    bool has_icon, has_headline, stacked;
    int icon_scale, icon_h, head_h, button_h, actions_h;
    int accept_w, cancel_w;
    char accept_label[INKCELL_LINE_MAX];
    char cancel_label[INKCELL_LINE_MAX];
    uint32_t text_lines;
    int height;
};

static struct inkcell_fb_dialog_metrics
inkcell_fb_dialog_measure(const struct inkcell_draw_state *state,
                          const struct inkcell_fb_dialog *dialog, int width, int room) {
    struct inkcell_fb_dialog_metrics m;
    memset(&m, 0, sizeof m);
    m.scale = state->scale;
    m.line = inkcell_fb_line_adv(state, m.scale);
    /* The panel's inset is the body margin, which is what the card uses: a surface over the
       body is padded by the same amount the body is padded from the panel edge. */
    m.pad = inkcell_fb_margin(state);
    m.edge = inkcell_fb_edge(state);
    m.adv = inkcell_fb_char_adv(state, m.scale);
    m.gap = inkcell_scale_px(2, m.scale);

    /* The panel's own text column, which is narrower than the body's - a dialog is inset from
       the screen and its words are inset again from its edge. */
    m.text_w = width - 2 * m.pad;
    /* In pixels, for the reason the snackbar's budget is: the panel has to hold the line as
       drawn. `text_cols` is what the headline's clip still counts in. */
    m.text_budget = m.text_w > 0 ? (size_t)m.text_w : 1U;
    m.text_cols = m.text_w > m.adv ? (size_t)(m.text_w / m.adv) : 1U;

    /*
     * The question is a headline and the explanation is supporting body, which is the whole of
     * what tells them apart now.
     *
     * They used to be the same glyph size in the same face, and the gap between them was the
     * only thing saying which was which - a dialog whose question and whose reasoning read as
     * one paragraph broken in two. The headline role is half a step over the body and set
     * heavier; the supporting role is half a step under it with a looser line, which is the
     * leading a paragraph that wraps actually wants.
     */
    m.head_style = inkcell_fb_type_style(state, INKCELL_TYPE_HEADLINE);
    m.text_style = inkcell_fb_type_style(state, INKCELL_TYPE_BODY_SOFT);
    m.text_line = inkcell_fb_line_adv_styled(state, &m.text_style);
    /* The headline's clip counts *its* cells, not the body's: bigger glyphs mean fewer of them
       across the same panel, and a question fitted to the body's column count is a question
       that runs off the panel edge. */
    const int head_adv = inkcell_fb_char_adv(state, m.head_style.scale);
    m.head_cols = (head_adv > 0 && m.text_w > head_adv) ? (size_t)(m.text_w / head_adv) : 1U;

    m.icon_scale = m.scale * INKCELL_FB_DIALOG_ICON_SCALE;
    m.has_icon = inkcell_icon_is_valid(dialog->icon);
    m.icon_h = m.has_icon ? inkcell_fb_line_adv(state, m.icon_scale) : 0;
    m.has_headline = dialog->headline != NULL && dialog->headline[0] != '\0';
    /* A little more than its own line. The step is still here now that the two roles differ in
       size - it is the gap between a question and its answer's reasoning, not the thing that
       distinguishes them. */
    m.head_h = m.has_headline
                   ? inkcell_fb_line_adv_styled(state, &m.head_style) + inkcell_step_px(m.scale)
                   : 0;
    m.button_h = m.line + inkcell_fb_space(state, INKCELL_SPACE_MD);

    /*
     * The action row, measured before the panel is: how tall the panel has to be depends on
     * whether the two answers fit beside each other.
     *
     * They do not always. "Reset the node database" at the largest glyph scale wants more than
     * the whole panel on its own, and side by side with Cancel it ran the cancel button off the
     * left-hand edge of the *screen* - on a destructive confirmation, where the safe answer is
     * the one that disappeared. So the row stacks when it has to, which is what every platform
     * does with dialog actions too long to sit in a line, and each label is fitted to the panel
     * so a single button can never overhang it either.
     */
    inkcell_fb_fit_button_label(state, INKCELL_ICON_CHECK, dialog->accept, m.scale, m.text_w,
                                m.accept_label, sizeof m.accept_label);
    inkcell_fb_fit_button_label(state, INKCELL_ICON_CLOSE, dialog->cancel, m.scale, m.text_w,
                                m.cancel_label, sizeof m.cancel_label);
    m.cancel_w = inkcell_fb_button_width(state, INKCELL_ICON_CLOSE, m.cancel_label, m.scale);
    m.accept_w = inkcell_fb_button_width(state, INKCELL_ICON_CHECK, m.accept_label, m.scale);
    /* Stacked puts the answer that acts on top, the way a stacked dialog orders them - the
       dismissive one stays nearest the thumb. */
    m.stacked = (m.accept_w + m.gap + m.cancel_w) > m.text_w;
    m.actions_h = m.stacked ? (2 * m.button_h + m.gap) : m.button_h;

    /*
     * The paragraph is the only part that gives way. Everything else on the panel is either
     * the question or the answers, and a dialog that dropped one of its buttons to fit its
     * explanation would be unanswerable - so the buttons, the headline and the icon are
     * reserved first and the supporting text takes what is left.
     */
    const int fixed = m.pad + m.icon_h + m.head_h + m.line / 2 + m.actions_h + m.pad;
    const int text_room = room - fixed;
    const uint32_t fits =
        (text_room > 0 && m.text_line > 0) ? (uint32_t)(text_room / m.text_line) : 0U;
    m.text_lines =
        (dialog->text != NULL && dialog->text[0] != '\0')
            ? inkcell_fb_wrapped_lines_styled(state, dialog->text, m.text_budget, &m.text_style)
            : 0U;
    if (m.text_lines > fits) {
        m.text_lines = fits;
    }
    m.height = fixed + (int)m.text_lines * m.text_line;
    if (m.height > room) {
        m.height = room;
    }
    return m;
}

int inkcell_fb_dialog_height(const struct inkcell_draw_state *state,
                             const struct inkcell_fb_dialog *dialog, int width, int room) {
    if (state == NULL || dialog == NULL) {
        return 0;
    }
    return inkcell_fb_dialog_measure(state, dialog, width, room).height;
}

void inkcell_fb_draw_dialog_at(const struct inkcell_draw_state *state, struct inkcell_fb_rect box,
                               const struct inkcell_fb_dialog *dialog) {
    if (state == NULL || dialog == NULL || box.w <= 0 || box.h <= 0) {
        return;
    }
    /* Measured against the box it was handed rather than against the body: the layer has
       already decided how big this is, and a second opinion about it here would be a panel
       whose contents disagree with its own edges while it is arriving. */
    const struct inkcell_fb_dialog_metrics m =
        inkcell_fb_dialog_measure(state, dialog, box.w, box.h);

    /* One decision for the whole panel: the icon, the headline and the accept button's fill all
       come off this, so a destructive dialog cannot end up half red. */
    const enum inkcell_family accept_family =
        dialog->destructive ? INKCELL_FAMILY_ERROR : INKCELL_FAMILY_PRIMARY;
    const enum inkcell_tone accent_tone = inkcell_family_tone(accept_family);

    const int radius = inkcell_fb_radius(state, INKCELL_SHAPE_LG);
    inkcell_fb_fill_round_rect(state, box.x, box.y, box.w, box.h, radius + m.edge,
                               inkcell_fb_color(state, INKCELL_COLOR_OUTLINE));
    inkcell_fb_fill_round_rect(state, box.x + m.edge, box.y + m.edge, box.w - 2 * m.edge,
                               box.h - 2 * m.edge, radius,
                               inkcell_fb_color(state, INKCELL_COLOR_SURFACE_HIGH));

    const int content_x = box.x + m.pad;
    int y = box.y + m.pad;

    if (m.has_icon) {
        inkcell_fb_draw_icon(state, content_x, y, dialog->icon, m.icon_scale,
                             inkcell_fb_tone_color(state, accent_tone),
                             inkcell_fb_color(state, INKCELL_COLOR_SURFACE_HIGH));
        y += m.icon_h;
    }

    if (m.has_headline) {
        struct inkcell_line headline;
        inkcell_line_reset(&headline);
        inkcell_line_printf(&headline, "%s", dialog->headline);
        inkcell_line_fit(&headline, m.head_cols);
        inkcell_fb_draw_text_styled(state, content_x, y, inkcell_line_text(&headline),
                                    &m.head_style, inkcell_fb_tone_color(state, accent_tone),
                                    inkcell_fb_color(state, INKCELL_COLOR_SURFACE_HIGH));
        y += m.head_h;
    }

    if (m.text_lines > 0U) {
        inkcell_fb_draw_wrapped_styled(state, content_x, y, dialog->text, m.text_budget,
                                       (int)m.text_lines, &m.text_style,
                                       inkcell_fb_tone_color(state, INKCELL_TONE_NORMAL),
                                       inkcell_fb_color(state, INKCELL_COLOR_SURFACE_HIGH));
    }

    /*
     * Side by side, the answer that does nothing goes on the left and the one that acts on the
     * right, so the press that costs something is never the one nearest a thumb resting where
     * it was. Stacked, the same reasoning puts the acting answer on top.
     */
    const int right = box.x + box.w - m.pad;
    const int accept_y =
        box.y + box.h - m.pad - (m.stacked ? (2 * m.button_h + m.gap) : m.button_h);
    const int cancel_y = m.stacked ? (accept_y + m.button_h + m.gap) : accept_y;
    const int accept_x = right - m.accept_w;
    const int cancel_x = m.stacked ? (right - m.cancel_w) : (accept_x - m.gap - m.cancel_w);

    const struct inkcell_fb_button cancel = {
        .rect = {cancel_x, cancel_y, m.cancel_w, m.button_h},
        .icon = INKCELL_ICON_CLOSE,
        .label = m.cancel_label,
        .focused = dialog->cursor != 0U,
        .variant = INKCELL_FB_BUTTON_TEXT,
        .shape = INKCELL_SHAPE_FULL,
        .idle_tone = INKCELL_TONE_NORMAL,
        .ground = INKCELL_COLOR_SURFACE_HIGH,
        .scale = m.scale,
        .focus_id = dialog->action_focus_id != INKCELL_FOCUS_NONE ? dialog->action_focus_id + 1U
                                                                  : INKCELL_FOCUS_NONE,
    };
    inkcell_fb_draw_button(state, &cancel);

    /*
     * Exactly one of the two carries a fill, and it is always the focused one.
     *
     * The obvious design gives the accept a standing fill so it reads as the proposed answer,
     * and lets the cursor pick between them. That works on three of the four themes and fails
     * on the one where it matters most: the high-contrast palette deliberately collapses
     * PRIMARY, PRIMARY_CONTAINER and SURFACE_ACTIVE onto a single yellow, because a theme built
     * for legibility does not have a "held back" version of its one accent. Two buttons whose
     * fills both resolve to that yellow are two buttons a user cannot tell apart, on the theme
     * chosen by the people least able to guess.
     *
     * So the fill means focus and nothing else, and what marks the accept as the affirmative is
     * its check and its family-coloured label - a cue that survives every palette because it is
     * ink on the panel rather than one fill against another.
     */
    const bool accept_focused = dialog->cursor == 0U;
    const struct inkcell_fb_button accept = {
        .rect = {accept_x, accept_y, m.accept_w, m.button_h},
        .icon = INKCELL_ICON_CHECK,
        .label = m.accept_label,
        .focused = accept_focused,
        .variant = accept_focused ? INKCELL_FB_BUTTON_TONAL : INKCELL_FB_BUTTON_TEXT,
        /* The dialog's family, so a destructive confirm is a red pill rather than a red word on
           the ordinary one - and the ink on it is the one checked against that red. */
        .family = accept_family,
        .shape = INKCELL_SHAPE_FULL,
        .idle_tone = accent_tone,
        .ground = INKCELL_COLOR_SURFACE_HIGH,
        .scale = m.scale,
        .focus_id = dialog->action_focus_id,
    };
    inkcell_fb_draw_button(state, &accept);
}

bool inkcell_fb_draw_dialog(struct inkcell_draw_state *state,
                            const struct inkcell_fb_layout *layout,
                            const struct inkcell_fb_dialog *dialog, uint32_t id, bool up) {
    if (state == NULL || layout == NULL || dialog == NULL) {
        return false;
    }
    /*
     * The region the dialog belongs to, and therefore what the scrim dims: from under the
     * navigation bar down to the top of the action bar.
     *
     * Not the whole panel, and that is the decision this port turns on. A dialog is one
     * screen asking a question; dimming the tab strip and the keycaps too would say the
     * application had been suspended, which is what a full-screen scrim means everywhere it is
     * used. The keycaps in particular must stay bright - they are how the question gets
     * answered.
     */
    const struct inkcell_fb_rect bounds = {
        .x = 0,
        .y = layout->nav_y,
        .w = inkcell_fb_panel_width(state),
        .h = layout->footer_y - layout->nav_y,
    };
    const int width = inkcell_fb_panel_width(state) - 2 * inkcell_fb_gutter(state);
    const int room = bounds.h - 2 * inkcell_fb_gutter(state);

    struct inkcell_overlay_frame frame;
    if (!inkcell_fb_overlay_begin(state,
                                  &(struct inkcell_overlay){
                                      .id = id,
                                      .up = up,
                                      .placement = INKCELL_OVERLAY_CENTER,
                                      /* A short way up rather than in from an edge: a dialog is not
                                         sliding past, it is appearing where the question is. */
                                      .travel = INKCELL_OVERLAY_TRAVEL_NEAR,
                                      .w = width,
                                      .h = inkcell_fb_dialog_height(state, dialog, width, room),
                                      .bounds = bounds,
                                      .scrim = true,
                                      .modal = true,
                                  },
                                  &frame)) {
        return false;
    }
    inkcell_fb_draw_dialog_at(state, frame.box, dialog);
    inkcell_fb_overlay_end(state, &frame);
    return true;
}

/* ---- the QR code ---------------------------------------------------------------------------- */

/*
 * How many pixels one module gets, and therefore how big the code comes out.
 *
 * Integer division on purpose - see the header. A code whose modules are 7.4 pixels across has
 * boundaries that fall between pixels, and a reader thresholding a photograph of that finds
 * edges the code does not have; seven is worth more than the 5% of the box that rounding down
 * gives away.
 */
static int inkcell_fb_qr_module_px(const struct inkcell_fb_qr *qr) {
    if (qr == NULL || qr->code == NULL || qr->code->size == 0U) {
        return 0;
    }
    /* Four modules of quiet zone on each side, which the standard asks for and a reader needs
       to lock on at all. */
    const int modules = (int)qr->code->size + 8;
    const int room = qr->box.w < qr->box.h ? qr->box.w : qr->box.h;
    return room / modules;
}

int inkcell_fb_qr_side(const struct inkcell_fb_qr *qr) {
    const int px = inkcell_fb_qr_module_px(qr);
    return px > 0 ? px * ((int)qr->code->size + 8) : 0;
}

void inkcell_fb_draw_qr(const struct inkcell_draw_state *state, const struct inkcell_fb_qr *qr) {
    const int px = inkcell_fb_qr_module_px(qr);
    if (px <= 0) {
        return;
    }
    const int size = (int)qr->code->size;
    const int side = px * (size + 8);
    const int x0 = qr->box.x + (qr->box.w - side) / 2;
    const int y0 = qr->box.y + (qr->box.h - side) / 2;

    /* The margin is drawn rather than left to the screen behind it: the quiet zone is part of
       the code, and a reader that cannot find it does not lock on. */
    inkcell_fb_fill_rect(state, x0, y0, side, side,
                         inkcell_fb_color(state, INKCELL_COLOR_CODE_GROUND));
    const struct inkcell_rgb ink = inkcell_fb_color(state, INKCELL_COLOR_CODE);
    for (int y = 0; y < size; ++y) {
        /* A run of dark modules is one fill rather than one per module: a code at the version
           cap is thirteen thousand of them, and the whole screen is repainted whenever anything
           on the frame moves. */
        int run = 0;
        for (int x = 0; x <= size; ++x) {
            const bool dark = x < size && inkcell_qr_dark(qr->code, x, y);
            if (dark) {
                run++;
                continue;
            }
            if (run > 0) {
                inkcell_fb_fill_rect(state, x0 + (x - run + 4) * px, y0 + (y + 4) * px, run * px,
                                     px, ink);
                run = 0;
            }
        }
    }
}

/* ---- the menu ------------------------------------------------------------------------------
 *
 * A column of verbs on a raised panel. See the header for why this is three dozen lines rather
 * than a screen: the box, the arrival, the scrim and the press all belong to the layer, so what
 * is left here is a measure and a loop.
 */

/* The room a menu leaves round its rows, and the room a row leaves round its words. Both are
   the panel inset the dialog and the card take, so three surfaces over the body are padded
   alike rather than each deciding. */
static int inkcell_fb_menu_pad(const struct inkcell_draw_state *state) {
    return inkcell_fb_gutter(state);
}

/* How tall one row is: a line, plus the padding that makes it a place to press rather than a
   line of text. The keycap's own height arithmetic, for the keycap's reason - a row a cursor
   lands on has to be big enough to read as a target. */
static int inkcell_fb_menu_row_h(const struct inkcell_draw_state *state) {
    return inkcell_fb_line_adv(state, state->scale) + inkcell_fb_space(state, INKCELL_SPACE_SM);
}

/* Whether a row is one the cursor may stand on. An item with no words is a gap a screen left
   in a fixed array, and a disabled one is a verb that cannot be done - neither is a place to
   be, and neither is registered. */
static bool inkcell_fb_menu_item_live(const struct inkcell_fb_menu_item *item) {
    return item->label != NULL && item->label[0] != '\0' && !item->disabled;
}

struct inkcell_fb_rect inkcell_fb_menu_box(const struct inkcell_draw_state *state,
                                           const struct inkcell_fb_menu *menu, int max_w) {
    struct inkcell_fb_rect box = {0, 0, 0, 0};
    if (state == NULL || menu == NULL || menu->items == NULL || menu->count == 0U) {
        return box;
    }
    const int scale = state->scale;
    const int pad = inkcell_fb_menu_pad(state);
    const int gap = inkcell_fb_space(state, INKCELL_SPACE_SM);
    const int icon = inkcell_fb_icon_box(state, scale);
    const int row_h = inkcell_fb_menu_row_h(state);

    /*
     * Both slots are reserved for the whole menu whether or not every row fills them.
     *
     * A menu where the rows with an icon start their words a gutter to the right of the rows
     * without one is a menu with two left edges, which is the list's own rule
     * (INKCELL_FB_LEADING_ICON) and is wrong here for the same reason. The trailing check is
     * the same argument at the other end: a menu that is a choice has one checked row, and the
     * other rows' words must not run under where that check is.
     */
    int widest = 0;
    uint32_t rows = 0U;
    for (size_t i = 0U; i < menu->count; ++i) {
        const struct inkcell_fb_menu_item *item = &menu->items[i];
        if (item->label == NULL || item->label[0] == '\0') {
            continue;
        }
        const int w = inkcell_fb_text_width(state, item->label, scale);
        if (w > widest) {
            widest = w;
        }
        ++rows;
    }
    if (rows == 0U) {
        return box;
    }
    const bool has_heading = menu->heading != NULL && menu->heading[0] != '\0';
    if (has_heading) {
        const int small = inkcell_fb_type_scale(state, INKCELL_TYPE_LABEL);
        const int head_w = inkcell_fb_text_width(state, menu->heading, small);
        if (head_w > widest) {
            widest = head_w;
        }
    }

    box.w = pad + icon + gap + widest + gap + icon + pad;
    if (max_w > 0 && box.w > max_w) {
        box.w = max_w;
    }
    box.h = 2 * pad + (int)rows * row_h;
    if (has_heading) {
        box.h += inkcell_fb_line_adv(state, inkcell_fb_type_scale(state, INKCELL_TYPE_LABEL));
    }
    return box;
}

void inkcell_fb_draw_menu(const struct inkcell_draw_state *state, struct inkcell_fb_rect box,
                          const struct inkcell_fb_menu *menu) {
    if (state == NULL || menu == NULL || menu->items == NULL || box.w <= 0 || box.h <= 0) {
        return;
    }
    const int scale = state->scale;
    const int pad = inkcell_fb_menu_pad(state);
    const int gap = inkcell_fb_space(state, INKCELL_SPACE_SM);
    const int icon = inkcell_fb_icon_box(state, scale);
    const int row_h = inkcell_fb_menu_row_h(state);
    const int edge = inkcell_fb_edge(state);
    const int radius = inkcell_fb_radius(state, INKCELL_SHAPE_MD);

    /*
     * The raised surface and its hairline, exactly as a card takes them. A menu is one step
     * further from the ground than the body and one step nearer than a dialog, which is what
     * SURFACE_HIGH already means - the edge is there because a fill one step off the ground is
     * not findable in daylight on its own.
     */
    inkcell_fb_fill_round_rect(state, box.x, box.y, box.w, box.h, radius + edge,
                               inkcell_fb_color(state, INKCELL_COLOR_OUTLINE));
    inkcell_fb_fill_round_rect(state, box.x + edge, box.y + edge, box.w - 2 * edge,
                               box.h - 2 * edge, radius,
                               inkcell_fb_color(state, INKCELL_COLOR_SURFACE_HIGH));
    const enum inkcell_color ground = INKCELL_COLOR_SURFACE_HIGH;

    int y = box.y + pad;
    if (menu->heading != NULL && menu->heading[0] != '\0') {
        /* At the label scale and in the dim ink, which is what a section heading is everywhere
           else in this toolkit - see inkcell_fb_list_subheader(). It is not a row: nothing
           lands on it, so it registers nothing. */
        const int small = inkcell_fb_type_scale(state, INKCELL_TYPE_LABEL);
        inkcell_fb_draw_text_weight(state, box.x + pad, y, menu->heading, small,
                                    inkcell_fb_type_weight(state, INKCELL_TYPE_LABEL),
                                    inkcell_fb_color(state, INKCELL_COLOR_TEXT_DIM),
                                    inkcell_fb_color(state, ground));
        y += inkcell_fb_line_adv(state, small);
    }

    const int text_x = box.x + pad + icon + gap;
    const int text_right = box.x + box.w - pad - icon - gap;
    for (size_t i = 0U; i < menu->count; ++i) {
        const struct inkcell_fb_menu_item *item = &menu->items[i];
        if (item->label == NULL || item->label[0] == '\0') {
            continue;
        }
        const bool live = inkcell_fb_menu_item_live(item);
        const bool focused = (uint32_t)i == menu->cursor;
        /*
         * The fill under the cursor is the row highlight the whole toolkit uses, taken from
         * the surface the menu is standing on rather than from the body's - a state layer
         * mixed into the wrong ground is a highlight that is the right colour on one surface
         * and a smudge on the other.
         */
        struct inkcell_rgb row_ground = inkcell_fb_color(state, ground);
        if (focused && live) {
            row_ground =
                inkcell_fb_state_layer(state, ground, INKCELL_COLOR_TEXT, INKCELL_STATE_FOCUSED);
            inkcell_fb_fill_round_rect(state, box.x + pad / 2, y, box.w - pad, row_h,
                                       inkcell_fb_radius(state, INKCELL_SHAPE_SM), row_ground);
        }

        /*
         * Disabled is the dim ink whatever the row meant, because a verb that cannot be done
         * is not a warning or a danger - it is absent. A red row greyed out would be shouting
         * about something the reader cannot act on.
         */
        const struct inkcell_rgb ink = live ? inkcell_fb_tone_color(state, item->tone)
                                            : inkcell_fb_color(state, INKCELL_COLOR_TEXT_DIM);

        const int glyph_y = y + (row_h - inkcell_fb_line_adv(state, scale)) / 2;
        if (inkcell_icon_is_valid(item->icon)) {
            inkcell_fb_draw_icon(state, box.x + pad, glyph_y, item->icon, scale, ink, row_ground);
        }
        /* Fitted to the room between the two slots, in pixels. The box was measured to hold
           the longest label whole, so this only ever cuts when a caller's `max_w` made the
           panel narrower than the menu asked for. */
        char label[INKCELL_LINE_MAX];
        inkcell_fb_fit_text(state, item->label, scale, text_right - text_x, label, sizeof label);
        inkcell_fb_draw_text(state, text_x, glyph_y, label, scale, ink, row_ground);
        if (item->checked) {
            inkcell_fb_draw_icon(state, text_right + gap, glyph_y, INKCELL_ICON_CHECK, scale, ink,
                                 row_ground);
        }

        const uint32_t focus_id = menu->focus_ids != NULL ? menu->focus_ids[i]
                                                          : (menu->focus_base != INKCELL_FOCUS_NONE
                                                                 ? menu->focus_base + (uint32_t)i
                                                                 : INKCELL_FOCUS_NONE);
        if (live && focus_id != INKCELL_FOCUS_NONE) {
            /* The box the highlight paints, so the ring lands on what the fill covers rather
               than on the text inside it - inkcell_fb_list_row()'s rule. */
            const struct inkcell_fb_rect hit = {box.x + pad / 2, y, box.w - pad, row_h};
            inkcell_fb_focus_register_shaped(state, focus_id, &hit, INKCELL_SHAPE_SM);
            if (focused) {
                inkcell_fb_focus_mark(state, focus_id);
            }
        }
        y += row_h;
    }
}

/* ---- the bottom sheet ----------------------------------------------------------------------- */

/* How wide the grabber is, as a fraction of the sheet: a tenth. Material's is 32dp against a
   360dp sheet, which is very nearly this, and a fraction rather than a length is what keeps it
   looking like the same mark at every scale the theme allows. */
#define INKCELL_FB_SHEET_GRABBER_NUM 1
#define INKCELL_FB_SHEET_GRABBER_DEN 10

/* The band the grabber hangs in: a line's worth, with the mark centred in it. */
static int inkcell_fb_sheet_grabber_h(const struct inkcell_draw_state *state) {
    return inkcell_fb_line_adv(state, inkcell_fb_type_scale(state, INKCELL_TYPE_LABEL));
}

static int inkcell_fb_sheet_title_h(const struct inkcell_draw_state *state,
                                    const struct inkcell_fb_sheet *sheet) {
    if (sheet == NULL || sheet->title == NULL || sheet->title[0] == '\0') {
        return 0;
    }
    return inkcell_fb_line_adv(state, inkcell_fb_type_scale(state, INKCELL_TYPE_TITLE)) +
           inkcell_fb_space(state, INKCELL_SPACE_SM);
}

int inkcell_fb_sheet_height(const struct inkcell_draw_state *state,
                            const struct inkcell_fb_sheet *sheet, int content_h) {
    if (state == NULL) {
        return 0;
    }
    /* The bottom inset only: the grabber already leaves air above the title. Flush with the
       edge of the panel below, because that edge is where the sheet came from. */
    return inkcell_fb_sheet_grabber_h(state) + inkcell_fb_sheet_title_h(state, sheet) +
           (content_h > 0 ? content_h : 0) + inkcell_fb_margin(state);
}

struct inkcell_fb_rect inkcell_fb_draw_sheet(const struct inkcell_draw_state *state,
                                             struct inkcell_fb_rect box,
                                             const struct inkcell_fb_sheet *sheet) {
    struct inkcell_fb_rect content = {0, 0, 0, 0};
    if (state == NULL || box.w <= 0 || box.h <= 0) {
        return content;
    }
    /*
     * The body's own margin, not the half-margin a panel stands off an edge by.
     *
     * A sheet is full bleed: it has no left or right edge to stand off, so what its content
     * lines up with is the *body behind it* - the rows the reader was looking at a moment ago.
     * Inset by a half-margin the sheet's words start eight pixels left of every row on the
     * screen it came from, which reads as a panel that missed.
     */
    const int pad = inkcell_fb_margin(state);
    const int edge = inkcell_fb_edge(state);
    const int radius = inkcell_fb_radius(state, INKCELL_SHAPE_LG);

    /*
     * Square at the bottom and round at the top, which is what says it came up from the edge
     * rather than being placed in the middle. It is the card list's own rule - a surface cut
     * by the panel keeps its corners square on the cut end, because a rounded corner where the
     * screen merely stopped is a surface claiming to end there.
     */
    inkcell_fb_fill_round_rect_ends(state, box.x, box.y, box.w, box.h, radius + edge,
                                    inkcell_fb_color(state, INKCELL_COLOR_OUTLINE), true, false);
    inkcell_fb_fill_round_rect_ends(
        state, box.x + edge, box.y + edge, box.w - 2 * edge, box.h - edge, radius,
        inkcell_fb_color(state, INKCELL_COLOR_SURFACE_HIGH), true, false);
    const enum inkcell_color ground = INKCELL_COLOR_SURFACE_HIGH;

    /*
     * The grabber. Not pressable - there is no touch on this device - and drawn anyway,
     * because it is the mark every platform has trained readers to read as "this came up, and
     * it can go back down". A sheet without it is a panel that looks as though it had always
     * been there.
     */
    const int grab_h = inkcell_fb_sheet_grabber_h(state);
    const int grab_w = box.w * INKCELL_FB_SHEET_GRABBER_NUM / INKCELL_FB_SHEET_GRABBER_DEN;
    const int bar = inkcell_fb_edge(state) * 2;
    inkcell_fb_fill_round_rect(state, box.x + (box.w - grab_w) / 2, box.y + (grab_h - bar) / 2,
                               grab_w, bar, bar / 2,
                               inkcell_fb_color(state, INKCELL_COLOR_OUTLINE));

    int y = box.y + grab_h;
    const int title_h = inkcell_fb_sheet_title_h(state, sheet);
    if (title_h > 0) {
        const int title_scale = inkcell_fb_type_scale(state, INKCELL_TYPE_TITLE);
        int right = box.x + box.w - pad;
        if (sheet->detail != NULL && sheet->detail[0] != '\0') {
            /* Against the trailing edge and in the quiet ink: a fact about the sheet rather
               than part of its name, which is the app bar's badge slot read one surface down.
               A caption, because that is what this is - "3 of 12", a size, a time - and the
               role's tabular figures are what stop such a line shifting as its number does. */
            const struct inkcell_type_style caption =
                inkcell_fb_type_style(state, INKCELL_TYPE_CAPTION);
            const int w = inkcell_fb_text_width_styled(state, sheet->detail, &caption);
            const int lift = (inkcell_fb_line_adv(state, title_scale) -
                              inkcell_fb_line_adv_styled(state, &caption)) /
                             2;
            inkcell_fb_draw_text_styled(state, right - w, y + lift, sheet->detail, &caption,
                                        inkcell_fb_color(state, INKCELL_COLOR_TEXT_DIM),
                                        inkcell_fb_color(state, ground));
            right -= w + inkcell_fb_space(state, INKCELL_SPACE_SM);
        }
        const int left = box.x + pad;
        char title[INKCELL_LINE_MAX];
        inkcell_fb_fit_text(state, sheet->title, title_scale, right - left, title, sizeof title);
        inkcell_fb_draw_text_weight(
            state, left, y, title, title_scale, inkcell_fb_type_weight(state, INKCELL_TYPE_TITLE),
            inkcell_fb_color(state, INKCELL_COLOR_TEXT), inkcell_fb_color(state, ground));
        y += title_h;
    }

    content.x = box.x + pad;
    content.y = y;
    content.w = box.w - 2 * pad;
    content.h = box.y + box.h - pad - y;
    if (content.w < 0) {
        content.w = 0;
    }
    if (content.h < 0) {
        content.h = 0;
    }
    return content;
}
