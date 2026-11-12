#define _POSIX_C_SOURCE 200809L

/*
 * The KMS backend: a card, a connector, a CRTC, a plane and two dumb buffers. See
 * inkcell/ui/kms.h.
 */

#include "inkcell/ui/fb_draw.h"

#include "inkcell/ui/kms.h"
#include "inkcell/ui/latency.h"

#include "inkwell/base/env.h"
#include "inkwell/base/log.h"
#include "inkwell/base/time.h"

#include <errno.h>

#if !defined(INKCELL_HAVE_KMS)

/*
 * No DRM on this host, or no kernel headers to speak it with. The drawing-state answers are
 * still given for a state that never comes to exist, so the vtable is complete either way.
 */

bool inkcell_backend_kms_is_available(void) {
    return false;
}

static int inkcell_backend_kms_init(void **state_out, void *userdata) {
    (void)state_out;
    (void)userdata;
    return -ENOTSUP;
}

static const struct inkcell_backend k_kms_backend = {
    .name = "kms",
    .init = inkcell_backend_kms_init,
};

const struct inkcell_backend *inkcell_backend_kms(void) {
    return &k_kms_backend;
}

#else /* INKCELL_HAVE_KMS */

#include <drm/drm.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_mode.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

/* libdrm's names for what the uapi headers leave as numbers. */
#define KMS_CONNECTED 1U
#define KMS_PLANE_TYPE_PRIMARY 1U
#define KMS_CARDS_MAX 16
/* How long a present() waits for the last flip. A vertical blank is 16 ms at 60 Hz; this is
   six of them, which no working panel takes, and short enough that one that has stopped
   answering costs a stutter rather than a hang. */
#define KMS_FLIP_WAIT_MS 100
/* How many rectangles a damage pass may report. Only their extent is used. */
#define KMS_DAMAGE_RECTS 8U

/*
 * One dumb buffer: the kernel's handle, the framebuffer made of it, and where it is mapped.
 *
 * `owed_top`/`owed_bottom` are the rows this buffer has not been given yet. A buffer is copied
 * into every other frame, so it is behind by whatever changed in both of the frames since its
 * last turn - and those rows, not just the latest frame's, are what it is owed.
 */
struct inkcell_kms_buffer {
    uint32_t handle;
    uint32_t fb_id;
    uint8_t *map;
    size_t size;
    uint32_t owed_top;
    uint32_t owed_bottom;
};

struct inkcell_kms_props {
    uint32_t connector_crtc_id;
    uint32_t crtc_mode_id;
    uint32_t crtc_active;
    uint32_t plane_fb_id;
    uint32_t plane_crtc_id;
    uint32_t plane_src_x;
    uint32_t plane_src_y;
    uint32_t plane_src_w;
    uint32_t plane_src_h;
    uint32_t plane_crtc_x;
    uint32_t plane_crtc_y;
    uint32_t plane_crtc_w;
    uint32_t plane_crtc_h;
};

/*
 * The panel behind the drawing state. `state` is first so the vtable can hand out either, as
 * the fb backend's does; its surface is the draw buffer, always, and the dumb buffers are only
 * ever copied into.
 */
struct inkcell_kms_panel {
    struct inkcell_draw_state state;
    int fd;
    bool atomic;
    uint32_t connector_id;
    uint32_t crtc_id;
    uint32_t plane_id;
    struct drm_mode_modeinfo mode;
    uint32_t mode_blob;
    struct inkcell_kms_props props;
    struct inkcell_kms_buffer buffers[2];
    unsigned front;
    /* The first frame is what the mode is set with, so the panel never shows a blank buffer. */
    bool mode_set;
    bool modeset_failed_logged;
    bool flip_pending;
    bool flip_failed_logged;
    bool flip_wait_logged;
    /* Whether `shown` holds a frame yet; until then the whole frame is damage. */
    bool frame_valid;
    bool presented;
    uint8_t *draw_buffer;
    /* The last frame drawn, which the next one is compared against to find what changed. */
    uint8_t *shown;
};

static struct inkcell_kms_panel *inkcell_kms_panel_of(void *state_ptr) {
    return (struct inkcell_kms_panel *)state_ptr;
}

/* glibc declares the request unsigned long and musl declares it int; DRM's numbers use the top
   bit, so it is carried as the wider type and narrowed once, here, to whichever this libc takes. */
#if defined(__GLIBC__)
typedef unsigned long inkcell_kms_request;
#else
typedef int inkcell_kms_request;
#endif

/* libdrm's drmIoctl(): an ioctl the kernel interrupted, or asked to be repeated, is repeated. */
static int inkcell_kms_ioctl(int fd, unsigned long request, void *arg) {
    int rc;
    do {
        rc = ioctl(fd, (inkcell_kms_request)request, arg);
    } while (rc == -1 && (errno == EINTR || errno == EAGAIN));
    return rc == -1 ? -errno : 0;
}

static uint64_t inkcell_kms_ptr(const void *p) {
    return (uint64_t)(uintptr_t)p;
}

/*
 * The card's objects. Asked twice - once for the counts, once with room for them - and asked
 * again if the counts moved in between, which a hotplug between the two calls does.
 */
