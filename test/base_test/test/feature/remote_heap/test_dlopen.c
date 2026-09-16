// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/*
 * test_dlopen.c - remote-heap dynamic loader feature tests.
 *
 * Exercises fastrpc_test_dlopen_stress() against a handle opened inside
 * the Audio static PD (audiopd).  Each DSP-side dlopen()/dlclose() cycle
 * exercises the dynamic loader's internal heap allocations, which for a
 * static PD are backed by the apps_mem remote-heap reverse-RPC path
 * (Docs/apps_mem.md).  Single-shot functional checks only, per project
 * convention that repeated-cycle stress variants belong in a separate
 * stress/perf test area.
 */

#include "fastrpc_test.h"
#include "remote_heap_feature_utils.h"
#include "test_utils.h"
#include "unity_fixture.h"

#include <stdint.h>
#include <stdio.h>

/*
 * Path the DSP-side dlopen() resolves against.  This is the on-target DSP
 * filesystem path libfastrpc_test_skel.so is deployed to (README.md's
 * "adb push builddir/bin/libfastrpc_test_skel.so /usr/lib/rfsa/adsp/"
 * step) - distinct from the CPU-side DSP_LIBRARY_PATH/SKEL_SEARCH_PATH
 * mechanism used for the *initial* CPU-initiated open.
 */
#define DLOPEN_TARGET_SO "/usr/lib/rfsa/adsp/libfastrpc_test_skel.so"
#define DLOPEN_NONEXISTENT_SO "/usr/lib/rfsa/adsp/no_such_remote_heap_test_lib.so"

/* ------------------------------------------------------------------ */

TEST_GROUP(RemoteHeapDynamicLoad);

TEST_SETUP(RemoteHeapDynamicLoad)
{
    int ret = remote_heap_feature_init();
    if (ret != 0) {
        TEST_IGNORE_MESSAGE("Audio static PD session not available - skipping");
    }
}

TEST_TEAR_DOWN(RemoteHeapDynamicLoad) { remote_heap_feature_cleanup(); }

/* ------------------------------------------------------------------ */

/*
 * DlopenInsideAudioPDSucceeds
 * One dlopen/dlclose cycle of the deployed skel .so from inside audiopd -
 * confirms dynamic loading (and its accompanying loader-heap allocation)
 * works inside the static PD.
 */
TEST(RemoteHeapDynamicLoad, DlopenInsideAudioPDSucceeds)
{
    uint64_t elapsed_us = 0;
    int ret;

    ret = fastrpc_test_dlopen_stress(g_remote_heap_feature_state.rpc_handle, DLOPEN_TARGET_SO, 1,
                                     &elapsed_us);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32(0, ret);
}
TEST_CASE_TAGS(RemoteHeapDynamicLoad, DlopenInsideAudioPDSucceeds, "RemoteHeap", "feature",
               "positive", "remote_heap_dynamic_load");

/*
 * DlopenNonexistentLibraryFailsGracefully (negative)
 * A bogus path must fail cleanly (AEE_ENOSUCHFILE), not crash or hang the
 * session.  A follow-up add() call proves the PD is still alive.
 */
TEST(RemoteHeapDynamicLoad, DlopenNonexistentLibraryFailsGracefully)
{
    uint64_t elapsed_us = 0;
    int result = 0;
    int ret;

    ret = fastrpc_test_dlopen_stress(g_remote_heap_feature_state.rpc_handle,
                                     DLOPEN_NONEXISTENT_SO, 1, &elapsed_us);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(0, ret,
                                  "dlopen of a nonexistent library should not succeed");

    /* Session must still be alive after the failed dlopen. */
    ret = fastrpc_test_add(g_remote_heap_feature_state.rpc_handle, 2, 3, &result);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32(0, ret);
    TEST_ASSERT_EQUAL(5, result);
}
TEST_CASE_TAGS(RemoteHeapDynamicLoad, DlopenNonexistentLibraryFailsGracefully, "RemoteHeap",
               "feature", "negative", "remote_heap_dynamic_load");

/* ------------------------------------------------------------------ */

TEST_GROUP_RUNNER(RemoteHeapDynamicLoad)
{
    RUN_TEST_CASE(RemoteHeapDynamicLoad, DlopenInsideAudioPDSucceeds);
    RUN_TEST_CASE(RemoteHeapDynamicLoad, DlopenNonexistentLibraryFailsGracefully);
}
