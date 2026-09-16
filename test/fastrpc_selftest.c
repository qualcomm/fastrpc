// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause

/*
 * fastrpc_selftest — Comprehensive hardware-independent self-test.
 *
 * Exercises every symbol exported by libcdsprpc.so (symbols.lst) without
 * requiring functional DSP hardware.  Tests are grouped as follows:
 *
 *   A  Compile-time constants and macro arithmetic (always pass)
 *   B  Handle lifecycle: open / close graceful failure without hardware
 *   C  Handle invoke with invalid handles (VERIFYC guard → no crash)
 *   D  Control APIs: handle_control, session_control
 *   E  Async job APIs: get_status / release_job with invalid jobid
 *   F  rpcmem: init, alloc, alloc2, to_fd, free, deinit
 *   G  fastrpc_mmap / munmap graceful failure
 *   H  remote_register_buf / buf_attr / buf_attr2 (deregister, no-op)
 *   I  remote_register_fd / fd2 (invalid fd → (void*)-1)
 *   J  remote_register_dma_handle / dma_handle_attr
 *   K  remote_set_mode
 *   L  Deprecated mapping APIs (remote_mmap/munmap, mmap64/munmap64, mem_map/unmap)
 *   M  dspqueue: create (fails without hardware), close(NULL), request,
 *               write/read/peek/get_stat only run when hardware is present
 *   N  HAP_debug_v2 / HAP_debug_runtime (just call, no crash)
 *
 * Exit codes (automake-compatible):
 *   0  - all tests passed
 *   1  - one or more tests failed
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "remote.h"
#include "rpcmem.h"
#include "dspqueue.h"
#include "HAP_debug.h"
#include "AEEStdErr.h"

/* remote_set_mode is exported but has no public header declaration */
extern int remote_set_mode(uint32_t mode);

static int passed;
static int failed;

static void check(const char *desc, int ok)
{
    if (ok) {
        printf("  ok: %s\n", desc);
        passed++;
    } else {
        printf("  FAIL: %s\n", desc);
        failed++;
    }
}

/* Helper: API must return non-zero (error) without hardware */
static void check_err(const char *desc, int ret)
{
    check(desc, ret != 0);
}

/* -----------------------------------------------------------------------
 * A — Compile-time constants and macro arithmetic
 * --------------------------------------------------------------------- */

static void test_a_constants(void)
{
    printf("\n[A] Compile-time constants:\n");

    /* Domain IDs */
    check("ADSP_DOMAIN_ID == 0",  ADSP_DOMAIN_ID  == 0);
    check("MDSP_DOMAIN_ID == 1",  MDSP_DOMAIN_ID  == 1);
    check("SDSP_DOMAIN_ID == 2",  SDSP_DOMAIN_ID  == 2);
    check("CDSP_DOMAIN_ID == 3",  CDSP_DOMAIN_ID  == 3);
    check("CDSP1_DOMAIN_ID == 4", CDSP1_DOMAIN_ID == 4);

    /* REMOTE_SCALARS encode / decode */
    uint32_t sc = REMOTE_SCALARS_MAKE(7, 3, 2);
    check("REMOTE_SCALARS method round-trips",  REMOTE_SCALARS_METHOD(sc)  == 7);
    check("REMOTE_SCALARS inbufs round-trips",  REMOTE_SCALARS_INBUFS(sc)  == 3);
    check("REMOTE_SCALARS outbufs round-trips", REMOTE_SCALARS_OUTBUFS(sc) == 2);
    check("REMOTE_SCALARS_LENGTH is sum",       REMOTE_SCALARS_LENGTH(sc)  == 5);
    check("all-zero scalars encode to 0", REMOTE_SCALARS_MAKE(0, 0, 0) == 0);

    /* rpcmem heap IDs */
    check("RPCMEM_HEAP_ID_SECURE == 9",  RPCMEM_HEAP_ID_SECURE == 9);
    check("RPCMEM_HEAP_ID_CONTIG == 22", RPCMEM_HEAP_ID_CONTIG == 22);
    check("RPCMEM_HEAP_ID_SYSTEM == 25", RPCMEM_HEAP_ID_SYSTEM == 25);

    /* dspqueue packet flags are non-zero */
    check("DSPQUEUE_PACKET_FLAG_MESSAGE != 0", DSPQUEUE_PACKET_FLAG_MESSAGE != 0);
    check("DSPQUEUE_PACKET_FLAG_BUFFERS != 0", DSPQUEUE_PACKET_FLAG_BUFFERS != 0);

    /* fastrpc_map_flags are contiguous starting at 0 */
    check("FASTRPC_MAP_STATIC == 0", FASTRPC_MAP_STATIC == 0);
    check("FASTRPC_MAP_FD > FASTRPC_MAP_STATIC", FASTRPC_MAP_FD > FASTRPC_MAP_STATIC);
}

