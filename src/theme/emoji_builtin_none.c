#include "emoji_internal.h"

/*
 * INKCELL_EMOJI=OFF: no pack compiled in.
 *
 * Every lookup misses, so an emoji draws as the replacement glyph an unknown character draws as,
 * and a pack handed over at run time (inkcell_emoji_use_pack(), inkcell_emoji_load_file()) is
 * the only way to have emoji at all - which is the point for a target whose flash is better
 * spent elsewhere, or that keeps the pack in a partition of its own.
 */
const uint8_t *inkcell_emoji_builtin(size_t *size) {
    *size = 0U;
    return NULL;
}
