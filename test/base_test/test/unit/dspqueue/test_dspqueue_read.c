// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file test_dspqueue_read.c
 * @brief Unit tests for dspqueue_read(), dspqueue_read_noblock(), and dspqueue_peek()
 *
 * Tests cover:
 * - Valid read operations (blocking and non-blocking)
 * - Valid peek operations
 * - Invalid parameter handling (NULL pointers, invalid handles)
 * - Buffer size validation
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

TEST_GROUP(DspQueueRead);
TEST_GROUP_META(DspQueueRead, "unit", "DSP Queue API", "Queue I/O",
                "dspqueue_read / dspqueue_read_noblock");

/* ------------------------------------------------------------------------- */
/* setUp / tearDown                                                           */
/* ------------------------------------------------------------------------- */

TEST_SETUP(DspQueueRead)
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
    int ret;
    ret = fastrpc_test_open(uri, &g_fastrpc_handle);
    if (ret != AEE_SUCCESS) {
        TEST_FAIL_MESSAGE(
            "TEST_SETUP: fastrpc_test_open failed - check skel path, ISA version, and unsigned PD");
    }
}

TEST_TEAR_DOWN(DspQueueRead)
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
 * @brief Helper to create a queue, export it, and start the DSP-side service.
 * Returns the queue handle. Fails the test on any error.
 */
static dspqueue_t create_test_queue(void)
{
    dspqueue_t queue = NULL;
    uint64_t dsp_queue_id;
    int ret;

    ret = dspqueue_create(g_test_config.domain_id, 0, QUEUE_SIZE_SMALL, QUEUE_SIZE_SMALL, NULL,
                          NULL, NULL, &queue);
    if (ret != AEE_SUCCESS) {
        printf("[helper] dspqueue_create failed: 0x%x (%s)\n", ret, test_utils_err_str(ret));
        TEST_FAIL_MESSAGE("Prerequisite: dspqueue_create must succeed");
    }
    TEST_ASSERT_NOT_NULL(queue);

    ret = dspqueue_export(queue, &dsp_queue_id);
    if (ret != AEE_SUCCESS) {
        dspqueue_close(queue);
        TEST_FAIL_MESSAGE("Prerequisite: dspqueue_export must succeed");
    }

    ret = fastrpc_test_dspqueue_start(g_fastrpc_handle, dsp_queue_id);
    if (ret != AEE_SUCCESS) {
        dspqueue_close(queue);
        printf("[helper] dspqueue_start failed: 0x%x (%s)\n", ret, test_utils_err_str(ret));
        TEST_FAIL_MESSAGE("Prerequisite: dspqueue_start must succeed");
    }

    return queue;
}

/**
 * @brief Helper to stop the DSP service and close a queue
 */
static void close_test_queue(dspqueue_t queue)
{
    uint64_t process_time = 0;
    int ret;

    ret = fastrpc_test_dspqueue_stop(g_fastrpc_handle, &process_time);
    if (ret != AEE_SUCCESS) {
        printf("[helper] dspqueue_stop failed: 0x%x (%s)\n", ret, test_utils_err_str(ret));
    }

    ret = dspqueue_close(queue);
    if (ret != AEE_SUCCESS) {
        printf("[helper] dspqueue_close failed: 0x%x (%s)\n", ret, test_utils_err_str(ret));
    }
    /* Small delay for cleanup */
    struct timespec ts = { 0, 50000000 }; /* 50ms */
    nanosleep(&ts, NULL);
}

/**
 * @brief Helper to write a test message via the DSP (into resp_queue)
 */
static int write_test_message(dspqueue_t queue, uint32_t value)
{
    (void)queue;
    return fastrpc_test_dspqueue_write_resp(g_fastrpc_handle, (const unsigned char *)&value,
                                            sizeof(value));
}

/* ========================================================================= */
/* Section 1: Positive Tests - Read from Empty Queue                        */
/* ========================================================================= */

/**
 * Test: Read from empty queue with noblock
 * Expected: Returns AEE_EWOULDBLOCK
 * Type: positive (expected behavior)
 */
