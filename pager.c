/* Copyright (c) 2020, Aliaksei Katovich <aliaksei.katovich at gmail.com>
 *
 * See LICENSE for license details.
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <wchar.h> /* st.h needs it */

#include "st.h"
#include "pager.h"

extern ssize_t xwrite(int, const char *, size_t);

/* history viewer related globals */

static int histfd = -1;
static size_t histmax = HISTORY_LINES;
static size_t histcur;
static char *histpath;
static const char *pager;
static const char *term;

#define HISTBUF_SIZE 128

static char histbuf[HISTBUF_SIZE + 1];
static char *histend = histbuf + HISTBUF_SIZE;
static char *histptr = histbuf;
static uint8_t histcolorflag;
static uint32_t histprevfg;
static uint32_t histprevbg;
static uint8_t pager_running;

#define HISTCOLOR_FMT "\033[0m\033[%d;%dm"

static int writehistorycolor(Glyph *g)
{
	char color[sizeof(HISTCOLOR_FMT)];
	size_t len;
	uint8_t bg;

	if (!histcolorflag
	    || (g->fg == histprevfg && g->bg == histprevbg)
	    || (!BETWEEN(g->fg, 0, 7) || !BETWEEN(g->bg, 0, 7)))
		return 0;

	if (g->mode & ATTR_BOLD)
		bg = 1;
	else
		bg = g->bg + 40;

	snprintf(color, sizeof(color), HISTCOLOR_FMT, bg, g->fg + 30);

	len = strlen(color);
	if (histptr + len > histend) {
		xwrite(histfd, histbuf, histptr - histbuf);
		memset(histbuf, 0, HISTBUF_SIZE);
		histptr = histbuf;
	}

	memcpy(histptr, color, len);
	histptr += len;
	*histptr++ = g->u;
	return 1;
}

static void spawn_pager(uint32_t win)
{
	if (!histpath || !pager)
		return;

	int len = strlen(term) + sizeof(" -w 0xffffffffffffffff -e ");

	len += sizeof(" -g 65535x65535 ");
	len += strlen(pager);
	len += strlen(histpath);

	char pagercmd[len];

	memset(pagercmd, 0, len);
	snprintf(pagercmd, len, "%s -g %dx%d -w 0x%x -e %s %s", term, getcol(),
	  getrow(), win, pager, histpath);
	system(pagercmd);
}

static void *show_history(void *arg)
{
	pid_t pid = fork();
	if (pid < 0) {
		fprintf(stderr, "failed to fork | %s", strerror(errno));
		goto out;
	}

	uint64_t win = (uint64_t) arg;
	if (pid == 0) { /* child */
		spawn_pager(win);
		_exit(0);
	}

	waitpid(pid, NULL, 0);
out:
	pager_running = 0;
	return NULL;
}

static void run_pager(uint32_t win)
{
	if (pager_running)
		return;

	pager_running = 1;
	pthread_t t;
	pthread_create(&t, NULL, show_history, (void *) (uint64_t) win);
}

#define HISTORY_NAME "st.4294967295"

void pager_init(const char *prog)
{
	term = prog;
	const char *path;
	const char *limit;

	if (!(pager = getenv("ST_HISTORY_PAGER")))
		pager = PAGER;

	if (getenv("ST_HISTORY_COLOR")) /* don't care about value for now */
		histcolorflag = 1;

	if ((limit = getenv("ST_HISTORY_LINES")) && !(histmax = atoi(limit)))
		histmax = HISTORY_LINES;

	if (!(path = getenv("ST_HISTORY_PATH")))
		path = HISTORY_PATH;

	errno = EEXIST;
	if (mkdir(path, S_IRWXU) < 0 && errno != EEXIST) {
		histfd = -1;
		goto out;
	}

	int len = sizeof(HISTORY_NAME) + strlen(path);
	if ((histpath = calloc(1, len))) {
		snprintf(histpath, len, "%s/st.%u", path, getpid());
	} else {
		histfd = -1;
		goto out;
	}

	if ((histfd = open(histpath, O_WRONLY | O_CREAT | O_TRUNC, 0600)) < 0)
		fprintf(stderr, "open('%s') failed\n", histpath);

out:
	unsetenv("ST_HISTORY_PAGER");
	unsetenv("ST_HISTORY_LINES");
	unsetenv("ST_HISTORY_PATH");
}

void pager_reset(void)
{
	if (histfd < 0)
		return;

	histcur = 0;
	ftruncate(histfd, 0);
	lseek(histfd, 0, SEEK_SET);
}

uint8_t pager_show(uint32_t win, uint32_t button)
{
	if (pager && (button == 4 || button == UINT32_MAX)) {
		run_pager(win);
		return 1;
	}

	return 0;
}

void pager_write(int y)
{
	if (histfd < 0)
		return;

	int x;
	char wrap = 0;

	for (x = 0; x < getcol(); x++) {
		Glyph *g = getglyph(x, y);

		if (histptr >= histend) {
			xwrite(histfd, histbuf, histptr - histbuf);
			memset(histbuf, 0, HISTBUF_SIZE);
			histptr = histbuf;
		}

		if (!writehistorycolor(g))
			*histptr++ = g->u;

		histprevfg = g->fg;
		histprevbg = g->bg;

		if (g->mode & ATTR_WRAP) {
			wrap = 1;
			x++;
		}
	}

	if (!wrap)
		*(histptr - 1) = '\n';

	if (++histcur >= histmax) { /* limit is reached, rotate history */
		histcur = 0;
		ftruncate(histfd, 0);
		lseek(histfd, 0, SEEK_SET);
	}
}
