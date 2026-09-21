#define _POSIX_C_SOURCE 200809L

/*
 * The gallery's driver: render every scene in every theme at every scale, and say what came out.
 *
 * Two jobs, and the second is why the first is worth having:
 *
 *   --out DIR   writes the pages as PPM, for scripts/frames.py to turn into pictures.
 *   (default)   prints a manifest line per page - scene, theme, scale, size, digest.
 *   --check F   reads a manifest and reports the lines that no longer match.
 *
 * The digest is what makes a widget testable. "The button looks right" is not a unit test
 * anybody can write; "the button looks like it did yesterday, and here is the picture of what
 * changed" is, and it costs one FNV pass over the page.
 *
 * Everything that could make a page depend on the machine it was rendered on is pinned here
 * rather than left to the environment: the theme, the scale, the locale and the clock. A capture
 * whose contents depend on the host is not a reviewable picture.
 */

#include "gallery.h"

#include "inkcell/ui/fb_capture.h"
#include "inkwell/base/env.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The scales the sheet is taken at.
 *
 * Two, not five. The scale is a glyph multiplier and almost everything in the toolkit is
 * derived from it, so a second scale is what catches a widget that stated a pixel count
 * instead - and a third would catch the same bug a third time for a third of the manifest.
 */
static const int k_scales[] = {INKCELL_SCALE(3), INKCELL_SCALE(4)};

struct gallery_page {
    const char *scene;
    const char *theme;
    int scale;
    uint32_t width;
    uint32_t height;
    uint64_t digest;
};

/*
 * FNV-1a over the visible pixels, row by row.
 *
 * Row by row rather than over the whole buffer because a page's stride may carry padding the
 * renderer never writes, and hashing uninitialised bytes is a digest that changes for no
 * reason. Four bytes a pixel, the fourth included: the alpha byte is composed rather than left
 * alone, so it is part of what was drawn.
 */
static uint64_t gallery_digest(const uint8_t *pixels, uint32_t width, uint32_t height,
                               size_t stride) {
    uint64_t hash = 1469598103934665603ULL;
    for (uint32_t row = 0U; row < height; ++row) {
        const uint8_t *p = pixels + (size_t)row * stride;
        for (size_t i = 0U; i < (size_t)width * 4U; ++i) {
            hash ^= p[i];
            hash *= 1099511628211ULL;
        }
    }
    return hash;
}

static void gallery_render(struct inkcell_draw_state *state, const void *snapshot, void *ctx) {
    (void)ctx;
    const struct gallery_scene *scene = snapshot;
    scene->render(state);
}

/* One page. Returns 0, or a negative errno. */
static int gallery_page(const struct gallery_scene *scene, const struct inkcell_theme *theme,
                        int scale, const char *out_dir, struct gallery_page *page) {
    struct inkcell_capture *capture = NULL;
    int rc = inkcell_capture_open(&capture, INKCELL_CAPTURE_WIDTH, INKCELL_CAPTURE_HEIGHT, scale);
    if (rc < 0) {
        return rc;
    }
    inkcell_capture_set_theme(capture, theme);
    /* After the theme, which carries a scale of its own and would otherwise win. */
    inkcell_capture_set_scale(capture, scale);

    struct inkcell_draw_state *state = inkcell_capture_state(capture);
    inkcell_fb_set_app(state, &(struct inkcell_fb_app){.render = gallery_render});
    inkcell_fb_state_set_now(state, GALLERY_CLOCK_MS);
    inkcell_capture_render(capture, scene);
    if (scene->settle_ms > 0U) {
        /* A second frame, with the scene's settling time between. The first frame is what
           starts anything that animates - a widget cannot know it has just appeared until it
           has been drawn once - so the picture worth keeping is the one after it. */
        inkcell_capture_advance(capture, scene->settle_ms);
        inkcell_capture_render(capture, scene);
    }

    uint32_t width = 0U;
    uint32_t height = 0U;
    size_t stride = 0U;
    const uint8_t *pixels = inkcell_capture_pixels(capture, &width, &height, &stride);
    if (pixels == NULL) {
        inkcell_capture_close(capture);
        return -EIO;
    }

    page->scene = scene->name;
    page->theme = theme->id;
    page->scale = scale;
    page->width = width;
    page->height = height;
    page->digest = gallery_digest(pixels, width, height, stride);

    if (out_dir != NULL) {
        char path[512];
        const int n =
            snprintf(path, sizeof path, "%s/%s@%s@%d.ppm", out_dir, scene->name, theme->id, scale);
        if (n < 0 || (size_t)n >= sizeof path) {
            inkcell_capture_close(capture);
            return -ENAMETOOLONG;
        }
        rc = inkcell_capture_write_ppm(capture, path);
        if (rc < 0) {
            inkcell_capture_close(capture);
            return rc;
        }
    }

    inkcell_capture_close(capture);
    return 0;
}

