# inkcell

A C17 UI toolkit for handheld Linux devices with a framebuffer and a gamepad — the TrimUI Brick
(NextUI/MinUI) first among them.

It is the part of [mesh-client](https://github.com/mcereal/mesh-client) that was never about
Meshtastic: the theme, the fonts and glyph tables, the layout arithmetic, the framebuffer
backend and its component set, and the evdev layer that turns a handheld's buttons into presses.

It stands on [inkwell](https://github.com/mcereal/inkwell), which is the part that was never
about *drawing* either: the epoll loop, the clock, the log, the environment knobs and the
codecs. Those lived here for a while because this library was extracted first, and a toolkit
that a platform layer has to depend on to write a log line has its arrows the wrong way round.

## What you get

| | |
|---|---|
| **Themes** | Colours by *role*, not by name. Four themes ship; a new one is a table. |
| **Fonts** | A 5x7 pixel face and a proportional UI face in two weights, all as coverage rather than 1-bit masks, resampled into whatever cell the theme asks for. |
| **Glyphs** | Emoji, icons and font tables, generated (`scripts/gen-*.py`) and committed. |
| **Layout** | Lines measured in *cells*, scroll windows, text wrapping done once for both the measure and the draw pass. |
| **Stacks** | A row or a column of unlike things, declared and then resolved: a basis, a share of what is left over, a floor to shrink to. What is placed adds up to the room exactly, and what will not fit is given up from the tail rather than drawn past the edge - so a screen states its shape instead of advancing a `y` cursor by hand. |
| **Width classes** | Compact, medium, expanded - measured in columns of body text rather than in pixels, so the same answer covers a 3.2" panel, a window dragged wide, and a reader who turned the text up. |
| **Grids** | A home screen: tiles laid out across and down, a window that scrolls by rows of them, and a press that knows a row's width. A list is the same window one column wide. |
| **Focus** | A d-pad answered against the rectangles the components drew - "right from here lands on *that*" - so a grid, a card with two verbs on it or a form with a chip row in it is a layout rather than an index somebody maintains. The cursor is one ring, and it travels. |
| **Motion** | Durations and curves as *tokens*, so a set of controls moves as one system - and the two things that move without being a control: the focus ring travelling between boxes, and a list gliding between windows instead of flicking between them. |
| **Widgets** | Buttons, chips, app bars, floating action buttons, list rows, tile grids, chat bubbles, cards, switches, segmented buttons, meters, charts, dialogs, menus, bottom sheets, snackbars, QR codes. |
| **Layers** | One z-stack for everything drawn *over* a screen: a box from a placement, an entrance and a shorter exit, a scrim over what is behind, and an answer to which overlay owns the press. A new overlay is its content and nothing else. |
| **Scrolling** | A body positioned in pixels rather than windowed by row index - so it can rest between two rows, give at its ends the way every touch platform does, and drive a large title that collapses into the app bar as it moves. |
| **Shapes** | Anti-aliased rounded rectangles, rings and arcs, in integers - so a curve is the same curve on every host that draws it. |
| **Framebuffer** | `/dev/fb0`, the page flip, damage tracking, a glyph cache, and an off-screen renderer for screenshots. |
| **Window** | The same frame in an SDL window: damage as texture uploads, a keyboard, and a development host that can run the UI instead of only screenshotting it. Optional - no SDL2, no window, everything else unchanged. |
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
- **A scale is not a pixel count.** It is counted in quarters of a glyph step
  (`INKCELL_SCALE_UNIT`), so that a type role can sit half a step above the body rather than a
  whole one - which is the difference between three type roles and Material's fifteen. Anything
  turning a scale into pixels goes through `inkcell_scale_px(steps, scale)`, and the very common
  "one step" case through `inkcell_step_px(scale)`. A bare scale in a pixel expression compiles,
  looks plausible and draws everything four times too large: it was the right arithmetic back
  when a scale was a whole multiplier, and every one of those sites is a bug now.

- **Text is measured, never counted.** A name written with one emoji is four bytes and one cell,
  and on the proportional face that cell is not the same width as the one beside it - so a
  `strlen`, a `%-12s`, or a cell count multiplied by the advance are all the same bug. Measure
  with `inkcell_fb_text_width()`, wrap and fit against pixels, and use `inkcell_fb_text_cols()`
  where a layout genuinely reserves whole columns. `inkcell_fb_char_adv()` is the *nominal*
  advance: an estimate, and exact only while the face is monospace.
- **A region of the frame has coordinates of its own.** The drawing layer has one transform
  for the *frame* - a screen sliding in - and a stack of them for regions inside it
  (`inkcell_fb_view_push()`). A scrolled body is content drawn at its own coordinates and cut
  where its window ends; an overlay is a panel that must not paint on the chrome while it is
  half way in. Both nest. The focus map goes through a view and deliberately not through the
  frame transform, and the difference is the whole reason there are two: a slide moves every
  box by one dx and changes no answer the map is asked for, while a view can move part of the
  frame *out of sight* - and a row scrolled past the top of its viewport is not a place to
  stand.
- **What is drawn is what can be reached.** A frame collects the box of everything focusable as
  it draws it, and a press is resolved against those boxes (`inkcell_focus_find()`). The
  components do the collecting - a card registers the verbs it had room for, a strip the pills
  that fitted, a list the rows in its window - so a screen pushes a map in
  (`inkcell_fb_set_focus_map()`), gives things ids, and holds one id rather than a map of
  itself. A screen keeping a cursor *index* instead is a screen that will eventually walk onto
  the verb a card dropped for want of room, because an index cannot tell what came out on the
  panel and a registered rectangle is nothing but that. What is *not* drawn - the three hundred
  rows of a list that is longer than its window - is a `struct inkcell_focus_run`, two numbers
  the screen already has, and `inkcell_focus_step()` answers a press against both halves at
  once. A grid says a third: how many of its ids are side by side, so that down from the tile
  the reader is on is a row further and not the next number. One ring is then drawn over the lot
  (`inkcell_fb_draw_focus_ring()`), and because it is one object rather than a property of each
  component it can do the thing none of them can: slide from the box the cursor left to the one
  it arrived at, taking that box's own shape as it lands.
- **Button hints are (button, string id) pairs**, never a sentence. A keycap is untranslated — it
  is what is printed on the case.

## Using it

Add it as a submodule and `add_subdirectory` it:

```bash
git submodule add https://github.com/mcereal/inkcell third_party/inkcell
git submodule update --init --recursive    # inkcell carries inkwell
```

```cmake
add_subdirectory(third_party/inkcell)
target_link_libraries(myapp PRIVATE inkcell::inkcell)
```

inkcell stands on [inkwell](https://github.com/mcereal/inkwell), the systems layer under it -
the loop, the clock, the log, the environment knobs, the codecs - and carries it as a submodule
of its own. An application that uses inkwell directly (most do: the loop is down there) should
`add_subdirectory()` its own copy *before* inkcell and link `inkwell::inkwell` itself; inkcell
only brings one in when nothing else has, so the whole tree builds one inkwell rather than two
targets of the same name.

Headers are `inkcell/ui/...`, `inkcell/i18n/...` and, for what moved down, `inkwell/base/...`.
The drawing toolkit
(`inkcell/ui/fb_draw.h`) and the components (`inkcell/ui/widgets.h`) are public: an application
built on inkcell writes the screens and nothing else, so what they are written against is the
library's surface.

### Wiring it up

inkcell never reaches into an application. Four things are pushed in rather than read out:

```c
/* 1. Your knobs and inkcell's share one namespace. Do this first. The prefix is inkwell's,
      because the environment is - inkcell reads `THEME` through it like everything else. */
inkwell_env_set_prefix("MYAPP");          /* MYAPP_THEME, MYAPP_FB_SCALE, ... */

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
make gallery     # 185 pages into build/gallery/, plus build/gallery/contact.png
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

**Linux is the target** — `linux/fb.h`, `linux/input.h`, and inkwell's `epoll` loop under them.

**macOS builds and passes the same suite as a development host**, so a UI can be worked on in a
window with no device: inkwell's loop is `kqueue` there, the fb backend and evdev compile to
refusals (`inkcell_backend_fb_is_available()` is false, `inkcell_input_init()` watches nothing),
and the SDL window is how anything is seen. The key codes the window sends are the evdev numbers
all the same, from `inkcell/ui/input_codes.h`, so a keycap means one thing on both. Nothing ships
for macOS; CI builds it so it does not rot. `brew install sdl2 ninja` is the setup.

SDL2 is the one optional dependency, and optional by *presence*: with it you get the window
backend, without it `inkcell/ui/sdl.h` still exists and reports itself unavailable. A plain
`git clone` builds and tests either way, and CI has a job for each - `INKCELL_WITH_SDL=OFF`
forces the second on a machine that has the library.

```bash
make test        # debug build + ctest: the unit suite, the no-prose check, the golden sheet
make debug       # build only
make gallery     # the pictures (see above)
make format      # clang-format, skipping the generated glyph tables
```

Sanitizers: `cmake -S . -B build -DINKCELL_ENABLE_ASAN=ON -DINKCELL_ENABLE_UBSAN=ON`. CI runs
the suite under both.

As a subdirectory of another project, the tests are off by default and inkcell installs nothing.

## Backends

A backend is six function pointers (`inkcell/ui/backend.h`) and the frame it presents is a
`struct inkcell_surface` - a pointer, a stride and a channel layout. Three of them ship:

| | |
|---|---|
| `inkcell_backend_fb()` | `/dev/fb0`. The device UI: changed spans copied into the mapping, `FBIOPAN_DISPLAY`, and the Brick's two-page mirror. |
| `inkcell_backend_sdl()` | An SDL window. The same rows, gathered into rectangles and uploaded to a streaming texture. |
| `inkcell_capture_*()` | No panel at all - a malloc'd page, which is what the gallery and the golden sheet render into. |

They share everything above the surface, which is the point: the rasteriser, the widgets and
every screen an application writes are the same code in all three, and the golden sheet holds
them to it. What differs is the last step, and `inkcell_fb_damage_rects()` and
`inkcell_fb_copy_damage()` are the two shapes that step comes in.

**The SDL backend is a presenter, not a GPU renderer.** The glyphs, the rounded rectangles and
the anti-aliasing are still the CPU's work; what moves to the GPU is the blit. Two
things about SDL do not fit one epoll loop and neither is hidden: it has no descriptor to wait
on, so its queue is drained from an inkwell timer registered through `struct inkcell_input_host`, and
`SDL_RenderPresent()` blocks under vsync, so vsync is off unless `<PREFIX>_SDL_VSYNC` asks for
it. See the header.

**A window re-measures when it is dragged.** The surface is reallocated at the window's new size
and the application is asked for a frame that shape, so the width classes
(`inkcell/ui/stack.h`) see the room the window actually has - a window dragged wide gets the
expanded layout rather than the handheld one, larger. It still *opens* at the device's geometry,
so a layout that only works at desktop proportions is caught the moment it comes up.
`<PREFIX>_SDL_FIXED` pins the frame at its opening size and scales it to the window instead,
which is what you want when the window is standing in for the device rather than being a
surface of its own.

## What is deliberately not here

- **An event loop.** An application has one already; a UI library that brought a second would be
  asking every app to run two. See `struct inkcell_input_host`.
- **A scene driver for screenshots.** `inkcell_capture_*` renders a frame off-screen, which is
  the reusable half. Driving an app through a scripted sequence of presses is a script against
  *that app's* navigation, so it lives with the app.
- **A GPU rasteriser.** The SDL backend uploads a software-rendered frame; it does not draw
  glyphs or shapes on the GPU. That would be a glyph atlas, signed-distance-field rounded
  rectangles and a second renderer to keep in step with this one, and it is worth doing only
  once there is a measurement on the device saying the upload is not enough.
- **A pad over SDL.** SDL's game-controller layer brings a button mapping of its own, and
  whether it agrees with the profile in `src/input/input_profile.c` about a given handheld is a
  question for that handheld. A window is driven from a keyboard; a device is driven from
  evdev.
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
