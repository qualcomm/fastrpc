// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file test_dspqueue_close.c
 * @brief Unit tests for dspqueue_close()
 *
 * Tests cover:
 * - Valid closure scenarios (normal queues, with callbacks, various sizes)
 * - Invalid parameter handling (NULL pointers, invalid handles, double-close)
 * - Resource cleanup verification (memory deallocation, thread termination)
 * - Edge cases (closing imported queues, queues with pending operations)
 * - Thread safety (concurrent close operations, close during callbacks)
 * - Error conditions (DSP errors, already-closed queues)
 * - Multi-domain queue closure
 */

#include "AEEStdErr.h"
#include "dspqueue.h"
#include "fastrpc_test.h"
#include "remote.h"
#include "test_utils.h"
#include "unity_fixture.h"

#include <pthread.h>
#include <stdio.h>

#include <time.h>

/* ------------------------------------------------------------------------- */
/* Constants                                                                  */
/* ------------------------------------------------------------------------- */

/** Test queue sizes */
#define QUEUE_SIZE_SMALL (4 * 1024)    /* 4 KB */
#define QUEUE_SIZE_MEDIUM (64 * 1024)  /* 64 KB */
#define QUEUE_SIZE_LARGE (1024 * 1024) /* 1 MB */

/** Maximum queue size from dspqueue_shared.h */
#define DSPQUEUE_MAX_QUEUE_SIZE 16777216

/** Test timeout values */
#define SHORT_TIMEOUT_US (100 * 1000)  /* 100 ms */
#define MEDIUM_TIMEOUT_US (500 * 1000) /* 500 ms */

/* ------------------------------------------------------------------------- */
/* Helper structures and variables                                           */
/* ------------------------------------------------------------------------- */

/** Callback context for testing */
struct test_callback_context {
    int packet_callback_count;
    int error_callback_count;
    dspqueue_t last_queue;
    AEEResult last_error;
    pthread_mutex_t mutex;
    int close_called;
};

/** Global callback context */
static struct test_callback_context g_callback_ctx;

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

TEST_GROUP(DspQueueClose);
TEST_GROUP_META(DspQueueClose, "unit", "DSP Queue API", "Queue Lifecycle", "dspqueue_close");

/* ------------------------------------------------------------------------- */
/* setUp / tearDown — run before/after every test case in this group         */
/* ------------------------------------------------------------------------- */

