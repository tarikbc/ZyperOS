/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * px2sim: the GKD Pixel 2's buttons and parts in QEMU's virt machine, and the
 * link to the simulator window on the Mac (ZyperOS's simulator/).
 *
 *   px2sim --daemon          run once by /etc/inittab, before ZyperOS
 *   px2sim COMMAND [ARG...]  tell the daemon, from a shell (over SSH):
 *       press|release BUTTON[+BUTTON...]   tap BUTTON[+BUTTON...] [MS]
 *       battery PERCENT    cable 0|1    headphones 0|1    state
 *       kind wall|computer  (what the cable is in: the USB PHY's DCP or SDP)
 *       current MA          (the charging current; 0 for the CC/CV model)
 *
 * Buttons: up down left right a b x y l1 r1 l2 r2 select start menu volup
 * voldown power, px2-input's names.
 *
 * The buttons. ZyperOS, Diatom, px2-input and plastron's SDL find the Pixel
 * 2's buttons by what each input device reports: the joypad by BTN_SOUTH,
 * the volume keys ("gpio-keys") by KEY_VOLUMEUP, the power key ("rk805
 * pwrkey") by KEY_POWER, the headphone jack by SW_HEADPHONE_INSERT, first
 * match wins. So the daemon makes those four through uinput, with the names
 * and ids the Pixel 2's kernel gives them, before anything else reads
 * /dev/input, and only then loads the virtio keyboard's driver: the four are
 * event0 to event3, as on the device. The joypad takes rumble (FF_RUMBLE), as
 * the Pixel 2's does with its motor. A keyboard (QEMU's window) is taken for
 * the daemon alone (EVIOCGRAB) and its keys turned into buttons:
 *
 *   arrows  d-pad            x  A (BTN_SOUTH)    z  B (BTN_EAST)
 *   s       X (BTN_NORTH)    a  Y (BTN_WEST)
 *   q / w   L1 / R1          e / r  L2 / R2
 *   Enter   START            Right Shift, Backspace  SELECT
 *   Esc, m  FUNCTION (MENU)  = / -  volume up / down
 *   p       power            h  headphones in / out
 *
 * The parts. The kernel module px2sim_hw makes the battery, the charger, the
 * backlight and the side lights, under their Pixel 2 names; the daemon loads
 * it, sets the battery and the cable through its parameters, and reads what
 * the programs do to the backlight and lights from /dev/px2sim. And the
 * RK817's volume: the daemon adds "Master Playback Volume" to the sound card,
 * the codec's name and range (0-255, 0.375 dB a step), which ZyperOS and
 * Diatom write as on the device and the image's asound.conf applies (softvol).
 *
 * The window. A virtio-serial port named org.zyperos.px2sim, whose other end
 * is a socket QEMU keeps on the Mac (tools/qemu-run.sh), carries lines both
 * ways: the commands above from the window, and from here every change of
 * state ("state leds=3 backlight=128 bl_power=0 battery=80 cable=1 kind=1
 * headphones=0", kind 1 a wall charger and 2 a computer) and of the motor ("rumble STRONG WEAK MS", 0-65535 each).
 * The window says "hello" when it connects and gets the state back.
 */
#define _GNU_SOURCE   /* accept4 */
#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <sound/asound.h>
#include <sound/tlv.h>
#include <time.h>
#include <dirent.h>
#include <unistd.h>

#include "kbdsel.h"

#define SOCKET   "/run/px2sim.sock"
#define PARAMS   "/sys/module/px2sim_hw/parameters/"
#define PORTNAME "org.zyperos.px2sim"

enum { PAD, VOL, PWR, JACK, NDEV };

static const struct {
	const char *name, *phys;
	struct input_id id;
} devs[NDEV] = {
	/* The joypad's ids from the Pixel 2's device tree (joypad-vendor,
	 * -product, -revision), so SDL's mappings take this one for it */
	[PAD]  = { "gkd_pixel2_joypad", "rocknix-singleadc-joypad/input0",
	           { BUS_HOST, 0x0001, 0x2102, 0x0100 } },
	[VOL]  = { "gpio-keys", "gpio-keys/input0", { BUS_HOST, 0x0001, 0x0001, 0x0100 } },
	[PWR]  = { "rk805 pwrkey", "rk805_pwrkey/input0", { BUS_HOST, 0, 0, 0 } },
	[JACK] = { "rk817_int Headphones", "ALSA", { BUS_HOST, 0, 0, 0 } },
};

static const int pad_keys[] = {
	BTN_DPAD_UP, BTN_DPAD_DOWN, BTN_DPAD_LEFT, BTN_DPAD_RIGHT,
	BTN_SOUTH, BTN_EAST, BTN_NORTH, BTN_WEST, BTN_TL, BTN_TR, BTN_TL2, BTN_TR2,
	BTN_SELECT, BTN_START, BTN_TRIGGER_HAPPY1,
};
static const int vol_keys[] = { KEY_VOLUMEUP, KEY_VOLUMEDOWN };
static const int pwr_keys[] = { KEY_POWER };
static const int jack_sw[] = { SW_HEADPHONE_INSERT };

/* A button by px2-input's name */
static const struct { const char *name; int dev, code; } buttons[] = {
	{ "up", PAD, BTN_DPAD_UP },     { "down", PAD, BTN_DPAD_DOWN },
	{ "left", PAD, BTN_DPAD_LEFT }, { "right", PAD, BTN_DPAD_RIGHT },
	{ "a", PAD, BTN_SOUTH },        { "b", PAD, BTN_EAST },
	{ "x", PAD, BTN_NORTH },        { "y", PAD, BTN_WEST },
	{ "l1", PAD, BTN_TL },          { "r1", PAD, BTN_TR },
	{ "l2", PAD, BTN_TL2 },         { "r2", PAD, BTN_TR2 },
	{ "select", PAD, BTN_SELECT },  { "start", PAD, BTN_START },
	{ "menu", PAD, BTN_TRIGGER_HAPPY1 },
	{ "volup", VOL, KEY_VOLUMEUP }, { "voldown", VOL, KEY_VOLUMEDOWN },
	{ "power", PWR, KEY_POWER },
};

/* A keyboard's key, and the button it is */
static const struct { int key; const char *button; } keymap[] = {
	{ KEY_UP, "up" },          { KEY_DOWN, "down" },
	{ KEY_LEFT, "left" },      { KEY_RIGHT, "right" },
	{ KEY_X, "a" },            { KEY_Z, "b" },
	{ KEY_S, "x" },            { KEY_A, "y" },
	{ KEY_Q, "l1" },           { KEY_W, "r1" },
	{ KEY_E, "l2" },           { KEY_R, "r2" },
	{ KEY_ENTER, "start" },    { KEY_KPENTER, "start" },
	{ KEY_RIGHTSHIFT, "select" }, { KEY_BACKSPACE, "select" },
	{ KEY_ESC, "menu" },       { KEY_M, "menu" },
	{ KEY_EQUAL, "volup" },    { KEY_KPPLUS, "volup" },    { KEY_VOLUMEUP, "volup" },
	{ KEY_MINUS, "voldown" },  { KEY_KPMINUS, "voldown" }, { KEY_VOLUMEDOWN, "voldown" },
	{ KEY_P, "power" },        { KEY_POWER, "power" },
};

#define MAX_KBD     8
#define MAX_CLIENTS 8
#define MAX_EFFECTS 16

static int out[NDEV];
static int kbd[MAX_KBD];
static int clients[MAX_CLIENTS];
static int port = -1, host_up;
static char port_buf[512];
static size_t port_len;
static int state_fd = -1;
static char hw_state[128] = "leds=0 backlight=0 bl_power=0 battery=100 cable=1 kind=1";
static int headphones;
static struct ff_effect effects[MAX_EFFECTS];

static void say(const char *text)
{
	int fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);

	if (fd >= 0) {
		if (write(fd, text, strlen(text)) < 0) { /* nowhere else to say it */ }
		close(fd);
	}
}

static void sleep_ms(int ms)
{
	struct timespec t = { ms / 1000, (ms % 1000) * 1000000L };

	nanosleep(&t, NULL);
}

static void run(const char *prog, const char *arg)
{
	pid_t pid = fork();

	if (pid == 0) {
		execl(prog, prog, arg, (char *)NULL);
		_exit(127);
	}
	if (pid > 0) waitpid(pid, NULL, 0);
}

static int make(int which, int type, const int *codes, size_t n)
{
	struct uinput_setup setup;
	int fd = open("/dev/uinput", O_RDWR | O_NONBLOCK | O_CLOEXEC);
	size_t i;

	if (fd < 0) return -1;
	ioctl(fd, UI_SET_EVBIT, type);
	for (i = 0; i < n; i++)
		ioctl(fd, type == EV_SW ? UI_SET_SWBIT : UI_SET_KEYBIT, codes[i]);
	memset(&setup, 0, sizeof setup);
	if (which == PAD) {
		ioctl(fd, UI_SET_EVBIT, EV_FF);
		ioctl(fd, UI_SET_FFBIT, FF_RUMBLE);
		setup.ff_effects_max = MAX_EFFECTS;
	}
	ioctl(fd, UI_SET_PHYS, devs[which].phys);
	setup.id = devs[which].id;
	snprintf(setup.name, sizeof setup.name, "%s", devs[which].name);
	if (ioctl(fd, UI_DEV_SETUP, &setup) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static void emit(int which, int type, int code, int value)
{
	struct input_event ev[2];

	memset(ev, 0, sizeof ev);
	ev[0].type = (unsigned short)type;
	ev[0].code = (unsigned short)code;
	ev[0].value = value;
	ev[1].type = EV_SYN;
	ev[1].code = SYN_REPORT;
	if (write(out[which], ev, sizeof ev) < 0) { /* the device went: nothing to do */ }
}

static void to_host(const char *line)
{
	if (port < 0 || !host_up) return;
	/* Dropped rather than waited for when the window doesn't read */
	if (write(port, line, strlen(line)) < 0) { /* next change sends the state again */ }
}

static void send_state(void)
{
	char line[192];

	snprintf(line, sizeof line, "state %s headphones=%d\n", hw_state, headphones);
	to_host(line);
}

/* ---- commands ------------------------------------------------------------- */

static int button(const char *name)
{
	size_t i;

	for (i = 0; i < sizeof buttons / sizeof buttons[0]; i++)
		if (!strcmp(name, buttons[i].name)) return (int)i;
	return -1;
}

/* BUTTON[+BUTTON...] down or up together; false for a name it doesn't know */
static int press(const char *chord, int value)
{
	char copy[128], *save = NULL, *name;
	int which[8], n = 0, i;

	snprintf(copy, sizeof copy, "%s", chord);
	for (name = strtok_r(copy, "+", &save); name && n < 8; name = strtok_r(NULL, "+", &save))
		if ((which[n++] = button(name)) < 0) return 0;
	for (i = 0; i < n; i++)
		emit(buttons[which[i]].dev, EV_KEY, buttons[which[i]].code, value);
	return n > 0;
}

static int set_param(const char *name, const char *value)
{
	char path[96];
	int fd, ok;

	snprintf(path, sizeof path, PARAMS "%s", name);
	if ((fd = open(path, O_WRONLY | O_CLOEXEC)) < 0) return 0;
	ok = write(fd, value, strlen(value)) >= 0;
	close(fd);
	return ok;
}

static int digits(const char *s)
{
	if (!s || !*s) return 0;
	for (; *s; s++)
		if (*s < '0' || *s > '9') return 0;
	return 1;
}

/* One command line; the answer goes in reply */
static void command(char *line, char *reply, size_t cap)
{
	char *save = NULL, *cmd = strtok_r(line, " \t\r\n", &save);
	char *a = strtok_r(NULL, " \t\r\n", &save), *b = strtok_r(NULL, " \t\r\n", &save);

	snprintf(reply, cap, "ok\n");
	if (!cmd) {
		snprintf(reply, cap, "error: no command\n");
	} else if (!strcmp(cmd, "hello") || !strcmp(cmd, "state")) {
		snprintf(reply, cap, "state %s headphones=%d\n", hw_state, headphones);
	} else if ((!strcmp(cmd, "press") || !strcmp(cmd, "release")) && a) {
		if (!press(a, cmd[0] == 'p')) snprintf(reply, cap, "error: no button %s\n", a);
	} else if (!strcmp(cmd, "tap") && a) {
		int ms = digits(b) ? atoi(b) : 80;

		if (ms > 5000) ms = 5000;
		if (!press(a, 1)) {
			snprintf(reply, cap, "error: no button %s\n", a);
		} else {
			sleep_ms(ms);
			press(a, 0);
		}
	} else if (!strcmp(cmd, "battery") && digits(a) && atoi(a) <= 100) {
		if (!set_param("capacity", a)) snprintf(reply, cap, "error: no px2sim_hw module\n");
	} else if (!strcmp(cmd, "cable") && a && (!strcmp(a, "0") || !strcmp(a, "1"))) {
		if (!set_param("cable", a)) snprintf(reply, cap, "error: no px2sim_hw module\n");
	} else if (!strcmp(cmd, "kind") && a && (!strcmp(a, "wall") || !strcmp(a, "computer"))) {
		if (!set_param("kind", a[0] == 'w' ? "1" : "2"))
			snprintf(reply, cap, "error: no px2sim_hw module\n");
	} else if (!strcmp(cmd, "current") && digits(a) && atoi(a) <= 3000) {
		if (!set_param("current_ma", a)) snprintf(reply, cap, "error: no px2sim_hw module\n");
	} else if (!strcmp(cmd, "headphones") && a && (!strcmp(a, "0") || !strcmp(a, "1"))) {
		headphones = a[0] == '1';
		emit(JACK, EV_SW, SW_HEADPHONE_INSERT, headphones);
		send_state();
	} else {
		snprintf(reply, cap, "error: unknown command\n");
	}
}

/* ---- the codec's volume ------------------------------------------------- */

/* "Master Playback Volume" on card 0, as the RK817 has it: two channels,
 * 0-255, a dB scale of 0.375 dB a step up to 0 dB, which is also the shape
 * alsa-lib's softvol keeps rather than replaces. Made before anything plays,
 * and left unlocked, so ZyperOS's and Diatom's writes land in it; at full,
 * until ZyperOS sets the player's level. */
static void make_volume(void)
{
	struct snd_ctl_elem_info info;
	struct snd_ctl_elem_value value;
	unsigned int tlv[6];
	int fd = open("/dev/snd/controlC0", O_RDWR | O_CLOEXEC);

	if (fd < 0) return;
	memset(&info, 0, sizeof info);
	info.id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	snprintf((char *)info.id.name, sizeof info.id.name, "Master Playback Volume");
	if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_INFO, &info) == 0) {   /* there already */
		close(fd);
		return;
	}
	info.type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	info.access = SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_TLV_READWRITE;
	info.count = 2;
	info.owner = 1;
	info.value.integer.max = 255;
	if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_ADD, &info) < 0) {
		say("px2sim: couldn't add the sound card's Master Playback Volume\n");
		close(fd);
		return;
	}
	tlv[0] = info.id.numid;
	tlv[1] = 4 * sizeof tlv[0];
	tlv[2] = SNDRV_CTL_TLVT_DB_SCALE;
	tlv[3] = 2 * sizeof tlv[0];
	tlv[4] = (unsigned int)-9562;   /* -95.625 dB, as alsa-lib rounds it */
	tlv[5] = 37;                    /* 0.375 dB */
	ioctl(fd, SNDRV_CTL_IOCTL_TLV_WRITE, tlv);
	memset(&value, 0, sizeof value);
	value.id = info.id;
	value.value.integer.value[0] = value.value.integer.value[1] = 255;
	ioctl(fd, SNDRV_CTL_IOCTL_ELEM_WRITE, &value);
	ioctl(fd, SNDRV_CTL_IOCTL_ELEM_UNLOCK, &info.id);
	close(fd);
}

