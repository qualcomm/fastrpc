// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef VERIFY_PRINT_ERROR
#define VERIFY_PRINT_ERROR
#endif /* VERIFY_PRINT_ERROR */

#define FARF_HIGH 1
#define FARF_LOW 1

#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <string.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include "AEEStdErr.h"
#include "AEEstd.h"
#include "HAP_farf.h"
#include "rpcmem_internal.h"
#include "adsp_listener.h"
#include "adsp_listener1.h"
#include "fastrpc_common.h"
#include "fastrpc_internal.h"
#include "listener_buf.h"
#include "mod_table.h"
#include "platform_libs.h"
#include "rpcmem.h"
#include "shared.h"
#include "verify.h"
#include "fastrpc_hash_table.h"

typedef struct {
  pthread_t thread;
  int eventfd;
  int update_requested;
  int params_updated;
  sem_t *r_sem;
  remote_handle64 adsp_listener1_handle;
  ADD_DOMAIN_HASH();
} listener_config;

DECLARE_HASH_TABLE(listener, listener_config);

extern void set_thread_context(int domain);

__QAIC_IMPL_EXPORT int
__QAIC_IMPL(apps_remotectl_open)(const char *name, uint32_t *handle, char *dlStr,
                                 int dlerrorLen,
                                 int *dlErr) __QAIC_IMPL_ATTRIBUTE {
  int domain = get_current_domain();
  int nErr = AEE_SUCCESS;
  remote_handle64 local;
  VERIFY(AEE_SUCCESS ==
         (nErr = mod_table_open(name, handle, dlStr, dlerrorLen, dlErr)));
  VERIFY(AEE_SUCCESS ==
         (nErr = fastrpc_update_module_list(
              REVERSE_HANDLE_LIST_PREPEND, domain, (remote_handle)*handle, &local, NULL)));
bail:
  return nErr;
}

__QAIC_IMPL_EXPORT int
__QAIC_IMPL(apps_remotectl_close)(uint32_t handle, char *errStr, int errStrLen,
                                  int *dlErr) __QAIC_IMPL_ATTRIBUTE {
  int domain = get_current_domain();
  int nErr = AEE_SUCCESS;

  if (AEE_SUCCESS !=
      (nErr = mod_table_close(handle, errStr, errStrLen, dlErr))) {
    if(!is_process_exiting(domain)) {
      FARF(ERROR,
          "Error 0x%x: %s: mod_table_close failed for handle:0x%x (dlErr %s)",
          nErr, __func__, handle, (char *)dlErr);
    }
    goto bail;
  }
  VERIFY(AEE_SUCCESS ==
         (nErr = fastrpc_update_module_list(
              REVERSE_HANDLE_LIST_DEQUEUE, domain, (remote_handle)handle, NULL, NULL)));
bail:
  return nErr;
}

#define RPC_FREEIF(buf)                                                        \
  do {                                                                         \
    if (buf) {                                                                 \
      rpcmem_free_internal(buf);                                               \
      buf = 0;                                                                 \
    }                                                                          \
  } while (0)

static __inline void *rpcmem_realloc(int heapid, uint32_t flags, void *buf,
                                     int oldsize, size_t size) {
  void *bufnew = rpcmem_alloc_internal(heapid, flags, size);
  if (buf && bufnew) {
    memmove(bufnew, buf, STD_MIN(oldsize, size));
    rpcmem_free_internal(buf);
    buf = NULL;
  }
  return bufnew;
}

#define MIN_BUF_SIZE 0x1000
#define ALIGNB(sz) ((sz) == 0 ? MIN_BUF_SIZE : _SBUF_ALIGN((sz), MIN_BUF_SIZE))