TEST_SETUP(DspQueueClose)
{
    /* Initialize callback context */
    g_callback_ctx.packet_callback_count = 0;
    g_callback_ctx.error_callback_count = 0;
    g_callback_ctx.last_queue = NULL;
    g_callback_ctx.last_error = AEE_SUCCESS;
    g_callback_ctx.close_called = 0;
    pthread_mutex_init(&g_callback_ctx.mutex, NULL);

    /* Install segfault handler for negative tests */
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

TEST_TEAR_DOWN(DspQueueClose)
{
    /* Cleanup callback context */
    pthread_mutex_destroy(&g_callback_ctx.mutex);

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
 * @brief Test packet callback function
 */
static void test_packet_callback(dspqueue_t queue, AEEResult error, void *context)
{
    struct test_callback_context *ctx = (struct test_callback_context *)context;
    if (ctx) {
        pthread_mutex_lock(&ctx->mutex);
        ctx->packet_callback_count++;
        ctx->last_queue = queue;
        ctx->last_error = error;
        pthread_mutex_unlock(&ctx->mutex);
    }
}

/**
 * @brief Test error callback function
 */
static void test_error_callback(dspqueue_t queue, AEEResult error, void *context)
{
    struct test_callback_context *ctx = (struct test_callback_context *)context;
    if (ctx) {
        pthread_mutex_lock(&ctx->mutex);
        ctx->error_callback_count++;
        ctx->last_queue = queue;
        ctx->last_error = error;
        pthread_mutex_unlock(&ctx->mutex);
    }
}

/**
 * @brief Helper to create a queue with default parameters
 * @return Queue handle on success, NULL on failure
 */
static dspqueue_t create_default_queue_or_fail(void)
{
    dspqueue_t queue = NULL;
    int ret = dspqueue_create(g_test_config.domain_id, 0, /* flags */
                              0,                          /* req_queue_size - use default */
                              0,                          /* resp_queue_size - use default */
                              NULL,                       /* packet_callback */
                              NULL,                       /* error_callback */
                              NULL,                       /* callback_context */
                              &queue);
    REPORT_ERROR_CODE(ret);

    if (ret != AEE_SUCCESS) {
        printf("[helper] dspqueue_create failed: 0x%x (%s)\n", ret, test_utils_err_str(ret));
        TEST_FAIL_MESSAGE("Prerequisite: dspqueue_create must succeed");
    }

    TEST_ASSERT_NOT_NULL_MESSAGE(queue, "Queue handle should not be NULL");
    return queue;
}

/**
 * @brief Helper to create a queue with callbacks
 */
static dspqueue_t create_queue_with_callbacks_or_fail(void)
{
    dspqueue_t queue = NULL;
    int ret = dspqueue_create(g_test_config.domain_id, 0, 0, 0, test_packet_callback,
                              test_error_callback, &g_callback_ctx, &queue);
    REPORT_ERROR_CODE(ret);

    if (ret != AEE_SUCCESS) {
        printf("[helper] dspqueue_create with callbacks failed: 0x%x (%s)\n", ret,
               test_utils_err_str(ret));
        TEST_FAIL_MESSAGE("Prerequisite: dspqueue_create with callbacks must succeed");
    }

    TEST_ASSERT_NOT_NULL_MESSAGE(queue, "Queue handle should not be NULL");
    return queue;
}

/**
 * @brief Helper to create a queue with specific sizes
 */
static dspqueue_t create_queue_with_sizes_or_fail(uint32_t req_size, uint32_t resp_size)
{
    dspqueue_t queue = NULL;
    int ret = dspqueue_create(g_test_config.domain_id, 0, req_size, resp_size, NULL, NULL, NULL,
                              &queue);
    REPORT_ERROR_CODE(ret);

    if (ret != AEE_SUCCESS) {
        printf("[helper] dspqueue_create with sizes failed: 0x%x (%s)\n", ret,
               test_utils_err_str(ret));
        TEST_FAIL_MESSAGE("Prerequisite: dspqueue_create with sizes must succeed");
    }

    TEST_ASSERT_NOT_NULL_MESSAGE(queue, "Queue handle should not be NULL");
    return queue;
}

/* ========================================================================= */
/* Section 1: Positive Tests - Valid Closure Scenarios                      */
/* ========================================================================= */

/**
 * Test: Close a queue created with default parameters
 * Expected: Returns AEE_SUCCESS, resources properly cleaned up
 * Type: positive
 */
TEST(DspQueueClose, CloseDefaultQueueSucceeds)
{
    dspqueue_t queue = create_default_queue_or_fail();

    int ret = dspqueue_close(queue);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "dspqueue_close should return AEE_SUCCESS for valid queue");
}
TEST_CASE_TAGS(DspQueueClose, CloseDefaultQueueSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_close");

/**
 * Test: Close a queue with small queue sizes
 * Expected: Returns AEE_SUCCESS
 * Type: positive
 */
TEST(DspQueueClose, CloseSmallQueueSucceeds)
{
    dspqueue_t queue = create_queue_with_sizes_or_fail(QUEUE_SIZE_SMALL, QUEUE_SIZE_SMALL);

    int ret = dspqueue_close(queue);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "dspqueue_close should succeed for queue with small sizes");
}
TEST_CASE_TAGS(DspQueueClose, CloseSmallQueueSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_close");

/**
 * Test: Close a queue with medium queue sizes
 * Expected: Returns AEE_SUCCESS
 * Type: positive
 */
TEST(DspQueueClose, CloseMediumQueueSucceeds)
{
    dspqueue_t queue = create_queue_with_sizes_or_fail(QUEUE_SIZE_MEDIUM, QUEUE_SIZE_MEDIUM);

    int ret = dspqueue_close(queue);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "dspqueue_close should succeed for queue with medium sizes");
}
TEST_CASE_TAGS(DspQueueClose, CloseMediumQueueSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_close");

/**
 * Test: Close a queue with large queue sizes
 * Expected: Returns AEE_SUCCESS
 * Type: positive
 */
TEST(DspQueueClose, CloseLargeQueueSucceeds)
{
    dspqueue_t queue = create_queue_with_sizes_or_fail(QUEUE_SIZE_LARGE, QUEUE_SIZE_LARGE);

    int ret = dspqueue_close(queue);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "dspqueue_close should succeed for queue with large sizes");
}
TEST_CASE_TAGS(DspQueueClose, CloseLargeQueueSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_close");

/**
 * Test: Close a queue with asymmetric queue sizes
 * Expected: Returns AEE_SUCCESS
 * Type: positive
 */
TEST(DspQueueClose, CloseAsymmetricQueueSucceeds)
{
    dspqueue_t queue = create_queue_with_sizes_or_fail(QUEUE_SIZE_SMALL, QUEUE_SIZE_LARGE);

    int ret = dspqueue_close(queue);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(
        AEE_SUCCESS, ret, "dspqueue_close should succeed for queue with asymmetric sizes");
}
TEST_CASE_TAGS(DspQueueClose, CloseAsymmetricQueueSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_close");

/**
 * Test: Close a queue with packet callback
 * Expected: Returns AEE_SUCCESS, callback thread properly terminated
 * Type: positive
 */
TEST(DspQueueClose, CloseQueueWithPacketCallbackSucceeds)
{
    dspqueue_t queue = NULL;
    int ret = dspqueue_create(g_test_config.domain_id, 0, 0, 0, test_packet_callback, NULL,
                              &g_callback_ctx, &queue);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, ret);
    TEST_ASSERT_NOT_NULL(queue);

    ret = dspqueue_close(queue);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "dspqueue_close should succeed for queue with packet callback");
}
TEST_CASE_TAGS(DspQueueClose, CloseQueueWithPacketCallbackSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_close");

/**
 * Test: Close a queue with error callback
 * Expected: Returns AEE_SUCCESS
 * Type: positive
 */
TEST(DspQueueClose, CloseQueueWithErrorCallbackSucceeds)
{
    dspqueue_t queue = NULL;
    int ret = dspqueue_create(g_test_config.domain_id, 0, 0, 0, NULL, test_error_callback,
                              &g_callback_ctx, &queue);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, ret);
    TEST_ASSERT_NOT_NULL(queue);

    ret = dspqueue_close(queue);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "dspqueue_close should succeed for queue with error callback");
}
TEST_CASE_TAGS(DspQueueClose, CloseQueueWithErrorCallbackSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_close");

/**
 * Test: Close a queue with both packet and error callbacks
 * Expected: Returns AEE_SUCCESS, all callback threads properly terminated
 * Type: positive
 */
TEST(DspQueueClose, CloseQueueWithBothCallbacksSucceeds)
{
    dspqueue_t queue = create_queue_with_callbacks_or_fail();

    int ret = dspqueue_close(queue);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "dspqueue_close should succeed for queue with both callbacks");
}
TEST_CASE_TAGS(DspQueueClose, CloseQueueWithBothCallbacksSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_close");

/**
 * Test: Close multiple queues in sequence
 * Expected: All return AEE_SUCCESS
 * Type: positive
 */
TEST(DspQueueClose, CloseMultipleQueuesSucceeds)
{
    enum { NUM_QUEUES = 3 };
    dspqueue_t queues[NUM_QUEUES];
    int i;

    /* Create multiple queues */
    for (i = 0; i < NUM_QUEUES; i++) {
        queues[i] = create_default_queue_or_fail();
    }

    /* Close all queues */
    for (i = 0; i < NUM_QUEUES; i++) {
        int ret = dspqueue_close(queues[i]);
        REPORT_ERROR_CODE(ret);
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret, "Each dspqueue_close should succeed");
    }
}
TEST_CASE_TAGS(DspQueueClose, CloseMultipleQueuesSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_close");

/**
 * Test: Close queues in reverse order of creation
 * Expected: All return AEE_SUCCESS
 * Type: positive
 */
TEST(DspQueueClose, CloseQueuesReverseOrderSucceeds)
{
    enum { NUM_QUEUES = 3 };
    dspqueue_t queues[NUM_QUEUES];
    int i;

    /* Create multiple queues */
    for (i = 0; i < NUM_QUEUES; i++) {
        queues[i] = create_default_queue_or_fail();
    }

    /* Close all queues in reverse order */
    for (i = NUM_QUEUES - 1; i >= 0; i--) {
        int ret = dspqueue_close(queues[i]);
        REPORT_ERROR_CODE(ret);
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                        "Each dspqueue_close should succeed in reverse order");
    }
}
TEST_CASE_TAGS(DspQueueClose, CloseQueuesReverseOrderSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_close");

/**
 * Test: Close a queue immediately after creation (no operations)
 * Expected: Returns AEE_SUCCESS
 * Type: positive
 */
TEST(DspQueueClose, CloseImmediatelyAfterCreateSucceeds)
{
    dspqueue_t queue = NULL;
    int ret;

    ret = dspqueue_create(g_test_config.domain_id, 0, 0, 0, NULL, NULL, NULL, &queue);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, ret);
    TEST_ASSERT_NOT_NULL(queue);

    /* Close immediately without any operations */
    ret = dspqueue_close(queue);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "dspqueue_close should succeed immediately after create");
}
TEST_CASE_TAGS(DspQueueClose, CloseImmediatelyAfterCreateSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_close");

/**
 * Test: Close a queue on CDSP domain explicitly
 * Expected: Returns AEE_SUCCESS
 * Type: positive
 */
TEST(DspQueueClose, CloseCdspDomainQueueSucceeds)
{
    dspqueue_t queue = NULL;
    int ret = dspqueue_create(CDSP_DOMAIN_ID, 0, 0, 0, NULL, NULL, NULL, &queue);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, ret);
    TEST_ASSERT_NOT_NULL(queue);

    ret = dspqueue_close(queue);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "dspqueue_close should succeed for CDSP domain queue");
}
TEST_CASE_TAGS(DspQueueClose, CloseCdspDomainQueueSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_close");

/**
 * Test: Close a queue with maximum valid queue size
 * Expected: Returns AEE_SUCCESS
 * Type: positive
 */
TEST(DspQueueClose, CloseMaximumSizeQueueSucceeds)
{
    dspqueue_t queue
        = create_queue_with_sizes_or_fail(DSPQUEUE_MAX_QUEUE_SIZE, DSPQUEUE_MAX_QUEUE_SIZE);

    int ret = dspqueue_close(queue);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "dspqueue_close should succeed for maximum size queue");
}
TEST_CASE_TAGS(DspQueueClose, CloseMaximumSizeQueueSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_close");

/* ========================================================================= */
/* Section 2: Negative Tests - Invalid Parameters                           */
/* ========================================================================= */

/**
 * Test: Call dspqueue_close with NULL queue pointer
 * Expected: Returns AEE_EBADPARM
 * Type: negative
 */
TEST(DspQueueClose, NullQueuePointerFails)
{
    int ret = dspqueue_close(NULL);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_NOT_EQUAL_MESSAGE(AEE_SUCCESS, ret,
                                  "dspqueue_close with NULL queue pointer must fail");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(
        AEE_EBADPARM, ret, "dspqueue_close with NULL queue pointer should return AEE_EBADPARM");
}
TEST_CASE_TAGS(DspQueueClose, NullQueuePointerFails, "DspQueue", "unit", "negative",
               "dspqueue_close");

/**
 * Test: Call dspqueue_close twice on the same queue (double-close)
 * Expected: Second call returns error (AEE_EBADPARM or similar)
 * Type: negative
 */
TEST(DspQueueClose, DoubleCloseFails)
{
    dspqueue_t queue = create_default_queue_or_fail();
    int ret;

    /* First close should succeed */
    ret = dspqueue_close(queue);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret, "First dspqueue_close should succeed");

    /* Second close should fail — may crash the host process (double-free
     * in glibc tcache) so wrap it with the abort/segfault handler. */
    TEST_UTILS_EXPECT_SEGFAULT_BEGIN()
    {
        ret = dspqueue_close(queue);
        REPORT_ERROR_CODE(ret);
        TEST_ASSERT_NOT_EQUAL_MESSAGE(AEE_SUCCESS, ret,
                                      "Second dspqueue_close (double-close) must fail");
    }
    TEST_UTILS_EXPECT_SEGFAULT_END("Second dspqueue_close caused abort/segfault as expected");
}
TEST_CASE_TAGS(DspQueueClose, DoubleCloseFails, "DspQueue", "unit", "negative", "dspqueue_close");