/* ---- the keyboard ------------------------------------------------------- */

/* A keyboard: one with letters, which none of the four has, that QEMU
 * gives the machine (on the virtio or the USB bus; see kbdsel.h). Not a
 * keyboard a program makes with uinput: PortMaster's gptokeyb makes one for
 * every port, the pad's presses as keys, and taken here they went to the
 * buttons and never to the port, and its Enter was START again (a loop,
 * found 2026-11-08: no port in the VM answered a key). A uinput device is
 * told by its place in sysfs, which it can't choose, not by its name, bus or
 * ids, which it can. Run again for every /dev/input change, so a keyboard
 * plugged in later (QEMU's device_add) is taken too. */
static void take(const char *path)
{
	unsigned long keys[KEY_MAX / (8 * sizeof(long)) + 1];
	char link[64], real[PATH_MAX];
	int fd, i, slot = -1;

	for (i = 0; i < MAX_KBD; i++)
		if (kbd[i] < 0) { slot = i; break; }
	if (slot < 0) return;
	snprintf(link, sizeof link, "/sys/class/input/%s", strrchr(path, '/') + 1);
	if (!realpath(link, real)) return;
	if ((fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC)) < 0) return;
	memset(keys, 0, sizeof keys);
	if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys) < 0 ||
	    !kbdsel_is_qemu_keyboard(real, (keys[KEY_A / (8 * sizeof(long))] >> (KEY_A % (8 * sizeof(long)))) & 1) ||
	    ioctl(fd, EVIOCGRAB, 1) < 0) {
		close(fd);
		return;
	}
	kbd[slot] = fd;
	say("px2sim: a keyboard is the Pixel 2's buttons now\n");
}

