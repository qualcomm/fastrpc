// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file test_dspqueue_write.c
 * @brief Unit tests for dspqueue_write() and dspqueue_write_noblock()
 *
 * Tests cover:
 * - Valid write operations (blocking and non-blocking)
 * - Invalid parameter handling (NULL pointers, invalid handles)
 * - Message size validation
 * - Buffer reference validation
 * - Timeout parameter validation
 * - Flag validation
 * - Error conditions
 */

#include "AEEStdErr.h"
#include "dspqueue.h"
#include "fastrpc_test.h"
#include "remote.h"
#include "test_utils.h"
#include "unity_fixture.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------------------- */
/* Constants                                                                  */
/* ------------------------------------------------------------------------- */

/** Test queue sizes */
#define QUEUE_SIZE_SMALL (4 * 1024)

/** Test timeouts */
#define SHORT_TIMEOUT_US (100 * 1000) /* 100 ms */

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

TEST_GROUP(DspQueueWrite);
TEST_GROUP_META(DspQueueWrite, "unit", "DSP Queue API", "Queue I/O",
                "dspqueue_write / dspqueue_write_noblock");

/* ------------------------------------------------------------------------- */
/* setUp / tearDown                                                           */
/* ------------------------------------------------------------------------- */

TEST_SETUP(DspQueueWrite)
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

TEST_TEAR_DOWN(DspQueueWrite)
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
/* Section 1: Positive Tests - Valid Write Operations                       */
/* ========================================================================= */

/**
 * Test: Write small message with dspqueue_write_noblock
 * Expected: Returns AEE_SUCCESS
 * Type: positive
 */