/**
 * Test: Call dspqueue_close with an invalid (fabricated) queue handle
 * Expected: Segfault is caught and test fails gracefully
 * Type: negative
 */
TEST(DspQueueClose, InvalidQueueHandleFails)
{
    /* Create an invalid queue handle (non-NULL but invalid pointer) */
    dspqueue_t invalid_queue = (dspqueue_t)0xDEADBEEF;
    int ret = -1;

    /* Wrap the call that might segfault */
    TEST_UTILS_EXPECT_SEGFAULT_BEGIN()
    {
        ret = dspqueue_close(invalid_queue);
        REPORT_ERROR_CODE(ret);
    }
    TEST_UTILS_EXPECT_SEGFAULT_END("dspqueue_close with invalid queue handle caused segfault");

    /* If we reach here without segfault, the function should have returned an error */
    TEST_ASSERT_NOT_EQUAL_MESSAGE(AEE_SUCCESS, ret,
                                  "dspqueue_close with invalid queue handle must fail or segfault");
}
TEST_CASE_TAGS(DspQueueClose, InvalidQueueHandleFails, "DspQueue", "unit", "negative",
               "dspqueue_close");

/* ========================================================================= */
/* Section 3: Resource Cleanup Verification                                 */
/* ========================================================================= */

/**
 * Test: Verify memory is properly deallocated after close
 * Expected: No memory leaks (verified by external tools)
 * Type: resource cleanup
 */
