// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/*
 * test_buffer_management.c - dspqueue buffer lifecycle tests
 *
 * Mirrors the buffer management in
 * examples/dspqueue/src/dspqueue_sample.c::process_test().
 * Tests rpcmem allocation, fastrpc_mmap, buffer reference flags
 * (REF/DEREF, FLUSH_SENDER, INVALIDATE_RECIPIENT) through a live
 * CPU<->DSP queue with the DSP service running.
 */

#include "AEEStdErr.h"
#include "dspqueue.h"
#include "dspqueue_feature_utils.h"
#include "rpcmem.h"
#include "test_utils.h"
#include "unity_fixture.h"

#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Message types - must match fastrpc_test_imp.c */
#define MSG_BYTE_SQUARE 3
#define MSG_BYTE_SQUARE_RESP 4

#define QUEUE_SIZE 4096
#define BUFFER_SIZE (4 * 1024)
#define WRITE_TIMEOUT_US 1000000

/* Minimal callback context: count responses and signal done */
struct buf_ctx {
    unsigned num_ops;
    unsigned num_done;
    sem_t done_sem;
    int failed;
    char err[128];
};

static void packet_cb(dspqueue_t queue, AEEResult error, void *context)
{
    struct buf_ctx *c = (struct buf_ctx *)context;
    uint32_t msg[2], flags, msg_len;
    struct dspqueue_buffer bufs[2];
    uint32_t num_bufs;

    while (1) {
        int ret = dspqueue_read_noblock(queue, &flags, 2, &num_bufs, bufs, sizeof(msg), &msg_len,
                                        (uint8_t *)msg);
        if (ret == AEE_EWOULDBLOCK)
            return;
        if (ret != AEE_SUCCESS) {
            c->failed = 1;
            snprintf(c->err, sizeof(c->err), "read_noblock: 0x%x", ret);
            sem_post(&c->done_sem);
            return;
        }
        if (msg[0] != MSG_BYTE_SQUARE_RESP)
            continue;
        c->num_done++;
        if (c->num_done == c->num_ops)
            sem_post(&c->done_sem);
    }
}

static void error_cb(dspqueue_t queue, AEEResult error, void *context)
{
    struct buf_ctx *c = (struct buf_ctx *)context;
    c->failed = 1;
    snprintf(c->err, sizeof(c->err), "error_cb: 0x%x", error);
    sem_post(&c->done_sem);
}

static int wait_done(struct buf_ctx *c, int timeout_sec)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_sec;
    return sem_timedwait(&c->done_sem, &ts);
}

/* Send one BYTE_SQUARE packet with the given buffer flags */
static int send_square(dspqueue_t queue, int in_fd, int out_fd, uint32_t in_flags,
                       uint32_t out_flags)
{
    uint32_t msg[2] = { MSG_BYTE_SQUARE, 0 };
    struct dspqueue_buffer bufs[2];
    memset(bufs, 0, sizeof(bufs));
    bufs[0].fd = in_fd;
    bufs[0].flags = in_flags;
    bufs[1].fd = out_fd;
    bufs[1].flags = out_flags;
    return dspqueue_write(queue, 0, 2, bufs, sizeof(msg), (uint8_t *)msg, WRITE_TIMEOUT_US);
}

/* ------------------------------------------------------------------ */

TEST_GROUP(DspQueueBufferManagement);
TEST_GROUP_META(DspQueueBufferManagement, "feature", "DSP Queue End-to-End Flows",
                "Buffer Lifecycle", "dspqueue buffer reference counting and cache coherency");

TEST_SETUP(DspQueueBufferManagement)
{
    int ret;
    /*
     * rpcmem is a process-global singleton already initialised by the
     * FastRPC driver via PL_INIT(rpcmem).  Calling rpcmem_init()/deinit()
     * here destroys the shared dmafd and breaks subsequent opens.
     */
    ret = dspqueue_feature_init(g_test_config.domain_id);
    if (ret != AEE_SUCCESS) {
        TEST_IGNORE_MESSAGE("DSP session not available - skipping");
    }
}

TEST_TEAR_DOWN(DspQueueBufferManagement) { dspqueue_feature_cleanup(); }

/* ------------------------------------------------------------------ */

