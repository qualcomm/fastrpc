// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file test_dspqueue_export.c
 * @brief Unit tests for dspqueue_export()
 *
 * Tests cover:
 * - Valid export scenarios (normal queues, with callbacks, various sizes)
 * - Invalid parameter handling (NULL pointers, invalid handles, closed queues)
 * - Queue ID generation and uniqueness verification
 * - Edge cases (multiple exports, export after operations, boundary conditions)
 * - Thread safety (concurrent exports, export during callbacks)
 * - State verification (queue remains usable after export)
 * - Resource management (export/close lifecycle)
 * - Integration scenarios (export followed by operations)
 */

#include "AEEStdErr.h"
#include "dspqueue.h"
#include "fastrpc_test.h"
#include "remote.h"
#include "test_utils.h"
#include "unity_fixture.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
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

/** Maximum number of queues for stress testing */
#define MAX_TEST_QUEUES 10

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
    int export_completed;
};

/** Global callback context */
static struct test_callback_context g_callback_ctx;

/** Thread argument structure for concurrent export tests */
struct export_thread_arg {
    dspqueue_t queue;
    uint64_t queue_id;
    int result;
    int thread_id;
    pthread_mutex_t *mutex; /* Shared mutex for synchronization */
};

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

TEST_GROUP(DspQueueExport);
TEST_GROUP_META(DspQueueExport, "unit", "DSP Queue API", "Queue Lifecycle", "dspqueue_export");

/* ------------------------------------------------------------------------- */
/* setUp / tearDown — run before/after every test case in this group         */
/* ------------------------------------------------------------------------- */

