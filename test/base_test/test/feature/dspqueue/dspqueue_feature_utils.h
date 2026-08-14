// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file dspqueue_feature_utils.h
 * @brief Shared utilities for dspqueue feature tests
 */

#ifndef DSPQUEUE_FEATURE_UTILS_H
#define DSPQUEUE_FEATURE_UTILS_H

#include "AEEStdErr.h"
#include "fastrpc_test.h"
#include "remote.h"
#include "test_utils.h"
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Global state for dspqueue feature tests
 *
 * dspqueue_rpc_handle is opened by dspqueue_feature_init() and held open
 * for the lifetime of the test group.  It serves two purposes:
 *
 *   1. Spawning the DSP PD.  On mainline the driver does not auto-create a
 *      PD session; remote_handle64_open() (called by fastrpc_test_open()) is
 *      the trigger.  Without an open handle, fastrpc_mmap() returns
 *      AEE_ENOTINITIALIZED (0x6B) and dspqueue_create() returns AEE_EFAILED
 *      (0x02) because the PD is not yet up.
 *
 *   2. Providing the fastrpc_test_dspqueue_start / _stop entry points used
 *      by tests that exercise the full CPU<->DSP queue round-trip.
 *
 * dspqueue_create() reuses the same PD session that fastrpc_test_open()
 * established — it does NOT open a second concurrent session, so there is
 * no session-limit collision.
 */
typedef struct {
    remote_handle64 dspqueue_rpc_handle; /**< fastrpc_test handle; 0 when closed */
    int initialized;                     /**< 1 after init: PD spawned, handle open */
    int dsp_started;                     /**< 1 after dspqueue_start, 0 after dspqueue_stop */
    int domain_id;                       /**< DSP domain ID set by dspqueue_feature_init() */
} dspqueue_feature_state_t;

/**
 * @brief Global dspqueue feature state
 */
extern dspqueue_feature_state_t g_dspqueue_feature_state;

/**
 * @brief Initialize the dspqueue feature test environment
 * @param domain_id DSP domain ID
 * @return AEE_SUCCESS on success, error code on failure
 *
 * Performs three steps:
 *   1. Validates the domain ID.
 *   2. Calls remote_session_control(DSPRPC_CONTROL_UNSIGNED_MODULE) so the
 *      domain will accept unsigned skels.
 *   3. Opens libfastrpc_test_skel.so via fastrpc_test_open() to spawn the
 *      DSP PD and stores the handle in g_dspqueue_feature_state.
 *
 * The handle MUST remain open for the duration of the test group.
 * On mainline the driver does not auto-create a PD; without an open handle
 * fastrpc_mmap() returns AEE_ENOTINITIALIZED (0x6B) and dspqueue_create()
 * returns AEE_EFAILED (0x02).  dspqueue_create() reuses the PD session
 * already established by fastrpc_test_open() — it does not open a second
 * concurrent session, so there is no session-limit collision.
 *
 * Call dspqueue_feature_cleanup() in TEST_TEAR_DOWN to close the handle.
 */
int dspqueue_feature_init(int domain_id);

/**
 * @brief Cleanup the DSP-side dspqueue RPC service
 * @return AEE_SUCCESS on success, error code on failure
 *
 * This function should be called after all dspqueue tests complete.
 * It closes the DSP-side dspqueue RPC service and frees resources.
 */
int dspqueue_feature_cleanup(void);

/**
 * @brief Check if dspqueue service is available
 * @return 1 if available, 0 if not
 */
static inline int dspqueue_feature_is_available(void)
{
    return g_dspqueue_feature_state.initialized;
}

/**
 * @brief Open a handle to the fastrpc_test module for dspqueue testing
 * @param domain_id DSP domain ID
 * @param handle Output handle
 * @return AEE_SUCCESS on success
 *
 * Builds the URI as fastrpc_test_URI + "&_dom=<name>" (e.g. "&_dom=cdsp").
 * The domain suffix must be a name string, not an integer — passing %d to
 * _dom produces an invalid URI that the FastRPC driver rejects.
 */
static inline int dspqueue_feature_open_handle(int domain_id, remote_handle64 *handle)
{
    char uri[512];
    const char *domain_suffix;
    switch (domain_id) {
    case ADSP_DOMAIN_ID:
        domain_suffix = ADSP_DOMAIN;
        break;
    case MDSP_DOMAIN_ID:
        domain_suffix = MDSP_DOMAIN;
        break;
    case SDSP_DOMAIN_ID:
        domain_suffix = SDSP_DOMAIN;
        break;
    case CDSP_DOMAIN_ID:
        domain_suffix = CDSP_DOMAIN;
        break;
    case CDSP1_DOMAIN_ID:
        domain_suffix = CDSP1_DOMAIN;
        break;
    case GDSP0_DOMAIN_ID:
        domain_suffix = GDSP0_DOMAIN;
        break;
    case GDSP1_DOMAIN_ID:
        domain_suffix = GDSP1_DOMAIN;
        break;
    default:
        domain_suffix = CDSP_DOMAIN;
        break;
    }
    snprintf(uri, sizeof(uri), "%s%s", fastrpc_test_URI, domain_suffix);
    return fastrpc_test_open(uri, handle);
}

/**
 * @brief Close the fastrpc_test module handle
 * @param handle Handle to close
 * @return AEE_SUCCESS on success
 */
static inline int dspqueue_feature_close_handle(remote_handle64 handle)
{
    return fastrpc_test_close(handle);
}

/**
 * @brief Start the DSP-side dspqueue service
 * @param handle fastrpc_test handle
 * @param dsp_queue_id Queue ID from dspqueue_export()
 * @return AEE_SUCCESS on success
 */
static inline int dspqueue_feature_start(remote_handle64 handle, uint64_t dsp_queue_id)
{
    return fastrpc_test_dspqueue_start(handle, dsp_queue_id);
}

/**
 * @brief Stop the DSP-side dspqueue service
 * @param handle fastrpc_test handle
 * @param process_time Output: total DSP processing time in microseconds
 * @return AEE_SUCCESS on success
 */
static inline int dspqueue_feature_stop(remote_handle64 handle, uint64_t *process_time)
{
    return fastrpc_test_dspqueue_stop(handle, process_time);
}

#ifdef __cplusplus
}
#endif

#endif /* DSPQUEUE_FEATURE_UTILS_H */
