#include "inkcell/ui/haptic.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#if defined(__linux__)
#include "inkwell/base/ioctl.h"

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <unistd.h>
#endif

uint16_t inkcell_haptic_magnitude(uint32_t strength) {
    const uint32_t s = strength > 100U ? 100U : strength;
    return (uint16_t)((s * s * 0xFFFFU) / 10000U);
}

#if defined(__linux__)

#define BITS_PER_LONG (8U * sizeof(unsigned long))

static bool bit_set(const unsigned long *bits, unsigned code) {
    return ((bits[code / BITS_PER_LONG] >> (code % BITS_PER_LONG)) & 1UL) != 0UL;
}

int inkcell_haptic_open(struct inkcell_haptic *h, const char *path) {
    if (h == NULL || path == NULL) {
        return -EINVAL;
    }
    memset(h, 0, sizeof *h);
    h->fd = -1;
    h->effect = -1;
    /* Read-write: the effect is played by writing an event to the node. */
    const int fd = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        return -errno;
    }
    unsigned long ff[(FF_MAX + BITS_PER_LONG) / BITS_PER_LONG];
    memset(ff, 0, sizeof ff);
    if (ioctl(fd, inkwell_ioctl_request_of(EVIOCGBIT(EV_FF, sizeof ff)), ff) < 0) {
        close(fd);
        return -ENOTSUP;
    }
    if (bit_set(ff, FF_RUMBLE)) {
        h->rumble = true;
    } else if (!bit_set(ff, FF_PERIODIC) || !bit_set(ff, FF_SINE)) {
        close(fd);
        return -ENOTSUP;
    }
    if (ioctl(fd, inkwell_ioctl_request_of(EVIOCGNAME(sizeof h->name - 1U)), h->name) < 0) {
        h->name[0] = '\0';
    }
    h->fd = fd;
    return 0;
}

int inkcell_haptic_find(struct inkcell_haptic *h, const char *dir) {
    if (h == NULL || dir == NULL) {
        return -EINVAL;
    }
    memset(h, 0, sizeof *h);
    h->fd = -1;
    h->effect = -1;
    DIR *d = opendir(dir);
    if (d == NULL) {
        return -ENOENT;
    }
    int found = -ENOENT;
    for (struct dirent *e = readdir(d); e != NULL && found != 0; e = readdir(d)) {
        if (strncmp(e->d_name, "event", 5) != 0) {
            continue;
        }
        char path[256];
        if (snprintf(path, sizeof path, "%s/%s", dir, e->d_name) >= (int)sizeof path) {
            continue;
        }
        found = inkcell_haptic_open(h, path) == 0 ? 0 : -ENOENT;
    }
    closedir(d);
    return found;
}

static int play(int fd, int16_t effect, int value) {
    struct input_event ev;
    memset(&ev, 0, sizeof ev);
    ev.type = EV_FF;
    ev.code = (uint16_t)effect;
    ev.value = value;
    return write(fd, &ev, sizeof ev) == (ssize_t)sizeof ev ? 0 : -errno;
}

int inkcell_haptic_pulse(struct inkcell_haptic *h, uint32_t ms, uint32_t strength) {
    if (h == NULL || h->fd < 0 || ms == 0U) {
        return -EINVAL;
    }
    const uint16_t length =
        (uint16_t)(ms > INKCELL_HAPTIC_PULSE_MAX_MS ? INKCELL_HAPTIC_PULSE_MAX_MS : ms);
    const uint16_t magnitude = inkcell_haptic_magnitude(strength);
    struct ff_effect effect;
    memset(&effect, 0, sizeof effect);
    effect.id = h->effect; /* -1 asks for a new one; an id updates that one in place */
    effect.replay.length = length;
    if (h->rumble) {
        effect.type = FF_RUMBLE;
        effect.u.rumble.strong_magnitude = magnitude;
        effect.u.rumble.weak_magnitude = magnitude;
    } else {
        effect.type = FF_PERIODIC;
        effect.u.periodic.waveform = FF_SINE;
        effect.u.periodic.period = 10; /* ms: about the resonance of a phone's actuator */
        effect.u.periodic.magnitude = (int16_t)(magnitude / 2U);
    }
    if (ioctl(h->fd, inkwell_ioctl_request_of(EVIOCSFF), &effect) < 0) {
        return -errno;
    }
    h->effect = effect.id;
    return play(h->fd, h->effect, 1);
}

void inkcell_haptic_stop(struct inkcell_haptic *h) {
    if (h != NULL && h->fd >= 0 && h->effect >= 0) {
        (void)play(h->fd, h->effect, 0);
    }
}

void inkcell_haptic_close(struct inkcell_haptic *h) {
    if (h == NULL || h->fd < 0) {
        return;
    }
    inkcell_haptic_stop(h);
    if (h->effect >= 0) {
        (void)ioctl(h->fd, inkwell_ioctl_request_of(EVIOCRMFF), (unsigned long)h->effect);
    }
    close(h->fd);
    h->fd = -1;
    h->effect = -1;
}

#else

/* evdev's force feedback is Linux's. A gamepad's rumble elsewhere is the SDL backend's
   question (SDL_GameControllerRumble), and a phone's actuator elsewhere is not inkcell's. */
int inkcell_haptic_open(struct inkcell_haptic *h, const char *path) {
    (void)path;
    if (h == NULL) {
        return -EINVAL;
    }
    memset(h, 0, sizeof *h);
    h->fd = -1;
    h->effect = -1;
    return -ENOSYS;
}

int inkcell_haptic_find(struct inkcell_haptic *h, const char *dir) {
    return inkcell_haptic_open(h, dir);
}

int inkcell_haptic_pulse(struct inkcell_haptic *h, uint32_t ms, uint32_t strength) {
    (void)h;
    (void)ms;
    (void)strength;
    return -EINVAL; /* never open */
}

void inkcell_haptic_stop(struct inkcell_haptic *h) {
    (void)h;
}

void inkcell_haptic_close(struct inkcell_haptic *h) {
    (void)h;
}

#endif