/* -----------------------------------------------------------------------
 * B — Handle lifecycle: graceful failure without hardware
 * --------------------------------------------------------------------- */

static void test_b_handle_lifecycle(void)
{
    printf("\n[B] Handle lifecycle (graceful failure):\n");

    remote_handle h = (remote_handle)-1;
    int ret = remote_handle_open("fastrpc_selftest_bogus_module", &h);
    if (ret == 0) {
        remote_handle_close(h);
        printf("  skip: DSP hardware present; open/close tested on real session\n");
    } else {
        check_err("remote_handle_open returns error without hardware", ret);
    }

    remote_handle64 h64 = (remote_handle64)-1;
    ret = remote_handle64_open("fastrpc_selftest_bogus_module", &h64);
    if (ret == 0) {
        remote_handle64_close(h64);
        printf("  skip: DSP hardware present; open64/close64 tested on real session\n");
    } else {
        check_err("remote_handle64_open returns error without hardware", ret);
    }

    /*
     * On a machine without an active DSP session the domain state is not
     * FASTRPC_DOMAIN_STATE_INIT, so is_process_exiting() returns true and
     * the fast-path "return 0" fires instead of AEE_EINVHANDLE.  Both 0
     * and non-zero are acceptable here; the critical property is no crash.
     */
    int ret_close = remote_handle_close((remote_handle)-1);
    printf("  ok: remote_handle_close(INVALID) returned %d (no crash)\n", ret_close);
    passed++;
    check_err("remote_handle64_close(INVALID) returns error",
              remote_handle64_close((remote_handle64)-1));
}

/* -----------------------------------------------------------------------
 * C — Handle invoke with invalid handles
 * --------------------------------------------------------------------- */

static void test_c_invoke(void)
{
    printf("\n[C] Handle invoke with invalid handles:\n");

    /* scalars=0 means no buffers → pra is never dereferenced */
    uint32_t sc0 = REMOTE_SCALARS_MAKE(0, 0, 0);

    /*
     * With INVALID handle and no active DSP session, is_process_exiting()
     * returns true (domain=-1 is invalid → bail → return true), triggering
     * the fast-path "return 0".  Verify no crash; don't require a specific
     * error code.
     */
    int ret_inv = remote_handle_invoke((remote_handle)-1, sc0, NULL);
    printf("  ok: remote_handle_invoke(INVALID) returned %d (no crash)\n", ret_inv);
    passed++;
    int ret_inv64 = remote_handle64_invoke((remote_handle64)-1, sc0, NULL);
    printf("  ok: remote_handle64_invoke(INVALID) returned %d (no crash)\n", ret_inv64);
    passed++;

    /*
     * remote_handle_invoke_async / remote_handle64_invoke_async are declared
     * in remote.h but are not exported from the library (not in symbols.lst).
     * They are intentionally omitted here.
     */
}

/* -----------------------------------------------------------------------
 * D — Control APIs
 * --------------------------------------------------------------------- */

