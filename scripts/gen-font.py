#!/usr/bin/env python3
"""Rasterise a face into the coverage table the UI draws its text from.

This is not part of the build. Run it by hand and commit the generated file, so the build stays
dependency-free and CI never reaches the network - the same arrangement scripts/gen-icons.py
has, and for the same reasons.

    python3 -m venv .venv && .venv/bin/pip install fonttools pillow
    .venv/bin/python scripts/gen-font.py Inter-Regular.ttf src/generated/font_ui_glyphs.c
    make format   # the table is emitted twelve values a line and clang-format repacks it

Monospace or proportional is not a flag: the script measures the face's own advances and
reports what it found. A face whose advances all agree is set on the grid it has always been
set on; one whose advances differ carries them per glyph into the table, and the UI steps the
pen by the glyph rather than by the cell. See inkcell_font_advance_cp().

The set of characters covered is not a list in this file: it is whatever the 5x7 font can draw,
read out of src/theme/font5x7.c, because a second font that covers less is a font that turns
some node names into boxes the moment a theme selects it. tests/suites/ui_theme.c holds the two
to that parity, so a face missing something fails the build rather than the name.

Why the geometry is what it is:

  CELL_H 32    The cell the device draws at: INKCELL_FONT_UI_CELL_H is 8 at scale 1 and the
               Brick's body scale is 4. Rasterising at exactly that size means the master is
               drawn 1:1 on the device and resampled only when a theme asks for another scale -
               so the face is as sharp as the panel can show it. One master unit is one device
               pixel at the body scale, which is what `master_scale` says.
  EM           The size the face is rasterised at, chosen so the cap height lands on CAP_TARGET.
               Searched rather than declared: two faces with the same nominal size draw
               capitals of quite different heights, and what has to match between them is the
               ink, because that is what sits beside an icon.
  BASELINE     The row in the master the pen sits on. Placed so the tallest accent in the
               covered set clears the top of the master and the deepest descender clears the
               bottom, which is a different character at each end and neither is the one the
               face's own ascent and descent describe.
  OVERHANG 4   Rows above the cell, for the diacritics that by definition sit above the cap
               height. Four is what the line gap leaves, and what an accented capital needs.
  SUPERSAMPLE  Rendered at 4x and resampled down, rather than asked for at 32 px directly.
               A hinted rasterisation snaps stems to whole pixels, which is what makes small
               bitmap text look mechanical; downsampling an unhinted one keeps the face's own
               proportions and puts the softness in the coverage where it belongs.

A proportional master is wider than the advance it carries, because a glyph is stored in a box
as wide as the widest one in the face and positioned inside it at its own left sidebearing.
Neighbouring boxes overlap when the text is drawn and that is fine: a zero-coverage span is
skipped, so only ink is painted. What varies per character is where the pen goes next.

Inter is licensed under the SIL Open Font License 1.1; licenses/OFL-1.1-Inter.txt travels with
the generated data. Point the script at another face to try one - the cell geometry is the
contract, not the file.
"""

import os
import re
import sys

from fontTools.ttLib import TTFont
from PIL import Image, ImageDraw, ImageFont

CELL_H = 32  # the cell's height, in device pixels at the body scale
OVERHANG = 4  # master rows above the cell, where the diacritics go
SUPERSAMPLE = 4
LEVELS = 16  # coverage values per pixel: 4 bits, 0 to 15

MASTER_H = CELL_H + OVERHANG

# The cap height to rasterise to, in master rows.
#
# This rather than a nominal point size is what keeps two faces interchangeable: an icon in a
# row slot is sized off the capitals beside it, and a face swapped in at the same nominal size
# but a different cap ratio moves every one of them. 21 is what the face this replaced drew.
CAP_TARGET = 21

# How much of the master a glyph may use before the run is failed. Antialiasing spreads a stroke
# a fraction of a pixel past the outline it came from, so a glyph drawn exactly to the edge
# bleeds a little over it and a zero tolerance would reject the geometry that best fills the
# cell. Half a pixel is the point at which the outermost column loses enough coverage to see.
SPILL_TOLERANCE = 0.5

# Characters a face is unlikely to draw, and the one it should draw instead. These are the
# typographic variants whose plain form is what this cell would show anyway - the same
# substitution src/theme/font5x7.c makes, listed here because a missing glyph in a face has to
# resolve to something rather than failing the run.
FALLBACK = {
    0x00A0: " ", 0x2002: " ", 0x2003: " ", 0x2007: " ", 0x2008: " ", 0x2009: " ",
    0x200A: " ", 0x202F: " ", 0x205F: " ", 0x3000: " ", 0x00AD: "-", 0x2010: "-",
    0x2011: "-", 0x2015: "-", 0x2212: "-", 0x201B: "'", 0x201F: '"', 0x2032: "'",
    0x2033: '"', 0x2044: "/",
}


