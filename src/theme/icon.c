#include "inkcell/ui/icon.h"

#include <string.h>

/* The glyph names, in enum order and from the same list the sprites were generated from. */
static const char *const k_names[INKCELL_ICON_COUNT] = {
    "",
#define INKCELL_ICON_ENTRY(id, glyph) glyph,
#include "inkcell/ui/icons.def"
#undef INKCELL_ICON_ENTRY
};

bool inkcell_icon_is_valid(enum inkcell_icon icon) {
    return icon > INKCELL_ICON_NONE && icon < INKCELL_ICON_COUNT;
}

const char *inkcell_icon_name(enum inkcell_icon icon) {
    return inkcell_icon_is_valid(icon) ? k_names[icon] : "";
}

void inkcell_icon_alpha(enum inkcell_icon icon,
                        uint8_t out[INKCELL_ICON_SIZE * INKCELL_ICON_SIZE]) {
    const size_t pixels = (size_t)INKCELL_ICON_SIZE * INKCELL_ICON_SIZE;
    if (out == NULL) {
        return;
    }
    memset(out, 0, pixels);
    if (!inkcell_icon_is_valid(icon)) {
        /* Including INKCELL_ICON_NONE, whose sprite is blank anyway: an empty slot and a slot
           holding an id from a newer build both come out as nothing drawn. */
        return;
    }

    const struct inkcell_icon_table *table = &inkcell_icon_table;
    const uint32_t start = table->run_offsets[icon];
    const uint32_t end = table->run_offsets[icon + 1U];

    size_t written = 0;
    for (uint32_t run = start; run < end && written < pixels; ++run) {
        const uint8_t count = table->runs[run * 2U];
        const uint8_t alpha = table->runs[run * 2U + 1U];
        for (uint8_t i = 0; i < count && written < pixels; ++i) {
            out[written++] = alpha;
        }
    }
}
