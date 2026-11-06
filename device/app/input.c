/* Input thread. The kernel keeps only a small queue per reader (about 128 events for the pen, which reports ~360
 * times a second even while hovering: roughly 60 ms). The main thread regularly spends far longer than that drawing
 * (saving a page, opening a menu, a flashing refresh), and when the queue overflows the kernel throws it away -
 * taps included, and sometimes the "pen lifted" event, which then also swallows the next tap.
 *
 * So a separate thread does nothing but read the pen, touch screen and power key as soon as events arrive and
 * queue them (time-stamped) for the main loop, which wakes up on input_fd(). If the kernel still reports a drop
 * (SYN_DROPPED), the thread asks the device for its current state and queues that instead, so the main loop never
 * believes the pen is down when it isn't. */
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "scribe.h"

#define QCAP (1 << 15) /* ~13 s of continuous pen input */

static InEv q[QCAP];
static unsigned qhead, qtail; /* head = next to read, tail = next to write; guarded by qlock */
static pthread_mutex_t qlock = PTHREAD_MUTEX_INITIALIZER;
static int fds[3] = { -1, -1, -1 }, note[2] = { -1, -1 }, stop_pipe[2] = { -1, -1 };
static pthread_t th;
static int running;
static volatile int dropped_kernel, dropped_queue; /* counts, read by the main thread for the log */

static void push(int src, int type, int code, int value, double t) {
	pthread_mutex_lock(&qlock);
	if (qtail - qhead >= QCAP) { qhead++; dropped_queue++; } /* main thread stuck for many seconds: drop the oldest */
	InEv *e = &q[qtail % QCAP];
	e->src = (unsigned char)src; e->type = (unsigned short)type; e->code = (unsigned short)code; e->value = value; e->t = t;
	qtail++;
	pthread_mutex_unlock(&qlock);
}

int input_next(InEv *e) {
	int got = 0;
	pthread_mutex_lock(&qlock);
	if (qhead != qtail) { *e = q[qhead % QCAP]; qhead++; got = 1; }
	pthread_mutex_unlock(&qlock);
	return got;
}

int input_fd(void) { return note[0]; }

void input_drain_note(void) {
	char buf[256];
	while (read(note[0], buf, sizeof buf) > 0) {}
}

void input_take_drops(int *kernel, int *queue) {
	*kernel = dropped_kernel; *queue = dropped_queue;
	dropped_kernel = dropped_queue = 0;
}

#define BITSET(a, b) (((a)[(b) / 8] >> ((b) % 8)) & 1)

/* After a kernel drop: queue the device's current state as if it had just been reported. */
static void resync(int src, int fd, double t) {
	if (src == SRC_PEN) {
		unsigned char k[KEY_MAX / 8 + 1];
		memset(k, 0, sizeof k);
		if (ioctl(fd, EVIOCGKEY(sizeof k), k) >= 0) {
			push(src, EV_KEY, BTN_TOOL_PEN, BITSET(k, BTN_TOOL_PEN), t);
			push(src, EV_KEY, BTN_TOOL_RUBBER, BITSET(k, BTN_TOOL_RUBBER), t);
		}
		const int axes[3] = { ABS_X, ABS_Y, ABS_PRESSURE };
		for (int i = 0; i < 3; i++) {
			struct input_absinfo ai;
			if (ioctl(fd, EVIOCGABS(axes[i]), &ai) >= 0) push(src, EV_ABS, axes[i], ai.value, t);
		}
		if (ioctl(fd, EVIOCGKEY(sizeof k), k) >= 0) push(src, EV_KEY, BTN_TOUCH, BITSET(k, BTN_TOUCH), t);
		push(src, EV_SYN, SYN_REPORT, 0, t);
	} else if (src == SRC_TOUCH) {
		const int codes[3] = { ABS_MT_TRACKING_ID, ABS_MT_POSITION_X, ABS_MT_POSITION_Y };
		int v[3][MT_SLOTS];
		for (int c = 0; c < 3; c++) {
			struct { unsigned code; int val[MT_SLOTS]; } s;
			s.code = (unsigned)codes[c];
			if (ioctl(fd, EVIOCGMTSLOTS(sizeof s), &s) < 0) return;
			memcpy(v[c], s.val, sizeof s.val);
		}
		for (int i = 0; i < MT_SLOTS; i++) {
			push(src, EV_ABS, ABS_MT_SLOT, i, t);
			if (v[0][i] >= 0) { push(src, EV_ABS, ABS_MT_POSITION_X, v[1][i], t); push(src, EV_ABS, ABS_MT_POSITION_Y, v[2][i], t); }
			push(src, EV_ABS, ABS_MT_TRACKING_ID, v[0][i], t);
		}
		push(src, EV_SYN, SYN_REPORT, 0, t);
	}
}

