# inkcell is built two ways and has to be right in both: on its own, where it owns the warning
# flags and builds its tests, and as an add_subdirectory() of an application, where it must not
# reach into the parent's targets or install anything. INKCELL_IS_TOP_LEVEL is what tells them
# apart - CMAKE_SOURCE_DIR equals this directory only in the first case.
cmake_minimum_required(VERSION 3.18)
project(inkcell VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(INKCELL_IS_TOP_LEVEL ON)
else()
    set(INKCELL_IS_TOP_LEVEL OFF)
endif()

option(INKCELL_BUILD_TESTS "Build inkcell's own test binary" ${INKCELL_IS_TOP_LEVEL})
option(INKCELL_BUILD_EXAMPLES "Build the component gallery" ${INKCELL_IS_TOP_LEVEL})
option(INKCELL_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(INKCELL_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)

# The same INTERFACE-target trick mesh-client uses, for the same reason: -fsanitize=address has
# to reach the compile *and* the link, and a variable holding one of them can be applied to a
# new target without the other. Always linked PRIVATE - this is how inkcell compiles, not
# something a consumer inherits.
add_library(inkcell_options INTERFACE)
target_compile_options(inkcell_options INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wshadow
    -Wcast-align
    -Wstrict-prototypes
    -Wmissing-prototypes
    -Wformat=2
)

if(INKCELL_ENABLE_ASAN)
    target_compile_options(inkcell_options INTERFACE -fsanitize=address)
    target_link_options(inkcell_options INTERFACE -fsanitize=address)
endif()

# -fno-sanitize-recover because UBSan otherwise prints a diagnostic and carries on, so the test
# that found the undefined behaviour still passes and CI still goes green.
if(INKCELL_ENABLE_UBSAN)
    target_compile_options(inkcell_options INTERFACE
                           -fsanitize=undefined -fno-sanitize-recover=undefined)
    target_link_options(inkcell_options INTERFACE -fsanitize=undefined)
endif()

add_library(inkcell STATIC
    # the vocabulary: colours by role, shapes, metrics, fonts, icons, emoji
    src/theme/theme.c
    src/theme/font.c
    src/theme/font5x7.c
    src/theme/font_ui.c
    src/theme/icon.c
    src/theme/emoji.c
    # the glyph tables scripts/gen-{emoji,icons,font}.py write. 42k lines and none of it read
    # by a human, which is why it is its own directory: a count of this codebase should be a
    # count of what somebody wrote.
    src/generated/emoji_glyphs.c
    src/generated/font_ui_glyphs.c
    src/generated/font_ui_bold_glyphs.c
    src/generated/icon_glyphs.c
    # measuring in cells, easing curves, chart arithmetic, press-to-panel timing
    src/layout.c
    src/anim.c
    src/trend.c
    src/latency.c
    # the on-screen keyboard's model: the grid, the panel ring, the edits. No drawing, which is
    # what lets the whole thing be tested without a panel.
    src/keyboard.c
    # where the d-pad goes: the rectangles a frame drew, and which one lies in a given
    # direction. A model for the keyboard's reason - no panel in it, and a second backend
    # resolving a press wants the same answer.
    src/focus.c
    # evdev to a key, and the button profile of the device under it
    src/input/input.c
    src/input/input_profile.c
    # the framebuffer backend: the panel, the primitives, and the components above them
    src/fb/fb.c
    src/fb/fb_draw.c
    src/fb/fb_capture.c
    src/fb/widgets_bubble.c
    src/fb/widgets_button.c
    src/fb/widgets_card.c
    src/fb/widgets_chrome.c
    src/fb/widgets_control.c
    src/fb/widgets_focus.c
    src/fb/widgets_item.c
    src/fb/widgets_keyboard.c
    src/fb/widgets_list.c
    src/fb/widgets_meter.c
    src/fb/widgets_overlay.c
    # the catalog mechanism and inkcell's own twenty-three strings
    src/i18n/strings.c
    # the leaves: no inkcell header above them includes one of these back
    src/utils/text.c
    src/utils/time.c
    src/utils/env.c
    src/utils/log.c
    src/utils/file.c
    src/utils/qr.c
)

target_include_directories(inkcell PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
# Private, so a consumer does not inherit a search path into src/: the one header down there
# that several files share (src/fb/list_internal.h) is included by its neighbours and is not
# part of the surface.
target_include_directories(inkcell PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/fb)
target_link_libraries(inkcell PRIVATE inkcell_options)
add_library(inkcell::inkcell ALIAS inkcell)

if(INKCELL_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(INKCELL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