static void test_d_control(void)
{
    printf("\n[D] Control APIs:\n");

    /*
     * DSPRPC_GET_DSP_INFO does not require an open handle; it queries the
     * kernel driver.  On a machine without /dev/fastrpc-* the call fails
     * gracefully with an ENODEV-like error.
     */
    fastrpc_capability cap = {
        .domain = CDSP_DOMAIN_ID,
        .attribute_ID = DOMAIN_SUPPORT,
        .capability = 0,
    };
    int ret = remote_handle_control(DSPRPC_GET_DSP_INFO, &cap, sizeof(cap));
    if (ret == 0) {
        printf("  skip: DSPRPC_GET_DSP_INFO succeeded (hardware or driver present)\n");
        printf("        DOMAIN_SUPPORT capability = %u\n", cap.capability);
    } else {
        check_err("remote_handle_control(GET_DSP_INFO) returns error without driver", ret);
    }

    /* DSPRPC_GET_DOMAIN on INVALID_HANDLE returns an error */
    remote_rpc_get_domain_t dom = {.domain = -1};
    check_err("remote_handle64_control(INVALID,GET_DOMAIN) returns error",
              remote_handle64_control((remote_handle64)-1, DSPRPC_GET_DOMAIN,
                                      &dom, sizeof(dom)));

    /* FASTRPC_THREAD_PARAMS: configure thread parameters for future sessions */
    struct remote_rpc_thread_params tp = {
        .domain = CDSP_DOMAIN_ID,
        .prio = -1,         /* use default */
        .stack_size = -1,   /* use default */
    };
    ret = remote_session_control(FASTRPC_THREAD_PARAMS, &tp, sizeof(tp));
    if (ret == 0) {
        printf("  ok: remote_session_control(THREAD_PARAMS) accepted parameters\n");
        passed++;
    } else {
        printf("  skip: remote_session_control(THREAD_PARAMS) returned 0x%x "
               "(no session open yet)\n", ret);
    }
}

/* -----------------------------------------------------------------------
 * E — Async job APIs with invalid jobid
 * --------------------------------------------------------------------- */

static void test_e_async(void)
{
    printf("\n[E] Async job APIs (invalid jobid):\n");

    int result = 0;
    check_err("fastrpc_async_get_status(jobid=0) returns error",
              fastrpc_async_get_status((fastrpc_async_jobid)0, 0, &result));
    check_err("fastrpc_release_async_job(jobid=0) returns error",
              fastrpc_release_async_job((fastrpc_async_jobid)0));
}

/* -----------------------------------------------------------------------
 * F — rpcmem lifecycle
 * --------------------------------------------------------------------- */

static void test_f_rpcmem(void)
{
    printf("\n[F] rpcmem lifecycle:\n");

    /* init / deinit are void — just verify no crash */
    rpcmem_init();
    printf("  ok: rpcmem_init() returned\n");
    passed++;

    /* rpcmem_free(NULL) is documented to ignore invalid buffers */
    rpcmem_free(NULL);
    printf("  ok: rpcmem_free(NULL) returned\n");
    passed++;

    /* Attempt allocation — may succeed (with ION/DMA-heap) or fail (no kernel support) */
    void *buf = rpcmem_alloc(RPCMEM_HEAP_ID_SYSTEM, RPCMEM_DEFAULT_FLAGS, 4096);
    if (buf) {
        int fd = rpcmem_to_fd(buf);
        check("rpcmem_to_fd returns non-negative for valid buffer", fd >= 0);
        rpcmem_free(buf);
        printf("  ok: rpcmem_alloc → rpcmem_to_fd → rpcmem_free cycle passed\n");
        passed++;
    } else {
        printf("  skip: rpcmem_alloc returned NULL (no ION/DMA-heap support)\n");
        /*
         * rpcmem_to_fd walks the internal allocation list looking for a
         * matching pointer.  NULL won't match any entry so it safely
         * returns -1 — exercise the symbol even without a real allocation.
         */
        check("rpcmem_to_fd(NULL) returns -1 (unregistered pointer)",
              rpcmem_to_fd(NULL) == -1);
    }

    void *buf2 = rpcmem_alloc2(RPCMEM_HEAP_ID_SYSTEM, RPCMEM_DEFAULT_FLAGS, 4096);
    if (buf2) {
        rpcmem_free(buf2);
        printf("  ok: rpcmem_alloc2 succeeded\n");
        passed++;
    } else {
        printf("  skip: rpcmem_alloc2 returned NULL\n");
    }

    void *bufd = rpcmem_alloc_def(4096);
    if (bufd) {
        rpcmem_free(bufd);
        printf("  ok: rpcmem_alloc_def succeeded\n");
        passed++;
    } else {
        printf("  skip: rpcmem_alloc_def returned NULL\n");
    }

    rpcmem_deinit();
    printf("  ok: rpcmem_deinit() returned\n");
    passed++;
}

