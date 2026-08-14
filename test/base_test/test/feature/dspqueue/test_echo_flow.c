// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/*
 * test_echo_flow.c - dspqueue echo round-trip tests
 *
 * Mirrors examples/dspqueue/src/dspqueue_sample.c::echo_test().
 * The CPU creates a queue, exports it, starts the DSP-side service via
 * fastrpc_test_dspqueue_start(), sends echo messages, waits for responses
 * in a packet callback, then stops the DSP service and closes the queue.
 */

#include "AEEStdErr.h"
#include "dspqueue.h"
#include "dspqueue_feature_utils.h"
#include "test_utils.h"
#include "unity_fixture.h"

#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Message types - must match fastrpc_test_imp.c */
#define MSG_ECHO 1
#define MSG_ECHO_RESP 2

#define ECHO_QUEUE_SIZE 256
#define WRITE_TIMEOUT_US 1000000

struct echo_ctx {
    uint32_t next;       /* next expected echo value */
    uint32_t done_value; /* sentinel that signals end */
    sem_t done_sem;
    int failed;
    char err[128];
};

static void packet_cb(dspqueue_t queue, AEEResult error, void *context)
{
    struct echo_ctx *c = (struct echo_ctx *)context;
    uint32_t msg[2], flags, msg_len, num_bufs;

    while (1) {
        int ret = dspqueue_read_noblock(queue, &flags, 0, &num_bufs, NULL, sizeof(msg), &msg_len,
                                        (uint8_t *)msg);
        if (ret == AEE_EWOULDBLOCK)
            return;
        if (ret != AEE_SUCCESS) {
            c->failed = 1;
            snprintf(c->err, sizeof(c->err), "read_noblock: 0x%x", ret);
            sem_post(&c->done_sem);
            return;
        }
        if (msg_len != 8 || msg[0] != MSG_ECHO_RESP)
            continue;

        if (msg[1] == c->done_value) {
            sem_post(&c->done_sem);
        } else {
            if (msg[1] != c->next) {
                c->failed = 1;
                snprintf(c->err, sizeof(c->err), "out-of-order: got %u expected %u", msg[1],
                         c->next);
            }
            c->next++;
        }
    }
}

static void error_cb(dspqueue_t queue, AEEResult error, void *context)
{
    struct echo_ctx *c = (struct echo_ctx *)context;
    c->failed = 1;
    snprintf(c->err, sizeof(c->err), "error_cb: 0x%x", error);
    sem_post(&c->done_sem);
}

static int wait_done(struct echo_ctx *c, int timeout_sec)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_sec;
    return sem_timedwait(&c->done_sem, &ts);
}

/* ------------------------------------------------------------------ */

TEST_GROUP(DspQueueEchoFlow);
TEST_GROUP_META(DspQueueEchoFlow, "feature", "DSP Queue End-to-End Flows", "Echo Communication",
                "dspqueue echo round-trip (CPU->DSP->CPU)");

TEST_SETUP(DspQueueEchoFlow)
{
    int ret = dspqueue_feature_init(g_test_config.domain_id);
    if (ret != AEE_SUCCESS)
        TEST_IGNORE_MESSAGE("DSP session not available - skipping");
}

TEST_TEAR_DOWN(DspQueueEchoFlow) { dspqueue_feature_cleanup(); }

/* ------------------------------------------------------------------ */

/*
 * EchoSingle - send one echo message and wait for the response.
 * Mirrors echo_test() with a single message.
 * The echo value starts at 0 to match the c.next ordering check in packet_cb.
 */
