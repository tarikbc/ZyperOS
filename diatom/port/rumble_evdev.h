/* Rumble through a Linux input device's force feedback (EV_FF, FF_RUMBLE).
 *
 * A port's own detail, kept out of the port file so any Linux port can use it:
 * the port finds the device, this runs the motor. One effect, uploaded once
 * and updated in place, which is how ff-memless expects a strength that keeps
 * changing (flycast2021 eases its Puru Puru pack down every frame). */
#ifndef DIATOM_RUMBLE_EVDEV_H
#define DIATOM_RUMBLE_EVDEV_H

#include <stdbool.h>
#include <stdint.h>

/* `fd` is a descriptor the port already holds on the device, read-only is
 * enough: it is only asked what the device can do. If it can rumble, `path`
 * is opened again for writing, which force feedback needs. True if so. */
bool evdev_rumble_open(int fd, const char *path);
bool evdev_rumble_set(uint16_t strong, uint16_t weak);
void evdev_rumble_close(void);

#endif
