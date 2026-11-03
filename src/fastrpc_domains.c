// Copyright (c) 2026, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/*
 * Level-1 domain table and remote_system_request() for the upstream kernel.
 * Domains are discovered by probing device node presence instead of sysfs.
 * Naming: label + physical_id, physical_id = type*1000 + instance_id.
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>

#include "AEEStdErr.h"
#include "AEEstd.h"
#include "AEEQList.h"
#include "remote.h"
#include "fastrpc_ioctl.h"
#include "fastrpc_internal.h"
#include "fastrpc_hash_table.h"

/* Level-1 domain hash table. Key = logical_id (1000, 5001, etc.). */
DECLARE_HASH_TABLE_TYPE(fastrpc_domain, struct fastrpc_domain_hash_node)
static fastrpc_domain_table g_domain_table;

static struct fastrpc_domain_hash_node *domain_table_find(int logical_id);
int fastrpc_discover_domains(void);
void fastrpc_notify_domain_status(fastrpc_domain domain_info);

/* Fallback poll interval (ms). */
#define FASTRPC_POLLING_INTERVAL_MS     1000

/* Quiet intervals after which the watcher exits once domains are found. */
#define FASTRPC_MAX_UNCHANGED_INTERVALS 100

/* Retry parameters for the ueventd/SELinux labeling race on node creation. */
#define FASTRPC_DEV_LABEL_RETRY_MAX     50
#define FASTRPC_DEV_LABEL_RETRY_MS      40

#define FASTRPC_DEV_PREFIX  "fastrpc-"
#define FASTRPC_DEV_DIR     "/dev"

#define FASTRPC_INOTIFY_EVENT_SIZE      (sizeof(struct inotify_event))
#define FASTRPC_INOTIFY_BUF_LEN \
	(128 * (FASTRPC_INOTIFY_EVENT_SIZE + 16))

/*
 * Device-node watcher: detects DSP up/down via inotify on /dev/ for
 * "fastrpc-*" nodes, falling back to stat() polling if inotify is
 * unavailable. Exits after a quiet period once domains have been found.
 */
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

/*
 * Legacy aliases for the handful of domains that predate dynamic discovery.
 * Everything else gets its legacy_domain_id/name left unset (-1/"").
 */
static const struct {
	const char *dev_name;           /* basename, no "fastrpc-"/"-secure" */
	int         legacy_domain_id;
	const char *legacy_domain_name;
} s_legacy_table[] = {
	{ "adsp",  ADSP_DOMAIN_ID,  "adsp"  },
	{ "cdsp",  CDSP_DOMAIN_ID,  "cdsp"  },
	{ "cdsp1", CDSP1_DOMAIN_ID, "cdsp1" },
	{ "sdsp",  SDSP_DOMAIN_ID,  "sdsp"  },
	{ "gdsp0", GDSP0_DOMAIN_ID, "gdsp0" },
	{ "gdsp1", GDSP1_DOMAIN_ID, "gdsp1" },
};
#define LEGACY_TABLE_SIZE \
	((int)(sizeof(s_legacy_table) / sizeof(s_legacy_table[0])))

/*
 * gdsp shares FASTRPC_NSP with the cdsp family; offset its instance ids
 * past cdsp's so e.g. cdsp0 and gdsp0 don't collide on the same logical id.
 */
#define GDSP_INSTANCE_OFFSET 4

/* Device-name family -> (type, instance id base offset). */
static const struct {
	const char         *prefix;
	fastrpc_domain_type type;
	int                 offset;
} s_family_table[] = {
	{ "adsp", FASTRPC_LPASS, 0 },
	{ "cdsp", FASTRPC_NSP,   0 },
	{ "sdsp", FASTRPC_SDSP,  0 },
	{ "mdsp", FASTRPC_MDSP,  0 },
	{ "lpass", FASTRPC_LPASS, 0 },
	{ "gdsp", FASTRPC_NSP,   GDSP_INSTANCE_OFFSET },
};
#define FAMILY_TABLE_SIZE \
	((int)(sizeof(s_family_table) / sizeof(s_family_table[0])))

/* Bound on distinct physical domains tracked in one discovery pass. */
#define FASTRPC_MAX_DISCOVERED_DOMAINS 64

static const char *domain_type_label(fastrpc_domain_type t)
{
	switch (t) {
	case FASTRPC_HPASS: return "hpass";
	case FASTRPC_NSP:   return "nsp";
	case FASTRPC_SDSP:  return "sdsp";
	case FASTRPC_LPASS: return "lpass";
	case FASTRPC_MDSP:  return "mdsp";
	default:            return "dsp";
	}
}

/*
 * Parses a device basename (e.g. "cdsp3", "gdsp1", "adsp") of the form
 * "<family><digits>" into a type + instance id. The trailing digits are
 * optional (absent == instance 0, e.g. "cdsp" == "cdsp0"). Unrecognized
 * families return false so callers can skip the node.
 */
