/* Forced into port/rumble_evdev.c when test/rumble_test.c builds it (-include):
 * the system headers that file uses, and then its four calls on the device
 * sent to the test's model of the kernel instead. Test fixture. */
#ifndef DIATOM_TEST_RUMBLE_FAKE_H
#define DIATOM_TEST_RUMBLE_FAKE_H

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

int     fake_open(const char *path, int flags, ...);
int     fake_ioctl(int fd, unsigned long req, ...);
ssize_t fake_write(int fd, const void *buf, size_t n);
int     fake_close(int fd);

#define open(...)  fake_open(__VA_ARGS__)
#define ioctl(...) fake_ioctl(__VA_ARGS__)
#define write(...) fake_write(__VA_ARGS__)
#define close(...) fake_close(__VA_ARGS__)

#endif
