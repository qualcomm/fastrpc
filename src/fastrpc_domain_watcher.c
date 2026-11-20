// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/*
 * Generic domain-availability watcher thread: inotify on a backend-supplied
 * directory, falling back to stat()/rescan polling if inotify is
 * unavailable. See fastrpc_domain_watcher.h.
 */

#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>

#include "AEEstd.h"
#include "fastrpc_internal.h"
#include "fastrpc_domain_watcher.h"

/* Fallback poll interval (ms). */
#define FASTRPC_POLLING_INTERVAL_MS     1000

/* Quiet intervals after which the watcher exits once domains are found. */
#define FASTRPC_MAX_UNCHANGED_INTERVALS 100

/* Retry parameters for the ueventd/SELinux labeling race on node creation. */
#define FASTRPC_DEV_LABEL_RETRY_MAX     50
#define FASTRPC_DEV_LABEL_RETRY_MS      40

#define FASTRPC_INOTIFY_EVENT_SIZE      (sizeof(struct inotify_event))
#define FASTRPC_INOTIFY_BUF_LEN \
	(128 * (FASTRPC_INOTIFY_EVENT_SIZE + 16))

typedef enum {
	WATCHER_MODE_POLLING = 0,
	WATCHER_MODE_INOTIFY = 1,
} watcher_mode_t;

typedef struct {
	pthread_t      thread;
	bool           thread_running;
	int            exit_pipe[2];    /* [0]=read, [1]=write */
	bool           domains_exist;
	watcher_mode_t mode;
	int            inotify_fd;
	int            inotify_wd;
	fastrpc_domain_watcher_config_t cfg;
} fastrpc_dev_watcher_t;

static fastrpc_dev_watcher_t g_poller = {
	.thread         = 0,
	.thread_running = false,
	.exit_pipe      = {-1, -1},
	.domains_exist  = false,
	.mode           = WATCHER_MODE_POLLING,
	.inotify_fd     = -1,
	.inotify_wd     = -1,
};

void fastrpc_domain_watcher_mark_domains_exist(void)
{
	g_poller.domains_exist = true;
}

static void *fastrpc_inotify_watcher_thread(void *arg)
{
	struct pollfd pfds[2];
	char buf[FASTRPC_INOTIFY_BUF_LEN];
	int unchanged_intervals = 0;
	size_t prefix_len = g_poller.cfg.event_name_prefix ?
		strlen(g_poller.cfg.event_name_prefix) : 0;

	(void)arg;

	/* fastrpc_domain_watcher_init() only starts this thread after pipe()
	 * succeeded, but check explicitly rather than assume the caller's
	 * invariant holds. */
	if (g_poller.exit_pipe[0] < 0)
		return NULL;

	g_poller.thread_running = true;

	while (1) {
		pfds[0].fd      = g_poller.exit_pipe[0];
		pfds[0].events  = POLLIN;
		pfds[0].revents = 0;
		pfds[1].fd      = g_poller.inotify_fd;
		pfds[1].events  = POLLIN;
		pfds[1].revents = 0;

		if (poll(pfds, 2, -1) < 0) {
			if (errno == EINTR) continue;
			break;
		}

		if (pfds[0].revents & POLLIN)
			break;

		if (pfds[1].revents & POLLIN) {
			int len = read(g_poller.inotify_fd, buf, sizeof(buf));
			if (len < 0) {
				if (errno == EINTR) continue;
				break;
			}

			int offset = 0;
			while (offset < len) {
				struct inotify_event *ev =
				    (struct inotify_event *)(buf + offset);
				offset += FASTRPC_INOTIFY_EVENT_SIZE + ev->len;

				if (!ev->len || !ev->name[0])
					continue;

				if (prefix_len &&
				    strncmp(ev->name, g_poller.cfg.event_name_prefix,
				            prefix_len) != 0)
					continue;

				bool appeared = !!(ev->mask & IN_CREATE);
				bool removed  = !!(ev->mask & IN_DELETE);
				if (!appeared && !removed)
					continue;

				if (appeared) {
					/* ueventd labeling race: retry stat(). */
					char path[256];
					struct stat st;
					int retry;
					struct pollfd rpfd;

					snprintf(path, sizeof(path), "%s/%s",
					         g_poller.cfg.watch_dir, ev->name);
					rpfd.fd      = g_poller.exit_pipe[0];
					rpfd.events  = POLLIN;
					rpfd.revents = 0;
					for (retry = 0;
					     retry < FASTRPC_DEV_LABEL_RETRY_MAX;
					     retry++) {
						if (stat(path, &st) == 0) break;
						int pr = poll(&rpfd, 1,
						    FASTRPC_DEV_LABEL_RETRY_MS);
						if (pr > 0 &&
						    (rpfd.revents & POLLIN))
							goto bail;
					}
				}

				g_poller.cfg.handle_event_fn(ev->name, appeared);
				unchanged_intervals = 0;
			}
		}

		unchanged_intervals++;
		if (g_poller.domains_exist &&
		    unchanged_intervals >= FASTRPC_MAX_UNCHANGED_INTERVALS)
			break;
	}

bail:
	g_poller.thread_running = false;
	return NULL;
}