static void listener(listener_config *me) {
  int nErr = AEE_SUCCESS, i = 0, domain = me->domain, ref = 0;
  adsp_listener1_invoke_ctx ctx = 0;
  uint8_t *outBufs = 0;
  int outBufsLen = 0, outBufsCapacity = 0;
  uint8_t *inBufs = 0;
  int inBufsLen = 0, inBufsLenReq = 0;
  int result = -1, bufs_len = 0;
  adsp_listener1_remote_handle handle = -1;
  uint32_t sc = 0;
  const char *eheap = getenv("ADSP_LISTENER_HEAP_ID");
  int heapid = eheap == 0 ? -1 : atoi(eheap);
  const char *eflags = getenv("ADSP_LISTENER_HEAP_FLAGS");
  uint32_t flags = eflags == 0 ? 0 : (uint32_t)atoi(eflags);
  const char *emin = getenv("ADSP_LISTENER_MEM_CACHE_SIZE");
  int cache_size = emin == 0 ? 0 : atoi(emin);
  remote_arg args[512];
  struct sbuf buf;
  eventfd_t event = 0xff;

  FARF(ALWAYS, "%s thread starting\n", __func__);
  memset(args, 0, sizeof(args));
  if (eheap || eflags || emin) {
    FARF(RUNTIME_RPC_HIGH,
         "listener using ion heap: %d flags: %x cache: %lld\n", (int)heapid,
         (int)flags, cache_size);
  }

  do {
  invoke:
    sc = 0xffffffff;
    if (result != 0) {
      outBufsLen = 0;
    }
    FARF(RUNTIME_RPC_HIGH,
         "%s responding 0x%x for ctx 0x%x, handle 0x%x, sc 0x%x", __func__,
         result, ctx, handle, sc);
    FASTRPC_PUT_REF(domain);
    if (me->adsp_listener1_handle != INVALID_HANDLE) {
      nErr = __QAIC_HEADER(adsp_listener1_next2)(
          me->adsp_listener1_handle, ctx, result, outBufs, outBufsLen, &ctx,
          &handle, &sc, inBufs, inBufsLen, &inBufsLenReq);
    } else {
      nErr = __QAIC_HEADER(adsp_listener_next2)(
          ctx, result, outBufs, outBufsLen, &ctx, &handle, &sc, inBufs,
          inBufsLen, &inBufsLenReq);
    }
    if (nErr) {
      if (nErr == AEE_EINTERRUPTED) {
        /* UserPD in CPZ migration. Keep retrying until migration is complete.
         * Also reset the context, as previous context is invalid after CPZ
         * migration
        */
        ctx = 0;
        result = -1;
        goto invoke;
      } else if (nErr == (DSP_AEE_EOFFSET + AEE_EBADSTATE)) {
          /* UserPD in irrecoverable bad state. Exit listener */
          goto bail;
      }
      /* For any other error, retry once and exit if error seen again */
      if (me->adsp_listener1_handle != INVALID_HANDLE) {
        nErr = __QAIC_HEADER(adsp_listener1_next2)(
            me->adsp_listener1_handle, ctx, nErr, 0, 0, &ctx, &handle, &sc,
            inBufs, inBufsLen, &inBufsLenReq);
      } else {
        nErr = __QAIC_HEADER(adsp_listener_next2)(ctx, nErr, 0, 0, &ctx,
                                                  &handle, &sc, inBufs,
                                                  inBufsLen, &inBufsLenReq);
      }
      if (nErr) {
        FARF(RUNTIME_HIGH,
               "Error 0x%x: %s response with result 0x%x for ctx 0x%x, handle "
               "0x%x, sc 0x%x failed\n",
               nErr, __func__, result, ctx, handle, sc);
        goto bail;
      }
    }
    FASTRPC_GET_REF(domain);
    if (__builtin_smul_overflow(inBufsLenReq, 2, &bufs_len)) {
      FARF(ERROR,
           "Error: %s: overflow occurred while multiplying input buffer size: "
           "%d * 2 = %d for handle 0x%x, sc 0x%x",
           __func__, inBufsLenReq, bufs_len, handle, sc);
      result = AEE_EBADSIZE;
      goto invoke;
    }
    if (ALIGNB(bufs_len) < inBufsLen && inBufsLen > cache_size) {
      void *buf;
      int size = ALIGNB(bufs_len);
      if (NULL ==
          (buf = rpcmem_realloc(heapid, flags, inBufs, inBufsLen, size))) {
        result = AEE_ENORPCMEMORY;
        FARF(RUNTIME_RPC_HIGH, "rpcmem_realloc shrink failed");
        goto invoke;
      }
      inBufs = buf;
      inBufsLen = size;
    }
    if (inBufsLenReq > inBufsLen) {
      void *buf;
      int req;
      int oldLen = inBufsLen;
      int size = _SBUF_ALIGN(inBufsLenReq, MIN_BUF_SIZE);
      if (AEE_SUCCESS ==
          (buf = rpcmem_realloc(heapid, flags, inBufs, inBufsLen, size))) {
        result = AEE_ENORPCMEMORY;
        FARF(ERROR, "rpcmem_realloc failed");
        goto invoke;
      }
      inBufs = buf;
      inBufsLen = size;
      if (me->adsp_listener1_handle != INVALID_HANDLE) {
        result = __QAIC_HEADER(adsp_listener1_get_in_bufs2)(
            me->adsp_listener1_handle, ctx, oldLen, inBufs + oldLen,
            inBufsLen - oldLen, &req);
      } else {
        result = __QAIC_HEADER(adsp_listener_get_in_bufs2)(
            ctx, oldLen, inBufs + oldLen, inBufsLen - oldLen, &req);
      }
      if (AEE_SUCCESS != result) {
        FARF(RUNTIME_RPC_HIGH, "adsp_listener_invoke_get_in_bufs2 failed  %x",
             result);
        goto invoke;
      }
      if (req > inBufsLen) {
        result = AEE_EBADPARM;
        FARF(RUNTIME_RPC_HIGH,
             "adsp_listener_invoke_get_in_bufs2 failed, size is invalid req %d "
             "inBufsLen %d result %d",
             req, inBufsLen, result);
        goto invoke;
      }
    }
    if (REMOTE_SCALARS_INHANDLES(sc) + REMOTE_SCALARS_OUTHANDLES(sc) > 0) {
      result = AEE_EBADPARM;
      goto invoke;
    }

    sbuf_init(&buf, 0, inBufs, inBufsLen);
    unpack_in_bufs(&buf, args, REMOTE_SCALARS_INBUFS(sc));
    unpack_out_lens(&buf, args + REMOTE_SCALARS_INBUFS(sc),
                    REMOTE_SCALARS_OUTBUFS(sc));

    sbuf_init(&buf, 0, 0, 0);
    pack_out_bufs(&buf, args + REMOTE_SCALARS_INBUFS(sc),
                  REMOTE_SCALARS_OUTBUFS(sc));
    outBufsLen = sbuf_needed(&buf);

    if (__builtin_smul_overflow(outBufsLen, 2, &bufs_len)) {
      FARF(ERROR,
           "%s: Overflow occured while multiplying output buffer size: %d * 2 "
           "= %d",
           __func__, outBufsLen, bufs_len);
      result = AEE_EBADSIZE;
      goto invoke;
    }
    if (ALIGNB(bufs_len) < outBufsCapacity && outBufsCapacity > cache_size) {
      void *buf;
      int size = ALIGNB(bufs_len);
      if (NULL == (buf = rpcmem_realloc(heapid, flags, outBufs, outBufsCapacity,
                                        size))) {
        result = AEE_ENORPCMEMORY;
        FARF(RUNTIME_RPC_HIGH, "listener rpcmem_realloc shrink failed");
        goto invoke;
      }
      outBufs = buf;
      outBufsCapacity = size;
    }
    if (outBufsLen > outBufsCapacity) {
      void *buf;
      int size = ALIGNB(outBufsLen);
      if (NULL == (buf = rpcmem_realloc(heapid, flags, outBufs, outBufsCapacity,
                                        size))) {
        result = AEE_ENORPCMEMORY;
        FARF(ERROR, "listener rpcmem_realloc failed");
        goto invoke;
      }
      outBufs = buf;
      outBufsLen = size;
      outBufsCapacity = size;
    }
    sbuf_init(&buf, 0, outBufs, outBufsLen);
    pack_out_bufs(&buf, args + REMOTE_SCALARS_INBUFS(sc),
                  REMOTE_SCALARS_OUTBUFS(sc));
    result = mod_table_invoke(handle, sc, args);
    if (result && is_process_exiting(domain))
      result = AEE_EBADSTATE; // override result as process is exiting
  } while (1);
bail:
  me->adsp_listener1_handle = INVALID_HANDLE;
  RPC_FREEIF(outBufs);
  RPC_FREEIF(inBufs);
  if (nErr != AEE_SUCCESS) {
    if(!is_process_exiting(domain)) {
      FARF(ERROR,
          "Error 0x%x: %s response with result 0x%x for ctx 0x%x, handle 0x%x, "
          "sc 0x%x failed : listener thread exited (errno %s)",
          nErr, __func__, result, ctx, handle, sc, strerror(errno));
    }
  }
  for (i = 0; i < RETRY_WRITE; i++) {
    if (AEE_SUCCESS == (nErr = eventfd_write(me->eventfd, event))) {
      break;
    }
    // Sleep for 1 sec before retry writing
    sleep(1);
  }
  if (nErr != AEE_SUCCESS) {
    VERIFY_EPRINTF(
        "Error 0x%x : Writing to listener event_fd %d failed (errno %s)", nErr,
        me->eventfd, strerror(errno));
  }
  FARF(ALWAYS, "%s thread exiting\n", __func__);
  dlerror();
}

