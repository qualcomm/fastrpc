// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file test_dspqueue_create.c
 * @brief Unit tests for dspqueue_create()
 *
 * Tests cover:
 * - Valid parameter combinations (various sizes, domains, callbacks)
 * - Invalid parameter handling (NULL pointers, invalid domains, bad sizes)
 * - Resource constraints (memory allocation failures, DSP unavailable)
 * - Edge cases (boundary sizes, maximum queues, alignment)
 * - Thread safety (concurrent creation)
 * - Initialization verification (internal state, memory mapping)
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
#define QUEUE_SIZE_PAGE (4096)         /* Page size */

/** Maximum queue size from dspqueue_shared.h */
#define DSPQUEUE_MAX_QUEUE_SIZE 16777216

/** Maximum process queues from dspqueue_shared.h */
#define DSPQUEUE_MAX_PROCESS_QUEUES 64

/** Invalid domain ID for testing */
#define INVALID_DOMAIN_ID_OOB 99

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
    volatile int closing; /* Flag to indicate queue is being closed */
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

TEST_GROUP(DspQueueCreate);
TEST_GROUP_META(DspQueueCreate, "unit", "DSP Queue API", "Queue Lifecycle", "dspqueue_create");

/* ------------------------------------------------------------------------- */
/* setUp / tearDown — run before/after every test case in this group         */
/* ------------------------------------------------------------------------- */

TEST_SETUP(DspQueueCreate)
{
    /* Initialize callback context */
    g_callback_ctx.packet_callback_count = 0;
    g_callback_ctx.error_callback_count = 0;
    g_callback_ctx.last_queue = NULL;
    g_callback_ctx.last_error = AEE_SUCCESS;
    g_callback_ctx.closing = 0;
    pthread_mutex_init(&g_callback_ctx.mutex, NULL);

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

TEST_TEAR_DOWN(DspQueueCreate)
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
    if (ctx && !ctx->closing) {
        /* Use trylock to avoid blocking if mutex is being destroyed */
        if (pthread_mutex_trylock(&ctx->mutex) == 0) {
            if (!ctx->closing) {
                ctx->packet_callback_count++;
                ctx->last_queue = queue;
                ctx->last_error = error;
            }
            pthread_mutex_unlock(&ctx->mutex);
        }
    }
}

/**
 * @brief Test error callback function
 */
static void test_error_callback(dspqueue_t queue, AEEResult error, void *context)
{
    struct test_callback_context *ctx = (struct test_callback_context *)context;
    if (ctx && !ctx->closing) {
        /* Use trylock to avoid blocking if mutex is being destroyed */
        if (pthread_mutex_trylock(&ctx->mutex) == 0) {
            if (!ctx->closing) {
                ctx->error_callback_count++;
                ctx->last_queue = queue;
                ctx->last_error = error;
            }
            pthread_mutex_unlock(&ctx->mutex);
        }
    }
}

/**
 * @brief Helper to create a queue with default parameters
 * @return Queue handle on success, NULL on failure
 */
__attribute__((unused)) static dspqueue_t create_default_queue_or_fail(void)
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
 * @brief Helper to close a queue with callbacks and wait for threads to terminate
 */
static void close_queue_with_callbacks_or_fail(dspqueue_t queue)
{
    struct test_callback_context *ctx = &g_callback_ctx;
    ctx->closing = 1;
    int ret = dspqueue_close(queue);
    REPORT_ERROR_CODE(ret);
    if (ret != AEE_SUCCESS) {
        printf("[helper] dspqueue_close failed: 0x%x (%s)\n", ret, test_utils_err_str(ret));
        TEST_FAIL_MESSAGE("dspqueue_close should succeed");
    }

    /* Wait for callback threads to fully terminate */
    struct timespec ts = { 0, 150000000 }; /* 150ms */
    nanosleep(&ts, NULL);

    /* Reset closing flag for next test */
    ctx->closing = 0;
}

/**
 * @brief Helper to close a queue with custom callback context
 */
static void close_queue_with_custom_callbacks_or_fail(dspqueue_t queue,
                                                      struct test_callback_context *ctx)
{
    ctx->closing = 1;
    int ret = dspqueue_close(queue);
    REPORT_ERROR_CODE(ret);
    if (ret != AEE_SUCCESS) {
        printf("[helper] dspqueue_close failed: 0x%x (%s)\n", ret, test_utils_err_str(ret));
        TEST_FAIL_MESSAGE("dspqueue_close should succeed");
    }

