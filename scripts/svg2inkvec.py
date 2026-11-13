#!/usr/bin/env python3
"""SVG to inkvec: artwork as it comes out of an editor, as inkcell draws it (inkcell/ui/vector.h).

Everything SVG says that inkvec does not - XML, CSS, inheritance, shapes other than paths,
relative and shorthand commands, arcs, transforms, gradients in bounding-box units - is resolved
here, on the build machine, into filled outlines in absolute coordinates. What inkvec 1 cannot
show faithfully is refused with the element and the reason, rather than dropped: an icon that
silently lost its outline is worse than a build that says "convert the stroke to a path".

    scripts/svg2inkvec.py icon.svg > icon.inkvec
    scripts/svg2inkvec.py icon.svg -o icon.inkvec

Supported: path (every command, arcs included), rect (rounded too), circle, ellipse, polygon,
polyline; g and nested svg-free groups; transform (matrix, translate, scale, rotate, skewX,
skewY); fill as a colour or url(#linearGradient), fill-opacity, opacity, fill-rule, and the same
through style=""; linearGradient with stops, href to another gradient's stops, either
gradientUnits. Refused: a visible stroke, radialGradient, pattern, clipPath, mask, filter, text,
image, use.
"""

import argparse
import math
import re
import sys
import xml.etree.ElementTree as ET

SVG_NS = "{http://www.w3.org/2000/svg}"
XLINK_HREF = "{http://www.w3.org/1999/xlink}href"
COORD_MAX = 8192  # inkvec's bound on a coordinate, in view-box units

NAMED = {
    "black": (0, 0, 0), "white": (255, 255, 255), "red": (255, 0, 0), "green": (0, 128, 0),
    "lime": (0, 255, 0), "blue": (0, 0, 255), "yellow": (255, 255, 0), "cyan": (0, 255, 255),
    "aqua": (0, 255, 255), "magenta": (255, 0, 255), "fuchsia": (255, 0, 255),
    "gray": (128, 128, 128), "grey": (128, 128, 128), "silver": (192, 192, 192),
    "maroon": (128, 0, 0), "olive": (128, 128, 0), "navy": (0, 0, 128), "purple": (128, 0, 128),
    "teal": (0, 128, 128), "orange": (255, 165, 0),
}

REFUSED = {
    "radialGradient": "inkvec 1 has linear gradients only",
    "pattern": "inkvec 1 has no patterns",
    "clipPath": "inkvec 1 has no clipping; apply the clip in the editor",
    "mask": "inkvec 1 has no masks; flatten it in the editor",
    "filter": "inkvec 1 has no filters",
    "text": "convert text to paths in the editor",
    "image": "inkvec has no bitmaps",
    "use": "inkvec 1 does not resolve <use>; unlink the clone in the editor",
}

# Elements that draw nothing and need nothing from us.
IGNORED = {"title", "desc", "metadata", "style", "defs", "linearGradient", "stop", "namedview"}


class SvgError(Exception):
    pass


def local(tag):
    return tag.rsplit("}", 1)[-1] if isinstance(tag, str) else ""


# ---- affine transforms: (a, b, c, d, e, f) as SVG's matrix() -------------------------------

IDENTITY = (1.0, 0.0, 0.0, 1.0, 0.0, 0.0)


def mul(m, n):
    """m * n: n applied first, then m - so mul(parent, child) is the child's space in the
    parent's, and successive transforms in one attribute compose left to right as SVG says."""
    a, b, c, d, e, f = m
    A, B, C, D, E, F = n
    return (a * A + c * B, b * A + d * B, a * C + c * D, b * C + d * D,
            a * E + c * F + e, b * E + d * F + f)


def apply(m, x, y):
    a, b, c, d, e, f = m
    return a * x + c * y + e, b * x + d * y + f


def numbers(text):
    return [float(n) for n in re.findall(r"[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?", text)]


def parse_transform(text):
    m = IDENTITY
    for name, args in re.findall(r"(\w+)\s*\(([^)]*)\)", text or ""):
        v = numbers(args)
        if name == "matrix" and len(v) == 6:
            t = tuple(v)
        elif name == "translate" and v:
            t = (1, 0, 0, 1, v[0], v[1] if len(v) > 1 else 0)
        elif name == "scale" and v:
            t = (v[0], 0, 0, v[1] if len(v) > 1 else v[0], 0, 0)
        elif name == "rotate" and v:
            r = math.radians(v[0])
            t = (math.cos(r), math.sin(r), -math.sin(r), math.cos(r), 0, 0)
            if len(v) == 3:
                t = mul(mul((1, 0, 0, 1, v[1], v[2]), t), (1, 0, 0, 1, -v[1], -v[2]))
        elif name == "skewX" and v:
            t = (1, 0, math.tan(math.radians(v[0])), 1, 0, 0)
        elif name == "skewY" and v:
            t = (1, math.tan(math.radians(v[0])), 0, 1, 0, 0)
        else:
            raise SvgError(f"transform {name}({args}) is not one SVG has")
        m = mul(m, t)
    return m