TEST(DspQueueRead, ReadNoBlockFromEmptyQueueReturnsWouldBlock)
{
    dspqueue_t queue = create_test_queue();
    uint32_t msg = 0;
    uint32_t msg_len = 0;
    uint32_t flags = 0;

    int ret = dspqueue_read_noblock(queue, &flags, 0, NULL, NULL, 4, &msg_len, (uint8_t *)&msg);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_EWOULDBLOCK, ret,
                                    "Read from empty queue should return EWOULDBLOCK");

    close_test_queue(queue);
}
TEST_CASE_TAGS(DspQueueRead, ReadNoBlockFromEmptyQueueReturnsWouldBlock, "DspQueue", "unit",
               "negative", "dspqueue_read");

/**
 * Test: Peek at empty queue with noblock
 * Expected: Returns AEE_EWOULDBLOCK
 * Type: positive (expected behavior)
 */
TEST(DspQueueRead, PeekNoBlockFromEmptyQueueReturnsWouldBlock)
{
    dspqueue_t queue = create_test_queue();
    uint32_t msg_len = 0;
    uint32_t flags = 0;

    int ret = dspqueue_peek_noblock(queue, &flags, NULL, &msg_len);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_EWOULDBLOCK, ret,
                                    "Peek at empty queue should return EWOULDBLOCK");

    close_test_queue(queue);
}
TEST_CASE_TAGS(DspQueueRead, PeekNoBlockFromEmptyQueueReturnsWouldBlock, "DspQueue", "unit",
               "negative", "dspqueue_read");

/* ========================================================================= */
/* Section 2: Negative Tests - Invalid Parameters                           */
/* ========================================================================= */

/**
 * Test: Read with NULL queue handle
 * Expected: Returns error or segfaults (caught by handler)
 * Type: negative
 */
TEST(DspQueueRead, ReadWithNullQueueFails)
{
    uint32_t msg = 0;
    uint32_t msg_len = 0;
    uint32_t flags = 0;
    int ret;

    TEST_UTILS_EXPECT_SEGFAULT_BEGIN()
    {
        ret = dspqueue_read_noblock(NULL, &flags, 0, NULL, NULL, 4, &msg_len, (uint8_t *)&msg);
        REPORT_ERROR_CODE(ret);

        TEST_ASSERT_NOT_EQUAL_MESSAGE(AEE_SUCCESS, ret, "Read with NULL queue must fail");
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_EBADPARM, ret, "Should return AEE_EBADPARM");
    }
    TEST_UTILS_EXPECT_SEGFAULT_END(
        "dspqueue_read_noblock with NULL queue caused segfault - missing NULL check");
}
TEST_CASE_TAGS(DspQueueRead, ReadWithNullQueueFails, "DspQueue", "unit", "negative",
               "dspqueue_read");

/**
 * Test: Read with NULL message buffer but non-zero max length
 * Expected: Returns AEE_EBADPARM
 * Type: negative
 */
TEST(DspQueueRead, ReadWithNullMessageBufferFails)
{
    dspqueue_t queue = create_test_queue();
    uint32_t msg_len = 0;
    uint32_t flags = 0;
    int ret = AEE_SUCCESS;

    /* Write a message first */
    write_test_message(queue, 0x12345678);

    TEST_UTILS_EXPECT_SEGFAULT_BEGIN()
    {
        ret = dspqueue_read_noblock(queue, &flags, 0, NULL, NULL, 4, &msg_len, NULL);
        REPORT_ERROR_CODE(ret);

        TEST_ASSERT_NOT_EQUAL_MESSAGE(
            AEE_SUCCESS, ret, "Read with NULL message buffer and non-zero length must fail");
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_EBADPARM, ret, "Should return AEE_EBADPARM");
    }
    TEST_UTILS_EXPECT_SEGFAULT_END(
        "dspqueue_read_noblock with NULL message buffer caused segfault");

    /* Cleanup unconditionally — queue is still live if the call didn't segfault */
    close_test_queue(queue);
}
TEST_CASE_TAGS(DspQueueRead, ReadWithNullMessageBufferFails, "DspQueue", "unit", "negative",
               "dspqueue_read");