TEST(DspQueueWrite, WriteNoBlockSmallMessageSucceeds)
{
    dspqueue_t queue = create_test_queue();
    uint32_t msg = 0x12345678;

    int ret = dspqueue_write_noblock(queue, 0, 0, NULL, 4, (uint8_t *)&msg);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "dspqueue_write_noblock should succeed for small message");

    close_test_queue(queue);
}
TEST_CASE_TAGS(DspQueueWrite, WriteNoBlockSmallMessageSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_write");

/**
 * Test: Write with blocking call and timeout
 * Expected: Returns AEE_SUCCESS
 * Type: positive
 */
TEST(DspQueueWrite, WriteBlockingWithTimeoutSucceeds)
{
    dspqueue_t queue = create_test_queue();
    uint32_t msg = 0xABCDEF00;

    int ret = dspqueue_write(queue, 0, 0, NULL, 4, (uint8_t *)&msg, SHORT_TIMEOUT_US);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret, "dspqueue_write with timeout should succeed");

    close_test_queue(queue);
}
TEST_CASE_TAGS(DspQueueWrite, WriteBlockingWithTimeoutSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_write");

/**
 * Test: Write with DSPQUEUE_TIMEOUT_NONE (infinite timeout)
 * Expected: Returns AEE_SUCCESS
 * Type: positive
 */
TEST(DspQueueWrite, WriteWithInfiniteTimeoutSucceeds)
{
    dspqueue_t queue = create_test_queue();
    uint32_t msg = 0xDEADBEEF;

    int ret = dspqueue_write(queue, 0, 0, NULL, 4, (uint8_t *)&msg, DSPQUEUE_TIMEOUT_NONE);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "dspqueue_write with TIMEOUT_NONE should succeed");

    close_test_queue(queue);
}
TEST_CASE_TAGS(DspQueueWrite, WriteWithInfiniteTimeoutSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_write");

/**
 * Test: Write zero-length message
 * Expected: Returns AEE_SUCCESS (valid edge case)
 * Type: positive
 */
TEST(DspQueueWrite, WriteZeroLengthMessageSucceeds)
{
    dspqueue_t queue = create_test_queue();

    int ret = dspqueue_write_noblock(queue, 0, 0, NULL, 0, NULL);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(
        AEE_SUCCESS, ret, "dspqueue_write_noblock with zero-length message should succeed");

    close_test_queue(queue);
}
TEST_CASE_TAGS(DspQueueWrite, WriteZeroLengthMessageSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_write");

/**
 * Test: Write with MESSAGE flag explicitly set
 * Expected: Returns AEE_SUCCESS
 * Type: positive
 */
TEST(DspQueueWrite, WriteWithMessageFlagSucceeds)
{
    dspqueue_t queue = create_test_queue();
    uint32_t msg = 0x11223344;

    int ret
        = dspqueue_write_noblock(queue, DSPQUEUE_PACKET_FLAG_MESSAGE, 0, NULL, 4, (uint8_t *)&msg);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "dspqueue_write_noblock with MESSAGE flag should succeed");

    close_test_queue(queue);
}
TEST_CASE_TAGS(DspQueueWrite, WriteWithMessageFlagSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_write");

/**
 * Test: Write multiple messages sequentially
 * Expected: All writes succeed
 * Type: positive
 */
TEST(DspQueueWrite, WriteMultipleMessagesSucceeds)
{
    dspqueue_t queue = create_test_queue();
    int i;

    for (i = 0; i < 10; i++) {
        uint32_t msg = i;
        int ret = dspqueue_write_noblock(queue, 0, 0, NULL, 4, (uint8_t *)&msg);
        REPORT_ERROR_CODE(ret);
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret, "Each write should succeed");
    }

    close_test_queue(queue);
}
TEST_CASE_TAGS(DspQueueWrite, WriteMultipleMessagesSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_write");

/* ========================================================================= */
/* Section 2: Negative Tests - Invalid Parameters                           */
/* ========================================================================= */

/**
 * Test: Write with NULL queue handle
 * Expected: Returns error or segfaults (caught by handler)
 * Type: negative
 */
TEST(DspQueueWrite, WriteWithNullQueueFails)
{
    uint32_t msg = 0x12345678;
    int ret;

    TEST_UTILS_EXPECT_SEGFAULT_BEGIN()
    {
        ret = dspqueue_write_noblock(NULL, 0, 0, NULL, 4, (uint8_t *)&msg);
        REPORT_ERROR_CODE(ret);

        TEST_ASSERT_NOT_EQUAL_MESSAGE(AEE_SUCCESS, ret,
                                      "dspqueue_write_noblock with NULL queue must fail");
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_EBADPARM, ret, "Should return AEE_EBADPARM");
    }
    TEST_UTILS_EXPECT_SEGFAULT_END(
        "dspqueue_write_noblock with NULL queue caused segfault - missing NULL check");
}
TEST_CASE_TAGS(DspQueueWrite, WriteWithNullQueueFails, "DspQueue", "unit", "negative",
               "dspqueue_write");

/**
 * Test: Write with NULL message buffer but non-zero length
 * Expected: Returns AEE_EBADPARM
 * Type: negative
 */
TEST(DspQueueWrite, WriteWithNullMessageBufferFails)
{
    dspqueue_t queue = create_test_queue();
    int ret = AEE_SUCCESS;

    TEST_UTILS_EXPECT_SEGFAULT_BEGIN()
    {
        ret = dspqueue_write_noblock(queue, 0, 0, NULL, 4, NULL);
        REPORT_ERROR_CODE(ret);

        TEST_ASSERT_NOT_EQUAL_MESSAGE(
            AEE_SUCCESS, ret, "Write with NULL message buffer and non-zero length must fail");
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_EBADPARM, ret, "Should return AEE_EBADPARM");
    }
    TEST_UTILS_EXPECT_SEGFAULT_END(
        "dspqueue_write_noblock with NULL message buffer caused segfault");

    /* Cleanup unconditionally — queue is still live if the call didn't segfault */
    close_test_queue(queue);
}
TEST_CASE_TAGS(DspQueueWrite, WriteWithNullMessageBufferFails, "DspQueue", "unit", "negative",
               "dspqueue_write");

/**
 * Test: Write with invalid queue handle (fabricated pointer)
 * Expected: Returns error or segfaults
 * Type: negative
 */
TEST(DspQueueWrite, WriteWithInvalidQueueHandleFails)
{
    dspqueue_t invalid_queue = (dspqueue_t)0xDEADBEEF;
    uint32_t msg = 0x12345678;
    int ret;

    TEST_UTILS_EXPECT_SEGFAULT_BEGIN()
    {
        ret = dspqueue_write_noblock(invalid_queue, 0, 0, NULL, 4, (uint8_t *)&msg);
        REPORT_ERROR_CODE(ret);

        TEST_ASSERT_NOT_EQUAL_MESSAGE(AEE_SUCCESS, ret,
                                      "Write with invalid queue handle must fail");
    }
    TEST_UTILS_EXPECT_SEGFAULT_END(
        "dspqueue_write_noblock with invalid queue handle caused segfault");
}
TEST_CASE_TAGS(DspQueueWrite, WriteWithInvalidQueueHandleFails, "DspQueue", "unit", "negative",
               "dspqueue_write");

