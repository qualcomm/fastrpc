// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/* File only compiled  when support to upstream kernel is required*/

#include "AEEStdErr.h"
#include "HAP_farf.h"
#include "fastrpc_internal.h"
#include "fastrpc_notif.h"
#include "remote.h"
#include "rpcmem.h"
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

/*
 * Buffer arguments without a dma-buf fd (fd == -1) are staged in 128-byte
 * aligned slices of one rpcmem buffer allocated per invoke, whose fd is passed
 * in place of -1. Slices may share a page: the kernel derives each argument's
 * physical range from its own ptr/length.
 */
#define SCRATCH_ALIGN 128
#define SCRATCH_ALIGN_UP(x) (((x) + (SCRATCH_ALIGN - 1)) & ~((uint64_t)SCRATCH_ALIGN - 1))

int ioctl_init(int dev, uint32_t flags, int attr, unsigned char *shell, int shelllen,
               int shellfd, char *mem, int memlen, int memfd, int tessiglen) {
  int ioErr = 0;
  struct fastrpc_ioctl_init_create init = {0};
  struct fastrpc_ioctl_init_create_static init_static = {0};

  switch (flags) {
  case FASTRPC_INIT_ATTACH:
    ioErr = ioctl(dev, FASTRPC_IOCTL_INIT_ATTACH, NULL);
    break;
  case FASTRPC_INIT_ATTACH_SENSORS:
    ioErr = ioctl(dev, FASTRPC_IOCTL_INIT_ATTACH_SNS, NULL);
    break;
  case FASTRPC_INIT_CREATE_STATIC:
    init_static.namelen = shelllen;
    init_static.memlen = memlen;
    init_static.name = (uint64_t)(uintptr_t)shell;
    ioErr = ioctl(dev, FASTRPC_IOCTL_INIT_CREATE_STATIC,
                  (unsigned long)&init_static);
    break;
  case FASTRPC_INIT_CREATE:
    init.file = (uint64_t)(uintptr_t)shell;
    init.filelen = shelllen;
    init.filefd = shellfd;
    init.attrs = attr;
    init.siglen = tessiglen;
    ioErr = ioctl(dev, FASTRPC_IOCTL_INIT_CREATE, (unsigned long)&init);
    break;
  default:
    FARF(ERROR, "ERROR: %s Invalid init flags passed %d", __func__, flags);
    ioErr = AEE_EBADPARM;
    break;
  }

  return ioErr;
}

int ioctl_invoke(int dev, int req, remote_handle handle, uint32_t sc, void *pra,
                 int *fds, unsigned int *attrs, unsigned int *crc,
                 uint64_t *perf_kernel, uint64_t *perf_dsp) {
  int ioErr = AEE_SUCCESS;
  int saved_errno = 0;
  struct fastrpc_ioctl_invoke invoke = {0};
  struct fastrpc_invoke_args *args = (struct fastrpc_invoke_args *)pra;
  struct fastrpc_invoke_args *kargs = NULL;
  int num_inbufs = REMOTE_SCALARS_INBUFS(sc);
  int num_bufs = num_inbufs + REMOTE_SCALARS_OUTBUFS(sc);
  int total = num_bufs + REMOTE_SCALARS_INHANDLES(sc) +
              REMOTE_SCALARS_OUTHANDLES(sc);
  uint8_t *scratch = NULL;
  int scratch_fd = -1;
  size_t scratch_size = 0;
  size_t off = 0;
  int i;

  if (!(req >= INVOKE && req <= INVOKE_FD))
    return AEE_EUNSUPPORTED;

  for (i = 0; args && i < num_bufs; i++) {
    if (args[i].fd < 0 && args[i].length > 0)
      scratch_size += SCRATCH_ALIGN_UP(args[i].length);
  }

  if (scratch_size) {
    scratch = (uint8_t *)rpcmem_alloc2(RPCMEM_HEAP_ID_SYSTEM,
                                       RPCMEM_DEFAULT_FLAGS, scratch_size);
    if (!scratch)
      return AEE_ENOMEMORY;
    scratch_fd = rpcmem_to_fd(scratch);

    /* Patch a copy so the caller's argument array is left untouched */
    kargs = (struct fastrpc_invoke_args *)malloc(total * sizeof(*kargs));
    if (!kargs) {
      ioErr = AEE_ENOMEMORY;
      goto bail;
    }
    memcpy(kargs, args, total * sizeof(*kargs));

    for (i = 0; i < num_bufs; i++) {
      if (args[i].fd >= 0 || !args[i].length)
        continue;

      if (i < num_inbufs && args[i].ptr)
        memcpy(scratch + off, (void *)(uintptr_t)args[i].ptr,
               (size_t)args[i].length);

      kargs[i].ptr = (uint64_t)(uintptr_t)(scratch + off);
      kargs[i].fd = scratch_fd;
      off += SCRATCH_ALIGN_UP(args[i].length);
    }
  }

  invoke.handle = handle;
  invoke.sc = sc;
  invoke.args = (uint64_t)(uintptr_t)(kargs ? kargs : pra);
  ioErr = ioctl(dev, FASTRPC_IOCTL_INVOKE, (unsigned long)&invoke);
  saved_errno = errno;

  if (ioErr == 0 && kargs) {
    for (i = num_inbufs; i < num_bufs; i++) {
      if (args[i].fd < 0 && args[i].length > 0 && args[i].ptr)
        memcpy((void *)(uintptr_t)args[i].ptr, (void *)(uintptr_t)kargs[i].ptr,
               (size_t)args[i].length);
    }
  }

bail:
  free(kargs);
  if (scratch)
    rpcmem_free(scratch);

  /* Callers map a failure via errno, so don't let cleanup clobber it */
  errno = saved_errno;
  return ioErr;
}

