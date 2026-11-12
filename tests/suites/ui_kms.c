#define _POSIX_C_SOURCE 200809L

/*
 * The KMS backend, as far as a machine without a panel can hold it: that it refuses where it
 * cannot run, and refuses cleanly - with an errno, and with nothing left open - where it can run
 * but has no card. Drawing to a real CRTC is checked on a virtual GPU instead, by
 * scripts/kms-vm.py, which compares every frame the gallery shows with what the panel scanned
 * out.
 */

#include "framework/inkcell_test.h"

#include "inkcell/ui/kms.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

INKCELL_TEST_CASE(kms_backend_is_named_and_complete, unit) {
    const struct inkcell_backend *backend = inkcell_backend_kms();
    INKCELL_TEST_FAIL_IF(backend == NULL || backend->init == NULL, "the vtable must exist");
    INKCELL_TEST_FAIL_IF(strcmp(backend->name, "kms") != 0, "it is called kms");
    record_success(test_name);
}

INKCELL_TEST_CASE(kms_init_without_a_context_is_refused, unit) {
    void *state = NULL;
    const int rc = inkcell_backend_kms()->init(&state, NULL);
#if defined(INKCELL_HAVE_KMS)
    INKCELL_TEST_FAIL_IF(rc != -EINVAL, "no context is an invalid argument");
#else
    INKCELL_TEST_FAIL_IF(rc != -ENOTSUP, "without DRM there is nothing to start");
#endif
    INKCELL_TEST_FAIL_IF(state != NULL, "a refused init hands back no state");
    record_success(test_name);
}

#if defined(INKCELL_HAVE_KMS)
INKCELL_TEST_CASE(kms_a_card_that_is_not_there_is_refused_and_leaks_nothing, unit) {
    /* The descriptor a successful open would take, found before and after: a refusal that
       left the card open would move it. */
    const int before = open("/dev/null", O_RDONLY | O_CLOEXEC);
    close(before);
    struct inkcell_backend_kms_context context = {.device = "/nonexistent/dri/card0"};
    void *state = NULL;
    const int rc = inkcell_backend_kms()->init(&state, &context);
    const int after = open("/dev/null", O_RDONLY | O_CLOEXEC);
    close(after);
    INKCELL_TEST_FAIL_IF(rc != -ENOENT, "a missing card is ENOENT");
    INKCELL_TEST_FAIL_IF(state != NULL, "a refused init hands back no state");
    INKCELL_TEST_FAIL_IF(before != after, "a refused init leaves nothing open");
    record_success(test_name);
}

INKCELL_TEST_CASE(kms_a_file_that_is_not_a_card_is_refused, unit) {
    struct inkcell_backend_kms_context context = {.device = "/dev/null"};
    void *state = NULL;
    const int rc = inkcell_backend_kms()->init(&state, &context);
    INKCELL_TEST_FAIL_IF(rc >= 0, "/dev/null has no modes to set");
    INKCELL_TEST_FAIL_IF(state != NULL, "a refused init hands back no state");
    record_success(test_name);
}
#else
INKCELL_TEST_CASE(kms_without_drm_refuses_rather_than_pretends, unit) {
    INKCELL_TEST_FAIL_IF(inkcell_backend_kms_is_available(), "no DRM, so no card to offer");
    record_success(test_name);
}
#endif
