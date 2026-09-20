# Examples are built when inkcell is top level and skipped when it is a subdirectory of an
# application, for the same reason the tests are: a consumer's build should not grow a binary
# it did not ask for.
add_subdirectory(gallery)