int ioctl_invoke2_notif(int dev, int *domain, int *session, int *status) {
  return AEE_EUNSUPPORTED;
}

int ioctl_mmap(int dev, int req, uint32_t flags, int attr, int fd, int offset,
               size_t len, uintptr_t vaddrin, uint64_t *vaddrout) {
  int ioErr = AEE_SUCCESS;

  switch (req) {
  case MEM_MAP: {
    struct fastrpc_ioctl_mem_map map = {0};
    map.version = 0;
    map.fd = fd;
    map.offset = offset;
    map.flags = flags;
    map.vaddrin = (uint64_t)vaddrin;
    map.length = len;
    map.attrs = attr;
    ioErr = ioctl(dev, FASTRPC_IOCTL_MEM_MAP, (unsigned long)&map);
    *vaddrout = (uint64_t)map.vaddrout;
  } break;
  case MMAP:
  case MMAP_64: {
    struct fastrpc_ioctl_req_mmap map = {0};
    map.fd = fd;
    map.flags = flags;
    map.vaddrin = (uint64_t)vaddrin;
    map.size = len;
    ioErr = ioctl(dev, FASTRPC_IOCTL_MMAP, (unsigned long)&map);
    *vaddrout = (uint64_t)map.vaddrout;
  } break;
  default:
    FARF(ERROR, "ERROR: %s Invalid request passed %d", __func__, req);
    ioErr = AEE_EBADPARM;
    break;
  }
  return ioErr;
}

int ioctl_munmap(int dev, int req, int attr, void *buf, int fd, int len,
                 uint64_t vaddr) {
  int ioErr = AEE_SUCCESS;

  switch (req) {
  case MEM_UNMAP:
  case MUNMAP_FD: {
    struct fastrpc_ioctl_mem_unmap unmap = {0};
    unmap.version = 0;
    unmap.fd = fd;
    unmap.vaddr = vaddr;
    unmap.length = len;
    ioErr = ioctl(dev, FASTRPC_IOCTL_MEM_UNMAP, (unsigned long)&unmap);
  } break;
  case MUNMAP:
  case MUNMAP_64: {
    struct fastrpc_ioctl_req_munmap unmap = {0};
    unmap.vaddrout = vaddr;
    unmap.size = (ssize_t)len;
    ioErr = ioctl(dev, FASTRPC_IOCTL_MUNMAP, (unsigned long)&unmap);
  } break;
  default:
    FARF(ERROR, "ERROR: %s Invalid request passed %d", __func__, req);
    break;
  }

  return ioErr;
}

int ioctl_getinfo(int dev, uint32_t *info) {
  *info = 1;
  return AEE_SUCCESS;
}

int ioctl_getdspinfo(int dev, int domain, uint32_t attr, uint32_t *capability) {
  int ioErr = AEE_SUCCESS;
  static struct fastrpc_ioctl_capability cap = {0};

  if (attr >= PERF_V2_DRIVER_SUPPORT && attr < FASTRPC_MAX_ATTRIBUTES) {
    *capability = 0;
    return 0;
  }

  cap.domain = domain;
  cap.attribute_id = attr;
  cap.capability = 0;
  ioErr = ioctl(dev, FASTRPC_IOCTL_GET_DSP_INFO, &cap);
  *capability = cap.capability;
  return ioErr;
}

int ioctl_setmode(int dev, int mode) {
  if (mode == FASTRPC_SESSION_ID1)
    return AEE_SUCCESS;

  return AEE_EUNSUPPORTED;
}

int ioctl_control(int dev, int req, void *c) {
  int ioErr = AEE_SUCCESS;
  struct fastrpc_ioctl_set_option opt = {0};

  if (req == FASTRPC_POLL_MODE) {
    opt = *(struct fastrpc_ioctl_set_option *)c;
    ioErr = ioctl(dev, FASTRPC_IOCTL_SET_OPTION, &opt);

    return ioErr;
  }
  return AEE_EUNSUPPORTED;
}

int ioctl_getperf(int dev, int key, void *data, int *datalen) {
  return AEE_EUNSUPPORTED;
}

int ioctl_signal_create(int dev, uint32_t signal, uint32_t flags) {
  return AEE_EUNSUPPORTED;
}

int ioctl_signal_destroy(int dev, uint32_t signal) {
  return AEE_EUNSUPPORTED;
}

int ioctl_signal_signal(int dev, uint32_t signal) {
  return AEE_EUNSUPPORTED;
}

int ioctl_signal_wait(int dev, uint32_t signal, uint32_t timeout_usec) {
  return AEE_EUNSUPPORTED;
}

int ioctl_signal_cancel_wait(int dev, uint32_t signal) {
  return AEE_EUNSUPPORTED;
}

int ioctl_sharedbuf(int dev,
                    struct fastrpc_proc_sharedbuf_info *sharedbuf_info) {
  return AEE_EUNSUPPORTED;
}

int ioctl_session_info(int dev, struct fastrpc_proc_sess_info *sess_info) {
  return AEE_EUNSUPPORTED;
}

int ioctl_optimization(int dev, uint32_t max_concurrency) {
  return AEE_EUNSUPPORTED;
}

int ioctl_mdctx_manage(int dev, int req, void *user_ctx,
	unsigned int *domain_ids, unsigned int num_domain_ids, uint64_t *ctx)
{
	// TODO: Implement this for opensource
	return AEE_EUNSUPPORTED;
}

int fastrpc_async_get_status(fastrpc_async_jobid jobid, int timeout_us,
                             int *result) {
  return AEE_EUNSUPPORTED;
}

int fastrpc_release_async_job(fastrpc_async_jobid jobid) {
  return AEE_EUNSUPPORTED;
}
