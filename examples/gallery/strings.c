#define _POSIX_C_SOURCE 200809L

/*
 * The gallery's catalog, assembled the way an application's is.
 *
 * Both halves in one table, inkcell's first and the gallery's continuing: an id *is* the table
 * index, so inkcell_str() stays a bounds check and a load with no branch on which half an id
 * fell in. See the tail of include/inkcell/i18n/strings.h, which this is the worked example of.
 */

#include "gallery.h"

#include <stddef.h>

/*
 * clang-format off - and this is not a style preference.
 *
 * These two directives are a *sequence*, not a set: an id is a table index, so inkcell's
 * catalog has to be expanded before the gallery's or every string in the build shifts by the
 * length of the other half and every label on the panel becomes the wrong word. clang-format
 * sorts includes alphabetically, "catalog.def" sorts before "inkcell/...", and it will quietly
 * swap them every time the tree is formatted.
 *
 * It did exactly that, once, and the golden sheet is what noticed. Nothing else would have:
 * the build stays clean, the _Static_assert below still passes because the table is still the
 * right *length*, and the only symptom is that the words are wrong on a screen no test was
 * looking at.
 */
/* clang-format off */
static const char *const k_english[] = {
#define INKCELL_STR_ENTRY(id, text) text,
#define INKCELL_STR_PLURAL_ENTRY(id, one, other) one, other,
#include "inkcell/i18n/catalog.def"    /* ids 0 .. INKCELL_STR_COUNT-1 */
#include "catalog.def"                 /* and the gallery's, continuing */
#undef INKCELL_STR_ENTRY
#undef INKCELL_STR_PLURAL_ENTRY
};

static const char *const k_id_names[] = {
#define INKCELL_STR_ENTRY(id, text) #id,
#define INKCELL_STR_PLURAL_ENTRY(id, one, other) #id "_ONE", #id "_OTHER",
#include "inkcell/i18n/catalog.def"
#include "catalog.def"
#undef INKCELL_STR_ENTRY
#undef INKCELL_STR_PLURAL_ENTRY
};
/* clang-format on */

/*
 * The one way this goes wrong is the two .def files drifting apart from the enum built off
 * them, and it is a compile-time question - so it is asked at compile time.
 */
_Static_assert(sizeof k_english / sizeof k_english[0] == (size_t)GALLERY_STR_COUNT,
               "the English table and enum gallery_str_id disagree about how many ids there are");
_Static_assert(sizeof k_id_names / sizeof k_id_names[0] == (size_t)GALLERY_STR_COUNT,
               "the id-name table and enum gallery_str_id disagree about how many ids there are");

static const struct inkcell_i18n_locale k_locales[] = {
    /* English has a NULL table because it *is* the fallback. A second language is a second
       entry here with a table of GALLERY_STR_COUNT entries, NULL wherever it has nothing to
       say yet - which is what lets a half-finished translation ship. */
    {.id = "en", .name = "English", .table = NULL, .plural_form = NULL},
};

static const struct inkcell_i18n_catalog k_catalog = {
    .count = (size_t)GALLERY_STR_COUNT,
    .english = k_english,
    .id_names = k_id_names,
    .locales = k_locales,
    .locale_count = sizeof k_locales / sizeof k_locales[0],
};

void gallery_i18n_install(void) {
    inkcell_i18n_set_catalog(&k_catalog);
}
