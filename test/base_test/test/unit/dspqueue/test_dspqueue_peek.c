// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file test_dspqueue_peek.c
 * @brief Unit tests for dspqueue_peek() and dspqueue_peek_noblock()
 *
 * Tests cover:
 * - Valid peek operations (blocking and non-blocking)
 * - Peek without consuming messages
 * - Invalid parameter handling (NULL pointers, invalid handles)
 * - Timeout parameter validation
 * - Flag validation
 * - Error conditions
 * - Peek behavior with empty and non-empty queues
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

TEST_GROUP(DspQueuePeek);
TEST_GROUP_META(DspQueuePeek, "unit", "DSP Queue API", "Queue I/O",
                "dspqueue_peek / dspqueue_peek_noblock");

/* ------------------------------------------------------------------------- */
/* setUp / tearDown                                                           */
/* ------------------------------------------------------------------------- */

TEST_SETUP(DspQueuePeek)
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

TEST_TEAR_DOWN(DspQueuePeek)
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
    REPORT_ERROR_CODE(ret);
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
 * @brief Helper to write a test message to queue via the DSP (into resp_queue)
 */
static int write_test_message(dspqueue_t queue, uint32_t value)
{
    (void)queue;
    return fastrpc_test_dspqueue_write_resp(g_fastrpc_handle, (const unsigned char *)&value,
                                            sizeof(value));
}

/**
 * @brief Helper to write a test message with flags via the DSP (into resp_queue)
 */
static int write_test_message_with_flags(dspqueue_t queue, uint32_t flags, uint32_t value)
{
    (void)queue;
    (void)flags;
    /* flags are set by the DSP-side dspqueue_write; pass the payload only */
    return fastrpc_test_dspqueue_write_resp(g_fastrpc_handle, (const unsigned char *)&value,
                                            sizeof(value));
}

/**
 * @brief Helper to write a zero-length message via the DSP (into resp_queue)
 */
static int write_zero_length_message(dspqueue_t queue)
{
    (void)queue;
    return fastrpc_test_dspqueue_write_resp(g_fastrpc_handle, NULL, 0);
}

/* ========================================================================= */
/* Section 1: Positive Tests - Peek from Empty Queue                        */
/* ========================================================================= */

/**
 * Test: Peek at empty queue with noblock
 * Expected: Returns AEE_EWOULDBLOCK
 * Type: positive (expected behavior)
 */
TEST(DspQueuePeek, PeekNoBlockFromEmptyQueueReturnsWouldBlock)
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
TEST_CASE_TAGS(DspQueuePeek, PeekNoBlockFromEmptyQueueReturnsWouldBlock, "DspQueue", "unit",
               "negative", "dspqueue_peek");

/**
 * Test: Peek at empty queue with timeout
 * Expected: Returns AEE_EEXPIRED after timeout
 * Type: positive (expected behavior)
 */
TEST(DspQueuePeek, PeekWithTimeoutFromEmptyQueueExpires)
{
    dspqueue_t queue = create_test_queue();
    uint32_t msg_len = 0;
    uint32_t flags = 0;
    uint64_t t1, t2;
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    t1 = ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000;

    int ret = dspqueue_peek(queue, &flags, NULL, &msg_len, 50000); /* 50ms */
    REPORT_ERROR_CODE(ret);

    clock_gettime(CLOCK_MONOTONIC, &ts);
    t2 = ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000;

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_EEXPIRED, ret,
                                    "Peek with timeout on empty queue should return EEXPIRED");

    /* Should have waited approximately the timeout duration */
    TEST_ASSERT_GREATER_THAN_MESSAGE(40000, (t2 - t1), "Should have waited at least 40ms");
    TEST_ASSERT_LESS_THAN_MESSAGE(100000, (t2 - t1), "Should not have waited more than 100ms");

    close_test_queue(queue);
}
TEST_CASE_TAGS(DspQueuePeek, PeekWithTimeoutFromEmptyQueueExpires, "DspQueue", "unit", "negative",
               "dspqueue_peek");