TEST(DspQueueEchoFlow, EchoSingle)
{
    dspqueue_t queue = NULL;
    uint64_t dsp_queue_id;
    uint64_t process_time;
    struct echo_ctx c;
    uint32_t msg[2];
    int ret;

    memset(&c, 0, sizeof(c));
    c.done_value = 0xffffffff;
    sem_init(&c.done_sem, 0, 0);

    ret = dspqueue_create(g_test_config.domain_id, 0, ECHO_QUEUE_SIZE, ECHO_QUEUE_SIZE, packet_cb,
                          error_cb, &c, &queue);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret, "dspqueue_create");

    ret = dspqueue_export(queue, &dsp_queue_id);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret, "dspqueue_export");

    ret = fastrpc_test_dspqueue_start(g_dspqueue_feature_state.dspqueue_rpc_handle, dsp_queue_id);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret, "dspqueue_start");
    g_dspqueue_feature_state.dsp_started = 1;

    /* Send one echo (value=0 matches c.next=0) then the done sentinel */
    msg[0] = MSG_ECHO;
    msg[1] = 0;
    ret = dspqueue_write(queue, 0, 0, NULL, sizeof(msg), (uint8_t *)msg, WRITE_TIMEOUT_US);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret, "write echo");

    msg[0] = MSG_ECHO;
    msg[1] = c.done_value;
    ret = dspqueue_write(queue, 0, 0, NULL, sizeof(msg), (uint8_t *)msg, WRITE_TIMEOUT_US);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret, "write done");

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, wait_done(&c, 5), "timeout waiting for echo");
    TEST_ASSERT_FALSE_MESSAGE(c.failed, c.err);

    fastrpc_test_dspqueue_stop(g_dspqueue_feature_state.dspqueue_rpc_handle, &process_time);
    g_dspqueue_feature_state.dsp_started = 0;
    {
        int _close_ret;
        _close_ret = dspqueue_close(queue);
        (void)_close_ret;
    }
    sem_destroy(&c.done_sem);
}
TEST_CASE_TAGS(DspQueueEchoFlow, EchoSingle, "DspQueue", "feature", "positive", "dspqueue_echo");

/*
 * EchoMultiple - send NUM_ECHOES messages and verify all come back in order.
 * Mirrors echo_test() with NUM_ECHOES iterations.
 */
#define NUM_ECHOES 20

TEST(DspQueueEchoFlow, EchoMultiple)
{
    dspqueue_t queue = NULL;
    uint64_t dsp_queue_id;
    uint64_t process_time;
    struct echo_ctx c;
    uint32_t msg[2];
    int ret, i;

    memset(&c, 0, sizeof(c));
    c.done_value = 0xffffffff;
    sem_init(&c.done_sem, 0, 0);

    ret = dspqueue_create(g_test_config.domain_id, 0, ECHO_QUEUE_SIZE, ECHO_QUEUE_SIZE, packet_cb,
                          error_cb, &c, &queue);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret, "dspqueue_create");

    ret = dspqueue_export(queue, &dsp_queue_id);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret, "dspqueue_export");

    ret = fastrpc_test_dspqueue_start(g_dspqueue_feature_state.dspqueue_rpc_handle, dsp_queue_id);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret, "dspqueue_start");
    g_dspqueue_feature_state.dsp_started = 1;

    for (i = 0; i <= NUM_ECHOES; i++) {
        msg[0] = MSG_ECHO;
        msg[1] = (i < NUM_ECHOES) ? (uint32_t)i : c.done_value;
        ret = dspqueue_write(queue, 0, 0, NULL, sizeof(msg), (uint8_t *)msg, WRITE_TIMEOUT_US);
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret, "dspqueue_write");
    }

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, wait_done(&c, 10), "timeout");
    TEST_ASSERT_FALSE_MESSAGE(c.failed, c.err);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(NUM_ECHOES, c.next, "response count");

    fastrpc_test_dspqueue_stop(g_dspqueue_feature_state.dspqueue_rpc_handle, &process_time);
    g_dspqueue_feature_state.dsp_started = 0;
    {
        int _close_ret;
        _close_ret = dspqueue_close(queue);
        (void)_close_ret;
    }
    sem_destroy(&c.done_sem);
}
TEST_CASE_TAGS(DspQueueEchoFlow, EchoMultiple, "DspQueue", "feature", "positive", "dspqueue_echo");

/* ------------------------------------------------------------------ */

TEST_GROUP_RUNNER(DspQueueEchoFlow)
{
    RUN_TEST_CASE(DspQueueEchoFlow, EchoSingle);
    RUN_TEST_CASE(DspQueueEchoFlow, EchoMultiple);
}
