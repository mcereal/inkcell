#include "inkcell/ui/icon.h"

#include <string.h>

/* The glyph names, in enum order and from the same list the sprites were generated from. */
static const char *const k_names[INKCELL_ICON_COUNT] = {
    "",
#define INKCELL_ICON_ENTRY(id, glyph) glyph,
#include "inkcell/ui/icons.def"
#undef INKCELL_ICON_ENTRY
};

/*
 * The application's half, or nothing.
 *
 * A pointer rather than a copy for the same reason inkcell_i18n_set_catalog() keeps one: the
 * table is generated static data, and copying it would mean either a fixed ceiling on how many
 * icons an application may have or an allocation in a library that does not otherwise make one.
 */
static const struct inkcell_icon_app_table *s_app;

void inkcell_icon_set_app_table(const struct inkcell_icon_app_table *table) {
    /* A table that says it holds icons but points at no runs would index a NULL below, so it is
       turned away here rather than at the first frame that draws one of its ids. */
    if (table != NULL && (table->runs == NULL || table->run_offsets == NULL)) {
        s_app = NULL;
        return;
    }
    s_app = table;
}

size_t inkcell_icon_count(void) {
    return (size_t)INKCELL_ICON_COUNT + (s_app != NULL ? s_app->count : 0U);
}

bool inkcell_icon_is_valid(enum inkcell_icon icon) {
    return icon > INKCELL_ICON_NONE && (size_t)icon < inkcell_icon_count();
}

/* Which table `icon` falls in, and where in it. NULL for inkcell's own and for an id that is in
   neither, which the callers already have to handle. */
static const struct inkcell_icon_app_table *app_slot(enum inkcell_icon icon, size_t *index) {
    if (s_app == NULL || (size_t)icon < (size_t)INKCELL_ICON_COUNT) {
        return NULL;
    }
    const size_t offset = (size_t)icon - (size_t)INKCELL_ICON_COUNT;
    if (offset >= s_app->count) {
        return NULL;
    }
    *index = offset;
    return s_app;
}

const char *inkcell_icon_name(enum inkcell_icon icon) {
    if (!inkcell_icon_is_valid(icon)) {
        return "";
    }
    size_t index = 0U;
    const struct inkcell_icon_app_table *const app = app_slot(icon, &index);
    if (app != NULL) {
        return app->names != NULL ? app->names[index] : "";
    }
    return k_names[icon];
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

    size_t index = (size_t)icon;
    const uint8_t *runs = inkcell_icon_table.runs;
    const uint32_t *run_offsets = inkcell_icon_table.run_offsets;
    const struct inkcell_icon_app_table *const app = app_slot(icon, &index);
    if (app != NULL) {
        runs = app->runs;
        run_offsets = app->run_offsets;
    }

    const uint32_t start = run_offsets[index];
    const uint32_t end = run_offsets[index + 1U];

    size_t written = 0;
    for (uint32_t run = start; run < end && written < pixels; ++run) {
        const uint8_t count = runs[run * 2U];
        const uint8_t alpha = runs[run * 2U + 1U];
        for (uint8_t i = 0; i < count && written < pixels; ++i) {
            out[written++] = alpha;
        }
    }
}
