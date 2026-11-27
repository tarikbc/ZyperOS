/* Forced into port/rumble_evdev.c when test/rumble_test.c builds it (-include):
 * the system headers that file uses, and then its four calls on the device
 * sent to the test's model of the kernel instead, and its thread's wait for
 * a wish and the signal of one through the test, which waits for that
 * thread to have nothing left to do. Test fixture. */
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

/* rumble_evdev.c waits on one condition alone (its thread's for a wish) and
 * signals only that one, a wish or the quit, both under its lock: so the
 * thread is idle, with nothing to say, from the wait's start until the next
 * signal or the wait's end (test/rumble_test.c, settle) */
int fake_cond_wait(pthread_cond_t *cv, pthread_mutex_t *mx);
int fake_cond_signal(pthread_cond_t *cv);

#define pthread_cond_wait(...)   fake_cond_wait(__VA_ARGS__)
#define pthread_cond_signal(...) fake_cond_signal(__VA_ARGS__)

/* A close's wait for the thread, 500 ms on the device. Longer here, as the
 * thread that has to run in it is one a busy machine may not run for a
 * while (make check runs many at once, 2026-11-24): a close that gave up on
 * it would leave the motor and the device to the thread, and the checks
 * after it would read a stuck device that is not. A close on a device that
 * is stuck still gives up, at this, and test/rumble_test.c's
 * TEST_CLOSE_WAIT_MS is the same number. */
#define CLOSE_WAIT_MS 2000

#endif
