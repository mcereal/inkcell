#pragma once

#include <stdbool.h>
#include <stdint.h>

struct inkcell_focus_map;

#ifdef __cplusplus
extern "C" {
#endif

struct inkcell_backend {
    const char *name;
    int (*init)(void **state, void *userdata);
    void (*shutdown)(void *state, void *userdata);
    void (*present)(void *state, const void *snapshot, void *userdata);
    /*
     * Whether the last frame is still moving, and so whether the backend is owed another one
     * without anything having changed.
     *
     * The store publishes on change, which is all a screen made of text ever needs. A control
     * that animates needs the opposite: several frames from one change. Rather than have the
     * store invent updates nobody asked for, a backend that animates says so here and the
     * controller keeps waking it until it stops - see inkcell_controller_init().
     *
     * Optional. A backend that draws everything in one go leaves it NULL and nothing ticks.
     */
    bool (*animating)(void *state, void *userdata);
    /*
     * How many body rows the last frame's paged list had room for. Optional: a backend that leaves
     * it NULL pages nothing, which is right for one that prints every row rather than scrolling a
     * window over them.
     */
    uint32_t (*page_rows)(void *state, void *userdata);
    /*
     * The boxes the last frame registered, for a press that has to be answered against them.
     *
     * The other half of include/inkcell/ui/focus.h. A map is built while a frame is drawn,
     * because only the draw knows what came out on the panel - and it is *read* between frames,
     * by whatever turns a button into a move. Those are two different places in an application
     * that keeps its navigation model away from its renderer, and this is the seam between
     * them: the same one `page_rows` above is, one fact along.
     *
     * The map belongs to whoever pushed it in with inkcell_fb_set_focus_map() and is rebuilt
     * every frame, so what comes back here is only good until the next one - which is exactly
     * as long as a press between two frames needs it.
     *
     * Optional, and NULL is the whole of the opt-out: a backend that registers nothing, or an
     * application that has not pushed a map, leaves the caller to whatever it did before.
     */
    const struct inkcell_focus_map *(*focus_map)(void *state, void *userdata);
};

#ifdef __cplusplus
}
#endif
