// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file remote_heap_feature_utils.c
 * @brief Implementation of shared utilities for remote_heap feature tests
 */

#include "remote_heap_feature_utils.h"
#include "fastrpc_test.h"
#include "test_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CREATE_STATICPD_AUDIOPD_URI ITRANSPORT_PREFIX "createstaticpd:audiopd" ADSP_DOMAIN

/*
 * adsprpcd_audiopd.service is the udev-triggered system daemon that
 * attaches to the same Audio static PD this suite creates
 * (files/adsprpcd_audiopd.service.in -> "adsprpcd audiopd" ->
 * src/adsp_default_listener.c, the same createstaticpd:audiopd open this
 * suite performs). Stopping it for the duration of the test avoids the
 * daemon and the test racing to attach the audiopd session; it is
 * restarted once the test's own session is torn down.
 */
#define ADSPRPCD_AUDIOPD_SERVICE "adsprpcd_audiopd.service"

remote_heap_feature_state_t g_remote_heap_feature_state = {
    .static_pd_handle = 0,
    .rpc_handle = 0,
    .initialized = 0,
};

/*
 * Best-effort: a non-zero/failed systemctl call (missing binary, no
 * systemd, no root) is only logged, never treated as a hard test failure -
 * the audiopd session this suite creates does not depend on the daemon
 * being stopped, this just removes a source of attach-time contention.
 */
static void remote_heap_stop_audiopd_daemon(void)
{
    int ret;

    printf("[remote-heap-feature] Stopping %s to avoid audiopd attach contention\n",
           ADSPRPCD_AUDIOPD_SERVICE);
    fflush(stdout);

    ret = system("systemctl stop " ADSPRPCD_AUDIOPD_SERVICE " 2>/dev/null");
    if (ret != 0) {
        printf("[remote-heap-feature] WARNING: 'systemctl stop %s' returned %d - continuing\n",
               ADSPRPCD_AUDIOPD_SERVICE, ret);
        fflush(stdout);
    }
}

static void remote_heap_start_audiopd_daemon(void)
{
    int ret;

    printf("[remote-heap-feature] Restarting %s\n", ADSPRPCD_AUDIOPD_SERVICE);
    fflush(stdout);

    ret = system("systemctl start " ADSPRPCD_AUDIOPD_SERVICE " 2>/dev/null");
    if (ret != 0) {
        printf("[remote-heap-feature] WARNING: 'systemctl start %s' returned %d\n",
               ADSPRPCD_AUDIOPD_SERVICE, ret);
        fflush(stdout);
    }
}

int remote_heap_feature_init(void)
{
    int ret = 0;
    char uri[512];
    int result = 0;

    if (g_remote_heap_feature_state.initialized) {
        printf("[remote-heap-feature] Already initialized\n");
        fflush(stdout);
        return 0;
    }

    printf("[remote-heap-feature] Initializing remote_heap feature on ADSP domain\n");
    fflush(stdout);

    /*
     * Step 0: Stop the system audiopd daemon so it does not race the test
     * for the same createstaticpd:audiopd attach below.
     */
    remote_heap_stop_audiopd_daemon();

    /*
     * Step 1: Enable unsigned PD on ADSP.
     * Must happen before any remote_handle64_open()/fastrpc_test_open()
     * on this domain. Non-zero return is a warning, not a hard error.
     */
    ret = test_utils_enable_unsigned_pd(ADSP_DOMAIN_ID);
    if (ret != 0) {
        printf("[remote-heap-feature] WARNING: enable_unsigned_pd returned 0x%x (%s) "
               "- continuing\n",
               ret, test_utils_err_str(ret));
        fflush(stdout);
    }

    /*
     * Step 2: Tag the ADSP domain's PD type as AUDIO_STATICPD.
     *
     * This open does NOT create the static PD yet and the returned handle
     * is a non-invokable sentinel (AUDIOPD_HANDLE) — it only records, in
     * the process' per-domain state, that the *next* real open on this
     * domain should target the Audio static PD instead of a fresh dynamic
     * PD. See src/fastrpc_apps_user.c: remote_handle_open_domain().
     */
    printf("[remote-heap-feature] Opening %s\n", CREATE_STATICPD_AUDIOPD_URI);
    fflush(stdout);
    ret = remote_handle64_open(CREATE_STATICPD_AUDIOPD_URI,
                               &g_remote_heap_feature_state.static_pd_handle);
    if (ret != 0) {
        printf("[remote-heap-feature] ERROR: createstaticpd:audiopd open failed: 0x%x (%s)\n",
               ret, test_utils_err_str(ret));
        fflush(stdout);
        remote_heap_start_audiopd_daemon();
        return ret;
    }

    /*
     * Step 3: Open libfastrpc_test_skel.so on the same domain.
     *
     * Because the domain was just tagged AUDIO_STATICPD, this open is what
     * actually creates the Audio static PD on the DSP (with its initial 3MB
     * remote-heap grant) and dynamically loads our skel inside it, instead
     * of spawning a fresh ordinary dynamic PD.
     */
    snprintf(uri, sizeof(uri), "%s%s", fastrpc_test_URI, ADSP_DOMAIN);
    printf("[remote-heap-feature] Opening URI: %s\n", uri);
    fflush(stdout);

    ret = fastrpc_test_open(uri, &g_remote_heap_feature_state.rpc_handle);
    if (ret != 0) {
        printf("[remote-heap-feature] ERROR: fastrpc_test_open failed: 0x%x (%s)\n", ret,
               test_utils_err_str(ret));
        fflush(stdout);
        remote_handle64_close(g_remote_heap_feature_state.static_pd_handle);
        g_remote_heap_feature_state.static_pd_handle = 0;
        remote_heap_start_audiopd_daemon();
        return ret;
    }

    /*
     * Step 4: Sanity RPC to confirm the skel is alive inside the static PD
     * before any test group runs against it.
     */
    ret = fastrpc_test_add(g_remote_heap_feature_state.rpc_handle, 2, 3, &result);
    if (ret != 0 || result != 5) {
        printf("[remote-heap-feature] ERROR: sanity add(2,3) failed: ret=0x%x result=%d\n", ret,
               result);
        fflush(stdout);
        fastrpc_test_close(g_remote_heap_feature_state.rpc_handle);
        g_remote_heap_feature_state.rpc_handle = 0;
        remote_handle64_close(g_remote_heap_feature_state.static_pd_handle);
        g_remote_heap_feature_state.static_pd_handle = 0;
        remote_heap_start_audiopd_daemon();
        return (ret != 0) ? ret : AEE_EFAILED;
    }

    printf("[remote-heap-feature] Audio static PD ready (handle: 0x%llx)\n",
           (unsigned long long)g_remote_heap_feature_state.rpc_handle);
    fflush(stdout);

    g_remote_heap_feature_state.initialized = 1;
    return 0;
}

int remote_heap_feature_cleanup(void)
{
    if (!g_remote_heap_feature_state.initialized) {
        return 0;
    }

    if (g_remote_heap_feature_state.rpc_handle != 0) {
        fastrpc_test_close(g_remote_heap_feature_state.rpc_handle);
        g_remote_heap_feature_state.rpc_handle = 0;
    }

    if (g_remote_heap_feature_state.static_pd_handle != 0) {
        remote_handle64_close(g_remote_heap_feature_state.static_pd_handle);
        g_remote_heap_feature_state.static_pd_handle = 0;
    }

    remote_heap_start_audiopd_daemon();

    g_remote_heap_feature_state.initialized = 0;
    return 0;
}