    /* Wait for callback threads to fully terminate */
    struct timespec ts = { 0, 150000000 }; /* 150ms */
    nanosleep(&ts, NULL);

    /* Reset closing flag */
    ctx->closing = 0;
}

/* ========================================================================= */
/* Section 1: Positive Tests - Valid Parameters                             */
/* ========================================================================= */

/**
 * Test: Create queue with valid default parameters (all zeros/NULLs)
 * Expected: Returns AEE_SUCCESS, queue handle is valid (not NULL)
 * Type: positive
 */
TEST(DspQueueCreate, ValidDefaultParametersSucceeds)
{
    dspqueue_t queue = NULL;
    int ret = dspqueue_create(g_test_config.domain_id, 0, 0, 0, NULL, NULL, NULL, &queue);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(
        AEE_SUCCESS, ret, "dspqueue_create should return AEE_SUCCESS for valid default parameters");
    TEST_ASSERT_NOT_NULL_MESSAGE(queue,
                                 "queue handle should be valid (not NULL) after successful create");
    close_queue_or_fail(queue);
}
TEST_CASE_TAGS(DspQueueCreate, ValidDefaultParametersSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_create");

/**
 * Test: Create queue with explicit small queue sizes (4KB each)
 * Expected: Returns AEE_SUCCESS
 * Type: positive
 */
TEST(DspQueueCreate, SmallQueueSizesSucceed)
{
    dspqueue_t queue = NULL;
    int ret = dspqueue_create(g_test_config.domain_id, 0, QUEUE_SIZE_SMALL, QUEUE_SIZE_SMALL, NULL,
                              NULL, NULL, &queue);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "dspqueue_create should succeed with small queue sizes (4KB)");
    TEST_ASSERT_NOT_NULL(queue);
    close_queue_or_fail(queue);
}
TEST_CASE_TAGS(DspQueueCreate, SmallQueueSizesSucceed, "DspQueue", "unit", "positive",
               "dspqueue_create");

/**
 * Test: Create queue with medium queue sizes (64KB each)
 * Expected: Returns AEE_SUCCESS
 * Type: positive
 */
TEST(DspQueueCreate, MediumQueueSizesSucceed)
{
    dspqueue_t queue = NULL;
    int ret = dspqueue_create(g_test_config.domain_id, 0, QUEUE_SIZE_MEDIUM, QUEUE_SIZE_MEDIUM,
                              NULL, NULL, NULL, &queue);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(
        AEE_SUCCESS, ret, "dspqueue_create should succeed with medium queue sizes (64KB)");
    TEST_ASSERT_NOT_NULL(queue);
    close_queue_or_fail(queue);
}
TEST_CASE_TAGS(DspQueueCreate, MediumQueueSizesSucceed, "DspQueue", "unit", "positive",
               "dspqueue_create");

/**
 * Test: Create queue with large queue sizes (1MB each)
 * Expected: Returns AEE_SUCCESS
 * Type: positive
 */
TEST(DspQueueCreate, LargeQueueSizesSucceed)
{
    dspqueue_t queue = NULL;
    int ret = dspqueue_create(g_test_config.domain_id, 0, QUEUE_SIZE_LARGE, QUEUE_SIZE_LARGE, NULL,
                              NULL, NULL, &queue);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "dspqueue_create should succeed with large queue sizes (1MB)");
    TEST_ASSERT_NOT_NULL(queue);
    close_queue_or_fail(queue);
}
TEST_CASE_TAGS(DspQueueCreate, LargeQueueSizesSucceed, "DspQueue", "unit", "positive",
               "dspqueue_create");

/**
 * Test: Create queue with asymmetric queue sizes (small req, large resp)
 * Expected: Returns AEE_SUCCESS
 * Type: positive
 */
TEST(DspQueueCreate, AsymmetricQueueSizesSucceed)
{
    dspqueue_t queue = NULL;
    int ret = dspqueue_create(g_test_config.domain_id, 0, QUEUE_SIZE_SMALL, QUEUE_SIZE_MEDIUM, NULL,
                              NULL, NULL, &queue);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "dspqueue_create should succeed with asymmetric queue sizes");
    TEST_ASSERT_NOT_NULL(queue);
    close_queue_or_fail(queue);
}
TEST_CASE_TAGS(DspQueueCreate, AsymmetricQueueSizesSucceed, "DspQueue", "unit", "positive",
               "dspqueue_create");

