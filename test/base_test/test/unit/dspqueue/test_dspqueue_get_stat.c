// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file test_dspqueue_get_stat.c
 * @brief Unit tests for dspqueue_get_stat()
 *
 * Tests cover:
 * - Valid stat queries for all stat types
 * - Invalid parameter handling (NULL pointers, invalid handles)
 * - Invalid stat type handling
 * - Error conditions
 */

#include "AEEStdErr.h"
#include "dspqueue.h"
#include "fastrpc_test.h"
#include "remote.h"
#include "test_utils.h"
#include "unity_fixture.h"

#include <stdio.h>
#include <time.h>

/* ------------------------------------------------------------------------- */
/* Constants                                                                  */
/* ------------------------------------------------------------------------- */

/** Test queue sizes */
#define QUEUE_SIZE_SMALL (4 * 1024)

/** Invalid stat type for testing */
#define INVALID_STAT_TYPE 9999

/**
 * DSP session handle — opened in TEST_SETUP to spawn the DSP PD.
 *
 * On mainline the FastRPC driver does NOT auto-create a PD session.
 * dspqueue_create() calls fastrpc_mmap() internally to map its shared
 * memory buffers into the DSP PD.  Without a live PD that call returns
 * AEE_ENOTINITIALIZED (0x6B), which propagates back to the caller.
 *
 * Opening libfastrpc_test_skel.so via fastrpc_test_open() is what
 * triggers PD creation.  The handle is kept open for the duration of
 * each test and closed in TEST_TEAR_DOWN.  dspqueue_create() reuses
 * the same PD session — it does not open a second concurrent session.
 */
static remote_handle64 g_fastrpc_handle = INVALID_HANDLE;

/* ------------------------------------------------------------------------- */
/* Fixture group declaration                                                  */
/* ------------------------------------------------------------------------- */

TEST_GROUP(DspQueueGetStat);
TEST_GROUP_META(DspQueueGetStat, "unit", "DSP Queue API", "Queue Statistics", "dspqueue_get_stat");

/* ------------------------------------------------------------------------- */
/* setUp / tearDown                                                           */
/* ------------------------------------------------------------------------- */

TEST_SETUP(DspQueueGetStat)
{
    test_utils_install_segfault_handler();

    /*
     * Spawn the DSP PD before any dspqueue_create() call.
     *
     * On mainline the driver does not auto-create a PD session.
     * fastrpc_test_open() triggers PD creation; without it
     * dspqueue_create() returns AEE_ENOTINITIALIZED (0x6B).
     */
    char uri[512];
    test_utils_domain_uri(uri, sizeof(uri));
    int ret = fastrpc_test_open(uri, &g_fastrpc_handle);
    if (ret != AEE_SUCCESS) {
        TEST_FAIL_MESSAGE(
            "TEST_SETUP: fastrpc_test_open failed - check skel path, ISA version, and unsigned PD");
    }
}

TEST_TEAR_DOWN(DspQueueGetStat)
{
    /* No per-test teardown needed */

    /* Close the DSP session opened in TEST_SETUP */
    if (g_fastrpc_handle != INVALID_HANDLE) {
        fastrpc_test_close(g_fastrpc_handle);
        g_fastrpc_handle = INVALID_HANDLE;
    }
}

/* ------------------------------------------------------------------------- */
/* Helper functions                                                           */
/* ------------------------------------------------------------------------- */

/**
 * @brief Helper to create a queue for testing
 */
static dspqueue_t create_test_queue(void)
{
    dspqueue_t queue = NULL;
    int ret = dspqueue_create(g_test_config.domain_id, 0, QUEUE_SIZE_SMALL, QUEUE_SIZE_SMALL, NULL,
                              NULL, NULL, &queue);
    REPORT_ERROR_CODE(ret);

    if (ret != AEE_SUCCESS) {
        printf("[helper] dspqueue_create failed: 0x%x (%s)\n", ret, test_utils_err_str(ret));
        TEST_FAIL_MESSAGE("Prerequisite: dspqueue_create must succeed");
    }

    TEST_ASSERT_NOT_NULL(queue);
    return queue;
}

/**
 * @brief Helper to close a queue
 */
