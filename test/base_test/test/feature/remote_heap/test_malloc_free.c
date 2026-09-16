// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/*
 * test_malloc_free.c - remote-heap malloc/free feature tests.
 *
 * Exercises fastrpc_test_malloc_free_stress() against a handle opened
 * inside the Audio static PD (audiopd).  Single-shot functional checks
 * only - not soak/stress runs, per project convention that repeated-cycle
 * variants belong in a separate stress/perf test area.
 *
 * Sizes are chosen relative to the 3MB initial remote-heap grant given to
 * audiopd on creation (fastrpc_apps_user.c) to distinguish allocations
 * that stay within the initial budget from ones that force the DSP loader
 * to request additional memory from the CPU via the apps_mem reverse-RPC
 * path (Docs/apps_mem.md).
 */

#include "fastrpc_test.h"
#include "remote_heap_feature_utils.h"
#include "test_utils.h"
#include "unity_fixture.h"

#include <stdint.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */

TEST_GROUP(RemoteHeapMallocFree);

TEST_SETUP(RemoteHeapMallocFree)
{
    int ret = remote_heap_feature_init();
    if (ret != 0) {
        TEST_IGNORE_MESSAGE("Audio static PD session not available - skipping");
    }
}

TEST_TEAR_DOWN(RemoteHeapMallocFree) { remote_heap_feature_cleanup(); }

/* ------------------------------------------------------------------ */

/*
 * SmallAllocationWithinInitialBudget
 * One round of small allocations that comfortably fits inside the 3MB
 * initial audiopd remote-heap grant - baseline sanity, no growth expected.
 */
TEST(RemoteHeapMallocFree, SmallAllocationWithinInitialBudget)
{
    uint32_t sizes[] = { 4096 };
    uint64_t elapsed_us = 0;
    int ret;

    ret = fastrpc_test_malloc_free_stress(g_remote_heap_feature_state.rpc_handle, sizes,
                                          sizeof(sizes) / sizeof(sizes[0]), 1, 4, &elapsed_us);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32(0, ret);
}
TEST_CASE_TAGS(RemoteHeapMallocFree, SmallAllocationWithinInitialBudget, "RemoteHeap", "feature",
               "positive", "remote_heap_malloc_free");

/*
 * LargeSingleAllocationExceedsInitialBudget
 * A single ~4MiB allocation exceeds the 3MB static-PD grant, forcing a
 * genuine apps_mem remote-heap growth round-trip.
 */
#define LARGE_ALLOC_SIZE (4 * 1024 * 1024) /* 4 MiB, exceeds the 3MB initial grant */

TEST(RemoteHeapMallocFree, LargeSingleAllocationExceedsInitialBudget)
{
    uint32_t sizes[] = { LARGE_ALLOC_SIZE };
    uint64_t elapsed_us = 0;
    int ret;

    ret = fastrpc_test_malloc_free_stress(g_remote_heap_feature_state.rpc_handle, sizes,
                                          sizeof(sizes) / sizeof(sizes[0]), 1, 1, &elapsed_us);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32(0, ret);
}
TEST_CASE_TAGS(RemoteHeapMallocFree, LargeSingleAllocationExceedsInitialBudget, "RemoteHeap",
               "feature", "positive", "remote_heap_malloc_free");

/*
 * ManySmallChunksSumExceedingBudget
 * 20 x 256KiB buffers held simultaneously in one round (5MiB total),
 * exceeding the initial budget via many small chunks instead of one big
 * allocation - a distinct allocation shape from LargeSingleAllocation*.
 */
#define CHUNK_SIZE (256 * 1024)
#define CHUNK_COUNT 20