static void *loop(void *arg) {
	(void)arg;
	int skipping[3] = { 0, 0, 0 }; /* after SYN_DROPPED: ignore events up to the next SYN_REPORT */
	for (;;) {
		struct pollfd p[4];
		int n = 0, map[4];
		for (int i = 0; i < 3; i++)
			if (fds[i] >= 0) { p[n].fd = fds[i]; p[n].events = POLLIN; p[n].revents = 0; map[n++] = i; }
		p[n].fd = stop_pipe[0]; p[n].events = POLLIN; p[n].revents = 0;
		if (poll(p, (nfds_t)n + 1, -1) < 0) { if (errno == EINTR) continue; break; }
		if (p[n].revents) break;
		int any = 0;
		for (int j = 0; j < n; j++) {
			if (!(p[j].revents & POLLIN)) continue;
			int src = map[j];
			struct input_event e[64];
			ssize_t r;
			while ((r = read(fds[src], e, sizeof e)) > 0) {
				double t = mono_ms();
				for (size_t i = 0; i < (size_t)r / sizeof e[0]; i++) {
					if (e[i].type == EV_SYN && e[i].code == SYN_DROPPED) { skipping[src] = 1; dropped_kernel++; continue; }
					if (skipping[src]) {
						if (e[i].type == EV_SYN && e[i].code == SYN_REPORT) { skipping[src] = 0; resync(src, fds[src], t); any = 1; }
						continue;
					}
					push(src, e[i].type, e[i].code, e[i].value, t);
					any = 1;
				}
			}
		}
		if (any) { char c = 1; if (write(note[1], &c, 1) < 0) {} } /* a full pipe already means "wake up" */
	}
	return NULL;
}

int input_start(int pen_fd, int touch_fd, int power_fd) {
	fds[SRC_PEN] = pen_fd; fds[SRC_TOUCH] = touch_fd; fds[SRC_POWER] = power_fd;
	if (pipe2(note, O_NONBLOCK | O_CLOEXEC) < 0 || pipe2(stop_pipe, O_CLOEXEC) < 0) return -1;
	if (pthread_create(&th, NULL, loop, NULL) != 0) return -1;
	running = 1;
	return 0;
}

void input_stop(void) {
	if (!running) return;
	char c = 1;
	if (write(stop_pipe[1], &c, 1) < 0) {}
	pthread_join(th, NULL);
	running = 0;
}

#ifdef INKLING_DEV /* dev builds only (device/app/build.sh --dev) */
/* ---- Developer hook: scripted input (SIGUSR2 with SECURE_DIR/devinput present) ----
 * Feeds raw pen/touch events into the same queue as the input thread, with real timing, so tests exercise the
 * whole input path (everything above the kernel). One command per line, screen pixels:
 *   pentap X Y            pen comes into range, touches for 60 ms, lifts, leaves
 *   penbounce X Y         a tap whose tip bounces (down/up twice within ~90 ms)
 *   penhover X Y MS       pen hovering (jittering, a report every 3 ms) for MS ms, then leaves
 *   penstroke X1 Y1 X2 Y2 a pen line
 *   erasestroke X1 Y1 X2 Y2  the same with the eraser end
 *   penzigzag X1 Y X2 LEGS   one stroke back and forth between X1 and X2 (a scratch-out)
 *   fingertap X Y [SLOT]  a finger tap (contact slot 0 or 1)
 *   fingerdown X Y SLOT / fingermove X Y SLOT / fingerup SLOT
 *   flick X1 Y X2         a horizontal finger swipe
 *   penin X Y / penout    pen enters range (hovering) and stays until penout
 *   power                 the power key
 *   stall MS              the main thread sleeps MS ms at its next turn (a long redraw)
 *   wait MS               */
static int sw = 1860, sh = 2480;
static volatile int stall_ms;
static int tracking = 100;

int input_take_stall(void) { int v = stall_ms; stall_ms = 0; return v; }

static void inj(int src, int type, int code, int value) { push(src, type, code, value, mono_ms()); }
static void syn(int src) {
	inj(src, EV_SYN, SYN_REPORT, 0);
	char c = 1;
	if (write(note[1], &c, 1) < 0) {}
}
static void msleep(int ms) { if (ms > 0) usleep((useconds_t)ms * 1000); }
static void pen_at(int x, int y, int touch, int pressure) {
	inj(SRC_PEN, EV_ABS, ABS_X, (int)((long)x * PEN_MAX_X / (sw - 1)));
	inj(SRC_PEN, EV_ABS, ABS_Y, (int)((long)y * PEN_MAX_Y / (sh - 1)));
	inj(SRC_PEN, EV_ABS, ABS_PRESSURE, pressure);
	if (touch >= 0) inj(SRC_PEN, EV_KEY, BTN_TOUCH, touch);
	syn(SRC_PEN);
}
static void pen_range(int in) { inj(SRC_PEN, EV_KEY, BTN_TOOL_PEN, in); syn(SRC_PEN); }
static void finger(int slot, int x, int y, int down) {
	inj(SRC_TOUCH, EV_ABS, ABS_MT_SLOT, slot);
	if (down == 1) inj(SRC_TOUCH, EV_ABS, ABS_MT_TRACKING_ID, tracking++);
	if (down >= 0) { inj(SRC_TOUCH, EV_ABS, ABS_MT_POSITION_X, x); inj(SRC_TOUCH, EV_ABS, ABS_MT_POSITION_Y, y); }
	else inj(SRC_TOUCH, EV_ABS, ABS_MT_TRACKING_ID, -1);
	syn(SRC_TOUCH);
}

