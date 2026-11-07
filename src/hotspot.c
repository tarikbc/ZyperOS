/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* Wi-Fi File Transfer's hotspot. See hotspot.h for what runs, and captive.h
 * for why every name a phone looks up answers with the handheld. */
#include <stdio.h>
#include <string.h>

#include "captive.h"
#include "fileio.h"
#include "hotspot.h"
#include "monoclock.h"

static hotspot_state g_state = HOTSPOT_OFF;
static char g_ssid[40], g_pass[16], g_qr[160];
static int  g_phones = -1;

const char *hotspot_ssid(void)     { return g_ssid; }
const char *hotspot_password(void) { return g_pass; }
const char *hotspot_qr_text(void)  { return g_qr; }
const char *hotspot_addr(void)     { return CAPTIVE_ADDR; }
int         hotspot_phones(void)   { return g_phones; }

#ifdef __linux__

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "wifi.h"

#define HOSTAPD   "/usr/sbin/hostapd"
#define UDHCPD    "/usr/sbin/udhcpd"
#define WLAN      "wlan0"
#define DIR       "/tmp/zyperos-hotspot"
#define HOSTAPD_CONF DIR "/hostapd.conf"
#define UDHCPD_CONF  DIR "/udhcpd.conf"
#define CTRL_DIR  "/var/run/hostapd"     /* hostapd.conf's ctrl_interface */
#define CTRL_ME   DIR "/ctrl"            /* our end of its control socket */

/* hostapd brings the driver up in AP mode in about a second; a driver that
 * cannot is usually refused at once, but one that hangs gets this long. */
#define START_MS  15000

static pid_t    g_hostapd, g_udhcpd;
static int      g_dns = -1, g_ctrl = -1;
static bool     g_was_on;            /* the supplicant ran before us */
static unsigned g_t0, g_next_ask;

bool hotspot_available(void)
{
	return access("/sys/class/net/" WLAN, F_OK) == 0 && access(HOSTAPD, X_OK) == 0;
}

/* A program of the system's, as a child of this one, its output to `log`.
 * Every descriptor past the three is closed in the child: the launcher holds
 * the web server's listening socket, and hostapd keeping it would keep port
 * 80 taken after the screen closed, so the next transfer came up on 8080. */
static pid_t spawn(char *const argv[], const char *log)
{
	pid_t pid = fork();

	if (pid < 0) return 0;
	if (pid == 0) {
		int fd, null = open("/dev/null", O_RDONLY);
		int out = open(log, O_WRONLY | O_CREAT | O_TRUNC, 0600);

		if (null >= 0) dup2(null, 0);
		if (out >= 0) { dup2(out, 1); dup2(out, 2); }
		for (fd = 3; fd < 1024; fd++) close(fd);
		setsid();
		execv(argv[0], argv);
		_exit(127);
	}
	return pid;
}

static bool alive(pid_t pid)
{
	int st;

	return pid > 0 && waitpid(pid, &st, WNOHANG) == 0;
}

/* TERM, a moment to go cleanly - hostapd puts the interface back in station
 * mode on the way out - then KILL. */
static void reap(pid_t *pid, unsigned ms)
{
	unsigned t;

	if (*pid <= 0) return;
	kill(*pid, SIGTERM);
	for (t = 0; t < ms; t += 20) {
		if (waitpid(*pid, NULL, WNOHANG) != 0) { *pid = 0; return; }
		usleep(20000);
	}
	kill(*pid, SIGKILL);
	waitpid(*pid, NULL, 0);
	*pid = 0;
}

/* A hostapd or udhcpd left from a launcher that died with the screen open:
 * by the PID file this file wrote, and only if /proc says it is still that
 * program, since a PID can be reused. */
static void reap_stale(const char *pidfile, const char *comm)
{
	char buf[32] = "", path[64], name[32] = "";
	int fd = open(pidfile, O_RDONLY | O_CLOEXEC);
	pid_t pid;

	if (fd < 0) return;
	if (read(fd, buf, sizeof buf - 1) < 0) buf[0] = '\0';
	close(fd);
	unlink(pidfile);
	if ((pid = (pid_t)atoi(buf)) <= 0) return;
	snprintf(path, sizeof path, "/proc/%d/comm", (int)pid);
	if ((fd = open(path, O_RDONLY | O_CLOEXEC)) < 0) return;
	if (read(fd, name, sizeof name - 1) < 0) name[0] = '\0';
	close(fd);
	if (strncmp(name, comm, strlen(comm))) return;
	kill(pid, SIGKILL);
	fprintf(stderr, "hotspot: stopped a %s left running (%d)\n", comm, (int)pid);
}