/**
 * Test: Write with NULL buffer references but non-zero count
 * Expected: Returns AEE_EBADPARM
 * Type: negative
 */
TEST(DspQueueWrite, WriteWithNullBufferReferencesButNonZeroCountFails)
{
    dspqueue_t queue = create_test_queue();
    uint32_t msg = 0x12345678;
    int ret = AEE_SUCCESS;

    TEST_UTILS_EXPECT_SEGFAULT_BEGIN()
    {
        ret = dspqueue_write_noblock(queue, 0, 2, NULL, 4, (uint8_t *)&msg);
        REPORT_ERROR_CODE(ret);

        TEST_ASSERT_NOT_EQUAL_MESSAGE(AEE_SUCCESS, ret,
                                      "Write with NULL buffer array but non-zero count must fail");
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_EBADPARM, ret, "Should return AEE_EBADPARM");
    }
    TEST_UTILS_EXPECT_SEGFAULT_END("dspqueue_write_noblock with NULL buffer array caused segfault");

    /* Cleanup unconditionally — queue is still live if the call didn't segfault */
    close_test_queue(queue);
}
TEST_CASE_TAGS(DspQueueWrite, WriteWithNullBufferReferencesButNonZeroCountFails, "DspQueue", "unit",
               "negative", "dspqueue_write");

/**
 * Test: Write with excessive message size
 * Expected: Returns AEE_EBADPARM or AEE_EBUFFERTOOSMALL
 * Type: negative
 */
TEST(DspQueueWrite, WriteWithExcessiveMessageSizeFails)
{
    dspqueue_t queue = create_test_queue();
    uint32_t msg = 0x12345678;

    /* Try to write message larger than queue size */
    int ret = dspqueue_write_noblock(queue, 0, 0, NULL, QUEUE_SIZE_SMALL + 1000, (uint8_t *)&msg);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_NOT_EQUAL_MESSAGE(AEE_SUCCESS, ret, "Write with excessive message size must fail");
    TEST_ASSERT_TRUE_MESSAGE(((ret & 0xFFFF) == (AEE_EBADPARM & 0xFFFF))
                                 || ((ret & 0xFFFF) == (AEE_EBUFFERTOOSMALL & 0xFFFF)),
                             "Should return AEE_EBADPARM or AEE_EBUFFERTOOSMALL");

    close_test_queue(queue);
}
TEST_CASE_TAGS(DspQueueWrite, WriteWithExcessiveMessageSizeFails, "DspQueue", "unit", "negative",
               "dspqueue_write");

/**
 * Test: Write with invalid flags
 * Expected: Returns AEE_EBADPARM
 * Type: negative
 */
TEST(DspQueueWrite, WriteWithInvalidFlagsFails)
{
    dspqueue_t queue = create_test_queue();
    uint32_t msg = 0x12345678;

    /* Use undefined/invalid flag bits */
    int ret = dspqueue_write_noblock(queue, 0xFFFF0000, 0, NULL, 4, (uint8_t *)&msg);
    REPORT_ERROR_CODE(ret);

    /*
     * Cleanup BEFORE assertion: close the queue unconditionally so that a
     * longjmp from the assertion below cannot skip cleanup.
     */
    close_test_queue(queue);

    TEST_ASSERT_NOT_EQUAL_MESSAGE(AEE_SUCCESS, ret, "Write with invalid flags must fail");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_EBADPARM, ret, "Should return AEE_EBADPARM");
}
TEST_CASE_TAGS(DspQueueWrite, WriteWithInvalidFlagsFails, "DspQueue", "unit", "negative",
               "dspqueue_write");

/**
 * Test: Blocking write with negative timeout
 * Expected: Returns AEE_EBADPARM
 * Type: negative
 */
