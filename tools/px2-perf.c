/* SPDX-License-Identifier: GPL-3.0-only */
/* px2-perf: how whatever runs on the GKD Pixel 2 runs, in one report.
 *
 *   px2-perf [SECONDS] [--json]          (10 s by default)
 *
 * Over the seconds given:
 *
 *   panel     the frames a second the panel shows and the spread of the time
 *             between them (min, median, 95th percentile, max), from the new
 *             framebuffer seen at each vertical blank, as px2-shot --fps
 *             counts them: whatever draws, launcher, core or port
 *   game      the core's speed against the clock and the audio frames Diatom
 *             dropped, from Diatom's now file (diatom/src/now.h), which it
 *             writes four times a second while px2-perf asks; with an older
 *             Diatom, the last "x of the clock" line in the log instead
 *   cpu       each core's load and clock (the mean, its least and most) and
 *             governor
 *   devfreq   each devfreq device's clock (the GPU's, the memory's) and
 *             governor
 *   thermal   each thermal zone (the SoC's), its mean and most
 *   processes Diatom's and the launcher's CPU (100% is one core) and RSS, and
 *             the busiest others
 *   memory    available and free
 *
 * Plain text, or --json for a script. Ctrl-C ends it early, with the report
 * for the time it ran. Run as root on the device (the framebuffer's CRTC);
 * nothing it reads needs a library. In QEMU (docs/qemu.md) there are no
 * clocks or thermal zones, and the panel's numbers are QEMU's display.
 *
 * Build: the plastron toolchain, no libraries, -lm. */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>

#include "px2-perf.h"

#define NOW_FILE  "/tmp/diatom-now"
#define LOG_FILE  "/mnt/SDCARD/.userdata/pixel2/logs/zyperos.log"
#define MAX_CPUS  8
#define MAX_DEV   8
#define MAX_ZONES 8
#define MAX_PROCS 1024
#define TOP       3

static volatile sig_atomic_t g_stop;
static void on_signal(int sig) { (void)sig; g_stop = 1; }

static uint64_t now_us(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return (uint64_t)t.tv_sec * 1000000u + (uint64_t)t.tv_nsec / 1000u;
}

static void sleep_us(uint64_t us)
{
	struct timespec t = { (time_t)(us / 1000000u), (long)(us % 1000000u) * 1000L };

	nanosleep(&t, NULL);
}

/* A small file whole, NUL-ended; the length, or -1 */
static int slurp(const char *path, char *buf, size_t n)
{
	int fd = open(path, O_RDONLY | O_CLOEXEC), got = 0;
	ssize_t r;

	if (fd < 0) return -1;
	while ((size_t)got < n - 1 && (r = read(fd, buf + got, n - 1 - (size_t)got)) > 0) got += (int)r;
	close(fd);
	buf[got] = '\0';
	return got;
}

static long read_long(const char *path)
{
	char b[64];

	return slurp(path, b, sizeof b) > 0 ? strtol(b, NULL, 10) : -1;
}

static void read_word(const char *path, char *out, size_t n)
{
	if (slurp(path, out, n) <= 0) { snprintf(out, n, "?"); return; }
	out[strcspn(out, "\n")] = '\0';
}

/* --- the panel ----------------------------------------------------------- */

static int g_drm = -1;
static uint32_t g_crtc;

static int panel_open(void)
{
	for (int i = 0; i < 4; i++) {
		char path[32];
		struct drm_mode_card_res res;
		uint32_t crtcs[8];
		int fd;

		snprintf(path, sizeof path, "/dev/dri/card%d", i);
		if ((fd = open(path, O_RDWR | O_CLOEXEC)) < 0) continue;
		/* Never the master: a program starting meanwhile must be able to
		 * set its mode (px2-shot says why) */
		ioctl(fd, DRM_IOCTL_DROP_MASTER, 0);
		memset(&res, 0, sizeof res);
		if (ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) == 0) {
			if (res.count_crtcs > 8) res.count_crtcs = 8;
			res.crtc_id_ptr = (uint64_t)(uintptr_t)crtcs;
			res.count_fbs = res.count_connectors = res.count_encoders = 0;
			if (ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) == 0)
				for (uint32_t c = 0; c < res.count_crtcs; c++) {
					struct drm_mode_crtc crtc = { .crtc_id = crtcs[c] };

					if (ioctl(fd, DRM_IOCTL_MODE_GETCRTC, &crtc) == 0 && crtc.fb_id) {
						g_drm = fd;
						g_crtc = crtcs[c];
						return 1;
					}
				}
		}
		close(fd);
	}
	return 0;
}