static void scan(void)
{
	char path[32];
	int i;

	for (i = 0; i < 32; i++) {
		snprintf(path, sizeof path, "/dev/input/event%d", i);
		if (access(path, R_OK) == 0) take(path);
	}
}

static void key(const struct input_event *ev)
{
	size_t i;

	if (ev->type != EV_KEY || ev->value == 2) return;   /* no repeats: the pad has none */
	if (ev->code == KEY_H) {
		if (ev->value == 1) {
			headphones = !headphones;
			emit(JACK, EV_SW, SW_HEADPHONE_INSERT, headphones);
			send_state();
		}
		return;
	}
	for (i = 0; i < sizeof keymap / sizeof keymap[0]; i++)
		if (keymap[i].key == ev->code) {
			press(keymap[i].button, ev->value);
			return;
		}
}

/* ---- rumble: the joypad's force feedback, answered here ------------------- */

static void pad_event(void)
{
	struct input_event ev;

	while (read(out[PAD], &ev, sizeof ev) == (ssize_t)sizeof ev) {
		if (ev.type == EV_UINPUT && ev.code == UI_FF_UPLOAD) {
			struct uinput_ff_upload up;

			memset(&up, 0, sizeof up);
			up.request_id = (unsigned)ev.value;
			if (ioctl(out[PAD], UI_BEGIN_FF_UPLOAD, &up) < 0) continue;
			if (up.effect.id >= 0 && up.effect.id < MAX_EFFECTS)
				effects[up.effect.id] = up.effect;
			up.retval = 0;
			ioctl(out[PAD], UI_END_FF_UPLOAD, &up);
		} else if (ev.type == EV_UINPUT && ev.code == UI_FF_ERASE) {
			struct uinput_ff_erase er;

			memset(&er, 0, sizeof er);
			er.request_id = (unsigned)ev.value;
			if (ioctl(out[PAD], UI_BEGIN_FF_ERASE, &er) < 0) continue;
			er.retval = 0;
			ioctl(out[PAD], UI_END_FF_ERASE, &er);
		} else if (ev.type == EV_FF && ev.code < MAX_EFFECTS) {
			const struct ff_effect *e = &effects[ev.code];
			char line[64];

			if (ev.value && e->type == FF_RUMBLE)
				snprintf(line, sizeof line, "rumble %u %u %u\n", e->u.rumble.strong_magnitude,
				         e->u.rumble.weak_magnitude, e->replay.length);
			else
				snprintf(line, sizeof line, "rumble 0 0 0\n");
			to_host(line);
		}
	}
}

