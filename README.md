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
| **Fonts** | A 5x7 pixel face and a proportional UI face in two weights, all as coverage rather than 1-bit masks, resampled into whatever cell the theme asks for. |
| **Glyphs** | Emoji, icons and font tables, generated (`scripts/gen-*.py`) and committed. |
| **Layout** | Lines measured in *cells*, scroll windows, text wrapping done once for both the measure and the draw pass. |
| **Focus** | A d-pad answered against the rectangles the components drew - "right from here lands on *that*" - so a grid, a card with two verbs on it or a form with a chip row in it is a layout rather than an index somebody maintains. The cursor is one ring, and it travels. |
| **Widgets** | Buttons, chips, app bars, list rows, chat bubbles, cards, switches, segmented buttons, meters, charts, dialogs, snackbars, QR codes. |
| **Shapes** | Anti-aliased rounded rectangles, rings and arcs, in integers - so a curve is the same curve on every host that draws it. |
| **Framebuffer** | `/dev/fb0`, the page flip, damage tracking, a glyph cache, and an off-screen renderer for screenshots. |
| **Input** | evdev to a logical key, hat axes, analogue triggers, key repeat, per-device button profiles. |
| **i18n** | A catalog mechanism with plural rules and format-string validation. |
| **Gallery** | Every component, in every theme, rendered with no device attached - and the golden sheet that keeps them that way. |

## The rules it is built on

These are authoring rules — breaking one compiles and looks fine.

- **Nothing is spelled out in a renderer.** A screen names an *id* and something else answers: a
  string (`INKCELL_STR_*`), an icon (`INKCELL_ICON_*`), a tone, a family, a role, a shape.
  `scripts/check-strings.py` fails the build on prose in a component.
- **A widget takes a *tone*, never a colour** — the same reason a stylesheet has a token called
  "danger" instead of the hex for red: it is what lets a theme change the answer.
- **Text is measured, never counted.** A name written with one emoji is four bytes and one cell,
  and on the proportional face that cell is not the same width as the one beside it - so a
  `strlen`, a `%-12s`, or a cell count multiplied by the advance are all the same bug. Measure
  with `inkcell_fb_text_width()`, wrap and fit against pixels, and use `inkcell_fb_text_cols()`
  where a layout genuinely reserves whole columns. `inkcell_fb_char_adv()` is the *nominal*
  advance: an estimate, and exact only while the face is monospace.
- **What is drawn is what can be reached.** A frame collects the box of everything focusable as
  it draws it, and a press is resolved against those boxes (`inkcell_focus_find()`). The
  components do the collecting - a card registers the verbs it had room for, a strip the pills
  that fitted, a list the rows in its window - so a screen pushes a map in
  (`inkcell_fb_set_focus_map()`), gives things ids, and holds one id rather than a map of
  itself. A screen keeping a cursor *index* instead is a screen that will eventually walk onto
  the verb a card dropped for want of room, because an index cannot tell what came out on the
  panel and a registered rectangle is nothing but that. One ring is then drawn over the lot
  (`inkcell_fb_draw_focus_ring()`), and because it is one object rather than a property of each
  component it can do the thing none of them can: slide from the box the cursor left to the one
  it arrived at, taking that box's own shape as it lands.
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

/* 2b. And so do your icons. inkcell's are the ones a widget reaches for; `album` is yours. */
inkcell_icon_set_app_table(&myapp_icon_table);

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

## Seeing it

The components are not a list you have to take on trust. `examples/gallery` is a program built
on inkcell the way an application is - it links `inkcell::inkcell`, installs its own string
catalog, and writes screens - and it draws every component the library ships, in every theme, at
two scales.

```bash
make gallery     # 113 pages into build/gallery/, plus build/gallery/contact.png
```

It renders through `inkcell_capture`, which is the fb backend with the device taken out of it,
so this works in a container with no framebuffer anywhere near it.

### The golden sheet

The same program is the test suite for the ten files under `src/fb/widgets_*.c`. "The button
looks right" is not a unit test anybody can write; "the button looks like it did yesterday, and
here is the picture of what changed" is.

```bash
make test            # includes inkcell_golden: every page, against tests/golden/manifest.txt
make gallery         # when one differs, the pictures to look at
make gallery-update  # once they have been looked at, record them
```

The manifest holds a digest per page rather than the images themselves - sixty pages of 1024x768
in git is forty megabytes every clone pays for. CI renders and uploads the pictures on failure,
which is the only time anybody wants them. The clang job runs the whole sheet under ASan and
UBSan, so the widget code is sanitizer-covered by the same pass.

`make gallery-update` is the one command in this tree that can quietly approve a mistake. A
manifest regenerated without looking at `make gallery` is a test that agrees with whatever it is
handed.

## Building

**Linux only** — `epoll`, `timerfd`, `linux/fb.h`, `linux/input.h`.

```bash
make test        # debug build + ctest: the unit suite, the no-prose check, the golden sheet
make debug       # build only
make gallery     # the pictures (see above)
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
- **Translations.** The mechanism is here and so are inkcell's own twenty-three strings. A translation
  covers the catalog in force — both halves at once — so it belongs with the application.
- **Your vocabulary.** Strings and icons work the same way: inkcell ships only what a *widget*
  needs to put on a panel, and an application's own continue the ids from there
  (`inkcell_i18n_set_catalog`, `inkcell_icon_set_app_table`). `scripts/gen-icons.py` will
  rasterise an application's own `icons.def` — `--def`, `--macro` and `--symbol` — so there is
  no copy of the generator to keep in step.
- **Anything that knows what your app is about.** No store, no navigation model, no screens.

## Licence

MIT. See [LICENSE](LICENSE).