/*
 * TakeAndReleaseReference
 * Send REF on the input buffer, then DEREF on the response path.
 * The DSP side sends back BYTE_SQUARE_RESP with DEREF flags.
 */
TEST(DspQueueBufferManagement, TakeAndReleaseReference)
{
    void *in_buf, *out_buf;
    int in_fd, out_fd;
    dspqueue_t queue = NULL;
    uint64_t dsp_queue_id, process_time;
    struct buf_ctx c;
    int ret;

    memset(&c, 0, sizeof(c));
    c.num_ops = 1;
    sem_init(&c.done_sem, 0, 0);

    in_buf = rpcmem_alloc(RPCMEM_HEAP_ID_SYSTEM, RPCMEM_DEFAULT_FLAGS | RPCMEM_HEAP_NOREG,
                          BUFFER_SIZE);
    out_buf = rpcmem_alloc(RPCMEM_HEAP_ID_SYSTEM, RPCMEM_DEFAULT_FLAGS | RPCMEM_HEAP_NOREG,
                           BUFFER_SIZE);
    TEST_ASSERT_NOT_NULL(in_buf);
    TEST_ASSERT_NOT_NULL(out_buf);

    in_fd = rpcmem_to_fd(in_buf);
    out_fd = rpcmem_to_fd(out_buf);
    TEST_ASSERT_GREATER_THAN(0, in_fd);
    TEST_ASSERT_GREATER_THAN(0, out_fd);

    ret = fastrpc_mmap(g_test_config.domain_id, in_fd, in_buf, 0, BUFFER_SIZE, FASTRPC_MAP_FD);
    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, ret);
    ret = fastrpc_mmap(g_test_config.domain_id, out_fd, out_buf, 0, BUFFER_SIZE, FASTRPC_MAP_FD);
    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, ret);

    /* Fill input: 0x05 -> square should be 0x19 */
    memset(in_buf, 0x05, BUFFER_SIZE);

    ret = dspqueue_create(g_test_config.domain_id, 0, QUEUE_SIZE, QUEUE_SIZE, packet_cb, error_cb,
                          &c, &queue);
    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, ret);
    ret = dspqueue_export(queue, &dsp_queue_id);
    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, ret);
    ret = fastrpc_test_dspqueue_start(g_dspqueue_feature_state.dspqueue_rpc_handle, dsp_queue_id);
    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, ret);
    g_dspqueue_feature_state.dsp_started = 1;

    /* REF on input (CPU writes, DSP reads): flush CPU, invalidate DSP */
    /* REF on output (DSP writes): flush CPU dirty lines */
    ret = send_square(queue, in_fd, out_fd,
                      DSPQUEUE_BUFFER_FLAG_REF | DSPQUEUE_BUFFER_FLAG_FLUSH_SENDER
                          | DSPQUEUE_BUFFER_FLAG_INVALIDATE_RECIPIENT,
                      DSPQUEUE_BUFFER_FLAG_REF | DSPQUEUE_BUFFER_FLAG_FLUSH_SENDER);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret, "dspqueue_write");

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, wait_done(&c, 10), "timeout");
    TEST_ASSERT_FALSE_MESSAGE(c.failed, c.err);

    /* Verify: 0x05 * 0x05 = 0x19 */
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x19, ((uint8_t *)out_buf)[0],
                                   "byte-square output should be 0x05^2 = 0x19");

    fastrpc_test_dspqueue_stop(g_dspqueue_feature_state.dspqueue_rpc_handle, &process_time);
    g_dspqueue_feature_state.dsp_started = 0;
    dspqueue_close(queue);
    sem_destroy(&c.done_sem);

    ret = fastrpc_munmap(g_test_config.domain_id, in_fd, NULL, 0);
    (void)ret;
    ret = fastrpc_munmap(g_test_config.domain_id, out_fd, NULL, 0);
    (void)ret;
    rpcmem_free(in_buf);
    rpcmem_free(out_buf);
}
TEST_CASE_TAGS(DspQueueBufferManagement, TakeAndReleaseReference, "DspQueue", "feature", "positive",
               "dspqueue_buffer_management");

/*
 * ReuseBufferMultipleTimes
 * Send the same buffer pair multiple times, verifying each response.
 * Mirrors the buffer-reuse loop in process_test().
 */
#define REUSE_OPS 5

