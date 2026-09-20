# One binary built from many translation units: a framework and one file per subject area under
# suites/.
#
# Cases register themselves from constructors (see framework/inkcell_test.h), which is why the
# suite files are listed here directly instead of being collected into a static library first: a
# linker is free to drop library members nothing references, and it would take every test inside
# them with it, silently. Add a new suite file to INKCELL_TEST_SUITES and it runs.
set(INKCELL_TEST_SUITES
    suites/ui_anim.c
    suites/ui_frame.c
    suites/ui_icon.c
    suites/ui_layout.c
    suites/ui_shapes.c
    suites/ui_text.c
    suites/ui_theme.c
)

add_executable(inkcell_tests
    framework/inkcell_test.c
    ${INKCELL_TEST_SUITES}
)

target_include_directories(inkcell_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(inkcell_tests PRIVATE inkcell inkcell_options)

add_test(NAME inkcell_unit COMMAND inkcell_tests)
set_tests_properties(inkcell_unit PROPERTIES LABELS unit)

# The same two checks mesh-client runs over its own tree, because both rules are inkcell's:
# a renderer that spells out a word is the thing the string catalog exists to prevent, and the
# layer table is what keeps utils a leaf.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME inkcell_hardcoded_strings
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../scripts/check-strings.py)
    set_tests_properties(inkcell_hardcoded_strings PROPERTIES LABELS unit)
endif()

# ---- the golden sheet -------------------------------------------------------------------------
#
# The gallery renders every component and this checks that what came out is what came out last
# time. It is the only test the ten files under src/fb/widgets_*.c have: "the button looks
# right" is not a unit test anybody can write, and "the button looks like it did, and here is
# the picture of what changed" is.
#
# A digest rather than committed images, because a page is 1024x768 and sixty of them in git
# would be forty megabytes of binary that every clone pays for. When one differs, `make gallery`
# produces the pictures to look at; CI uploads them for the same reason.
if(TARGET inkcell_gallery)
    add_test(NAME inkcell_golden
             COMMAND inkcell_gallery --check ${CMAKE_CURRENT_SOURCE_DIR}/golden/manifest.txt)
    set_tests_properties(inkcell_golden PROPERTIES LABELS golden)
endif()
