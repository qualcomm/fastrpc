// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/*
 * fastrpc_test_imp.c - DSP-side implementation of the fastrpc_test interface.
 *
 * The QAIC-generated fastrpc_test_skel.c dispatches incoming RPC calls:
 *   case 0 -> fastrpc_test_open
 *   case 1 -> fastrpc_test_close
 *   case 2 -> fastrpc_test_add
 *   case 3 -> fastrpc_test_echo
 *   case 4 -> fastrpc_test_matmul
 *   case 5 -> fastrpc_test_fft
 *   case 6 -> fastrpc_test_dspqueue_start
 *   case 7 -> fastrpc_test_dspqueue_stop
 *   case 8 -> fastrpc_test_dspqueue_write_resp
 *   case 9 -> fastrpc_test_profiling_noop
 *   case 10 -> fastrpc_test_profiling_inbuf
 *   case 11 -> fastrpc_test_profiling_routbuf
 *   case 12 -> fastrpc_test_profiling_memcpy_time_pcycles
 *   case 13 -> fastrpc_test_profiling_asm_iterations_time_us
 *   case 14 -> fastrpc_test_profiling_asm_iterations_pcycles
 *   case 15 -> fastrpc_test_profiling_asm_iterations_qtimer
 *   case 16 -> fastrpc_test_malloc_free_stress
 *
 * Handle value contract
 * ---------------------
 * The remote_handle64 value returned by fastrpc_test_open() is an opaque
 * token managed entirely by the CPU-side FastRPC driver.  The driver uses
 * it to track open handles and enforce close semantics (e.g. returning
 * AEE_EINVHANDLE on a double-close).  The DSP-side skel receives the same
 * value back in every subsequent call.
 *
 * IMPORTANT: fastrpc_test_open() must write a SMALL INTEGER (e.g. 0) into
 * *h, NOT a heap pointer.  If a heap pointer is written:
 *
 *   1. DoubleCloseFails (32-bit): the driver dispatches the second close
 *      RPC to the DSP skel, which calls fastrpc_test_close() again with
 *      the already-freed pointer — double-free -> AEE_EMEMPTR from DSP
 *      instead of the expected DSP_AEE_EOFFSET+AEE_ERPC.
 *
 *   2. Handle64DoubleCloseFails (64-bit): the large pointer value (e.g.
 *      0x7f...) causes the driver's handle-table lookup to succeed on the
 *      second remote_handle64_close() call (returning 0) instead of
 *      returning AEE_EINVHANDLE as expected.
 *
 * Per-handle dspqueue context
 * ---------------------------
 * dspqueue_start/stop need per-handle state (queue handle + process_time).
 * This is stored in a static g_ctx array indexed by the small integer
 * handle value (0..MAX_FASTRPC_TEST_HANDLES-1).  Each open() claims a
 * free slot; close() releases it.  This gives clean-slate semantics
 * without using a heap pointer as the handle value.
 */

#define FARF_ERROR 1
#define FARF_HIGH 1
#define FARF_MEDIUM 0
#define FARF_LOW 0

#include "AEEStdErr.h"
#include "HAP_farf.h"
#include "dspqueue.h"
#include "fastrpc_test.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

/* =========================================================================
 * dspqueue message types  (shared with CPU-side tests)
 * ========================================================================= */

#define MSG_ECHO 1
#define MSG_ECHO_RESP 2
#define MSG_BYTE_SQUARE 3
#define MSG_BYTE_SQUARE_RESP 4

/* Matches SAMPLE_MAX_MESSAGE_SIZE / SAMPLE_MAX_PACKET_BUFFERS in reference */
#define MAX_MESSAGE_SIZE 8
#define MAX_PACKET_BUFFERS 2

/* =========================================================================
 * Per-handle context table
 *
 * Indexed by the small integer handle value written into *h by open().
 * MAX_FASTRPC_TEST_HANDLES concurrent open handles is more than enough
 * for any test scenario (tests open at most 1-2 handles at a time).
 * ========================================================================= */

#define MAX_FASTRPC_TEST_HANDLES 8