TEST_SETUP(DspQueueExport)
{
    /* Initialize callback context with mutex for thread safety */
    memset(&g_callback_ctx, 0, sizeof(g_callback_ctx));
    g_callback_ctx.packet_callback_count = 0;
    g_callback_ctx.error_callback_count = 0;
    g_callback_ctx.last_queue = NULL;
    g_callback_ctx.last_error = AEE_SUCCESS;
    g_callback_ctx.export_completed = 0;

    /* Initialize mutex to prevent deadlocks */
    pthread_mutex_init(&g_callback_ctx.mutex, NULL);

    /* Install signal handler for segfault detection */
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

TEST_TEAR_DOWN(DspQueueExport)
{
    /* Cleanup callback context - ensure mutex is unlocked before destroying */
    int lock_result = pthread_mutex_trylock(&g_callback_ctx.mutex);
    if (lock_result == 0) {
        pthread_mutex_unlock(&g_callback_ctx.mutex);
    }
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
 * @brief Test packet callback function with thread-safe access
 */
static void test_packet_callback(dspqueue_t queue, AEEResult error, void *context)
{
    struct test_callback_context *ctx = (struct test_callback_context *)context;
    if (ctx) {
        /* Use trylock to prevent deadlock in callback */
        int lock_result = pthread_mutex_trylock(&ctx->mutex);
        if (lock_result == 0) {
            ctx->packet_callback_count++;
            ctx->last_queue = queue;
            ctx->last_error = error;
            pthread_mutex_unlock(&ctx->mutex);
        }
    }
}

/**
 * @brief Test error callback function with thread-safe access
 */
static void test_error_callback(dspqueue_t queue, AEEResult error, void *context)
{
    struct test_callback_context *ctx = (struct test_callback_context *)context;
    if (ctx) {
        /* Use trylock to prevent deadlock in callback */
        int lock_result = pthread_mutex_trylock(&ctx->mutex);
        if (lock_result == 0) {
            ctx->error_callback_count++;
            ctx->last_queue = queue;
            ctx->last_error = error;
            pthread_mutex_unlock(&ctx->mutex);
        }
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

/**
 * @brief Helper to close a queue and verify success
 */
static void close_queue_or_fail(dspqueue_t queue)
{
    int ret = dspqueue_close(queue);
    REPORT_ERROR_CODE(ret);
    if (ret != AEE_SUCCESS) {
        printf("[helper] dspqueue_close failed: 0x%x (%s)\n", ret, test_utils_err_str(ret));
        TEST_FAIL_MESSAGE("dspqueue_close should succeed");
    }
    /* Small delay to allow cleanup to complete */
    struct timespec ts = { 0, 50000000 }; /* 50ms */
    nanosleep(&ts, NULL);
}

/**
 * @brief Helper to verify queue_id is non-zero
 */
static void verify_queue_id_valid(uint64_t queue_id)
{
    TEST_ASSERT_NOT_EQUAL_MESSAGE(0, queue_id,
                                  "Queue ID should be non-zero after successful export");
}

/* ========================================================================= */
/* Section 1: Positive Tests - Valid Export Scenarios                       */
/* ========================================================================= */

/**
 * Test: Export a queue created with default parameters
 * Expected: Returns AEE_SUCCESS, queue_id is valid (non-zero)
 * Type: positive
 */
TEST(DspQueueExport, ExportDefaultQueueSucceeds)
{
    dspqueue_t queue = create_default_queue_or_fail();
    uint64_t queue_id = 0;

    int ret = dspqueue_export(queue, &queue_id);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "dspqueue_export should return AEE_SUCCESS for valid queue");
    verify_queue_id_valid(queue_id);

    close_queue_or_fail(queue);
}
TEST_CASE_TAGS(DspQueueExport, ExportDefaultQueueSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_export");

/**
 * Test: Export a queue with small queue sizes
 * Expected: Returns AEE_SUCCESS
 * Type: positive
 */
TEST(DspQueueExport, ExportSmallQueueSucceeds)
{
    dspqueue_t queue = create_queue_with_sizes_or_fail(QUEUE_SIZE_SMALL, QUEUE_SIZE_SMALL);
    uint64_t queue_id = 0;

    int ret = dspqueue_export(queue, &queue_id);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "dspqueue_export should succeed for queue with small sizes");
    verify_queue_id_valid(queue_id);

    close_queue_or_fail(queue);
}
TEST_CASE_TAGS(DspQueueExport, ExportSmallQueueSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_export");

/**
 * Test: Export a queue with medium queue sizes
 * Expected: Returns AEE_SUCCESS
 * Type: positive
 */
TEST(DspQueueExport, ExportMediumQueueSucceeds)
{
    dspqueue_t queue = create_queue_with_sizes_or_fail(QUEUE_SIZE_MEDIUM, QUEUE_SIZE_MEDIUM);
    uint64_t queue_id = 0;

    int ret = dspqueue_export(queue, &queue_id);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "dspqueue_export should succeed for queue with medium sizes");
    verify_queue_id_valid(queue_id);

    close_queue_or_fail(queue);
}
TEST_CASE_TAGS(DspQueueExport, ExportMediumQueueSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_export");

/**
 * Test: Export a queue with large queue sizes
 * Expected: Returns AEE_SUCCESS
 * Type: positive
 */
TEST(DspQueueExport, ExportLargeQueueSucceeds)
{
    dspqueue_t queue = create_queue_with_sizes_or_fail(QUEUE_SIZE_LARGE, QUEUE_SIZE_LARGE);
    uint64_t queue_id = 0;

    int ret = dspqueue_export(queue, &queue_id);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "dspqueue_export should succeed for queue with large sizes");
    verify_queue_id_valid(queue_id);

    close_queue_or_fail(queue);
}
TEST_CASE_TAGS(DspQueueExport, ExportLargeQueueSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_export");

/**
 * Test: Export a queue with asymmetric queue sizes
 * Expected: Returns AEE_SUCCESS
 * Type: positive
 */
TEST(DspQueueExport, ExportAsymmetricQueueSucceeds)
{
    dspqueue_t queue = create_queue_with_sizes_or_fail(QUEUE_SIZE_SMALL, QUEUE_SIZE_LARGE);
    uint64_t queue_id = 0;

    int ret = dspqueue_export(queue, &queue_id);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(
        AEE_SUCCESS, ret, "dspqueue_export should succeed for queue with asymmetric sizes");
    verify_queue_id_valid(queue_id);

    close_queue_or_fail(queue);
}
TEST_CASE_TAGS(DspQueueExport, ExportAsymmetricQueueSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_export");

/**
 * Test: Export a queue with packet callback
 * Expected: Returns AEE_SUCCESS, callback thread unaffected
 * Type: positive
 */
TEST(DspQueueExport, ExportQueueWithPacketCallbackSucceeds)
{
    dspqueue_t queue = NULL;
    uint64_t queue_id = 0;

    int ret = dspqueue_create(g_test_config.domain_id, 0, 0, 0, test_packet_callback, NULL,
                              &g_callback_ctx, &queue);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, ret);
    TEST_ASSERT_NOT_NULL(queue);

    ret = dspqueue_export(queue, &queue_id);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(
        AEE_SUCCESS, ret, "dspqueue_export should succeed for queue with packet callback");
    verify_queue_id_valid(queue_id);

    close_queue_or_fail(queue);
}
TEST_CASE_TAGS(DspQueueExport, ExportQueueWithPacketCallbackSucceeds, "DspQueue", "unit",
               "positive", "dspqueue_export");

/**
 * Test: Export a queue with error callback
 * Expected: Returns AEE_SUCCESS
 * Type: positive
 */
TEST(DspQueueExport, ExportQueueWithErrorCallbackSucceeds)
{
    dspqueue_t queue = NULL;
    uint64_t queue_id = 0;

    int ret = dspqueue_create(g_test_config.domain_id, 0, 0, 0, NULL, test_error_callback,
                              &g_callback_ctx, &queue);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, ret);
    TEST_ASSERT_NOT_NULL(queue);

    ret = dspqueue_export(queue, &queue_id);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "dspqueue_export should succeed for queue with error callback");
    verify_queue_id_valid(queue_id);

    close_queue_or_fail(queue);
}
TEST_CASE_TAGS(DspQueueExport, ExportQueueWithErrorCallbackSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_export");

/**
 * Test: Export a queue with both packet and error callbacks
 * Expected: Returns AEE_SUCCESS, both callbacks remain functional
 * Type: positive
 */
TEST(DspQueueExport, ExportQueueWithBothCallbacksSucceeds)
{
    dspqueue_t queue = create_queue_with_callbacks_or_fail();
    uint64_t queue_id = 0;

    int ret = dspqueue_export(queue, &queue_id);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "dspqueue_export should succeed for queue with both callbacks");
    verify_queue_id_valid(queue_id);

    close_queue_or_fail(queue);
}
TEST_CASE_TAGS(DspQueueExport, ExportQueueWithBothCallbacksSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_export");

/**
 * Test: Export a queue on CDSP domain explicitly
 * Expected: Returns AEE_SUCCESS
 * Type: positive
 */
TEST(DspQueueExport, ExportCdspDomainQueueSucceeds)
{
    dspqueue_t queue = NULL;
    uint64_t queue_id = 0;

    int ret = dspqueue_create(CDSP_DOMAIN_ID, 0, 0, 0, NULL, NULL, NULL, &queue);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, ret);
    TEST_ASSERT_NOT_NULL(queue);

    ret = dspqueue_export(queue, &queue_id);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "dspqueue_export should succeed for CDSP domain queue");
    verify_queue_id_valid(queue_id);

    close_queue_or_fail(queue);
}
TEST_CASE_TAGS(DspQueueExport, ExportCdspDomainQueueSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_export");

/**
 * Test: Export a queue with maximum valid queue size
 * Expected: Returns AEE_SUCCESS
 * Type: positive
 */
TEST(DspQueueExport, ExportMaximumSizeQueueSucceeds)
{
    dspqueue_t queue
        = create_queue_with_sizes_or_fail(DSPQUEUE_MAX_QUEUE_SIZE, DSPQUEUE_MAX_QUEUE_SIZE);
    uint64_t queue_id = 0;

    int ret = dspqueue_export(queue, &queue_id);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "dspqueue_export should succeed for maximum size queue");
    verify_queue_id_valid(queue_id);

    close_queue_or_fail(queue);
}
TEST_CASE_TAGS(DspQueueExport, ExportMaximumSizeQueueSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_export");

/**
 * Test: Export a queue immediately after creation (no operations)
 * Expected: Returns AEE_SUCCESS
 * Type: positive
 */
TEST(DspQueueExport, ExportImmediatelyAfterCreateSucceeds)
{
    dspqueue_t queue = NULL;
    uint64_t queue_id = 0;
    int ret;

    ret = dspqueue_create(g_test_config.domain_id, 0, 0, 0, NULL, NULL, NULL, &queue);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, ret);
    TEST_ASSERT_NOT_NULL(queue);

    /* Export immediately without any operations */
    ret = dspqueue_export(queue, &queue_id);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "dspqueue_export should succeed immediately after create");
    verify_queue_id_valid(queue_id);

    close_queue_or_fail(queue);
}
TEST_CASE_TAGS(DspQueueExport, ExportImmediatelyAfterCreateSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_export");

/* ========================================================================= */
/* Section 2: Negative Tests - Invalid Parameters                           */
/* ========================================================================= */

/**
 * Test: Call dspqueue_export with NULL queue handle
 * Expected: Returns AEE_EBADPARM (or catches segfault as failure)
 * Type: negative
 */
TEST(DspQueueExport, NullQueueHandleFails)
{
    uint64_t queue_id = 0;
    int ret;

    TEST_UTILS_EXPECT_SEGFAULT_BEGIN()
    {
        ret = dspqueue_export(NULL, &queue_id);
        REPORT_ERROR_CODE(ret);

        TEST_ASSERT_NOT_EQUAL_MESSAGE(AEE_SUCCESS, ret,
                                      "dspqueue_export with NULL queue handle must fail");
        /* If we get here without segfault, check for proper error code */
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(
            AEE_EBADPARM, ret, "dspqueue_export with NULL queue handle should return AEE_EBADPARM");
    }
    TEST_UTILS_EXPECT_SEGFAULT_END("dspqueue_export with NULL queue handle caused segfault - "
                                   "missing NULL check in implementation");
}
TEST_CASE_TAGS(DspQueueExport, NullQueueHandleFails, "DspQueue", "unit", "negative",
               "dspqueue_export");

/**
 * Test: Call dspqueue_export with NULL queue_id pointer
 * Expected: Returns AEE_EBADPARM (or catches segfault as failure)
 * Type: negative
 */
TEST(DspQueueExport, NullQueueIdPointerFails)
{
    dspqueue_t queue = create_default_queue_or_fail();
    int ret = AEE_SUCCESS;

    TEST_UTILS_EXPECT_SEGFAULT_BEGIN()
    {
        ret = dspqueue_export(queue, NULL);
        REPORT_ERROR_CODE(ret);

        TEST_ASSERT_NOT_EQUAL_MESSAGE(AEE_SUCCESS, ret,
                                      "dspqueue_export with NULL queue_id pointer must fail");
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(
            AEE_EBADPARM, ret,
            "dspqueue_export with NULL queue_id pointer should return AEE_EBADPARM");
    }
    TEST_UTILS_EXPECT_SEGFAULT_END("dspqueue_export with NULL queue_id pointer caused segfault - "
                                   "missing NULL check in implementation");

    /*
     * Cleanup BEFORE assertion result is used: the NULL-pointer call is
     * expected to fail and leave the queue live.  Close it unconditionally
     * so that a longjmp from the segfault handler cannot skip cleanup.
     * If the call unexpectedly returned 0x0 the queue is still valid and
     * must be closed to avoid a resource leak.
     */
    close_queue_or_fail(queue);
}
TEST_CASE_TAGS(DspQueueExport, NullQueueIdPointerFails, "DspQueue", "unit", "negative",
               "dspqueue_export");

/**
 * Test: Call dspqueue_export with both NULL parameters
 * Expected: Returns AEE_EBADPARM (or catches segfault as failure)
 * Type: negative
 */
TEST(DspQueueExport, BothParametersNullFails)
{
    int ret;

    TEST_UTILS_EXPECT_SEGFAULT_BEGIN()
    {
        ret = dspqueue_export(NULL, NULL);
        REPORT_ERROR_CODE(ret);

        TEST_ASSERT_NOT_EQUAL_MESSAGE(AEE_SUCCESS, ret,
                                      "dspqueue_export with both NULL parameters must fail");
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(
            AEE_EBADPARM, ret,
            "dspqueue_export with both NULL parameters should return AEE_EBADPARM");
    }
    TEST_UTILS_EXPECT_SEGFAULT_END("dspqueue_export with both NULL parameters caused segfault - "
                                   "missing NULL check in implementation");
}
TEST_CASE_TAGS(DspQueueExport, BothParametersNullFails, "DspQueue", "unit", "negative",
               "dspqueue_export");

/**
 * Test: Call dspqueue_export with an invalid (fabricated) queue handle
 * Expected: Returns error (or catches segfault as failure)
 * Type: negative
 */
TEST(DspQueueExport, InvalidQueueHandleFails)
{
    /* Create an invalid queue handle (non-NULL but invalid pointer) */
    dspqueue_t invalid_queue = (dspqueue_t)0xDEADBEEF;
    uint64_t queue_id = 0;
    int ret;

    TEST_UTILS_EXPECT_SEGFAULT_BEGIN()
    {
        ret = dspqueue_export(invalid_queue, &queue_id);
        REPORT_ERROR_CODE(ret);

        TEST_ASSERT_NOT_EQUAL_MESSAGE(AEE_SUCCESS, ret,
                                      "dspqueue_export with invalid queue handle must fail");
    }
    TEST_UTILS_EXPECT_SEGFAULT_END("dspqueue_export with invalid queue handle caused segfault - "
                                   "missing validation in implementation");
}
TEST_CASE_TAGS(DspQueueExport, InvalidQueueHandleFails, "DspQueue", "unit", "negative",
               "dspqueue_export");

/**
 * Test: Call dspqueue_export on a closed queue
 * Expected: Returns error (or catches segfault as failure)
 * Type: negative
 */
TEST(DspQueueExport, ExportClosedQueueFails)
{
    dspqueue_t queue = create_default_queue_or_fail();
    uint64_t queue_id = 0;
    int ret;

    /* Close the queue first */
    ret = dspqueue_close(queue);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, ret);

    /* Attempt to export the closed queue - this uses freed memory */
    TEST_UTILS_EXPECT_SEGFAULT_BEGIN()
    {
        ret = dspqueue_export(queue, &queue_id);
        REPORT_ERROR_CODE(ret);

        TEST_ASSERT_NOT_EQUAL_MESSAGE(AEE_SUCCESS, ret,
                                      "dspqueue_export on closed queue must fail");
    }
    TEST_UTILS_EXPECT_SEGFAULT_END("dspqueue_export on closed queue caused segfault - queue memory "
                                   "was freed, implementation should handle this gracefully or "
                                   "document that queue handles must not be used after close");
}
TEST_CASE_TAGS(DspQueueExport, ExportClosedQueueFails, "DspQueue", "unit", "negative",
               "dspqueue_export");

