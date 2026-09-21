# Thin wrappers over cmake/ctest. The build types live in CMakeLists.txt; nothing here
# configures anything the plain cmake commands would not.

BUILD ?= build

.PHONY: all test debug format clean gallery gallery-update

all: debug

debug:
	cmake -S . -B $(BUILD) -G Ninja -DCMAKE_BUILD_TYPE=Debug
	cmake --build $(BUILD)

# The verify step, and what CI runs: the unit suite plus the check that no renderer spells out
# a word of prose.
test: debug
	ctest --test-dir $(BUILD) --output-on-failure

# ---- the component gallery -------------------------------------------------------------------
#
# `make gallery` renders every scene in every theme at every scale and leaves the pictures in
# $(SHOTS). That is the sheet to look at before and after a change to anything under src/fb/,
# and the contact sheet is the one image that shows whether a change touched a scene nobody
# expected it to.
#
# `make gallery-update` records what came out as the new golden manifest. Only ever run it when
# the pictures have been *looked at* - the manifest is what stops a rendering change going in
# unnoticed, and a manifest updated without looking is a test that agrees with whatever it is
# handed.
SHOTS ?= $(BUILD)/gallery

gallery: debug
	rm -rf $(SHOTS) && mkdir -p $(SHOTS)
	$(BUILD)/examples/gallery/inkcell_gallery --out $(SHOTS) > /dev/null
	python3 scripts/frames.py $(SHOTS) --sheet $(SHOTS)/contact.png

gallery-update: debug
	$(BUILD)/examples/gallery/inkcell_gallery > tests/golden/manifest.txt
	@echo "recorded $$(grep -vc '^#' tests/golden/manifest.txt) page(s); review the diff"

# The generated glyph tables are excluded deliberately - see the note in .github/workflows/ci.yml.
#
# Untracked files are included, and that is the whole of why this is two commands rather than
# one. `git ls-files` lists what git is *tracking*, so a file written and not yet added was
# silently skipped - which means the one moment a formatter is most needed, on a file nobody
# has ever run it over, is the one moment it did nothing. The CI check runs after the add and
# caught it there instead, which is a round trip for something the author could have been told
# locally. --others --exclude-standard is the untracked half, minus whatever .gitignore says.
format:
	git ls-files --cached --others --exclude-standard '*.c' '*.h' \
	  | grep -v '^src/generated/' | xargs clang-format -i

clean:
	rm -rf $(BUILD)