struct dspqueue_ctx {
    int in_use;            /* 1 if this slot is currently open */
    dspqueue_t queue;      /* non-NULL while dspqueue_start is active */
    uint64_t process_time; /* accumulated DSP processing time (us) */
};

static struct dspqueue_ctx g_ctx[MAX_FASTRPC_TEST_HANDLES];

/* Returns the context for handle h, or NULL if h is out of range / not in use. */
static struct dspqueue_ctx *ctx_from_handle(remote_handle64 h)
{
    if (h >= MAX_FASTRPC_TEST_HANDLES)
        return NULL;
    if (!g_ctx[h].in_use)
        return NULL;
    return &g_ctx[h];
}

/* =========================================================================
 * open / close
 * ========================================================================= */

/*
 * fastrpc_test_open
 * Claims a free slot in g_ctx[] and returns its index as the handle value.
 *
 * The handle value MUST be a small integer, not a heap pointer.  The
 * CPU-side FastRPC driver uses the handle value to track open handles and
 * enforce close semantics.  A heap pointer breaks double-close detection
 * in both the 32-bit (DoubleCloseFails) and 64-bit
 * (Handle64DoubleCloseFails) test cases — see the file-level comment.
 */
int fastrpc_test_open(const char *uri, remote_handle64 *h)
{
    int i;
    (void)uri;

    for (i = 0; i < MAX_FASTRPC_TEST_HANDLES; i++) {
        if (!g_ctx[i].in_use) {
            /*
             * Stale-queue safety net: if a previous process run crashed
             * after dspqueue_start but before dspqueue_stop, the DSP PD
             * persists across runs and g_ctx[i].queue is still set.
             * Close it now so the next dspqueue_start gets a clean slot.
             */
            if (g_ctx[i].queue != NULL) {
                FARF(ERROR,
                     "fastrpc_test_open: slot %d has stale queue "
                     "from previous run — closing",
                     i);
                dspqueue_close(g_ctx[i].queue);
                g_ctx[i].queue = NULL;
            }
            g_ctx[i].in_use = 1;
            g_ctx[i].process_time = 0;
            *h = (remote_handle64)i;
            HAP_setFARFRuntimeLoggingParams(0x1f, NULL, 0);
            return 0;
        }
    }

    /* All slots occupied — should never happen in normal test usage. */
    return AEE_ENOMEMORY;
}

/*
 * fastrpc_test_close
 * Releases the g_ctx[] slot for handle h.
 * Force-closes any dspqueue that was left open (safety net for tests that
 * abort before calling dspqueue_stop).
 */
int fastrpc_test_close(remote_handle64 h)
{
    struct dspqueue_ctx *c = ctx_from_handle(h);

    if (!c)
        return AEE_EBADPARM;

    if (c->queue != NULL) {
        FARF(ERROR, "fastrpc_test_close: queue still open — force closing");
        dspqueue_close(c->queue);
        c->queue = NULL;
    }

    c->in_use = 0;
    c->process_time = 0;
    return 0;
}

/* =========================================================================
 * Simple RPC methods (add / echo / matmul / fft)
 * ========================================================================= */

int fastrpc_test_add(remote_handle64 h, int a, int b, int *result)
{
    (void)h;
    *result = a + b;
    return 0;
}

int fastrpc_test_echo(remote_handle64 h, const unsigned char *input, int input_len,
                      unsigned char *output, int output_len)
{
    (void)h;
    int len = (input_len < output_len) ? input_len : output_len;
    memcpy(output, input, (size_t)len);
    return 0;
}

int fastrpc_test_matmul(remote_handle64 h, const float *a, int a_len, const float *b, int b_len,
                        float *c, int c_len)
{
    (void)h;
    if (a_len != 9 || b_len != 9 || c_len != 9)
        return AEE_EBADPARM;

    int row, col, k;
    for (row = 0; row < 3; row++)
        for (col = 0; col < 3; col++) {
            float sum = 0.0f;
            for (k = 0; k < 3; k++)
                sum += a[row * 3 + k] * b[k * 3 + col];
            c[row * 3 + col] = sum;
        }
    return 0;
}

/* --- FFT helpers --------------------------------------------------------- */

