// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file dspqueue_feature_utils.c
 * @brief Implementation of shared utilities for dspqueue feature tests
 */

#include "dspqueue_feature_utils.h"
#include "fastrpc_test.h"
#include "test_utils.h"
#include <stdio.h>
#include <string.h>

/* Global state for dspqueue feature tests */
dspqueue_feature_state_t g_dspqueue_feature_state = {
    .dspqueue_rpc_handle = 0,
    .initialized = 0,
    .dsp_started = 0,
    .domain_id = -1,
};

int dspqueue_feature_init(int domain_id)
{
    int ret = AEE_SUCCESS;
    char uri[512];

    if (g_dspqueue_feature_state.initialized) {
        printf("[dspqueue-feature] Already initialized for domain %d\n",
               g_dspqueue_feature_state.domain_id);
        fflush(stdout);
        return AEE_SUCCESS;
    }

    printf("[dspqueue-feature] Initializing dspqueue feature for domain %d\n", domain_id);
    fflush(stdout);

    /*
     * Step 1: Resolve the URI for this domain ID.
     * The FastRPC driver requires a name string (e.g. "&_dom=cdsp"),
     * not an integer, in the URI.
     */
    test_utils_domain_uri_for(domain_id, uri, sizeof(uri));

    /*
     * Step 2: Enable unsigned PD.
     * Must happen before remote_handle64_open() on this domain.
     * Non-zero return is a warning, not a hard error.
     */
    ret = test_utils_enable_unsigned_pd(domain_id);
    if (ret != AEE_SUCCESS) {
        printf("[dspqueue-feature] WARNING: enable_unsigned_pd returned 0x%x (%s) "
               "- continuing\n",
               ret, test_utils_err_str(ret));
        fflush(stdout);
    }

    /*
     * Step 3: Open libfastrpc_test_skel.so to spawn the DSP PD.
     *
     * Now that dspqueue_rpc_skel has been released, this open creates a
     * fresh PD.  All subsequent dspqueue_create() calls in the test body
     * will open dspqueue_rpc_skel in this same PD, so dspqueue_import()
     * can find the queue registered by dspqueue_rpc_create_queue.
     */
    printf("[dspqueue-feature] Opening URI: %s\n", uri);
    fflush(stdout);

    ret = fastrpc_test_open(uri, &g_dspqueue_feature_state.dspqueue_rpc_handle);
    if (ret != AEE_SUCCESS) {
        printf("[dspqueue-feature] ERROR: fastrpc_test_open failed: 0x%x (%s)\n", ret,
               test_utils_err_str(ret));
        fflush(stdout);
        return ret;
    }

    printf("[dspqueue-feature] DSP PD spawned (handle: 0x%llx)\n",
           (unsigned long long)g_dspqueue_feature_state.dspqueue_rpc_handle);
    fflush(stdout);

    g_dspqueue_feature_state.initialized = 1;
    g_dspqueue_feature_state.domain_id = domain_id;
    return AEE_SUCCESS;
}

int dspqueue_feature_cleanup(void)
{
    if (!g_dspqueue_feature_state.initialized) {
        return AEE_SUCCESS;
    }

    /*
     * Safety net: if a test called dspqueue_start but failed before
     * dspqueue_stop (e.g. assertion mid-test), the DSP-side queue handle
     * is still open.  The next test's dspqueue_start returns AEE_EBADSTATE.
     * Always stop here if the flag is set.
     */
    if (g_dspqueue_feature_state.dsp_started) {
        uint64_t t;
        fastrpc_test_dspqueue_stop(g_dspqueue_feature_state.dspqueue_rpc_handle, &t);
        g_dspqueue_feature_state.dsp_started = 0;
    }

    /* Close the fastrpc_test handle opened in dspqueue_feature_init(). */
    if (g_dspqueue_feature_state.dspqueue_rpc_handle != 0) {
        fastrpc_test_close(g_dspqueue_feature_state.dspqueue_rpc_handle);
        g_dspqueue_feature_state.dspqueue_rpc_handle = 0;
    }

    g_dspqueue_feature_state.initialized = 0;
    g_dspqueue_feature_state.domain_id = -1;
    return AEE_SUCCESS;
}