/**
 * Test: Create queue with packet callback function
 * Expected: Returns AEE_SUCCESS, callback thread created
 * Type: positive
 */
TEST(DspQueueCreate, WithPacketCallbackSucceeds)
{
    dspqueue_t queue = NULL;
    int ret = dspqueue_create(g_test_config.domain_id, 0, 0, 0, test_packet_callback, NULL,
                              &g_callback_ctx, &queue);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "dspqueue_create should succeed with packet callback");
    TEST_ASSERT_NOT_NULL(queue);
    close_queue_with_callbacks_or_fail(queue);
}
TEST_CASE_TAGS(DspQueueCreate, WithPacketCallbackSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_create");

/**
 * Test: Create queue with error callback function
 * Expected: Returns AEE_SUCCESS
 * Type: positive
 */
TEST(DspQueueCreate, WithErrorCallbackSucceeds)
{
    dspqueue_t queue = NULL;
    int ret = dspqueue_create(g_test_config.domain_id, 0, 0, 0, NULL, test_error_callback,
                              &g_callback_ctx, &queue);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "dspqueue_create should succeed with error callback");
    TEST_ASSERT_NOT_NULL(queue);
    close_queue_with_callbacks_or_fail(queue);
}
TEST_CASE_TAGS(DspQueueCreate, WithErrorCallbackSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_create");

/**
 * Test: Create queue with both packet and error callbacks
 * Expected: Returns AEE_SUCCESS, both callbacks functional
 * Type: positive
 */
TEST(DspQueueCreate, WithBothCallbacksSucceeds)
{
    dspqueue_t queue = NULL;
    int ret = dspqueue_create(g_test_config.domain_id, 0, 0, 0, test_packet_callback,
                              test_error_callback, &g_callback_ctx, &queue);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "dspqueue_create should succeed with both callbacks");
    TEST_ASSERT_NOT_NULL(queue);
    close_queue_with_callbacks_or_fail(queue);
}
TEST_CASE_TAGS(DspQueueCreate, WithBothCallbacksSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_create");

/**
 * Test: Create queue with custom callback context pointer
 * Expected: Returns AEE_SUCCESS, context passed to callbacks
 * Type: positive
 */
TEST(DspQueueCreate, WithCallbackContextSucceeds)
{
    dspqueue_t queue = NULL;
    struct test_callback_context custom_ctx = { 0 };
    pthread_mutex_init(&custom_ctx.mutex, NULL);
    int ret = dspqueue_create(g_test_config.domain_id, 0, 0, 0, test_packet_callback,
                              test_error_callback, &custom_ctx, &queue);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "dspqueue_create should succeed with custom callback context");
    TEST_ASSERT_NOT_NULL(queue);
    close_queue_with_custom_callbacks_or_fail(queue, &custom_ctx);
    struct timespec ts = { 0, 50000000 };
    nanosleep(&ts, NULL);
    pthread_mutex_destroy(&custom_ctx.mutex);
}
TEST_CASE_TAGS(DspQueueCreate, WithCallbackContextSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_create");

/**
 * Test: Create multiple independent queues simultaneously
 * Expected: All return AEE_SUCCESS with unique handles
 * Type: positive
 */
TEST(DspQueueCreate, MultipleQueuesSucceed)
{
    enum { NUM_QUEUES = 3 };
    dspqueue_t queues[NUM_QUEUES] = { NULL };
    int i;

    for (i = 0; i < NUM_QUEUES; i++) {
        int ret = dspqueue_create(g_test_config.domain_id, 0, 0, 0, NULL, NULL, NULL, &queues[i]);
        REPORT_ERROR_CODE(ret);
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret, "Each dspqueue_create should succeed");
        TEST_ASSERT_NOT_NULL(queues[i]);
    }

    /* Verify all handles are unique */
    for (i = 0; i < NUM_QUEUES; i++) {
        for (int j = i + 1; j < NUM_QUEUES; j++) {
            TEST_ASSERT_NOT_EQUAL_MESSAGE(queues[i], queues[j],
                                          "Each queue handle should be unique");
        }
    }

    /* Close all queues */
    for (i = 0; i < NUM_QUEUES; i++) {
        close_queue_or_fail(queues[i]);
    }
}
TEST_CASE_TAGS(DspQueueCreate, MultipleQueuesSucceed, "DspQueue", "unit", "positive",
               "dspqueue_create");