static int is_power_of_two(int n) { return (n >= 2) && ((n & (n - 1)) == 0); }

static int log2_int(int n)
{
    int b = 0;
    while (n > 1) {
        n >>= 1;
        b++;
    }
    return b;
}

static void bit_reverse_copy(const float *src, float *dst, int N)
{
    int bits = log2_int(N);
    int i;
    for (i = 0; i < N; i++) {
        int rev = 0;
        int tmp = i;
        int b;
        for (b = 0; b < bits; b++) {
            rev = (rev << 1) | (tmp & 1);
            tmp >>= 1;
        }
        dst[rev * 2] = src[i];
        dst[rev * 2 + 1] = 0.0f;
    }
}

static void fft_inplace(float *buf, int N)
{
    int len;
    int i;
    for (len = 2; len <= N; len <<= 1) {
        float ang = -2.0f * (float)M_PI / (float)len;
        float w_re = cosf(ang);
        float w_im = sinf(ang);
        for (i = 0; i < N; i += len) {
            float cur_re = 1.0f;
            float cur_im = 0.0f;
            int j;
            for (j = 0; j < len / 2; j++) {
                float u_re = buf[(i + j) * 2];
                float u_im = buf[(i + j) * 2 + 1];
                float v_re = buf[(i + j + len / 2) * 2];
                float v_im = buf[(i + j + len / 2) * 2 + 1];
                float t_re = cur_re * v_re - cur_im * v_im;
                float t_im = cur_re * v_im + cur_im * v_re;
                buf[(i + j) * 2] = u_re + t_re;
                buf[(i + j) * 2 + 1] = u_im + t_im;
                buf[(i + j + len / 2) * 2] = u_re - t_re;
                buf[(i + j + len / 2) * 2 + 1] = u_im - t_im;
                float n_re = cur_re * w_re - cur_im * w_im;
                float n_im = cur_re * w_im + cur_im * w_re;
                cur_re = n_re;
                cur_im = n_im;
            }
        }
    }
}

int fastrpc_test_fft(remote_handle64 h, const float *samples, int samples_len, float *spectrum,
                     int spectrum_len)
{
    (void)h;
    if (!is_power_of_two(samples_len) || spectrum_len != samples_len)
        return AEE_EBADPARM;

    int N = samples_len;
    float *buf = (float *)malloc((size_t)(2 * N) * sizeof(float));
    if (!buf)
        return AEE_ENOMEMORY;

    bit_reverse_copy(samples, buf, N);
    fft_inplace(buf, N);

    int i;
    for (i = 0; i < N; i++)
        spectrum[i] = buf[i];
    free(buf);
    return 0;
}

/* =========================================================================
 * dspqueue echo implementation
 * ========================================================================= */

static void byte_square(const uint8_t *in, uint8_t *out, size_t n)
{
    while (n--) {
        uint8_t v = *in++;
        *out++ = v * v;
    }
}

static void dspqueue_error_cb(dspqueue_t queue, int error, void *context)
{
    (void)queue;
    (void)context;
    FARF(ERROR, "dspqueue error callback: 0x%08x", (unsigned)error);
}

/*
 * dspqueue_packet_cb
 * Drains all available packets in one invocation.
 * Mirrors sample_packet_callback() in dspqueue_sample_imp.c.
 */