struct inkcell_kms_resources {
    uint32_t *crtcs;
    uint32_t *connectors;
    uint32_t count_crtcs;
    uint32_t count_connectors;
};

static void inkcell_kms_resources_free(struct inkcell_kms_resources *res) {
    free(res->crtcs);
    free(res->connectors);
    *res = (struct inkcell_kms_resources){0};
}

static int inkcell_kms_resources_get(int fd, struct inkcell_kms_resources *out) {
    *out = (struct inkcell_kms_resources){0};
    for (int attempt = 0; attempt < 4; ++attempt) {
        struct drm_mode_card_res res = {0};
        int rc = inkcell_kms_ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res);
        if (rc < 0) {
            return rc;
        }
        const uint32_t crtcs = res.count_crtcs;
        const uint32_t connectors = res.count_connectors;
        uint32_t *crtc_ids = calloc(crtcs + 1U, sizeof *crtc_ids);
        uint32_t *connector_ids = calloc(connectors + 1U, sizeof *connector_ids);
        if (crtc_ids == NULL || connector_ids == NULL) {
            free(crtc_ids);
            free(connector_ids);
            return -ENOMEM;
        }
        res = (struct drm_mode_card_res){
            .crtc_id_ptr = inkcell_kms_ptr(crtc_ids),
            .connector_id_ptr = inkcell_kms_ptr(connector_ids),
            .count_crtcs = crtcs,
            .count_connectors = connectors,
        };
        rc = inkcell_kms_ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res);
        if (rc < 0) {
            free(crtc_ids);
            free(connector_ids);
            return rc;
        }
        if (res.count_crtcs > crtcs || res.count_connectors > connectors) {
            free(crtc_ids);
            free(connector_ids);
            continue;
        }
        *out = (struct inkcell_kms_resources){
            .crtcs = crtc_ids,
            .connectors = connector_ids,
            .count_crtcs = res.count_crtcs,
            .count_connectors = res.count_connectors,
        };
        return 0;
    }
    return -EAGAIN;
}

/*
 * One connector: whether something is plugged into it, its modes and its encoders.
 *
 * The first call is made with every count at zero, which is also what makes the kernel probe
 * the connector rather than answer from what it last knew - a panel that came up after the
 * driver did is found here.
 */
struct inkcell_kms_connector {
    uint32_t id;
    uint32_t connection;
    uint32_t encoder_id;
    struct drm_mode_modeinfo *modes;
    uint32_t count_modes;
    uint32_t *encoders;
    uint32_t count_encoders;
};

static void inkcell_kms_connector_free(struct inkcell_kms_connector *c) {
    free(c->modes);
    free(c->encoders);
    *c = (struct inkcell_kms_connector){0};
}

static int inkcell_kms_connector_get(int fd, uint32_t id, struct inkcell_kms_connector *out) {
    *out = (struct inkcell_kms_connector){0};
    for (int attempt = 0; attempt < 4; ++attempt) {
        struct drm_mode_get_connector conn = {.connector_id = id};
        int rc = inkcell_kms_ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn);
        if (rc < 0) {
            return rc;
        }
        const uint32_t modes = conn.count_modes;
        const uint32_t encoders = conn.count_encoders;
        struct drm_mode_modeinfo *mode_list = calloc(modes + 1U, sizeof *mode_list);
        uint32_t *encoder_list = calloc(encoders + 1U, sizeof *encoder_list);
        if (mode_list == NULL || encoder_list == NULL) {
            free(mode_list);
            free(encoder_list);
            return -ENOMEM;
        }
        conn = (struct drm_mode_get_connector){
            .connector_id = id,
            .modes_ptr = inkcell_kms_ptr(mode_list),
            .count_modes = modes,
            .encoders_ptr = inkcell_kms_ptr(encoder_list),
            .count_encoders = encoders,
        };
        rc = inkcell_kms_ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn);
        if (rc < 0) {
            free(mode_list);
            free(encoder_list);
            return rc;
        }
        if (conn.count_modes > modes || conn.count_encoders > encoders) {
            free(mode_list);
            free(encoder_list);
            continue;
        }
        *out = (struct inkcell_kms_connector){
            .id = id,
            .connection = conn.connection,
            .encoder_id = conn.encoder_id,
            .modes = mode_list,
            .count_modes = conn.count_modes,
            .encoders = encoder_list,
            .count_encoders = conn.count_encoders,
        };
        return 0;
    }
    return -EAGAIN;
}

/* The mode the connector says it prefers; the first it lists when it prefers none, which by
   convention is the panel's native one. */
static const struct drm_mode_modeinfo *
inkcell_kms_pick_mode(const struct inkcell_kms_connector *c) {
    for (uint32_t i = 0U; i < c->count_modes; ++i) {
        if ((c->modes[i].type & DRM_MODE_TYPE_PREFERRED) != 0U) {
            return &c->modes[i];
        }
    }
    return c->count_modes > 0U ? &c->modes[0] : NULL;
}

static int inkcell_kms_crtc_index(const struct inkcell_kms_resources *res, uint32_t crtc_id) {
    for (uint32_t i = 0U; i < res->count_crtcs; ++i) {
        if (res->crtcs[i] == crtc_id) {
            return (int)i;
        }
    }
    return -1;
}