/* -----------------------------------------------------------------------
 * G — fastrpc_mmap / munmap graceful failure
 * --------------------------------------------------------------------- */

static void test_g_mmap(void)
{
    printf("\n[G] fastrpc_mmap / munmap (graceful failure):\n");

    check_err("fastrpc_mmap(invalid fd) returns error",
              fastrpc_mmap(CDSP_DOMAIN_ID, -1, NULL, 0, 0, FASTRPC_MAP_FD));
    check_err("fastrpc_munmap(invalid fd) returns error",
              fastrpc_munmap(CDSP_DOMAIN_ID, -1, NULL, 0));
}

/* -----------------------------------------------------------------------
 * H — remote_register_buf / buf_attr / buf_attr2
 * --------------------------------------------------------------------- */

static void test_h_register_buf(void)
{
    printf("\n[H] remote_register_buf / buf_attr / buf_attr2:\n");

    /*
     * Deregister (fd=-1) a stack buffer that was never registered.
     * Per the API contract these are void and must not crash.
     */
    char dummy[64];
    remote_register_buf(dummy, (int)sizeof(dummy), -1);
    printf("  ok: remote_register_buf(deregister) returned\n");
    passed++;

    remote_register_buf_attr(dummy, (int)sizeof(dummy), -1, FASTRPC_ATTR_NONE);
    printf("  ok: remote_register_buf_attr(deregister) returned\n");
    passed++;

    remote_register_buf_attr2(dummy, sizeof(dummy), -1, FASTRPC_ATTR_NONE);
    printf("  ok: remote_register_buf_attr2(deregister) returned\n");
    passed++;
}

/* -----------------------------------------------------------------------
 * I — remote_register_fd / fd2
 * --------------------------------------------------------------------- */

static void test_i_register_fd(void)
{
    printf("\n[I] remote_register_fd / fd2 (invalid fd):\n");

    /*
     * remote_register_fd_attr() initialises po=NULL and returns po on any
     * failure path — the header documents (void*)-1 but the implementation
     * returns NULL.  Accept either as evidence of a graceful failure.
     * If neither NULL nor (void*)-1, the call unexpectedly succeeded and
     * we must unregister to avoid leaking the mapping.
     */
    void *p = remote_register_fd(-1, 4096);
    if (p == NULL || p == (void *)-1) {
        printf("  ok: remote_register_fd(-1) failed gracefully (returned %s)\n",
               p == NULL ? "NULL" : "(void*)-1");
        passed++;
    } else {
        printf("  unexpected: remote_register_fd(-1) returned a mapping %p; unregistering\n", p);
        remote_register_buf(p, 4096, -1);
        check("remote_register_fd(-1) should not return a valid mapping", 0);
    }

    void *p2 = remote_register_fd2(-1, 4096);
    if (p2 == NULL || p2 == (void *)-1) {
        printf("  ok: remote_register_fd2(-1) failed gracefully (returned %s)\n",
               p2 == NULL ? "NULL" : "(void*)-1");
        passed++;
    } else {
        printf("  unexpected: remote_register_fd2(-1) returned a mapping %p; unregistering\n", p2);
        remote_register_buf(p2, 4096, -1);
        check("remote_register_fd2(-1) should not return a valid mapping", 0);
    }
}