/* --- the clocks, the zones, the load ------------------------------------ */

struct range { double sum, min, max; int n; };

static void range_add(struct range *r, double v)
{
	if (v < 0) return;
	if (!r->n || v < r->min) r->min = v;
	if (!r->n || v > r->max) r->max = v;
	r->sum += v;
	r->n++;
}

static double range_mean(const struct range *r) { return r->n ? r->sum / r->n : 0.0; }

static int g_ncpu;
static struct range g_cpu_mhz[MAX_CPUS];
static char g_cpu_gov[MAX_CPUS][32];
static int g_cpu_online[MAX_CPUS];

static int g_ndev;
static char g_dev_name[MAX_DEV][64], g_dev_gov[MAX_DEV][32];
static struct range g_dev_mhz[MAX_DEV];

static int g_nzone;
static char g_zone_type[MAX_ZONES][32];
static struct range g_zone_c[MAX_ZONES];

static void find_sensors(void)
{
	DIR *d;
	struct dirent *e;
	char p[256];

	for (g_ncpu = 0; g_ncpu < MAX_CPUS; g_ncpu++) {
		long on;

		snprintf(p, sizeof p, "/sys/devices/system/cpu/cpu%d", g_ncpu);
		if (access(p, F_OK)) break;
		snprintf(p, sizeof p, "/sys/devices/system/cpu/cpu%d/online", g_ncpu);
		on = read_long(p);
		g_cpu_online[g_ncpu] = on != 0;   /* cpu0 has no "online": always */
	}
	if ((d = opendir("/sys/class/devfreq"))) {
		while ((e = readdir(d)) && g_ndev < MAX_DEV) {
			if (e->d_name[0] == '.') continue;
			snprintf(g_dev_name[g_ndev], sizeof g_dev_name[0], "%.63s", e->d_name);
			g_ndev++;
		}
		closedir(d);
	}
	for (g_nzone = 0; g_nzone < MAX_ZONES; g_nzone++) {
		snprintf(p, sizeof p, "/sys/class/thermal/thermal_zone%d/type", g_nzone);
		if (access(p, F_OK)) break;
		read_word(p, g_zone_type[g_nzone], sizeof g_zone_type[0]);
	}
}

static void sample_sensors(void)
{
	char p[256];

	for (int i = 0; i < g_ncpu; i++) {
		long khz;

		snprintf(p, sizeof p, "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", i);
		if ((khz = read_long(p)) > 0) range_add(&g_cpu_mhz[i], khz / 1000.0);
		snprintf(p, sizeof p, "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_governor", i);
		read_word(p, g_cpu_gov[i], sizeof g_cpu_gov[0]);
	}
	for (int i = 0; i < g_ndev; i++) {
		long hz;

		snprintf(p, sizeof p, "/sys/class/devfreq/%.63s/cur_freq", g_dev_name[i]);
		if ((hz = read_long(p)) > 0) range_add(&g_dev_mhz[i], hz / 1e6);
		snprintf(p, sizeof p, "/sys/class/devfreq/%.63s/governor", g_dev_name[i]);
		read_word(p, g_dev_gov[i], sizeof g_dev_gov[0]);
	}
	for (int i = 0; i < g_nzone; i++) {
		long mc;

		snprintf(p, sizeof p, "/sys/class/thermal/thermal_zone%d/temp", i);
		if ((mc = read_long(p)) != -1) range_add(&g_zone_c[i], mc / 1000.0);
	}
}

/* Each core's busy and total jiffies from /proc/stat */
struct cpu_times { unsigned long long busy[MAX_CPUS], total[MAX_CPUS]; };