def text_for(codepoint, cmap):
    """What to rasterise for `codepoint`.

    The substitution wins over the face's own glyph where there is one, rather than only filling
    a gap: these are the typographic variants whose plain form is what a cell this size would
    show anyway, and a face that draws a real fraction slash draws it full-height, which is a
    glyph the master has no room for and no reader would thank us for.
    """
    plain = FALLBACK.get(codepoint)
    if plain is not None:
        return plain
    return chr(codepoint) if codepoint in cmap else None


def coverage(path):
    """Every codepoint src/theme/font5x7.c can draw, as a sorted list.

    Parsed rather than hardcoded: the three tables there are the definition of what the UI can
    render, and a copy of them here would be a copy to keep in step.
    """
    text = open(path).read()
    found = set(range(0x20, 0x7F))  # the ASCII table, which is indexed rather than listed
    for name in ("k_literals", "k_aliases", "k_composed"):
        match = re.search(name + r"\[\]\s*=\s*\{(.*?)\n\};", text, re.S)
        if match is None:
            raise SystemExit("no %s table in %s" % (name, path))
        for entry in re.finditer(r"\{\s*0x([0-9A-Fa-f]{4,6})\s*,", match.group(1)):
            found.add(int(entry.group(1), 16))
    return sorted(found)


def cap_height(face):
    """How many master rows a rasterised 'H' stands, measured off the ink.

    Off the ink rather than out of the OS/2 table, because what the UI matches an icon to is
    what is on the panel - and after the supersample and the resample, that is this.
    """
    box = face.getbbox("H", anchor="ls")
    return (box[3] - box[1]) / float(SUPERSAMPLE)


def pick_em(font_path):
    """The rasterisation size whose cap height is CAP_TARGET master rows.

    A linear first guess off the face's own metrics, then a short walk: the relationship is very
    nearly linear but the rasteriser rounds, so the guess is usually right and occasionally one
    step out.
    """
    probe = 100 * SUPERSAMPLE
    face = ImageFont.truetype(font_path, probe)
    ratio = cap_height(face) / (probe / float(SUPERSAMPLE))
    em = int(round(CAP_TARGET / ratio))
    best, best_err = em, None
    for candidate in range(max(1, em - 4), em + 5):
        err = abs(cap_height(ImageFont.truetype(font_path, candidate * SUPERSAMPLE)) - CAP_TARGET)
        if best_err is None or err < best_err:
            best, best_err = candidate, err
    return best


def advances(face, cmap, wanted):
    """Each covered character's advance, in master units, and whether they all agree."""
    out = {}
    for codepoint in wanted:
        text = text_for(codepoint, cmap)
        if text is None:
            continue
        out[codepoint] = face.getlength(text) / float(SUPERSAMPLE)
    spread = max(out.values()) - min(out.values()) if out else 0.0
    return out, spread > 0.5


def left_bearing(face, cmap, wanted):
    """How far left of the pen any covered glyph reaches, in master units.

    Not zero, and not a rounding artefact: the accent on a narrow letter is centred on the stem
    and wider than it, so the circumflex of an 'i' starts left of the origin the advance is
    measured from. The master carries that much room on its left and the renderer draws the box
    that much before the pen - the horizontal mirror of the overhang, and for the same reason.
    """
    worst = 0.0
    for codepoint in wanted:
        text = text_for(codepoint, cmap)
        if text is None:
            continue
        box = face.getbbox(text, anchor="ls")
        worst = max(worst, -box[0] / float(SUPERSAMPLE))
    return worst