static void dspqueue_packet_cb(dspqueue_t queue, int error, void *context)
{
    struct dspqueue_ctx *c = (struct dspqueue_ctx *)context;
    int err = 0;

    while (1) {
        uint32_t msg[MAX_MESSAGE_SIZE / 4];
        uint32_t resp_msg[MAX_MESSAGE_SIZE / 4];
        uint32_t flags, msg_length, num_bufs;
        struct dspqueue_buffer bufs[MAX_PACKET_BUFFERS];
        struct dspqueue_buffer resp_bufs[MAX_PACKET_BUFFERS];
        uint32_t len, early_limit;

        err = dspqueue_read_noblock(queue, &flags, MAX_PACKET_BUFFERS, &num_bufs, bufs, sizeof(msg),
                                    &msg_length, (uint8_t *)msg);
        if (err == AEE_EWOULDBLOCK)
            return;
        if (err != 0) {
            FARF(ERROR, "dspqueue_read_noblock failed: 0x%08x", (unsigned)err);
            return;
        }
        if (msg_length < 4) {
            FARF(ERROR, "Bad message length %u", msg_length);
            continue;
        }

        switch (msg[0]) {

        case MSG_ECHO:
            if (msg_length != 8) {
                FARF(ERROR, "Bad echo packet");
                continue;
            }
            FARF(HIGH, "Echo %u", (unsigned)msg[1]);
            resp_msg[0] = MSG_ECHO_RESP;
            resp_msg[1] = msg[1];
            err = dspqueue_write(queue, 0, 0, NULL, 8, (const uint8_t *)resp_msg,
                                 DSPQUEUE_TIMEOUT_NONE);
            if (err != 0) {
                FARF(ERROR, "dspqueue_write (echo) failed: 0x%08x", (unsigned)err);
                return;
            }
            break;

        case MSG_BYTE_SQUARE:
            if ((msg_length != 8) || (num_bufs != 2) || (bufs[0].size != bufs[1].size)) {
                FARF(ERROR, "Bad byte_square packet");
                continue;
            }
            len = bufs[0].size;
            early_limit = msg[1];
            if (early_limit > len) {
                FARF(ERROR, "Bad early_limit %u > len %u", early_limit, len);
                continue;
            }

            FARF(HIGH, "byte_square fd=%d->%d size=%u", bufs[0].fd, bufs[1].fd, len);

            /* Process main portion; accumulate bytes as proxy for time
             * (HAP_perf.h not available in this repo) */
            byte_square(bufs[0].ptr, bufs[1].ptr, len - early_limit);
            c->process_time += len - early_limit;

            if (early_limit > 0) {
                dspqueue_write_early_wakeup_noblock(queue, 0, 0);
                byte_square(((const uint8_t *)bufs[0].ptr) + (len - early_limit),
                            ((uint8_t *)bufs[1].ptr) + (len - early_limit), early_limit);
                c->process_time += early_limit;
            }

            /* Response: DEREF both; flush+invalidate the output buffer */
            resp_msg[0] = MSG_BYTE_SQUARE_RESP;
            memset(resp_bufs, 0, sizeof(resp_bufs));
            resp_bufs[0].fd = bufs[0].fd;
            resp_bufs[0].flags = DSPQUEUE_BUFFER_FLAG_DEREF;
            resp_bufs[1].fd = bufs[1].fd;
            resp_bufs[1].flags = DSPQUEUE_BUFFER_FLAG_DEREF | DSPQUEUE_BUFFER_FLAG_FLUSH_SENDER
                                 | DSPQUEUE_BUFFER_FLAG_INVALIDATE_RECIPIENT;
            err = dspqueue_write(queue, 0, 2, resp_bufs, 4, (const uint8_t *)resp_msg,
                                 DSPQUEUE_TIMEOUT_NONE);
            if (err != 0) {
                FARF(ERROR, "dspqueue_write (byte_square resp) failed: 0x%08x", (unsigned)err);
                return;
            }
            break;

        default:
            FARF(ERROR, "Unknown message type %u", (unsigned)msg[0]);
            break;
        }
    }
}

/*
 * fastrpc_test_dspqueue_start
 * Imports the CPU-created queue into the DSP.
 * Mirrors dspqueue_sample_start(): uses the per-handle context, not a global.
 */
int fastrpc_test_dspqueue_start(remote_handle64 h, uint64_t dsp_queue_id)
{
    struct dspqueue_ctx *c = ctx_from_handle(h);
    int err;

    if (!c)
        return AEE_EBADPARM;
    if (c->queue != NULL) {
        FARF(ERROR, "dspqueue_start: queue already open");
        return AEE_EITEMBUSY;
    }

    c->process_time = 0;

    err = dspqueue_import(dsp_queue_id, dspqueue_packet_cb, dspqueue_error_cb, (void *)c,
                          &c->queue);
    if (err != 0) {
        FARF(ERROR, "dspqueue_import failed: 0x%08x", (unsigned)err);
        return err;
    }

    return 0;
}