static void cpu_times(struct cpu_times *t)
{
	static char buf[8192];
	char *l;

	memset(t, 0, sizeof *t);
	if (slurp("/proc/stat", buf, sizeof buf) <= 0) return;
	for (l = buf; l && *l; l = strchr(l, '\n') ? strchr(l, '\n') + 1 : NULL) {
		unsigned long long v[8] = { 0 };
		int c;

		if (strncmp(l, "cpu", 3) || l[3] < '0' || l[3] > '9') continue;
		if (sscanf(l + 3, "%d %llu %llu %llu %llu %llu %llu %llu %llu", &c, &v[0], &v[1], &v[2],
		           &v[3], &v[4], &v[5], &v[6], &v[7]) < 5 || c < 0 || c >= MAX_CPUS)
			continue;
		t->busy[c] = v[0] + v[1] + v[2] + v[5] + v[6] + v[7];
		t->total[c] = t->busy[c] + v[3] + v[4];
	}
}

/* --- the processes ------------------------------------------------------- */

struct proc { int pid; char comm[32]; unsigned long long ticks; double cpu; long rss_kb; };

static int procs(struct proc *out, int cap)
{
	DIR *d = opendir("/proc");
	struct dirent *e;
	int n = 0;

	if (!d) return 0;
	while ((e = readdir(d)) && n < cap) {
		char p[64], line[1024];
		int pid = atoi(e->d_name);

		if (pid <= 0) continue;
		snprintf(p, sizeof p, "/proc/%d/stat", pid);
		if (slurp(p, line, sizeof line) <= 0 || !perf_stat_ticks(line, &out[n].ticks)) continue;
		out[n].pid = pid;
		snprintf(p, sizeof p, "/proc/%d/comm", pid);
		read_word(p, out[n].comm, sizeof out[n].comm);
		out[n].cpu = 0;
		out[n].rss_kb = -1;
		n++;
	}
	closedir(d);
	return n;
}

static long rss_kb(int pid)
{
	char p[64], buf[4096], v[32];
	const char *r;

	snprintf(p, sizeof p, "/proc/%d/status", pid);
	if (slurp(p, buf, sizeof buf) <= 0 || !(r = strstr(buf, "VmRSS:"))) return -1;
	snprintf(v, sizeof v, "%.31s", r + 6);
	return strtol(v, NULL, 10);
}

/* --- Diatom's own measure ------------------------------------------------ */

static char g_now_a[4096], g_now_b[4096];

/* The last "x of the clock" line in the log, for a Diatom from before its now
 * file: a speed step's, logged when the step changes and at a game's end */
static void log_last_clock(char *out, size_t n)
{
	static char buf[65536];
	FILE *f = fopen(getenv("PX2_LOG") ? getenv("PX2_LOG") : LOG_FILE, "r");
	long size;
	size_t got;
	char *hit = NULL, *p;

	out[0] = '\0';
	if (!f) return;
	fseek(f, 0, SEEK_END);
	size = ftell(f);
	fseek(f, size > (long)sizeof buf - 1 ? size - (long)sizeof buf + 1 : 0, SEEK_SET);
	got = fread(buf, 1, sizeof buf - 1, f);
	fclose(f);
	buf[got] = '\0';
	for (p = buf; (p = strstr(p, "of the clock")); p++) hit = p;
	if (!hit) return;
	while (hit > buf && hit[-1] != '\n') hit--;
	snprintf(out, n, "%.*s", (int)strcspn(hit, "\n"), hit);
}

/* --- out ----------------------------------------------------------------- */

static void json_str(const char *s)
{
	putchar('"');
	for (; *s; s++) {
		unsigned char c = (unsigned char)*s;

		if (c == '"' || c == '\\') printf("\\%c", c);
		else if (c < 0x20) printf("\\u%04x", c);
		else putchar(c);
	}
	putchar('"');
}

static void base_of(const char *path, char *out, size_t n)
{
	const char *b = strrchr(path, '/');

	snprintf(out, n, "%s", b ? b + 1 : path);
}