extern int apps_remotectl_skel_invoke(uint32_t _sc, remote_arg *_pra);
extern int apps_std_skel_invoke(uint32_t _sc, remote_arg *_pra);
extern int apps_mem_skel_invoke(uint32_t _sc, remote_arg *_pra);
extern int adspmsgd_apps_skel_invoke(uint32_t _sc, remote_arg *_pra);
extern int fastrpc_set_remote_uthread_params(int domain);

PL_DEP(mod_table);
PL_DEP(apps_std);

static void *listener_start_thread(void *arg) {
  int nErr = AEE_SUCCESS;
  listener_config *me = (listener_config *)arg;
  int domain = me->domain;
  remote_handle64 adsp_listener1_handle = INVALID_HANDLE;

  /*
   * Need to set TLS key of listener thread to right domain.
   * Otherwise, the init2() call will go to default domain.
   */
  set_thread_context(domain);
  if ((adsp_listener1_handle = get_adsp_listener1_handle(domain)) != INVALID_HANDLE) {
    nErr = __QAIC_HEADER(adsp_listener1_init2)(adsp_listener1_handle);
    if ((nErr == DSP_AEE_EOFFSET + AEE_ERPC) ||
        nErr == DSP_AEE_EOFFSET + AEE_ENOSUCHMOD) {
      FARF(ERROR, "Error 0x%x: %s domains support not available in listener",
           nErr, __func__);
      fastrpc_update_module_list(DOMAIN_LIST_DEQUEUE, domain, _const_adsp_listener1_handle, NULL, NULL);
      adsp_listener1_handle = INVALID_HANDLE;
      VERIFY(AEE_SUCCESS == (nErr = __QAIC_HEADER(adsp_listener_init2)()));
    } else if (nErr == AEE_SUCCESS) {
      me->adsp_listener1_handle = adsp_listener1_handle;
    }
  } else {
    VERIFY(AEE_SUCCESS == (nErr = __QAIC_HEADER(adsp_listener_init2)()));
  }

  if (me->update_requested) {
    /* Update parameters on DSP and signal main thread to proceed */
    me->params_updated = fastrpc_set_remote_uthread_params(domain);
    sem_post(me->r_sem);
    VERIFY(AEE_SUCCESS == (nErr = me->params_updated));
  }
  listener(me);
bail:
  me->adsp_listener1_handle = INVALID_HANDLE;
  if (nErr != AEE_SUCCESS) {
    sem_post(me->r_sem);
    VERIFY_EPRINTF("Error 0x%x: %s failed for domain %d\n", nErr, __func__,
                   domain);
  }
  return (void *)(uintptr_t)nErr;
}