# ---- colours and style ----------------------------------------------------------------------

def parse_color(text, where):
    text = text.strip().lower()
    if text.startswith("#"):
        h = text[1:]
        if len(h) == 3:
            h = "".join(ch * 2 for ch in h)
        if len(h) == 6 and re.fullmatch(r"[0-9a-f]{6}", h):
            return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))
    m = re.fullmatch(r"rgb\(\s*([^)]*)\)", text)
    if m:
        parts = [p.strip() for p in m.group(1).split(",")]
        if len(parts) == 3:
            return tuple(round(float(p[:-1]) * 2.55) if p.endswith("%") else int(float(p))
                         for p in parts)
    if text in NAMED:
        return NAMED[text]
    raise SvgError(f"{where}: colour {text!r} is not one this converter knows")


def style_of(el):
    """The element's presentation attributes, style="" winning over attributes as in CSS."""
    props = {k: v for k, v in el.attrib.items() if not k.startswith("{")}
    for decl in (el.get("style") or "").split(";"):
        if ":" in decl:
            k, v = decl.split(":", 1)
            props[k.strip()] = v.strip()
    return props


INHERITED = ("fill", "fill-opacity", "fill-rule", "stroke", "stroke-width", "stroke-opacity")


# ---- path data ------------------------------------------------------------------------------

TOKEN = re.compile(r"[MmZzLlHhVvCcSsQqTtAa]|[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?")


