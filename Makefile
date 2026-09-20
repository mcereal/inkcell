# Thin wrappers over cmake/ctest. The build types live in CMakeLists.txt; nothing here
# configures anything the plain cmake commands would not.

BUILD ?= build

.PHONY: all test debug format clean

all: debug

debug:
	cmake -S . -B $(BUILD) -G Ninja -DCMAKE_BUILD_TYPE=Debug
	cmake --build $(BUILD)

# The verify step, and what CI runs: the unit suite plus the check that no renderer spells out
# a word of prose.
test: debug
	ctest --test-dir $(BUILD) --output-on-failure

# The generated glyph tables are excluded deliberately - see the note in .github/workflows/ci.yml.
format:
	git ls-files '*.c' '*.h' | grep -v '^src/generated/' | xargs clang-format -i

clean:
	rm -rf $(BUILD)
