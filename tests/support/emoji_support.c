#include "support/emoji_support.h"

#include "inkcell/ui/emoji.h"

#if defined(INKCELL_TEST_EMOJI_HANDED_OVER)

/*
 * INKCELL_EMOJI is OFF, so the library holds no pack and every case that draws an emoji would
 * draw a replacement box. The suite instead compiles the generated pack in here under another
 * name and hands it over before any case runs - which is exactly what a program without a
 * built-in pack does with one it has mapped, and so the OFF build runs the whole suite through
 * the run-time path rather than skipping the half that draws.
 */
#define inkcell_emoji_builtin inkcell_test_emoji_pack
#include "emoji_pack.c"
#undef inkcell_emoji_builtin

int inkcell_test_emoji_restore(void) {
    size_t size = 0U;
    const uint8_t *bytes = inkcell_test_emoji_pack(&size);
    return inkcell_emoji_use_pack(bytes, size);
}

__attribute__((constructor)) static void inkcell_test_emoji_hand_over(void) {
    (void)inkcell_test_emoji_restore();
}

#else

int inkcell_test_emoji_restore(void) {
    return inkcell_emoji_use_builtin();
}

#endif
