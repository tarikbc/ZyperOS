/* SPDX-License-Identifier: GPL-3.0-only */
/* See cablefresh.h. */
#include "cablefresh.h"

#include "hwput.h"
#include "klog.h"
#include "linksteps.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* The check's stand-in for the computer's answer (cable_fresh_test_probe) */
static bool (*g_test_answer)(void);

void cable_fresh_test_probe(bool (*answered)(void)) { g_test_answer = answered; }

static bool read_word(const char *path, char *buf, size_t n)
{
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	ssize_t got;

	buf[0] = '\0';
	if (fd < 0) return false;
	got = read(fd, buf, n - 1);
	close(fd);
	buf[got > 0 ? got : 0] = '\0';
	return got > 0;
}

/* What the child needs to ask, found before the fork (if_nametoindex and
 * the ioctls are no business of a child of a threaded process) */
typedef struct {
	int           ifindex;
	unsigned char mac[6];
	unsigned      sip, tip;
	bool          state_tells, configured, carrier;
} probe_arg;

/* In the child, after the asking: whether the link is made fresh, said */
static bool fresh_after(const probe_arg *a, bool answered)
{
	if (!link_wants_fresh(false, a->state_tells, a->configured, a->carrier, answered)) {
		klog_raw("usb: the computer answers over the cable: the link left as it is");
		return false;
	}
	klog_raw("usb: the computer did not answer over the cable: a fresh link");
	return true;
}

static bool test_needs_fresh(void *arg)
{
	return fresh_after(arg, g_test_answer());
}

#ifdef __linux__
#include <arpa/inet.h>
#include <errno.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <time.h>

static unsigned probe_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (unsigned)(ts.tv_sec * 1000u + (unsigned)(ts.tv_nsec / 1000000));
}

/* In the child: true when the link needs making fresh, that is when the
 * computer did not answer. Two requests 200 ms apart; a computer on a
 * working link answers in a few ms. Async-signal-safe calls only. */
static bool probe_needs_fresh(void *arg)
{
	const probe_arg *a = arg;
	struct sockaddr_ll to;
	unsigned char req[LINK_ARP_LEN], got[64];
	unsigned t0 = probe_ms();
	int fd = socket(AF_PACKET, SOCK_DGRAM | SOCK_CLOEXEC, htons(ETH_P_ARP)), sent = 0;
	bool answered = false;

	if (fd < 0) {
		klog_raw("usb: the computer could not be asked: a fresh link");
		return true;
	}
	memset(&to, 0, sizeof to);
	to.sll_family = AF_PACKET;
	to.sll_protocol = htons(ETH_P_ARP);
	to.sll_ifindex = a->ifindex;
	if (bind(fd, (struct sockaddr *)&to, sizeof to) < 0) {
		close(fd);
		klog_raw("usb: the computer could not be asked: a fresh link");
		return true;
	}
	to.sll_halen = 6;
	memset(to.sll_addr, 0xff, 6);
	link_arp_request(req, a->mac, a->sip, a->tip);
	while (!answered && probe_ms() - t0 < 400u) {
		struct pollfd p = { fd, POLLIN, 0 };
		unsigned since = probe_ms() - t0;

		if (sent < 2 && since >= (unsigned)sent * 200u) {
			if (sendto(fd, req, sizeof req, 0, (struct sockaddr *)&to, sizeof to) == (ssize_t)sizeof req)
				sent++;
			else
				sent = 2;
		}
		if (poll(&p, 1, 50) > 0) {
			ssize_t n = recv(fd, got, sizeof got, 0);

			if (n > 0 && link_arp_answer(got, (size_t)n, a->tip)) answered = true;
		}
	}
	close(fd);
	return fresh_after(a, answered);
}

static bool probe_fill(probe_arg *a, const char *ifname, const char *peer)
{
	struct ifreq ifr;
	struct in_addr tip;
	int fd;
	bool ok;

	memset(a, 0, sizeof *a);
	if (!(a->ifindex = (int)if_nametoindex(ifname)) || inet_pton(AF_INET, peer, &tip) != 1)
		return false;
	a->tip = tip.s_addr;
	if ((fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0)) < 0) return false;
	memset(&ifr, 0, sizeof ifr);
	snprintf(ifr.ifr_name, sizeof ifr.ifr_name, "%s", ifname);
	ok = ioctl(fd, SIOCGIFHWADDR, &ifr) == 0;
	if (ok) memcpy(a->mac, ifr.ifr_hwaddr.sa_data, 6);
	memset(&ifr, 0, sizeof ifr);
	snprintf(ifr.ifr_name, sizeof ifr.ifr_name, "%s", ifname);
	ifr.ifr_addr.sa_family = AF_INET;
	ok = ok && ioctl(fd, SIOCGIFADDR, &ifr) == 0;
	if (ok) a->sip = ((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr.s_addr;
	close(fd);
	return ok;
}
#else
static bool probe_needs_fresh(void *arg) { return fresh_after(arg, false); }
static bool probe_fill(probe_arg *a, const char *ifname, const char *peer)
{
	(void)a; (void)ifname; (void)peer;
	return false;                        /* no AF_PACKET here */
}
#endif

void cable_fresh(const cable_fresh_cfg *c)
{
	char path[300], state[64] = "", carrier[8] = "";
	bool configured, up;
	static probe_arg probe;              /* the child's copy is its own */
	hwput_step steps[3];
	hwput_job job;

	if (hwput_busy()) {
		fprintf(stderr, "usb: no fresh link: the controller is still in a write\n");
		return;
	}
	snprintf(path, sizeof path, "%s/state", c->udc);
	read_word(path, state, sizeof state);
	state[strcspn(state, "\n")] = '\0';
	snprintf(path, sizeof path, "%s/%s/carrier", c->net ? c->net : "/sys/class/net", c->ifname);
	read_word(path, carrier, sizeof carrier);
	configured = link_udc_configured(state);
	up = carrier[0] == '1';
	snprintf(path, sizeof path, "%s/soft_connect", c->udc);
	steps[0] = (hwput_step){ "usb: soft_connect disconnect", path, "disconnect", 300 };
	steps[1] = (hwput_step){ "usb: soft_connect connect", path, "connect", 0 };
	steps[2] = (hwput_step){ "wire: the cable connection made fresh", NULL, NULL, 0 };
	job = (hwput_job){ "soft_connect", NULL, NULL, steps, 3 };
	if (!link_fresh_ask(c->just_bound, c->state_tells, configured, up)) {
		if (!link_wants_fresh(c->just_bound, c->state_tells, configured, up, false)) {
			fprintf(stderr, "usb: the link was bound a moment ago: no fresh link\n");
			return;
		}
		fprintf(stderr, "usb: the link is not whole (%s, carrier %s): a fresh link\n",
		        state[0] ? state : "no state", up ? "on" : "off");
	} else if (g_test_answer) {
		memset(&probe, 0, sizeof probe);
		probe.state_tells = c->state_tells;
		probe.configured = configured;
		probe.carrier = up;
		job.need = test_needs_fresh;
		job.arg = &probe;
	} else if (!probe_fill(&probe, c->ifname, c->peer)) {
		fprintf(stderr, "usb: the link looks whole and %s has no address to ask from: a fresh link\n",
		        c->ifname);
	} else {
		probe.state_tells = c->state_tells;
		probe.configured = configured;
		probe.carrier = up;
		job.need = probe_needs_fresh;
		job.arg = &probe;
	}
	hwput_run(&job, false);
}