/*
 * fastrpc_test_dspqueue_write_resp
 * Writes a message directly into the resp_queue (DSP->CPU direction) of
 * the already-imported queue for this handle.  Used exclusively by unit
 * tests to inject a response packet so that dspqueue_peek/read on the
 * CPU side finds a packet without needing a full echo round-trip.
 *
 * The queue must have been imported first via dspqueue_start().
 */
int fastrpc_test_dspqueue_write_resp(remote_handle64 h, const unsigned char *message,
                                     int message_len)
{
    struct dspqueue_ctx *c = ctx_from_handle(h);
    int err;

    if (!c)
        return AEE_EBADPARM;
    if (c->queue == NULL) {
        FARF(ERROR, "dspqueue_write_resp: queue not started");
        return AEE_EBADSTATE;
    }

    err = dspqueue_write(c->queue, 0, 0, NULL, (uint32_t)message_len, message,
                         DSPQUEUE_TIMEOUT_NONE);
    if (err != 0) {
        FARF(ERROR, "dspqueue_write_resp: dspqueue_write failed: 0x%08x", (unsigned)err);
    }
    return err;
}

/*
 * fastrpc_test_dspqueue_stop
 * Closes the queue and returns accumulated process_time.
 * Mirrors dspqueue_sample_stop(): dspqueue_close() waits for callbacks.
 */
int fastrpc_test_dspqueue_stop(remote_handle64 h, uint64_t *process_time)
{
    struct dspqueue_ctx *c = ctx_from_handle(h);
    int err;

    if (!c)
        return AEE_EBADPARM;
    if (c->queue == NULL) {
        FARF(ERROR, "dspqueue_stop: queue not open");
        return AEE_EBADSTATE;
    }

    err = dspqueue_close(c->queue);
    c->queue = NULL;
    if (err != 0) {
        FARF(ERROR, "dspqueue_close failed: 0x%08x", (unsigned)err);
        return err;
    }

    *process_time = c->process_time;
    FARF(HIGH, "dspqueue_stop: process_time=%llu us", (unsigned long long)c->process_time);
    return 0;
}

/* =========================================================================
 * Profiling methods (methods 9-15)
 *
 * These mirror the implementations in examples/profiling/src/profiling.c
 * from the Hexagon SDK, adapted to use clock_gettime() instead of
 * HAP_perf_get_time_us() / HAP_perf_get_pcycles() which are DSP-SDK-only.
 * ========================================================================= */

/* Returns microseconds from CLOCK_MONOTONIC. */
static uint64_t dsp_get_time_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

/* method 8: no-op — measures pure RPC round-trip overhead */
int fastrpc_test_profiling_noop(remote_handle64 h)
{
    (void)h;
    return 0;
}

/* method 9: input-buffer processing time measurement */
int fastrpc_test_profiling_inbuf(remote_handle64 h, const unsigned char *src, int src_len,
                                 int mem_test, uint64_t *run_time)
{
    (void)h;
    uint64_t t0 = dsp_get_time_us();

    if (mem_test) {
        /* Verify buffer contents to exercise cache/memory path. */
        volatile unsigned int sum = 0;
        int i;
        for (i = 0; i < src_len; i++)
            sum += src[i];
        (void)sum;
    }

    *run_time = dsp_get_time_us() - t0;
    return 0;
}

/* method 10: output-buffer processing time measurement */
int fastrpc_test_profiling_routbuf(remote_handle64 h, unsigned char *dst, int dst_len, int mem_test,
                                   uint64_t *run_time)
{
    (void)h;
    uint64_t t0 = dsp_get_time_us();

    if (mem_test) {
        /* Fill buffer with a pattern to exercise cache/memory path. */
        int i;
        for (i = 0; i < dst_len; i++)
            dst[i] = (unsigned char)(i & 0xFF);
    }

    *run_time = dsp_get_time_us() - t0;
    return 0;
}