void listener_android_deinit(void) {
  HASH_TABLE_CLEANUP(listener_config);
  PL_DEINIT(mod_table);
  PL_DEINIT(apps_std);
}

int listener_android_init(void) {
  int nErr = 0;

  HASH_TABLE_INIT(listener_config);

  VERIFY(AEE_SUCCESS == (nErr = PL_INIT(mod_table)));
  VERIFY(AEE_SUCCESS == (nErr = PL_INIT(apps_std)));
  VERIFY(AEE_SUCCESS == (nErr = mod_table_register_const_handle(
                             0, "apps_remotectl", apps_remotectl_skel_invoke)));
  VERIFY(AEE_SUCCESS ==
         (nErr = mod_table_register_static("apps_std", apps_std_skel_invoke)));
  VERIFY(AEE_SUCCESS ==
         (nErr = mod_table_register_static("apps_mem", apps_mem_skel_invoke)));
  VERIFY(AEE_SUCCESS == (nErr = mod_table_register_static(
                             "adspmsgd_apps", adspmsgd_apps_skel_invoke)));
bail:
  if (nErr != AEE_SUCCESS) {
    listener_android_deinit();
    VERIFY_EPRINTF("Error %x: fastrpc listener initialization error", nErr);
  }
  return nErr;
}

void listener_android_domain_deinit(int domain) {
  listener_config *me = NULL;

  GET_HASH_NODE(listener_config, domain, me);
  if (!me)
    return;

  FARF(RUNTIME_RPC_HIGH, "fastrpc listener joining to exit");
  if (me->thread) {
    pthread_join(me->thread, 0);
    me->thread = 0;
  }
  FARF(RUNTIME_RPC_HIGH, "fastrpc listener joined");
  me->adsp_listener1_handle = INVALID_HANDLE;
  if (me->eventfd != -1) {
    close(me->eventfd);
    FARF(RUNTIME_RPC_HIGH, "Closed Listener event_fd %d for domain %d\n",
         me->eventfd, domain);
    me->eventfd = -1;
  }
}

