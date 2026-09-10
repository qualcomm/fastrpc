// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file remote_heap_feature_utils.h
 * @brief Shared utilities for remote_heap feature tests
 */

#ifndef REMOTE_HEAP_FEATURE_UTILS_H
#define REMOTE_HEAP_FEATURE_UTILS_H

#include "fastrpc_test.h"
#include "remote.h"
#include "test_utils.h"
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Global state for remote_heap feature tests
 *
 * static_pd_handle is the sentinel handle returned by opening
 * "createstaticpd:audiopd&_dom=adsp" — it is NOT invokable (the driver
 * rejects remote_handle64_invoke() on it), it only tags the ADSP domain's
 * PD type as AUDIO_STATICPD for the process. The actual invokable handle
 * is rpc_handle, obtained by a subsequent fastrpc_test_open() on the same
 * domain, which lands inside the already-tagged audiopd static PD and
 * receives the 3MB initial remote-heap grant (fastrpc_apps_user.c).
 */
typedef struct {
    remote_handle64 static_pd_handle; /**< sentinel from createstaticpd: open; 0 when closed */
    remote_handle64 rpc_handle;       /**< fastrpc_test handle inside audiopd; 0 when closed */
    int initialized;                  /**< 1 after init: audiopd spawned, handle open */
} remote_heap_feature_state_t;

extern remote_heap_feature_state_t g_remote_heap_feature_state;

/**
 * @brief Initialize the remote_heap feature test environment
 * @return 0 on success, error code on failure
 *
 * Always targets ADSP_DOMAIN_ID — the Audio static PD is ADSP-only
 * (Docs/daemons.md), so g_test_config.domain_id / -d is intentionally
 * ignored here. Performs:
 *   1. Waits for the ADSP fastrpc device node.
 *   2. Enables unsigned PD on ADSP_DOMAIN_ID.
 *   3. Opens "createstaticpd:audiopd&_dom=adsp" to tag the domain's PD
 *      type as AUDIO_STATICPD.
 *   4. Opens libfastrpc_test_skel.so on the same domain via
 *      fastrpc_test_open() — this is what actually creates the static PD
 *      and dynamically loads the skel inside it.
 *   5. Sanity RPC: add(2,3) must return 5, confirming the skel is alive
 *      inside the static PD before any test group runs.
 *
 * Call remote_heap_feature_cleanup() in TEST_TEAR_DOWN to close handles.
 */
int remote_heap_feature_init(void);

/**
 * @brief Cleanup the remote_heap feature test environment
 * @return 0 on success, error code on failure
 */
int remote_heap_feature_cleanup(void);

/**
 * @brief Check if the remote_heap feature environment is available
 * @return 1 if available, 0 if not
 */
static inline int remote_heap_feature_is_available(void)
{
    return g_remote_heap_feature_state.initialized;
}

#ifdef __cplusplus
}
#endif

#endif /* REMOTE_HEAP_FEATURE_UTILS_H */
