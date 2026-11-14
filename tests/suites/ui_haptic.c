#define _GNU_SOURCE

/*
 * A force-feedback motor. The strength curve everywhere; refusals for what is not there; and on
 * Linux as root, the real thing: a uinput device that says it has a rumble motor, opened by a
 * child that pulses it, while this process answers the kernel's upload and sees the play arrive.
 */

#include "framework/inkcell_test.h"

#include "inkcell/ui/haptic.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__)
#include "inkwell/base/ioctl.h"

#include <dirent.h>
#include <fcntl.h>
#include <linux/uinput.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

INKCELL_TEST_CASE(haptic_strength_is_a_squared_curve, unit) {
    INKCELL_TEST_FAIL_IF(inkcell_haptic_magnitude(0U) != 0U, "0 is still");
    INKCELL_TEST_FAIL_IF(inkcell_haptic_magnitude(100U) != 0xFFFFU, "100 is full");
    INKCELL_TEST_FAIL_IF(inkcell_haptic_magnitude(500U) != 0xFFFFU, "past 100 is 100");
    const uint16_t half = inkcell_haptic_magnitude(50U);
    INKCELL_TEST_FAIL_IF(half < 0x3F00U || half > 0x4100U, "50 is a quarter of the drive");
    record_success(test_name);
}

INKCELL_TEST_CASE(haptic_refuses_what_is_not_there, unit) {
    struct inkcell_haptic h;
#if defined(__linux__)
    INKCELL_TEST_FAIL_IF(inkcell_haptic_open(&h, "/nonexistent/event0") != -ENOENT, "no node");
    char dir[] = "/tmp/inkcell-haptic-XXXXXX";
    INKCELL_TEST_FAIL_IF(mkdtemp(dir) == NULL, "a directory");
    INKCELL_TEST_FAIL_IF(inkcell_haptic_find(&h, dir) != -ENOENT, "an empty directory: none");
    (void)rmdir(dir);
#else
    INKCELL_TEST_FAIL_IF(inkcell_haptic_open(&h, "/dev/input/event0") != -ENOSYS,
                         "off Linux, every open refuses");
#endif
    INKCELL_TEST_FAIL_IF(h.fd != -1, "a refused handle is closed");
    INKCELL_TEST_FAIL_IF(inkcell_haptic_pulse(&h, 10U, 50U) != -EINVAL, "a closed one buzzes no");
    inkcell_haptic_close(&h);
    record_success(test_name);
}

#if defined(__linux__)

/* The evdev node uinput made for `sysname`, made in `dir` from its sysfs numbers - a container's
   /dev does not grow nodes by itself. */
static bool node_of(const char *sysname, const char *dir, char *path, size_t size) {
    char sys[160];
    snprintf(sys, sizeof sys, "/sys/devices/virtual/input/%s", sysname);
    DIR *d = opendir(sys);
    if (d == NULL) {
        return false;
    }
    bool made = false;
    for (struct dirent *e = readdir(d); e != NULL && !made; e = readdir(d)) {
        if (strncmp(e->d_name, "event", 5) != 0) {
            continue;
        }
        char devfile[512];
        snprintf(devfile, sizeof devfile, "%s/%s/dev", sys, e->d_name);
        FILE *f = fopen(devfile, "r");
        unsigned major_n = 0, minor_n = 0;
        if (f != NULL && fscanf(f, "%u:%u", &major_n, &minor_n) == 2) {
            snprintf(path, size, "%s/%s", dir, e->d_name);
            made = mknod(path, S_IFCHR | 0600, makedev(major_n, minor_n)) == 0;
        }
        if (f != NULL) {
            fclose(f);
        }
    }
    closedir(d);
    return made;
}

