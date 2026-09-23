#include "framework/inkcell_test.h"

#include "inkcell/ui/input.h"
#include "inkwell/runtime/timer.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

struct input_windows_probe {
    int watched;
    bool removed;
};

static int input_windows_add(void *ctx, int fd,
                             int (*callback)(int fd, uint32_t events, void *userdata),
                             void *userdata) {
    struct input_windows_probe *probe = (struct input_windows_probe *)ctx;
    (void)callback;
    (void)userdata;
    probe->watched = fd;
    return 0;
}

static void input_windows_remove(void *ctx, int fd) {
    struct input_windows_probe *probe = (struct input_windows_probe *)ctx;
    if (fd == probe->watched) {
        probe->removed = true;
    }
}

INKCELL_TEST_CASE(input_windows_releases_repeat_timer, unit) {
    struct input_windows_probe probe = {.watched = -1};
    const struct inkcell_input_host host = {
        .ctx = &probe, .add_fd = input_windows_add, .remove_fd = input_windows_remove};
    struct inkcell_input input;
    INKCELL_TEST_FAIL_IF(inkcell_input_init(&input, &host) != 0,
                         "window input should initialize without evdev");
    const int timer = input.repeat_timer_fd;
    inkcell_input_shutdown(&input);
    INKCELL_TEST_FAIL_IF(timer <= 0 || probe.watched != timer || !probe.removed,
                         "repeat timer should have been watched and removed");
    INKCELL_TEST_FAIL_IF(inkwell_timer_read(timer) != -EBADF,
                         "shutdown should close the native timer handle");
    record_success(test_name);
}
