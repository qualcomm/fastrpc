// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/*
 * Level-1 domain table, status-notification registry, and
 * remote_system_request(). Generic: this file has no knowledge of *how*
 * domains are found -- that's the job of a discovery backend (today,
 * fastrpc_domain_discovery_devnode.c, which probes /dev for "fastrpc-*"
 * nodes) built on top of the generic watcher thread in
 * fastrpc_domain_watcher.c. Kept generic so a discovery backend based on a
 * different source (e.g. a sysfs tree) can be swapped in without touching
 * this file.
 */

#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "AEEStdErr.h"
#include "AEEstd.h"
#include "AEEQList.h"
#include "remote.h"
#include "fastrpc_ioctl.h"
#include "fastrpc_internal.h"
#include "fastrpc_hash_table.h"
#include "fastrpc_domain_watcher.h"

/* Level-1 domain hash table. Key = logical_id (1000, 5001, etc.). Declared
 * extern in fastrpc_internal.h for the watcher thread and discovery
 * backends. */
fastrpc_domain_table g_domain_table;

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

struct fastrpc_domain_hash_node *domain_table_add(int logical_id)
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

struct fastrpc_domain_hash_node *domain_table_find(int logical_id)
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
	if (!QList_IsEmpty(&domain_status_notif_list)) {
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

	VERIFYC(NULL != (node = calloc(1, sizeof(*node))), AEE_ENOMEMORY);
	QNode_CtorZ(&node->qn);
	node->domain_id   = info->domain_id;
	node->domain_type = (info->domain_id == -1) ? info->domain_type
	                                             : FASTRPC_ALL_DOMAINS;
	node->context     = info->context;
	node->notifier_fn = info->notifier_fn;

	pthread_mutex_lock(&domain_status_notif_mut);
	QList_AppendNode(&domain_status_notif_list, &node->qn);
	pthread_mutex_unlock(&domain_status_notif_mut);

	fastrpc_domains_table_lock();
	HASH_ITER(hh, g_domain_table.tbl, dnode, tmp) {
		if (snapshot_count >= FASTRPC_MAX_DISCOVERED_DOMAINS)
			break;
		if (fastrpc_domain_filter_matches(node->domain_id, node->domain_type,
		                                  &dnode->domain_obj.domain_info))
			snapshot[snapshot_count++] = dnode->domain_obj.domain_info;
	}
	fastrpc_domains_table_unlock();

	for (i = 0; i < snapshot_count; i++)
		node->notifier_fn(node->context, &snapshot[i]);
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
	fastrpc_domain_watcher_deinit();
	HASH_TABLE_OBJ_CLEANUP(struct fastrpc_domain_hash_node, g_domain_table);
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

static int fastrpc_get_domains(fastrpc_domains_info *info)
{
	struct fastrpc_domain_hash_node *node = NULL, *tmp = NULL;
	fastrpc_domain_type filter;
	int count = 0;

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

/* Caches an arch-specific DSP library search path per physical domain,
 * resolved by the caller from ARCH_VER at domain-init time. */
void fastrpc_set_cached_dsp_search_path(int logical_id, const char *path)
{
	struct fastrpc_domain_hash_node *node = domain_table_find(logical_id);

	if (!node) {
		FARF(ALWAYS,
		     "%s: logical_id %d not in Level-1 table — not caching search path",
		     __func__, logical_id);
		return;
	}

	/* dsp_search_path is a node field -- only safe to touch under the
	 * table lock, same as dsp_image_config/domain_info.
	 */
	pthread_mutex_lock(&g_domain_table.mut);
	strlcpy(node->domain_obj.dsp_search_path, path,
		sizeof(node->domain_obj.dsp_search_path));
	pthread_mutex_unlock(&g_domain_table.mut);

	FARF(ALWAYS, "%s: cached DSP search path for logical_id %d: %s",
	     __func__, logical_id, path);
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
