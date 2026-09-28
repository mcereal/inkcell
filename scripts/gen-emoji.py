#!/usr/bin/env python3
"""Rasterise Noto Color Emoji into the C table the framebuffer backend draws from.

This is not part of the build. Run it by hand when the emoji set should change and commit
the generated file, so the build stays dependency-free and CI never reaches the network.

    python3 -m venv .venv && .venv/bin/pip install fonttools pillow
    curl -sSLo NotoColorEmoji.ttf \
        https://github.com/googlefonts/noto-emoji/raw/main/fonts/NotoColorEmoji.ttf
    .venv/bin/python scripts/gen-emoji.py NotoColorEmoji.ttf src/generated/emoji_glyphs.c

The font now lives at 2D/fonts/NotoColorEmoji.ttf in that repository.

Noto Color Emoji is under the SIL Open Font License; licenses/OFL-NotoColorEmoji.txt travels
with the generated data.
"""

import io
import sys
from collections import defaultdict

from fontTools.ttLib import TTFont
from PIL import Image

SIZE = 32          # sprite edge, in pixels - INKCELL_EMOJI_SIZE, which must match
ALPHA_FLOOR = 16   # below this a source pixel is transparent outright
ALPHA_LEVELS = 8   # opacity steps a run can carry; see alpha_level()
RUN_MAX = 256 // ALPHA_LEVELS  # the longest run the count bits left over can say
PALETTE_COLORS = 255  # index 0 is reserved for transparent


def glyph_bitmaps(font):
    """glyph name -> PNG bytes, from the CBDT strike."""
    out = {}
    for name, glyph in font["CBDT"].strikeData[0].items():
        data = glyph.data
        start = data.find(b"\x89PNG")
        if start >= 0:
            out[name] = data[start:]
    return out


def sequences(font):
    """codepoint tuple -> glyph name, for singles and for GSUB ligature sequences."""
    cmap = font.getBestCmap()
    reverse = {name: cp for cp, name in cmap.items()}
    out = {(cp,): name for cp, name in cmap.items()}

    # Ligatures carry the flags, keycaps and ZWJ sequences: a first glyph plus components,
    # all of which map back to codepoints through the cmap.
    for lookup in font["GSUB"].table.LookupList.Lookup:
        for table in lookup.SubTable:
            for first, ligatures in getattr(table, "ligatures", {}).items():
                first_cp = reverse.get(first)
                if first_cp is None:
                    continue
                for lig in ligatures:
                    parts = [reverse.get(c) for c in lig.Component]
                    if any(p is None for p in parts):
                        continue
                    out[(first_cp, *parts)] = lig.LigGlyph
    return out


