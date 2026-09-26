# AGENTS.md

Contributor guide for inkcell. [`README.md`](README.md) says what the toolkit is and the rules it
is built on; this says how to work in it.

## The one-paragraph version

inkcell is a C17 UI toolkit for d-pad-first, small-screen interfaces, standing on
[inkwell](https://github.com/mcereal/inkwell) (the runtime). It draws one frame through one of
three backends - `/dev/fb0`, an SDL window, or an off-screen capture - and the rasteriser, the
widgets and every screen an application writes are the same code on all three. No threads, no
event loop of its own: the application's inkwell loop drives it. `make test` before every push.

SDL2 is the one optional dependency, optional by presence: without it `inkcell/ui/sdl.h` still
exists and reports itself unavailable.

## Layout

```
include/inkcell/ui/           the public surface: fb_draw.h, widgets.h and widgets/, theme, input, ...
include/inkcell/i18n/         the string catalog mechanism; catalog.def is inkcell's own words
src/fb/                       the framebuffer backend, the capture backend and the widgets
src/sdl/, src/input/          the window backend; evdev and the per-device button profiles
src/theme/                    palettes by role, fonts, icons
src/generated/                glyph tables written by scripts/gen-*.py - never edit, never format
examples/gallery/             every component in every theme; also the golden-sheet test
tests/framework/              the self-registering case runner
tests/suites/ui_*.c           one file per subject
tests/golden/manifest.txt     one digest per gallery page
scripts/check-strings.py      fails on prose in a renderer
scripts/check-platform.py     fails on a platform header in include/
```

## Style

- clang-format 18, config in `.clang-format`. `make format` before pushing; CI checks it.
  `src/generated/` is excluded on purpose: reflowing megabytes of glyph tables is an unreadable
  diff.
- `inkcell_` on everything public, `INKCELL_` on macros and enum members. A symbol carries the
  prefix of whoever owns it.
- A public header says in prose *why* it works the way it does. The reasoning is the valuable
  part; a signature can be read off the line below it.
- A new or changed public header wraps its declarations in `extern "C"` under `__cplusplus`.
  Not every existing header does yet, so a C++ caller cannot assume it.
- Functions return `0` or a negative `errno` for status, and the quantity itself when the answer
  is a quantity - inkwell's rule.

## Rules that compile fine when broken

The README's "rules it is built on" are the drawing rules (ids not prose, tones not colours,
scales not pixels, text measured not counted, focus from what was drawn). These are the rest:

- **No application vocabulary.** Not in code, not in comments. inkcell ships only what a
  *widget* needs to put on a panel; an application's words and icons continue inkcell's ids
  (`inkcell_i18n_set_catalog()`, `inkcell_icon_set_app_table()`). A string belongs in
  `catalog.def` only when inkcell itself prints it.
- **No kernel call above inkwell.** Register `INKWELL_LOOP_IN`/`_OUT`, never `EPOLLIN`, and
  take a timer, a wake or a descriptor from inkwell. Code that is genuinely one platform's - the
  fb backend, evdev - refuses elsewhere rather than disappearing behind an `#ifdef` in a public
  header. `check-platform.py` holds `include/` to that; the macOS CI job holds the rest.
- **No threads, no loop.** inkcell registers descriptors through `struct inkcell_input_host`
  and draws when it is asked for a frame.
- **`#include <inkstand/...>` or an application's headers is a layering bug.** inkcell stands on
  inkwell and nothing else of this stack.
- **The golden sheet is only as good as the looking.** A change under `src/fb/` that moves a
  pixel fails `inkcell_golden`. Run `make gallery`, look at the pages that changed, and only
  then `make gallery-update` - an unlooked-at manifest is a test that agrees with anything.
- **The glyph tables are generated.** Change the generator in `scripts/gen-*.py`, run it by hand,
  and commit its output; the build does not run it.

## Docs

The docs are reference: the README says what inkcell is and the rules it is built on, this file
says how to work in it, and each public header says why its component works the way it does.
**Planned work, open questions and roadmaps are tracked as issues, not written into the
repository.** A doc describes what is true now; it is not a log of how it got that way, and a
count that will go stale (pages, files, strings) is better left out.

## Tests

```bash
make test                                   # unit suite, both checks, and the golden sheet
make gallery                                # the pictures, into build/gallery/
./build/tests/inkcell_tests --list
./build/tests/inkcell_tests --filter focus  # substring match on the case name
./build/tests/inkcell_tests --suite ui_grid # substring match on the suite file
```

Cases register themselves from constructors, so adding one is a single `INKCELL_TEST_CASE` in a
single file. A new *suite file* goes in `INKCELL_TEST_SUITES` in `tests/CMakeLists.txt` - the
suites compile straight into the executable, because a linker is free to drop a library member
nothing references and would take every case in it along.

Sanitizers: `cmake -S . -B build -DINKCELL_ENABLE_ASAN=ON -DINKCELL_ENABLE_UBSAN=ON`. CI runs the
suite under both, and without SDL (`-DINKCELL_WITH_SDL=OFF`).

## Pull requests

- `make test` green, `make format` clean.
- Conventional Commits (`feat:`, `fix:`, `refactor:`, `docs:`, `test:`, `chore:`).
- A change that moves pixels says so, and names the gallery pages that changed.