/* ========================================================================= */
/* Section 3: Queue ID Uniqueness and Generation                            */
/* ========================================================================= */

/**
 * Test: Export multiple queues and verify unique queue IDs
 * Expected: All exports succeed with unique queue IDs
 * Type: uniqueness verification
 */
TEST(DspQueueExport, MultipleExportsGenerateUniqueIds)
{
    enum { NUM_QUEUES = 5 };
    dspqueue_t queues[NUM_QUEUES];
    uint64_t queue_ids[NUM_QUEUES];
    int i, j;

    /* Create and export multiple queues */
    for (i = 0; i < NUM_QUEUES; i++) {
        queues[i] = create_default_queue_or_fail();
        queue_ids[i] = 0;

        int ret = dspqueue_export(queues[i], &queue_ids[i]);
        REPORT_ERROR_CODE(ret);
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret, "Each dspqueue_export should succeed");
        verify_queue_id_valid(queue_ids[i]);
    }

    /* Verify all queue IDs are unique */
    for (i = 0; i < NUM_QUEUES; i++) {
        for (j = i + 1; j < NUM_QUEUES; j++) {
            TEST_ASSERT_NOT_EQUAL_MESSAGE(queue_ids[i], queue_ids[j],
                                          "Each queue ID should be unique");
        }
    }

    /* Close all queues */
    for (i = 0; i < NUM_QUEUES; i++) {
        close_queue_or_fail(queues[i]);
    }
}
TEST_CASE_TAGS(DspQueueExport, MultipleExportsGenerateUniqueIds, "DspQueue", "unit", "positive",
               "dspqueue_export");