INKCELL_TEST_CASE(haptic_pulses_a_real_motor_through_the_kernel, unit) {
    const int ui = geteuid() == 0 ? open("/dev/uinput", O_RDWR | O_CLOEXEC) : -1;
    if (ui < 0) {
        printf("  (skipped: needs root and /dev/uinput)\n");
        record_success(test_name);
        return;
    }
    (void)ioctl(ui, inkwell_ioctl_request_of(UI_SET_EVBIT), EV_FF);
    (void)ioctl(ui, inkwell_ioctl_request_of(UI_SET_FFBIT), FF_RUMBLE);
    struct uinput_setup setup;
    memset(&setup, 0, sizeof setup);
    setup.id.bustype = BUS_VIRTUAL;
    snprintf(setup.name, sizeof setup.name, "inkcell test motor");
    setup.ff_effects_max = 1;
    char sysname[64] = "";
    char dir[] = "/tmp/inkcell-motor-XXXXXX";
    char node[128] = "";
    const bool ready =
        ioctl(ui, inkwell_ioctl_request_of(UI_DEV_SETUP), &setup) == 0 &&
        ioctl(ui, inkwell_ioctl_request_of(UI_DEV_CREATE)) == 0 &&
        ioctl(ui, inkwell_ioctl_request_of(UI_GET_SYSNAME(sizeof sysname)), sysname) >= 0 &&
        mkdtemp(dir) != NULL && node_of(sysname, dir, node, sizeof node);
    if (!ready) {
        close(ui);
        printf("  (skipped: uinput would not make a motor here)\n");
        record_success(test_name);
        return;
    }

    /* The child is the program: it finds the motor and buzzes it once. Its upload blocks until
       this side, the motor's driver, says yes. */
    const pid_t child = fork();
    if (child == 0) {
        struct inkcell_haptic h;
        int r = inkcell_haptic_find(&h, dir);
        if (r == 0) {
            r = inkcell_haptic_pulse(&h, 30U, 60U);
        }
        _exit(r == 0 && h.rumble && strcmp(h.name, "inkcell test motor") == 0 ? 0 : 1);
    }
    bool uploaded = false, played = false;
    uint16_t magnitude = 0, length = 0;
    for (int i = 0; i < 100 && !played; ++i) {
        struct pollfd p = {.fd = ui, .events = POLLIN};
        if (poll(&p, 1, 50) <= 0) {
            continue;
        }
        struct input_event ev;
        if (read(ui, &ev, sizeof ev) != (ssize_t)sizeof ev) {
            break;
        }
        if (ev.type == EV_UINPUT && ev.code == UI_FF_UPLOAD) {
            struct uinput_ff_upload up;
            memset(&up, 0, sizeof up);
            up.request_id = (uint32_t)ev.value;
            (void)ioctl(ui, inkwell_ioctl_request_of(UI_BEGIN_FF_UPLOAD), &up);
            magnitude = up.effect.u.rumble.strong_magnitude;
            length = up.effect.replay.length;
            up.retval = 0;
            (void)ioctl(ui, inkwell_ioctl_request_of(UI_END_FF_UPLOAD), &up);
            uploaded = up.effect.type == FF_RUMBLE;
        } else if (ev.type == EV_FF && ev.value == 1) {
            played = true;
        }
    }
    int status = 1;
    (void)waitpid(child, &status, 0);
    (void)ioctl(ui, inkwell_ioctl_request_of(UI_DEV_DESTROY));
    close(ui);
    (void)unlink(node);
    (void)rmdir(dir);
    INKCELL_TEST_FAIL_IF(!uploaded, "a rumble effect was uploaded");
    INKCELL_TEST_FAIL_IF(length != 30U || magnitude != inkcell_haptic_magnitude(60U),
                         "30 ms at 60 percent");
    INKCELL_TEST_FAIL_IF(!played, "and played");
    INKCELL_TEST_FAIL_IF(!WIFEXITED(status) || WEXITSTATUS(status) != 0,
                         "the program found the motor by its name and pulsed it");
    record_success(test_name);
}

#endif
