// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef FASTRPC_DOMAIN_WATCHER_H
#define FASTRPC_DOMAIN_WATCHER_H

#include <stdbool.h>

/*
 * Generic domain-availability watcher: a background thread that detects
 * domains appearing/disappearing and triggers a rescan, using inotify when
 * available and falling back to polling. The thread mechanics (inotify
 * setup, polling fallback, exit signaling, the ueventd/SELinux label-race
 * retry) don't depend on *how* domains are found -- only the watched
 * directory and the rescan/event callbacks do, so those are supplied by the
 * discovery backend through this config. Kept generic so a future discovery
 * backend (e.g. one that watches a different directory) can reuse this
 * watcher as-is instead of duplicating the thread logic.
 */
typedef struct {
    const char *watch_dir;          /* directory to inotify-watch for create/delete */
    const char *event_name_prefix;  /* only react to inotify events whose name starts
                                      * with this; NULL/empty reacts to all events */
    int  (*rescan_fn)(void);        /* full rescan of the watched source */
    void (*handle_event_fn)(const char *name, bool appeared); /* incremental
                                      * update for one inotify create/delete event */
} fastrpc_domain_watcher_config_t;

/*
 * Starts the watcher thread. Callers are responsible for ensuring this runs
 * at most once per process (e.g. via pthread_once()) -- it is not itself
 * idempotent. *cfg is copied by value, so it need not outlive this call.
 */
void fastrpc_domain_watcher_init(const fastrpc_domain_watcher_config_t *cfg);

/* Stops the watcher thread and releases its resources. Safe to call even if
 * the watcher was never started. */
void fastrpc_domain_watcher_deinit(void);

/* Marks that at least one domain has been seen, so the watcher can start
 * its quiet-period exit countdown. */
void fastrpc_domain_watcher_mark_domains_exist(void);

#endif /* FASTRPC_DOMAIN_WATCHER_H */