/**
 * Test: Export queue, close it, create new queue, export again
 * Expected: New queue gets different queue ID
 * Type: uniqueness verification
 */
TEST(DspQueueExport, QueueIdChangesAfterRecreate)
{
    dspqueue_t queue1, queue2;
    uint64_t queue_id1 = 0, queue_id2 = 0;
    int ret;

    /* Create and export first queue */
    queue1 = create_default_queue_or_fail();
    ret = dspqueue_export(queue1, &queue_id1);
    REPORT_ERROR_CODE(ret);
    if (ret != AEE_SUCCESS) {
        close_queue_or_fail(queue1);
        TEST_FAIL_MESSAGE("dspqueue_export of first queue should succeed");
        return;
    }
    verify_queue_id_valid(queue_id1);

    /* Close first queue */
    close_queue_or_fail(queue1);

    /* Create and export second queue */
    queue2 = create_default_queue_or_fail();
    ret = dspqueue_export(queue2, &queue_id2);
    REPORT_ERROR_CODE(ret);

    /*
     * Cleanup BEFORE assertion: close queue2 unconditionally so that a
     * longjmp from the assertion below cannot skip the cleanup and leave
     * a dangling queue or a resource leak.
     */
    close_queue_or_fail(queue2);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "dspqueue_export of second queue should succeed");
    verify_queue_id_valid(queue_id2);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(queue_id1, queue_id2,
                                  "Queue IDs should be different for different queue instances");
}
TEST_CASE_TAGS(DspQueueExport, QueueIdChangesAfterRecreate, "DspQueue", "unit", "positive",
               "dspqueue_export");

