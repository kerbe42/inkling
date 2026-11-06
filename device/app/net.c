#define _GNU_SOURCE
/* Network jobs: one worker thread runs one job at a time (an AI turn, an email, a key check, an update step); the
 * main thread submits it and picks the result up when net_result_fd() becomes readable. */
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <stdint.h>

#include "scribe.h"

/* Two lanes, each with its own worker thread: lane 0 runs the page's turns, emails, key checks and updates (one at a
 * time); lane 1 runs drawings, which take ~10 s and must not hold up the next turn. Both report through one pipe. */
#define LANES 2
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv[LANES] = { PTHREAD_COND_INITIALIZER, PTHREAD_COND_INITIALIZER };
static int pfd[2] = { -1, -1 };
static int busy[LANES];
static struct {
	int has, kind;
	JobFn fn;
	void *arg;
} job[LANES];
static struct {
	int ready, kind, status;
	char *body;
	size_t len;
} res[LANES];

static void *worker(void *arg) {
	int lane = (int)(intptr_t)arg;
	for (;;) {
		pthread_mutex_lock(&mu);
		while (!job[lane].has) pthread_cond_wait(&cv[lane], &mu);
		int kind = job[lane].kind;
		JobFn fn = job[lane].fn;
		void *farg = job[lane].arg;
		job[lane].has = 0;
		pthread_mutex_unlock(&mu);

		char *out = NULL;
		size_t olen = 0;
		int st = fn(farg, &out, &olen);

		pthread_mutex_lock(&mu);
		res[lane].ready = 1; res[lane].kind = kind; res[lane].status = st; res[lane].body = out; res[lane].len = olen;
		pthread_mutex_unlock(&mu);
		ssize_t w = write(pfd[1], "x", 1);
		(void)w;
	}
	return NULL;
}

void net_start(void) {
	if (pipe(pfd) == 0) {
		fcntl(pfd[0], F_SETFL, O_NONBLOCK);
		fcntl(pfd[0], F_SETFD, FD_CLOEXEC);
		fcntl(pfd[1], F_SETFD, FD_CLOEXEC);
	}
	for (int lane = 0; lane < LANES; lane++) {
		pthread_t t;
		pthread_create(&t, NULL, worker, (void *)(intptr_t)lane);
		pthread_detach(t);
	}
}

int net_busy(void) { return busy[0]; }
int net_lane_busy(int lane) { return lane >= 0 && lane < LANES && busy[lane]; }
int net_result_fd(void) { return pfd[0]; }

/* 1 if a result is waiting (the main loop then polls without sleeping). */
int net_pending(void) {
	pthread_mutex_lock(&mu);
	int r = res[0].ready || res[1].ready;
	pthread_mutex_unlock(&mu);
	return r;
}

/* kind is echoed back with the result. */
int net_submit_lane(int lane, JobFn fn, void *arg, int kind) {
	if (lane < 0 || lane >= LANES || busy[lane] || !fn) return -1;
	pthread_mutex_lock(&mu);
	busy[lane] = 1;
	job[lane].kind = kind; job[lane].fn = fn; job[lane].arg = arg; job[lane].has = 1;
	pthread_cond_signal(&cv[lane]);
	pthread_mutex_unlock(&mu);
	return 0;
}

int net_submit_fn(JobFn fn, void *arg, int kind) { return net_submit_lane(0, fn, arg, kind); }

/* Takes one waiting result (lane 0 first); call again while net_pending(). */
int net_take_result(int *kind, int *http_status, char **body, size_t *len) {
	char c;
	while (read(pfd[0], &c, 1) == 1) {}
	pthread_mutex_lock(&mu);
	int got = 0;
	for (int lane = 0; lane < LANES && !got; lane++)
		if (res[lane].ready) {
			got = 1;
			*kind = res[lane].kind; *http_status = res[lane].status; *body = res[lane].body; *len = res[lane].len;
			res[lane].ready = 0; res[lane].body = NULL;
			busy[lane] = 0;
		}
	pthread_mutex_unlock(&mu);
	return got;
}
