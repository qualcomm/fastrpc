// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/*
 * test_data_processing_flow.c - dspqueue byte-square processing tests
 *
 * Mirrors examples/dspqueue/src/dspqueue_sample.c::process_test().
 * The CPU allocates shared buffers, maps them to the DSP, creates a queue,
 * starts the DSP-side service, sends BYTE_SQUARE requests, waits for
 * responses in a packet callback, verifies the output, then stops the
 * DSP service, unmaps buffers, and closes the queue.
 */

#include "AEEStdErr.h"
#include "dspqueue.h"
#include "dspqueue_feature_utils.h"
#include "rpcmem.h"
#include "test_utils.h"
#include "unity_fixture.h"

#include <assert.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Message types - must match fastrpc_test_imp.c */
#define MSG_BYTE_SQUARE 3
#define MSG_BYTE_SQUARE_RESP 4

#define QUEUE_SIZE 4096
#define WRITE_TIMEOUT_US 1000000
#define MAX_BUFFERS 16

struct proc_ctx {
    unsigned num_ops;     /* total requests sent */
    unsigned num_squares; /* responses received */
    sem_t done_sem;
    int failed;
    char err[128];
};

static void packet_cb(dspqueue_t queue, AEEResult error, void *context)
{
    struct proc_ctx *c = (struct proc_ctx *)context;
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

        c->num_squares++;
        if (c->num_squares == c->num_ops)
            sem_post(&c->done_sem);
    }
}

static void error_cb(dspqueue_t queue, AEEResult error, void *context)
{
    struct proc_ctx *c = (struct proc_ctx *)context;
    c->failed = 1;
    snprintf(c->err, sizeof(c->err), "error_cb: 0x%x", error);
    sem_post(&c->done_sem);
}

static int wait_done(struct proc_ctx *c, int timeout_sec)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_sec;
    return sem_timedwait(&c->done_sem, &ts);
}

/* Verify byte-wise square: out[i] == in[i]*in[i] */
static int verify_byte_square(const uint8_t *in, const uint8_t *out, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (out[i] != (uint8_t)(in[i] * in[i])) {
            printf("mismatch at %zu: in=0x%02x out=0x%02x expected=0x%02x\n", i, in[i], out[i],
                   (uint8_t)(in[i] * in[i]));
            return 0;
        }
    }
    return 1;
}

/* ------------------------------------------------------------------ */

TEST_GROUP(DspQueueDataProcessingFlow);
TEST_GROUP_META(DspQueueDataProcessingFlow, "feature", "DSP Queue End-to-End Flows",
                "Data Processing", "dspqueue byte-square processing with shared buffers");

TEST_SETUP(DspQueueDataProcessingFlow)
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

TEST_TEAR_DOWN(DspQueueDataProcessingFlow) { dspqueue_feature_cleanup(); }

/* ------------------------------------------------------------------ */

/*
 * ProcessSingle - one byte-square request, verify output.
 * Mirrors process_test(..., num_ops=1, test=1).
 */
TEST(DspQueueDataProcessingFlow, ProcessSingle)
{
    const size_t buf_size = 4 * 1024;
    const int num_bufs = 2; /* [0]=input  [1]=output */
    void *buffers[2];
    int fds[2];
    dspqueue_t queue = NULL;
    uint64_t dsp_queue_id, process_time;
    struct proc_ctx c;
    int ret, i;

    memset(&c, 0, sizeof(c));
    c.num_ops = 1;
    sem_init(&c.done_sem, 0, 0);

    /* Allocate and map buffers */
    for (i = 0; i < num_bufs; i++) {
        buffers[i] = rpcmem_alloc(RPCMEM_HEAP_ID_SYSTEM, RPCMEM_DEFAULT_FLAGS | RPCMEM_HEAP_NOREG,
                                  buf_size);
        TEST_ASSERT_NOT_NULL_MESSAGE(buffers[i], "rpcmem_alloc");
        fds[i] = rpcmem_to_fd(buffers[i]);
        TEST_ASSERT_GREATER_THAN_MESSAGE(0, fds[i], "rpcmem_to_fd");
        ret = fastrpc_mmap(g_test_config.domain_id, fds[i], buffers[i], 0, buf_size,
                           FASTRPC_MAP_FD);
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret, "fastrpc_mmap");
    }

    /* Fill input with a sweep starting at the fd value (matches reference) */
    uint8_t *p = buffers[0];
    uint8_t v = (uint8_t)fds[0];
    for (size_t j = 0; j < buf_size; j++)
        *p++ = v++;

    /* Create queue */
    ret = dspqueue_create(g_test_config.domain_id, 0, QUEUE_SIZE, QUEUE_SIZE, packet_cb, error_cb,
                          &c, &queue);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret, "dspqueue_create");

    ret = dspqueue_export(queue, &dsp_queue_id);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret, "dspqueue_export");

    ret = fastrpc_test_dspqueue_start(g_dspqueue_feature_state.dspqueue_rpc_handle, dsp_queue_id);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret, "dspqueue_start");
    g_dspqueue_feature_state.dsp_started = 1;

    /* Send one BYTE_SQUARE request */
    uint32_t msg[2] = { MSG_BYTE_SQUARE, 0 };
    struct dspqueue_buffer bufs[2];
    memset(bufs, 0, sizeof(bufs));
    bufs[0].fd = fds[0];
    bufs[0].flags = DSPQUEUE_BUFFER_FLAG_REF | DSPQUEUE_BUFFER_FLAG_FLUSH_SENDER
                    | DSPQUEUE_BUFFER_FLAG_INVALIDATE_RECIPIENT;
    bufs[1].fd = fds[1];
    bufs[1].flags = DSPQUEUE_BUFFER_FLAG_REF | DSPQUEUE_BUFFER_FLAG_FLUSH_SENDER;

    ret = dspqueue_write(queue, 0, 2, bufs, sizeof(msg), (uint8_t *)msg, WRITE_TIMEOUT_US);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret, "dspqueue_write");

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, wait_done(&c, 10), "timeout");
    TEST_ASSERT_FALSE_MESSAGE(c.failed, c.err);

    /* Verify output */
    TEST_ASSERT_TRUE_MESSAGE(verify_byte_square(buffers[0], buffers[1], buf_size),
                             "byte-square output mismatch");

    fastrpc_test_dspqueue_stop(g_dspqueue_feature_state.dspqueue_rpc_handle, &process_time);
    g_dspqueue_feature_state.dsp_started = 0;
    ret = dspqueue_close(queue);
    (void)ret;
    sem_destroy(&c.done_sem);

    for (i = 0; i < num_bufs; i++) {
        ret = fastrpc_munmap(g_test_config.domain_id, fds[i], NULL, 0);
        (void)ret;
        rpcmem_free(buffers[i]);
    }
}
TEST_CASE_TAGS(DspQueueDataProcessingFlow, ProcessSingle, "DspQueue", "feature", "positive",
               "dspqueue_data_processing");