static void write_pid(const char *pidfile, pid_t pid)
{
	FILE *f = fopen(pidfile, "we");

	if (!f) return;
	fprintf(f, "%d\n", (int)pid);
	fclose(f);
}

/* Readable by root only: the hostapd configuration holds the password. */
static bool write_file(const char *path, const char *text)
{
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	size_t n = strlen(text);
	bool ok;

	if (fd < 0) return false;
	ok = write(fd, text, n) == (ssize_t)n;
	close(fd);
	return ok;
}

/* The handheld's address on wlan0, and the interface up; "0.0.0.0" takes the
 * address away again. ioctls rather than busybox's ip: nothing to fork. */
static void set_addr(const char *addr, bool up)
{
	struct ifreq ifr;
	struct sockaddr_in *sin = (struct sockaddr_in *)&ifr.ifr_addr;
	int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);

	if (fd < 0) return;
	memset(&ifr, 0, sizeof ifr);
	snprintf(ifr.ifr_name, IFNAMSIZ, "%s", WLAN);
	sin->sin_family = AF_INET;
	inet_pton(AF_INET, addr, &sin->sin_addr);
	if (ioctl(fd, SIOCSIFADDR, &ifr) < 0 && up)
		fprintf(stderr, "hotspot: address %s: %s\n", addr, strerror(errno));
	if (up) {
		inet_pton(AF_INET, "255.255.255.0", &sin->sin_addr);
		ioctl(fd, SIOCSIFNETMASK, &ifr);
	}
	if (ioctl(fd, SIOCGIFFLAGS, &ifr) == 0) {
		if (up) ifr.ifr_flags |= IFF_UP;
		else    ifr.ifr_flags &= ~IFF_UP;
		ioctl(fd, SIOCSIFFLAGS, &ifr);
	}
	close(fd);
}

/* One question to hostapd's control socket and its answer, waiting at most
 * 30 ms: hostapd answers in well under one, and this runs in a frame. */
static int ctrl_ask(const char *cmd, char *out, size_t cap)
{
	struct pollfd p;
	ssize_t got;

	if (g_ctrl < 0) {
		struct sockaddr_un me = { .sun_family = AF_UNIX }, it = { .sun_family = AF_UNIX };

		snprintf(me.sun_path, sizeof me.sun_path, "%s", CTRL_ME);
		snprintf(it.sun_path, sizeof it.sun_path, "%s/%s", CTRL_DIR, WLAN);
		g_ctrl = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
		if (g_ctrl < 0) return -1;
		unlink(CTRL_ME);
		if (bind(g_ctrl, (struct sockaddr *)&me, sizeof me) < 0 ||
		    connect(g_ctrl, (struct sockaddr *)&it, sizeof it) < 0) {
			close(g_ctrl);
			g_ctrl = -1;
			unlink(CTRL_ME);
			return -1;              /* not there yet: hostapd is starting */
		}
	}
	/* An answer to a question that timed out last time would be read as
	 * this one's: drop whatever is waiting first. */
	while (recv(g_ctrl, out, cap - 1, MSG_DONTWAIT) > 0) { }
	if (send(g_ctrl, cmd, strlen(cmd), 0) < 0) return -1;
	p.fd = g_ctrl;
	p.events = POLLIN;
	if (poll(&p, 1, 30) <= 0) return -1;
	got = recv(g_ctrl, out, cap - 1, MSG_DONTWAIT);
	if (got <= 0) return -1;
	out[got] = '\0';
	return (int)got;
}

static void ctrl_close(void)
{
	if (g_ctrl >= 0) close(g_ctrl);
	g_ctrl = -1;
	unlink(CTRL_ME);
}

/* The last line hostapd wrote, which on a failure is what failed. */
static void say_why(const char *what)
{
	char log[2048], *end, *line;

	file_head(DIR "/hostapd.log", log, sizeof log);
	end = log + strlen(log);
	while (end > log && (end[-1] == '\n' || end[-1] == ' ')) *--end = '\0';
	line = strrchr(log, '\n');
	fprintf(stderr, "hotspot: %s%s%s\n", what, log[0] ? ": " : "",
	        line ? line + 1 : log);
}

static void fail(const char *what)
{
	say_why(what);
	g_state = HOTSPOT_FAILED;
}

/* The access point is up: the handheld's address on it, the DNS socket on
 * that address, and the DHCP server that tells each phone about both. */