/**
 * Test: Peek at queue with message
 * Expected: Returns AEE_SUCCESS and message info
 * Type: positive
 */
TEST(DspQueuePeek, PeekNoBlockWithMessageSucceeds)
{
    dspqueue_t queue = create_test_queue();
    uint32_t msg_len = 0;
    uint32_t flags = 0;

    /* Write a message first */
    int write_ret = write_test_message(queue, 0x12345678);
    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, write_ret);

    int ret = dspqueue_peek_noblock(queue, &flags, NULL, &msg_len);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret, "Peek at queue with message should succeed");
    TEST_ASSERT_EQUAL_MESSAGE(4, msg_len, "Message length should be 4 bytes");

    close_test_queue(queue);
}
TEST_CASE_TAGS(DspQueuePeek, PeekNoBlockWithMessageSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_peek");

/**
 * Test: Peek with blocking call and timeout
 * Expected: Returns AEE_SUCCESS
 * Type: positive
 */
TEST(DspQueuePeek, PeekBlockingWithTimeoutSucceeds)
{
    dspqueue_t queue = create_test_queue();
    uint32_t msg_len = 0;
    uint32_t flags = 0;

    /* Write a message first */
    int write_ret = write_test_message(queue, 0xABCDEF00);
    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, write_ret);

    int ret = dspqueue_peek(queue, &flags, NULL, &msg_len, SHORT_TIMEOUT_US);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret, "dspqueue_peek with timeout should succeed");
    TEST_ASSERT_EQUAL_MESSAGE(4, msg_len, "Message length should be 4 bytes");

    close_test_queue(queue);
}
TEST_CASE_TAGS(DspQueuePeek, PeekBlockingWithTimeoutSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_peek");

/**
 * Test: Peek zero-length message
 * Expected: Returns AEE_SUCCESS with msg_len = 0
 * Type: positive
 */
TEST(DspQueuePeek, PeekZeroLengthMessageSucceeds)
{
    dspqueue_t queue = create_test_queue();
    uint32_t msg_len = 0;
    uint32_t flags = 0;

    /* Write a zero-length message */
    int write_ret = write_zero_length_message(queue);
    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, write_ret);

    int ret = dspqueue_peek_noblock(queue, &flags, NULL, &msg_len);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret, "Peek at zero-length message should succeed");
    TEST_ASSERT_EQUAL_MESSAGE(0, msg_len, "Message length should be 0 bytes");

    close_test_queue(queue);
}
TEST_CASE_TAGS(DspQueuePeek, PeekZeroLengthMessageSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_peek");

/**
 * Test: Peek does not consume message
 * Expected: Multiple peeks return same message info
 * Type: positive
 */
TEST(DspQueuePeek, PeekDoesNotConsumeMessage)
{
    dspqueue_t queue = create_test_queue();
    uint32_t msg_len1 = 0, msg_len2 = 0;
    uint32_t flags1 = 0, flags2 = 0;

    /* Write a message */
    int write_ret = write_test_message(queue, 0x11223344);
    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, write_ret);

    /* First peek */
    int ret1 = dspqueue_peek_noblock(queue, &flags1, NULL, &msg_len1);
    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, ret1);
    TEST_ASSERT_EQUAL(4, msg_len1);

    /* Second peek should return same message */
    int ret2 = dspqueue_peek_noblock(queue, &flags2, NULL, &msg_len2);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret2, "Second peek should also succeed");
    TEST_ASSERT_EQUAL_MESSAGE(msg_len1, msg_len2, "Message length should be same on second peek");
    TEST_ASSERT_EQUAL_MESSAGE(flags1, flags2, "Flags should be same on second peek");

    close_test_queue(queue);
}
TEST_CASE_TAGS(DspQueuePeek, PeekDoesNotConsumeMessage, "DspQueue", "unit", "positive",
               "dspqueue_peek");