/* -----------------------------------------------------------------------
 * J — remote_register_dma_handle / dma_handle_attr
 * --------------------------------------------------------------------- */

static void test_j_register_dma(void)
{
    printf("\n[J] remote_register_dma_handle / dma_handle_attr:\n");

    /*
     * fd=-1 triggers deregister path; with len=0 this is invalid input.
     * The API must return non-zero / set errno rather than crashing.
     */
    check_err("remote_register_dma_handle(-1, 0) returns error",
              remote_register_dma_handle(-1, 0));
    check_err("remote_register_dma_handle_attr(-1, 0, 0) returns error",
              remote_register_dma_handle_attr(-1, 0, 0));
}

/* -----------------------------------------------------------------------
 * K — remote_set_mode
 * --------------------------------------------------------------------- */

static void test_k_set_mode(void)
{
    printf("\n[K] remote_set_mode:\n");

    int ret = remote_set_mode(REMOTE_MODE_PARALLEL);
    if (ret == 0) {
        printf("  ok: remote_set_mode(PARALLEL) accepted\n");
        passed++;
    } else {
        printf("  skip: remote_set_mode returned 0x%x (no active session)\n", ret);
    }

    ret = remote_set_mode(REMOTE_MODE_SERIAL);
    if (ret == 0) {
        printf("  ok: remote_set_mode(SERIAL) accepted\n");
        passed++;
    } else {
        printf("  skip: remote_set_mode(SERIAL) returned 0x%x\n", ret);
    }
}

/* -----------------------------------------------------------------------
 * L — Deprecated mapping APIs
 * --------------------------------------------------------------------- */

static void test_l_deprecated_mmap(void)
{
    printf("\n[L] Deprecated mapping APIs (graceful failure):\n");

    uint32_t vout32 = 0;
    check_err("remote_mmap(fd=-1) returns error",
              remote_mmap(-1, 0, 0, 0, &vout32));
    check_err("remote_munmap(addr=0) returns error",
              remote_munmap(0, 0));

    uint64_t vout64 = 0;
    check_err("remote_mmap64(fd=-1) returns error",
              remote_mmap64(-1, 0, 0, 0, &vout64));
    check_err("remote_munmap64(addr=0) returns error",
              remote_munmap64(0, 0));

    uint64_t remote_addr = 0;
    check_err("remote_mem_map(fd=-1) returns error",
              remote_mem_map(CDSP_DOMAIN_ID, -1, 0, 0, 0, &remote_addr));
    check_err("remote_mem_unmap(addr=0) returns error",
              remote_mem_unmap(CDSP_DOMAIN_ID, 0, 0));
}

/* -----------------------------------------------------------------------
 * M — dspqueue
 * --------------------------------------------------------------------- */