TEST(DspQueueWrite, WriteWithNegativeTimeoutFails)
{
    dspqueue_t queue = create_test_queue();
    uint32_t msg = 0x12345678;

    int ret = dspqueue_write(queue, 0, 0, NULL, 4, (uint8_t *)&msg, -1);
    REPORT_ERROR_CODE(ret);

    /*
     * Cleanup BEFORE assertion: close the queue unconditionally so that a
     * longjmp from the assertion below cannot skip cleanup.
     */
    close_test_queue(queue);

    TEST_ASSERT_NOT_EQUAL_MESSAGE(AEE_SUCCESS, ret, "Write with negative timeout must fail");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_EBADPARM, ret, "Should return AEE_EBADPARM");
}
TEST_CASE_TAGS(DspQueueWrite, WriteWithNegativeTimeoutFails, "DspQueue", "unit", "negative",
               "dspqueue_write");

/* ========================================================================= */
/* Section 3: Edge Cases and Boundary Conditions                            */
/* ========================================================================= */

/**
 * Test: Write with maximum valid message size
 * Expected: Returns AEE_SUCCESS
 * Type: edge case
 */
TEST(DspQueueWrite, WriteMaximumMessageSizeSucceeds)
{
    dspqueue_t queue = create_test_queue();
    void *large_msg = NULL;
    size_t max_msg_size = QUEUE_SIZE_SMALL - 100; /* Leave room for header */

    large_msg = malloc(max_msg_size);
    TEST_ASSERT_NOT_NULL(large_msg);
    memset(large_msg, 0xAB, max_msg_size);

    int ret = dspqueue_write_noblock(queue, 0, 0, NULL, max_msg_size, (uint8_t *)large_msg);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "Write with maximum message size should succeed");

    free(large_msg);
    close_test_queue(queue);
}
TEST_CASE_TAGS(DspQueueWrite, WriteMaximumMessageSizeSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_write");

/**
 * Test: Write with timeout of zero (should return immediately)
 * Expected: Returns AEE_SUCCESS or AEE_EWOULDBLOCK
 * Type: edge case
 */
TEST(DspQueueWrite, WriteWithZeroTimeoutReturnsImmediately)
{
    dspqueue_t queue = create_test_queue();
    uint32_t msg = 0x12345678;
    uint64_t t1, t2;
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    t1 = ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000;

    int ret = dspqueue_write(queue, 0, 0, NULL, 4, (uint8_t *)&msg, 0);
    REPORT_ERROR_CODE(ret);

    clock_gettime(CLOCK_MONOTONIC, &ts);
    t2 = ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000;

    /* Should return immediately (within 10ms) */
    TEST_ASSERT_LESS_THAN_MESSAGE(10000, (t2 - t1),
                                  "Write with zero timeout should return immediately");

    /* Result can be SUCCESS or EWOULDBLOCK depending on queue state */
    TEST_ASSERT_TRUE_MESSAGE((ret == AEE_SUCCESS) || (ret == AEE_EWOULDBLOCK),
                             "Should return SUCCESS or EWOULDBLOCK");

    close_test_queue(queue);
}
TEST_CASE_TAGS(DspQueueWrite, WriteWithZeroTimeoutReturnsImmediately, "DspQueue", "unit",
               "positive", "dspqueue_write");

/**
 * Test: Write after queue is closed
 * Expected: Returns error
 * Type: edge case
 *
 * Strategy: use dspqueue_get_stat(DSPQUEUE_STAT_WRITE_QUEUE_PACKETS) as the
 * oracle, on the same queue, before the close.
 *
 * Background on queue directions:
 *   dspqueue_write / dspqueue_write_noblock  --> req_queue  (CPU -> DSP)
 *   dspqueue_read  / dspqueue_read_noblock   <-- resp_queue (DSP -> CPU)
 * These are two separate ring-buffers; a CPU-side write cannot be read back
 * by a CPU-side read.  The only CPU-visible way to confirm a packet is
 * sitting in the req_queue is via DSPQUEUE_STAT_WRITE_QUEUE_PACKETS, which
 * counts packets written but not yet consumed by the DSP.
 *
 * Steps:
 *   1. Create a queue.
 *   2. Confirm WRITE_QUEUE_PACKETS == 0 (queue is empty).
 *   3. Write one packet; confirm WRITE_QUEUE_PACKETS == 1.
 *      This proves the stat correctly tracks writes on a live queue.
 *   4. Close the queue.
 *   5. Attempt a second write on the now-closed (freed) queue.
 *      A correct implementation must return a non-zero error.
 *      A buggy implementation silently returns 0 (the failure we are
 *      catching).
 */
TEST(DspQueueWrite, WriteAfterCloseQueueFails)
{
    /* --- Step 1: create queue ------------------------------------------- */
    dspqueue_t queue = create_test_queue();
    int ret;

    /* --- Step 2: confirm queue starts empty ----------------------------- */
    uint64_t packets_before = 0xFFFFFFFFFFFFFFFFULL;
    ret = dspqueue_get_stat(queue, DSPQUEUE_STAT_WRITE_QUEUE_PACKETS, &packets_before);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret, "get_stat before write must succeed");
    TEST_ASSERT_EQUAL_MESSAGE(0, packets_before,
                              "WRITE_QUEUE_PACKETS must be 0 on a freshly created queue");

    /* --- Step 3: write one packet and confirm the stat increments ------- */
    const uint32_t SENTINEL = 0xABCD1234;
    ret = dspqueue_write_noblock(queue, 0, 0, NULL, sizeof(SENTINEL), (const uint8_t *)&SENTINEL);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "Pre-close write must succeed (queue is healthy)");

    uint64_t packets_after_write = 0;
    ret = dspqueue_get_stat(queue, DSPQUEUE_STAT_WRITE_QUEUE_PACKETS, &packets_after_write);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret, "get_stat after write must succeed");
    TEST_ASSERT_EQUAL_MESSAGE(1, packets_after_write,
                              "WRITE_QUEUE_PACKETS must be 1 after one write");

    /* --- Step 4: close the queue ---------------------------------------- */
    ret = dspqueue_close(queue);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret, "dspqueue_close must succeed");

    /* --- Step 5: attempt write-after-close ------------------------------ */
    /*
     * The queue struct and its shared memory have been freed by dspqueue_close.
     * A correct implementation must return a non-zero error code.
     * A buggy implementation silently returns 0 because it writes into the
     * freed (but still mapped) memory without any closed-state guard.
     *
     * The call may also segfault if the allocator has poisoned the freed
     * region; the segfault handler catches that case and the test still
     * passes (a segfault is also a non-zero outcome).
     */
    int write_after_close_ret = AEE_SUCCESS;
    const uint32_t SENTINEL_AFTER = 0xDEADBEEF;

    TEST_UTILS_EXPECT_SEGFAULT_BEGIN()
    {
        write_after_close_ret = dspqueue_write_noblock(queue, 0, 0, NULL, sizeof(SENTINEL_AFTER),
                                                       (const uint8_t *)&SENTINEL_AFTER);
        REPORT_ERROR_CODE(write_after_close_ret);

        TEST_ASSERT_NOT_EQUAL_MESSAGE(
            AEE_SUCCESS, write_after_close_ret,
            "dspqueue_write_noblock after close must return a non-zero error code");
    }
    TEST_UTILS_EXPECT_SEGFAULT_END(
        "dspqueue_write_noblock after close caused segfault - queue memory freed");
}
TEST_CASE_TAGS(DspQueueWrite, WriteAfterCloseQueueFails, "DspQueue", "unit", "negative",
               "dspqueue_write");