static void close_test_queue(dspqueue_t queue)
{
    int ret = dspqueue_close(queue);
    REPORT_ERROR_CODE(ret);
    if (ret != AEE_SUCCESS) {
        printf("[helper] dspqueue_close failed: 0x%x (%s)\n", ret, test_utils_err_str(ret));
    }
    /* Small delay for cleanup */
    struct timespec ts = { 0, 50000000 }; /* 50ms */
    nanosleep(&ts, NULL);
}

/* ========================================================================= */
/* Section 1: Positive Tests - Valid Stat Queries                           */
/* ========================================================================= */

/**
 * Test: Get WRITE_QUEUE_PACKETS stat
 * Expected: Returns AEE_SUCCESS, stat value is valid
 * Type: positive
 */
TEST(DspQueueGetStat, GetWriteQueuePacketsSucceeds)
{
    dspqueue_t queue = create_test_queue();
    uint64_t stat = 0xFFFFFFFFFFFFFFFFULL;

    int ret = dspqueue_get_stat(queue, DSPQUEUE_STAT_WRITE_QUEUE_PACKETS, &stat);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "dspqueue_get_stat for WRITE_QUEUE_PACKETS should succeed");
    TEST_ASSERT_EQUAL_MESSAGE(0, stat, "WRITE_QUEUE_PACKETS should be 0 for empty queue");

    close_test_queue(queue);
}
TEST_CASE_TAGS(DspQueueGetStat, GetWriteQueuePacketsSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_get_stat");

/**
 * Test: Get WRITE_QUEUE_BYTES stat
 * Expected: Returns AEE_SUCCESS, stat value is valid
 * Type: positive
 */
TEST(DspQueueGetStat, GetWriteQueueBytesSucceeds)
{
    dspqueue_t queue = create_test_queue();
    uint64_t stat = 0xFFFFFFFFFFFFFFFFULL;

    int ret = dspqueue_get_stat(queue, DSPQUEUE_STAT_WRITE_QUEUE_BYTES, &stat);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "dspqueue_get_stat for WRITE_QUEUE_BYTES should succeed");
    TEST_ASSERT_EQUAL_MESSAGE(0, stat, "WRITE_QUEUE_BYTES should be 0 for empty queue");

    close_test_queue(queue);
}
TEST_CASE_TAGS(DspQueueGetStat, GetWriteQueueBytesSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_get_stat");

/**
 * Test: Get READ_QUEUE_PACKETS stat
 * Expected: Returns AEE_SUCCESS, stat value is valid
 * Type: positive
 */
TEST(DspQueueGetStat, GetReadQueuePacketsSucceeds)
{
    dspqueue_t queue = create_test_queue();
    uint64_t stat = 0xFFFFFFFFFFFFFFFFULL;

    int ret = dspqueue_get_stat(queue, DSPQUEUE_STAT_READ_QUEUE_PACKETS, &stat);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "dspqueue_get_stat for READ_QUEUE_PACKETS should succeed");
    TEST_ASSERT_EQUAL_MESSAGE(0, stat, "READ_QUEUE_PACKETS should be 0 for empty queue");

    close_test_queue(queue);
}
TEST_CASE_TAGS(DspQueueGetStat, GetReadQueuePacketsSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_get_stat");

/**
 * Test: Get READ_QUEUE_BYTES stat
 * Expected: Returns AEE_SUCCESS, stat value is valid
 * Type: positive
 */
TEST(DspQueueGetStat, GetReadQueueBytesSucceeds)
{
    dspqueue_t queue = create_test_queue();
    uint64_t stat = 0xFFFFFFFFFFFFFFFFULL;

    int ret = dspqueue_get_stat(queue, DSPQUEUE_STAT_READ_QUEUE_BYTES, &stat);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "dspqueue_get_stat for READ_QUEUE_BYTES should succeed");
    TEST_ASSERT_EQUAL_MESSAGE(0, stat, "READ_QUEUE_BYTES should be 0 for empty queue");

    close_test_queue(queue);
}
TEST_CASE_TAGS(DspQueueGetStat, GetReadQueueBytesSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_get_stat");

/**
 * Test: Get EARLY_WAKEUP_WAIT_TIME stat
 * Expected: Returns AEE_SUCCESS, stat value is valid
 * Type: positive
 */
TEST(DspQueueGetStat, GetEarlyWakeupWaitTimeSucceeds)
{
    dspqueue_t queue = create_test_queue();
    uint64_t stat = 0xFFFFFFFFFFFFFFFFULL;

    int ret = dspqueue_get_stat(queue, DSPQUEUE_STAT_EARLY_WAKEUP_WAIT_TIME, &stat);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "dspqueue_get_stat for EARLY_WAKEUP_WAIT_TIME should succeed");
    TEST_ASSERT_EQUAL_MESSAGE(0, stat, "EARLY_WAKEUP_WAIT_TIME should be 0 initially");

    close_test_queue(queue);
}
TEST_CASE_TAGS(DspQueueGetStat, GetEarlyWakeupWaitTimeSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_get_stat");

/**
 * Test: Get EARLY_WAKEUP_MISSES stat
 * Expected: Returns AEE_SUCCESS, stat value is valid
 * Type: positive
 */
TEST(DspQueueGetStat, GetEarlyWakeupMissesSucceeds)
{
    dspqueue_t queue = create_test_queue();
    uint64_t stat = 0xFFFFFFFFFFFFFFFFULL;

    int ret = dspqueue_get_stat(queue, DSPQUEUE_STAT_EARLY_WAKEUP_MISSES, &stat);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "dspqueue_get_stat for EARLY_WAKEUP_MISSES should succeed");
    TEST_ASSERT_EQUAL_MESSAGE(0, stat, "EARLY_WAKEUP_MISSES should be 0 initially");

    close_test_queue(queue);
}
TEST_CASE_TAGS(DspQueueGetStat, GetEarlyWakeupMissesSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_get_stat");

/**
 * Test: Get all stat types sequentially
 * Expected: All queries succeed
 * Type: positive
 */
TEST(DspQueueGetStat, GetAllStatTypesSucceeds)
{
    dspqueue_t queue = create_test_queue();
    uint64_t stat = 0;
    int ret;

    /* Test all known stat types */
    ret = dspqueue_get_stat(queue, DSPQUEUE_STAT_WRITE_QUEUE_PACKETS, &stat);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, ret);

    ret = dspqueue_get_stat(queue, DSPQUEUE_STAT_WRITE_QUEUE_BYTES, &stat);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, ret);

    ret = dspqueue_get_stat(queue, DSPQUEUE_STAT_READ_QUEUE_PACKETS, &stat);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, ret);

    ret = dspqueue_get_stat(queue, DSPQUEUE_STAT_READ_QUEUE_BYTES, &stat);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, ret);

    ret = dspqueue_get_stat(queue, DSPQUEUE_STAT_EARLY_WAKEUP_WAIT_TIME, &stat);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, ret);

    ret = dspqueue_get_stat(queue, DSPQUEUE_STAT_EARLY_WAKEUP_MISSES, &stat);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, ret);

    close_test_queue(queue);
}
TEST_CASE_TAGS(DspQueueGetStat, GetAllStatTypesSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_get_stat");

/**
 * Test: Get same stat multiple times
 * Expected: All queries succeed with consistent values
 * Type: positive
 */
TEST(DspQueueGetStat, GetSameStatMultipleTimesSucceeds)
{
    dspqueue_t queue = create_test_queue();
    uint64_t stat1 = 0, stat2 = 0, stat3 = 0;
    int ret;

    ret = dspqueue_get_stat(queue, DSPQUEUE_STAT_WRITE_QUEUE_PACKETS, &stat1);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, ret);

    ret = dspqueue_get_stat(queue, DSPQUEUE_STAT_WRITE_QUEUE_PACKETS, &stat2);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, ret);

    ret = dspqueue_get_stat(queue, DSPQUEUE_STAT_WRITE_QUEUE_PACKETS, &stat3);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, ret);

    /* Values should be consistent for empty queue */
    TEST_ASSERT_EQUAL_MESSAGE(stat1, stat2, "Consecutive stat queries should return same value");
    TEST_ASSERT_EQUAL_MESSAGE(stat2, stat3, "Consecutive stat queries should return same value");

    close_test_queue(queue);
}
TEST_CASE_TAGS(DspQueueGetStat, GetSameStatMultipleTimesSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_get_stat");

/* ========================================================================= */
/* Section 2: Negative Tests - Invalid Parameters                           */
/* ========================================================================= */

/**
 * Test: Get stat with NULL queue handle
 * Expected: Returns AEE_EBADPARM or segfaults
 * Type: negative
 */
TEST(DspQueueGetStat, GetStatWithNullQueueFails)
{
    uint64_t stat = 0;
    int ret;

    TEST_UTILS_EXPECT_SEGFAULT_BEGIN()
    {
        ret = dspqueue_get_stat(NULL, DSPQUEUE_STAT_WRITE_QUEUE_PACKETS, &stat);
        REPORT_ERROR_CODE(ret);

        TEST_ASSERT_NOT_EQUAL_MESSAGE(AEE_SUCCESS, ret,
                                      "dspqueue_get_stat with NULL queue must fail");
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_EBADPARM, ret, "Should return AEE_EBADPARM");
    }
    TEST_UTILS_EXPECT_SEGFAULT_END(
        "dspqueue_get_stat with NULL queue caused segfault - missing NULL check");
}
TEST_CASE_TAGS(DspQueueGetStat, GetStatWithNullQueueFails, "DspQueue", "unit", "negative",
               "dspqueue_get_stat");