/**
 * Test: Read with NULL flags pointer
 * Expected: Returns AEE_EBADPARM or succeeds (implementation dependent)
 * Type: negative
 */
TEST(DspQueueRead, ReadWithNullFlagsPointer)
{
    dspqueue_t queue = create_test_queue();
    uint32_t msg = 0;
    uint32_t msg_len = 0;
    int ret = AEE_SUCCESS;

    /* Write a message first */
    write_test_message(queue, 0x12345678);

    TEST_UTILS_EXPECT_SEGFAULT_BEGIN()
    {
        ret = dspqueue_read_noblock(queue, NULL, 0, NULL, NULL, 4, &msg_len, (uint8_t *)&msg);
        REPORT_ERROR_CODE(ret);

        /* NULL flags might be acceptable - implementation dependent */
        if (ret != AEE_SUCCESS) {
            TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_EBADPARM, ret,
                                            "If NULL flags fails, should return AEE_EBADPARM");
        }
    }
    TEST_UTILS_EXPECT_SEGFAULT_END("dspqueue_read_noblock with NULL flags caused segfault");

    /* Cleanup unconditionally — queue is still live if the call didn't segfault */
    close_test_queue(queue);
}
TEST_CASE_TAGS(DspQueueRead, ReadWithNullFlagsPointer, "DspQueue", "unit", "negative",
               "dspqueue_read");

/**
 * Test: Read with NULL message length pointer
 * Expected: Returns AEE_EBADPARM
 * Type: negative
 */
TEST(DspQueueRead, ReadWithNullMessageLengthPointerFails)
{
    dspqueue_t queue = create_test_queue();
    uint32_t msg = 0;
    uint32_t flags = 0;
    int ret = AEE_SUCCESS;

    /* Write a message first */
    write_test_message(queue, 0x12345678);

    TEST_UTILS_EXPECT_SEGFAULT_BEGIN()
    {
        ret = dspqueue_read_noblock(queue, &flags, 0, NULL, NULL, 4, NULL, (uint8_t *)&msg);
        REPORT_ERROR_CODE(ret);

        TEST_ASSERT_NOT_EQUAL_MESSAGE(AEE_SUCCESS, ret,
                                      "Read with NULL message length pointer must fail");
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_EBADPARM, ret, "Should return AEE_EBADPARM");
    }
    TEST_UTILS_EXPECT_SEGFAULT_END(
        "dspqueue_read_noblock with NULL message length pointer caused segfault");

    /* Cleanup unconditionally — queue is still live if the call didn't segfault */
    close_test_queue(queue);
}
TEST_CASE_TAGS(DspQueueRead, ReadWithNullMessageLengthPointerFails, "DspQueue", "unit", "negative",
               "dspqueue_read");

/**
 * Test: Read with invalid queue handle (fabricated pointer)
 * Expected: Returns error or segfaults
 * Type: negative
 */
TEST(DspQueueRead, ReadWithInvalidQueueHandleFails)
{
    dspqueue_t invalid_queue = (dspqueue_t)0xDEADBEEF;
    uint32_t msg = 0;
    uint32_t msg_len = 0;
    uint32_t flags = 0;
    int ret;

    TEST_UTILS_EXPECT_SEGFAULT_BEGIN()
    {
        ret = dspqueue_read_noblock(invalid_queue, &flags, 0, NULL, NULL, 4, &msg_len,
                                    (uint8_t *)&msg);
        REPORT_ERROR_CODE(ret);

        TEST_ASSERT_NOT_EQUAL_MESSAGE(AEE_SUCCESS, ret, "Read with invalid queue handle must fail");
    }
    TEST_UTILS_EXPECT_SEGFAULT_END(
        "dspqueue_read_noblock with invalid queue handle caused segfault");
}
TEST_CASE_TAGS(DspQueueRead, ReadWithInvalidQueueHandleFails, "DspQueue", "unit", "negative",
               "dspqueue_read");

