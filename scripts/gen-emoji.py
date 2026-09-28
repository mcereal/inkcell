#!/usr/bin/env python3
"""Turn Twemoji's SVGs into the vector emoji pack inkcell draws from.

This is not part of the build. Run it by hand when the emoji set should change and commit the
generated file, so the build stays dependency-free and CI never reaches the network.

    python3 -m venv .venv && .venv/bin/pip install fonttools picosvg
    # the SVGs are assets/svg/ of https://github.com/jdecked/twemoji at a release tag
    .venv/bin/python scripts/gen-emoji.py twemoji/assets/svg src/generated/emoji_pack.c \\
        --pack build/emoji.pack

The C file is the pack compiled in; `--pack` also writes the same bytes as a file, for an
application that loads its emoji at run time (inkcell_emoji_load_file()) instead.

Twemoji's graphics are under CC-BY 4.0; licenses/CC-BY-4.0-Twemoji.txt travels with the
generated data, and an application that shows emoji owes Twemoji an attribution line.

What comes out is drawing instructions rather than pictures. Each emoji is a stack of layers,
each layer one colour and one outline, and the outline is TrueType's: closed contours of
points, each on the curve or a quadratic control point off it, two off-curve points in a row
implying an on-curve one between them. That is the shape of a COLRv0 font, and for the reason
fonts chose it - a quadratic is the cheapest curve to flatten, and it scales to any size
without a stored pixel to blur.

The pack layout is in include/inkcell/ui/emoji.h, beside the code that reads it.
"""

import argparse
import os
import struct
import sys
import xml.etree.ElementTree as ET

from fontTools.pens.cu2quPen import Cu2QuPen
from fontTools.pens.recordingPen import RecordingPen
from fontTools.svgLib.path import parse_path
from picosvg.svg import SVG

VERSION = 1
VIEW = 36.0         # Twemoji's viewBox edge; every file is 0 0 36 36 but one flag
GRID = 1152         # pack units across the em square: 32 per SVG unit
CU2QU_ERROR = 0.02  # most a quadratic may stray from the cubic it replaces, in SVG units
VS16 = 0xFE0F

NAMED = {"black": (0, 0, 0), "white": (255, 255, 255), "red": (255, 0, 0),
         "green": (0, 128, 0), "navy": (0, 0, 128), "blue": (0, 0, 255)}


def parse_colour(text):
    if text is None:
        return (0, 0, 0)  # SVG's default fill
    text = text.strip().lower()
    if text in NAMED:
        return NAMED[text]
    if text.startswith("#"):
        digits = text[1:]
        if len(digits) == 3:
            digits = "".join(c * 2 for c in digits)
        if len(digits) == 6:
            return tuple(int(digits[i:i + 2], 16) for i in (0, 2, 4))
    raise ValueError("unhandled fill %r" % text)


def codepoints_of(filename):
    """1f3f3-fe0f-200d-1f308.svg -> (0x1F3F3, 0x200D, 0x1F308).

    The variation selector comes out because the runtime matches with it taken out: VS16 only
    asks for the emoji presentation of whatever it follows, and people type it inconsistently,
    so a table keyed with it would miss every spelling that left it off."""
    stem = os.path.splitext(os.path.basename(filename))[0]
    return tuple(cp for cp in (int(part, 16) for part in stem.split("-")) if cp != VS16)


class Contours:
    """A pen that keeps what the rasteriser needs: closed contours of (x, y, on_curve) in pack
    units, on-curve first, with the duplicate closing point dropped."""

    def __init__(self, scale, dy):
        self.scale = scale
        self.dy = dy
        self.contours = []
        self.current = None

    def point(self, p, on):
        return (round(p[0] * self.scale), round((p[1] + self.dy) * self.scale), on)

    def moveTo(self, p):
        self.close()
        self.current = [self.point(p, True)]

    def lineTo(self, p):
        self.current.append(self.point(p, True))

    def qCurveTo(self, *points):
        if points[-1] is None:
            raise ValueError("an all-off-curve contour")
        for p in points[:-1]:
            self.current.append(self.point(p, False))
        self.current.append(self.point(points[-1], True))

    def curveTo(self, *points):
        raise ValueError("a cubic survived cu2qu")

    def closePath(self):
        self.close()

    endPath = closePath

    def close(self):
        contour = self.current
        self.current = None
        if not contour:
            return
        if len(contour) > 1 and contour[-1] == contour[0]:
            contour.pop()
        # Rounding onto the grid can fold a sliver into a line of coincident points. Consecutive
        # duplicates add nothing to an outline.
        tidy = [p for i, p in enumerate(contour) if i == 0 or p != contour[i - 1]]
        if len(tidy) >= 2:
            self.contours.append(tidy)