static void gallery_print(const struct gallery_page *page, FILE *out) {
    fprintf(out, "%s %s %d %ux%u %016llx\n", page->scene, page->theme, page->scale, page->width,
            page->height, (unsigned long long)page->digest);
}

/*
 * The manifest a --check run compares against.
 *
 * A line per page, in the order the scenes are listed crossed with the themes and the scales.
 * Blank lines and anything after a '#' are ignored, so the file can say what it is.
 */
static int gallery_check(const char *path, const struct gallery_page *pages, size_t count) {
    FILE *f = fopen(path, "re");
    if (f == NULL) {
        fprintf(stderr, "gallery: cannot read %s: %s\n", path, strerror(errno));
        return 2;
    }

    size_t index = 0U;
    size_t failures = 0U;
    char line[512];
    while (fgets(line, sizeof line, f) != NULL) {
        char *hash = strchr(line, '#');
        if (hash != NULL) {
            *hash = '\0';
        }
        char scene[128];
        char theme[64];
        int scale = 0;
        unsigned width = 0U;
        unsigned height = 0U;
        unsigned long long digest = 0ULL;
        if (sscanf(line, "%127s %63s %d %ux%u %llx", scene, theme, &scale, &width, &height,
                   &digest) != 6) {
            continue;
        }
        if (index >= count) {
            fprintf(stderr, "gallery: %s has more pages than this build renders\n", path);
            ++failures;
            break;
        }
        const struct gallery_page *page = &pages[index++];
        if (strcmp(scene, page->scene) != 0 || strcmp(theme, page->theme) != 0 ||
            scale != page->scale) {
            fprintf(stderr, "gallery: expected %s %s %d, rendered %s %s %d\n", scene, theme, scale,
                    page->scene, page->theme, page->scale);
            ++failures;
            continue;
        }
        if (digest != page->digest || width != page->width || height != page->height) {
            fprintf(stderr, "gallery: %s@%s@%d changed: %ux%u %016llx -> %ux%u %016llx\n",
                    page->scene, page->theme, page->scale, width, height, digest, page->width,
                    page->height, (unsigned long long)page->digest);
            ++failures;
        }
    }
    fclose(f);

    if (index < count) {
        fprintf(stderr, "gallery: %s is missing %zu page(s) this build renders\n", path,
                count - index);
        ++failures;
    }
    if (failures > 0U) {
        fprintf(stderr,
                "gallery: %zu page(s) differ. Look at them with `make gallery`, and if the change\n"
                "         was intended, record it with `make gallery-update`.\n",
                failures);
        return 1;
    }
    return 0;
}

static void gallery_usage(FILE *out) {
    fputs("usage: inkcell_gallery [--out DIR] [--check FILE] [--scene NAME] [--theme ID]\n"
          "                       [--scale N] [--list]\n",
          out);
}