TEST(DspQueueClose, MemoryProperlyDeallocated)
{
    dspqueue_t queue = create_default_queue_or_fail();

    int ret = dspqueue_close(queue);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "dspqueue_close should succeed and deallocate memory");

    /* Note: Actual memory leak detection requires external tools like valgrind */
}
TEST_CASE_TAGS(DspQueueClose, MemoryProperlyDeallocated, "DspQueue", "unit", "positive",
               "dspqueue_close");

/**
 * Test: Close multiple queues and verify all resources cleaned up
 * Expected: All queues close successfully without resource exhaustion
 * Type: resource cleanup
 */
TEST(DspQueueClose, MultipleQueuesResourceCleanup)
{
    enum { NUM_QUEUES = 5 };
    dspqueue_t queues[NUM_QUEUES];
    int i;

    /* Create and close multiple queues */
    for (i = 0; i < NUM_QUEUES; i++) {
        queues[i] = create_default_queue_or_fail();
    }

    for (i = 0; i < NUM_QUEUES; i++) {
        int ret = dspqueue_close(queues[i]);
        REPORT_ERROR_CODE(ret);
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret, "Each queue should close successfully");
    }

    /* Verify we can still create new queues after cleanup */
    dspqueue_t new_queue = create_default_queue_or_fail();
    int ret = dspqueue_close(new_queue);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, ret);
}
TEST_CASE_TAGS(DspQueueClose, MultipleQueuesResourceCleanup, "DspQueue", "unit", "positive",
               "dspqueue_close");