/**
 * Test: Read with NULL buffer references but non-zero max count
 * Expected: Returns AEE_EBADPARM
 * Type: negative
 */
TEST(DspQueueRead, ReadWithNullBufferReferencesButNonZeroMaxFails)
{
    dspqueue_t queue = create_test_queue();
    uint32_t msg = 0;
    uint32_t msg_len = 0;
    uint32_t flags = 0;
    uint32_t num_bufs = 0;
    int ret = AEE_SUCCESS;

    /* Write a message first */
    write_test_message(queue, 0x12345678);

    TEST_UTILS_EXPECT_SEGFAULT_BEGIN()
    {
        ret = dspqueue_read_noblock(queue, &flags, 2, &num_bufs, NULL, 4, &msg_len,
                                    (uint8_t *)&msg);
        REPORT_ERROR_CODE(ret);

        TEST_ASSERT_NOT_EQUAL_MESSAGE(AEE_SUCCESS, ret,
                                      "Read with NULL buffer array but non-zero max must fail");
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_EBADPARM, ret, "Should return AEE_EBADPARM");
    }
    TEST_UTILS_EXPECT_SEGFAULT_END("dspqueue_read_noblock with NULL buffer array caused segfault");

    /* Cleanup unconditionally — queue is still live if the call didn't segfault */
    close_test_queue(queue);
}
TEST_CASE_TAGS(DspQueueRead, ReadWithNullBufferReferencesButNonZeroMaxFails, "DspQueue", "unit",
               "negative", "dspqueue_read");

/**
 * Test: Peek with NULL queue handle
 * Expected: Returns error or segfaults
 * Type: negative
 */
TEST(DspQueueRead, PeekWithNullQueueFails)
{
    uint32_t msg_len = 0;
    uint32_t flags = 0;
    int ret;

    TEST_UTILS_EXPECT_SEGFAULT_BEGIN()
    {
        ret = dspqueue_peek_noblock(NULL, &flags, NULL, &msg_len);
        REPORT_ERROR_CODE(ret);

        TEST_ASSERT_NOT_EQUAL_MESSAGE(AEE_SUCCESS, ret, "Peek with NULL queue must fail");
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_EBADPARM, ret, "Should return AEE_EBADPARM");
    }
    TEST_UTILS_EXPECT_SEGFAULT_END("dspqueue_peek_noblock with NULL queue caused segfault");
}
TEST_CASE_TAGS(DspQueueRead, PeekWithNullQueueFails, "DspQueue", "unit", "negative",
               "dspqueue_read");

/**
 * Test: Peek with NULL message length pointer
 * Expected: Returns AEE_EBADPARM
 * Type: negative
 */
TEST(DspQueueRead, PeekWithNullMessageLengthPointerFails)
{
    dspqueue_t queue = create_test_queue();
    uint32_t flags = 0;
    int ret = AEE_SUCCESS;

    /* Write a message first */
    write_test_message(queue, 0x12345678);

    TEST_UTILS_EXPECT_SEGFAULT_BEGIN()
    {
        ret = dspqueue_peek_noblock(queue, &flags, NULL, NULL);
        REPORT_ERROR_CODE(ret);

        TEST_ASSERT_NOT_EQUAL_MESSAGE(AEE_SUCCESS, ret,
                                      "Peek with NULL message length pointer must fail");
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_EBADPARM, ret, "Should return AEE_EBADPARM");
    }
    TEST_UTILS_EXPECT_SEGFAULT_END(
        "dspqueue_peek_noblock with NULL message length pointer caused segfault");

    /* Cleanup unconditionally — queue is still live if the call didn't segfault */
    close_test_queue(queue);
}
TEST_CASE_TAGS(DspQueueRead, PeekWithNullMessageLengthPointerFails, "DspQueue", "unit", "negative",
               "dspqueue_read");