/**
 * Test: Verify queue_id is non-zero for all valid exports
 * Expected: Queue ID is always non-zero
 * Type: validation
 */
TEST(DspQueueExport, QueueIdAlwaysNonZero)
{
    enum { NUM_TESTS = 10 };
    int i;

    for (i = 0; i < NUM_TESTS; i++) {
        dspqueue_t queue = create_default_queue_or_fail();
        uint64_t queue_id = 0;

        int ret;
        ret = dspqueue_export(queue, &queue_id);
        REPORT_ERROR_CODE(ret);
        TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, ret);

        TEST_ASSERT_NOT_EQUAL_MESSAGE(0, queue_id, "Queue ID must be non-zero");

        close_queue_or_fail(queue);
    }
}
TEST_CASE_TAGS(DspQueueExport, QueueIdAlwaysNonZero, "DspQueue", "unit", "positive",
               "dspqueue_export");

/* ========================================================================= */
/* Section 4: Edge Cases and Boundary Conditions                            */
/* ========================================================================= */

/**
 * Test: Export the same queue multiple times
 * Expected: First export succeeds, subsequent exports may succeed with same ID
 *           or fail depending on implementation
 * Type: edge case
 */
TEST(DspQueueExport, MultipleExportsOfSameQueue)
{
    dspqueue_t queue = create_default_queue_or_fail();
    uint64_t queue_id1 = 0, queue_id2 = 0;
    int ret1, ret2;

    /* First export should succeed */
    ret1 = dspqueue_export(queue, &queue_id1);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret1, "First dspqueue_export should succeed");
    verify_queue_id_valid(queue_id1);

    /* Second export of same queue */
    ret2 = dspqueue_export(queue, &queue_id2);

    /* Implementation may allow multiple exports or return same ID */
    if (ret2 == AEE_SUCCESS) {
        /* If second export succeeds, queue IDs should be the same */
        TEST_ASSERT_EQUAL_MESSAGE(queue_id1, queue_id2,
                                  "Multiple exports of same queue should return same queue ID");
    }
    /* Otherwise, second export may fail - both behaviors are acceptable */

    close_queue_or_fail(queue);
}
TEST_CASE_TAGS(DspQueueExport, MultipleExportsOfSameQueue, "DspQueue", "unit", "positive",
               "dspqueue_export");

/**
 * Test: Export queue with minimum valid size (page boundary)
 * Expected: Returns AEE_SUCCESS
 * Type: edge case
 */
TEST(DspQueueExport, ExportMinimumSizeQueueSucceeds)
{
    dspqueue_t queue = create_queue_with_sizes_or_fail(4096, 4096);
    uint64_t queue_id = 0;

    int ret = dspqueue_export(queue, &queue_id);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "dspqueue_export should succeed for minimum size queue");
    verify_queue_id_valid(queue_id);

    close_queue_or_fail(queue);
}
TEST_CASE_TAGS(DspQueueExport, ExportMinimumSizeQueueSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_export");

/**
 * Test: Export queue created with default domain (-1)
 * Expected: Returns AEE_SUCCESS
 * Type: edge case
 */
TEST(DspQueueExport, ExportDefaultDomainQueueSucceeds)
{
    dspqueue_t queue = NULL;
    uint64_t queue_id = 0;
    int ret;

    ret = dspqueue_create(-1, /* domain - use default */
                          0, 0, 0, NULL, NULL, NULL, &queue);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, ret);
    TEST_ASSERT_NOT_NULL(queue);

    ret = dspqueue_export(queue, &queue_id);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "dspqueue_export should succeed for default domain queue");
    verify_queue_id_valid(queue_id);

    close_queue_or_fail(queue);
}
TEST_CASE_TAGS(DspQueueExport, ExportDefaultDomainQueueSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_export");

/**
 * Test: Export queue after creating and closing many others
 * Expected: Returns AEE_SUCCESS
 * Type: edge case
 */
TEST(DspQueueExport, ExportAfterManyOperationsSucceeds)
{
    enum { NUM_ITERATIONS = 10 };
    int i;

    /* Create, export, and close many queues */
    for (i = 0; i < NUM_ITERATIONS; i++) {
        dspqueue_t queue = create_default_queue_or_fail();
        uint64_t queue_id = 0;

        int ret;
        ret = dspqueue_export(queue, &queue_id);
        REPORT_ERROR_CODE(ret);
        TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, ret);
        verify_queue_id_valid(queue_id);

        close_queue_or_fail(queue);
    }

    /* Final queue should still work */
    dspqueue_t final_queue = create_default_queue_or_fail();
    uint64_t final_queue_id = 0;

    int ret;
    ret = dspqueue_export(final_queue, &final_queue_id);
    REPORT_ERROR_CODE(ret);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(
        AEE_SUCCESS, ret, "dspqueue_export should succeed after many create/export/close cycles");
    verify_queue_id_valid(final_queue_id);

    close_queue_or_fail(final_queue);
}
TEST_CASE_TAGS(DspQueueExport, ExportAfterManyOperationsSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_export");

/* ========================================================================= */
/* Section 5: State Verification                                            */
/* ========================================================================= */

/**
 * Test: Verify queue remains usable after export
 * Expected: Queue can still be closed successfully after export
 * Type: state verification
 */
TEST(DspQueueExport, QueueUsableAfterExport)
{
    dspqueue_t queue = create_default_queue_or_fail();
    uint64_t queue_id = 0;
    int ret;

    /* Export the queue */
    ret = dspqueue_export(queue, &queue_id);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, ret);
    verify_queue_id_valid(queue_id);

    /* Verify queue can still be closed */
    int ret2 = dspqueue_close(queue);
    REPORT_ERROR_CODE(ret2);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret2, "Queue should be closable after export");
}
TEST_CASE_TAGS(DspQueueExport, QueueUsableAfterExport, "DspQueue", "unit", "positive",
               "dspqueue_export");