/*
 * ProcessMultiple - NUM_OPS byte-square requests with buffer reuse.
 * Mirrors process_test(..., num_ops=10, test=1).
 */
#define NUM_OPS 10
#define NUM_BUFS 4 /* 2 input + 2 output, rotated */

TEST(DspQueueDataProcessingFlow, ProcessMultiple)
{
    const size_t buf_size = 4 * 1024;
    void *buffers[NUM_BUFS];
    int fds[NUM_BUFS];
    dspqueue_t queue = NULL;
    uint64_t dsp_queue_id, process_time;
    struct proc_ctx c;
    int ret, i;

    memset(&c, 0, sizeof(c));
    c.num_ops = NUM_OPS;
    sem_init(&c.done_sem, 0, 0);

    for (i = 0; i < NUM_BUFS; i++) {
        buffers[i] = rpcmem_alloc(RPCMEM_HEAP_ID_SYSTEM, RPCMEM_DEFAULT_FLAGS | RPCMEM_HEAP_NOREG,
                                  buf_size);
        TEST_ASSERT_NOT_NULL(buffers[i]);
        fds[i] = rpcmem_to_fd(buffers[i]);
        TEST_ASSERT_GREATER_THAN(0, fds[i]);
        ret = fastrpc_mmap(g_test_config.domain_id, fds[i], buffers[i], 0, buf_size,
                           FASTRPC_MAP_FD);
        TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, ret);
    }

    /* Fill input buffers (first half) */
    for (i = 0; i < NUM_BUFS / 2; i++) {
        uint8_t *p = buffers[i];
        uint8_t v = (uint8_t)fds[i];
        for (size_t j = 0; j < buf_size; j++)
            *p++ = v++;
    }

    ret = dspqueue_create(g_test_config.domain_id, 0, QUEUE_SIZE, QUEUE_SIZE, packet_cb, error_cb,
                          &c, &queue);
    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, ret);

    ret = dspqueue_export(queue, &dsp_queue_id);
    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, ret);

    ret = fastrpc_test_dspqueue_start(g_dspqueue_feature_state.dspqueue_rpc_handle, dsp_queue_id);
    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, ret);
    g_dspqueue_feature_state.dsp_started = 1;

    for (i = 0; i < NUM_OPS; i++) {
        int in_idx = i % (NUM_BUFS / 2);
        int out_idx = in_idx + (NUM_BUFS / 2);
        uint32_t msg[2] = { MSG_BYTE_SQUARE, 0 };
        struct dspqueue_buffer bufs[2];
        memset(bufs, 0, sizeof(bufs));
        bufs[0].fd = fds[in_idx];
        bufs[0].flags = DSPQUEUE_BUFFER_FLAG_REF | DSPQUEUE_BUFFER_FLAG_FLUSH_SENDER
                        | DSPQUEUE_BUFFER_FLAG_INVALIDATE_RECIPIENT;
        bufs[1].fd = fds[out_idx];
        bufs[1].flags = DSPQUEUE_BUFFER_FLAG_REF | DSPQUEUE_BUFFER_FLAG_FLUSH_SENDER;
        ret = dspqueue_write(queue, 0, 2, bufs, sizeof(msg), (uint8_t *)msg, WRITE_TIMEOUT_US);
        TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, ret);
    }

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, wait_done(&c, 30), "timeout");
    TEST_ASSERT_FALSE_MESSAGE(c.failed, c.err);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(NUM_OPS, c.num_squares, "response count");

    fastrpc_test_dspqueue_stop(g_dspqueue_feature_state.dspqueue_rpc_handle, &process_time);
    g_dspqueue_feature_state.dsp_started = 0;
    ret = dspqueue_close(queue);
    (void)ret;
    sem_destroy(&c.done_sem);

    for (i = 0; i < NUM_BUFS; i++) {
        ret = fastrpc_munmap(g_test_config.domain_id, fds[i], NULL, 0);
        (void)ret;
        rpcmem_free(buffers[i]);
    }
}
TEST_CASE_TAGS(DspQueueDataProcessingFlow, ProcessMultiple, "DspQueue", "feature", "positive",
               "dspqueue_data_processing");

/* ------------------------------------------------------------------ */

TEST_GROUP_RUNNER(DspQueueDataProcessingFlow)
{
    RUN_TEST_CASE(DspQueueDataProcessingFlow, ProcessSingle);
    RUN_TEST_CASE(DspQueueDataProcessingFlow, ProcessMultiple);
}
