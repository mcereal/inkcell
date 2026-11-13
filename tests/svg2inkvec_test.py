#!/usr/bin/env python3
"""scripts/svg2inkvec.py: what an editor's SVG becomes, and what it refuses.

Run by ctest as inkcell_svg2inkvec. The C side (tests/suites/ui_vector.c) pins how inkvec is
drawn; this pins what SVG turns into it, down to the coordinates where they are exact.
"""

import math
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "scripts"))
import svg2inkvec  # noqa: E402

NS = 'xmlns="http://www.w3.org/2000/svg"'


def svg(body, attrs='viewBox="0 0 24 24"'):
    return f"<svg {NS} {attrs}>{body}</svg>"


def lines(body, attrs='viewBox="0 0 24 24"'):
    return svg2inkvec.convert(svg(body, attrs)).splitlines()


def coords(line):
    return [float(w) for w in line.split() if w.lstrip("-").replace(".", "", 1).isdigit()]


class Convert(unittest.TestCase):
    def test_header_is_the_view_box(self):
        self.assertEqual(lines("", 'viewBox="0 0 108 54"'), ["inkvec 1 108 54"])
        self.assertEqual(lines("", 'width="48" height="32"'), ["inkvec 1 48 32"])

    def test_view_box_origin_is_moved_to_zero(self):
        out = lines('<rect x="10" y="10" width="4" height="4" fill="#f00"/>',
                    'viewBox="10 10 24 24"')
        self.assertEqual(out[1], "fill #ff0000 M 0 0 L 4 0 L 4 4 L 0 4 Z")

    def test_relative_and_shorthand_become_absolute(self):
        out = lines('<path d="m2 2h10v10h-10z" fill="black"/>')
        self.assertEqual(out[1], "fill #000000 M 2 2 L 12 2 L 12 12 L 2 12 Z")
        out = lines('<path d="M0 0 C 0 4 4 4 4 0 S 8 -4 8 0 Q 10 4 12 0 T 16 0"/>')
        self.assertIn("C 0 4 4 4 4 0 C 4 -4 8 -4 8 0 Q 10 4 12 0 Q 14 -4 16 0", out[1])

    def test_implicit_lines_after_a_move(self):
        out = lines('<path d="M1 1 5 1 5 5z"/>')
        self.assertEqual(out[1], "fill #000000 M 1 1 L 5 1 L 5 5 Z")

    def test_arcs_become_quarter_turn_cubics(self):
        out = lines('<circle cx="12" cy="12" r="10" fill="#00f"/>')
        self.assertEqual(out[1].count(" C "), 4)
        pts = coords(out[1])
        xs, ys = pts[0::2], pts[1::2]
        self.assertAlmostEqual(min(xs), 2, places=2)
        self.assertAlmostEqual(max(ys), 22, places=2)
        # A half circle is two quarter turns.
        out = lines('<path d="M2 12 A10 10 0 0 1 22 12"/>')
        self.assertEqual(out[1].count(" C "), 2)

    def test_rounded_rect(self):
        out = lines('<rect x="2" y="2" width="20" height="20" rx="4" fill="red"/>')
        self.assertTrue(out[1].startswith("fill #ff0000 M 6 2 L 18 2 C"))

    def test_transforms_compose(self):
        out = lines('<g transform="translate(10 0)"><rect transform="scale(2)" '
                    'width="2" height="2"/></g>')
        self.assertEqual(out[1], "fill #000000 M 10 0 L 14 0 L 14 4 L 10 4 Z")
        out = lines('<rect transform="rotate(90 12 12)" x="12" y="0" width="0.001" height="2"/>')
        x, y = coords(out[1])[:2]
        self.assertAlmostEqual(x, 24, places=2)
        self.assertAlmostEqual(y, 12, places=2)

    def test_style_inheritance_and_opacity(self):
        out = lines('<g style="fill:#336699;opacity:0.5"><path d="M0 0L1 0L1 1Z" '
                    'fill-opacity="0.5"/></g>')
        self.assertTrue(out[1].startswith("fill #33669940 "))  # a quarter, 0x40
        out = lines('<g fill="none"><path d="M0 0L1 0L1 1Z"/></g>')
        self.assertEqual(len(out), 1, "fill none drew")
        out = lines('<path d="M0 0L1 0L1 1Z" fill-rule="evenodd" fill="rgb(255,0,0)"/>')
        self.assertTrue(out[1].startswith("fill #ff0000 evenodd M"))

    def test_linear_gradient_in_bounding_box_units(self):
        out = lines('<defs><linearGradient id="g"><stop offset="0" stop-color="#000"/>'
                    '<stop offset="100%" stop-color="#fff" stop-opacity="0.5"/>'
                    '</linearGradient></defs><rect x="4" y="4" width="16" height="8" '
                    'fill="url(#g)"/>')
        self.assertTrue(out[1].startswith("linear 4 4 20 4 0 #000000 1 #ffffff80 M 4 4"))

    def test_gradient_stops_through_href(self):
        out = lines('<defs><linearGradient id="a"><stop offset="0" stop-color="red"/>'
                    '<stop offset="1" stop-color="blue"/></linearGradient>'
                    '<linearGradient id="b" href="#a" gradientUnits="userSpaceOnUse" '
                    'x1="0" y1="0" x2="0" y2="24"/></defs>'
                    '<rect width="24" height="24" fill="url(#b)"/>')
        self.assertTrue(out[1].startswith("linear 0 0 0 24 0 #ff0000 1 #0000ff M"))

    def test_refusals_say_what_and_why(self):
        cases = [
            ('<path d="M0 0L5 5" stroke="black"/>', "Stroke to Path"),
            ('<defs><radialGradient id="r"/></defs><rect width="1" height="1" '
             'fill="url(#r)"/>', "linear gradients only"),
            ('<text>hi</text>', "convert text to paths"),
            ('<mask id="m"/>', "no masks"),
            ('<path d="M0 0L1"/>', "ends inside a command"),
            ('<path d="M0 0" fill="chartreuse-ish"/>', "colour"),
            ('<path d="M0 0 L 9000 0"/>', "past inkvec"),
        ]
        for body, reason in cases:
            with self.subTest(body=body):
                with self.assertRaises(svg2inkvec.SvgError) as caught:
                    svg2inkvec.convert(svg(body))
                self.assertIn(reason, str(caught.exception))
        with self.assertRaises(svg2inkvec.SvgError):
            svg2inkvec.convert(f"<svg {NS}></svg>")  # no size at all

    def test_a_stroke_of_none_or_zero_is_no_stroke(self):
        out = lines('<path d="M0 0L1 0L1 1Z" stroke="none"/>'
                    '<path d="M0 0L1 0L1 1Z" stroke="red" stroke-width="0"/>')
        self.assertEqual(len(out), 3)

    def test_numbers_are_rounded_to_thousandths(self):
        out = lines(f'<path d="M{math.pi} 0 L 1 1 L 0 1 Z"/>')
        self.assertIn("M 3.142 0", out[1])


if __name__ == "__main__":
    unittest.main()