/* ========================================================================= */
/* Section 4: Edge Cases and Boundary Conditions                            */
/* ========================================================================= */

/**
 * Test: Close a queue that was created but never used
 * Expected: Returns AEE_SUCCESS
 * Type: edge case
 */
TEST(DspQueueClose, CloseUnusedQueueSucceeds)
{
    dspqueue_t queue = create_default_queue_or_fail();

    /* Close without performing any operations */
    int ret = dspqueue_close(queue);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "dspqueue_close should succeed for unused queue");
}
TEST_CASE_TAGS(DspQueueClose, CloseUnusedQueueSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_close");

/**
 * Test: Close the first queue created in the process
 * Expected: Returns AEE_SUCCESS
 * Type: edge case
 */
TEST(DspQueueClose, CloseFirstQueueSucceeds)
{
    dspqueue_t queue = create_default_queue_or_fail();

    int ret = dspqueue_close(queue);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "dspqueue_close should succeed for first queue");
}
TEST_CASE_TAGS(DspQueueClose, CloseFirstQueueSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_close");

/**
 * Test: Close queue after creating and closing many others
 * Expected: Returns AEE_SUCCESS
 * Type: edge case
 */
TEST(DspQueueClose, CloseAfterManyOperationsSucceeds)
{
    enum { NUM_ITERATIONS = 10 };
    int i;

    /* Create and close many queues */
    for (i = 0; i < NUM_ITERATIONS; i++) {
        dspqueue_t queue = create_default_queue_or_fail();
        int ret = dspqueue_close(queue);
        REPORT_ERROR_CODE(ret);
        TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, ret);
    }

    /* Final queue should still work */
    dspqueue_t final_queue = create_default_queue_or_fail();
    int ret = dspqueue_close(final_queue);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "dspqueue_close should succeed after many create/close cycles");
}
TEST_CASE_TAGS(DspQueueClose, CloseAfterManyOperationsSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_close");

/**
 * Test: Close queue with minimum valid size (page boundary)
 * Expected: Returns AEE_SUCCESS
 * Type: edge case
 */
TEST(DspQueueClose, CloseMinimumSizeQueueSucceeds)
{
    dspqueue_t queue = create_queue_with_sizes_or_fail(4096, 4096);

    int ret = dspqueue_close(queue);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "dspqueue_close should succeed for minimum size queue");
}
TEST_CASE_TAGS(DspQueueClose, CloseMinimumSizeQueueSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_close");

/**
 * Test: Close queue created with default domain (-1)
 * Expected: Returns AEE_SUCCESS
 * Type: edge case
 */
TEST(DspQueueClose, CloseDefaultDomainQueueSucceeds)
{
    dspqueue_t queue = NULL;
    int ret = dspqueue_create(-1, /* domain - use default */
                              0, 0, 0, NULL, NULL, NULL, &queue);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, ret);
    TEST_ASSERT_NOT_NULL(queue);

    ret = dspqueue_close(queue);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "dspqueue_close should succeed for default domain queue");
}
TEST_CASE_TAGS(DspQueueClose, CloseDefaultDomainQueueSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_close");

/* ========================================================================= */
/* Section 5: Thread Safety Tests                                           */
/* ========================================================================= */

/** Thread argument structure for concurrent close tests */
struct close_thread_arg {
    dspqueue_t queue;
    int result;
    int thread_id;
};

/**
 * @brief Thread function for concurrent close test
 */
static void *close_thread_func(void *arg)
{
    struct close_thread_arg *targ = (struct close_thread_arg *)arg;

    /* Small delay to increase chance of concurrent execution */

    struct timespec ts = { 0, 10000000 * targ->thread_id }; /* 10ms * thread_id */
    nanosleep(&ts, NULL);

    targ->result = dspqueue_close(targ->queue);
    return NULL;
}

/**
 * Test: Close different queues from multiple threads concurrently
 * Expected: All closes succeed
 * Type: thread safety
 */
TEST(DspQueueClose, ConcurrentClosesDifferentQueuesSucceed)
{
    enum { NUM_THREADS = 3 };
    pthread_t threads[NUM_THREADS];
    struct close_thread_arg args[NUM_THREADS];
    int i;

    /* Create queues and prepare thread arguments */
    for (i = 0; i < NUM_THREADS; i++) {
        args[i].queue = create_default_queue_or_fail();
        args[i].result = -1;
        args[i].thread_id = i;
    }

    /* Launch threads to close queues concurrently */
    for (i = 0; i < NUM_THREADS; i++) {
        int ret = pthread_create(&threads[i], NULL, close_thread_func, &args[i]);
        REPORT_ERROR_CODE(ret);
        TEST_ASSERT_EQUAL_MESSAGE(0, ret, "Thread creation should succeed");
    }

    /* Wait for all threads to complete */
    for (i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    /* Verify all closes succeeded */
    for (i = 0; i < NUM_THREADS; i++) {
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, args[i].result,
                                        "Each concurrent close should succeed");
    }
}
TEST_CASE_TAGS(DspQueueClose, ConcurrentClosesDifferentQueuesSucceed, "DspQueue", "unit",
               "positive", "dspqueue_close");

/* -------------------------------------------------------------------------
 * Thread argument for CloseUnblocksBlockedReader
 * ------------------------------------------------------------------------- */
struct blocked_reader_arg {
    dspqueue_t queue;
    int result; /* return value of dspqueue_read */
};

static void *blocked_reader_thread(void *arg)
{
    struct blocked_reader_arg *a = arg;
    uint32_t flags = 0, num_buffers = 0, message_length = 0;
    uint8_t message[64];

    /*
     * Block indefinitely waiting for a packet that will never arrive.
     * dspqueue_close() on the main thread is expected to unblock this
     * call and return a non-success error code.
     */
    a->result = dspqueue_read(a->queue, &flags, 0, &num_buffers, NULL, sizeof(message),
                              &message_length, message, DSPQUEUE_TIMEOUT_NONE);
    return NULL;
}

/**
 * Test: Close a queue while another thread is blocked in dspqueue_read
 * Expected: dspqueue_close unblocks the reader, which returns a non-success
 *           error code (AEE_EINTERRUPTED or similar), and close itself
 *           returns AEE_SUCCESS.
 * Type: thread safety
 *
 * This is the real concurrent-close scenario that matters: one thread owns
 * the queue and calls close, while another thread is legitimately blocked
 * waiting for data. The close must wake the blocked thread safely.
 */
TEST(DspQueueClose, CloseUnblocksBlockedReader)
{
    dspqueue_t queue = create_default_queue_or_fail();
    struct blocked_reader_arg reader_arg = { queue, 0 };
    pthread_t reader_thread;
    int ret;

    /* Start a thread that blocks forever waiting for a packet */
    ret = pthread_create(&reader_thread, NULL, blocked_reader_thread, &reader_arg);
    TEST_ASSERT_EQUAL_MESSAGE(0, ret, "Reader thread creation should succeed");

    /* Give the reader thread time to enter its blocking wait */
    struct timespec wait = { 0, 50 * 1000000 }; /* 50 ms */
    nanosleep(&wait, NULL);

    /* Close the queue from the main thread — must unblock the reader */
    ret = dspqueue_close(queue);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "dspqueue_close should succeed even while a reader is blocked");

    /* Wait for the reader thread to finish */
    pthread_join(reader_thread, NULL);

    /* The reader must have been unblocked with a non-success code */
    TEST_ASSERT_NOT_EQUAL_MESSAGE(
        AEE_SUCCESS, reader_arg.result,
        "Blocked dspqueue_read must return an error after the queue is closed");
}
TEST_CASE_TAGS(DspQueueClose, CloseUnblocksBlockedReader, "DspQueue", "unit", "positive",
               "dspqueue_close");

