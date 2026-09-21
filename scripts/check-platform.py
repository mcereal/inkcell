#!/usr/bin/env python3
"""Fail when a platform's headers reach inkcell's public surface.

inkcell draws frames; it does not know what shows them. That claim is only worth something if
the headers an application includes can be *compiled* somewhere with no framebuffer, and for a
long time they could not: fb_draw.h named `struct fb_bitfield` to describe a pixel's channels
and so pulled <linux/fb.h> into every screen written against it.

The pixel format is inkcell's own now (`struct inkcell_pixel_format`), and the kernel's version
of it is converted once, in the one backend that asked an ioctl for it. This is the check that
keeps it that way - the compiler has no opinion, because on a Linux build host the include
simply works.

A backend under src/ may include whatever its platform needs; that is what a backend is for.
The rule is about include/, which is what an application sees.

Run it directly, or through `ctest` / `make test`.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Headers that belong to one way of showing a frame. A public inkcell header that includes one
# has decided, on behalf of every application, which platform it runs on.
PLATFORM_HEADERS = (
    "linux/fb.h",
    "linux/kd.h",
    "linux/input.h",
    "SDL.h",
    "SDL2/SDL.h",
    "GLES2/gl2.h",
    "EGL/egl.h",
    "xcb/xcb.h",
    "wayland-client.h",
)

INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.MULTILINE)


def main() -> int:
    failures = []
    for header in sorted((ROOT / "include").rglob("*.h")):
        text = header.read_text(encoding="utf-8", errors="replace")
        for included in INCLUDE.findall(text):
            if included in PLATFORM_HEADERS:
                failures.append(f"{header.relative_to(ROOT)}: includes <{included}>")

    if failures:
        print("A platform header reached inkcell's public surface:\n", file=sys.stderr)
        for line in failures:
            print(f"  {line}", file=sys.stderr)
        print(
            "\nDescribe the fact in inkcell's own terms and convert in the backend - see\n"
            "struct inkcell_pixel_format and inkcell_fb_format_from_var() in src/fb/fb.c.",
            file=sys.stderr,
        )
        return 1

    print(f"checked {len(list((ROOT / 'include').rglob('*.h')))} public headers")
    return 0


if __name__ == "__main__":
    sys.exit(main())
