# inkcell

A C17 UI toolkit for handheld Linux devices with a framebuffer and a gamepad — the TrimUI Brick
(NextUI/MinUI) first among them.

It is the part of [mesh-client](https://github.com/mcereal/mesh-client) that was never about
Meshtastic: the theme, the fonts and glyph tables, the layout arithmetic, the framebuffer
backend and its component set, and the evdev layer that turns a handheld's buttons into presses.

## What you get

| | |
|---|---|
| **Themes** | Colours by *role*, not by name. Four themes ship; a new one is a table. |
| **Fonts** | A 5x7 pixel face and a rasterised UI face, both as coverage rather than 1-bit masks, resampled into whatever cell the theme asks for. |
| **Glyphs** | Emoji, icons and font tables, generated (`scripts/gen-*.py`) and committed. |
| **Layout** | Lines measured in *cells*, scroll windows, text wrapping done once for both the measure and the draw pass. |
| **Widgets** | Buttons, chips, app bars, list rows, chat bubbles, cards, switches, segmented buttons, meters, charts, dialogs, snackbars, QR codes. |
| **Framebuffer** | `/dev/fb0`, the page flip, damage tracking, a glyph cache, and an off-screen renderer for screenshots. |
| **Input** | evdev to a logical key, hat axes, analogue triggers, key repeat, per-device button profiles. |
| **i18n** | A catalog mechanism with plural rules and format-string validation. |

## The rules it is built on

These are authoring rules — breaking one compiles and looks fine.

- **Nothing is spelled out in a renderer.** A screen names an *id* and something else answers: a
  string (`INKCELL_STR_*`), an icon (`INKCELL_ICON_*`), a tone, a family, a role, a shape.
  `scripts/check-strings.py` fails the build on prose in a component.
- **A widget takes a *tone*, never a colour** — the same reason a stylesheet has a token called
  "danger" instead of the hex for red: it is what lets a theme change the answer.
- **Layout is measured in cells, not bytes.** A name written with one emoji is four bytes and one
  column, so a `strlen` or a `%-12s` in a component is a bug. There is no byte-counting entry
  point to reach for.
- **Button hints are (button, string id) pairs**, never a sentence. A keycap is untranslated — it
  is what is printed on the case.

## Using it

Add it as a submodule and `add_subdirectory` it:

```bash
git submodule add https://github.com/mcereal/inkcell third_party/inkcell
```

```cmake
add_subdirectory(third_party/inkcell)
target_link_libraries(myapp PRIVATE inkcell::inkcell)
```

Headers are `inkcell/ui/...`, `inkcell/utils/...`, `inkcell/i18n/...`. The drawing toolkit
(`inkcell/ui/fb_draw.h`) and the components (`inkcell/ui/widgets.h`) are public: an application
built on inkcell writes the screens and nothing else, so what they are written against is the
library's surface.

### Wiring it up

inkcell never reaches into an application. Four things are pushed in rather than read out:

```c
/* 1. Your knobs and inkcell's share one namespace. Do this first. */
inkcell_env_set_prefix("MYAPP");          /* MYAPP_THEME, MYAPP_FB_SCALE, ... */

/* 2. Your words continue inkcell's. Both halves, one table - see inkcell/i18n/strings.h. */
inkcell_i18n_set_catalog(&my_catalog);
inkcell_i18n_init();

/* 3. inkcell does not own an event loop; yours registers the input descriptors. */
struct inkcell_input_host host = {
    .ctx = &my_loop, .add_fd = my_add_fd, .remove_fd = my_remove_fd,
    .request_stop = my_request_stop,
};
inkcell_input_init(&input, &host);

/* 4. The frame is yours to draw. The snapshot is a void * inkcell never looks inside. */
inkcell_fb_set_app(state, &(struct inkcell_fb_app){ .ctx = app, .render = my_render });
```

Two facts a frame needs that no snapshot carries are pushed in the same way: which way the
reader just moved (`inkcell_fb_transition_begin()` — what counts as "further in" is your
question) and which theme they chose (`inkcell_fb_state_set_theme_by_id()`).

## Building

**Linux only** — `epoll`, `timerfd`, `linux/fb.h`, `linux/input.h`.

```bash
make test        # debug build + ctest: the unit suite and the no-prose check
make debug       # build only
make format      # clang-format, skipping the generated glyph tables
```

Sanitizers: `cmake -S . -B build -DINKCELL_ENABLE_ASAN=ON -DINKCELL_ENABLE_UBSAN=ON`. CI runs
the suite under both.

As a subdirectory of another project, the tests are off by default and inkcell installs nothing.

## What is deliberately not here

- **An event loop.** An application has one already; a UI library that brought a second would be
  asking every app to run two. See `struct inkcell_input_host`.
- **A scene driver for screenshots.** `inkcell_capture_*` renders a frame off-screen, which is
  the reusable half. Driving an app through a scripted sequence of presses is a script against
  *that app's* navigation, so it lives with the app.
- **Translations.** The mechanism is here and so are inkcell's own fifteen strings. A translation
  covers the catalog in force — both halves at once — so it belongs with the application.
- **Anything that knows what your app is about.** No store, no navigation model, no screens.

## Licence

MIT. See [LICENSE](LICENSE).