/*
 * rescan-based fallback watcher used when inotify is unavailable. Cannot
 * detect disappearance on its own since there's no event source; relies on
 * periodic re-probing via the backend's rescan_fn.
 */
static void *fastrpc_polling_watcher_thread(void *arg)
{
	struct pollfd exit_pfd;
	int unchanged_intervals = 0;
	int status_sum_before, status_sum_after;
	struct fastrpc_domain_hash_node *node, *tmp;

	(void)arg;

	/* fastrpc_domain_watcher_init() only starts this thread after pipe()
	 * succeeded, but check explicitly rather than assume the caller's
	 * invariant holds. */
	if (g_poller.exit_pipe[0] < 0)
		return NULL;

	g_poller.thread_running = true;

	while (1) {
		exit_pfd.fd      = g_poller.exit_pipe[0];
		exit_pfd.events  = POLLIN;
		exit_pfd.revents = 0;

		if (poll(&exit_pfd, 1, FASTRPC_POLLING_INTERVAL_MS) < 0) {
			if (errno == EINTR) continue;
			break;
		}
		if (exit_pfd.revents & POLLIN)
			break;

		status_sum_before = 0;
		pthread_mutex_lock(&g_domain_table.mut);
		HASH_ITER(hh, g_domain_table.tbl, node, tmp) {
			status_sum_before += node->domain_obj.domain_info.status;
			if (node->domain_obj.domain_info.status)
				g_poller.domains_exist = true;
		}
		pthread_mutex_unlock(&g_domain_table.mut);

		g_poller.cfg.rescan_fn();

		status_sum_after = 0;
		pthread_mutex_lock(&g_domain_table.mut);
		HASH_ITER(hh, g_domain_table.tbl, node, tmp)
			status_sum_after += node->domain_obj.domain_info.status;
		pthread_mutex_unlock(&g_domain_table.mut);

		if (status_sum_before != status_sum_after) {
			unchanged_intervals = 0;
		} else {
			unchanged_intervals++;
			if (g_poller.domains_exist &&
			    unchanged_intervals >= FASTRPC_MAX_UNCHANGED_INTERVALS)
				break;
		}
	}

	g_poller.thread_running = false;
	return NULL;
}

static void *fastrpc_watcher_thread(void *arg)
{
	if (g_poller.mode == WATCHER_MODE_INOTIFY)
		return fastrpc_inotify_watcher_thread(arg);
	return fastrpc_polling_watcher_thread(arg);
}

/* Detects inotify support; falls back to polling mode if unavailable. */
static void fastrpc_watcher_setup_mode(void)
{
	g_poller.inotify_fd = inotify_init();
	if (g_poller.inotify_fd < 0) {
		g_poller.mode = WATCHER_MODE_POLLING;
		return;
	}

	g_poller.inotify_wd = inotify_add_watch(g_poller.inotify_fd,
	                                         g_poller.cfg.watch_dir,
	                                         IN_CREATE | IN_DELETE);
	if (g_poller.inotify_wd < 0) {
		close(g_poller.inotify_fd);
		g_poller.inotify_fd = -1;
		g_poller.mode = WATCHER_MODE_POLLING;
		return;
	}

	g_poller.mode = WATCHER_MODE_INOTIFY;
}

void fastrpc_domain_watcher_init(const fastrpc_domain_watcher_config_t *cfg)
{
	if (!cfg || !cfg->watch_dir || !cfg->rescan_fn || !cfg->handle_event_fn)
		return;

	g_poller.cfg = *cfg;

	fastrpc_watcher_setup_mode();

	if (pipe(g_poller.exit_pipe) != 0) {
		g_poller.exit_pipe[0] = g_poller.exit_pipe[1] = -1;
		return;
	}

	if (pthread_create(&g_poller.thread, NULL,
	                   fastrpc_watcher_thread, NULL) != 0) {
		close(g_poller.exit_pipe[0]);
		close(g_poller.exit_pipe[1]);
		g_poller.exit_pipe[0] = g_poller.exit_pipe[1] = -1;
	}
}

void fastrpc_domain_watcher_deinit(void)
{
	if (g_poller.mode == WATCHER_MODE_INOTIFY &&
	    g_poller.inotify_fd != -1 && g_poller.inotify_wd != -1) {
		inotify_rm_watch(g_poller.inotify_fd, g_poller.inotify_wd);
		g_poller.inotify_wd = -1;
	}

	if (g_poller.exit_pipe[1] != -1) {
		write(g_poller.exit_pipe[1], "x", 1);
		close(g_poller.exit_pipe[1]);
		g_poller.exit_pipe[1] = -1;
	}
	if (g_poller.thread) {
		pthread_join(g_poller.thread, NULL);
		g_poller.thread = 0;
	}
	if (g_poller.exit_pipe[0] != -1) {
		close(g_poller.exit_pipe[0]);
		g_poller.exit_pipe[0] = -1;
	}
	if (g_poller.inotify_fd != -1) {
		close(g_poller.inotify_fd);
		g_poller.inotify_fd = -1;
	}
	g_poller.thread_running = false;
	g_poller.domains_exist  = false;
}