/* ========================================================================= */
/* Section 3: Edge Cases and Boundary Conditions                            */
/* ========================================================================= */

/**
 * Test: Read with zero max message length
 * Expected: Returns AEE_SUCCESS or AEE_EBADPARM (implementation dependent)
 * Type: edge case
 */
TEST(DspQueueRead, ReadWithZeroMaxMessageLength)
{
    dspqueue_t queue = create_test_queue();
    uint32_t msg_len = 0;
    uint32_t flags = 0;

    /* Write a message first */
    write_test_message(queue, 0x12345678);

    int ret = dspqueue_read_noblock(queue, &flags, 0, NULL, NULL, 0, &msg_len, NULL);
    REPORT_ERROR_CODE(ret);

    /*
     * Cleanup BEFORE assertion: close the queue unconditionally so that a
     * longjmp from the assertion below cannot skip cleanup.
     */
    close_test_queue(queue);

    /* Zero length might be valid for getting message info without reading data */
    TEST_ASSERT_TRUE_MESSAGE(
        (ret == AEE_SUCCESS) || (ret == AEE_EBADPARM) || (ret == AEE_EBUFFERTOOSMALL),
        "Read with zero max length should return SUCCESS, EBADPARM, or EBUFFERTOOSMALL");
}
TEST_CASE_TAGS(DspQueueRead, ReadWithZeroMaxMessageLength, "DspQueue", "unit", "positive",
               "dspqueue_read");

/**
 * Test: Read with timeout of zero (should return immediately)
 * Expected: Returns AEE_EWOULDBLOCK or AEE_SUCCESS
 * Type: edge case
 */
TEST(DspQueueRead, ReadWithZeroTimeoutReturnsImmediately)
{
    dspqueue_t queue = create_test_queue();
    uint32_t msg = 0;
    uint32_t msg_len = 0;
    uint32_t flags = 0;
    uint64_t t1, t2;
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    t1 = ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000;

    int ret = dspqueue_read(queue, &flags, 0, NULL, NULL, 4, &msg_len, (uint8_t *)&msg, 0);
    REPORT_ERROR_CODE(ret);

    clock_gettime(CLOCK_MONOTONIC, &ts);
    t2 = ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000;

    /* Should return immediately (within 10ms) */
    TEST_ASSERT_LESS_THAN_MESSAGE(10000, (t2 - t1),
                                  "Read with zero timeout should return immediately");

    /* Result should be EWOULDBLOCK for empty queue */
    TEST_ASSERT_TRUE_MESSAGE((ret == AEE_EWOULDBLOCK)
                                 || ((ret & 0xFFFF) == (AEE_EEXPIRED & 0xFFFF)),
                             "Should return EWOULDBLOCK or EEXPIRED");

    close_test_queue(queue);
}
TEST_CASE_TAGS(DspQueueRead, ReadWithZeroTimeoutReturnsImmediately, "DspQueue", "unit", "positive",
               "dspqueue_read");

/**
 * Test: Read after queue is closed
 * Expected: Returns error
 * Type: edge case
 */
TEST(DspQueueRead, ReadAfterCloseQueueFails)
{
    dspqueue_t queue = create_test_queue();
    uint32_t msg = 0;
    uint32_t msg_len = 0;
    uint32_t flags = 0;
    int ret;
    /* stop+close properly so the DSP-side queue is released first;
     * dspqueue_close alone returns AEE_EBADPARM while still imported */
    close_test_queue(queue);

    /* Try to read after close — queue memory is freed, may segfault */
    TEST_UTILS_EXPECT_SEGFAULT_BEGIN()
    {
        ret = dspqueue_read_noblock(queue, &flags, 0, NULL, NULL, 4, &msg_len, (uint8_t *)&msg);
        REPORT_ERROR_CODE(ret);

        /*
         * If the implementation returned 0x0 instead of failing, mark the
         * test as failed.  No live queue exists at this point so there is
         * nothing to clean up.
         */
        TEST_ASSERT_NOT_EQUAL_MESSAGE(AEE_SUCCESS, ret, "Read after close must fail");
    }
    TEST_UTILS_EXPECT_SEGFAULT_END(
        "dspqueue_read_noblock after close caused segfault - queue memory freed");
}
TEST_CASE_TAGS(DspQueueRead, ReadAfterCloseQueueFails, "DspQueue", "unit", "negative",
               "dspqueue_read");