/* ---- the link to the window ------------------------------------------------- */

static void open_port(void)
{
	DIR *d = opendir("/sys/class/virtio-ports");
	struct dirent *e;

	while (d && (e = readdir(d))) {
		char path[300], name[64] = "";
		FILE *f;

		if (e->d_name[0] == '.') continue;
		snprintf(path, sizeof path, "/sys/class/virtio-ports/%s/name", e->d_name);
		if (!(f = fopen(path, "r"))) continue;
		if (!fgets(name, sizeof name, f)) name[0] = '\0';
		fclose(f);
		name[strcspn(name, "\n")] = '\0';
		if (strcmp(name, PORTNAME)) continue;
		snprintf(path, sizeof path, "/dev/%s", e->d_name);
		port = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
		break;
	}
	if (d) closedir(d);
}

static void port_lines(void)
{
	ssize_t n;
	char *nl, reply[192];

	while ((n = read(port, port_buf + port_len, sizeof port_buf - 1 - port_len)) > 0) {
		port_len += (size_t)n;
		port_buf[port_len] = '\0';
		while ((nl = strchr(port_buf, '\n'))) {
			*nl = '\0';
			command(port_buf, reply, sizeof reply);
			/* The window hears the state, and errors; an ok is the state's
			 * change, which comes by itself */
			if (strncmp(reply, "ok", 2)) to_host(reply);
			memmove(port_buf, nl + 1, port_len - (size_t)(nl + 1 - port_buf) + 1);
			port_len -= (size_t)(nl + 1 - port_buf);
		}
		if (port_len == sizeof port_buf - 1) port_len = 0;   /* a line too long */
	}
}