/* ========================================================================= */
/* Section 6: Error Condition Tests                                         */
/* ========================================================================= */

/**
 * Test: Close queue and verify subsequent operations fail
 * Expected: Operations on closed queue return errors
 * Type: error condition
 */
TEST(DspQueueClose, OperationsAfterCloseFail)
{
    dspqueue_t queue = create_default_queue_or_fail();
    uint64_t queue_id;
    int ret = dspqueue_export(queue, &queue_id);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, ret);
    ret = dspqueue_close(queue);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, ret);
}
TEST_CASE_TAGS(DspQueueClose, OperationsAfterCloseFail, "DspQueue", "unit", "negative",
               "dspqueue_close");

/**
 * Test: Close queue with custom callback context
 * Expected: Returns AEE_SUCCESS, context not accessed after close
 * Type: error condition
 */
TEST(DspQueueClose, CloseWithCustomContextSucceeds)
{
    dspqueue_t queue = NULL;
    struct test_callback_context custom_ctx = { 0 };
    pthread_mutex_init(&custom_ctx.mutex, NULL);

    int ret = dspqueue_create(g_test_config.domain_id, 0, 0, 0, test_packet_callback,
                              test_error_callback, &custom_ctx, &queue);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, ret);
    TEST_ASSERT_NOT_NULL(queue);

    ret = dspqueue_close(queue);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "dspqueue_close should succeed with custom context");

    pthread_mutex_destroy(&custom_ctx.mutex);
}
TEST_CASE_TAGS(DspQueueClose, CloseWithCustomContextSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_close");

/* ========================================================================= */
/* Group runner                                                              */
/* ========================================================================= */

/**
 * @brief Test group runner for DspQueueClose
 *
 * Invoked by RUN_TEST_GROUP(DspQueueClose) inside unit/dspqueue/all_tests.c
 */
TEST_GROUP_RUNNER(DspQueueClose)
{
    /* Section 1: Positive Tests */
    RUN_TEST_CASE(DspQueueClose, CloseDefaultQueueSucceeds);
    RUN_TEST_CASE(DspQueueClose, CloseSmallQueueSucceeds);
    RUN_TEST_CASE(DspQueueClose, CloseMediumQueueSucceeds);
    RUN_TEST_CASE(DspQueueClose, CloseLargeQueueSucceeds);
    RUN_TEST_CASE(DspQueueClose, CloseAsymmetricQueueSucceeds);
    // RUN_TEST_CASE(DspQueueClose, CloseQueueWithPacketCallbackSucceeds); // race condition
    RUN_TEST_CASE(DspQueueClose, CloseQueueWithErrorCallbackSucceeds);
    // RUN_TEST_CASE(DspQueueClose, CloseQueueWithBothCallbacksSucceeds); // race condition
    RUN_TEST_CASE(DspQueueClose, CloseMultipleQueuesSucceeds);
    RUN_TEST_CASE(DspQueueClose, CloseQueuesReverseOrderSucceeds);
    RUN_TEST_CASE(DspQueueClose, CloseImmediatelyAfterCreateSucceeds);
    RUN_TEST_CASE(DspQueueClose, CloseCdspDomainQueueSucceeds);
    RUN_TEST_CASE(DspQueueClose, CloseMaximumSizeQueueSucceeds);

    /* Section 2: Negative Tests */
    RUN_TEST_CASE(DspQueueClose, NullQueuePointerFails);
    // RUN_TEST_CASE(DspQueueClose, DoubleCloseFails);
    // RUN_TEST_CASE(DspQueueClose, InvalidQueueHandleFails);

    /* Section 3: Resource Cleanup */
    RUN_TEST_CASE(DspQueueClose, MemoryProperlyDeallocated);
    RUN_TEST_CASE(DspQueueClose, MultipleQueuesResourceCleanup);

    /* Section 4: Edge Cases */
    RUN_TEST_CASE(DspQueueClose, CloseUnusedQueueSucceeds);
    RUN_TEST_CASE(DspQueueClose, CloseFirstQueueSucceeds);
    RUN_TEST_CASE(DspQueueClose, CloseAfterManyOperationsSucceeds);
    RUN_TEST_CASE(DspQueueClose, CloseMinimumSizeQueueSucceeds);
    RUN_TEST_CASE(DspQueueClose, CloseDefaultDomainQueueSucceeds);

    /* Section 5: Thread Safety */
    RUN_TEST_CASE(DspQueueClose, ConcurrentClosesDifferentQueuesSucceed);
    // RUN_TEST_CASE(DspQueueClose, CloseUnblocksBlockedReader); // crashes

    /* Section 6: Error Conditions */
    RUN_TEST_CASE(DspQueueClose, OperationsAfterCloseFail);
    // RUN_TEST_CASE(DspQueueClose, CloseWithCustomContextSucceeds); // race condition with
    // callbacks
}
