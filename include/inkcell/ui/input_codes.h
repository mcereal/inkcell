#pragma once

/*
 * The evdev key, button and axis numbering, which is inkcell's input vocabulary.
 *
 * inkcell_input_handle_event() takes a kernel event's type and code, the device profiles are
 * tables of BTN_ codes, and the window backend translates a keyboard into the same KEY_ codes a
 * USB keyboard on the device would send - so a keycap means one thing everywhere. The numbers
 * are the Linux input ABI and do not change.
 *
 * On Linux they come from the kernel's own header. Anywhere else - macOS, as a development host
 * with a window and no evdev - the ones inkcell and its applications name are spelled out below
 * with the kernel's values, each behind #ifndef so a host that does have them is not redefined.
 * <linux/input-event-codes.h> is only #defines, which is why it may appear in a public header
 * where <linux/input.h> (struct input_event, the ioctls) may not; see scripts/check-platform.py.
 *
 * A code this list lacks is one to add here, with the kernel's value, rather than to #ifdef at
 * the place that wanted it.
 */

#if defined(__linux__)
#include <linux/input-event-codes.h>
#else

/* Event types. */
#ifndef EV_SYN
#define EV_SYN 0x00
#define EV_KEY 0x01
#define EV_ABS 0x03
#define EV_REP 0x14
#define EV_MAX 0x1f
#endif

/* Keyboard keys. */
#ifndef KEY_ESC
#define KEY_ESC 1
#define KEY_BACKSPACE 14
#define KEY_TAB 15
#define KEY_ENTER 28
#define KEY_X 45
#define KEY_SPACE 57
#define KEY_F1 59
#define KEY_F2 60
#define KEY_UP 103
#define KEY_PAGEUP 104
#define KEY_LEFT 105
#define KEY_RIGHT 106
#define KEY_DOWN 108
#define KEY_PAGEDOWN 109
#define KEY_DELETE 111
#define KEY_POWER 116
#define KEY_MENU 139
#define KEY_CANCEL 223
#define KEY_MAX 0x2ff
#endif

/* Gamepad buttons. BTN_A/B/X/Y are the kernel's aliases for the four positions. */
#ifndef BTN_SOUTH
#define BTN_SOUTH 0x130
#define BTN_A BTN_SOUTH
#define BTN_EAST 0x131
#define BTN_B BTN_EAST
#define BTN_NORTH 0x133
#define BTN_X BTN_NORTH
#define BTN_WEST 0x134
#define BTN_Y BTN_WEST
#define BTN_TL 0x136
#define BTN_TR 0x137
#define BTN_TL2 0x138
#define BTN_TR2 0x139
#define BTN_SELECT 0x13a
#define BTN_START 0x13b
#define BTN_MODE 0x13c
#define BTN_DPAD_UP 0x220
#define BTN_DPAD_DOWN 0x221
#define BTN_DPAD_LEFT 0x222
#define BTN_DPAD_RIGHT 0x223
#endif

/* Absolute axes: the two analogue triggers and the d-pad hat. */
#ifndef ABS_Z
#define ABS_Z 0x02
#define ABS_RZ 0x05
#define ABS_HAT0X 0x10
#define ABS_HAT0Y 0x11
#define ABS_MAX 0x3f
#endif

#endif