static void client(int fd)
{
	/* A second for the command, so a client that says nothing can't hold
	 * the buttons up */
	struct timeval wait = { 1, 0 };
	char line[256], reply[192];
	ssize_t n;

	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &wait, sizeof wait);
	n = read(fd, line, sizeof line - 1);

	if (n > 0) {
		line[n] = '\0';
		command(line, reply, sizeof reply);
		if (write(fd, reply, strlen(reply)) < 0) { /* gone */ }
	}
	close(fd);
}

/* ---- the daemon ----------------------------------------------------------- */

static int daemon_main(void)
{
	struct sockaddr_un addr = { .sun_family = AF_UNIX };
	struct pollfd p[5 + MAX_KBD + MAX_CLIENTS];
	int ino, listener, i, n;
	pid_t pid;

	run("/sbin/modprobe", "px2sim_hw");
	make_volume();
	out[PAD]  = make(PAD, EV_KEY, pad_keys, sizeof pad_keys / sizeof pad_keys[0]);
	out[VOL]  = make(VOL, EV_KEY, vol_keys, sizeof vol_keys / sizeof vol_keys[0]);
	out[PWR]  = make(PWR, EV_KEY, pwr_keys, 1);
	out[JACK] = make(JACK, EV_SW, jack_sw, 1);
	for (i = 0; i < NDEV; i++)
		if (out[i] < 0) {
			say("px2sim: couldn't make the Pixel 2's input devices (no /dev/uinput?)\n");
			return 1;
		}

	/* The devices are there: init carries on, and the keyboard's driver
	 * comes after them */
	if ((pid = fork()) < 0) return 1;
	if (pid > 0) return 0;
	setsid();
	signal(SIGPIPE, SIG_IGN);
	run("/sbin/modprobe", "virtio_input");

	for (i = 0; i < MAX_KBD; i++) kbd[i] = -1;
	for (i = 0; i < MAX_CLIENTS; i++) clients[i] = -1;
	ino = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (ino >= 0) inotify_add_watch(ino, "/dev/input", IN_CREATE | IN_ATTRIB);
	scan();
	state_fd = open("/dev/px2sim", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	open_port();
	unlink(SOCKET);
	listener = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
	snprintf(addr.sun_path, sizeof addr.sun_path, "%s", SOCKET);
	if (listener >= 0 && (bind(listener, (struct sockaddr *)&addr, sizeof addr) < 0 ||
	                      listen(listener, 4) < 0)) {
		close(listener);
		listener = -1;
	}
	say("px2sim: the Pixel 2's buttons and parts are simulated\n");

	for (;;) {
		int count = 0, kind[5 + MAX_KBD], index[5 + MAX_KBD];

		/* The window's end: there while QEMU's socket has someone on it.
		 * A port with nobody there reads as hung up, always, so it is
		 * looked at twice a second rather than polled. */
		if (port >= 0) {
			struct pollfd q = { .fd = port, .events = POLLIN };
			int was = host_up;

			host_up = poll(&q, 1, 0) >= 0 && !(q.revents & POLLHUP);
			if (host_up && !was) send_state();
		}
#define WATCH(fd_, k, i_) do { p[count].fd = (fd_); p[count].events = POLLIN; \
		kind[count] = (k); index[count++] = (i_); } while (0)
		if (ino >= 0) WATCH(ino, 0, 0);
		WATCH(out[PAD], 1, 0);
		if (state_fd >= 0) WATCH(state_fd, 2, 0);
		if (port >= 0 && host_up) WATCH(port, 3, 0);
		if (listener >= 0) WATCH(listener, 4, 0);
		for (i = 0; i < MAX_KBD; i++)
			if (kbd[i] >= 0) WATCH(kbd[i], 5, i);
		if (poll(p, (nfds_t)count, port >= 0 && !host_up ? 500 : -1) < 0) {
			if (errno == EINTR) continue;
			return 1;
		}
		for (i = 0; i < count; i++) {
			if (!(p[i].revents & (POLLIN | POLLERR | POLLHUP))) continue;
			switch (kind[i]) {
			case 0: {
				char buf[4096];

				while (read(ino, buf, sizeof buf) > 0) { }
				scan();
				break;
			}
			case 1:
				pad_event();
				break;
			case 2: {
				char buf[128];

				n = (int)read(state_fd, buf, sizeof buf - 1);
				if (n > 0) {
					buf[n] = '\0';
					buf[strcspn(buf, "\n")] = '\0';
					snprintf(hw_state, sizeof hw_state, "%s", buf);
					send_state();
				}
				break;
			}
			case 3:
				port_lines();
				break;
			case 4: {
				int fd = accept4(listener, NULL, NULL, SOCK_CLOEXEC);

				if (fd >= 0) client(fd);
				break;
			}
			case 5: {
				struct input_event ev[32];
				int k = index[i], e;

				while ((n = (int)read(kbd[k], ev, sizeof ev)) > 0)
					for (e = 0; e < n / (int)sizeof ev[0]; e++) key(&ev[e]);
				if (n < 0 && errno != EAGAIN) {   /* unplugged */
					close(kbd[k]);
					kbd[k] = -1;
				}
				break;
			}
			}
		}
	}
}