/**
 * Test: Get stat with NULL stat output pointer
 * Expected: Returns AEE_EBADPARM or segfaults
 * Type: negative
 */
TEST(DspQueueGetStat, GetStatWithNullStatPointerFails)
{
    dspqueue_t queue = create_test_queue();
    int ret = AEE_SUCCESS;

    TEST_UTILS_EXPECT_SEGFAULT_BEGIN()
    {
        ret = dspqueue_get_stat(queue, DSPQUEUE_STAT_WRITE_QUEUE_PACKETS, NULL);
        REPORT_ERROR_CODE(ret);

        TEST_ASSERT_NOT_EQUAL_MESSAGE(AEE_SUCCESS, ret,
                                      "dspqueue_get_stat with NULL stat pointer must fail");
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_EBADPARM, ret, "Should return AEE_EBADPARM");
    }
    TEST_UTILS_EXPECT_SEGFAULT_END(
        "dspqueue_get_stat with NULL stat pointer caused segfault - missing NULL check");

    /*
     * Cleanup unconditionally: the NULL-pointer call is expected to fail
     * and leave the queue live.  Close it here so that a longjmp from the
     * segfault handler or a 0x0 return cannot skip cleanup.
     */
    close_test_queue(queue);
}
TEST_CASE_TAGS(DspQueueGetStat, GetStatWithNullStatPointerFails, "DspQueue", "unit", "negative",
               "dspqueue_get_stat");