/**
 * Test: Write with both MESSAGE and BUFFERS flags
 * Expected: Returns AEE_SUCCESS (both are valid)
 * Type: edge case
 */
TEST(DspQueueWrite, WriteWithBothMessageAndBuffersFlagsSucceeds)
{
    dspqueue_t queue = create_test_queue();
    uint32_t msg = 0x12345678;

    int ret
        = dspqueue_write_noblock(queue, DSPQUEUE_PACKET_FLAG_MESSAGE | DSPQUEUE_PACKET_FLAG_BUFFERS,
                                 0, NULL, 4, (uint8_t *)&msg);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "Write with both MESSAGE and BUFFERS flags should succeed");

    close_test_queue(queue);
}
TEST_CASE_TAGS(DspQueueWrite, WriteWithBothMessageAndBuffersFlagsSucceeds, "DspQueue", "unit",
               "positive", "dspqueue_write");

/**
 * Test: Write with 1-byte message
 * Expected: Returns AEE_SUCCESS
 * Type: edge case
 */
TEST(DspQueueWrite, WriteOneByteMessageSucceeds)
{
    dspqueue_t queue = create_test_queue();
    uint8_t msg = 0x42;

    int ret = dspqueue_write_noblock(queue, 0, 0, NULL, 1, &msg);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret, "Write with 1-byte message should succeed");

    close_test_queue(queue);
}
TEST_CASE_TAGS(DspQueueWrite, WriteOneByteMessageSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_write");

/* ========================================================================= */
/* Section 4: Buffer Reference Validation                                   */
/* ========================================================================= */

/**
 * Test: Write with buffer references but zero count
 * Expected: Returns AEE_SUCCESS (buffer array ignored)
 * Type: edge case
 */
TEST(DspQueueWrite, WriteWithBufferArrayButZeroCountSucceeds)
{
    dspqueue_t queue = create_test_queue();
    uint32_t msg = 0x12345678;
    struct dspqueue_buffer dummy_buf;

    memset(&dummy_buf, 0, sizeof(dummy_buf));

    int ret = dspqueue_write_noblock(queue, 0, 0, &dummy_buf, 4, (uint8_t *)&msg);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "Write with buffer array but zero count should succeed");

    close_test_queue(queue);
}
TEST_CASE_TAGS(DspQueueWrite, WriteWithBufferArrayButZeroCountSucceeds, "DspQueue", "unit",
               "positive", "dspqueue_write");