static void test_m_dspqueue(void)
{
    printf("\n[M] dspqueue:\n");

    dspqueue_t q = NULL;
    int ret = dspqueue_create(CDSP_DOMAIN_ID, 0, 0, 0, NULL, NULL, NULL, &q);

    if (ret == 0 && q != NULL) {
        /* Hardware is present — test full queue lifecycle */
        printf("  ok: dspqueue_create succeeded (hardware present)\n");
        passed++;

        uint64_t qid = 0;
        check("dspqueue_export succeeds", dspqueue_export(q, &qid) == 0);

        uint32_t flags = 0, num_bufs = 0, msg_len = 0;
        /* Non-blocking read on empty queue must return AEE_EWOULDBLOCK */
        ret = dspqueue_read_noblock(q, &flags, 0, &num_bufs, NULL,
                                    0, &msg_len, NULL);
        check("dspqueue_read_noblock returns EWOULDBLOCK on empty queue",
              ret == AEE_EWOULDBLOCK);

        ret = dspqueue_peek_noblock(q, &flags, &num_bufs, &msg_len);
        check("dspqueue_peek_noblock returns EWOULDBLOCK on empty queue",
              ret == AEE_EWOULDBLOCK);

        /* Non-blocking write with no payload should succeed if queue not full */
        ret = dspqueue_write_noblock(q, DSPQUEUE_PACKET_FLAG_MESSAGE,
                                     0, NULL, 0, NULL);
        if (ret == 0) {
            printf("  ok: dspqueue_write_noblock (empty message) succeeded\n");
            passed++;
        } else {
            printf("  skip: dspqueue_write_noblock returned 0x%x\n", ret);
        }

        /* Early wakeup (non-blocking, always safe) */
        ret = dspqueue_write_early_wakeup_noblock(q, 0, 0);
        if (ret == 0 || ret == AEE_EWOULDBLOCK) {
            printf("  ok: dspqueue_write_early_wakeup_noblock returned\n");
            passed++;
        } else {
            check_err("dspqueue_write_early_wakeup_noblock", ret);
        }

        uint64_t stat_val = 0;
        ret = dspqueue_get_stat(q, DSPQUEUE_STAT_WRITE_QUEUE_PACKETS, &stat_val);
        check("dspqueue_get_stat succeeds", ret == 0);

        /* dspqueue_request with invalid (zero-init) context */
        dspqueue_request_payload req = {0}; /* DSPQUEUE_CREATE with ctx=0 */
        ret = dspqueue_request(&req);
        check_err("dspqueue_request(ctx=0) returns error", ret);

        check("dspqueue_close succeeds", dspqueue_close(q) == 0);

    } else {
        /* No hardware — test only the NULL-safe APIs */
        check_err("dspqueue_create returns error without hardware", ret);

        check("dspqueue_close(NULL) returns AEE_EBADPARM",
              dspqueue_close(NULL) == AEE_EBADPARM);

        dspqueue_request_payload req = {0};
        check_err("dspqueue_request(ctx=0) returns error", dspqueue_request(&req));

        printf("  skip: DSP hardware not available; write/read/peek/export "
               "require a valid queue\n");
    }
}

/* -----------------------------------------------------------------------
 * N — HAP_debug_v2 / HAP_debug_runtime
 * --------------------------------------------------------------------- */

static void test_n_hap_debug(void)
{
    printf("\n[N] HAP debug APIs:\n");

    /*
     * Both symbols are declared __attribute__((weak)) in HAP_debug.h.
     * Guard with NULL check in case the runtime image does not provide them.
     */
    if (HAP_debug_v2) {
        HAP_debug_v2(HAP_LEVEL_LOW, __FILE__, __LINE__,
                     "fastrpc_selftest: HAP_debug_v2 reachable");
        printf("  ok: HAP_debug_v2 called\n");
        passed++;
    } else {
        printf("  skip: HAP_debug_v2 symbol is NULL (older firmware)\n");
    }

    if (HAP_debug_runtime) {
        HAP_debug_runtime(HAP_LEVEL_LOW, __FILE__, __LINE__,
                          "fastrpc_selftest: HAP_debug_runtime reachable");
        printf("  ok: HAP_debug_runtime called\n");
        passed++;
    } else {
        printf("  skip: HAP_debug_runtime symbol is NULL (older firmware)\n");
    }
}

/* -----------------------------------------------------------------------
 * main
 * --------------------------------------------------------------------- */

int main(void)
{
    printf("fastrpc self-test (hardware-independent)\n");
    printf("==========================================\n");

    test_a_constants();
    test_b_handle_lifecycle();
    test_c_invoke();
    test_d_control();
    test_e_async();
    test_f_rpcmem();
    test_g_mmap();
    test_h_register_buf();
    test_i_register_fd();
    test_j_register_dma();
    test_k_set_mode();
    test_l_deprecated_mmap();
    test_m_dspqueue();
    test_n_hap_debug();

    printf("\n==========================================\n");
    printf("%d passed, %d failed\n", passed, failed);
    printf("==========================================\n");

    return failed ? 1 : 0;
}
