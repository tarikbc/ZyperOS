/* What port/rumble_evdev.c takes from <linux/input.h>, for test/rumble_test.c
 * on a machine that has no Linux headers (macOS). The names are the kernel's;
 * the request numbers are this test's own, read back by its fake ioctl, never
 * a kernel's. Test fixture. */
#ifndef DIATOM_TEST_FAKE_LINUX_INPUT_H
#define DIATOM_TEST_FAKE_LINUX_INPUT_H

#include <stdint.h>
#include <sys/time.h>

#define EV_FF      0x15
#define FF_RUMBLE  0x50
#define FF_MAX     0x7f

#define FAKE_EVIOCGBIT_BASE 0x2000u
#define EVIOCGBIT(ev, len)  ((unsigned long)(FAKE_EVIOCGBIT_BASE + (ev)) | ((unsigned long)(len) << 16))
#define EVIOCSFF            0x4580u

struct input_event {
	struct timeval time;
	uint16_t type;
	uint16_t code;
	int32_t  value;
};

struct ff_replay  { uint16_t length; uint16_t delay; };
struct ff_trigger { uint16_t button; uint16_t interval; };
struct ff_rumble_effect { uint16_t strong_magnitude; uint16_t weak_magnitude; };

struct ff_effect {
	uint16_t type;
	int16_t  id;
	uint16_t direction;
	struct ff_trigger trigger;
	struct ff_replay  replay;
	union { struct ff_rumble_effect rumble; } u;
};

#endif