int listener_android_domain_init(int domain, int update_requested,
                                 sem_t *r_sem) {
  listener_config *me = NULL;
  int nErr = AEE_SUCCESS;

  GET_HASH_NODE(listener_config, domain, me);
  if (!me) {
    ALLOC_AND_ADD_NEW_NODE_TO_TABLE(listener_config, domain, me);
  }
  me->eventfd = -1;
  VERIFYC(-1 != (me->eventfd = eventfd(0, 0)), AEE_EBADPARM);
  FARF(RUNTIME_RPC_HIGH, "Opened Listener event_fd %d for domain %d\n",
       me->eventfd, domain);
  me->update_requested = update_requested;
  me->r_sem = r_sem;
  me->adsp_listener1_handle = INVALID_HANDLE;
  me->domain = domain;
  VERIFY(AEE_SUCCESS ==
         (nErr = pthread_create(&me->thread, 0, listener_start_thread,
                                (void *)me)));

  if (me->update_requested) {
    /*
     * Semaphore initialized to 0. If main thread reaches wait first,
     * then it will wait for listener to increment semaphore to 1.
     * If listener posted semaphore first, then this wait will decrement
     * semaphore to 0 and proceed.
     */
    sem_wait(me->r_sem);
    VERIFY(AEE_SUCCESS == (nErr = me->params_updated));
  }
bail:
  if (nErr != AEE_SUCCESS) {
    VERIFY_EPRINTF("Error 0x%x: %s failed for domain %d\n", nErr, __func__,
                   domain);
    listener_android_domain_deinit(domain);
  }
  return nErr;
}

int close_reverse_handle(remote_handle64 h, char *dlerr, int dlerrorLen,
                         int *dlErr) {
  return apps_remotectl_close((uint32_t)h, dlerr, dlerrorLen, dlErr);
}

int listener_android_geteventfd(int domain, int *fd) {
  listener_config *me = NULL;
  int nErr = 0;

  GET_HASH_NODE(listener_config, domain, me);
  VERIFYC(me, AEE_ERESOURCENOTFOUND);
  VERIFYC(-1 != me->eventfd, AEE_EBADPARM);
  *fd = me->eventfd;
bail:
  if (nErr != AEE_SUCCESS) {
    VERIFY_EPRINTF("Error %x: listener android getevent file descriptor failed "
                   "for domain %d\n",
                   nErr, domain);
  }
  return nErr;
}

PL_DEFINE(listener_android, listener_android_init, listener_android_deinit)