/**
 * Test: Get stat with invalid queue handle (fabricated pointer)
 * Expected: Returns error or segfaults
 * Type: negative
 */
TEST(DspQueueGetStat, GetStatWithInvalidQueueHandleFails)
{
    dspqueue_t invalid_queue = (dspqueue_t)0xDEADBEEF;
    uint64_t stat = 0;
    int ret;

    TEST_UTILS_EXPECT_SEGFAULT_BEGIN()
    {
        ret = dspqueue_get_stat(invalid_queue, DSPQUEUE_STAT_WRITE_QUEUE_PACKETS, &stat);
        REPORT_ERROR_CODE(ret);

        TEST_ASSERT_NOT_EQUAL_MESSAGE(AEE_SUCCESS, ret,
                                      "dspqueue_get_stat with invalid queue handle must fail");
    }
    TEST_UTILS_EXPECT_SEGFAULT_END("dspqueue_get_stat with invalid queue handle caused segfault");
}
TEST_CASE_TAGS(DspQueueGetStat, GetStatWithInvalidQueueHandleFails, "DspQueue", "unit", "negative",
               "dspqueue_get_stat");

/**
 * Test: Get stat with invalid stat type
 * Expected: Returns AEE_EBADPARM
 * Type: negative
 */
TEST(DspQueueGetStat, GetStatWithInvalidStatTypeFails)
{
    dspqueue_t queue = create_test_queue();
    uint64_t stat = 0;

    int ret = dspqueue_get_stat(queue, INVALID_STAT_TYPE, &stat);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_NOT_EQUAL_MESSAGE(AEE_SUCCESS, ret,
                                  "dspqueue_get_stat with invalid stat type must fail");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_EBADPARM, ret, "Should return AEE_EBADPARM");

    close_test_queue(queue);
}
TEST_CASE_TAGS(DspQueueGetStat, GetStatWithInvalidStatTypeFails, "DspQueue", "unit", "negative",
               "dspqueue_get_stat");