def outline(d, scale, dy):
    record = RecordingPen()
    parse_path(d, Cu2QuPen(record, CU2QU_ERROR, reverse_direction=False, all_quadratic=True))
    pen = Contours(scale, dy)
    record.replay(pen)
    pen.close()
    return pen.contours


def varint(value, out):
    while True:
        byte = value & 0x7F
        value >>= 7
        if value:
            out.append(byte | 0x80)
        else:
            out.append(byte)
            return


def zigzag(value):
    return (value << 1) ^ (value >> 31) if value >= 0 else ((-value) << 1) - 1


def encode_path(contours):
    """varint contours; per contour a varint point count, the on-curve flags as bits (point i
    is bit i % 8 of byte i / 8), then each point as zigzag varint deltas from the one before -
    carried across contours, starting from the origin."""
    out = bytearray()
    varint(len(contours), out)
    x = y = 0
    for contour in contours:
        varint(len(contour), out)
        flags = bytearray((len(contour) + 7) // 8)
        for i, (_, _, on) in enumerate(contour):
            if on:
                flags[i // 8] |= 1 << (i % 8)
        out += flags
        for px, py, _ in contour:
            varint(zigzag(px - x), out)
            varint(zigzag(py - y), out)
            x, y = px, py
    return bytes(out)


def load(path):
    """An SVG as [(rgba, contours)], bottom layer first."""
    pico = SVG.parse(path).topicosvg()
    root = ET.fromstring(pico.tostring())
    view = [float(v) for v in root.get("viewBox").split()]
    width, height = view[2], view[3]
    if abs(width - VIEW) > 1e-6 or view[0] != 0 or view[1] != 0:
        raise ValueError("unexpected viewBox %r" % view)
    scale = GRID / VIEW
    dy = (VIEW - height) / 2.0  # the one short flag sits centred, as it does in a line
    layers = []
    for element in root.iter():
        if not element.tag.endswith("path"):
            continue
        opacity = float(element.get("opacity", "1")) * float(element.get("fill-opacity", "1"))
        if element.get("fill") == "none" or opacity <= 0:
            continue
        rgb = parse_colour(element.get("fill"))
        contours = outline(element.get("d"), scale, dy)
        if contours:
            layers.append((rgb + (max(0, min(255, round(opacity * 255))),), contours))
    return layers


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("svg_dir")
    parser.add_argument("out_c")
    parser.add_argument("--pack", help="also write the pack's bytes to this file")
    args = parser.parse_args()

    names = sorted(n for n in os.listdir(args.svg_dir) if n.endswith(".svg"))
    colours, colour_index = [], {}
    paths, path_index = [], {}
    glyphs, glyph_index = [], {}
    entries = {}
    for name in names:
        key = codepoints_of(name)
        if key in entries:
            print("skipping %s: %s already spells it" % (name, entries[key][1]), file=sys.stderr)
            continue
        layers = []
        for rgba, contours in load(os.path.join(args.svg_dir, name)):
            if rgba not in colour_index:
                colour_index[rgba] = len(colours)
                colours.append(rgba)
            data = encode_path(contours)
            if data not in path_index:
                path_index[data] = len(paths)
                paths.append(data)
            layers.append((path_index[data], colour_index[rgba]))
        layers = tuple(layers)
        # A glyph is its layers, so two spellings drawn identically share one.
        if layers not in glyph_index:
            glyph_index[layers] = len(glyphs)
            glyphs.append(layers)
        entries[key] = (glyph_index[layers], name)

    if len(paths) > 0xFFFF or len(colours) > 0xFFFF or len(glyphs) > 0xFFFF:
        raise SystemExit("a layer's path or colour, or a glyph id, no longer fits 16 bits")

    singles = sorted((cp[0], g) for cp, (g, _) in entries.items() if len(cp) == 1)
    multi = [(cp, g) for cp, (g, _) in entries.items() if len(cp) > 1]
    # Longest first within a leading codepoint, so the first that fits is the greediest match.
    multi.sort(key=lambda item: (item[0][0], -len(item[0]), item[0]))
    tail, tail_index, sequences = [], {}, []
    for cp, g in multi:
        rest = tuple(cp[1:])
        if rest not in tail_index:
            tail_index[rest] = len(tail)
            tail.extend(rest)
        sequences.append((cp[0], tail_index[rest], g, len(cp)))
    longest = max(len(cp) for cp in entries)

    path_data = bytearray()
    path_offsets = []
    for data in paths:
        path_offsets.append(len(path_data))
        path_data += data
    path_offsets.append(len(path_data))
    path_data += b"\0" * (-len(path_data) % 4)

    layer_offsets, layer_rows = [], []
    for layers in glyphs:
        layer_offsets.append(len(layer_rows))
        layer_rows.extend(layers)
    layer_offsets.append(len(layer_rows))

    body = bytearray()
    body += b"".join(struct.pack("<4B", *c) for c in colours)
    body += b"".join(struct.pack("<I", o) for o in path_offsets)
    body += path_data
    body += b"".join(struct.pack("<I", o) for o in layer_offsets)
    body += b"".join(struct.pack("<HH", p, c) for p, c in layer_rows)
    body += b"".join(struct.pack("<IHH", cp, g, 0) for cp, g in singles)
    body += b"".join(struct.pack("<IIHBB", f, t, g, n, 0) for f, t, g, n in sequences)
    body += b"".join(struct.pack("<I", c) for c in tail)

    header = b"ICEM" + struct.pack(
        "<HHIIIIIIIIBBH", VERSION, GRID, len(colours), len(paths), len(path_data),
        len(glyphs), len(layer_rows), len(singles), len(sequences), len(tail), longest, 0, 0)
    pack = header + bytes(body)

    if args.pack:
        with open(args.pack, "wb") as f:
            f.write(pack)
    write_c(args.out_c, pack, len(entries), len(glyphs), len(paths), len(layer_rows),
            len(colours))
    print("%d emoji over %d glyphs, %d paths, %d layers, %d colours; pack %.0f KB"
          % (len(entries), len(glyphs), len(paths), len(layer_rows), len(colours),
             len(pack) / 1024.0))
    return 0


def c_byte(byte):
    """One byte inside a C string literal, as itself when that is safe and shorter.

    Printable ASCII is one character instead of four. The exceptions: the quote and the
    backslash need escaping, '?' could start a trigraph, and an octal escape swallows up to
    three digits - so every escape is three digits long, and a digit after one is unaffected."""
    if 0x20 <= byte < 0x7F and byte not in (0x22, 0x5C, 0x3F):
        return chr(byte)
    return "\\%03o" % byte


def write_c(path, pack, emoji, glyphs, paths, layers, colours):
    with open(path, "w") as f:
        w = f.write
        w("/*\n")
        w(" * Generated by scripts/gen-emoji.py from Twemoji - do not edit by hand.\n")
        w(" *\n")
        w(" * Twemoji's graphics are licensed under CC-BY 4.0; the licence text is in\n")
        w(" * licenses/CC-BY-4.0-Twemoji.txt and covers this derived data too.\n")
        w(" *\n")
        w(" * %d emoji over %d glyphs, %d paths, %d layers, %d colours; %d bytes.\n"
          % (emoji, glyphs, paths, layers, colours, len(pack)))
        w(" */\n\n")
        w('#include "emoji_internal.h"\n\n')
        w("/* One string literal of about a megabyte. C99 only requires compilers to support 4095\n")
        w("   characters, so a conforming compiler is right to warn; gcc and clang both handle\n")
        w("   megabytes, and a braced list of a million integers costs minutes of build time for\n")
        w("   the same bytes. Aligned so the pack's own four-byte alignment holds in memory. */\n")
        w("#if defined(__GNUC__)\n")
        w('#pragma GCC diagnostic ignored "-Woverlength-strings"\n')
        w("#endif\n\n")
        w("static _Alignas(4) const uint8_t k_pack[] =\n")
        for i in range(0, len(pack), 48):
            w('    "' + "".join(c_byte(b) for b in pack[i:i + 48]) + '"\n')
        w(";\n\n")
        w("const uint8_t *inkcell_emoji_builtin(size_t *size) {\n")
        w("    *size = sizeof k_pack - 1U; /* the literal's own NUL is not the pack's */\n")
        w("    return k_pack;\n")
        w("}\n")


if __name__ == "__main__":
    sys.exit(main())