/**
 * Test: Peek with MESSAGE flag
 * Expected: Returns AEE_SUCCESS and flags contain MESSAGE
 * Type: positive
 */
TEST(DspQueuePeek, PeekMessageWithMessageFlagSucceeds)
{
    dspqueue_t queue = create_test_queue();
    uint32_t msg_len = 0;
    uint32_t flags = 0;

    /* Write a message with MESSAGE flag */
    int write_ret = write_test_message_with_flags(queue, DSPQUEUE_PACKET_FLAG_MESSAGE, 0x55667788);
    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, write_ret);

    int ret = dspqueue_peek_noblock(queue, &flags, NULL, &msg_len);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret, "Peek should succeed");
    TEST_ASSERT_EQUAL_MESSAGE(4, msg_len, "Message length should be 4 bytes");
    TEST_ASSERT_EQUAL_MESSAGE(DSPQUEUE_PACKET_FLAG_MESSAGE, flags,
                              "Flags should contain MESSAGE flag");

    close_test_queue(queue);
}
TEST_CASE_TAGS(DspQueuePeek, PeekMessageWithMessageFlagSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_peek");

/**
 * Test: Peek multiple messages sequentially
 * Expected: Each peek returns correct message info
 * Type: positive
 */
TEST(DspQueuePeek, PeekMultipleMessagesSequentially)
{
    dspqueue_t queue = create_test_queue();
    int i;

    /* Write multiple messages */
    for (i = 0; i < 5; i++) {
        uint32_t msg = i;
        int ret = write_test_message(queue, msg);
        REPORT_ERROR_CODE(ret);
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret, "Each write should succeed");
    }

    /* Peek each message */
    for (i = 0; i < 5; i++) {
        uint32_t msg_len = 0;
        uint32_t flags = 0;
        int ret = dspqueue_peek_noblock(queue, &flags, NULL, &msg_len);
        REPORT_ERROR_CODE(ret);

        TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret, "Each peek should succeed");
        TEST_ASSERT_EQUAL_MESSAGE(4, msg_len, "Each message should be 4 bytes");
    }

    close_test_queue(queue);
}
TEST_CASE_TAGS(DspQueuePeek, PeekMultipleMessagesSequentially, "DspQueue", "unit", "positive",
               "dspqueue_peek");

/* ========================================================================= */
/* Section 2: Negative Tests - Invalid Parameters                           */
/* ========================================================================= */

/**
 * Test: Peek with NULL queue handle
 * Expected: Returns error or segfaults (caught by handler)
 * Type: negative
 */
TEST(DspQueuePeek, PeekWithNullQueueFails)
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
    TEST_UTILS_EXPECT_SEGFAULT_END(
        "dspqueue_peek_noblock with NULL queue caused segfault - missing NULL check");
}
TEST_CASE_TAGS(DspQueuePeek, PeekWithNullQueueFails, "DspQueue", "unit", "negative",
               "dspqueue_peek");

/**
 * Test: Peek with NULL message length pointer
 * Expected: Returns AEE_EBADPARM
 * Type: negative
 */
TEST(DspQueuePeek, PeekWithNullMessageLengthPointerFails)
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
TEST_CASE_TAGS(DspQueuePeek, PeekWithNullMessageLengthPointerFails, "DspQueue", "unit", "negative",
               "dspqueue_peek");

/**
 * Test: Peek with NULL flags pointer
 * Expected: Returns AEE_EBADPARM or succeeds (implementation dependent)
 * Type: negative
 */