/**
 * Test: Get stat with negative stat type
 * Expected: Returns AEE_EBADPARM
 * Type: negative
 */
TEST(DspQueueGetStat, GetStatWithNegativeStatTypeFails)
{
    dspqueue_t queue = create_test_queue();
    uint64_t stat = 0;

    int ret = dspqueue_get_stat(queue, -1, &stat);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_NOT_EQUAL_MESSAGE(AEE_SUCCESS, ret,
                                  "dspqueue_get_stat with negative stat type must fail");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_EBADPARM, ret, "Should return AEE_EBADPARM");

    close_test_queue(queue);
}
TEST_CASE_TAGS(DspQueueGetStat, GetStatWithNegativeStatTypeFails, "DspQueue", "unit", "negative",
               "dspqueue_get_stat");

/* ========================================================================= */
/* Section 3: Edge Cases and Boundary Conditions                            */
/* ========================================================================= */

/**
 * Test: Get stat immediately after queue creation
 * Expected: Returns AEE_SUCCESS with zero values
 * Type: edge case
 */
TEST(DspQueueGetStat, GetStatImmediatelyAfterCreateSucceeds)
{
    dspqueue_t queue = create_test_queue();
    uint64_t stat = 0xFFFFFFFFFFFFFFFFULL;

    /* Get stat immediately - should be zero */
    int ret = dspqueue_get_stat(queue, DSPQUEUE_STAT_WRITE_QUEUE_PACKETS, &stat);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "Get stat immediately after create should succeed");
    TEST_ASSERT_EQUAL_MESSAGE(0, stat, "Stat should be 0 for newly created queue");

    close_test_queue(queue);
}
TEST_CASE_TAGS(DspQueueGetStat, GetStatImmediatelyAfterCreateSucceeds, "DspQueue", "unit",
               "positive", "dspqueue_get_stat");

/**
 * Test: Get stat after queue is closed
 * Expected: Returns error
 * Type: edge case
 */
TEST(DspQueueGetStat, GetStatAfterCloseQueueFails)
{
    dspqueue_t queue = create_test_queue();
    uint64_t stat = 0;
    int ret;

    /* Close the queue */
    ret = dspqueue_close(queue);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, ret);

    /* Try to get stat after close — queue memory is freed, may segfault */
    TEST_UTILS_EXPECT_SEGFAULT_BEGIN()
    {
        ret = dspqueue_get_stat(queue, DSPQUEUE_STAT_WRITE_QUEUE_PACKETS, &stat);
        REPORT_ERROR_CODE(ret);

        /*
         * If the implementation returned 0x0 instead of failing, the test
         * must still be marked as a failure.  The assertion is placed inside
         * the segfault block so the segfault handler can catch it if needed;
         * no live queue exists at this point so there is nothing to clean up.
         */
        TEST_ASSERT_NOT_EQUAL_MESSAGE(AEE_SUCCESS, ret, "Get stat after close must fail");
    }
    TEST_UTILS_EXPECT_SEGFAULT_END(
        "dspqueue_get_stat after close caused segfault - queue memory freed");
}
TEST_CASE_TAGS(DspQueueGetStat, GetStatAfterCloseQueueFails, "DspQueue", "unit", "negative",
               "dspqueue_get_stat");

/**
 * Test: Get stat with stat type at boundary (0)
 * Expected: Returns AEE_SUCCESS (if 0 is valid) or AEE_EBADPARM
 * Type: edge case
 */
TEST(DspQueueGetStat, GetStatWithZeroStatType)
{
    dspqueue_t queue = create_test_queue();
    uint64_t stat = 0;

    int ret = dspqueue_get_stat(queue, 0, &stat);
    REPORT_ERROR_CODE(ret);

    /* Stat type 0 might be valid (WRITE_QUEUE_PACKETS) or invalid */
    TEST_ASSERT_TRUE_MESSAGE((ret == AEE_SUCCESS) || (ret == AEE_EBADPARM),
                             "Stat type 0 should return SUCCESS or EBADPARM");

    close_test_queue(queue);
}
TEST_CASE_TAGS(DspQueueGetStat, GetStatWithZeroStatType, "DspQueue", "unit", "positive",
               "dspqueue_get_stat");