TEST(RemoteHeapMallocFree, ManySmallChunksSumExceedingBudget)
{
    uint32_t sizes[] = { CHUNK_SIZE };
    uint64_t elapsed_us = 0;
    int ret;

    ret = fastrpc_test_malloc_free_stress(g_remote_heap_feature_state.rpc_handle, sizes,
                                          sizeof(sizes) / sizeof(sizes[0]), 1, CHUNK_COUNT,
                                          &elapsed_us);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32(0, ret);
}
TEST_CASE_TAGS(RemoteHeapMallocFree, ManySmallChunksSumExceedingBudget, "RemoteHeap", "feature",
               "positive", "remote_heap_malloc_free");

/*
 * GrowThenShrinkRoundTrip
 * One round that exceeds the initial budget (grow), followed by a second
 * ordinary small round (shrink back to baseline usage) - confirms the
 * map+unmap cycle leaves the session usable for further calls.
 */
TEST(RemoteHeapMallocFree, GrowThenShrinkRoundTrip)
{
    uint32_t grow_sizes[] = { LARGE_ALLOC_SIZE };
    uint32_t small_sizes[] = { 4096 };
    uint64_t elapsed_us = 0;
    int result = 0;
    int ret;

    ret = fastrpc_test_malloc_free_stress(g_remote_heap_feature_state.rpc_handle, grow_sizes,
                                          sizeof(grow_sizes) / sizeof(grow_sizes[0]), 1, 1,
                                          &elapsed_us);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(0, ret, "grow round");

    ret = fastrpc_test_malloc_free_stress(g_remote_heap_feature_state.rpc_handle, small_sizes,
                                          sizeof(small_sizes) / sizeof(small_sizes[0]), 1, 4,
                                          &elapsed_us);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(0, ret, "shrink round");

    /* Confirm the session is still fully usable after the grow/shrink cycle. */
    ret = fastrpc_test_add(g_remote_heap_feature_state.rpc_handle, 2, 3, &result);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32(0, ret);
    TEST_ASSERT_EQUAL(5, result);
}
TEST_CASE_TAGS(RemoteHeapMallocFree, GrowThenShrinkRoundTrip, "RemoteHeap", "feature", "positive",
               "remote_heap_malloc_free");

/*
 * OversizedAllocationFailsGracefully (negative)
 * An implausibly large single allocation (~1GiB) must fail cleanly
 * (AEE_ENOMEMORY or similar), not crash or hang the session.  A follow-up
 * add() call proves the PD/session survived the failed allocation.
 */
#define OVERSIZED_ALLOC_SIZE (1024u * 1024u * 1024u) /* 1 GiB */

TEST(RemoteHeapMallocFree, OversizedAllocationFailsGracefully)
{
    uint32_t sizes[] = { OVERSIZED_ALLOC_SIZE };
    uint64_t elapsed_us = 0;
    int result = 0;
    int ret;

    ret = fastrpc_test_malloc_free_stress(g_remote_heap_feature_state.rpc_handle, sizes,
                                          sizeof(sizes) / sizeof(sizes[0]), 1, 1, &elapsed_us);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(0, ret, "1GiB allocation should not succeed");

    /* Session must still be alive after the failed allocation. */
    ret = fastrpc_test_add(g_remote_heap_feature_state.rpc_handle, 2, 3, &result);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32(0, ret);
    TEST_ASSERT_EQUAL(5, result);
}
TEST_CASE_TAGS(RemoteHeapMallocFree, OversizedAllocationFailsGracefully, "RemoteHeap", "feature",
               "negative", "remote_heap_malloc_free");

/* ------------------------------------------------------------------ */

TEST_GROUP_RUNNER(RemoteHeapMallocFree)
{
    RUN_TEST_CASE(RemoteHeapMallocFree, SmallAllocationWithinInitialBudget);
    RUN_TEST_CASE(RemoteHeapMallocFree, LargeSingleAllocationExceedsInitialBudget);
    RUN_TEST_CASE(RemoteHeapMallocFree, ManySmallChunksSumExceedingBudget);
    RUN_TEST_CASE(RemoteHeapMallocFree, GrowThenShrinkRoundTrip);
    RUN_TEST_CASE(RemoteHeapMallocFree, OversizedAllocationFailsGracefully);
}