static int client_main(int argc, char **argv)
{
	struct sockaddr_un addr = { .sun_family = AF_UNIX };
	char line[256] = "", reply[256];
	int fd, i;
	ssize_t n;

	for (i = 1; i < argc; i++) {
		strncat(line, argv[i], sizeof line - strlen(line) - 2);
		strncat(line, i + 1 < argc ? " " : "\n", sizeof line - strlen(line) - 1);
	}
	snprintf(addr.sun_path, sizeof addr.sun_path, "%s", SOCKET);
	if ((fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0)) < 0 ||
	    connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
		fprintf(stderr, "px2sim: the daemon isn't running\n");
		return 1;
	}
	if (write(fd, line, strlen(line)) < 0) return 1;
	n = read(fd, reply, sizeof reply - 1);
	close(fd);
	if (n <= 0) return 1;
	reply[n] = '\0';
	fputs(reply, strncmp(reply, "error", 5) ? stdout : stderr);
	return strncmp(reply, "error", 5) ? 0 : 1;
}

int main(int argc, char **argv)
{
	if (argc == 2 && !strcmp(argv[1], "--daemon")) return daemon_main();
	if (argc < 2) {
		fprintf(stderr, "usage: px2sim press|release|tap BUTTON[+BUTTON] [MS]\n"
		                "       px2sim battery PERCENT | cable 0|1 | kind wall|computer | current MA |\n"
		                "              headphones 0|1 | state\n");
		return 2;
	}
	return client_main(argc, argv);
}