def render(face, text, master_w, baseline, pen):
    """One character as master_w x MASTER_H coverage values, 0 to LEVELS - 1.

    `pen` is where the origin sits in the master. A monospace face centres its glyph in the cell,
    which is what keeps its stems in the same column from one row of text to the next. A
    proportional one sets the pen at the master's left edge and lets the face's own sidebearings
    place the ink, because that is what the advance is measured from.
    """
    scale = SUPERSAMPLE
    pad = MASTER_H * scale
    canvas = Image.new("L", (master_w * scale + 2 * pad, MASTER_H * scale + 2 * pad), 0)
    ImageDraw.Draw(canvas).text((pad + pen * scale, pad + baseline * scale), text,
                                font=face, fill=255, anchor="ls")
    box = canvas.crop((pad, pad, pad + master_w * scale, pad + MASTER_H * scale))
    small = box.resize((master_w, MASTER_H), Image.LANCZOS)
    step = 255 // (LEVELS - 1)
    return [min(LEVELS - 1, (value + step // 2) // step) for value in small.getdata()], canvas


def spill(canvas, pad, master_w):
    """How far ink reached outside the master, in master units. 0 when the glyph fits."""
    ink = canvas.getbbox()
    if ink is None:
        return 0.0
    over = max(pad - ink[0], pad - ink[1],
               ink[2] - (pad + master_w * SUPERSAMPLE), ink[3] - (pad + MASTER_H * SUPERSAMPLE))
    return max(over, 0) / float(SUPERSAMPLE)


def pick_baseline(face, cmap, wanted, master_w, pen_for):
    """The baseline row that puts every covered glyph inside the master.

    Searched over the whole covered set rather than derived from the face's ascent and descent:
    the binding constraint at the top is an accented capital and at the bottom a cedilla, and
    neither is what those metrics describe.
    """
    # Over the whole master, not from the cell down: the baseline sits where the accents above
    # it and the descenders below it both clear, and that is nearer the middle of the master
    # than either edge.
    best = None
    for baseline in range(CAP_TARGET, MASTER_H + 1):
        worst = 0.0
        for codepoint in wanted:
            text = text_for(codepoint, cmap)
            if text is None:
                continue
            _, canvas = render(face, text, master_w, baseline, pen_for(codepoint))
            worst = max(worst, spill(canvas, MASTER_H * SUPERSAMPLE, master_w))
            if worst > SPILL_TOLERANCE:
                break
        if worst <= SPILL_TOLERANCE:
            return baseline, worst
        if best is None or worst < best[1]:
            best = (baseline, worst)
    return best


def trim(pixels, master_w):
    """The ink box of a master, as (x, y, w, h, values inside it).

    Trimming rather than run-length encoding, which is where the icon set stores its coverage.
    The difference is what the data looks like: a filled symbol is long runs of 15 and long runs
    of 0, and a letter at this size is neither - it is a small dense patch of varying coverage
    inside a mostly empty cell.
    """
    ink = [i for i, value in enumerate(pixels) if value]
    if not ink:
        return 0, 0, 0, 0, []
    xs = [i % master_w for i in ink]
    ys = [i // master_w for i in ink]
    x0, x1, y0, y1 = min(xs), max(xs) + 1, min(ys), max(ys) + 1
    box = [pixels[y * master_w + x] for y in range(y0, y1) for x in range(x0, x1)]
    return x0, y0, x1 - x0, y1 - y0, box


def pack(values):
    """Coverage packed two pixels to a byte, low nibble first."""
    out = bytearray()
    for i in range(0, len(values), 2):
        high = values[i + 1] if i + 1 < len(values) else 0
        out.append(values[i] | (high << 4))
    return out


def main():
    if len(sys.argv) not in (3, 4):
        print(__doc__)
        return 1
    font_path, out_path = sys.argv[1], sys.argv[2]
    # Which table the file defines. The regular face is the unsuffixed one; a second weight is
    # the same data under another name, so one generator serves both rather than a second script
    # that would drift from this one.
    suffix = sys.argv[3] if len(sys.argv) == 4 else ""
    table = "inkcell_font_ui%s_table" % (("_" + suffix) if suffix else "")
    # The repo root, spelled out rather than sliced off `__file__`: it is only absolute here
    # because Python has made it so since 3.9, and this script's whole job depends on finding
    # font5x7.c next to it.
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

    wanted = coverage(root + "/src/theme/font5x7.c")
    cmap = TTFont(font_path, lazy=True).getBestCmap()
    em = pick_em(font_path)
    face = ImageFont.truetype(font_path, em * SUPERSAMPLE)

    adv, proportional = advances(face, cmap, wanted)
    if not adv:
        print("the face draws nothing in the covered set", file=sys.stderr)
        return 1

    missing = [cp for cp in wanted if text_for(cp, cmap) is None]
    if missing:
        # Not a warning: a font that covers less than 5x7 turns a name the UI could draw into a
        # row of boxes, and it would do it only for the people whose names need those letters.
        print("the face has no glyph for, and no fallback for: "
              + ", ".join("U+%04X" % cp for cp in missing), file=sys.stderr)
        return 1

    if proportional:
        # The master holds the widest advance in the face, the room a glyph's ink may reach past
        # it - an overshoot on a round letter - and the room an accent needs to the left of the
        # pen. The pen sits `left` columns in, and the renderer draws the box that much early.
        left = int(-(-left_bearing(face, cmap, wanted) // 1))  # ceil
        master_w = int(round(max(adv.values()))) + left + 4
        nominal = sorted(adv[cp] for cp in adv if 0x61 <= cp <= 0x7A)
        nominal = nominal[len(nominal) // 2] if nominal else sum(adv.values()) / len(adv)
        pen_for = lambda cp: float(left)
    else:
        left = 0
        master_w = int(round(max(adv.values())))
        nominal = master_w
        pen_for = lambda cp: (master_w - adv[cp]) / 2.0

    baseline, worst = pick_baseline(face, cmap, wanted, master_w, pen_for)
    if worst > SPILL_TOLERANCE:
        print("no baseline fits the covered set in a %dx%d master (best spills %.2f)"
              % (master_w, MASTER_H, worst), file=sys.stderr)
        return 1

    blob = bytearray()
    entries = []
    for codepoint in wanted:
        text = text_for(codepoint, cmap)
        pixels, _ = render(face, text, master_w, baseline, pen_for(codepoint))
        x, y, w, h, box = trim(pixels, master_w)
        step = int(round(adv[codepoint]))
        entries.append((codepoint, len(blob), x, y, w, h, max(step, 0)))
        blob += pack(box)

    caps = int(round(cap_height(face)))
    face_name = TTFont(font_path, lazy=True)["name"].getDebugName(4) or font_path

    with open(out_path, "w") as f:
        w = f.write
        w("/*\n")
        w(" * Generated by scripts/gen-font.py from %s - do not edit by hand.\n" % face_name)
        w(" *\n")
        w(" * The face is licensed under the SIL Open Font License 1.1; the licence text is in\n")
        w(" * licenses/ and covers this derived data too.\n")
        w(" *\n")
        w(" * %d glyphs on a %dx%d master (%d cell rows under a %d-row overhang) at %d coverage\n"
          % (len(entries), master_w, MASTER_H, CELL_H, OVERHANG, LEVELS))
        w(" * levels, %d bytes of pixels, rasterised at %d px on a %d-row baseline. The coverage\n"
          % (len(blob), em, baseline))
        w(" * set is whatever src/theme/font5x7.c can draw, so the two fonts render the same\n")
        w(" * names.\n")
        w(" *\n")
        if proportional:
            w(" * Proportional: every glyph carries its own advance, and the master is wider than\n")
            w(" * any of them because a glyph is positioned inside it at its own sidebearing.\n")
        else:
            w(" * Monospace: every glyph steps the same advance, which is the master's width.\n")
        w(" */\n\n")
        w('#include "inkcell/ui/font_ui.h"\n\n')

        w("/* Every glyph's ink box, one after another: coverage packed two pixels to a byte,\n")
        w("   low nibble first, row by row within the box. */\n")
        w("static const uint8_t k_pixels[] = {\n")
        for i in range(0, len(blob), 12):
            w("    " + " ".join("0x%02X," % value for value in blob[i:i + 12]) + "\n")
        w("};\n\n")

        w("/* The glyphs, by ascending codepoint - which is what lets the lookup bisect. */\n")
        w("static const struct inkcell_font_ui_glyph k_glyphs[] = {\n")
        for codepoint, offset, x, y, gw, gh, step in entries:
            w("    {0x%04X, %6d, %2d, %2d, %2d, %2d, %2d},\n"
              % (codepoint, offset, x, y, gw, gh, step))
        w("};\n\n")

        w("const struct inkcell_font_ui_table %s = {\n" % table)
        w("    .pixels = k_pixels,\n")
        w("    .glyphs = k_glyphs,\n")
        w("    .count = %d,\n" % len(entries))
        w("    /* Measured off a rasterised 'H': what an icon beside the text is sized to. */\n")
        w("    .cap_rows = %d,\n" % caps)
        w("    .master_w = %d,\n" % master_w)
        w("    /* The pen sits this far into the master: the room an accent on a narrow letter\n")
        w("       needs to the left of the origin its advance is measured from. */\n")
        w("    .master_left = %d,\n" % left)
        w("    /* The advance a layout estimates with: the median lowercase, so a guess is wrong\n")
        w("       in both directions rather than always short. */\n")
        w("    .nominal = %d,\n" % int(round(nominal)))
        w("    .proportional = %s,\n" % ("true" if proportional else "false"))
        w("    .name = \"%s\",\n" % face_name)
        w("};\n")

    print("%s: %d glyphs, %d bytes, %s, master %dx%d (pen at +%d), em %d, baseline %d, "
          "cap %d, nominal %d"
          % (out_path, len(entries), len(blob),
             "proportional" if proportional else "monospace",
             master_w, MASTER_H, left, em, baseline, caps, int(round(nominal))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