def render(png, size):
    """PNG bytes -> a size x size RGBA image, letterboxed so nothing is stretched."""
    image = Image.open(io.BytesIO(png)).convert("RGBA")
    image.thumbnail((size, size), Image.LANCZOS)
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    canvas.paste(image, ((size - image.width) // 2, (size - image.height) // 2))
    return canvas


def build_palette(images):
    """One 255-colour palette shared by every sprite, so a sprite is one byte per pixel."""
    montage = Image.new("RGB", (SIZE, SIZE * len(images)), (0, 0, 0))
    for row, image in enumerate(images):
        montage.paste(image.convert("RGB"), (0, row * SIZE))
    quantized = montage.quantize(colors=PALETTE_COLORS, method=Image.MEDIANCUT)
    raw = quantized.getpalette()[: PALETTE_COLORS * 3]
    return quantized, [tuple(raw[i * 3 : i * 3 + 3]) for i in range(PALETTE_COLORS)]


def alpha_level(alpha):
    """An opaque-enough source alpha as one of ALPHA_LEVELS steps, the nearest of 31, 63, ...
    255 - what the runtime widens a level back to (`level << 5 | 31`).

    The edge is the point of keeping any of this. A sprite cut at one alpha threshold has a
    staircase for an outline, and scaling it smoothly only blurs the staircase; with its
    anti-aliasing kept, the outline survives being drawn at any size."""
    return max(0, min(ALPHA_LEVELS - 1, (alpha + 16) // 32 - 1))


def rle(pixels):
    """(head, index) pairs over (index, level) pixels, where head is the level in the top three
    bits and the run length less one in the bottom five. Emoji are flat fills inside a
    transparent margin, so this is where most of the raw megabytes go."""
    out = []
    current = pixels[0]
    run = 0
    for value in pixels:
        if value != current or run == RUN_MAX:
            out.append(((current[1] << 5) | (run - 1), current[0]))
            current = value
            run = 0
        run += 1
    out.append(((current[1] << 5) | (run - 1), current[0]))
    return out


def c_byte(byte):
    """One byte inside a C string literal, as itself when that is safe and shorter.

    Printable ASCII is one character instead of four, which is most of why the file is half the
    size it would be as hex escapes. The exceptions: the quote and the backslash need escaping,
    '?' could start a trigraph, and an octal escape swallows up to three digits - so every escape
    is three digits long, and a digit after one is unaffected."""
    if 0x20 <= byte < 0x7F and byte not in (0x22, 0x5C, 0x3F):
        return chr(byte)
    return "\\%03o" % byte


def c_string(data, per_line=48):
    """Binary as a C string literal rather than a braced list of integers.

    This is not cosmetic. A braced initialiser of a million elements is a million expressions
    for the compiler to parse and fold, which costs minutes and gigabytes; the same bytes as
    adjacent string literals are a handful of tokens and compile in a blink."""
    lines = []
    for i in range(0, len(data), per_line):
        chunk = data[i : i + per_line]
        lines.append('    "' + "".join(c_byte(b) for b in chunk) + '"')
    return "\n".join(lines) if lines else '    ""'


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 1
    font_path, out_path = sys.argv[1], sys.argv[2]

    font = TTFont(font_path)
    bitmaps = glyph_bitmaps(font)

    entries = []
    for codepoints, name in sorted(sequences(font).items()):
        png = bitmaps.get(name)
        if png is not None:
            entries.append((codepoints, render(png, SIZE)))

    quantized, palette = build_palette([image for _, image in entries])

    # Quantise, then fold identical sprites together: a third of the set is duplicates, most
    # of them sequences that differ only in a modifier the bitmap does not show.
    sprite_ids = []
    unique = {}
    runs = []
    offsets = []
    for row, (codepoints, image) in enumerate(entries):
        band = quantized.crop((0, row * SIZE, SIZE, (row + 1) * SIZE))
        alpha = image.getchannel("A")
        pixels = tuple(
            # Index 0 means transparent, so every opaque colour shifts up by one. A transparent
            # pixel carries level 0 so that the whole margin is one value and runs as one.
            (0, 0) if a < ALPHA_FLOOR else (index + 1, alpha_level(a))
            for index, a in zip(band.getdata(), alpha.getdata())
        )
        if pixels not in unique:
            unique[pixels] = len(offsets)
            offsets.append(len(runs) // 2)
            for head, index in rle(pixels):
                runs.append(head)
                runs.append(index)
        sprite_ids.append((codepoints, unique[pixels]))
    offsets.append(len(runs) // 2)

    singles = [(cp[0], i) for cp, i in sprite_ids if len(cp) == 1]
    multi = [(cp, i) for cp, i in sprite_ids if len(cp) > 1]
    singles.sort()
    # Longest first within a starting codepoint, so a lookup takes the greediest match.
    multi.sort(key=lambda item: (item[0][0], -len(item[0]), item[0]))

    tail = []
    tail_index = {}
    seq_rows = []
    for codepoints, sprite_index in multi:
        rest = tuple(codepoints[1:])
        if rest not in tail_index:
            tail_index[rest] = len(tail)
            tail.extend(rest)
        seq_rows.append((codepoints[0], tail_index[rest], len(codepoints), sprite_index))

    longest = max(len(cp) for cp, _ in sprite_ids)

    with open(out_path, "w") as f:
        w = f.write
        w("/*\n")
        w(" * Generated by scripts/gen-emoji.py from Noto Color Emoji - do not edit by hand.\n")
        w(" *\n")
        w(" * Noto Color Emoji is licensed under the SIL Open Font License v1.1; the licence\n")
        w(" * text is in licenses/OFL-NotoColorEmoji.txt and covers this derived data too.\n")
        w(" *\n")
        w(" * %d sprites over %d unique bitmaps, %d runs, %d palette colours.\n"
          % (len(sprite_ids), len(offsets) - 1, len(runs) // 2, len(palette)))
        w(" */\n\n")
        w('#include "inkcell/ui/emoji.h"\n\n')
        w("/* The pixel data below is one string literal of several hundred kilobytes. C99 only\n")
        w("   requires compilers to support 4095 characters, so a conforming compiler is right\n")
        w("   to warn; gcc and clang both handle megabytes, and the alternative - a braced list\n")
        w("   of a million integers - costs minutes of build time for the same bytes. */\n")
        w("#if defined(__GNUC__)\n")
        w('#pragma GCC diagnostic ignored "-Woverlength-strings"\n')
        w("#endif\n\n")

        w("/* One palette for every sprite. Index 0 is transparent and has no entry here, so\n")
        w("   a stored index of N means k_palette[N - 1]. */\n")
        w("static const uint8_t k_palette[%d][3] = {\n" % len(palette))
        for i in range(0, len(palette), 6):
            row = "".join("{0x%02X, 0x%02X, 0x%02X}, " % c for c in palette[i : i + 6])
            w("    " + row.rstrip() + "\n")
        w("};\n\n")

        w("/* Run-length pairs: a head byte - the opacity level in the top three bits, the run\n")
        w("   length less one in the bottom five - and the palette index it repeats.\n")
        w("   Written as a string literal so the compiler sees a few tokens instead of a\n")
        w("   million integer expressions; the trailing NUL a literal carries is unused. */\n")
        w("static const uint8_t k_runs[] =\n")
        w(c_string(runs))
        w(";\n\n")

        w("static const uint32_t k_run_offsets[] = {\n")
        for i in range(0, len(offsets), 12):
            w("    " + " ".join("%d," % o for o in offsets[i : i + 12]) + "\n")
        w("};\n\n")

        w("/* Single-codepoint emoji, sorted so a lookup can bisect. */\n")
        w("static const struct inkcell_emoji_single k_singles[] = {\n")
        for i in range(0, len(singles), 6):
            row = "".join("{0x%05X, %d}, " % s for s in singles[i : i + 6])
            w("    " + row.rstrip() + "\n")
        w("};\n\n")

        w("/* Multi-codepoint sequences - flags, keycaps, ZWJ - keyed by their first codepoint\n")
        w("   and ordered longest first, so the greediest match wins. */\n")
        w("static const uint32_t k_sequence_tail[] = {\n")
        for i in range(0, len(tail), 8):
            w("    " + " ".join("0x%05X," % c for c in tail[i : i + 8]) + "\n")
        w("};\n\n")

        w("static const struct inkcell_emoji_sequence k_sequences[] = {\n")
        for i in range(0, len(seq_rows), 4):
            row = "".join("{0x%05X, %d, %d, %d}, " % s for s in seq_rows[i : i + 4])
            w("    " + row.rstrip() + "\n")
        w("};\n\n")

        w("const struct inkcell_emoji_table inkcell_emoji_table = {\n")
        w("    .palette = k_palette,\n")
        w("    .palette_size = %d,\n" % len(palette))
        w("    .runs = k_runs,\n")
        w("    .run_offsets = k_run_offsets,\n")
        w("    .singles = k_singles,\n")
        w("    .single_count = %d,\n" % len(singles))
        w("    .sequences = k_sequences,\n")
        w("    .sequence_count = %d,\n" % len(seq_rows))
        w("    .sequence_tail = k_sequence_tail,\n")
        w("    .longest_sequence = %d,\n" % longest)
        w("};\n")

    data = len(runs) + len(offsets) * 4 + len(singles) * 8 + len(seq_rows) * 12 + len(tail) * 4
    print(
        "sprites %d over %d bitmaps (singles %d, sequences %d), runs %d, longest %d"
        % (len(sprite_ids), len(offsets) - 1, len(singles), len(seq_rows), len(runs) // 2, longest)
    )
    print("approx binary cost: %.0f KB" % (data / 1024.0))
    return 0


if __name__ == "__main__":
    sys.exit(main())
