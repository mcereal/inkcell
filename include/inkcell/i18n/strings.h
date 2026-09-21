#ifndef INKCELL_I18N_STRINGS_H
#define INKCELL_I18N_STRINGS_H

/*
 * Every word the user reads, in one place - or rather in two, joined end to end.
 *
 * A screen never spells out a sentence. It names a *string id* - INKCELL_STR_TIME_NOW, or one
 * of the application's own - and this module answers with the text for the locale in
 * force, exactly the way src/ui/theme/theme.c answers a colour role. That is what makes a language
 * switch total rather than a hunt: the renderers hold no opinion about English, so there is
 * nowhere for an untranslated sentence to hide.
 *
 * The catalog is include/inkcell/i18n/catalog.def, one line per string. Adding a string is adding
 * a line there; the enum, the English table and the translation template all come off the same
 * list, so they cannot drift apart.
 *
 * A locale is a table of the same length with NULL where it has nothing to say, and NULL falls
 * back to English. A half-finished translation therefore ships and reads as a mixture rather
 * than as blanks, which is the state every translation is in for a while.
 *
 * What is deliberately *not* here:
 *
 *   - Log lines. inkwell_log_info() and its siblings write output that is for whoever is reading
 * `deploy-logs`, and a bug report in a language the maintainer cannot read is worse than no bug
 * report.
 *   - Names shared with the rest of Meshtastic: region codes ("EU 868"), hardware models
 *     ("Heltec V3"), modem presets ("Long Range - Fast"), device roles ("Router"). A setting
 *     read off the Brick has to be recognisable in the phone app and back, so those stay in
 *     src/core/session/radio_settings.c untranslated, for the same reason a channel key is shown as
 *     base64.
 *   - Protocol, path, environment and config text. Nobody reads it as prose.
 *   - src/main.c's --help and the cli/stub backends, which are the headless developer
 *     surfaces and never reach the device screen.
 *
 * See docs/i18n.md.
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The ids, generated from the catalog.
 *
 * A plural entry occupies two consecutive ids - _ONE and _OTHER - because the id *is* the
 * table index, and inkcell_str_plural() picks between them by the locale's rule rather than by
 * `n == 1`, which is an English rule and not even that in every sentence.
 */
enum inkcell_str_id {
#define INKCELL_STR_ENTRY(id, text) INKCELL_STR_##id,
#define INKCELL_STR_PLURAL_ENTRY(id, one, other) INKCELL_STR_##id##_ONE, INKCELL_STR_##id##_OTHER,
#include "inkcell/i18n/catalog.def"
#undef INKCELL_STR_ENTRY
#undef INKCELL_STR_PLURAL_ENTRY
    INKCELL_STR_COUNT
};

/* How many forms a plural entry has. Two covers English and the languages that count like it;
   Polish, Russian and Arabic need three to six, and widening them is this constant, the
   INKCELL_STR_PLURAL_ENTRY macro above, and nothing else. See docs/i18n.md. */
#define INKCELL_STR_PLURAL_FORMS 2

/*
 * One language.
 *
 * `table` is INKCELL_STR_COUNT entries; a NULL entry means "not translated yet" and resolves to
 * English. English itself has a NULL table, because it *is* the fallback.
 */
struct inkcell_i18n_locale {
    const char *id;   /* what <PREFIX>_LANG and the Settings row match, e.g. "en" */
    const char *name; /* what the Settings row shows, in that language */
    /* As many entries as the catalog in force has ids - INKCELL_STR_COUNT for inkcell's own,
       or the registered catalog's `count`. NULL means "not translated yet" and resolves to
       English. */
    const char *const *table;
    /* Which plural form (0 .. INKCELL_STR_PLURAL_FORMS-1) `n` takes. NULL means the English rule,
       which is "one for 1, other for everything else including 0". */
    uint8_t (*plural_form)(uint32_t n);
};

/* The text for `id` in the current locale, never NULL: an untranslated entry falls back to
   English and an out-of-range id to the empty string. The pointer is to static storage and
   stays valid until the locale changes. */
const char *inkcell_str(enum inkcell_str_id id);

/* The text for `id` in `locale` specifically, with the same guarantees. */
const char *inkcell_str_in(const struct inkcell_i18n_locale *locale, enum inkcell_str_id id);

/* The form of a plural entry that `count` takes. `one_form` is the _ONE id; the locale's rule
   picks the offset from it. */
const char *inkcell_str_plural(enum inkcell_str_id one_form, uint32_t count);

/*
 * snprintf() with a catalog entry as the format.
 *
 * Catalog entries carry their own %-specifiers - "Sent to %s" is one string, not "Sent to "
 * plus a name, because a language that puts the verb last cannot translate the halves. That
 * makes the format non-literal, which -Wformat=2 rightly objects to at every call site, so the
 * objection is answered once here instead of everywhere: this is the only place in the client
 * that formats a string it did not write, and inkcell_i18n_validate() is what checks a
 * translation did not change the specifiers out from under a caller.
 *
 * Returns what snprintf() would, and always NUL-terminates when out_len > 0.
 */
int inkcell_str_format(char *out, size_t out_len, enum inkcell_str_id id, ...);
int inkcell_str_vformat(char *out, size_t out_len, enum inkcell_str_id id, va_list args);