int main(int argc, char **argv)
{
	double seconds = 10.0;
	int json = 0, panel, now_asked = 0, have_a = 0, have_b = 0, polled = 0;
	uint64_t *ft = NULL;
	int nft = 0, cap = 0;
	uint32_t last_fb = 0;
	uint64_t t0, t_end, next_sample, a_at;
	struct cpu_times c0, c1;
	static struct proc p0[MAX_PROCS], p1[MAX_PROCS];
	int n0, n1;
	long ticks_s = sysconf(_SC_CLK_TCK);
	struct perf_frames fr;
	char now_ask[300], logline[512] = "", rom[1024] = "", core[1024] = "", meminfo[4096];
	double speed = 0.0, dropped = 0.0, watched;
	struct proc *dia = NULL, *launcher = NULL, *top[TOP] = { NULL };

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--json")) json = 1;
		else if (atof(argv[i]) > 0) seconds = atof(argv[i]);
		else {
			fprintf(stderr, "usage: px2-perf [SECONDS] [--json]\n");
			return 2;
		}
	}
	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);

	find_sensors();
	panel = panel_open();
	/* Diatom's counters, asked for now: it writes them four times a second
	 * while the ask is there. A file someone else made is left to them. */
	snprintf(now_ask, sizeof now_ask, "%s.ask", NOW_FILE);
	if (access(now_ask, F_OK)) {
		int fd = open(now_ask, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);

		if (fd >= 0) { close(fd); now_asked = 1; }
	}
	cpu_times(&c0);
	n0 = procs(p0, MAX_PROCS);
	t0 = now_us();
	t_end = t0 + (uint64_t)(seconds * 1e6);
	next_sample = t0;
	a_at = t0 + 600000;   /* past Diatom's next writing, and its quarter second */

	while (!g_stop && now_us() < t_end) {
		uint64_t t;

		if (panel) {
			union drm_wait_vblank vbl = { .request = { .type = _DRM_VBLANK_RELATIVE, .sequence = 1 } };
			struct drm_mode_crtc crtc = { .crtc_id = g_crtc };

			if (ioctl(g_drm, DRM_IOCTL_WAIT_VBLANK, &vbl) < 0) {
				/* No vertical blank to wait on (QEMU's display): poll,
				 * and say so, since the times are the poll's then */
				polled = 1;
				sleep_us(4000);
			}
			t = now_us();
			if (ioctl(g_drm, DRM_IOCTL_MODE_GETCRTC, &crtc) == 0 && crtc.fb_id != last_fb) {
				/* The first one seen is the frame on screen as the watch
				 * began, not a new one */
				if (last_fb) {
					if (nft == cap) {
						cap = cap ? cap * 2 : 1024;
						if (!(ft = realloc(ft, sizeof *ft * (size_t)cap))) return 1;
					}
					ft[nft++] = t;
				}
				last_fb = crtc.fb_id;
			}
		} else {
			sleep_us(50000);
			t = now_us();
		}
		if (t >= next_sample) {
			sample_sensors();
			next_sample = t + 1000000;
		}
		if (!have_a && t >= a_at)
			have_a = slurp(NOW_FILE, g_now_a, sizeof g_now_a) > 0;
	}
	watched = (now_us() - t0) / 1e6;
	cpu_times(&c1);
	n1 = procs(p1, MAX_PROCS);
	have_b = slurp(NOW_FILE, g_now_b, sizeof g_now_b) > 0;
	if (now_asked) unlink(now_ask);
	if (have_a && have_b) speed = perf_speed(g_now_a, g_now_b, &dropped);
	if (have_b) {
		perf_kv(g_now_b, "rom", rom, sizeof rom);
		perf_kv(g_now_b, "core", core, sizeof core);
	}
	if (!have_b || !speed) log_last_clock(logline, sizeof logline);

	/* Each process's CPU over the watch, and the busiest */
	for (int i = 0; i < n1; i++) {
		for (int j = 0; j < n0; j++)
			if (p0[j].pid == p1[i].pid && !strcmp(p0[j].comm, p1[i].comm)) {
				p1[i].cpu = (double)(p1[i].ticks - p0[j].ticks) * 100.0 / ticks_s / watched;
				break;
			}
		if (!strcmp(p1[i].comm, "diatom") && !dia) dia = &p1[i];
		else if (!strcmp(p1[i].comm, "zyperos.elf") && !launcher) launcher = &p1[i];
		else if (p1[i].pid != getpid() && p1[i].cpu > 0.0)
			for (int k = 0; k < TOP; k++)
				if (!top[k] || p1[i].cpu > top[k]->cpu) {
					memmove(&top[k + 1], &top[k], sizeof top[0] * (size_t)(TOP - 1 - k));
					top[k] = &p1[i];
					break;
				}
	}
	if (dia) dia->rss_kb = rss_kb(dia->pid);
	if (launcher) launcher->rss_kb = rss_kb(launcher->pid);
	for (int k = 0; k < TOP; k++)
		if (top[k]) top[k]->rss_kb = rss_kb(top[k]->pid);
	slurp("/proc/meminfo", meminfo, sizeof meminfo);
	{
		const char *m;
		long total = (m = strstr(meminfo, "MemTotal:")) ? strtol(m + 9, NULL, 10) : -1;
		long avail = (m = strstr(meminfo, "MemAvailable:")) ? strtol(m + 13, NULL, 10) : -1;
		long freek = (m = strstr(meminfo, "MemFree:")) ? strtol(m + 8, NULL, 10) : -1;
		char name[256];

		perf_frame_stats(ft, nft, watched, &fr);
		base_of(rom, name, sizeof name);

		if (json) {
			printf("{\"seconds\":%.2f,\n \"panel\":", watched);
			if (panel && fr.times)
				printf("{\"fps\":%.2f,\"frames\":%d,\"vblank\":%s,\"frame_ms\":{\"min\":%.2f,"
				       "\"median\":%.2f,\"p95\":%.2f,\"max\":%.2f}}", fr.fps, fr.frames,
				       polled ? "false" : "true", fr.min_ms, fr.median_ms, fr.p95_ms, fr.max_ms);
			else if (panel)
				printf("{\"fps\":%.2f,\"frames\":%d,\"vblank\":%s,\"frame_ms\":null}", fr.fps,
				       fr.frames, polled ? "false" : "true");
			else
				printf("null");
			printf(",\n \"game\":");
			if (have_b) {
				printf("{\"rom\":");
				json_str(rom);
				printf(",\"core\":");
				json_str(core);
				printf(",\"fps\":%.4f,\"speed_step\":%g,", perf_kv_num(g_now_b, "fps"),
				       perf_kv_num(g_now_b, "speed"));
				if (speed > 0.0)
					printf("\"of_the_clock\":%.3f,\"audio_dropped\":%.0f}", speed, dropped);
				else
					printf("\"of_the_clock\":null,\"audio_dropped\":null}");
			} else {
				printf("null");
			}
			printf(",\n \"log_last_clock\":");
			if (logline[0]) json_str(logline); else printf("null");
			printf(",\n \"cpu\":[");
			for (int i = 0; i < g_ncpu; i++) {
				unsigned long long db = c1.busy[i] - c0.busy[i], dt = c1.total[i] - c0.total[i];

				printf("%s{\"cpu\":%d,\"online\":%s,\"load\":%.1f,", i ? "," : "", i,
				       g_cpu_online[i] ? "true" : "false", dt ? db * 100.0 / dt : 0.0);
				if (g_cpu_mhz[i].n)
					printf("\"mhz\":%.0f,\"mhz_min\":%.0f,\"mhz_max\":%.0f,\"governor\":",
					       range_mean(&g_cpu_mhz[i]), g_cpu_mhz[i].min, g_cpu_mhz[i].max);
				else
					printf("\"mhz\":null,\"governor\":");
				json_str(g_cpu_mhz[i].n ? g_cpu_gov[i] : "");
				putchar('}');
			}
			printf("],\n \"devfreq\":[");
			for (int i = 0; i < g_ndev; i++) {
				printf("%s{\"name\":", i ? "," : "");
				json_str(g_dev_name[i]);
				printf(",\"mhz\":%.0f,\"mhz_min\":%.0f,\"mhz_max\":%.0f,\"governor\":",
				       range_mean(&g_dev_mhz[i]), g_dev_mhz[i].min, g_dev_mhz[i].max);
				json_str(g_dev_gov[i]);
				putchar('}');
			}
			printf("],\n \"thermal\":[");
			for (int i = 0; i < g_nzone; i++) {
				printf("%s{\"zone\":", i ? "," : "");
				json_str(g_zone_type[i]);
				printf(",\"c\":%.1f,\"c_max\":%.1f}", range_mean(&g_zone_c[i]), g_zone_c[i].max);
			}
			printf("],\n \"processes\":{");
			{
				struct proc *named[2] = { dia, launcher };
				const char *key[2] = { "diatom", "launcher" };

				for (int k = 0; k < 2; k++) {
					printf("%s\"%s\":", k ? "," : "", key[k]);
					if (named[k])
						printf("{\"pid\":%d,\"cpu\":%.1f,\"rss_kb\":%ld}", named[k]->pid,
						       named[k]->cpu, named[k]->rss_kb);
					else
						printf("null");
				}
				printf(",\"busiest\":[");
				for (int k = 0; k < TOP && top[k]; k++) {
					printf("%s{\"pid\":%d,\"name\":", k ? "," : "", top[k]->pid);
					json_str(top[k]->comm);
					printf(",\"cpu\":%.1f,\"rss_kb\":%ld}", top[k]->cpu, top[k]->rss_kb);
				}
				printf("]}");
			}
			printf(",\n \"memory_kb\":{\"total\":%ld,\"available\":%ld,\"free\":%ld}}\n",
			       total, avail, freek);
			return 0;
		}

		printf("px2-perf: %.1f s", watched);
		if (have_b) printf(", %s running %s", name, strrchr(core, '/') ? strrchr(core, '/') + 1 : core);
		printf("\n");
		if (panel && fr.times)
			printf("panel     %.1f fps; frame time min %.1f, median %.1f, 95th %.1f, max %.1f ms (%d frames%s)\n",
			       fr.fps, fr.min_ms, fr.median_ms, fr.p95_ms, fr.max_ms, fr.frames,
			       polled ? "; no vertical blank here, polled every 4 ms" : "");
		else if (panel)
			printf("panel     %.1f fps (%d new frames: nothing moves)\n", fr.fps, fr.frames);
		else
			printf("panel     no framebuffer on screen\n");
		if (have_b && speed > 0.0)
			printf("game      %.2fx of the clock at speed step %gx (its own %.2f fps), %.0f audio frame(s) dropped\n",
			       speed, perf_kv_num(g_now_b, "speed"), perf_kv_num(g_now_b, "fps"), dropped);
		else if (have_b)
			printf("game      loaded, not running (in the menu, or a Diatom that answers no ask)\n");
		else
			printf("game      none in Diatom\n");
		if (logline[0]) printf("log       %s\n", logline);
		for (int i = 0; i < g_ncpu; i++) {
			unsigned long long db = c1.busy[i] - c0.busy[i], dt = c1.total[i] - c0.total[i];

			printf("cpu%d      %5.1f%% busy", i, dt ? db * 100.0 / dt : 0.0);
			if (!g_cpu_online[i]) printf(", offline");
			if (g_cpu_mhz[i].n)
				printf(", %.0f MHz (%.0f-%.0f), %s", range_mean(&g_cpu_mhz[i]), g_cpu_mhz[i].min,
				       g_cpu_mhz[i].max, g_cpu_gov[i]);
			printf("\n");
		}
		if (!g_ncpu || !g_cpu_mhz[0].n) printf("cpufreq   none here\n");
		for (int i = 0; i < g_ndev; i++)
			printf("devfreq   %s %.0f MHz (%.0f-%.0f), %s\n", g_dev_name[i], range_mean(&g_dev_mhz[i]),
			       g_dev_mhz[i].min, g_dev_mhz[i].max, g_dev_gov[i]);
		if (!g_ndev) printf("devfreq   none here\n");
		for (int i = 0; i < g_nzone; i++)
			printf("thermal   %s %.1f C (most %.1f)\n", g_zone_type[i], range_mean(&g_zone_c[i]),
			       g_zone_c[i].max);
		if (!g_nzone) printf("thermal   none here\n");
		if (dia)
			printf("diatom    pid %d, %.1f%% CPU, %.1f MB RSS\n", dia->pid, dia->cpu, dia->rss_kb / 1024.0);
		else
			printf("diatom    not running\n");
		if (launcher)
			printf("launcher  pid %d, %.1f%% CPU, %.1f MB RSS\n", launcher->pid, launcher->cpu,
			       launcher->rss_kb / 1024.0);
		else
			printf("launcher  not running\n");
		for (int k = 0; k < TOP && top[k]; k++)
			printf("busy      %s (pid %d), %.1f%% CPU, %.1f MB RSS\n", top[k]->comm, top[k]->pid,
			       top[k]->cpu, top[k]->rss_kb > 0 ? top[k]->rss_kb / 1024.0 : 0.0);
		printf("memory    %.0f MB available, %.0f MB free, of %.0f MB\n", avail / 1024.0,
		       freek / 1024.0, total / 1024.0);
	}
	return 0;
}