static bool fastrpc_parse_dev_family(const char *base, fastrpc_domain_type *type,
                                      int *instance_id)
{
	int i;

	for (i = 0; i < FAMILY_TABLE_SIZE; i++) {
		size_t plen = strlen(s_family_table[i].prefix);
		const char *suffix;
		long digit = 0;

		if (strncmp(base, s_family_table[i].prefix, plen) != 0)
			continue;

		suffix = base + plen;
		if (suffix[0]) {
			char *endp = NULL;

			digit = strtol(suffix, &endp, 10);
			if (*endp != '\0' || digit < 0)
				continue; /* not a clean "<family><digits>" name */
		}

		*type        = s_family_table[i].type;
		*instance_id = (int)digit + s_family_table[i].offset;
		return true;
	}
	return false;
}

/*
 * Strips the "fastrpc-" prefix and an optional "-secure" suffix from a
 * /dev entry name, writing the bare family basename (e.g. "cdsp3") to
 * "out". Returns false if "filename" isn't a fastrpc device node.
 */
static bool fastrpc_dev_basename(const char *filename, char *out, size_t out_len)
{
	const char *dn = filename;
	size_t dn_len;
	static const char secure_suffix[] = "-secure";
	size_t secure_len = sizeof(secure_suffix) - 1;

	if (strncmp(dn, FASTRPC_DEV_PREFIX, strlen(FASTRPC_DEV_PREFIX)) != 0)
		return false;
	dn += strlen(FASTRPC_DEV_PREFIX);

	dn_len = strlen(dn);
	if (dn_len > secure_len &&
	    !strcmp(dn + dn_len - secure_len, secure_suffix))
		dn_len -= secure_len;

	if (dn_len == 0 || dn_len >= out_len)
		return false;

	memcpy(out, dn, dn_len);
	out[dn_len] = '\0';
	return true;
}

/* Looks up legacy_domain_id/name for a bare device basename, if any. */
static void fastrpc_legacy_lookup(const char *base, int *legacy_id,
                                   const char **legacy_nm)
{
	int i;

	*legacy_id = -1;
	*legacy_nm = "";
	for (i = 0; i < LEGACY_TABLE_SIZE; i++) {
		if (!strcmp(base, s_legacy_table[i].dev_name)) {
			*legacy_id = s_legacy_table[i].legacy_domain_id;
			*legacy_nm = s_legacy_table[i].legacy_domain_name;
			return;
		}
	}
}

/* Translates a device node appearance/disappearance into a status update. */
static void fastrpc_handle_dev_event(const char *filename, bool appeared)
{
	char base[MAX_DOMAIN_NAMELEN];
	fastrpc_domain_type type;
	int instance_id, logical_id;
	int new_status = appeared ? 1 : 0;
	struct fastrpc_domain_hash_node *node;

	if (!fastrpc_dev_basename(filename, base, sizeof(base)))
		return;
	if (!fastrpc_parse_dev_family(base, &type, &instance_id))
		return;
	logical_id = (int)type * 1000 + instance_id;

	node = domain_table_find(logical_id);
	if (!node) {
		/* Not yet in Level-1 table; run a full discover pass. */
		if (appeared)
			fastrpc_discover_domains();
		return;
	}

	/* domain_info fields are only safe to touch under this lock -- see
	 * fastrpc_discover_domains() for why domain_table_find() alone isn't
	 * enough.
	 */
	pthread_mutex_lock(&g_domain_table.mut);
	if (node->domain_obj.domain_info.status == new_status) {
		pthread_mutex_unlock(&g_domain_table.mut);
		return; /* no change */
	}
	node->domain_obj.domain_info.status = new_status;
	fastrpc_domain domain_info = node->domain_obj.domain_info;
	pthread_mutex_unlock(&g_domain_table.mut);

	if (appeared)
		g_poller.domains_exist = true;

	/* Fire callbacks outside the table lock */
	fastrpc_notify_domain_status(domain_info);
}