TEST(DspQueuePeek, PeekWithNullFlagsPointer)
{
    dspqueue_t queue = create_test_queue();
    uint32_t msg_len = 0;
    int ret = AEE_SUCCESS;

    /* Write a message first */
    write_test_message(queue, 0x12345678);

    TEST_UTILS_EXPECT_SEGFAULT_BEGIN()
    {
        ret = dspqueue_peek_noblock(queue, NULL, NULL, &msg_len);
        REPORT_ERROR_CODE(ret);

        /* NULL flags might be acceptable - implementation dependent */
        if (ret != AEE_SUCCESS) {
            TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_EBADPARM, ret,
                                            "If NULL flags fails, should return AEE_EBADPARM");
        }
    }
    TEST_UTILS_EXPECT_SEGFAULT_END("dspqueue_peek_noblock with NULL flags caused segfault");

    /* Cleanup unconditionally — queue is still live if the call didn't segfault */
    close_test_queue(queue);
}
TEST_CASE_TAGS(DspQueuePeek, PeekWithNullFlagsPointer, "DspQueue", "unit", "negative",
               "dspqueue_peek");

/**
 * Test: Peek with invalid queue handle (fabricated pointer)
 * Expected: Returns error or segfaults
 * Type: negative
 */
TEST(DspQueuePeek, PeekWithInvalidQueueHandleFails)
{
    dspqueue_t invalid_queue = (dspqueue_t)0xDEADBEEF;
    uint32_t msg_len = 0;
    uint32_t flags = 0;
    int ret;

    TEST_UTILS_EXPECT_SEGFAULT_BEGIN()
    {
        ret = dspqueue_peek_noblock(invalid_queue, &flags, NULL, &msg_len);
        REPORT_ERROR_CODE(ret);

        TEST_ASSERT_NOT_EQUAL_MESSAGE(AEE_SUCCESS, ret, "Peek with invalid queue handle must fail");
    }
    TEST_UTILS_EXPECT_SEGFAULT_END(
        "dspqueue_peek_noblock with invalid queue handle caused segfault");
}
TEST_CASE_TAGS(DspQueuePeek, PeekWithInvalidQueueHandleFails, "DspQueue", "unit", "negative",
               "dspqueue_peek");

/* ========================================================================= */
/* Section 3: Edge Cases and Boundary Conditions                            */
/* ========================================================================= */

/**
 * Test: Peek with zero timeout (should return immediately)
 * Expected: Returns AEE_EWOULDBLOCK or AEE_SUCCESS
 * Type: edge case
 */
TEST(DspQueuePeek, PeekWithZeroTimeoutReturnsImmediately)
{
    dspqueue_t queue = create_test_queue();
    uint32_t msg_len = 0;
    uint32_t flags = 0;
    uint64_t t1, t2;
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    t1 = ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000;

    int ret = dspqueue_peek(queue, &flags, NULL, &msg_len, 0);
    REPORT_ERROR_CODE(ret);

    clock_gettime(CLOCK_MONOTONIC, &ts);
    t2 = ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000;

    /* Should return immediately (within 10ms) */
    TEST_ASSERT_LESS_THAN_MESSAGE(10000, (t2 - t1),
                                  "Peek with zero timeout should return immediately");

    /* Result should be EWOULDBLOCK for empty queue */
    TEST_ASSERT_TRUE_MESSAGE((ret == AEE_EWOULDBLOCK)
                                 || ((ret & 0xFFFF) == (AEE_EEXPIRED & 0xFFFF)),
                             "Should return EWOULDBLOCK or EEXPIRED");

    close_test_queue(queue);
}
TEST_CASE_TAGS(DspQueuePeek, PeekWithZeroTimeoutReturnsImmediately, "DspQueue", "unit", "positive",
               "dspqueue_peek");

/**
 * Test: Peek after queue is closed
 * Expected: Returns error
 * Type: edge case
 */