/**
 * Test: Peek after queue is closed
 * Expected: Returns error
 * Type: edge case
 */
TEST(DspQueueRead, PeekAfterCloseQueueFails)
{
    dspqueue_t queue = create_test_queue();
    uint32_t msg_len = 0;
    uint32_t flags = 0;
    int ret;
    /* stop+close properly so the DSP-side queue is released first */
    close_test_queue(queue);

    /* Try to peek after close — queue memory is freed, may segfault */
    TEST_UTILS_EXPECT_SEGFAULT_BEGIN()
    {
        ret = dspqueue_peek_noblock(queue, &flags, NULL, &msg_len);
        REPORT_ERROR_CODE(ret);

        /*
         * If the implementation returned 0x0 instead of failing, mark the
         * test as failed.  No live queue exists at this point so there is
         * nothing to clean up.
         */
        TEST_ASSERT_NOT_EQUAL_MESSAGE(AEE_SUCCESS, ret, "Peek after close must fail");
    }
    TEST_UTILS_EXPECT_SEGFAULT_END(
        "dspqueue_peek_noblock after close caused segfault - queue memory freed");
}
TEST_CASE_TAGS(DspQueueRead, PeekAfterCloseQueueFails, "DspQueue", "unit", "negative",
               "dspqueue_read");

/**
 * Test: Read with buffer array but zero max buffers
 * Expected: Returns AEE_SUCCESS (buffer array ignored)
 * Type: edge case
 */
TEST(DspQueueRead, ReadWithBufferArrayButZeroMaxSucceeds)
{
    dspqueue_t queue = create_test_queue();
    uint32_t msg = 0;
    uint32_t msg_len = 0;
    uint32_t flags = 0;
    uint32_t num_bufs = 0;
    struct dspqueue_buffer dummy_buf;

    /* Write a message first */
    write_test_message(queue, 0x12345678);

    memset(&dummy_buf, 0, sizeof(dummy_buf));

    int ret = dspqueue_read_noblock(queue, &flags, 0, &num_bufs, &dummy_buf, 4, &msg_len,
                                    (uint8_t *)&msg);
    REPORT_ERROR_CODE(ret);

    /*
     * Cleanup BEFORE assertion: close the queue unconditionally so that a
     * longjmp from the assertion below cannot skip cleanup.
     */
    close_test_queue(queue);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "Read with buffer array but zero max should succeed");
    TEST_ASSERT_EQUAL(0, num_bufs);
}
TEST_CASE_TAGS(DspQueueRead, ReadWithBufferArrayButZeroMaxSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_read");

/**
 * Test: Read with excessive max buffer count
 * Expected: Returns AEE_SUCCESS or AEE_EBADPARM
 * Type: edge case
 */
TEST(DspQueueRead, ReadWithExcessiveMaxBufferCount)
{
    dspqueue_t queue = create_test_queue();
    uint32_t msg = 0;
    uint32_t msg_len = 0;
    uint32_t flags = 0;
    uint32_t num_bufs = 0;
    struct dspqueue_buffer bufs[1];

    /* Write a message first */
    write_test_message(queue, 0x12345678);

    memset(bufs, 0, sizeof(bufs));

    /* Try with unreasonably large max buffer count */
    int ret
        = dspqueue_read_noblock(queue, &flags, 1000, &num_bufs, bufs, 4, &msg_len, (uint8_t *)&msg);
    REPORT_ERROR_CODE(ret);

    /*
     * Cleanup BEFORE assertion: close the queue unconditionally so that a
     * longjmp from the assertion below cannot skip cleanup.
     */
    close_test_queue(queue);

    /* Implementation may accept this or reject it */
    TEST_ASSERT_TRUE_MESSAGE(
        (ret == AEE_SUCCESS) || (ret == AEE_EBADPARM),
        "Read with excessive max buffer count should return SUCCESS or EBADPARM");
}
TEST_CASE_TAGS(DspQueueRead, ReadWithExcessiveMaxBufferCount, "DspQueue", "unit", "negative",
               "dspqueue_read");

/**
 * Test: Blocking peek with timeout
 * Expected: Returns AEE_EEXPIRED for empty queue
 * Type: edge case
 */
TEST(DspQueueRead, PeekWithTimeoutOnEmptyQueueExpires)
{
    dspqueue_t queue = create_test_queue();
    uint32_t msg_len = 0;
    uint32_t flags = 0;
    uint64_t t1, t2;
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    t1 = ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000;

    /* Peek with short timeout on empty queue */
    int ret = dspqueue_peek(queue, &flags, NULL, &msg_len, 50000); /* 50ms */
    REPORT_ERROR_CODE(ret);

    clock_gettime(CLOCK_MONOTONIC, &ts);
    t2 = ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000;

    TEST_ASSERT_NOT_EQUAL_MESSAGE(AEE_SUCCESS, ret, "Peek with timeout on empty queue should fail");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_EEXPIRED, ret, "Should return AEE_EEXPIRED");

    /* Should have waited approximately the timeout duration */
    TEST_ASSERT_GREATER_THAN_MESSAGE(40000, (t2 - t1), "Should have waited at least 40ms");
    TEST_ASSERT_LESS_THAN_MESSAGE(100000, (t2 - t1), "Should not have waited more than 100ms");

    close_test_queue(queue);
}
TEST_CASE_TAGS(DspQueueRead, PeekWithTimeoutOnEmptyQueueExpires, "DspQueue", "unit", "negative",
               "dspqueue_read");

