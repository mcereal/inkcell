# The gallery: a program built on inkcell exactly as an application would be, and therefore the
# check that inkcell can be built on. It links inkcell::inkcell and nothing else, includes only
# the public headers, and reaches into src/ nowhere - if any of that stops being true the
# example stops building, which is the point of shipping one.
add_executable(inkcell_gallery
    main.c
    strings.c
    scenes.c
    scenes_buttons.c
    scenes_chrome.c
    scenes_controls.c
    scenes_list.c
    scenes_cards.c
    scenes_meters.c
    scenes_transcript.c
    scenes_overlays.c
    scenes_keyboard.c
    scenes_focus.c
    scenes_glide.c
    scenes_typography.c
    scenes_palette.c
    scenes_shapes.c
)

# Its own directory, so that catalog.def and gallery.h are found the way an application finds
# its own headers rather than through inkcell's include path.
target_include_directories(inkcell_gallery PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(inkcell_gallery PRIVATE inkcell::inkcell inkcell_options)