/*
 * A CRTC to drive the connector: the one already driving it when there is one, since taking
 * that over is a modeset the panel is already set up for; otherwise the first any of the
 * connector's encoders can reach.
 */
static int inkcell_kms_pick_crtc(int fd, const struct inkcell_kms_resources *res,
                                 const struct inkcell_kms_connector *c, uint32_t *crtc_out) {
    if (c->encoder_id != 0U) {
        struct drm_mode_get_encoder enc = {.encoder_id = c->encoder_id};
        if (inkcell_kms_ioctl(fd, DRM_IOCTL_MODE_GETENCODER, &enc) == 0 && enc.crtc_id != 0U &&
            inkcell_kms_crtc_index(res, enc.crtc_id) >= 0) {
            *crtc_out = enc.crtc_id;
            return 0;
        }
    }
    for (uint32_t e = 0U; e < c->count_encoders; ++e) {
        struct drm_mode_get_encoder enc = {.encoder_id = c->encoders[e]};
        if (inkcell_kms_ioctl(fd, DRM_IOCTL_MODE_GETENCODER, &enc) < 0) {
            continue;
        }
        for (uint32_t i = 0U; i < res->count_crtcs && i < 32U; ++i) {
            if ((enc.possible_crtcs & (1U << i)) != 0U) {
                *crtc_out = res->crtcs[i];
                return 0;
            }
        }
    }
    return -ENODEV;
}

/*
 * An object's property, by name: its id, and optionally its current value.
 *
 * Atomic modesetting addresses everything by property id, and the ids are the driver's to
 * number, so each one is looked up by the name the kernel documents for it.
 */
static int inkcell_kms_prop(int fd, uint32_t obj_id, uint32_t obj_type, const char *name,
                            uint32_t *id_out, uint64_t *value_out) {
    struct drm_mode_obj_get_properties props = {.obj_id = obj_id, .obj_type = obj_type};
    int rc = inkcell_kms_ioctl(fd, DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &props);
    if (rc < 0) {
        return rc;
    }
    const uint32_t count = props.count_props;
    uint32_t *ids = calloc(count + 1U, sizeof *ids);
    uint64_t *values = calloc(count + 1U, sizeof *values);
    if (ids == NULL || values == NULL) {
        free(ids);
        free(values);
        return -ENOMEM;
    }
    props.props_ptr = inkcell_kms_ptr(ids);
    props.prop_values_ptr = inkcell_kms_ptr(values);
    props.count_props = count;
    rc = inkcell_kms_ioctl(fd, DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &props);
    const uint32_t got = props.count_props < count ? props.count_props : count;
    rc = rc < 0 ? rc : -ENOENT;
    for (uint32_t i = 0U; rc == -ENOENT && i < got; ++i) {
        struct drm_mode_get_property prop = {.prop_id = ids[i]};
        if (inkcell_kms_ioctl(fd, DRM_IOCTL_MODE_GETPROPERTY, &prop) == 0 &&
            strncmp(prop.name, name, DRM_PROP_NAME_LEN) == 0) {
            *id_out = ids[i];
            if (value_out != NULL) {
                *value_out = values[i];
            }
            rc = 0;
        }
    }
    free(ids);
    free(values);
    return rc;
}

/* The CRTC's primary plane: the one a frame goes on when nothing else is composed over it. */
static int inkcell_kms_pick_plane(int fd, int crtc_index, uint32_t *plane_out) {
    struct drm_mode_get_plane_res res = {0};
    int rc = inkcell_kms_ioctl(fd, DRM_IOCTL_MODE_GETPLANERESOURCES, &res);
    if (rc < 0) {
        return rc;
    }
    const uint32_t count = res.count_planes;
    uint32_t *planes = calloc(count + 1U, sizeof *planes);
    if (planes == NULL) {
        return -ENOMEM;
    }
    res.plane_id_ptr = inkcell_kms_ptr(planes);
    res.count_planes = count;
    rc = inkcell_kms_ioctl(fd, DRM_IOCTL_MODE_GETPLANERESOURCES, &res);
    const uint32_t got = res.count_planes < count ? res.count_planes : count;
    rc = rc < 0 ? rc : -ENODEV;
    for (uint32_t i = 0U; rc == -ENODEV && i < got; ++i) {
        struct drm_mode_get_plane plane = {.plane_id = planes[i]};
        if (inkcell_kms_ioctl(fd, DRM_IOCTL_MODE_GETPLANE, &plane) < 0 || crtc_index < 0 ||
            crtc_index >= 32 || (plane.possible_crtcs & (1U << crtc_index)) == 0U) {
            continue;
        }
        uint32_t type_id = 0U;
        uint64_t type = 0U;
        if (inkcell_kms_prop(fd, planes[i], DRM_MODE_OBJECT_PLANE, "type", &type_id, &type) == 0 &&
            type == KMS_PLANE_TYPE_PRIMARY) {
            *plane_out = planes[i];
            rc = 0;
        }
    }
    free(planes);
    return rc;
}

/* Every property id an atomic commit names. Any one missing and the driver gets the legacy
   calls instead. */
