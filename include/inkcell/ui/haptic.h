#ifndef INKCELL_HAPTIC_H
#define INKCELL_HAPTIC_H

/*
 * A force-feedback motor: a phone's vibration actuator, a gamepad's rumble. The answer a device
 * gives a touch through the hand rather than the eye.
 *
 * On Linux it is evdev's force-feedback interface, the same input node a device reports keys
 * on: EVIOCSFF uploads an effect, an EV_FF event on the node plays it. Drivers for phone haptic
 * actuators and for gamepads both speak it, so one call buzzes either. Elsewhere every open
 * refuses with -ENOSYS: the header exists and every symbol links, as for the KMS backend.
 *
 * **One pulse at a time.** inkcell_haptic_pulse() plays a single buzz of a length and a strength,
 * and replaces the one playing; a pattern (a heartbeat, an alarm's rhythm) is the caller's
 * timer stepping through pulses. Keeping the device to one effect, re-uploaded in place, is what
 * every driver supports, where a bank of effects is not.
 *
 * **Which kind of effect.** A device that does FF_RUMBLE gets one - two motors at the same
 * strength. One that only does FF_PERIODIC gets a sine at the strength; one that does neither
 * is not a motor this can drive, and opening it refuses.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The longest one pulse may be: a buzz longer than this is a pattern, and a pattern is pulses. */
#define INKCELL_HAPTIC_PULSE_MAX_MS 5000U

struct inkcell_haptic {
    int fd;         /* -1 while closed */
    int16_t effect; /* the uploaded effect's id; -1 before the first pulse */
    bool rumble;    /* FF_RUMBLE; otherwise FF_PERIODIC */
    char name[64];  /* what the device calls itself */
};

/* Opens the input node at `path` if it has a motor. 0, -ENOTSUP for a node with no motor this
   drives, another negative errno for no such node, or -ENOSYS off Linux. */
int inkcell_haptic_open(struct inkcell_haptic *h, const char *path);

/* The first node under `dir` (/dev/input, or a test's) whose name starts "event" and has a motor.
   0, or -ENOENT for none (-ENOSYS off Linux). */
int inkcell_haptic_find(struct inkcell_haptic *h, const char *dir);

/* A strength in percent (clamped to 0..100) as the 16-bit magnitude an effect carries. Squared,
   since how strong a buzz feels is not linear in the drive. Pure, so tested everywhere. */
uint16_t inkcell_haptic_magnitude(uint32_t strength);

/* One buzz, `ms` long (at most INKCELL_HAPTIC_PULSE_MAX_MS) at `strength` percent, in place of
   any playing. 0 or a negative errno; -EINVAL for a closed handle or a pulse of 0 ms. */
int inkcell_haptic_pulse(struct inkcell_haptic *h, uint32_t ms, uint32_t strength);

/* The motor stopped now. Safe on a closed handle. */
void inkcell_haptic_stop(struct inkcell_haptic *h);

/* Stops, takes the effect back, and closes. Safe on a closed handle. */
void inkcell_haptic_close(struct inkcell_haptic *h);

#ifdef __cplusplus
}
#endif

#endif /* INKCELL_HAPTIC_H */