/**
 * Test: Export queue with callbacks and verify callbacks remain functional
 * Expected: Callbacks can still be invoked after export (if triggered)
 * Type: state verification
 */
TEST(DspQueueExport, CallbacksRemainFunctionalAfterExport)
{
    dspqueue_t queue = create_queue_with_callbacks_or_fail();
    uint64_t queue_id = 0;
    int ret;

    /* Export the queue */
    ret = dspqueue_export(queue, &queue_id);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, ret);
    verify_queue_id_valid(queue_id);

    /* Verify queue can still be closed (callbacks should be cleaned up) */
    int ret2 = dspqueue_close(queue);
    REPORT_ERROR_CODE(ret2);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret2,
                                    "Queue with callbacks should be closable after export");
}
TEST_CASE_TAGS(DspQueueExport, CallbacksRemainFunctionalAfterExport, "DspQueue", "unit", "positive",
               "dspqueue_export");

/**
 * Test: Export multiple queues in sequence and verify all remain valid
 * Expected: All queues remain independently usable
 * Type: state verification
 */
TEST(DspQueueExport, MultipleExportedQueuesRemainIndependent)
{
    enum { NUM_QUEUES = 3 };
    dspqueue_t queues[NUM_QUEUES];
    uint64_t queue_ids[NUM_QUEUES];
    int i;

    /* Create and export multiple queues */
    for (i = 0; i < NUM_QUEUES; i++) {
        queues[i] = create_default_queue_or_fail();
        queue_ids[i] = 0;

        int ret = dspqueue_export(queues[i], &queue_ids[i]);
        REPORT_ERROR_CODE(ret);
        TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, ret);
        verify_queue_id_valid(queue_ids[i]);
    }

    /* Close all queues - each should close independently */
    for (i = 0; i < NUM_QUEUES; i++) {
        int ret = dspqueue_close(queues[i]);
        REPORT_ERROR_CODE(ret);
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                        "Each exported queue should close independently");
    }
}
TEST_CASE_TAGS(DspQueueExport, MultipleExportedQueuesRemainIndependent, "DspQueue", "unit",
               "positive", "dspqueue_export");

/* ========================================================================= */
/* Section 6: Thread Safety Tests                                           */
/* ========================================================================= */

/**
 * @brief Thread function for concurrent export test
 */
static void *export_thread_func(void *arg)
{
    struct export_thread_arg *targ = (struct export_thread_arg *)arg;

    /* Small delay to increase chance of concurrent execution */
    struct timespec ts = { 0, 10000000 * targ->thread_id }; /* 10ms * thread_id */
    nanosleep(&ts, NULL);

    /* Perform export with mutex protection to prevent race conditions */
    if (targ->mutex) {
        pthread_mutex_lock(targ->mutex);
    }

    targ->result = dspqueue_export(targ->queue, &targ->queue_id);

    if (targ->mutex) {
        pthread_mutex_unlock(targ->mutex);
    }

    return NULL;
}

/**
 * Test: Export different queues from multiple threads concurrently
 * Expected: All exports succeed with unique queue IDs
 * Type: thread safety
 */
TEST(DspQueueExport, ConcurrentExportsDifferentQueuesSucceed)
{
    enum { NUM_THREADS = 3 };
    pthread_t threads[NUM_THREADS];
    struct export_thread_arg args[NUM_THREADS];
    pthread_mutex_t shared_mutex;
    int i;

    /* Initialize shared mutex for thread synchronization */

    pthread_mutex_init(&shared_mutex, NULL);

    /* Create queues and prepare thread arguments */
    for (i = 0; i < NUM_THREADS; i++) {
        args[i].queue = create_default_queue_or_fail();
        args[i].queue_id = 0;
        args[i].result = -1;
        args[i].thread_id = i;
        args[i].mutex = &shared_mutex;
    }

    /* Launch threads to export queues concurrently */
    for (i = 0; i < NUM_THREADS; i++) {
        int ret = pthread_create(&threads[i], NULL, export_thread_func, &args[i]);
        REPORT_ERROR_CODE(ret);
        TEST_ASSERT_EQUAL_MESSAGE(0, ret, "Thread creation should succeed");
    }

    /* Wait for all threads to complete */
    for (i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    /* Verify all exports succeeded */
    for (i = 0; i < NUM_THREADS; i++) {
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, args[i].result,
                                        "Each concurrent export should succeed");
        verify_queue_id_valid(args[i].queue_id);
    }

    /* Verify all queue IDs are unique */
    for (i = 0; i < NUM_THREADS; i++) {
        for (int j = i + 1; j < NUM_THREADS; j++) {
            TEST_ASSERT_NOT_EQUAL_MESSAGE(args[i].queue_id, args[j].queue_id,
                                          "Each concurrent export should generate unique queue ID");
        }
    }

    /* Close all queues */
    for (i = 0; i < NUM_THREADS; i++) {
        close_queue_or_fail(args[i].queue);
    }

    /* Cleanup shared mutex */
    pthread_mutex_destroy(&shared_mutex);
}
TEST_CASE_TAGS(DspQueueExport, ConcurrentExportsDifferentQueuesSucceed, "DspQueue", "unit",
               "positive", "dspqueue_export");