/**
 * Test: Write with excessive buffer count
 * Expected: Returns AEE_EBADPARM
 * Type: negative
 */
TEST(DspQueueWrite, WriteWithExcessiveBufferCountFails)
{
    dspqueue_t queue = create_test_queue();
    uint32_t msg = 0x12345678;
    struct dspqueue_buffer bufs[1];

    memset(bufs, 0, sizeof(bufs));

    /* Try with unreasonably large buffer count */
    int ret = dspqueue_write_noblock(queue, 0, 1000, bufs, 4, (uint8_t *)&msg);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_NOT_EQUAL_MESSAGE(AEE_SUCCESS, ret, "Write with excessive buffer count must fail");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_EBADPARM, ret, "Should return AEE_EBADPARM");

    close_test_queue(queue);
}
TEST_CASE_TAGS(DspQueueWrite, WriteWithExcessiveBufferCountFails, "DspQueue", "unit", "negative",
               "dspqueue_write");

/* ========================================================================= */
/* Group runner                                                              */
/* ========================================================================= */

TEST_GROUP_RUNNER(DspQueueWrite)
{
    /* Section 1: Positive Tests */
    RUN_TEST_CASE(DspQueueWrite, WriteNoBlockSmallMessageSucceeds);
    RUN_TEST_CASE(DspQueueWrite, WriteBlockingWithTimeoutSucceeds);
    RUN_TEST_CASE(DspQueueWrite, WriteWithInfiniteTimeoutSucceeds);
    RUN_TEST_CASE(DspQueueWrite, WriteZeroLengthMessageSucceeds);
    RUN_TEST_CASE(DspQueueWrite, WriteWithMessageFlagSucceeds);
    RUN_TEST_CASE(DspQueueWrite, WriteMultipleMessagesSucceeds);

    /* Section 2: Negative Tests */
    // RUN_TEST_CASE(DspQueueWrite, WriteWithNullQueueFails);
    RUN_TEST_CASE(DspQueueWrite, WriteWithNullMessageBufferFails);
    RUN_TEST_CASE(DspQueueWrite, WriteWithInvalidQueueHandleFails);
    RUN_TEST_CASE(DspQueueWrite, WriteWithNullBufferReferencesButNonZeroCountFails);
    RUN_TEST_CASE(DspQueueWrite, WriteWithExcessiveMessageSizeFails);
    RUN_TEST_CASE(DspQueueWrite, WriteWithInvalidFlagsFails);
    // RUN_TEST_CASE(DspQueueWrite, WriteWithNegativeTimeoutFails);

    /* Section 3: Edge Cases */
    RUN_TEST_CASE(DspQueueWrite, WriteMaximumMessageSizeSucceeds);
    RUN_TEST_CASE(DspQueueWrite, WriteWithZeroTimeoutReturnsImmediately);
    // RUN_TEST_CASE(DspQueueWrite, WriteAfterCloseQueueFails);
    RUN_TEST_CASE(DspQueueWrite, WriteWithBothMessageAndBuffersFlagsSucceeds);
    RUN_TEST_CASE(DspQueueWrite, WriteOneByteMessageSucceeds);

    /* Section 4: Buffer Reference Validation */
    RUN_TEST_CASE(DspQueueWrite, WriteWithBufferArrayButZeroCountSucceeds);
    RUN_TEST_CASE(DspQueueWrite, WriteWithExcessiveBufferCountFails);
}