int main(int argc, char **argv) {
    const char *out_dir = NULL;
    const char *check = NULL;
    const char *only_scene = NULL;
    const char *only_theme = NULL;
    int only_scale = 0;
    bool list = false;

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        const bool has_value = (i + 1) < argc;
        if (strcmp(arg, "--list") == 0) {
            list = true;
        } else if (strcmp(arg, "--out") == 0 && has_value) {
            out_dir = argv[++i];
        } else if (strcmp(arg, "--check") == 0 && has_value) {
            check = argv[++i];
        } else if (strcmp(arg, "--scene") == 0 && has_value) {
            only_scene = argv[++i];
        } else if (strcmp(arg, "--theme") == 0 && has_value) {
            only_theme = argv[++i];
        } else if (strcmp(arg, "--scale") == 0 && has_value) {
            /* In whole steps, like <PREFIX>_FB_SCALE: the manifest states a scale in units,
               but the flag is what somebody types to re-render one row of the sheet. */
            only_scale = INKCELL_SCALE(atoi(argv[++i]));
        } else if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            gallery_usage(stdout);
            return 0;
        } else {
            fprintf(stderr, "gallery: unrecognised argument: %s\n", arg);
            gallery_usage(stderr);
            return 2;
        }
    }

    /*
     * The four things the README says an application pushes in, done here for real. The prefix
     * comes first because everything else reads the environment through it.
     */
    inkwell_env_set_prefix("INKCELL_GALLERY");
    gallery_i18n_install();
    inkcell_i18n_init();
    /* And then pinned, because a manifest rendered in the host's language is a manifest that
       fails on a machine whose LANG says something else. */
    (void)inkcell_i18n_set_locale("en");

    size_t scene_count = 0U;
    const struct gallery_scene *scenes = gallery_scenes(&scene_count);

    if (list) {
        for (size_t i = 0U; i < scene_count; ++i) {
            printf("%s\n", scenes[i].name);
        }
        return 0;
    }

    const size_t theme_count = inkcell_theme_count();
    const size_t scale_count = sizeof k_scales / sizeof k_scales[0];
    struct gallery_page *pages = calloc(scene_count * theme_count * scale_count, sizeof *pages);
    if (pages == NULL) {
        fprintf(stderr, "gallery: out of memory\n");
        return 2;
    }

    size_t count = 0U;
    for (size_t s = 0U; s < scene_count; ++s) {
        if (only_scene != NULL && strcmp(only_scene, scenes[s].name) != 0) {
            continue;
        }
        for (size_t t = 0U; t < theme_count; ++t) {
            const struct inkcell_theme *theme = inkcell_theme_at(t);
            if (only_theme != NULL && strcmp(only_theme, theme->id) != 0) {
                continue;
            }
            for (size_t k = 0U; k < scale_count; ++k) {
                if (only_scale != 0 && only_scale != k_scales[k]) {
                    continue;
                }
                const int rc = gallery_page(&scenes[s], theme, k_scales[k], out_dir, &pages[count]);
                if (rc < 0) {
                    fprintf(stderr, "gallery: %s@%s@%d failed: %s\n", scenes[s].name, theme->id,
                            k_scales[k], strerror(-rc));
                    free(pages);
                    return 2;
                }
                ++count;
            }
        }
    }

    int status = 0;
    if (check != NULL) {
        status = gallery_check(check, pages, count);
    } else {
        /* The header goes out with the pages, so that `make gallery-update` - which is this
           command redirected over the manifest - rewrites the file whole rather than stripping
           the explanation off the top of it. */
        fputs("# The gallery's pages, as rendered by a build that was looked at. One line per\n"
              "# page:\n"
              "#\n"
              "#     <scene> <theme> <scale> <width>x<height> <fnv1a-64 of the pixels>\n"
              "#\n"
              "# Regenerate with `make gallery-update`, and only after looking at `make\n"
              "# gallery`. A digest that changed without a picture being reviewed is a\n"
              "# rendering change going in unnoticed, which is the one thing this file exists\n"
              "# to stop.\n\n",
              stdout);
        for (size_t i = 0U; i < count; ++i) {
            gallery_print(&pages[i], stdout);
        }
    }
    free(pages);
    return status;
}