static void bring_up(void)
{
	struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(53) };
	char *dhcp[] = { (char *)UDHCPD, (char *)"-f", (char *)UDHCPD_CONF, NULL };
	int one = 1;

	set_addr(CAPTIVE_ADDR, true);
	inet_pton(AF_INET, CAPTIVE_ADDR, &a.sin_addr);
	g_dns = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	if (g_dns >= 0) {
		setsockopt(g_dns, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
		if (bind(g_dns, (struct sockaddr *)&a, sizeof a) < 0) {
			fprintf(stderr, "hotspot: DNS on port 53: %s\n", strerror(errno));
			close(g_dns);
			g_dns = -1;
		}
	}
	g_udhcpd = spawn(dhcp, DIR "/udhcpd.log");
	if (g_udhcpd > 0) write_pid(DIR "/udhcpd.pid", g_udhcpd);
	g_state = HOTSPOT_UP;
	fprintf(stderr, "hotspot: up as %s at %s after %u ms (%ld)\n", g_ssid,
	        CAPTIVE_ADDR, now_ms32() - g_t0, (long)time(NULL));
}

/* Every question waiting, at most a handful a frame. */
static void dns_answer(void)
{
	static const uint8_t ip[4] = { 10, 42, 1, 1 };
	uint8_t q[512], r[512];
	int k;

	if (g_dns < 0) return;
	for (k = 0; k < 16; k++) {
		struct sockaddr_in from;
		socklen_t fl = sizeof from;
		ssize_t n = recvfrom(g_dns, q, sizeof q, MSG_DONTWAIT,
		                     (struct sockaddr *)&from, &fl);
		size_t len;

		if (n <= 0) break;
		len = captive_dns_answer(q, (size_t)n, ip, r, sizeof r);
		if (len) sendto(g_dns, r, len, MSG_DONTWAIT, (struct sockaddr *)&from, fl);
	}
}

/* "num_sta[0]=2" out of hostapd's STATUS, and whether it says ENABLED. */
static bool ask_status(void)
{
	char out[2048], *p;

	if (ctrl_ask("STATUS", out, sizeof out) <= 0) return false;
	if ((p = strstr(out, "num_sta[0]="))) g_phones = atoi(p + 11);
	return strstr(out, "state=ENABLED") != NULL;
}

bool hotspot_start(void)
{
	uint8_t rnd[CAPTIVE_PASS_RANDOM];
	char mac[32], conf[1024];
	/* -t: each line of its log with the time, so a phone's join can be laid
	 * beside the moment the code went up */
	char *ap[] = { (char *)HOSTAPD, (char *)"-t", (char *)HOSTAPD_CONF, NULL };
	int fd;
	ssize_t got = 0;

	if (g_state != HOTSPOT_OFF) hotspot_stop();
	if (!hotspot_available()) return false;
	mkdir(DIR, 0700);
	reap_stale(DIR "/hostapd.pid", "hostapd");
	reap_stale(DIR "/udhcpd.pid", "udhcpd");

	/* The supplicant owns the adapter while it runs; it goes for the
	 * screen's time and comes back after (hotspot_stop). */
	g_was_on = wifi_status(NULL, 0, NULL, 0) != WIFI_OFF;
	if (g_was_on) wifi_down();

	/* A new password every time the screen opens, from the kernel's
	 * generator, which on a handheld that has just booted may still be
	 * gathering: urandom does not wait, and neither should this. */
	if (getrandom(rnd, sizeof rnd, GRND_NONBLOCK) != (ssize_t)sizeof rnd) {
		fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
		if (fd >= 0) { got = read(fd, rnd, sizeof rnd); close(fd); }
		if (got != (ssize_t)sizeof rnd) {
			fprintf(stderr, "hotspot: no randomness for a password\n");
			hotspot_stop();
			return false;
		}
	}
	file_head("/sys/class/net/" WLAN "/address", mac, sizeof mac);
	captive_ssid(g_ssid, sizeof g_ssid, mac);
	captive_password(g_pass, sizeof g_pass, rnd);
	memset(rnd, 0, sizeof rnd);
	captive_wifi_qr(g_qr, sizeof g_qr, g_ssid, g_pass);

	/* One fixed channel, 6, the middle of the three that never overlap: a
	 * channel survey would cost seconds the player is watching, for a link
	 * that carries files across a room. No country: the world domain
	 * allows 6 everywhere. */
	snprintf(conf, sizeof conf,
	         "interface=" WLAN "\n"
	         "driver=nl80211\n"
	         "ctrl_interface=" CTRL_DIR "\n"
	         "ssid=%s\n"
	         "utf8_ssid=1\n"
	         "hw_mode=g\n"
	         "channel=6\n"
	         "ieee80211n=1\n"
	         "wmm_enabled=1\n"
	         "auth_algs=1\n"
	         "wpa=2\n"
	         "wpa_key_mgmt=WPA-PSK\n"
	         "rsn_pairwise=CCMP\n"
	         "wpa_passphrase=%s\n"
	         "max_num_sta=8\n", g_ssid, g_pass);
	if (!write_file(HOSTAPD_CONF, conf)) {
		fprintf(stderr, "hotspot: could not write %s\n", HOSTAPD_CONF);
		hotspot_stop();
		return false;
	}
	memset(conf, 0, sizeof conf);
	/* The handheld as each phone's router as well as its DNS: a network
	 * with no router is one a phone may not bother checking, and checking
	 * is what opens the sign-in sheet. Nothing is routed anywhere. */
	write_file(UDHCPD_CONF,
	           "interface " WLAN "\n"
	           "start " CAPTIVE_POOL_LO "\n"
	           "end " CAPTIVE_POOL_HI "\n"
	           "max_leases 51\n"
	           "option subnet 255.255.255.0\n"
	           "option router " CAPTIVE_ADDR "\n"
	           "option dns " CAPTIVE_ADDR "\n"
	           "option lease 3600\n"
	           "lease_file " DIR "/udhcpd.leases\n");

	g_phones = -1;
	g_hostapd = spawn(ap, DIR "/hostapd.log");
	if (g_hostapd <= 0) {
		fprintf(stderr, "hotspot: could not start hostapd\n");
		hotspot_stop();
		return false;
	}
	write_pid(DIR "/hostapd.pid", g_hostapd);
	g_t0 = now_ms32();
	g_next_ask = g_t0 + 200;
	g_state = HOTSPOT_STARTING;
	fprintf(stderr, "hotspot: starting %s\n", g_ssid);
	return true;
}

