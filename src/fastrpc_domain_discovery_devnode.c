// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/*
 * Discovery backend for the upstream kernel: domains are found by probing
 * /dev for "fastrpc-*" node presence instead of sysfs.
 * Naming: label + physical_id, physical_id = type*1000 + instance_id.
 *
 * Talks to the generic registry (fastrpc_domains.c) and the generic watcher
 * thread (fastrpc_domain_watcher.c) through their published APIs only, so a
 * different discovery backend (e.g. one that scans sysfs instead of /dev)
 * can be swapped in later without touching either of those.
 */

#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "AEEStdErr.h"
#include "AEEstd.h"
#include "remote.h"
#include "fastrpc_internal.h"
#include "fastrpc_domain_watcher.h"

#define FASTRPC_DEV_PREFIX  "fastrpc-"
#define FASTRPC_DEV_DIR     "/dev"

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
	{ "adsp", FASTRPC_HPASS, 0 },
	{ "cdsp", FASTRPC_NSP,   0 },
	{ "sdsp", FASTRPC_SDSP,  0 },
	{ "mdsp", FASTRPC_MDSP,  0 },
	{ "lpass", FASTRPC_LPASS, 0 },
	{ "gdsp", FASTRPC_NSP,   GDSP_INSTANCE_OFFSET },
};
#define FAMILY_TABLE_SIZE \
	((int)(sizeof(s_family_table) / sizeof(s_family_table[0])))

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
		fastrpc_domain_watcher_mark_domains_exist();

	/* Fire callbacks outside the table lock */
	fastrpc_notify_domain_status(domain_info);
}

static void fastrpc_devnode_watcher_start(void)
{
	static const fastrpc_domain_watcher_config_t cfg = {
		.watch_dir         = FASTRPC_DEV_DIR,
		.event_name_prefix = FASTRPC_DEV_PREFIX,
		.rescan_fn         = fastrpc_discover_domains,
		.handle_event_fn   = fastrpc_handle_dev_event,
	};

	fastrpc_domain_watcher_init(&cfg);
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
	pthread_once(&watcher_once, fastrpc_devnode_watcher_start);

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

		/* Fired outside the lock -- see fastrpc_notify_domain_status(). */
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