/**
 * Test: Get stat with maximum valid stat type
 * Expected: Returns AEE_SUCCESS
 * Type: edge case
 */
TEST(DspQueueGetStat, GetStatWithMaximumValidStatType)
{
    dspqueue_t queue = create_test_queue();
    uint64_t stat = 0;

    int ret = dspqueue_get_stat(queue, DSPQUEUE_STAT_EARLY_WAKEUP_MISSES, &stat);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "Get stat with maximum valid stat type should succeed");

    close_test_queue(queue);
}
TEST_CASE_TAGS(DspQueueGetStat, GetStatWithMaximumValidStatType, "DspQueue", "unit", "positive",
               "dspqueue_get_stat");

/**
 * Test: Get stat with stat type just beyond maximum
 * Expected: Returns AEE_EBADPARM
 * Type: edge case
 */
TEST(DspQueueGetStat, GetStatWithStatTypeBeyondMaximumFails)
{
    dspqueue_t queue = create_test_queue();
    uint64_t stat = 0;

    /* Try stat type 6 (one beyond EARLY_WAKEUP_MISSES) */
    int ret = dspqueue_get_stat(queue, 6, &stat);
    REPORT_ERROR_CODE(ret);

    /*
     * Cleanup BEFORE assertion: close the queue unconditionally so that a
     * longjmp from the assertion below cannot skip cleanup.
     */
    close_test_queue(queue);

    TEST_ASSERT_NOT_EQUAL_MESSAGE(AEE_SUCCESS, ret,
                                  "Get stat with stat type beyond maximum must fail");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_EBADPARM, ret, "Should return AEE_EBADPARM");
}
TEST_CASE_TAGS(DspQueueGetStat, GetStatWithStatTypeBeyondMaximumFails, "DspQueue", "unit",
               "negative", "dspqueue_get_stat");

/* ========================================================================= */
/* Group runner                                                              */
/* ========================================================================= */

TEST_GROUP_RUNNER(DspQueueGetStat)
{
    /* Section 1: Positive Tests */
    RUN_TEST_CASE(DspQueueGetStat, GetWriteQueuePacketsSucceeds);
    RUN_TEST_CASE(DspQueueGetStat, GetWriteQueueBytesSucceeds);
    RUN_TEST_CASE(DspQueueGetStat, GetReadQueuePacketsSucceeds);
    RUN_TEST_CASE(DspQueueGetStat, GetReadQueueBytesSucceeds);
    RUN_TEST_CASE(DspQueueGetStat, GetEarlyWakeupWaitTimeSucceeds);
    RUN_TEST_CASE(DspQueueGetStat, GetEarlyWakeupMissesSucceeds);
    RUN_TEST_CASE(DspQueueGetStat, GetAllStatTypesSucceeds);
    RUN_TEST_CASE(DspQueueGetStat, GetSameStatMultipleTimesSucceeds);

    /* Section 2: Negative Tests */
    // RUN_TEST_CASE(DspQueueGetStat, GetStatWithNullQueueFails);
    // RUN_TEST_CASE(DspQueueGetStat, GetStatWithNullStatPointerFails);
    // RUN_TEST_CASE(DspQueueGetStat, GetStatWithInvalidQueueHandleFails);
    RUN_TEST_CASE(DspQueueGetStat, GetStatWithInvalidStatTypeFails);
    RUN_TEST_CASE(DspQueueGetStat, GetStatWithNegativeStatTypeFails);

    /* Section 3: Edge Cases */
    RUN_TEST_CASE(DspQueueGetStat, GetStatImmediatelyAfterCreateSucceeds);
    // RUN_TEST_CASE(DspQueueGetStat, GetStatAfterCloseQueueFails);
    RUN_TEST_CASE(DspQueueGetStat, GetStatWithZeroStatType);
    RUN_TEST_CASE(DspQueueGetStat, GetStatWithMaximumValidStatType);
    // RUN_TEST_CASE(DspQueueGetStat, GetStatWithStatTypeBeyondMaximumFails);
}
