/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
#ifndef MUSE_VID_H
#define MUSE_VID_H

#include <stdbool.h>

/* The picture half of a file that has one: MP4's video.
 *
 * Its own thread with its own demuxer and decoder, so the sound's thread -
 * which is what everyone hears glitch - never waits on a video frame. The
 * sound stays the clock: the player thread says where the listener is after
 * every write (vid_clock), and this thread puts each picture out when that
 * clock reaches it, into the shared frames in /tmp (src/mp4frame.h) the
 * launcher draws from. A picture that comes too late is dropped rather than
 * shown late, and a decoder that keeps falling behind skips the frames
 * nothing else refers to, so the picture keeps time with the sound however
 * heavy the file is.
 *
 * Every call is cheap and never blocks on decoding: the work happens on the
 * thread, which takes the newest request whenever it gets to it. */

/* Map the frames file and start the thread. False when the file cannot be
 * made, and then the daemon plays sound only, as it always has. */
bool vid_init(void);
void vid_quit(void);

/* The file `path` with its picture at `at` seconds - opened if it is a new
 * file, moved there if it is the same one and more than a frame or two away,
 * and otherwise left alone, so a pause and resume does not seek. */
void vid_sync(const char *path, double at);

/* No picture: the file has none, or nothing is playing. */
void vid_close(void);

/* Where the listener is, in the file's seconds, and whether that is moving.
 * From the player thread after each write and each command. */
void vid_clock(double heard, bool playing);

/* Whether anyone is looking. While nobody is - the launcher on another
 * screen, the panel dark - nothing is decoded, and the picture is moved to
 * the sound again when somebody looks. */
void vid_show(bool on);

#endif