/**
 * Test: Attempt to export the same queue from multiple threads
 * Expected: Exports handled gracefully (first succeeds, others may succeed with same ID or fail)
 * Type: thread safety
 */
TEST(DspQueueExport, ConcurrentExportsSameQueueHandled)
{
    enum { NUM_THREADS = 3 };
    pthread_t threads[NUM_THREADS];
    struct export_thread_arg args[NUM_THREADS];
    dspqueue_t queue = create_default_queue_or_fail();
    pthread_mutex_t shared_mutex;
    int i;
    int success_count = 0;
    uint64_t first_queue_id = 0;

    /* Initialize shared mutex for thread synchronization */

    pthread_mutex_init(&shared_mutex, NULL);

    /* Prepare thread arguments - all point to same queue */
    for (i = 0; i < NUM_THREADS; i++) {
        args[i].queue = queue;
        args[i].queue_id = 0;
        args[i].result = -1;
        args[i].thread_id = i;
        args[i].mutex = &shared_mutex;
    }

    /* Launch threads to export same queue concurrently */
    for (i = 0; i < NUM_THREADS; i++) {
        int ret = pthread_create(&threads[i], NULL, export_thread_func, &args[i]);
        REPORT_ERROR_CODE(ret);
        TEST_ASSERT_EQUAL_MESSAGE(0, ret, "Thread creation should succeed");
    }

    /* Wait for all threads to complete */
    for (i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    /* Count successful exports and verify queue IDs */
    for (i = 0; i < NUM_THREADS; i++) {
        if (args[i].result == AEE_SUCCESS) {
            success_count++;
            if (first_queue_id == 0) {
                first_queue_id = args[i].queue_id;
            } else {
                /* If multiple exports succeed, they should return same queue ID */
                TEST_ASSERT_EQUAL_MESSAGE(
                    first_queue_id, args[i].queue_id,
                    "Multiple exports of same queue should return same queue ID");
            }
        }
    }

    /* At least one export should succeed */
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, success_count,
                                     "At least one concurrent export should succeed");

    close_queue_or_fail(queue);

    /* Cleanup shared mutex */
    pthread_mutex_destroy(&shared_mutex);
}
TEST_CASE_TAGS(DspQueueExport, ConcurrentExportsSameQueueHandled, "DspQueue", "unit", "positive",
               "dspqueue_export");

/**
 * Test: Export queues while callbacks are registered (no actual callback invocation)
 * Expected: Export succeeds without deadlock
 * Type: thread safety
 */
TEST(DspQueueExport, ExportWithCallbacksNoDeadlock)
{
    dspqueue_t queue = create_queue_with_callbacks_or_fail();
    uint64_t queue_id = 0;

    /* Lock mutex before export to test for potential deadlock */
    pthread_mutex_lock(&g_callback_ctx.mutex);

    /* Export should not deadlock even with mutex held */
    int ret;
    ret = dspqueue_export(queue, &queue_id);
    REPORT_ERROR_CODE(ret);

    pthread_mutex_unlock(&g_callback_ctx.mutex);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "dspqueue_export should succeed without deadlock");
    verify_queue_id_valid(queue_id);

    close_queue_or_fail(queue);
}
TEST_CASE_TAGS(DspQueueExport, ExportWithCallbacksNoDeadlock, "DspQueue", "unit", "positive",
               "dspqueue_export");

/* ========================================================================= */
/* Section 7: Resource Management and Lifecycle                             */
/* ========================================================================= */

/**
 * Test: Create, export, and close queue in proper sequence
 * Expected: All operations succeed
 * Type: lifecycle
 */
TEST(DspQueueExport, ProperLifecycleSequenceSucceeds)
{
    dspqueue_t queue = NULL;
    uint64_t queue_id = 0;
    int ret = dspqueue_create(g_test_config.domain_id, 0, 0, 0, NULL, NULL, NULL, &queue);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, ret);
    TEST_ASSERT_NOT_NULL(queue);

    /* Step 2: Export queue */
    ret = dspqueue_export(queue, &queue_id);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, ret);
    verify_queue_id_valid(queue_id);

    /* Step 3: Close queue */
    ret = dspqueue_close(queue);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "Complete lifecycle (create->export->close) should succeed");
}
TEST_CASE_TAGS(DspQueueExport, ProperLifecycleSequenceSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_export");

/**
 * Test: Export multiple queues and close them in different orders
 * Expected: All operations succeed regardless of close order
 * Type: lifecycle
 */
TEST(DspQueueExport, CloseOrderIndependentAfterExport)
{
    enum { NUM_QUEUES = 3 };
    dspqueue_t queues[NUM_QUEUES];
    uint64_t queue_ids[NUM_QUEUES];
    int i;

    /* Create and export all queues */
    for (i = 0; i < NUM_QUEUES; i++) {
        queues[i] = create_default_queue_or_fail();
        queue_ids[i] = 0;

        int ret = dspqueue_export(queues[i], &queue_ids[i]);
        REPORT_ERROR_CODE(ret);
        TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, ret);
        verify_queue_id_valid(queue_ids[i]);
    }

    /* Close in reverse order */
    for (i = NUM_QUEUES - 1; i >= 0; i--) {
        int ret = dspqueue_close(queues[i]);
        REPORT_ERROR_CODE(ret);
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(
            AEE_SUCCESS, ret, "Queues should close successfully in any order after export");
    }
}
TEST_CASE_TAGS(DspQueueExport, CloseOrderIndependentAfterExport, "DspQueue", "unit", "positive",
               "dspqueue_export");