/* ========================================================================= */
/* Group runner                                                              */
/* ========================================================================= */

TEST_GROUP_RUNNER(DspQueueRead)
{
    /* Section 1: Read from Empty Queue */
    RUN_TEST_CASE(DspQueueRead, ReadNoBlockFromEmptyQueueReturnsWouldBlock);
    RUN_TEST_CASE(DspQueueRead, PeekNoBlockFromEmptyQueueReturnsWouldBlock);

    /* Section 2: Negative Tests */
    // RUN_TEST_CASE(DspQueueRead, ReadWithNullQueueFails);
    // RUN_TEST_CASE(DspQueueRead, ReadWithNullMessageBufferFails);
    RUN_TEST_CASE(DspQueueRead, ReadWithNullFlagsPointer);
    // RUN_TEST_CASE(DspQueueRead, ReadWithNullMessageLengthPointerFails);
    // RUN_TEST_CASE(DspQueueRead, ReadWithInvalidQueueHandleFails);
    // RUN_TEST_CASE(DspQueueRead, ReadWithNullBufferReferencesButNonZeroMaxFails);
    // RUN_TEST_CASE(DspQueueRead, PeekWithNullQueueFails);
    // RUN_TEST_CASE(DspQueueRead, PeekWithNullMessageLengthPointerFails);

    /* Section 3: Edge Cases */
    RUN_TEST_CASE(DspQueueRead, ReadWithZeroMaxMessageLength);
    RUN_TEST_CASE(DspQueueRead, ReadWithZeroTimeoutReturnsImmediately);
    // RUN_TEST_CASE(DspQueueRead, ReadAfterCloseQueueFails);
    // RUN_TEST_CASE(DspQueueRead, PeekAfterCloseQueueFails);
    RUN_TEST_CASE(DspQueueRead, ReadWithBufferArrayButZeroMaxSucceeds);
    RUN_TEST_CASE(DspQueueRead, ReadWithExcessiveMaxBufferCount);
    RUN_TEST_CASE(DspQueueRead, PeekWithTimeoutOnEmptyQueueExpires);
}