TEST(DspQueueBufferManagement, ReuseBufferMultipleTimes)
{
    void *in_buf, *out_buf;
    int in_fd, out_fd;
    dspqueue_t queue = NULL;
    uint64_t dsp_queue_id, process_time;
    struct buf_ctx c;
    int ret, i;

    memset(&c, 0, sizeof(c));
    c.num_ops = REUSE_OPS;
    sem_init(&c.done_sem, 0, 0);

    in_buf = rpcmem_alloc(RPCMEM_HEAP_ID_SYSTEM, RPCMEM_DEFAULT_FLAGS | RPCMEM_HEAP_NOREG,
                          BUFFER_SIZE);
    out_buf = rpcmem_alloc(RPCMEM_HEAP_ID_SYSTEM, RPCMEM_DEFAULT_FLAGS | RPCMEM_HEAP_NOREG,
                           BUFFER_SIZE);
    TEST_ASSERT_NOT_NULL(in_buf);
    TEST_ASSERT_NOT_NULL(out_buf);
    in_fd = rpcmem_to_fd(in_buf);
    out_fd = rpcmem_to_fd(out_buf);
    TEST_ASSERT_GREATER_THAN(0, in_fd);
    TEST_ASSERT_GREATER_THAN(0, out_fd);

    ret = fastrpc_mmap(g_test_config.domain_id, in_fd, in_buf, 0, BUFFER_SIZE, FASTRPC_MAP_FD);
    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, ret);
    ret = fastrpc_mmap(g_test_config.domain_id, out_fd, out_buf, 0, BUFFER_SIZE, FASTRPC_MAP_FD);
    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, ret);

    /* Fill input: 0x03 -> square should be 0x09 */
    memset(in_buf, 0x03, BUFFER_SIZE);

    ret = dspqueue_create(g_test_config.domain_id, 0, QUEUE_SIZE, QUEUE_SIZE, packet_cb, error_cb,
                          &c, &queue);
    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, ret);
    ret = dspqueue_export(queue, &dsp_queue_id);
    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, ret);
    ret = fastrpc_test_dspqueue_start(g_dspqueue_feature_state.dspqueue_rpc_handle, dsp_queue_id);
    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, ret);
    g_dspqueue_feature_state.dsp_started = 1;

    for (i = 0; i < REUSE_OPS; i++) {
        ret = send_square(queue, in_fd, out_fd,
                          DSPQUEUE_BUFFER_FLAG_REF | DSPQUEUE_BUFFER_FLAG_FLUSH_SENDER
                              | DSPQUEUE_BUFFER_FLAG_INVALIDATE_RECIPIENT,
                          DSPQUEUE_BUFFER_FLAG_REF | DSPQUEUE_BUFFER_FLAG_FLUSH_SENDER);
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret, "dspqueue_write");
    }

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, wait_done(&c, 30), "timeout");
    TEST_ASSERT_FALSE_MESSAGE(c.failed, c.err);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(REUSE_OPS, c.num_done, "response count");

    /* 0x03 * 0x03 = 0x09 */
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x09, ((uint8_t *)out_buf)[0],
                                   "byte-square output should be 0x03^2 = 0x09");

    fastrpc_test_dspqueue_stop(g_dspqueue_feature_state.dspqueue_rpc_handle, &process_time);
    g_dspqueue_feature_state.dsp_started = 0;
    ret = dspqueue_close(queue);
    (void)ret;
    sem_destroy(&c.done_sem);

    ret = fastrpc_munmap(g_test_config.domain_id, in_fd, NULL, 0);
    (void)ret;
    ret = fastrpc_munmap(g_test_config.domain_id, out_fd, NULL, 0);
    (void)ret;
    rpcmem_free(in_buf);
    rpcmem_free(out_buf);
}
TEST_CASE_TAGS(DspQueueBufferManagement, ReuseBufferMultipleTimes, "DspQueue", "feature",
               "positive", "dspqueue_buffer_management");

/* ------------------------------------------------------------------ */

TEST_GROUP_RUNNER(DspQueueBufferManagement)
{
    RUN_TEST_CASE(DspQueueBufferManagement, TakeAndReleaseReference);
    RUN_TEST_CASE(DspQueueBufferManagement, ReuseBufferMultipleTimes);
}