/* The same, choosing the plural form for `count` first. `count` is not passed on to the
   format; pass it again in the arguments if the sentence shows the number. */
int inkcell_str_format_plural(char *out, size_t out_len, enum inkcell_str_id one_form,
                              uint32_t count, ...);

/* ---- locales ------------------------------------------------------------------------------ */

size_t inkcell_i18n_locale_count(void);
const struct inkcell_i18n_locale *inkcell_i18n_locale_at(size_t index);
const struct inkcell_i18n_locale *inkcell_i18n_locale_by_id(const char *id); /* NULL when unknown */
const struct inkcell_i18n_locale *inkcell_i18n_locale_english(void);

/* The locale in force, never NULL. */
const struct inkcell_i18n_locale *inkcell_i18n_locale(void);

/* Switch languages. False, and no change, when `id` names no locale. */
bool inkcell_i18n_set_locale(const char *id);

/*
 * Pick the starting locale from the environment: <PREFIX>_LANG first, then LC_ALL, LC_MESSAGES
 * and LANG, each matched on the language part alone so "fr_CA.UTF-8" finds "fr". English when
 * nothing matches. Safe to call more than once.
 */
void inkcell_i18n_init(void);

/* Explicit <PREFIX>_LANG overrides the saved id; otherwise a known saved id wins over
   the system locale. Empty or unknown saved ids retain the environment's fallback. */
void inkcell_i18n_init_with_preference(const char *id);
bool inkcell_i18n_is_overridden(void);

/*
 * Whether `locale` is fit to ship: the ids are in range, and every entry it does translate
 * carries the same %-specifiers, in the same order, as the English it replaces. A translation
 * that turns "%u of %u" into "%s of %u" is a crash, not a typo, which is why this runs over
 * every locale in the tests.
 *
 * `reason` is filled with a one-line explanation on failure when it is non-NULL.
 */
bool inkcell_i18n_validate(const struct inkcell_i18n_locale *locale, char *reason,
                           size_t reason_len);

/* The catalog id's spelling, for the translation template and for test failures:
   "TAB_NODES", not the text. NULL when `id` is out of range. */
const char *inkcell_str_id_name(enum inkcell_str_id id);

/* ---- extending the catalog -----------------------------------------------------------------
 *
 * inkcell's own strings are ids 0 .. INKCELL_STR_COUNT-1, and they are only ever the words the
 * toolkit itself prints. An application continues the numbering from INKCELL_STR_COUNT and
 * registers the whole thing - both halves in one table - here.
 *
 * Both halves rather than only the app's, because an id *is* a table index: one array indexed
 * straight through is what keeps inkcell_str() a bounds check and a load, with no branch on
 * which half an id fell in and no second lookup for a translation. Building it is a matter of
 * including the two .def files in order:
 *
 *     static const char *const k_english[] = {
 *     #define INKCELL_STR_ENTRY(id, text) text,
 *     #define INKCELL_STR_PLURAL_ENTRY(id, one, other) one, other,
 *     #include "inkcell/i18n/catalog.def"    <- ids 0 .. INKCELL_STR_COUNT-1
 *     #include "myapp/i18n/catalog.def"      <- and the application's, continuing
 *     #undef INKCELL_STR_ENTRY
 *     #undef INKCELL_STR_PLURAL_ENTRY
 *     };
 *
 * and declaring the application's own enum so that it starts where inkcell's left off:
 *
 *     enum myapp_str_id {
 *         MYAPP_STR__BASE = INKCELL_STR_COUNT - 1,
 *     #define INKCELL_STR_ENTRY(id, text) MYAPP_STR_##id,
 *     ...
 *     };
 *
 * A _Static_assert that the table's length equals the app's own count is worth writing: the two
 * .def files drifting apart is one way this goes wrong, and it is a compile-time question.
 *
 * The other way is not, and it is worth stating because it is silent. **The two #includes are a
 * sequence, not a set.** An id is a table index, so inkcell's half has to expand first; swap
 * them and every string shifts by the length of the other half, every label on the panel
 * becomes the wrong word, and nothing complains - the build is clean and the assertion above
 * still passes, because the table is still the right length. clang-format sorts includes
 * alphabetically and will make that swap on its own, so wrap the block in `clang-format off`.
 * See examples/gallery/strings.c, where it happened.
 */
struct inkcell_i18n_catalog {
    /* Total ids, inkcell's included. Must be at least INKCELL_STR_COUNT. */
    size_t count;
    const char *const *english;  /* `count` entries; never NULL */
    const char *const *id_names; /* `count` entries, for templates and test failures; may be NULL */
    /* Every language in the build, English first. Each table is `count` entries. May be NULL,
       which leaves English as the only locale. */
    const struct inkcell_i18n_locale *locales;
    size_t locale_count;
};

/*
 * Installs the application's catalog. Call once, before inkcell_i18n_init().
 *
 * The structure and everything it points at must outlive the process - these are static tables,
 * not something to build on a stack. Passing NULL restores inkcell's own catalog, which is what
 * a test, a capture harness or the library on its own runs with.
 */
void inkcell_i18n_set_catalog(const struct inkcell_i18n_catalog *catalog);

/* How many ids the catalog in force has - INKCELL_STR_COUNT when none was registered. */
size_t inkcell_i18n_string_count(void);

#ifdef __cplusplus
}
#endif

#endif /* INKCELL_I18N_STRINGS_H */