static int inkcell_kms_atomic_props(struct inkcell_kms_panel *panel) {
    struct inkcell_kms_props *p = &panel->props;
    const struct {
        uint32_t obj;
        uint32_t type;
        const char *name;
        uint32_t *id;
    } wanted[] = {
        {panel->connector_id, DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID", &p->connector_crtc_id},
        {panel->crtc_id, DRM_MODE_OBJECT_CRTC, "MODE_ID", &p->crtc_mode_id},
        {panel->crtc_id, DRM_MODE_OBJECT_CRTC, "ACTIVE", &p->crtc_active},
        {panel->plane_id, DRM_MODE_OBJECT_PLANE, "FB_ID", &p->plane_fb_id},
        {panel->plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_ID", &p->plane_crtc_id},
        {panel->plane_id, DRM_MODE_OBJECT_PLANE, "SRC_X", &p->plane_src_x},
        {panel->plane_id, DRM_MODE_OBJECT_PLANE, "SRC_Y", &p->plane_src_y},
        {panel->plane_id, DRM_MODE_OBJECT_PLANE, "SRC_W", &p->plane_src_w},
        {panel->plane_id, DRM_MODE_OBJECT_PLANE, "SRC_H", &p->plane_src_h},
        {panel->plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_X", &p->plane_crtc_x},
        {panel->plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_Y", &p->plane_crtc_y},
        {panel->plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_W", &p->plane_crtc_w},
        {panel->plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_H", &p->plane_crtc_h},
    };
    for (size_t i = 0U; i < sizeof wanted / sizeof wanted[0]; ++i) {
        const int rc = inkcell_kms_prop(panel->fd, wanted[i].obj, wanted[i].type, wanted[i].name,
                                        wanted[i].id, NULL);
        if (rc < 0) {
            inkwell_log_warn("ui", "KMS: no %s property (%s)", wanted[i].name, strerror(-rc));
            return rc;
        }
    }
    struct drm_mode_create_blob blob = {
        .data = inkcell_kms_ptr(&panel->mode),
        .length = sizeof panel->mode,
    };
    const int rc = inkcell_kms_ioctl(panel->fd, DRM_IOCTL_MODE_CREATEPROPBLOB, &blob);
    if (rc < 0) {
        return rc;
    }
    panel->mode_blob = blob.blob_id;
    return 0;
}

static void inkcell_kms_buffer_destroy(int fd, struct inkcell_kms_buffer *b) {
    if (b->map != NULL) {
        munmap(b->map, b->size);
    }
    if (b->fb_id != 0U) {
        uint32_t fb = b->fb_id;
        (void)inkcell_kms_ioctl(fd, DRM_IOCTL_MODE_RMFB, &fb);
    }
    if (b->handle != 0U) {
        struct drm_mode_destroy_dumb destroy = {.handle = b->handle};
        (void)inkcell_kms_ioctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
    }
    *b = (struct inkcell_kms_buffer){0};
}

/* A 32-bit XRGB8888 buffer the size of the mode, mapped. XRGB8888 is the one format every
   primary plane is required to take. */
static int inkcell_kms_buffer_create(int fd, uint32_t width, uint32_t height,
                                     struct inkcell_kms_buffer *b, uint32_t *pitch_out) {
    *b = (struct inkcell_kms_buffer){0};
    struct drm_mode_create_dumb create = {.width = width, .height = height, .bpp = 32U};
    int rc = inkcell_kms_ioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create);
    if (rc < 0) {
        return rc;
    }
    b->handle = create.handle;
    b->size = (size_t)create.size;

    struct drm_mode_fb_cmd2 fb = {
        .width = width,
        .height = height,
        .pixel_format = DRM_FORMAT_XRGB8888,
        .handles = {create.handle},
        .pitches = {create.pitch},
    };
    rc = inkcell_kms_ioctl(fd, DRM_IOCTL_MODE_ADDFB2, &fb);
    if (rc < 0) {
        inkcell_kms_buffer_destroy(fd, b);
        return rc;
    }
    b->fb_id = fb.fb_id;

    struct drm_mode_map_dumb map = {.handle = create.handle};
    rc = inkcell_kms_ioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map);
    if (rc < 0) {
        inkcell_kms_buffer_destroy(fd, b);
        return rc;
    }
    void *mapping = mmap(NULL, b->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)map.offset);
    if (mapping == MAP_FAILED) {
        rc = -errno;
        inkcell_kms_buffer_destroy(fd, b);
        return rc;
    }
    b->map = mapping;
    b->owed_top = 0U;
    b->owed_bottom = height;
    *pitch_out = create.pitch;
    return 0;
}

/*
 * The card, the connector, the CRTC and the plane - everything short of a buffer.
 * Returns 0, or a negative errno; on failure the descriptor is closed.
 */
static int inkcell_kms_open_card(struct inkcell_kms_panel *panel, const char *path) {
    panel->fd = open(path, O_RDWR | O_CLOEXEC | O_NONBLOCK);
    if (panel->fd < 0) {
        return -errno;
    }
    struct drm_get_cap cap = {.capability = DRM_CAP_DUMB_BUFFER};
    int rc = inkcell_kms_ioctl(panel->fd, DRM_IOCTL_GET_CAP, &cap);
    if (rc < 0 || cap.value == 0U) {
        rc = rc < 0 ? rc : -ENOTSUP;
        goto fail;
    }

    struct inkcell_kms_resources res;
    rc = inkcell_kms_resources_get(panel->fd, &res);
    if (rc < 0) {
        goto fail;
    }
    rc = -ENODEV;
    for (uint32_t i = 0U; rc < 0 && i < res.count_connectors; ++i) {
        struct inkcell_kms_connector conn;
        if (inkcell_kms_connector_get(panel->fd, res.connectors[i], &conn) < 0) {
            continue;
        }
        const struct drm_mode_modeinfo *mode = inkcell_kms_pick_mode(&conn);
        uint32_t crtc = 0U;
        if (conn.connection == KMS_CONNECTED && mode != NULL &&
            inkcell_kms_pick_crtc(panel->fd, &res, &conn, &crtc) == 0) {
            panel->connector_id = conn.id;
            panel->crtc_id = crtc;
            panel->mode = *mode;
            rc = 0;
        }
        inkcell_kms_connector_free(&conn);
    }
    const int crtc_index = inkcell_kms_crtc_index(&res, panel->crtc_id);
    inkcell_kms_resources_free(&res);
    if (rc < 0) {
        goto fail;
    }

    /* Atomic, unless asked not to or refused. Universal planes come first because atomic
       needs them, and the primary plane is only listed once they are on. */
    const bool legacy = inkwell_env_bool("KMS_LEGACY", "the legacy KMS calls", false);
    if (!legacy) {
        struct drm_set_client_cap universal = {.capability = DRM_CLIENT_CAP_UNIVERSAL_PLANES,
                                               .value = 1U};
        struct drm_set_client_cap atomic = {.capability = DRM_CLIENT_CAP_ATOMIC, .value = 1U};
        panel->atomic = inkcell_kms_ioctl(panel->fd, DRM_IOCTL_SET_CLIENT_CAP, &universal) == 0 &&
                        inkcell_kms_ioctl(panel->fd, DRM_IOCTL_SET_CLIENT_CAP, &atomic) == 0 &&
                        inkcell_kms_pick_plane(panel->fd, crtc_index, &panel->plane_id) == 0 &&
                        inkcell_kms_atomic_props(panel) == 0;
        if (!panel->atomic) {
            inkwell_log_warn("ui", "KMS: atomic modesetting unavailable on %s; using legacy", path);
        }
    }

    /* Master is what a modeset needs. The first opener is given it when nobody holds it, and
       asking again is harmless; a refusal here means somebody else owns the panel, which the
       first modeset will report. */
    if (inkcell_kms_ioctl(panel->fd, DRM_IOCTL_SET_MASTER, NULL) < 0) {
        inkwell_log_warn("ui", "KMS: not DRM master of %s (%s); another client owns it", path,
                         strerror(errno));
    }
    return 0;

fail:
    close(panel->fd);
    panel->fd = -1;
    return rc;
}

static int inkcell_kms_open_any(struct inkcell_kms_panel *panel, const char *device, char *path_out,
                                size_t path_size) {
    if (device != NULL && device[0] != '\0') {
        snprintf(path_out, path_size, "%s", device);
        return inkcell_kms_open_card(panel, device);
    }
    int last = -ENOENT;
    for (int i = 0; i < KMS_CARDS_MAX; ++i) {
        snprintf(path_out, path_size, "/dev/dri/card%d", i);
        const int rc = inkcell_kms_open_card(panel, path_out);
        if (rc == 0) {
            return 0;
        }
        /* A card that exists but has nothing plugged in is a better reason than the next
           number not existing. */
        if (rc != -ENOENT) {
            last = rc;
        }
    }
    return last;
}

static void inkcell_kms_release(struct inkcell_kms_panel *panel) {
    for (size_t i = 0U; i < 2U; ++i) {
        inkcell_kms_buffer_destroy(panel->fd, &panel->buffers[i]);
    }
    if (panel->mode_blob != 0U) {
        struct drm_mode_destroy_blob blob = {.blob_id = panel->mode_blob};
        (void)inkcell_kms_ioctl(panel->fd, DRM_IOCTL_MODE_DESTROYPROPBLOB, &blob);
        panel->mode_blob = 0U;
    }
    free(panel->draw_buffer);
    free(panel->shown);
    panel->draw_buffer = NULL;
    panel->shown = NULL;
    panel->state.surface.pixels = NULL;
    if (panel->fd >= 0) {
        close(panel->fd);
        panel->fd = -1;
    }
}

static int inkcell_backend_kms_init(void **state_out, void *userdata) {
    const struct inkcell_backend_kms_context *context =
        (const struct inkcell_backend_kms_context *)userdata;
    if (context == NULL) {
        return -EINVAL;
    }

    static struct inkcell_kms_panel panel_storage;
    struct inkcell_kms_panel *panel = &panel_storage;
    memset(panel, 0, sizeof *panel);
    panel->fd = -1;

    const char *device = context->device != NULL ? context->device : inkwell_env_get("KMS_DEVICE");
    char path[64];
    int rc = inkcell_kms_open_any(panel, device, path, sizeof path);
    if (rc < 0) {
        inkwell_log_warn("ui", "KMS: no usable card (%s)", strerror(-rc));
        return rc;
    }

    const uint32_t width = panel->mode.hdisplay;
    const uint32_t height = panel->mode.vdisplay;
    uint32_t pitch = 0U;
    for (size_t i = 0U; i < 2U; ++i) {
        uint32_t this_pitch = 0U;
        rc = inkcell_kms_buffer_create(panel->fd, width, height, &panel->buffers[i], &this_pitch);
        if (rc == 0 && i > 0U && this_pitch != pitch) {
            rc = -EINVAL; /* one draw buffer copies into both, so they must agree */
        }
        if (rc < 0) {
            inkwell_log_warn("ui", "KMS: dumb buffer %ux%u failed: %s", width, height,
                             strerror(-rc));
            inkcell_kms_release(panel);
            return rc;
        }
        pitch = this_pitch;
    }

    const size_t page_bytes = (size_t)pitch * height;
    panel->draw_buffer = calloc(1U, page_bytes);
    panel->shown = calloc(1U, page_bytes);
    if (panel->draw_buffer == NULL || panel->shown == NULL) {
        inkcell_kms_release(panel);
        return -ENOMEM;
    }

    struct inkcell_draw_state *state = &panel->state;
    /* XRGB8888, little-endian: blue in the low byte. The X byte is not alpha and the plane
       ignores it, so the format says there is none. */
    state->surface = (struct inkcell_surface){
        .pixels = panel->draw_buffer,
        .size = page_bytes,
        .width = width,
        .height = height,
        .stride = pitch,
        .bytes_per_pixel = 4U,
        .format =
            {
                .r = {16U, 8U},
                .g = {8U, 8U},
                .b = {0U, 8U},
                .a = {24U, 0U},
                .bits_per_pixel = 32U,
            },
    };
    inkcell_fb_state_apply_theme_from_env(state);
    inkcell_fb_set_app(state, context->app);

    inkwell_log_info("ui",
                     "KMS UI backend active (%s, %ux%u@%u, connector %u, crtc %u, %s, theme %s "
                     "at scale %d)",
                     path, width, height, panel->mode.vrefresh, panel->connector_id, panel->crtc_id,
                     panel->atomic ? "atomic" : "legacy", state->theme->id, state->scale);
    if (state_out != NULL) {
        *state_out = panel;
    }
    return 0;
}

/* Reads whatever events the card has queued; a completed flip is the only one asked for. */
static void inkcell_kms_drain_events(struct inkcell_kms_panel *panel) {
    uint8_t buf[1024];
    for (;;) {
        const ssize_t n = read(panel->fd, buf, sizeof buf);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            return;
        }
        for (size_t at = 0U; at + sizeof(struct drm_event) <= (size_t)n;) {
            struct drm_event event;
            memcpy(&event, buf + at, sizeof event);
            if (event.length < sizeof event) {
                return;
            }
            if (event.type == DRM_EVENT_FLIP_COMPLETE) {
                panel->flip_pending = false;
            }
            at += event.length;
        }
    }
}

/*
 * Until the last flip has happened. The buffer it flipped away from is the one about to be
 * written, and the panel may be reading it until the flip completes.
 */
static void inkcell_kms_wait_flip(struct inkcell_kms_panel *panel) {
    const uint64_t deadline = inkwell_time_monotonic_ms() + KMS_FLIP_WAIT_MS;
    while (panel->flip_pending) {
        const uint64_t now = inkwell_time_monotonic_ms();
        if (now >= deadline) {
            if (!panel->flip_wait_logged) {
                inkwell_log_warn("ui", "KMS: a page flip took over %d ms; not waiting for it",
                                 KMS_FLIP_WAIT_MS);
                panel->flip_wait_logged = true;
            }
            panel->flip_pending = false;
            return;
        }
        struct pollfd pfd = {.fd = panel->fd, .events = POLLIN};
        const int ready = poll(&pfd, 1, (int)(deadline - now));
        if (ready < 0 && errno != EINTR) {
            panel->flip_pending = false;
            return;
        }
        if (ready > 0) {
            inkcell_kms_drain_events(panel);
        }
    }
}

struct inkcell_kms_commit {
    uint32_t objs[3];
    uint32_t counts[3];
    uint32_t props[16];
    uint64_t values[16];
    uint32_t nobjs;
    uint32_t nprops;
};

static void inkcell_kms_commit_add(struct inkcell_kms_commit *c, uint32_t obj, uint32_t prop,
                                   uint64_t value) {
    if (c->nobjs == 0U || c->objs[c->nobjs - 1U] != obj) {
        c->objs[c->nobjs] = obj;
        c->counts[c->nobjs] = 0U;
        ++c->nobjs;
    }
    c->counts[c->nobjs - 1U]++;
    c->props[c->nprops] = prop;
    c->values[c->nprops] = value;
    ++c->nprops;
}

static int inkcell_kms_commit_run(int fd, struct inkcell_kms_commit *c, uint32_t flags) {
    struct drm_mode_atomic atomic = {
        .flags = flags,
        .count_objs = c->nobjs,
        .objs_ptr = inkcell_kms_ptr(c->objs),
        .count_props_ptr = inkcell_kms_ptr(c->counts),
        .props_ptr = inkcell_kms_ptr(c->props),
        .prop_values_ptr = inkcell_kms_ptr(c->values),
    };
    return inkcell_kms_ioctl(fd, DRM_IOCTL_MODE_ATOMIC, &atomic);
}

/* Lights the panel with `fb_id`: the mode, the CRTC and the plane in one blocking commit. */
static int inkcell_kms_modeset(struct inkcell_kms_panel *panel, uint32_t fb_id) {
    const uint32_t w = panel->state.surface.width;
    const uint32_t h = panel->state.surface.height;
    if (!panel->atomic) {
        uint32_t connector = panel->connector_id;
        struct drm_mode_crtc crtc = {
            .set_connectors_ptr = inkcell_kms_ptr(&connector),
            .count_connectors = 1U,
            .crtc_id = panel->crtc_id,
            .fb_id = fb_id,
            .mode_valid = 1U,
            .mode = panel->mode,
        };
        return inkcell_kms_ioctl(panel->fd, DRM_IOCTL_MODE_SETCRTC, &crtc);
    }
    const struct inkcell_kms_props *p = &panel->props;
    struct inkcell_kms_commit c = {0};
    inkcell_kms_commit_add(&c, panel->connector_id, p->connector_crtc_id, panel->crtc_id);
    inkcell_kms_commit_add(&c, panel->crtc_id, p->crtc_mode_id, panel->mode_blob);
    inkcell_kms_commit_add(&c, panel->crtc_id, p->crtc_active, 1U);
    inkcell_kms_commit_add(&c, panel->plane_id, p->plane_fb_id, fb_id);
    inkcell_kms_commit_add(&c, panel->plane_id, p->plane_crtc_id, panel->crtc_id);
    /* The source rectangle is 16.16 fixed point; the destination is whole pixels. */
    inkcell_kms_commit_add(&c, panel->plane_id, p->plane_src_x, 0U);
    inkcell_kms_commit_add(&c, panel->plane_id, p->plane_src_y, 0U);
    inkcell_kms_commit_add(&c, panel->plane_id, p->plane_src_w, (uint64_t)w << 16);
    inkcell_kms_commit_add(&c, panel->plane_id, p->plane_src_h, (uint64_t)h << 16);
    inkcell_kms_commit_add(&c, panel->plane_id, p->plane_crtc_x, 0U);
    inkcell_kms_commit_add(&c, panel->plane_id, p->plane_crtc_y, 0U);
    inkcell_kms_commit_add(&c, panel->plane_id, p->plane_crtc_w, w);
    inkcell_kms_commit_add(&c, panel->plane_id, p->plane_crtc_h, h);
    return inkcell_kms_commit_run(panel->fd, &c, DRM_MODE_ATOMIC_ALLOW_MODESET);
}

/* Shows `fb_id` at the next vertical blank, and asks to be told when it has. */
static int inkcell_kms_flip(struct inkcell_kms_panel *panel, uint32_t fb_id) {
    if (!panel->atomic) {
        struct drm_mode_crtc_page_flip flip = {
            .crtc_id = panel->crtc_id,
            .fb_id = fb_id,
            .flags = DRM_MODE_PAGE_FLIP_EVENT,
        };
        return inkcell_kms_ioctl(panel->fd, DRM_IOCTL_MODE_PAGE_FLIP, &flip);
    }
    struct inkcell_kms_commit c = {0};
    inkcell_kms_commit_add(&c, panel->plane_id, panel->props.plane_fb_id, fb_id);
    return inkcell_kms_commit_run(panel->fd, &c,
                                  DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT);
}

static void inkcell_kms_owe(struct inkcell_kms_buffer *b, uint32_t top, uint32_t bottom) {
    if (b->owed_top >= b->owed_bottom) {
        b->owed_top = top;
        b->owed_bottom = bottom;
        return;
    }
    b->owed_top = top < b->owed_top ? top : b->owed_top;
    b->owed_bottom = bottom > b->owed_bottom ? bottom : b->owed_bottom;
}

static void inkcell_backend_kms_present(void *state_ptr, const void *snapshot, void *userdata) {
    (void)userdata;
    struct inkcell_kms_panel *panel = inkcell_kms_panel_of(state_ptr);
    if (panel == NULL || snapshot == NULL || panel->draw_buffer == NULL) {
        return;
    }
    struct inkcell_draw_state *state = &panel->state;
    inkcell_fb_state_set_now(state, inkwell_time_monotonic_ms());
    inkcell_latency_frame_begin();
    inkcell_fb_render(state, snapshot);

    /* What changed since the last frame, as a band of rows. */
    struct inkcell_fb_damage_rect rects[KMS_DAMAGE_RECTS];
    const size_t count = inkcell_fb_damage_rects(state, panel->draw_buffer, panel->shown,
                                                 !panel->frame_valid, rects, KMS_DAMAGE_RECTS);
    panel->frame_valid = true;
    uint32_t top = state->surface.height;
    uint32_t bottom = 0U;
    for (size_t i = 0U; i < count; ++i) {
        if (!rects[i].valid) {
            continue;
        }
        const uint32_t y = rects[i].y > 0 ? (uint32_t)rects[i].y : 0U;
        const uint32_t b = rects[i].bottom > 0 ? (uint32_t)rects[i].bottom : 0U;
        top = y < top ? y : top;
        bottom = b > bottom ? b : bottom;
    }
    bottom = bottom < state->surface.height ? bottom : state->surface.height;
    if (top >= bottom && panel->mode_set) {
        /* Nothing moved: the panel already shows this frame. */
        panel->presented = true;
        inkcell_latency_frame_drawn(0U);
        inkcell_latency_frame_end(0U);
        return;
    }

    /* The back buffer, once the flip away from it has happened. */
    inkcell_kms_wait_flip(panel);
    const unsigned back_index = panel->mode_set ? 1U - panel->front : 0U;
    struct inkcell_kms_buffer *back = &panel->buffers[back_index];
    struct inkcell_kms_buffer *other = &panel->buffers[1U - back_index];
    if (top < bottom) {
        inkcell_kms_owe(back, top, bottom);
        inkcell_kms_owe(other, top, bottom);
    }
    size_t written = 0U;
    if (back->owed_top < back->owed_bottom) {
        const size_t stride = state->surface.stride;
        const size_t from = (size_t)back->owed_top * stride;
        written = (size_t)(back->owed_bottom - back->owed_top) * stride;
        memcpy(back->map + from, panel->draw_buffer + from, written);
        back->owed_top = 0U;
        back->owed_bottom = 0U;
    }
    inkcell_latency_frame_drawn(written);

    if (!panel->mode_set) {
        const int rc = inkcell_kms_modeset(panel, back->fb_id);
        if (rc < 0) {
            if (!panel->modeset_failed_logged) {
                inkwell_log_warn("ui", "KMS: modeset failed: %s", strerror(-rc));
                panel->modeset_failed_logged = true;
            }
            /* Owed in full again, so the next attempt puts up a whole frame. */
            inkcell_kms_owe(back, 0U, state->surface.height);
        } else {
            panel->mode_set = true;
            panel->front = back_index;
        }
    } else {
        const int rc = inkcell_kms_flip(panel, back->fb_id);
        if (rc < 0) {
            if (!panel->flip_failed_logged) {
                inkwell_log_warn("ui", "KMS: page flip failed: %s", strerror(-rc));
                panel->flip_failed_logged = true;
            }
        } else {
            panel->flip_pending = true;
            panel->front = back_index;
        }
    }
    panel->presented = true;
    inkcell_latency_frame_end(written);
}

static void inkcell_backend_kms_shutdown(void *state_ptr, void *userdata) {
    (void)userdata;
    struct inkcell_kms_panel *panel = inkcell_kms_panel_of(state_ptr);
    if (panel == NULL) {
        return;
    }
    inkcell_kms_wait_flip(panel);
    inkcell_fb_set_app(&panel->state, NULL);
    inkcell_fb_glyph_cache_free(&panel->state);
    inkcell_kms_release(panel);
}

static bool inkcell_backend_kms_animating(void *state_ptr, void *userdata) {
    (void)userdata;
    struct inkcell_kms_panel *panel = inkcell_kms_panel_of(state_ptr);
    return panel != NULL && inkcell_fb_state_animating(&panel->state);
}

static uint32_t inkcell_backend_kms_page_rows(void *state_ptr, void *userdata) {
    (void)userdata;
    struct inkcell_kms_panel *panel = inkcell_kms_panel_of(state_ptr);
    return panel != NULL ? panel->state.page_rows : 0U;
}

static const struct inkcell_focus_map *inkcell_backend_kms_focus_map(void *state_ptr,
                                                                     void *userdata) {
    (void)userdata;
    struct inkcell_kms_panel *panel = inkcell_kms_panel_of(state_ptr);
    return panel != NULL ? panel->state.focus : NULL;
}

/* The frame as drawn: the draw buffer, which the dumb buffers are copies of and which is in
   ordinary memory rather than a mapping that is slow to read back. */
static bool inkcell_backend_kms_frame(void *state_ptr, void *userdata,
                                      struct inkcell_surface *out) {
    (void)userdata;
    struct inkcell_kms_panel *panel = inkcell_kms_panel_of(state_ptr);
    if (panel == NULL || out == NULL || !panel->presented || panel->draw_buffer == NULL) {
        return false;
    }
    *out = panel->state.surface;
    return true;
}

static const struct inkcell_backend k_kms_backend = {
    .name = "kms",
    .init = inkcell_backend_kms_init,
    .shutdown = inkcell_backend_kms_shutdown,
    .present = inkcell_backend_kms_present,
    .animating = inkcell_backend_kms_animating,
    .page_rows = inkcell_backend_kms_page_rows,
    .focus_map = inkcell_backend_kms_focus_map,
    .frame = inkcell_backend_kms_frame,
};

bool inkcell_backend_kms_is_available(void) {
    const char *device = inkwell_env_get("KMS_DEVICE");
    if (device != NULL && device[0] != '\0') {
        return access(device, R_OK | W_OK) == 0;
    }
    for (int i = 0; i < KMS_CARDS_MAX; ++i) {
        char path[32];
        snprintf(path, sizeof path, "/dev/dri/card%d", i);
        if (access(path, R_OK | W_OK) == 0) {
            return true;
        }
    }
    return false;
}

const struct inkcell_backend *inkcell_backend_kms(void) {
    return &k_kms_backend;
}

#endif /* INKCELL_HAVE_KMS */