def arc_to_cubics(x0, y0, rx, ry, phi, large, sweep, x, y):
    """SVG's endpoint arc as cubic Beziers of at most a quarter turn each (SVG 1.1, F.6.5)."""
    if rx == 0 or ry == 0 or (x0 == x and y0 == y):
        return [] if (x0 == x and y0 == y) else [("L", (x, y))]
    rx, ry = abs(rx), abs(ry)
    cp, sp = math.cos(math.radians(phi)), math.sin(math.radians(phi))
    dx, dy = (x0 - x) / 2, (y0 - y) / 2
    x1p, y1p = cp * dx + sp * dy, -sp * dx + cp * dy
    lam = (x1p / rx) ** 2 + (y1p / ry) ** 2
    if lam > 1:
        rx, ry = rx * math.sqrt(lam), ry * math.sqrt(lam)
    num = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p
    den = rx * rx * y1p * y1p + ry * ry * x1p * x1p
    coef = math.sqrt(max(0.0, num / den)) if den else 0.0
    if large == sweep:
        coef = -coef
    cxp, cyp = coef * rx * y1p / ry, -coef * ry * x1p / rx
    cx = cp * cxp - sp * cyp + (x0 + x) / 2
    cy = sp * cxp + cp * cyp + (y0 + y) / 2

    def angle(ux, uy, vx, vy):
        a = math.atan2(ux * vy - uy * vx, ux * vx + uy * vy)
        return a

    t1 = angle(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry)
    dt = angle((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry)
    if not sweep and dt > 0:
        dt -= 2 * math.pi
    elif sweep and dt < 0:
        dt += 2 * math.pi
    segments = max(1, math.ceil(abs(dt) / (math.pi / 2) - 1e-9))
    step = dt / segments
    k = 4 / 3 * math.tan(step / 4)
    out = []

    def point(t):
        ct, st = math.cos(t), math.sin(t)
        return (cx + rx * ct * cp - ry * st * sp, cy + rx * ct * sp + ry * st * cp)

    def deriv(t):
        ct, st = math.cos(t), math.sin(t)
        return (-rx * st * cp - ry * ct * sp, -rx * st * sp + ry * ct * cp)

    t = t1
    for _ in range(segments):
        p0, p3 = point(t), point(t + step)
        d0, d3 = deriv(t), deriv(t + step)
        out.append(("C", (p0[0] + k * d0[0], p0[1] + k * d0[1],
                          p3[0] - k * d3[0], p3[1] - k * d3[1], p3[0], p3[1])))
        t += step
    return out


def parse_path(d, where):
    """Path data as absolute ("M"|"L"|"Q"|"C"|"Z", points) commands."""
    tokens = TOKEN.findall(d or "")
    i, out = 0, []
    cmd = None
    cx = cy = sx = sy = 0.0
    last_c = last_q = None  # the previous control point, for S and T

    def take(n):
        nonlocal i
        if i + n > len(tokens) or any(re.fullmatch(r"[A-Za-z]", t) for t in tokens[i:i + n]):
            raise SvgError(f"{where}: path data ends inside a command")
        vals = [float(t) for t in tokens[i:i + n]]
        i += n
        return vals

    while i < len(tokens):
        if re.fullmatch(r"[A-Za-z]", tokens[i]):
            cmd = tokens[i]
            i += 1
        elif cmd is None:
            raise SvgError(f"{where}: path data does not start with a command")
        rel = cmd.islower()
        c = cmd.upper()
        ox, oy = (cx, cy) if rel else (0.0, 0.0)
        if c == "Z":
            out.append(("Z", ()))
            cx, cy = sx, sy
            last_c = last_q = None
            continue
        if c == "M":
            x, y = take(2)
            cx, cy = sx, sy = x + ox, y + oy
            out.append(("M", (cx, cy)))
            cmd = "l" if rel else "L"  # further pairs are lines
            last_c = last_q = None
        elif c in "LHV":
            if c == "L":
                x, y = take(2)
                cx, cy = x + ox, y + oy
            elif c == "H":
                cx = take(1)[0] + ox
            else:
                cy = take(1)[0] + oy
            out.append(("L", (cx, cy)))
            last_c = last_q = None
        elif c in "CS":
            if c == "C":
                x1, y1, x2, y2, x, y = take(6)
                x1, y1 = x1 + ox, y1 + oy
            else:
                x2, y2, x, y = take(4)
                x1, y1 = (2 * cx - last_c[0], 2 * cy - last_c[1]) if last_c else (cx, cy)
            x2, y2, x, y = x2 + ox, y2 + oy, x + ox, y + oy
            out.append(("C", (x1, y1, x2, y2, x, y)))
            last_c, last_q = (x2, y2), None
            cx, cy = x, y
        elif c in "QT":
            if c == "Q":
                x1, y1, x, y = take(4)
                x1, y1 = x1 + ox, y1 + oy
            else:
                x, y = take(2)
                x1, y1 = (2 * cx - last_q[0], 2 * cy - last_q[1]) if last_q else (cx, cy)
            x, y = x + ox, y + oy
            out.append(("Q", (x1, y1, x, y)))
            last_q, last_c = (x1, y1), None
            cx, cy = x, y
        elif c == "A":
            rx, ry, phi, large, sweep, x, y = take(7)
            x, y = x + ox, y + oy
            out.extend(arc_to_cubics(cx, cy, rx, ry, phi, bool(large), bool(sweep), x, y))
            cx, cy = x, y
            last_c = last_q = None
        else:
            raise SvgError(f"{where}: path command {cmd!r}")
    return out


def num(el, name, default=0.0):
    v = el.get(name)
    if v is None:
        return default
    v = v.strip()
    if v.endswith("px"):
        v = v[:-2]
    if v.endswith("%"):
        raise SvgError(f"<{local(el.tag)}>: {name}={v!r} - percentages are not resolved")
    return float(v)


def shape_path(el):
    """A shape element as path commands, in its own user space."""
    tag = local(el.tag)
    where = f"<{tag} id={el.get('id')!r}>" if el.get("id") else f"<{tag}>"
    if tag == "path":
        return parse_path(el.get("d"), where)
    if tag == "rect":
        x, y, w, h = num(el, "x"), num(el, "y"), num(el, "width"), num(el, "height")
        rx, ry = el.get("rx"), el.get("ry")
        rx = float(rx) if rx is not None else (float(ry) if ry is not None else 0.0)
        ry = float(ry) if ry is not None else rx
        rx, ry = min(rx, w / 2), min(ry, h / 2)
        if w <= 0 or h <= 0:
            return []
        if rx <= 0 or ry <= 0:
            return [("M", (x, y)), ("L", (x + w, y)), ("L", (x + w, y + h)), ("L", (x, y + h)),
                    ("Z", ())]
        d = (f"M{x + rx},{y} H{x + w - rx} A{rx},{ry} 0 0 1 {x + w},{y + ry} V{y + h - ry} "
             f"A{rx},{ry} 0 0 1 {x + w - rx},{y + h} H{x + rx} A{rx},{ry} 0 0 1 {x},{y + h - ry} "
             f"V{y + ry} A{rx},{ry} 0 0 1 {x + rx},{y} Z")
        return parse_path(d, where)
    if tag in ("circle", "ellipse"):
        cx, cy = num(el, "cx"), num(el, "cy")
        if tag == "circle":
            rx = ry = num(el, "r")
        else:
            rx, ry = num(el, "rx"), num(el, "ry")
        if rx <= 0 or ry <= 0:
            return []
        d = (f"M{cx + rx},{cy} A{rx},{ry} 0 0 1 {cx},{cy + ry} A{rx},{ry} 0 0 1 {cx - rx},{cy} "
             f"A{rx},{ry} 0 0 1 {cx},{cy - ry} A{rx},{ry} 0 0 1 {cx + rx},{cy} Z")
        return parse_path(d, where)
    if tag in ("polygon", "polyline"):
        pts = numbers(el.get("points") or "")
        if len(pts) < 4:
            return []
        cmds = [("M", (pts[0], pts[1]))]
        cmds += [("L", (pts[k], pts[k + 1])) for k in range(2, len(pts) - 1, 2)]
        return cmds + [("Z", ())]
    raise SvgError(f"{where}: not a shape")


def bbox(cmds):
    xs = [p for _, pts in cmds for p in pts[0::2]]
    ys = [p for _, pts in cmds for p in pts[1::2]]
    return (min(xs), min(ys), max(xs), max(ys)) if xs else (0, 0, 0, 0)


# ---- the document ---------------------------------------------------------------------------

def fmt(v):
    v = round(v, 3)
    if abs(v) > COORD_MAX:
        raise SvgError(f"coordinate {v} is past inkvec's {COORD_MAX}")
    text = f"{v:.3f}".rstrip("0").rstrip(".")
    return "0" if text in ("-0", "") else text


def hex_color(rgb, alpha):
    a = max(0, min(255, round(alpha * 255)))
    text = "#%02x%02x%02x" % tuple(rgb)
    return text if a == 255 else text + "%02x" % a


class Converter:
    def __init__(self, root):
        self.root = root
        self.ids = {el.get("id"): el for el in root.iter() if el.get("id")}
        self.lines = []

    def gradient_stops(self, grad, seen=()):
        stops = [s for s in grad if local(s.tag) == "stop"]
        if not stops:
            href = grad.get("href") or grad.get(XLINK_HREF)
            if href and href.startswith("#") and href[1:] in self.ids and href not in seen:
                return self.gradient_stops(self.ids[href[1:]], seen + (href,))
        out = []
        for s in stops:
            st = style_of(s)
            off = st.get("offset", "0").strip()
            off = float(off[:-1]) / 100 if off.endswith("%") else float(off)
            off = max(0.0, min(1.0, off))
            if out and off < out[-1][0]:
                off = out[-1][0]  # SVG clamps an out-of-order stop to the one before
            rgb = parse_color(st.get("stop-color", "black"), "<stop>")
            out.append((off, rgb, float(st.get("stop-opacity", "1"))))
        if not out:
            raise SvgError(f"<linearGradient id={grad.get('id')!r}> has no stops")
        if len(out) > 8:
            raise SvgError(f"<linearGradient id={grad.get('id')!r}> has more than 8 stops")
        return out

    def paint(self, fill, alpha, cmds, ctm, where):
        """The paint words of a line, or None for no fill."""
        if fill is None or fill == "none":
            return None
        m = re.fullmatch(r"url\(\s*#([^)\s]+)\s*\)(?:\s+.*)?", fill)
        if not m:
            if fill == "currentColor":
                raise SvgError(f"{where}: fill currentColor - name the colour")
            return "fill " + hex_color(parse_color(fill, where), alpha)
        grad = self.ids.get(m.group(1))
        if grad is None or local(grad.tag) != "linearGradient":
            kind = local(grad.tag) if grad is not None else "missing"
            reason = REFUSED.get(kind, "not a linear gradient")
            raise SvgError(f"{where}: fill url(#{m.group(1)}) is {kind}: {reason}")
        units = grad.get("gradientUnits", "objectBoundingBox")
        bb = units == "objectBoundingBox"
        x1 = float(grad.get("x1", "0").rstrip("%")) / (100 if "%" in grad.get("x1", "") else 1)
        y1 = float(grad.get("y1", "0").rstrip("%")) / (100 if "%" in grad.get("y1", "") else 1)
        x2 = float(grad.get("x2", "100%" if bb else "0").rstrip("%")) / (
            100 if "%" in grad.get("x2", "100%" if bb else "") else 1)
        y2 = float(grad.get("y2", "0").rstrip("%")) / (100 if "%" in grad.get("y2", "") else 1)
        g = parse_transform(grad.get("gradientTransform"))
        if bb:
            bx0, by0, bx1, by1 = bbox(cmds)
            g = mul((bx1 - bx0, 0, 0, by1 - by0, bx0, by0), g)
        g = mul(ctm, g)
        (ax, ay), (bx, by) = apply(g, x1, y1), apply(g, x2, y2)
        stops = " ".join(f"{fmt(off)} {hex_color(rgb, op * alpha)}"
                         for off, rgb, op in self.gradient_stops(grad))
        return f"linear {fmt(ax)} {fmt(ay)} {fmt(bx)} {fmt(by)} {stops}"

    def walk(self, el, ctm, inherited, opacity):
        tag = local(el.tag)
        if tag in IGNORED:
            return
        where = f"<{tag} id={el.get('id')!r}>" if el.get("id") else f"<{tag}>"
        if tag in REFUSED:
            raise SvgError(f"{where}: {REFUSED[tag]}")
        st = style_of(el)
        if st.get("display") == "none" or st.get("visibility") == "hidden":
            return
        props = dict(inherited)
        props.update({k: st[k] for k in INHERITED if k in st})
        opacity *= float(st.get("opacity", "1"))
        ctm = mul(ctm, parse_transform(el.get("transform")))
        if tag in ("g", "svg", "a", "switch"):
            for child in el:
                self.walk(child, ctm, props, opacity)
            return
        if tag not in ("path", "rect", "circle", "ellipse", "polygon", "polyline", "line"):
            raise SvgError(f"{where}: inkvec 1 does not know this element")
        stroke = props.get("stroke", "none")
        if (stroke != "none" and float(props.get("stroke-width", "1")) > 0 and
                float(props.get("stroke-opacity", "1")) > 0):
            raise SvgError(f"{where}: a stroke - inkvec 1 fills outlines only; use the editor's "
                           "stroke to path (Inkscape: Path > Stroke to Path)")
        if tag == "line":
            return  # a line with no stroke draws nothing
        cmds = shape_path(el)
        if not cmds:
            return
        alpha = opacity * float(props.get("fill-opacity", "1"))
        paint = self.paint(props.get("fill", "black"), alpha, cmds, ctm, where)
        if paint is None or alpha <= 0:
            return
        if props.get("fill-rule") == "evenodd":
            paint += " evenodd"
        words = []
        last = None
        for c, pts in cmds:
            if c == "L" and pts == last:
                continue  # a zero-length line: a pill's flat side that has no length
            last = pts[-2:] if pts else last
            words.append(c)
            for k in range(0, len(pts), 2):
                x, y = apply(ctm, pts[k], pts[k + 1])
                words += [fmt(x), fmt(y)]
        self.lines.append(paint + " " + " ".join(words))

    def convert(self):
        root = self.root
        if local(root.tag) != "svg":
            raise SvgError("not an SVG document")
        vb = numbers(root.get("viewBox") or "")
        if len(vb) == 4:
            minx, miny, w, h = vb
        else:
            minx = miny = 0.0
            w, h = num(root, "width", 0), num(root, "height", 0)
        if w < 1 or h < 1:
            raise SvgError("the SVG has no viewBox or width and height of at least 1")
        ctm = (1, 0, 0, 1, -minx, -miny)
        for child in root:
            self.walk(child, ctm, {}, float(style_of(root).get("opacity", "1")))
        return "\n".join([f"inkvec 1 {fmt(w)} {fmt(h)}"] + self.lines) + "\n"


def convert(svg_text):
    """inkvec text for an SVG document's text; raises SvgError for what inkvec 1 cannot show."""
    try:
        root = ET.fromstring(svg_text)
    except ET.ParseError as e:
        raise SvgError(f"not XML: {e}") from None
    return Converter(root).convert()


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("svg")
    parser.add_argument("-o", "--output", help="where to write the inkvec (default: stdout)")
    args = parser.parse_args()
    with open(args.svg, encoding="utf-8") as f:
        text = f.read()
    try:
        out = convert(text)
    except SvgError as e:
        print(f"svg2inkvec: {args.svg}: {e}", file=sys.stderr)
        return 1
    if args.output:
        with open(args.output, "w", encoding="utf-8") as f:
            f.write(out)
    else:
        sys.stdout.write(out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
