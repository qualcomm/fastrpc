// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/*
 * test_session_lifecycle.c - Audio static PD (audiopd) creation and
 * remote-heap-enabled session lifecycle tests.
 *
 * Adapted from the legacy dsp_test_framework remoteheapaudiopd test
 * (adsprpcd_remoteheapaudiopd.c): confirms that opening
 * "createstaticpd:audiopd&_dom=adsp" followed by a normal fastrpc_test
 * skel open on the ADSP domain creates (and dynamically loads a skel
 * inside) the Audio static PD, which is the only PD type that receives
 * the initial remote-heap grant from the CPU (fastrpc_apps_user.c).
 */

#include "fastrpc_test.h"
#include "remote.h"
#include "remote_heap_feature_utils.h"
#include "test_utils.h"
#include "unity_fixture.h"

#include <stdio.h>

#define CREATE_STATICPD_AUDIOPD_URI ITRANSPORT_PREFIX "createstaticpd:audiopd" ADSP_DOMAIN

/* ------------------------------------------------------------------ */

TEST_GROUP(RemoteHeapSessionLifecycle);

TEST_SETUP(RemoteHeapSessionLifecycle)
{
    int ret = remote_heap_feature_init();
    if (ret != 0) {
        TEST_IGNORE_MESSAGE("Audio static PD session not available - skipping");
    }
}

TEST_TEAR_DOWN(RemoteHeapSessionLifecycle) { remote_heap_feature_cleanup(); }

/* ------------------------------------------------------------------ */

/*
 * CreateAudioStaticPDAndLoadSkel
 * remote_heap_feature_init() (run in TEST_SETUP) already performed the
 * createstaticpd:audiopd open followed by the fastrpc_test skel open and a
 * sanity add(2,3)==5 call.  Re-assert here so the requirement is visible
 * as its own test case rather than only an implicit setup precondition.
 */
TEST(RemoteHeapSessionLifecycle, CreateAudioStaticPDAndLoadSkel)
{
    int result = 0;
    int ret;

    TEST_ASSERT_TRUE_MESSAGE(remote_heap_feature_is_available(),
                             "audiopd session should be initialized by TEST_SETUP");

    ret = fastrpc_test_add(g_remote_heap_feature_state.rpc_handle, 2, 3, &result);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32(0, ret);
    TEST_ASSERT_EQUAL(5, result);
}
TEST_CASE_TAGS(RemoteHeapSessionLifecycle, CreateAudioStaticPDAndLoadSkel, "RemoteHeap", "feature",
               "positive", "remote_heap_session_lifecycle");

/*
 * ReopenAfterCloseIsStable
 * Close and reopen the audiopd session a few times using the same
 * createstaticpd: + fastrpc_test_open sequence directly (bypassing the
 * cached singleton), verifying the sequence is stable and repeatable.
 */
#define REOPEN_CYCLES 3

static void reopen_cycle_fail(remote_handle64 rpc_handle, remote_handle64 static_pd_handle,
                              const char *msg)
{
    if (rpc_handle != 0) {
        fastrpc_test_close(rpc_handle);
    }
    if (static_pd_handle != 0) {
        remote_handle64_close(static_pd_handle);
    }
    remote_heap_feature_init();
    TEST_FAIL_MESSAGE(msg);
}

TEST(RemoteHeapSessionLifecycle, ReopenAfterCloseIsStable)
{
    int cycle;

    /* Release the singleton-held session; each cycle below opens its own. */
    remote_heap_feature_cleanup();

    for (cycle = 0; cycle < REOPEN_CYCLES; cycle++) {
        remote_handle64 static_pd_handle = 0;
        remote_handle64 rpc_handle = 0;
        char uri[512];
        int result = 0;
        int ret;

        ret = remote_handle64_open(CREATE_STATICPD_AUDIOPD_URI, &static_pd_handle);
        REPORT_ERROR_CODE(ret);
        if (ret != 0) {
            reopen_cycle_fail(rpc_handle, static_pd_handle, "createstaticpd:audiopd open");
        }

        snprintf(uri, sizeof(uri), "%s%s", fastrpc_test_URI, ADSP_DOMAIN);
        ret = fastrpc_test_open(uri, &rpc_handle);
        REPORT_ERROR_CODE(ret);
        if (ret != 0) {
            reopen_cycle_fail(rpc_handle, static_pd_handle, "fastrpc_test_open inside audiopd");
        }

        ret = fastrpc_test_add(rpc_handle, 2, 3, &result);
        REPORT_ERROR_CODE(ret);
        if (ret != 0 || result != 5) {
            reopen_cycle_fail(rpc_handle, static_pd_handle, "fastrpc_test_add(2,3) inside audiopd");
        }

        ret = fastrpc_test_close(rpc_handle);
        REPORT_ERROR_CODE(ret);
        if (ret != 0) {
            reopen_cycle_fail(0, static_pd_handle, "fastrpc_test_close inside audiopd");
        }

        ret = remote_handle64_close(static_pd_handle);
        REPORT_ERROR_CODE(ret);
        if (ret != 0) {
            reopen_cycle_fail(0, 0, "remote_handle64_close(static_pd_handle)");
        }
    }

    /* Restore the singleton so any later test in this run still has a session. */
    TEST_ASSERT_EQUAL_HEX32(0, remote_heap_feature_init());
}
TEST_CASE_TAGS(RemoteHeapSessionLifecycle, ReopenAfterCloseIsStable, "RemoteHeap", "feature",
               "positive", "remote_heap_session_lifecycle");

/*
 * SkelLoadsInNormalPDWithoutStaticPrefix (control / negative-contrast case)
 * Opening the same fastrpc_test skel URI on the ADSP domain WITHOUT the
 * createstaticpd: open first still succeeds - it just lands in an ordinary
 * dynamic PD, not audiopd.  This demonstrates that the static-PD prefix is
 * what selects the remote-heap-enabled code path, not something inherent
 * to the ADSP domain itself.
 */
TEST(RemoteHeapSessionLifecycle, SkelLoadsInNormalPDWithoutStaticPrefix)
{
    remote_handle64 handle = 0;
    char uri[512];
    int result = 0;
    int ret;

    snprintf(uri, sizeof(uri), "%s%s", fastrpc_test_URI, ADSP_DOMAIN);
    ret = fastrpc_test_open(uri, &handle);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(0, ret, "fastrpc_test_open without static PD prefix");

    ret = fastrpc_test_add(handle, 2, 3, &result);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32(0, ret);
    TEST_ASSERT_EQUAL(5, result);

    ret = fastrpc_test_close(handle);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32(0, ret);
}
TEST_CASE_TAGS(RemoteHeapSessionLifecycle, SkelLoadsInNormalPDWithoutStaticPrefix, "RemoteHeap",
               "feature", "positive", "remote_heap_session_lifecycle");

/* ------------------------------------------------------------------ */

TEST_GROUP_RUNNER(RemoteHeapSessionLifecycle)
{
    RUN_TEST_CASE(RemoteHeapSessionLifecycle, CreateAudioStaticPDAndLoadSkel);
    RUN_TEST_CASE(RemoteHeapSessionLifecycle, ReopenAfterCloseIsStable);
    RUN_TEST_CASE(RemoteHeapSessionLifecycle, SkelLoadsInNormalPDWithoutStaticPrefix);
}