static void *script(void *arg) {
	char *path = arg, line[160];
	FILE *f = fopen(path, "r");
	if (f) unlink(path);
	while (f && fgets(line, sizeof line, f)) {
		int a1 = 0, a2 = 0, a3 = 0, a4 = 0;
		char cmd[24];
		int n = sscanf(line, "%23s %d %d %d %d", cmd, &a1, &a2, &a3, &a4);
		if (n < 1 || cmd[0] == '#') continue;
		if (!strcmp(cmd, "wait")) msleep(a1);
		else if (!strcmp(cmd, "stall")) stall_ms = a1;
		else if (!strcmp(cmd, "penin")) { pen_range(1); pen_at(a1, a2, -1, 0); } /* hover, stays in range */
		else if (!strcmp(cmd, "penout")) pen_range(0);
		else if (!strcmp(cmd, "power")) { inj(SRC_POWER, EV_KEY, KEY_POWER, 1); syn(SRC_POWER); inj(SRC_POWER, EV_KEY, KEY_POWER, 0); syn(SRC_POWER); }
		else if (!strcmp(cmd, "pentap")) {
			pen_range(1); pen_at(a1, a2, -1, 0); msleep(30);
			pen_at(a1, a2, 1, 1500); msleep(60);
			pen_at(a1 + 1, a2, 0, 0); msleep(30);
			pen_range(0);
		} else if (!strcmp(cmd, "penbounce")) {
			pen_range(1); pen_at(a1, a2, -1, 0); msleep(20);
			pen_at(a1, a2, 1, 900); msleep(25); pen_at(a1, a2, 0, 0); msleep(20);
			pen_at(a1, a2 + 2, 1, 900); msleep(25); pen_at(a1, a2 + 2, 0, 0); msleep(30);
			pen_range(0);
		} else if (!strcmp(cmd, "penhover")) {
			pen_range(1);
			for (int t = 0; t < a3; t += 3) { pen_at(a1 + (t / 3) % 5, a2 + (t / 7) % 4, -1, 0); msleep(3); }
			pen_range(0);
		} else if (!strcmp(cmd, "penstroke")) {
			pen_range(1); pen_at(a1, a2, -1, 0); msleep(20);
			for (int k = 0; k <= 40; k++) { pen_at(a1 + (a3 - a1) * k / 40, a2 + (a4 - a2) * k / 40, 1, 1800); msleep(4); }
			pen_at(a3, a4, 0, 0); msleep(20);
			pen_range(0);
		} else if (!strcmp(cmd, "penzigzag")) { /* X1 Y X2 LEGS: back and forth, drifting down a little */
			pen_range(1); pen_at(a1, a2, -1, 0); msleep(20);
			for (int leg = 0; leg < a4; leg++)
				for (int k = 0; k <= 12; k++) {
					int f = leg % 2 ? 12 - k : k;
					pen_at(a1 + (a3 - a1) * f / 12, a2 - 20 + (leg * 12 + k) * 40 / (a4 * 12), 1, 1700); msleep(4);
				}
			pen_at(a1, a2, 0, 0); msleep(20);
			pen_range(0);
		} else if (!strcmp(cmd, "erasestroke")) { /* the eraser end: BTN_TOOL_RUBBER instead of the pen */
			inj(SRC_PEN, EV_KEY, BTN_TOOL_RUBBER, 1); pen_at(a1, a2, -1, 0); msleep(20);
			for (int k = 0; k <= 40; k++) { pen_at(a1 + (a3 - a1) * k / 40, a2 + (a4 - a2) * k / 40, 1, 1800); msleep(4); }
			pen_at(a3, a4, 0, 0); msleep(20);
			inj(SRC_PEN, EV_KEY, BTN_TOOL_RUBBER, 0); syn(SRC_PEN);
		} else if (!strcmp(cmd, "fingertap")) {
			int slot = n >= 4 ? a3 : 0;
			finger(slot, a1, a2, 1); msleep(70); finger(slot, a1 + 3, a2 + 2, 0); msleep(10); finger(slot, 0, 0, -1);
		} else if (!strcmp(cmd, "fingerdown")) finger(a3, a1, a2, 1);
		else if (!strcmp(cmd, "fingermove")) finger(a3, a1, a2, 0);
		else if (!strcmp(cmd, "fingerup")) finger(a1, 0, 0, -1);
		else if (!strcmp(cmd, "flick")) {
			finger(0, a1, a2, 1);
			for (int k = 1; k <= 10; k++) { msleep(15); finger(0, a1 + (a3 - a1) * k / 10, a2, 0); }
			msleep(10); finger(0, 0, 0, -1);
		}
	}
	if (f) fclose(f);
	return NULL;
}

void input_script_start(const char *path, int w, int h) {
	static char p[256];
	snprintf(p, sizeof p, "%s", path);
	sw = w; sh = h;
	pthread_t t;
	if (pthread_create(&t, NULL, script, p) == 0) pthread_detach(t);
}
#endif /* INKLING_DEV */