TEST(DspQueuePeek, PeekAfterCloseQueueFails)
{
    dspqueue_t queue = create_test_queue();
    uint32_t msg_len = 0;
    uint32_t flags = 0;
    int ret;

    /* stop+close properly so the DSP-side queue is released first;
     * dspqueue_close alone returns AEE_EBADPARM while still imported */
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
TEST_CASE_TAGS(DspQueuePeek, PeekAfterCloseQueueFails, "DspQueue", "unit", "negative",
               "dspqueue_peek");

/**
 * Test: Peek with num_buffers pointer
 * Expected: Returns AEE_SUCCESS with num_buffers = 0 (no buffers in message)
 * Type: edge case
 */
TEST(DspQueuePeek, PeekWithNumBuffersPointer)
{
    dspqueue_t queue = create_test_queue();
    uint32_t msg_len = 0;
    uint32_t flags = 0;
    uint32_t num_buffers = 0;

    /* Write a message without buffers */
    int write_ret = write_test_message(queue, 0x12345678);
    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, write_ret);

    int ret = dspqueue_peek_noblock(queue, &flags, &num_buffers, &msg_len);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "Peek with num_buffers pointer should succeed");
    TEST_ASSERT_EQUAL_MESSAGE(0, num_buffers,
                              "num_buffers should be 0 for message without buffers");
    TEST_ASSERT_EQUAL_MESSAGE(4, msg_len, "Message length should be 4 bytes");

    close_test_queue(queue);
}
TEST_CASE_TAGS(DspQueuePeek, PeekWithNumBuffersPointer, "DspQueue", "unit", "positive",
               "dspqueue_peek");

/**
 * Test: Peek with NULL num_buffers pointer
 * Expected: Returns AEE_SUCCESS (num_buffers is optional)
 * Type: edge case
 */
TEST(DspQueuePeek, PeekWithNullNumBuffersPointer)
{
    dspqueue_t queue = create_test_queue();
    uint32_t msg_len = 0;
    uint32_t flags = 0;

    /* Write a message */
    int write_ret = write_test_message(queue, 0x12345678);
    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, write_ret);

    int ret = dspqueue_peek_noblock(queue, &flags, NULL, &msg_len);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "Peek with NULL num_buffers pointer should succeed");
    TEST_ASSERT_EQUAL_MESSAGE(4, msg_len, "Message length should be 4 bytes");

    close_test_queue(queue);
}
TEST_CASE_TAGS(DspQueuePeek, PeekWithNullNumBuffersPointer, "DspQueue", "unit", "positive",
               "dspqueue_peek");

/**
 * Test: Peek large message
 * Expected: Returns AEE_SUCCESS with correct message length
 * Type: edge case
 */
TEST(DspQueuePeek, PeekLargeMessageSucceeds)
{
    dspqueue_t queue = create_test_queue();
    uint32_t msg_len = 0;
    uint32_t flags = 0;
    void *large_msg = NULL;
    size_t large_msg_size = 1024; /* 1KB message */

    large_msg = malloc(large_msg_size);
    TEST_ASSERT_NOT_NULL(large_msg);
    memset(large_msg, 0xAB, large_msg_size);

    /* Write large message via DSP into resp_queue */
    int write_ret = fastrpc_test_dspqueue_write_resp(
        g_fastrpc_handle, (const unsigned char *)large_msg, (int)large_msg_size);
    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, write_ret);

    int ret = dspqueue_peek_noblock(queue, &flags, NULL, &msg_len);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret, "Peek at large message should succeed");
    TEST_ASSERT_EQUAL_MESSAGE(large_msg_size, msg_len, "Message length should match written size");

    free(large_msg);
    close_test_queue(queue);
}
TEST_CASE_TAGS(DspQueuePeek, PeekLargeMessageSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_peek");

/**
 * Test: Peek then read returns same message
 * Expected: Peek and read return same message info
 * Type: edge case
 */
TEST(DspQueuePeek, PeekThenReadReturnsSameMessage)
{
    dspqueue_t queue = create_test_queue();
    uint32_t peek_msg_len = 0;
    uint32_t read_msg_len = 0;
    uint32_t peek_flags = 0;
    uint32_t read_flags = 0;
    uint32_t msg_data = 0;

    /* Write a message */
    uint32_t write_data = 0x12345678;
    int write_ret = write_test_message(queue, write_data);
    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, write_ret);

    /* Peek the message */
    int peek_ret = dspqueue_peek_noblock(queue, &peek_flags, NULL, &peek_msg_len);
    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, peek_ret);

    /* Read the message */
    int read_ret = dspqueue_read_noblock(queue, &read_flags, 0, NULL, NULL, 4, &read_msg_len,
                                         (uint8_t *)&msg_data);
    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, read_ret);

    /* Verify peek and read returned same info */
    TEST_ASSERT_EQUAL_MESSAGE(peek_msg_len, read_msg_len,
                              "Peek and read should return same message length");
    TEST_ASSERT_EQUAL_MESSAGE(peek_flags, read_flags, "Peek and read should return same flags");
    TEST_ASSERT_EQUAL_MESSAGE(write_data, msg_data, "Read message should match written data");

    close_test_queue(queue);
}
TEST_CASE_TAGS(DspQueuePeek, PeekThenReadReturnsSameMessage, "DspQueue", "unit", "positive",
               "dspqueue_peek");