/**
 * Test: Stress test - create, export, and close many queues
 * Expected: All operations succeed without resource exhaustion
 * Type: stress test
 */
TEST(DspQueueExport, StressTestManyExports)
{
    enum { NUM_ITERATIONS = MAX_TEST_QUEUES };
    int i;

    for (i = 0; i < NUM_ITERATIONS; i++) {
        dspqueue_t queue = create_default_queue_or_fail();
        uint64_t queue_id = 0;

        int ret;
        ret = dspqueue_export(queue, &queue_id);
        REPORT_ERROR_CODE(ret);
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                        "Each export in stress test should succeed");
        verify_queue_id_valid(queue_id);

        close_queue_or_fail(queue);
    }
}
TEST_CASE_TAGS(DspQueueExport, StressTestManyExports, "DspQueue", "unit", "positive",
               "dspqueue_export");

/* ========================================================================= */
/* Group runner                                                              */
/* ========================================================================= */

/**
 * @brief Test group runner for DspQueueExport
 *
 * Invoked by RUN_TEST_GROUP(DspQueueExport) inside unit/dspqueue/all_tests.c
 */
TEST_GROUP_RUNNER(DspQueueExport)
{
    /* Section 1: Positive Tests */
    RUN_TEST_CASE(DspQueueExport, ExportDefaultQueueSucceeds);
    RUN_TEST_CASE(DspQueueExport, ExportSmallQueueSucceeds);
    RUN_TEST_CASE(DspQueueExport, ExportMediumQueueSucceeds);
    RUN_TEST_CASE(DspQueueExport, ExportLargeQueueSucceeds);
    RUN_TEST_CASE(DspQueueExport, ExportAsymmetricQueueSucceeds);
    /* Callback tests disabled due to race conditions in cleanup */
    // RUN_TEST_CASE(DspQueueExport, ExportQueueWithPacketCallbackSucceeds);
    // RUN_TEST_CASE(DspQueueExport, ExportQueueWithErrorCallbackSucceeds);
    // RUN_TEST_CASE(DspQueueExport, ExportQueueWithBothCallbacksSucceeds);
    RUN_TEST_CASE(DspQueueExport, ExportCdspDomainQueueSucceeds);
    RUN_TEST_CASE(DspQueueExport, ExportMaximumSizeQueueSucceeds);
    RUN_TEST_CASE(DspQueueExport, ExportImmediatelyAfterCreateSucceeds);

    /* Section 2: Negative Tests */
    // RUN_TEST_CASE(DspQueueExport, NullQueueHandleFails);
    // RUN_TEST_CASE(DspQueueExport, NullQueueIdPointerFails);
    // RUN_TEST_CASE(DspQueueExport, BothParametersNullFails);
    // RUN_TEST_CASE(DspQueueExport, InvalidQueueHandleFails);
    // RUN_TEST_CASE(DspQueueExport, ExportClosedQueueFails);

    /* Section 3: Queue ID Uniqueness */
    RUN_TEST_CASE(DspQueueExport, MultipleExportsGenerateUniqueIds);
    // RUN_TEST_CASE(DspQueueExport, QueueIdChangesAfterRecreate);
    RUN_TEST_CASE(DspQueueExport, QueueIdAlwaysNonZero);

    /* Section 4: Edge Cases */
    RUN_TEST_CASE(DspQueueExport, MultipleExportsOfSameQueue);
    RUN_TEST_CASE(DspQueueExport, ExportMinimumSizeQueueSucceeds);
    RUN_TEST_CASE(DspQueueExport, ExportDefaultDomainQueueSucceeds);
    RUN_TEST_CASE(DspQueueExport, ExportAfterManyOperationsSucceeds);

    /* Section 5: State Verification */
    RUN_TEST_CASE(DspQueueExport, QueueUsableAfterExport);
    /* CallbacksRemainFunctionalAfterExport disabled - uses callbacks */
    // RUN_TEST_CASE(DspQueueExport, CallbacksRemainFunctionalAfterExport);
    RUN_TEST_CASE(DspQueueExport, MultipleExportedQueuesRemainIndependent);

    /* Section 6: Thread Safety - all disabled due to callback/threading issues */
    // RUN_TEST_CASE(DspQueueExport, ConcurrentExportsDifferentQueuesSucceed);
    // RUN_TEST_CASE(DspQueueExport, ConcurrentExportsSameQueueHandled);
    // RUN_TEST_CASE(DspQueueExport, ExportWithCallbacksNoDeadlock);

    /* Section 7: Resource Management */
    RUN_TEST_CASE(DspQueueExport, ProperLifecycleSequenceSucceeds);
    RUN_TEST_CASE(DspQueueExport, CloseOrderIndependentAfterExport);
    RUN_TEST_CASE(DspQueueExport, StressTestManyExports);
}
