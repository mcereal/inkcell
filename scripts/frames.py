#!/usr/bin/env python3
"""Turn the gallery's pages into pictures.

`inkcell_gallery --out DIR` writes binary PPM, because the one thing the aarch64 static link
must not grow for a developer convenience is a zlib dependency - see inkcell_capture_write_ppm().
Compression is this script's job instead, on the host, out of the standard library.

    python3 scripts/frames.py build/gallery            # every .ppm beside itself as .png
    python3 scripts/frames.py build/gallery --sheet contact.png   # and one contact sheet

A contact sheet is what makes a theme reviewable: the same scene in four looks, side by side,
is a question a person can answer in a glance and cannot answer from four separate files.
"""

import argparse
import struct
import sys
import zlib
from pathlib import Path

# ---- PPM in ---------------------------------------------------------------------------------


def read_ppm(path):
    """(width, height, rgb bytes) from a binary PPM. Raises ValueError on anything else."""
    data = path.read_bytes()

    # The header is whitespace-separated tokens, with '#' to end of line as a comment. Parsing
    # it by hand rather than by split() because the pixel data that follows is binary and may
    # hold anything a split() would treat as a separator.
    pos = 0

    def token():
        nonlocal pos
        while pos < len(data):
            if data[pos : pos + 1].isspace():
                pos += 1
            elif data[pos : pos + 1] == b"#":
                while pos < len(data) and data[pos : pos + 1] != b"\n":
                    pos += 1
            else:
                break
        start = pos
        while pos < len(data) and not data[pos : pos + 1].isspace():
            pos += 1
        return data[start:pos]

    if token() != b"P6":
        raise ValueError(f"{path}: not a binary PPM")
    width = int(token())
    height = int(token())
    maxval = int(token())
    if maxval != 255:
        raise ValueError(f"{path}: only 8 bits per channel is supported, got maxval {maxval}")
    pos += 1  # exactly one whitespace byte separates the header from the data

    expected = width * height * 3
    pixels = data[pos : pos + expected]
    if len(pixels) != expected:
        raise ValueError(f"{path}: truncated: wanted {expected} bytes of pixels, got {len(pixels)}")
    return width, height, pixels


# ---- PNG out --------------------------------------------------------------------------------


def _chunk(kind, payload):
    return (
        struct.pack(">I", len(payload))
        + kind
        + payload
        + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)
    )


def _filter_rows(width, height, pixels):
    """Each row under the PNG filter that codes it smallest.

    A UI screenshot is mostly flat fill, so Sub and Up both collapse a row to near nothing and
    picking per row costs a few passes over 3 KB. The heuristic is libpng's: choose the filter
    whose output has the smallest sum of absolute signed differences, which correlates well
    enough with what deflate will do and is far cheaper than trying all five through zlib.
    """
    stride = width * 3
    out = bytearray()
    previous = bytearray(stride)
    for y in range(height):
        row = pixels[y * stride : (y + 1) * stride]
        candidates = []

        # 0: none
        candidates.append((0, row))
        # 1: sub - this pixel less the one to its left
        sub = bytearray(stride)
        for i in range(stride):
            left = row[i - 3] if i >= 3 else 0
            sub[i] = (row[i] - left) & 0xFF
        candidates.append((1, sub))
        # 2: up - this pixel less the one above it
        up = bytearray(stride)
        for i in range(stride):
            up[i] = (row[i] - previous[i]) & 0xFF
        candidates.append((2, up))

        def score(candidate):
            return sum(b if b < 128 else 256 - b for b in candidate[1])

        kind, encoded = min(candidates, key=score)
        out.append(kind)
        out.extend(encoded)
        previous = row
    return bytes(out)


def write_png(path, width, height, pixels):
    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)  # 8-bit truecolour RGB
    body = zlib.compress(_filter_rows(width, height, pixels), 9)
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n" + _chunk(b"IHDR", header) + _chunk(b"IDAT", body) + _chunk(b"IEND", b"")
    )


# ---- the contact sheet -----------------------------------------------------------------------


def contact_sheet(pages, columns, scale_down):
    """One image of every page, `columns` across, each shrunk by `scale_down`.

    Nearest-neighbour, because the sheet is an index rather than a rendering: what it has to
    show is which cell changed, and a box filter would blur exactly the hard edges this toolkit
    is made of.
    """
    if not pages:
        raise ValueError("nothing to put on a sheet")

    cell_w = pages[0][1] // scale_down
    cell_h = pages[0][2] // scale_down
    rows = (len(pages) + columns - 1) // columns
    sheet_w = cell_w * columns
    sheet_h = cell_h * rows
    sheet = bytearray(sheet_w * sheet_h * 3)

    for index, (_, width, height, pixels) in enumerate(pages):
        ox = (index % columns) * cell_w
        oy = (index // columns) * cell_h
        for y in range(min(cell_h, height // scale_down)):
            src = (y * scale_down) * width * 3
            dst = ((oy + y) * sheet_w + ox) * 3
            for x in range(min(cell_w, width // scale_down)):
                s = src + x * scale_down * 3
                sheet[dst + x * 3 : dst + x * 3 + 3] = pixels[s : s + 3]
    return sheet_w, sheet_h, bytes(sheet)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("directory", type=Path, help="where inkcell_gallery --out wrote its PPMs")
    parser.add_argument("--sheet", type=Path, help="also write a contact sheet here")
    parser.add_argument("--columns", type=int, default=4, help="cells across the sheet")
    parser.add_argument("--shrink", type=int, default=4, help="how far down to scale each cell")
    parser.add_argument("--keep-ppm", action="store_true", help="do not delete the PPMs after")
    args = parser.parse_args()

    sources = sorted(args.directory.glob("*.ppm"))
    if not sources:
        print(f"frames: no .ppm files in {args.directory}", file=sys.stderr)
        return 1

    pages = []
    for source in sources:
        width, height, pixels = read_ppm(source)
        write_png(source.with_suffix(".png"), width, height, pixels)
        pages.append((source.stem, width, height, pixels))
        if not args.keep_ppm:
            source.unlink()

    if args.sheet is not None:
        width, height, pixels = contact_sheet(pages, args.columns, args.shrink)
        args.sheet.parent.mkdir(parents=True, exist_ok=True)
        write_png(args.sheet, width, height, pixels)
        print(f"frames: {len(pages)} page(s), sheet at {args.sheet}")
    else:
        print(f"frames: {len(pages)} page(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