/**
 * Test: Peek with very short timeout
 * Expected: Returns AEE_EEXPIRED for empty queue
 * Type: edge case
 */
TEST(DspQueuePeek, PeekWithVeryShortTimeoutExpires)
{
    dspqueue_t queue = create_test_queue();
    uint32_t msg_len = 0;
    uint32_t flags = 0;

    int ret = dspqueue_peek(queue, &flags, NULL, &msg_len, 1000); /* 1ms */
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_EEXPIRED, ret,
                                    "Peek with very short timeout should expire");

    close_test_queue(queue);
}
TEST_CASE_TAGS(DspQueuePeek, PeekWithVeryShortTimeoutExpires, "DspQueue", "unit", "negative",
               "dspqueue_peek");

/* ========================================================================= */
/* Group runner                                                              */
/* ========================================================================= */

TEST_GROUP_RUNNER(DspQueuePeek)
{
    /* Section 1: Positive Tests - Empty Queue */
    RUN_TEST_CASE(DspQueuePeek, PeekNoBlockFromEmptyQueueReturnsWouldBlock);
    RUN_TEST_CASE(DspQueuePeek, PeekWithTimeoutFromEmptyQueueExpires);

    /* Section 1: Positive Tests - With Message */
    RUN_TEST_CASE(DspQueuePeek, PeekNoBlockWithMessageSucceeds);
    RUN_TEST_CASE(DspQueuePeek, PeekBlockingWithTimeoutSucceeds);
    RUN_TEST_CASE(DspQueuePeek, PeekZeroLengthMessageSucceeds);
    RUN_TEST_CASE(DspQueuePeek, PeekDoesNotConsumeMessage);
    // RUN_TEST_CASE(DspQueuePeek, PeekMessageWithMessageFlagSucceeds);
    RUN_TEST_CASE(DspQueuePeek, PeekMultipleMessagesSequentially);

    /* Section 2: Negative Tests */
    // RUN_TEST_CASE(DspQueuePeek, PeekWithNullQueueFails);
    // RUN_TEST_CASE(DspQueuePeek, PeekWithNullMessageLengthPointerFails);
    RUN_TEST_CASE(DspQueuePeek, PeekWithNullFlagsPointer);
    // RUN_TEST_CASE(DspQueuePeek, PeekWithInvalidQueueHandleFails);

    /* Section 3: Edge Cases */
    RUN_TEST_CASE(DspQueuePeek, PeekWithZeroTimeoutReturnsImmediately);
    // RUN_TEST_CASE(DspQueuePeek, PeekAfterCloseQueueFails);
    RUN_TEST_CASE(DspQueuePeek, PeekWithNumBuffersPointer);
    RUN_TEST_CASE(DspQueuePeek, PeekWithNullNumBuffersPointer);
    RUN_TEST_CASE(DspQueuePeek, PeekLargeMessageSucceeds);
    RUN_TEST_CASE(DspQueuePeek, PeekThenReadReturnsSameMessage);
    RUN_TEST_CASE(DspQueuePeek, PeekWithVeryShortTimeoutExpires);
}