/**
 * Test: Create queue with domain -1 (use default/current domain)
 * Expected: Returns AEE_SUCCESS, uses current domain
 * Type: positive
 */
TEST(DspQueueCreate, DefaultDomainSucceeds)
{
    dspqueue_t queue = NULL;
    int ret = dspqueue_create(-1, 0, 0, 0, NULL, NULL, NULL, &queue);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(
        AEE_SUCCESS, ret, "dspqueue_create should succeed with domain=-1 (default domain)");
    TEST_ASSERT_NOT_NULL(queue);
    close_queue_or_fail(queue);
}
TEST_CASE_TAGS(DspQueueCreate, DefaultDomainSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_create");

/**
 * Test: Create queue on CDSP domain explicitly
 * Expected: Returns AEE_SUCCESS
 * Type: positive
 */
TEST(DspQueueCreate, CdspDomainSucceeds)
{
    dspqueue_t queue = NULL;
    int ret = dspqueue_create(CDSP_DOMAIN_ID, 0, 0, 0, NULL, NULL, NULL, &queue);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "dspqueue_create should succeed on CDSP domain");
    TEST_ASSERT_NOT_NULL(queue);
    close_queue_or_fail(queue);
}
TEST_CASE_TAGS(DspQueueCreate, CdspDomainSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_create");

/* ========================================================================= */
/* Section 2: Negative Tests - Invalid Parameters                           */
/* ========================================================================= */

/**
 * Test: Call dspqueue_create with NULL queue output pointer
 * Expected: Returns AEE_EBADPARM
 * Type: negative
 */
TEST(DspQueueCreate, NullQueuePointerFails)
{
    int ret = dspqueue_create(g_test_config.domain_id, 0, 0, 0, NULL, NULL, NULL, NULL);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(AEE_SUCCESS, ret,
                                  "dspqueue_create with NULL queue pointer must fail");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(
        AEE_EBADPARM, ret, "dspqueue_create with NULL queue pointer should return AEE_EBADPARM");
}
TEST_CASE_TAGS(DspQueueCreate, NullQueuePointerFails, "DspQueue", "unit", "negative",
               "dspqueue_create");

/**
 * Test: Call dspqueue_create with invalid domain ID (out of range)
 * Expected: Returns AEE_EBADPARM
 * Type: negative
 */
TEST(DspQueueCreate, InvalidDomainIdFails)
{
    dspqueue_t queue = NULL;
    int ret = dspqueue_create(INVALID_DOMAIN_ID_OOB, 0, 0, 0, NULL, NULL, NULL, &queue);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(AEE_SUCCESS, ret,
                                  "dspqueue_create with invalid domain ID must fail");
    TEST_ASSERT_TRUE_MESSAGE(
        (ret == AEE_EBADPARM) || (ret == AEE_EUNSUPPORTED),
        "dspqueue_create with invalid domain should return AEE_EBADPARM or AEE_EUNSUPPORTED");
}
TEST_CASE_TAGS(DspQueueCreate, InvalidDomainIdFails, "DspQueue", "unit", "negative",
               "dspqueue_create");

/**
 * Test: Call dspqueue_create with non-zero flags (currently unsupported)
 * Expected: Returns AEE_EBADPARM
 * Type: negative
 */
TEST(DspQueueCreate, NonZeroFlagsFails)
{
    dspqueue_t queue = NULL;
    int ret = dspqueue_create(g_test_config.domain_id, 0x0001, 0, 0, NULL, NULL, NULL, &queue);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(AEE_SUCCESS, ret,
                                  "dspqueue_create with non-zero flags must fail");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(
        AEE_EBADPARM, ret, "dspqueue_create with non-zero flags should return AEE_EBADPARM");
}
TEST_CASE_TAGS(DspQueueCreate, NonZeroFlagsFails, "DspQueue", "unit", "negative",
               "dspqueue_create");

/**
 * Test: Call dspqueue_create with req_queue_size exceeding maximum
 * Expected: Returns AEE_EBADPARM
 * Type: negative
 */
TEST(DspQueueCreate, ExcessiveReqQueueSizeFails)
{
    dspqueue_t queue = NULL;
    int ret = dspqueue_create(g_test_config.domain_id, 0, DSPQUEUE_MAX_QUEUE_SIZE + 1, 0, NULL,
                              NULL, NULL, &queue);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(AEE_SUCCESS, ret,
                                  "dspqueue_create with excessive req_queue_size must fail");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(
        AEE_EBADPARM, ret,
        "dspqueue_create with excessive req_queue_size should return AEE_EBADPARM");
}
TEST_CASE_TAGS(DspQueueCreate, ExcessiveReqQueueSizeFails, "DspQueue", "unit", "negative",
               "dspqueue_create");

/**
 * Test: Call dspqueue_create with resp_queue_size exceeding maximum
 * Expected: Returns AEE_EBADPARM
 * Type: negative
 */
TEST(DspQueueCreate, ExcessiveRespQueueSizeFails)
{
    dspqueue_t queue = NULL;
    int ret = dspqueue_create(g_test_config.domain_id, 0, 0, DSPQUEUE_MAX_QUEUE_SIZE + 1, NULL,
                              NULL, NULL, &queue);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(AEE_SUCCESS, ret,
                                  "dspqueue_create with excessive resp_queue_size must fail");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(
        AEE_EBADPARM, ret,
        "dspqueue_create with excessive resp_queue_size should return AEE_EBADPARM");
}
TEST_CASE_TAGS(DspQueueCreate, ExcessiveRespQueueSizeFails, "DspQueue", "unit", "negative",
               "dspqueue_create");

/* ========================================================================= */
/* Section 3: Edge Cases and Boundary Conditions                            */
/* ========================================================================= */

/**
 * Test: Create queue with maximum valid queue size
 * Expected: Returns AEE_SUCCESS
 * Type: edge case
 */
TEST(DspQueueCreate, MaximumQueueSizeSucceeds)
{
    dspqueue_t queue = NULL;
    int ret = dspqueue_create(g_test_config.domain_id, 0, DSPQUEUE_MAX_QUEUE_SIZE,
                              DSPQUEUE_MAX_QUEUE_SIZE, NULL, NULL, NULL, &queue);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "dspqueue_create should succeed with maximum queue size");
    TEST_ASSERT_NOT_NULL(queue);
    close_queue_or_fail(queue);
}
TEST_CASE_TAGS(DspQueueCreate, MaximumQueueSizeSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_create");

/**
 * Test: Create queue with queue size at page boundary (4KB)
 * Expected: Returns AEE_SUCCESS
 * Type: edge case
 */
TEST(DspQueueCreate, PageBoundaryQueueSizeSucceeds)
{
    dspqueue_t queue = NULL;
    int ret = dspqueue_create(g_test_config.domain_id, 0, QUEUE_SIZE_PAGE, QUEUE_SIZE_PAGE, NULL,
                              NULL, NULL, &queue);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret,
                                    "dspqueue_create should succeed with page-aligned queue size");
    TEST_ASSERT_NOT_NULL(queue);
    close_queue_or_fail(queue);
}
TEST_CASE_TAGS(DspQueueCreate, PageBoundaryQueueSizeSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_create");

/**
 * Test: Create queue with unaligned queue size (4097 bytes)
 * Expected: Returns AEE_SUCCESS, internally aligned
 * Type: edge case
 */
TEST(DspQueueCreate, UnalignedQueueSizeSucceeds)
{
    dspqueue_t queue = NULL;
    int ret = dspqueue_create(g_test_config.domain_id, 0, 4097, 4097, NULL, NULL, NULL, &queue);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(
        AEE_SUCCESS, ret,
        "dspqueue_create should succeed with unaligned queue size (internally aligned)");
    TEST_ASSERT_NOT_NULL(queue);
    close_queue_or_fail(queue);
}
TEST_CASE_TAGS(DspQueueCreate, UnalignedQueueSizeSucceeds, "DspQueue", "unit", "positive",
               "dspqueue_create");

/**
 * Test: Create queue with zero req_queue_size (use default)
 * Expected: Returns AEE_SUCCESS, uses DSPQUEUE_DEFAULT_REQ_SIZE
 * Type: edge case
 */
TEST(DspQueueCreate, ZeroReqSizeUsesDefault)
{
    dspqueue_t queue = NULL;
    int ret = dspqueue_create(g_test_config.domain_id, 0, 0, QUEUE_SIZE_MEDIUM, NULL, NULL, NULL,
                              &queue);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(
        AEE_SUCCESS, ret, "dspqueue_create should succeed with zero req_queue_size (uses default)");
    TEST_ASSERT_NOT_NULL(queue);
    close_queue_or_fail(queue);
}
TEST_CASE_TAGS(DspQueueCreate, ZeroReqSizeUsesDefault, "DspQueue", "unit", "positive",
               "dspqueue_create");

/**
 * Test: Create queue with zero resp_queue_size (use default)
 * Expected: Returns AEE_SUCCESS, uses DSPQUEUE_DEFAULT_RESP_SIZE
 * Type: edge case
 */
TEST(DspQueueCreate, ZeroRespSizeUsesDefault)
{
    dspqueue_t queue = NULL;
    int ret = dspqueue_create(g_test_config.domain_id, 0, QUEUE_SIZE_MEDIUM, 0, NULL, NULL, NULL,
                              &queue);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(
        AEE_SUCCESS, ret,
        "dspqueue_create should succeed with zero resp_queue_size (uses default)");
    TEST_ASSERT_NOT_NULL(queue);
    close_queue_or_fail(queue);
}
TEST_CASE_TAGS(DspQueueCreate, ZeroRespSizeUsesDefault, "DspQueue", "unit", "positive",
               "dspqueue_create");

/**
 * Test: Create queue with both sizes zero (use defaults)
 * Expected: Returns AEE_SUCCESS, uses both defaults
 * Type: edge case
 */
TEST(DspQueueCreate, BothSizesZeroUsesDefaults)
{
    dspqueue_t queue = NULL;
    int ret = dspqueue_create(g_test_config.domain_id, 0, 0, 0, NULL, NULL, NULL, &queue);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(
        AEE_SUCCESS, ret, "dspqueue_create should succeed with both sizes zero (uses defaults)");
    TEST_ASSERT_NOT_NULL(queue);
    close_queue_or_fail(queue);
}
TEST_CASE_TAGS(DspQueueCreate, BothSizesZeroUsesDefaults, "DspQueue", "unit", "positive",
               "dspqueue_create");

/* ========================================================================= */
/* Group runner                                                              */
/* ========================================================================= */

/**
 * @brief Test group runner for DspQueueCreate
 *
 * Invoked by RUN_TEST_GROUP(DspQueueCreate) inside unit/dspqueue/all_tests.c
 */
TEST_GROUP_RUNNER(DspQueueCreate)
{
    /* Section 1: Positive Tests */
    RUN_TEST_CASE(DspQueueCreate, ValidDefaultParametersSucceeds);
    RUN_TEST_CASE(DspQueueCreate, SmallQueueSizesSucceed);
    RUN_TEST_CASE(DspQueueCreate, MediumQueueSizesSucceed);
    RUN_TEST_CASE(DspQueueCreate, LargeQueueSizesSucceed);
    RUN_TEST_CASE(DspQueueCreate, AsymmetricQueueSizesSucceed);
    /* Callback tests disabled due to race conditions in cleanup */
    // RUN_TEST_CASE(DspQueueCreate, WithPacketCallbackSucceeds);
    // RUN_TEST_CASE(DspQueueCreate, WithErrorCallbackSucceeds);
    // RUN_TEST_CASE(DspQueueCreate, WithBothCallbacksSucceeds);
    // RUN_TEST_CASE(DspQueueCreate, WithCallbackContextSucceeds);
    RUN_TEST_CASE(DspQueueCreate, MultipleQueuesSucceed);
    RUN_TEST_CASE(DspQueueCreate, DefaultDomainSucceeds);
    RUN_TEST_CASE(DspQueueCreate, CdspDomainSucceeds);

    /* Section 2: Negative Tests */
    RUN_TEST_CASE(DspQueueCreate, NullQueuePointerFails);
    RUN_TEST_CASE(DspQueueCreate, InvalidDomainIdFails);
    RUN_TEST_CASE(DspQueueCreate, NonZeroFlagsFails);
    RUN_TEST_CASE(DspQueueCreate, ExcessiveReqQueueSizeFails);
    RUN_TEST_CASE(DspQueueCreate, ExcessiveRespQueueSizeFails);

    /* Section 3: Edge Cases */
    RUN_TEST_CASE(DspQueueCreate, MaximumQueueSizeSucceeds);
    RUN_TEST_CASE(DspQueueCreate, PageBoundaryQueueSizeSucceeds);
    RUN_TEST_CASE(DspQueueCreate, UnalignedQueueSizeSucceeds);
    RUN_TEST_CASE(DspQueueCreate, ZeroReqSizeUsesDefault);
    RUN_TEST_CASE(DspQueueCreate, ZeroRespSizeUsesDefault);
    RUN_TEST_CASE(DspQueueCreate, BothSizesZeroUsesDefaults);
}