hotspot_state hotspot_poll(void)
{
	unsigned now = now_ms32();

	switch (g_state) {
	case HOTSPOT_STARTING:
		if (!alive(g_hostapd)) { g_hostapd = 0; fail("hostapd stopped"); break; }
		if ((int)(now - g_next_ask) >= 0) {
			g_next_ask = now + 250;
			if (ask_status()) { bring_up(); break; }
		}
		if (now - g_t0 > START_MS) fail("not up in time");
		break;
	case HOTSPOT_UP:
		dns_answer();
		if (!alive(g_hostapd)) { g_hostapd = 0; fail("hostapd stopped"); break; }
		if ((int)(now - g_next_ask) >= 0) {
			g_next_ask = now + 1000;
			ask_status();
			/* udhcpd gone is phones that cannot get an address: start it
			 * again rather than leave a network nobody can use. */
			if (g_udhcpd > 0 && !alive(g_udhcpd)) {
				char *dhcp[] = { (char *)UDHCPD, (char *)"-f", (char *)UDHCPD_CONF, NULL };

				fprintf(stderr, "hotspot: udhcpd stopped, starting it again\n");
				g_udhcpd = spawn(dhcp, DIR "/udhcpd.log");
			}
		}
		break;
	default:
		break;
	}
	return g_state;
}

void hotspot_stop(void)
{
	bool was = g_was_on;

	if (g_dns >= 0) close(g_dns);
	g_dns = -1;
	ctrl_close();
	reap(&g_udhcpd, 1000);
	reap(&g_hostapd, 2000);
	unlink(DIR "/udhcpd.pid");
	unlink(DIR "/hostapd.pid");
	unlink(HOSTAPD_CONF);
	if (g_state != HOTSPOT_OFF) set_addr("0.0.0.0", false);
	g_state = HOTSPOT_OFF;
	g_phones = -1;
	g_was_on = false;
	memset(g_pass, 0, sizeof g_pass);
	memset(g_qr, 0, sizeof g_qr);
	if (was) {
		fprintf(stderr, "hotspot: stopped, Wi-Fi back on\n");
		wifi_up();
	}
}

#else  /* the host build: no radio, no hotspot */

bool          hotspot_available(void) { return false; }
bool          hotspot_start(void)     { return false; }
hotspot_state hotspot_poll(void)      { return g_state; }
void          hotspot_stop(void)      { g_state = HOTSPOT_OFF; }

#endif