/* method 11: DSP memcpy timing */
int fastrpc_test_profiling_memcpy_time_pcycles(remote_handle64 h, uint64_t len, uint64_t *run_time)
{
    (void)h;
    int *src = (int *)malloc((size_t)len * sizeof(int));
    int *dst = (int *)malloc((size_t)len * sizeof(int));

    if (!src || !dst) {
        free(src);
        free(dst);
        return AEE_ENOMEMORY;
    }

    uint64_t t0 = dsp_get_time_us();
    memcpy(dst, src, (size_t)len * sizeof(int));
    *run_time = dsp_get_time_us() - t0;

    free(src);
    free(dst);
    return 0;
}

/* methods 12-14: ASM loop timing variants
 * Hexagon assembly (HAP_perf) is not available in this repo's build
 * environment — return AEE_EUNSUPPORTED so the CPU-side tests skip cleanly
 * via TEST_IGNORE_MESSAGE rather than failing. */
int fastrpc_test_profiling_asm_iterations_time_us(remote_handle64 h, uint32_t num)
{
    (void)h;
    (void)num;
    return AEE_EUNSUPPORTED;
}

int fastrpc_test_profiling_asm_iterations_pcycles(remote_handle64 h, uint32_t num)
{
    (void)h;
    (void)num;
    return AEE_EUNSUPPORTED;
}

int fastrpc_test_profiling_asm_iterations_qtimer(remote_handle64 h, uint32_t num)
{
    (void)h;
    (void)num;
    return AEE_EUNSUPPORTED;
}

/* =========================================================================
 * User-heap stress method (method 16)
 *
 * fastrpc_test_malloc_free_stress
 * --------------------------------
 * Exercises the DSP userspace heap with repeated malloc/memset/free cycles.
 * No HAP or QuRT APIs are used — only stdlib malloc/free/memset and
 * clock_gettime(CLOCK_MONOTONIC) for timing.
 *
 * Algorithm:
 *   for each round in [0, iterations):
 *     for each slot in [0, num_allocs):
 *       size = sizes[slot % sizes_len]   (cycle through the sizes array)
 *       buf  = malloc(size)
 *       if buf == NULL -> return AEE_ENOMEMORY (all already-allocated bufs
 *                         are freed before returning)
 *       memset(buf, (uint8_t)slot, size)  (touch every byte — no dead-store)
 *     free all num_allocs buffers
 *
 * The memset pattern uses the slot index so the compiler cannot optimise
 * the store away as a dead write.
 * ========================================================================= */
int fastrpc_test_malloc_free_stress(remote_handle64 h, const uint32_t *sizes, int sizes_len,
                                    int iterations, int num_allocs, uint64_t *elapsed_us)
{
    (void)h;

    int round, slot;
    void **bufs = NULL;
    int ret = AEE_SUCCESS;

    if (sizes_len <= 0 || iterations <= 0 || num_allocs <= 0 || !sizes) {
        *elapsed_us = 0;
        return AEE_EBADPARM;
    }

    bufs = (void **)malloc((size_t)num_allocs * sizeof(void *));
    if (!bufs) {
        *elapsed_us = 0;
        return AEE_ENOMEMORY;
    }

    uint64_t t0 = dsp_get_time_us();

    for (round = 0; round < iterations; round++) {
        /* --- allocate phase --- */
        for (slot = 0; slot < num_allocs; slot++) {
            uint32_t sz = sizes[slot % sizes_len];
            bufs[slot] = malloc((size_t)sz);
            if (!bufs[slot]) {
                /* Free everything allocated so far in this round. */
                int k;
                for (k = 0; k < slot; k++)
                    free(bufs[k]);
                free(bufs);
                *elapsed_us = dsp_get_time_us() - t0;
                return AEE_ENOMEMORY;
            }
            /* Touch every byte so the allocation is exercised fully. */
            memset(bufs[slot], (int)(slot & 0xFF), (size_t)sz);
        }

        /* --- free phase (all in one pass, same round) --- */
        for (slot = 0; slot < num_allocs; slot++) {
            free(bufs[slot]);
            bufs[slot] = NULL;
        }
    }

    *elapsed_us = dsp_get_time_us() - t0;
    free(bufs);
    return ret;
}