static void *fastrpc_inotify_watcher_thread(void *arg)
{
	struct pollfd pfds[2];
	char buf[FASTRPC_INOTIFY_BUF_LEN];
	int unchanged_intervals = 0;

	(void)arg;

	/* fastrpc_polling_watcher_init() only starts this thread after pipe()
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

				if (strncmp(ev->name, FASTRPC_DEV_PREFIX,
				            strlen(FASTRPC_DEV_PREFIX)) != 0)
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

					snprintf(path, sizeof(path), "/dev/%s",
					         ev->name);
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

				fastrpc_handle_dev_event(ev->name, appeared);
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
 * stat()-based fallback watcher used when inotify is unavailable. Cannot
 * detect SSR on its own since the node stays in /dev/; relies on periodic
 * re-probing via fastrpc_discover_domains().
 */
static void *fastrpc_polling_watcher_thread(void *arg)
{
	struct pollfd exit_pfd;
	int unchanged_intervals = 0;
	int status_sum_before, status_sum_after;
	struct fastrpc_domain_hash_node *node, *tmp;

	(void)arg;

	/* fastrpc_polling_watcher_init() only starts this thread after pipe()
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

		fastrpc_discover_domains();

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
	                                         FASTRPC_DEV_DIR,
	                                         IN_CREATE | IN_DELETE);
	if (g_poller.inotify_wd < 0) {
		close(g_poller.inotify_fd);
		g_poller.inotify_fd = -1;
		g_poller.mode = WATCHER_MODE_POLLING;
		return;
	}

	g_poller.mode = WATCHER_MODE_INOTIFY;
}

static void fastrpc_polling_watcher_init(void)
{
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

static void fastrpc_polling_watcher_deinit(void)
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

/* -----------------------------------------------------------------------
 * Domain-status notification list.
 * ----------------------------------------------------------------------- */
struct fastrpc_domain_status_notif {
	QNode qn;
	int                                 domain_id;   /* logical_id or -1 for type filter */
	fastrpc_domain_type                 domain_type; /* used when domain_id == -1 */
	void                               *context;
	fastrpc_domain_status_notif_fn_t    notifier_fn;
};

static QList             domain_status_notif_list;
static pthread_mutex_t   domain_status_notif_mut = PTHREAD_MUTEX_INITIALIZER;

void fastrpc_domains_table_lock(void)
{
	pthread_mutex_lock(&g_domain_table.mut);
}

void fastrpc_domains_table_unlock(void)
{
	pthread_mutex_unlock(&g_domain_table.mut);
}

static struct fastrpc_domain_hash_node *domain_table_add(int logical_id)
{
	struct fastrpc_domain_hash_node *node = NULL;

	pthread_mutex_lock(&g_domain_table.mut);
	HASH_FIND_INT(g_domain_table.tbl, &logical_id, node);
	if (!node) {
		node = (struct fastrpc_domain_hash_node *)
			calloc(1, sizeof(*node));
		if (node) {
			node->domain = logical_id;
			HASH_ADD_INT(g_domain_table.tbl, domain, node);
		}
	}
	pthread_mutex_unlock(&g_domain_table.mut);
	return node;
}

static struct fastrpc_domain_hash_node *domain_table_find(int logical_id)
{
	struct fastrpc_domain_hash_node *node = NULL;

	pthread_mutex_lock(&g_domain_table.mut);
	HASH_FIND_INT(g_domain_table.tbl, &logical_id, node);
	pthread_mutex_unlock(&g_domain_table.mut);
	return node;
}

void fastrpc_domain_status_notif_init(void)
{
	QList_Ctor(&domain_status_notif_list);
}

void fastrpc_domain_status_notif_deinit(void)
{
	QNode *pn = NULL, *pnn = NULL;
	struct fastrpc_domain_status_notif *node = NULL;

	pthread_mutex_lock(&domain_status_notif_mut);
	/* Guards a never-QList_Ctor()'d list (fastrpc_apps_user_init() can
	 * bail before fastrpc_domain_status_notif_init() runs) -- on a
	 * zero-filled list QList_IsEmpty() wrongly reports "not empty"
	 * (pNext is NULL, not &n), causing a NULL deref below. */
	if (!QList_IsNull(&domain_status_notif_list) &&
	    !QList_IsEmpty(&domain_status_notif_list)) {
		QLIST_NEXTSAFE_FOR_ALL(&domain_status_notif_list, pn, pnn) {
			node = STD_RECOVER_REC(struct fastrpc_domain_status_notif, qn, pn);
			if (node) {
				QNode_DequeueZ(&node->qn);
				free(node);
			}
		}
	}
	pthread_mutex_unlock(&domain_status_notif_mut);
	/*
	 * domain_status_notif_mut is a static PTHREAD_MUTEX_INITIALIZER, never
	 * pthread_mutex_init()'d by fastrpc_domain_status_notif_init() -- it's
	 * valid for the whole process lifetime. Do not destroy it here: this
	 * deinit can run more than once per process (e.g. fastrpc_apps_user_init()
	 * calls it from its own failure path, and the normal process-exit path
	 * calls it again), and locking an already-destroyed mutex on any
	 * subsequent call is undefined behavior.
	 */
}

/* Callbacks run outside domain_status_notif_mut to avoid reentrant deadlock. */
void fastrpc_notify_domain_status(fastrpc_domain domain_info)
{
	QNode *pn = NULL, *pnn = NULL;
	struct fastrpc_domain_status_notif *node = NULL;
	struct { fastrpc_domain_status_notif_fn_t fn; void *ctx; } *cbs = NULL, *grown;
	int count = 0, cap = 0, i;

	pthread_mutex_lock(&domain_status_notif_mut);
	QLIST_NEXTSAFE_FOR_ALL(&domain_status_notif_list, pn, pnn) {
		node = STD_RECOVER_REC(struct fastrpc_domain_status_notif, qn, pn);
		if (!node || !fastrpc_domain_filter_matches(node->domain_id,
		                                            node->domain_type,
		                                            &domain_info))
			continue;
		if (count == cap) {
			cap = cap ? cap * 2 : 4;
			grown = realloc(cbs, cap * sizeof(*cbs));
			if (!grown)
				break;
			cbs = grown;
		}
		cbs[count].fn  = node->notifier_fn;
		cbs[count].ctx = node->context;
		count++;
	}
	pthread_mutex_unlock(&domain_status_notif_mut);

	for (i = 0; i < count; i++)
		cbs[i].fn(cbs[i].ctx, &domain_info);
	free(cbs);
}

/* Also fires a catch-up callback for already-discovered matching domains. */
int fastrpc_register_domain_status_notif(fastrpc_domain_notif_info *info)
{
	struct fastrpc_domain_status_notif *node = NULL;
	struct fastrpc_domain_hash_node *dnode = NULL, *tmp = NULL;
	fastrpc_domain snapshot[FASTRPC_MAX_DISCOVERED_DOMAINS];
	int snapshot_count = 0, i;
	int nErr = AEE_SUCCESS;

	VERIFYC(info && info->notifier_fn, AEE_EBADPARM);
	/*
	 * -1 is the documented wildcard-by-type sentinel; any other negative
	 * value is invalid input, not a wildcard. Without this check it falls
	 * through to fastrpc_domain_filter_matches()'s domain_id>=0 branch,
	 * which treats any id<0 the same as -1, silently matching every
	 * domain regardless of the caller's domain_type.
	 */
	VERIFYC(info->domain_id >= -1, AEE_EBADPARM);

	fastrpc_domain_type domain_type = (info->domain_id == -1) ? info->domain_type
	                                                           : FASTRPC_ALL_DOMAINS;

	VERIFYC(NULL != (node = calloc(1, sizeof(*node))), AEE_ENOMEMORY);
	QNode_CtorZ(&node->qn);
	node->domain_id   = info->domain_id;
	node->domain_type = domain_type;
	node->context     = info->context;
	node->notifier_fn = info->notifier_fn;

	pthread_mutex_lock(&domain_status_notif_mut);
	QList_AppendNode(&domain_status_notif_list, &node->qn);
	pthread_mutex_unlock(&domain_status_notif_mut);

	/* node-> must not be touched below: once appended and unlocked, a
	 * concurrent (or self-triggered, from inside notifier_fn) deregister
	 * can free it at any time. */
	fastrpc_domains_table_lock();
	HASH_ITER(hh, g_domain_table.tbl, dnode, tmp) {
		if (snapshot_count >= FASTRPC_MAX_DISCOVERED_DOMAINS)
			break;
		if (fastrpc_domain_filter_matches(info->domain_id, domain_type,
		                                  &dnode->domain_obj.domain_info))
			snapshot[snapshot_count++] = dnode->domain_obj.domain_info;
	}
	fastrpc_domains_table_unlock();

	for (i = 0; i < snapshot_count; i++)
		info->notifier_fn(info->context, &snapshot[i]);
bail:
	if (nErr)
		free(node);
	return nErr;
}

int fastrpc_deregister_domain_status_notif(fastrpc_domain_notif_info *info)
{
	QNode *pn = NULL, *pnn = NULL;
	struct fastrpc_domain_status_notif *node = NULL;
	int nErr = AEE_ENOSUCH;

	VERIFYC(info && info->notifier_fn, AEE_EBADPARM);
	pthread_mutex_lock(&domain_status_notif_mut);
	QLIST_NEXTSAFE_FOR_ALL(&domain_status_notif_list, pn, pnn) {
		node = STD_RECOVER_REC(struct fastrpc_domain_status_notif, qn, pn);
		if (node &&
		    node->domain_id   == info->domain_id &&
		    node->domain_type == ((info->domain_id == -1)
		                          ? info->domain_type : FASTRPC_ALL_DOMAINS) &&
		    node->context     == info->context &&
		    node->notifier_fn == info->notifier_fn) {
			QNode_DequeueZ(&node->qn);
			free(node);
			nErr = AEE_SUCCESS;
			break;
		}
	}
	pthread_mutex_unlock(&domain_status_notif_mut);
bail:
	return nErr;
}

void fastrpc_domain_table_init(void)
{
	HASH_TABLE_OBJ_INIT(g_domain_table);
}

void fastrpc_domain_table_deinit(void)
{
	fastrpc_polling_watcher_deinit();
	HASH_TABLE_OBJ_CLEANUP(struct fastrpc_domain_hash_node, g_domain_table);
}

/*
 * Scans /dev for "fastrpc-*" nodes and (re)populates the Level-1 table.
 * Node names are parsed via fastrpc_parse_dev_family() instead of being
 * matched against a fixed per-device list, so adding more instances of an
 * existing family (another cdspN, adspN, ...) needs no code changes here.
 */
int fastrpc_discover_domains(void)
{
	DIR *dirp;
	struct dirent *de;
	static pthread_once_t watcher_once = PTHREAD_ONCE_INIT;
	int seen_ids[FASTRPC_MAX_DISCOVERED_DOMAINS];
	int seen_count = 0;
	int i;

	/*
	 * fastrpc_discover_domains() can run concurrently (an app thread via
	 * FASTRPC_GET_DOMAINS, and the watcher thread itself re-entering via
	 * fastrpc_handle_dev_event()); pthread_once() keeps the watcher
	 * thread from being started twice.
	 */
	pthread_once(&watcher_once, fastrpc_polling_watcher_init);

	dirp = opendir(FASTRPC_DEV_DIR);
	if (!dirp)
		return AEE_SUCCESS; /* best-effort; nothing found this pass */

	while ((de = readdir(dirp)) != NULL) {
		char base[MAX_DOMAIN_NAMELEN];
		char name[MAX_DOMAIN_NAMELEN];
		fastrpc_domain_type type;
		int instance_id, logical_id, legacy_id;
		const char *legacy_nm;
		struct fastrpc_domain_hash_node *node;
		int new_status, old_status;

		if (!fastrpc_dev_basename(de->d_name, base, sizeof(base)))
			continue;
		if (!fastrpc_parse_dev_family(base, &type, &instance_id))
			continue;

		logical_id = (int)type * 1000 + instance_id;

		if (seen_count < FASTRPC_MAX_DISCOVERED_DOMAINS)
			seen_ids[seen_count++] = logical_id;

		node = domain_table_add(logical_id);
		if (!node)
			continue;

		fastrpc_legacy_lookup(base, &legacy_id, &legacy_nm);
		snprintf(name, sizeof(name), "%s%d",
		         domain_type_label(type), logical_id);

		/*
		 * domain_table_add() only locks around the hash insert/find --
		 * the node's fields themselves need their own locked section
		 * here, since readers (fastrpc_get_domains() etc.) read them
		 * under this same lock.
		 */
		pthread_mutex_lock(&g_domain_table.mut);
		new_status = 1;
		old_status = node->domain_obj.domain_info.status;
		node->domain_obj.domain_info.id          = logical_id;
		node->domain_obj.domain_info.instance_id = instance_id;
		node->domain_obj.domain_info.type        = type;
		node->domain_obj.domain_info.status      = new_status;
		node->domain_obj.domain_info.card        = 0;
		node->domain_obj.domain_info.soc_id      = 0;
		strlcpy(node->domain_obj.domain_info.name, name, MAX_DOMAIN_NAMELEN);
		strlcpy(node->domain_obj.dev_basename, base, MAX_DOMAIN_NAMELEN);
		node->domain_obj.legacy_domain_id = legacy_id;
		if (legacy_nm && legacy_nm[0])
			strlcpy(node->domain_obj.legacy_domain_name,
				legacy_nm, MAX_DOMAIN_NAMELEN);
		fastrpc_domain domain_info = node->domain_obj.domain_info;
		pthread_mutex_unlock(&g_domain_table.mut);

		/*
		 * Guarantee a session-0 entry exists for every discovered
		 * domain -- callers that query a capability or send a control
		 * request without ever opening a session (e.g.
		 * DSPRPC_GET_DSP_INFO right after FASTRPC_GET_DOMAINS) need
		 * session 0 to already resolve. Idempotent, cheap to repeat
		 * on every discovery pass.
		 */
		fastrpc_ensure_default_session(logical_id);

		/*
		 * Fire domain-status callbacks when status changes. This handles:
		 *   - DSP coming up after being down  (0→1)
		 *   - SSR recovery (1→0→1)
		 *   - First discovery when callbacks are already registered
		 *     and the domain is found up (0→1 for new entry)
		 * Fired outside the lock -- see fastrpc_notify_domain_status().
		 */
		if (old_status != new_status)
			fastrpc_notify_domain_status(domain_info);
	}
	closedir(dirp);

	/* Anything previously up but not seen this pass is now down (SSR etc). */
	{
		struct fastrpc_domain_hash_node *node, *tmp;
		fastrpc_domain down[FASTRPC_MAX_DISCOVERED_DOMAINS];
		int down_count = 0;

		pthread_mutex_lock(&g_domain_table.mut);
		HASH_ITER(hh, g_domain_table.tbl, node, tmp) {
			bool seen = false;

			if (node->domain_obj.domain_info.status == 0)
				continue;
			for (i = 0; i < seen_count; i++) {
				if (seen_ids[i] == node->domain_obj.domain_info.id) {
					seen = true;
					break;
				}
			}
			if (seen)
				continue;

			node->domain_obj.domain_info.status = 0;
			if (down_count < FASTRPC_MAX_DISCOVERED_DOMAINS)
				down[down_count++] = node->domain_obj.domain_info;
		}
		pthread_mutex_unlock(&g_domain_table.mut);

		/* fire callbacks outside g_domain_table.mut */
		for (i = 0; i < down_count; i++)
			fastrpc_notify_domain_status(down[i]);
	}

	return AEE_SUCCESS;
}

int fastrpc_next_logical_domain(int prev)
{
	struct fastrpc_domain_hash_node *node, *tmp;
	int next = -1;

	pthread_mutex_lock(&g_domain_table.mut);
	HASH_ITER(hh, g_domain_table.tbl, node, tmp) {
		if (node->domain > prev && (next < 0 || node->domain < next))
			next = node->domain;
	}
	pthread_mutex_unlock(&g_domain_table.mut);
	return next;
}

int fastrpc_get_logical_id_for_legacy(int legacy_domain_id)
{
	struct fastrpc_domain_hash_node *node = NULL, *tmp = NULL;

	pthread_mutex_lock(&g_domain_table.mut);
	HASH_ITER(hh, g_domain_table.tbl, node, tmp) {
		if (node->domain_obj.legacy_domain_id == legacy_domain_id) {
			int lid = node->domain_obj.domain_info.id;
			pthread_mutex_unlock(&g_domain_table.mut);
			return lid;
		}
	}
	pthread_mutex_unlock(&g_domain_table.mut);
	return -1;
}

/* Matches against both the dynamic name ("hpass5001") and legacy name ("adsp"). */
bool fastrpc_get_domain_by_name(const char *name, size_t name_len,
                                 fastrpc_domain_internal *out)
{
	struct fastrpc_domain_hash_node *node = NULL, *tmp = NULL;
	size_t cmp_len;
	bool found = false;

	if (!name || !out)
		return false;

	cmp_len = name_len ? name_len : strlen(name);

	pthread_mutex_lock(&g_domain_table.mut);
	HASH_ITER(hh, g_domain_table.tbl, node, tmp) {
		/* Match dynamic name ("hpass5001") */
		if (strlen(node->domain_obj.domain_info.name) == cmp_len &&
		    !strncmp(node->domain_obj.domain_info.name, name, cmp_len)) {
			*out = node->domain_obj;
			found = true;
			break;
		}
		/* Match legacy name ("adsp") */
		if (node->domain_obj.legacy_domain_name[0] &&
		    strlen(node->domain_obj.legacy_domain_name) == cmp_len &&
		    !strncmp(node->domain_obj.legacy_domain_name, name, cmp_len)) {
			*out = node->domain_obj;
			found = true;
			break;
		}
		/* Match the raw /dev basename ("cdsp2") -- a dynamic-only instance
		 * has no legacy alias and a synthetic name ("nsp1002") unrelated
		 * to its device node name, so callers who naturally refer to it by
		 * the name matching its /dev node (e.g. per-instance daemon units)
		 * need this to resolve at all. */
		if (node->domain_obj.dev_basename[0] &&
		    strlen(node->domain_obj.dev_basename) == cmp_len &&
		    !strncmp(node->domain_obj.dev_basename, name, cmp_len)) {
			*out = node->domain_obj;
			found = true;
			break;
		}
	}
	pthread_mutex_unlock(&g_domain_table.mut);
	return found;
}

bool fastrpc_get_domain_by_logical_id(int logical_id, fastrpc_domain_internal *out)
{
	struct fastrpc_domain_hash_node *node;
	bool found = false;

	if (!out)
		return false;

	pthread_mutex_lock(&g_domain_table.mut);
	HASH_FIND_INT(g_domain_table.tbl, &logical_id, node);
	if (node) {
		*out = node->domain_obj;
		found = true;
	}
	pthread_mutex_unlock(&g_domain_table.mut);
	return found;
}

int convert_legacy_domain_to_dynamic(int domain)
{
	struct fastrpc_domain_hash_node *node = NULL, *tmp = NULL;

	pthread_mutex_lock(&g_domain_table.mut);
	HASH_ITER(hh, g_domain_table.tbl, node, tmp) {
		if (node->domain_obj.legacy_domain_id == domain) {
			int lid = node->domain_obj.domain_info.id;
			pthread_mutex_unlock(&g_domain_table.mut);
			return lid;
		}
	}
	pthread_mutex_unlock(&g_domain_table.mut);
	return -1;
}

/*
 * Legacy formula: effec = domain + NUM_DOMAINS * session.
 * Dynamic formula: effec = logical_id * EFFEC_DOMAIN_ID_MULTIPLIER + session.
 */
int convert_legacy_effec_domain_to_dynamic(int effec_domain)
{
	int legacy_domain_id, sess_id, dynamic_domain_id;

	/* Legacy formula: effec = domain + NUM_DOMAINS * session */
	legacy_domain_id = effec_domain % NUM_DOMAINS;
	sess_id          = effec_domain / NUM_DOMAINS;

	dynamic_domain_id = convert_legacy_domain_to_dynamic(legacy_domain_id);
	if (dynamic_domain_id < 0)
		return effec_domain; /* no dynamic equivalent — keep legacy */

	return GET_DYNAMIC_EFFEC_DOMAIN_ID(dynamic_domain_id, sess_id);
}

/* True if logical_id's /dev node basename belongs to the adsp family
 * ("adsp", "adsp1", ...) -- the only family whose type is a capability,
 * not a name, so the only one classify_adsp_domain() needs to touch. */
static bool fastrpc_is_adsp_family_domain(const fastrpc_domain_internal *d)
{
	return !strncmp(d->dev_basename, "adsp", 4);
}

/*
 * adsp-family domains are discovered defaulting to FASTRPC_LPASS (the
 * family table's default) -- LPASS vs HPASS is a capability, not a name:
 * HVX 64B/128B support + ARCH_VER >= v81 means the instance is HPASS;
 * otherwise it stays LPASS. fastrpc_get_cap() opens/closes its own session
 * internally for these attributes, so no pre-existing session is needed.
 */
static bool fastrpc_adsp_domain_supports_hvx(int effec_domain)
{
	uint32_t hvx64 = 0, hvx128 = 0;

	fastrpc_get_cap((uint32_t)effec_domain, HVX_SUPPORT_64B, &hvx64);
	fastrpc_get_cap((uint32_t)effec_domain, HVX_SUPPORT_128B, &hvx128);

	return hvx64 != 0 || hvx128 != 0;
}

/* Resolves logical_id's real type exactly once, the first time it's seen
 * still unclassified -- callers must not hold g_domain_table.mut, since
 * this takes it itself via fastrpc_get_domain_by_logical_id()/
 * fastrpc_set_domain_type(). */
static void fastrpc_classify_adsp_domain(int logical_id)
{
	fastrpc_domain_internal d;
	int effec_domain;
	uint32_t arch_cap = 0;
	bool is_hpass;

	if (!fastrpc_get_domain_by_logical_id(logical_id, &d) ||
	    d.type_classified || !fastrpc_is_adsp_family_domain(&d))
		return;

	effec_domain = GET_DYNAMIC_EFFEC_DOMAIN_ID(logical_id, 0);

	if (fastrpc_get_cap((uint32_t)effec_domain, ARCH_VER, &arch_cap) != AEE_SUCCESS)
		return;

	is_hpass = fastrpc_adsp_domain_supports_hvx(effec_domain) &&
		   (arch_cap & 0xFF) >= 0x81;
	fastrpc_set_domain_type(logical_id, is_hpass ? FASTRPC_HPASS : FASTRPC_LPASS);
}

/* Reclassifies a physical domain's type (e.g. adsp-family LPASS vs HPASS,
 * resolved from capability queries at domain-init time, not from name). */
void fastrpc_set_domain_type(int logical_id, fastrpc_domain_type type)
{
	struct fastrpc_domain_hash_node *node = domain_table_find(logical_id);

	if (!node)
		return;

	pthread_mutex_lock(&g_domain_table.mut);
	node->domain_obj.domain_info.type = type;
	node->domain_obj.type_classified = true;
	pthread_mutex_unlock(&g_domain_table.mut);
}

static int fastrpc_get_domains(fastrpc_domains_info *info)
{
	struct fastrpc_domain_hash_node *node = NULL, *tmp = NULL;
	fastrpc_domain_type filter;
	int count = 0;
	int unclassified[FASTRPC_MAX_DISCOVERED_DOMAINS];
	int unclassified_count = 0, i;

	if (!info)
		return AEE_EBADPARM;

	/* domains == NULL with max_domains > 0 is an inconsistent buffer-size
	 * claim, and vice versa -- reject both. */
	if (!info->domains && info->max_domains > 0)
		return AEE_EBADPARM;
	if (info->domains && !info->max_domains)
		return AEE_EBADPARM;

	/* Re-probe to catch domains that came up after init, or SSR status. */
	fastrpc_discover_domains();

	/*
	 * Resolve any still-unclassified adsp-family domain's real type
	 * before anything below reads domain_info.type, so a caller of
	 * remote_system_request(FASTRPC_GET_DOMAINS) never observes the
	 * family-table placeholder. Collected under the lock, classified
	 * outside it (see fastrpc_classify_adsp_domain()'s locking note).
	 */
	pthread_mutex_lock(&g_domain_table.mut);
	HASH_ITER(hh, g_domain_table.tbl, node, tmp) {
		if (!node->domain_obj.type_classified &&
		    fastrpc_is_adsp_family_domain(&node->domain_obj) &&
		    unclassified_count < FASTRPC_MAX_DISCOVERED_DOMAINS)
			unclassified[unclassified_count++] = node->domain;
	}
	pthread_mutex_unlock(&g_domain_table.mut);

	for (i = 0; i < unclassified_count; i++)
		fastrpc_classify_adsp_domain(unclassified[i]);

	filter = (fastrpc_domain_type)
		(info->flags & ((1 << DOMAINS_LIST_FLAGS_TYPE_POS) - 1));

	pthread_mutex_lock(&g_domain_table.mut);
	HASH_ITER(hh, g_domain_table.tbl, node, tmp) {
		fastrpc_domain_type t = node->domain_obj.domain_info.type;

		if (filter != FASTRPC_ALL_DOMAINS && filter != t)
			continue;

		if (info->domains && count < info->max_domains) {
			fastrpc_domain *d = &info->domains[count];
			*d = node->domain_obj.domain_info; /* copy */
		}
		count++;
	}
	pthread_mutex_unlock(&g_domain_table.mut);

	info->num_domains = count;

	if (info->domains && info->max_domains < count)
		return AEE_EBADPARM;

	return AEE_SUCCESS;
}

/* Caches DSP_IMAGE_CONFIG per physical domain; queries the kernel once. */
uint32_t fastrpc_get_cached_image_config(int logical_id, int dev)
{
    struct fastrpc_domain_hash_node *node = domain_table_find(logical_id);

    if (!node) {
        FARF(ALWAYS,
             "%s: logical_id %d not in Level-1 table — returning 0 (unknown)",
             __func__, logical_id);
        return 0;
    }

    /* dsp_image_config is a node field -- only safe to touch under the
     * table lock, same as domain_info (see fastrpc_discover_domains()).
     */
    pthread_mutex_lock(&g_domain_table.mut);
    uint32_t cached_cfg = node->domain_obj.dsp_image_config;
    pthread_mutex_unlock(&g_domain_table.mut);

    /* Cache hit: return immediately without any kernel call */
    if (cached_cfg != 0) {
        FARF(RUNTIME_RPC_HIGH,
             "%s: cache hit for logical_id %d → dsp_image_config=%u",
             __func__, logical_id, cached_cfg);
        return cached_cfg;
    }

    /* Cache miss: query kernel via ioctl_getdspinfo() */
    if (dev < 0) {
        FARF(ALWAYS,
             "%s: cache miss for logical_id %d but dev=%d — cannot query",
             __func__, logical_id, dev);
        return 0;
    }

    uint32_t cfg = 0;
    int nErr = ioctl_getdspinfo(dev, logical_id, DSP_IMAGE_CONFIG, &cfg);
    if (nErr != AEE_SUCCESS || cfg == 0) {
        FARF(ALWAYS,
             "Warning 0x%x: %s: DSP_IMAGE_CONFIG ioctl failed for "
             "logical_id %d dev %d — defaulting to 0 (unknown)",
             nErr, __func__, logical_id, dev);
        return 0;
    }

    /* Store in Level-1 — shared across ALL sessions of this physical domain */
    char name[MAX_DOMAIN_NAMELEN];
    pthread_mutex_lock(&g_domain_table.mut);
    node->domain_obj.dsp_image_config = cfg;
    strlcpy(name, node->domain_obj.domain_info.name, sizeof(name));
    pthread_mutex_unlock(&g_domain_table.mut);

    const char *cfg_str =
        (cfg == FASTRPC_DSP_COMPUTE)       ? "COMPUTE" :
        (cfg == FASTRPC_DSP_COMPUTE_LITE)  ? "COMPUTE_LITE" :
        (cfg == FASTRPC_DSP_AUDIO)         ? "AUDIO" :
        "UNKNOWN";
    FARF(ALWAYS,
         "%s: cached DSP_IMAGE_CONFIG for logical_id %d (%s): %u → %s",
         __func__, logical_id, name, cfg, cfg_str);

    return cfg;
}

/*
 * Returns the exact shell path that worked last time for this physical
 * domain's signed (unsigned_shell=0) or unsigned (unsigned_shell=1) shell,
 * if one was recorded -- avoids re-walking DSP_LIBS_LOCATION/VENDOR_DSP_LOCATION
 * on every session of a domain whose shell isn't at the default location.
 * False (out untouched) if nothing cached yet.
 */
bool fastrpc_get_cached_shell_path(int logical_id, int unsigned_shell,
                                   char *out, size_t out_len)
{
	struct fastrpc_domain_hash_node *node = domain_table_find(logical_id);
	int idx = unsigned_shell ? 1 : 0;
	bool found = false;

	if (!node || !out)
		return false;

	pthread_mutex_lock(&g_domain_table.mut);
	if (node->domain_obj.shell_path[idx][0] != '\0') {
		strlcpy(out, node->domain_obj.shell_path[idx], out_len);
		found = true;
	}
	pthread_mutex_unlock(&g_domain_table.mut);
	return found;
}

/* Caches the exact shell path that worked for a physical domain, separately
 * for the signed/unsigned shell variant (a client can run different
 * sessions of the same domain as signed vs unsigned). */
void fastrpc_set_cached_shell_path(int logical_id, int unsigned_shell,
                                   const char *path)
{
	struct fastrpc_domain_hash_node *node = domain_table_find(logical_id);
	int idx = unsigned_shell ? 1 : 0;

	if (!node) {
		FARF(ALWAYS,
		     "%s: logical_id %d not in Level-1 table — not caching shell path",
		     __func__, logical_id);
		return;
	}

	pthread_mutex_lock(&g_domain_table.mut);
	strlcpy(node->domain_obj.shell_path[idx], path,
		sizeof(node->domain_obj.shell_path[idx]));
	pthread_mutex_unlock(&g_domain_table.mut);

	FARF(ALWAYS, "%s: cached %s shell path for logical_id %d: %s",
	     __func__, unsigned_shell ? "unsigned" : "signed", logical_id, path);
}

int remote_system_request(system_req_payload *req)
{
	int nErr = AEE_SUCCESS;

	if (!req)
		return AEE_EBADPARM;

	/* Ensures the Level-1 table is populated before the first query. */
	VERIFY(AEE_SUCCESS == (nErr = fastrpc_init_once()));


	switch (req->id) {
	case FASTRPC_GET_DOMAINS:
		return fastrpc_get_domains(&req->sys);

	case FASTRPC_REGISTER_DOMAIN_STATUS_NOTIFICATIONS:
		/*
		 * Register a callback for domain up/down events. Registration
		 * is accepted unconditionally. Callbacks fire during the
		 * re-probe in fastrpc_get_domains() when status changes.
		 */
		return fastrpc_register_domain_status_notif(&req->domain_notif);

	case FASTRPC_DEREGISTER_DOMAIN_STATUS_NOTIFICATIONS:
		return fastrpc_deregister_domain_status_notif(&req->domain_notif);

	default:
		return AEE_EBADPARM;
	}

bail:
	return nErr;
}
