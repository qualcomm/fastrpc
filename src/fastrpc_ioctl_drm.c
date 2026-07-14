// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "AEEStdErr.h"
#include "HAP_farf.h"
#include "fastrpc_internal.h"
#include "fastrpc_notif.h"
#include "remote.h"
#include "fastrpc_ioctl_drm.h"
#include "rpcmem_internal.h"
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <string.h>

/*
 * Alignment used for individual scratch-buffer slices allocated via
 * rpcmem_alloc_internal(). This intentionally matches FASTRPC_ALIGN
 * (128 bytes) as defined in the kernel driver's qda_fastrpc.h -- the same
 * granularity the kernel's own (now-removed) process_direct_buffer() used
 * to pack multiple small buffer arguments tightly within a single physical
 * page. The kernel's GEM-handle buffer path (process_fd_buffer(), via
 * calculate_vma_offset()/calculate_page_aligned_size()) computes physical
 * page descriptors purely from each argument's own pointer + length, so
 * slices are free to share a physical page with other slices; there is no
 * requirement for each slice to start on its own page. Using 128-byte
 * alignment instead of a full page per slice avoids wasting up to
 * (QDA_PAGE_SIZE - 1) bytes of scratch-buffer space for every small/scalar
 * argument (e.g. a 4-byte length or an 8-byte result), which matters since
 * the vast majority of "direct" buffer arguments are only a few bytes long.
 */
#define QDA_SCRATCH_ALIGN 128
#define QDA_SCRATCH_ALIGN_UP(x) (((x) + (QDA_SCRATCH_ALIGN - 1)) & ~(QDA_SCRATCH_ALIGN - 1))

static int import_fd_to_gem_handle(int dev, int fd, uint32_t *handle_out);
static int close_gem_handle(int dev, uint32_t handle);

/*
 * import_fd_to_gem_handle() - Import a DMA-BUF fd into the QDA device as a GEM handle
 *
 * The driver only accepts GEM handles in ioctl structures.  This helper
 * performs the fd→handle import (DRM_IOCTL_PRIME_FD_TO_HANDLE) so that
 * any DMA-BUF fd obtained from fdlist can be translated before an ioctl call.
 * The caller must release the handle with close_gem_handle() after the ioctl.
 *
 * Returns 0 on success, -errno on failure.
 */
static int import_fd_to_gem_handle(int dev, int fd, uint32_t *handle_out)
{
  struct drm_prime_handle ph;

  memset(&ph, 0, sizeof(ph));
  ph.fd = fd;
  ph.handle = 0;

  if (ioctl(dev, DRM_IOCTL_PRIME_FD_TO_HANDLE, &ph) != 0)
    return -1;

  *handle_out = ph.handle;
  return 0;
}

/*
 * close_gem_handle() - Release a GEM handle imported for a single ioctl call
 *
 * Imported handles (from import_fd_to_gem_handle) must be closed after use
 * to avoid leaking handle table entries in the driver.
 */
static int close_gem_handle(int dev, uint32_t handle)
{
  struct drm_gem_close gc;

  memset(&gc, 0, sizeof(gc));
  gc.handle = handle;
  return ioctl(dev, DRM_IOCTL_GEM_CLOSE, &gc);
}

/* Returns the name of the domain based on the following
 ADSP/CDSP - Return accel device node
 */
const char *get_secure_domain_name(int domain_id) {
  const char *name;
  int domain = GET_DOMAIN_FROM_EFFEC_DOMAIN_ID(domain_id);

  switch (domain) {
  case ADSP_DOMAIN_ID:
    name = ADSPRPC_DEVICE;  /* /dev/accel/accel0 */
    break;
  case CDSP_DOMAIN_ID:
    name = CDSPRPC_DEVICE;  /* /dev/accel/accel1 */
    break;
  default:
    /* For unsupported domains, fallback to ADSP */
    name = ADSPRPC_DEVICE;
    break;
  }
  return name;
}

/*
 * ioctl_invoke() - Issue a FastRPC invoke ioctl to the QDA DRM driver.
 *
 * Scratch buffer for "direct"/inline FastRPC buffer arguments
 * ============================================================
 * Buffer arguments that do not carry a caller-supplied dma-buf fd
 * (fastrpc_invoke_args[i].fd <= 0) need to be backed by DMA-capable memory
 * so the driver can map them to the DSP.  A single contiguous scratch
 * buffer is allocated from the system heap via rpcmem_alloc_internal() for
 * every ioctl_invoke() call that needs one.  Each such argument gets a
 * 128-byte-aligned slice of this buffer:
 *
 *   - IN  buffers: caller data is copied into the slice before the ioctl.
 *   - OUT buffers: driver-written data is copied back to the caller after
 *                  the ioctl completes.
 *
 * Unlike the upstream fastrpc driver (which accepts a dma-buf fd directly
 * on each invoke arg), the QDA driver only accepts GEM handles.  The
 * scratch buffer's dma-buf fd is therefore imported once, up front, to a
 * single GEM handle shared by every scratch-backed argument in this call;
 * caller-supplied fds are imported individually per argument.  All
 * imported handles are closed again before returning.
 *
 * A fresh scratch buffer is allocated per call and freed before returning,
 * keeping its lifetime strictly bounded to a single invoke.
 */
int ioctl_invoke(int dev, int req, remote_handle handle, uint32_t sc, void *pra,
                 int *fds, unsigned int *attrs, unsigned int *crc,
                 uint64_t *perf_kernel, uint64_t *perf_dsp) {
  int ioErr = AEE_SUCCESS;
  struct qda_invoke invoke = {0};
  struct fastrpc_invoke_args *fastrpc_args = (struct fastrpc_invoke_args *)pra;
  struct qda_invoke_args *qda_args = NULL;
  uint8_t *scratch_used = NULL;
  void *scratch_buf = NULL;
  uint32_t scratch_gem_handle = 0;
  int scratch_fd = -1;
  int num_bufs = 0;
  int num_inbufs = 0;
  int i;
  size_t scratch_needed = 0;
  size_t scratch_off = 0;

  /* Calculate number of buffer arguments from scalars (in + out) */
  num_inbufs = (sc >> 16) & 0xff;
  num_bufs = num_inbufs + ((sc >> 8) & 0xff);

  if (num_bufs > 0 && fastrpc_args) {
    qda_args = (struct qda_invoke_args *)calloc(num_bufs, sizeof(struct qda_invoke_args));
    if (!qda_args)
      return AEE_ENOMEMORY;

    scratch_used = (uint8_t *)calloc(num_bufs, sizeof(uint8_t));
    if (!scratch_used) {
      free(qda_args);
      return AEE_ENOMEMORY;
    }

    /*
     * First pass: calculate total scratch space needed for buffer
     * arguments that have no caller-supplied fd.  Each slice is rounded
     * up to QDA_SCRATCH_ALIGN so that adjacent slices remain properly
     * aligned.
     */
    for (i = 0; i < num_bufs; i++) {
      if (fastrpc_args[i].fd <= 0 && fastrpc_args[i].length > 0) {
        scratch_needed += QDA_SCRATCH_ALIGN_UP(fastrpc_args[i].length);
      }
    }

    if (scratch_needed > 0) {
      /*
       * Allocate a single contiguous scratch buffer from the system heap.
       * rpcmem_alloc_internal() returns DMA-capable memory backed by a
       * dma-buf allocation; import its fd to the single GEM handle shared
       * by every scratch-backed argument in this call.
       */
      scratch_buf = rpcmem_alloc_internal(RPCMEM_HEAP_ID_SYSTEM, RPCMEM_DEFAULT_FLAGS,
                                          scratch_needed);
      if (!scratch_buf) {
        FARF(ERROR, "%s: Failed to allocate scratch buffer of size %zu",
             __func__, scratch_needed);
        ioErr = AEE_ENOMEMORY;
        goto bail;
      }

      scratch_fd = rpcmem_to_fd_internal(scratch_buf);
      if (import_fd_to_gem_handle(dev, scratch_fd, &scratch_gem_handle) != 0) {
        FARF(ERROR, "%s: Failed to import scratch fd %d to GEM handle",
             __func__, scratch_fd);
        ioErr = AEE_EFAILED;
        goto bail;
      }
    }

    /*
     * Second pass: populate qda_args.  For args with a valid caller fd,
     * import that fd to its own GEM handle.  For args without fd, redirect
     * ptr to the corresponding scratch slice, set the shared scratch GEM
     * handle, and copy IN data if needed.
     */
    for (i = 0; i < num_bufs; i++) {
      qda_args[i].ptr    = fastrpc_args[i].ptr;
      qda_args[i].length = fastrpc_args[i].length;
      qda_args[i].attr   = fastrpc_args[i].attr;
      qda_args[i].handle = 0;

      if (fastrpc_args[i].fd <= 0 && fastrpc_args[i].length > 0) {
        void *slice = (uint8_t *)scratch_buf + scratch_off;

        qda_args[i].handle = scratch_gem_handle;
        qda_args[i].ptr    = (uint64_t)(uintptr_t)slice;

        /* IN buffer: copy caller data into the scratch slice before the ioctl */
        if (i < num_inbufs && fastrpc_args[i].ptr) {
          memcpy(slice, (void *)(uintptr_t)fastrpc_args[i].ptr,
                 (size_t)fastrpc_args[i].length);
        }

        scratch_used[i] = 1;
        scratch_off += QDA_SCRATCH_ALIGN_UP(fastrpc_args[i].length);
      } else if (fastrpc_args[i].fd > 0) {
        if (import_fd_to_gem_handle(dev, fastrpc_args[i].fd, &qda_args[i].handle) != 0) {
          FARF(ERROR, "%s: Failed to import fd %d to GEM handle", __func__,
               fastrpc_args[i].fd);
          ioErr = AEE_EFAILED;
          goto bail;
        }
      }
    }
  }

  invoke.handle = handle;
  invoke.sc = sc;
  invoke.args = (uint64_t)qda_args;

  if (req >= INVOKE && req <= INVOKE_FD)
    ioErr = ioctl(dev, DRM_IOCTL_QDA_INVOKE, &invoke);
  else
    ioErr = AEE_EUNSUPPORTED;

  if (ioErr != 0) {
    FARF(ERROR, "%s: DRM_IOCTL_QDA_INVOKE failed ioErr=%d errno=%d",
         __func__, ioErr, errno);
  }

  /* Copy OUT buffers backed by the scratch pool back to the caller */
  if (ioErr == 0 && scratch_used) {
    for (i = num_inbufs; i < num_bufs; i++) {
      if (scratch_used[i] && fastrpc_args[i].ptr && fastrpc_args[i].length > 0) {
        memcpy((void *)(uintptr_t)fastrpc_args[i].ptr,
               (void *)(uintptr_t)qda_args[i].ptr,
               (size_t)fastrpc_args[i].length);
      }
    }
  }

bail:
  if (scratch_gem_handle)
    close_gem_handle(dev, scratch_gem_handle);
  if (scratch_buf)
    rpcmem_free_internal(scratch_buf);
  if (scratch_used)
    free(scratch_used);

  /* Close per-argument GEM handles imported for caller-supplied fds */
  if (qda_args) {
    for (i = 0; i < num_bufs; i++) {
      if (fastrpc_args[i].fd > 0 && qda_args[i].handle)
        close_gem_handle(dev, qda_args[i].handle);
    }
    free(qda_args);
  }

  return ioErr;
}

int ioctl_init(int dev, uint32_t flags, int attr, unsigned char *shell, int shelllen,
               int shellfd, char *mem, int memlen, int memfd, int tessiglen) {
  int ioErr = 0;
  struct qda_init_create init = {0};
  uint32_t filehandle = 0;

  switch (flags) {
  case FASTRPC_INIT_ATTACH:
    ioErr = ioctl(dev, DRM_IOCTL_QDA_INIT_ATTACH, NULL);
    break;
  case FASTRPC_INIT_ATTACH_SENSORS:
    /* Sensors not supported in QDA, fallback to regular attach */
    ioErr = ioctl(dev, DRM_IOCTL_QDA_INIT_ATTACH, NULL);
    break;
  case FASTRPC_INIT_CREATE_STATIC:
  case FASTRPC_INIT_CREATE:
    /* Import ELF file DMA-BUF fd to GEM handle; driver only accepts handles */
    if (shellfd > 0) {
      if (import_fd_to_gem_handle(dev, shellfd, &filehandle) != 0) {
        FARF(ERROR, "ERROR: %s Failed to import shellfd %d to GEM handle",
             __func__, shellfd);
        return AEE_EFAILED;
      }
    }
    init.file = (uint64_t)shell;
    init.filelen = shelllen;
    init.filehandle = filehandle;
    init.attrs = attr;
    init.siglen = tessiglen;
    ioErr = ioctl(dev, DRM_IOCTL_QDA_INIT_CREATE, &init);
    break;
  default:
    FARF(ERROR, "ERROR: %s Invalid init flags passed %d", __func__, flags);
    ioErr = AEE_EBADPARM;
    break;
  }

  return ioErr;
}

int ioctl_invoke2_response(int dev, fastrpc_async_jobid *jobid,
                           remote_handle *handle, uint32_t *sc, int *result,
                           uint64_t *perf_kernel, uint64_t *perf_dsp) {
  return AEE_EUNSUPPORTED;
}

int ioctl_invoke2_notif(int dev, int *domain, int *session, int *status) {
  return AEE_EUNSUPPORTED;
}

int ioctl_mmap(int dev, int req, uint32_t flags, int attr, int fd, int offset,
               size_t len, uintptr_t vaddrin, uint64_t *vaddrout) {
  int ioErr = AEE_SUCCESS;
  struct qda_mem_map qda_map = {0};
  uint32_t gem_handle = 0;

  /* Import DMA-BUF fd to GEM handle; driver only accepts GEM handles */
  if (fd > 0) {
    if (import_fd_to_gem_handle(dev, fd, &gem_handle) != 0) {
      FARF(ERROR, "ERROR: %s Failed to import fd %d to GEM handle", __func__, fd);
      return AEE_EFAILED;
    }
  }

  switch (req) {
  case MEM_MAP: {
    /* Handle-based mapping with attributes */
    qda_map.request = QDA_MAP_REQUEST_ATTR;
    qda_map.flags = flags;
    qda_map.handle = gem_handle;
    qda_map.dsp_handle = fd;
    qda_map.attrs = attr;
    qda_map.offset = offset;
    qda_map.vaddrin = (uint64_t)vaddrin;
    qda_map.size = len;

    ioErr = ioctl(dev, DRM_IOCTL_QDA_MAP, &qda_map);
    if (ioErr != 0) {
      FARF(ERROR, "%s: FAILED DRM_IOCTL_QDA_MAP (MEM_MAP), ioErr=%d", __func__, ioErr);
    } else {
      FARF(ALWAYS, "%s: MEM_MAP successful, vaddrout=0x%llx", __func__, qda_map.vaddrout);
    }
    *vaddrout = qda_map.vaddrout;
  } break;
  case MMAP:
  case MMAP_64: {
    /* Legacy mapping operation */
    qda_map.request = QDA_MAP_REQUEST_LEGACY;
    qda_map.flags = flags;
    qda_map.handle = gem_handle;
    qda_map.dsp_handle = fd;
    qda_map.vaddrin = (uint64_t)vaddrin;
    qda_map.size = len;
    /* attrs and offset remain 0 for legacy */

    ioErr = ioctl(dev, DRM_IOCTL_QDA_MAP, &qda_map);
    if (ioErr != 0) {
      FARF(ERROR, "%s: FAILED DRM_IOCTL_QDA_MAP (MMAP_64), ioErr=%d", __func__, ioErr);
    } else {
      FARF(ALWAYS, "%s: MMAP_64 successful, vaddrout=0x%llx", __func__, qda_map.vaddrout);
    }
    *vaddrout = qda_map.vaddrout;
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
  struct qda_mem_unmap qda_unmap = {0};
  uint32_t gem_handle = 0;

  /* Import DMA-BUF fd to GEM handle; driver only accepts GEM handles */
  if (fd > 0) {
    if (import_fd_to_gem_handle(dev, fd, &gem_handle) != 0) {
      FARF(ERROR, "ERROR: %s Failed to import fd %d to GEM handle", __func__, fd);
      return AEE_EFAILED;
    }
  }

  switch (req) {
  case MEM_UNMAP:
  case MUNMAP_FD: {
    /* Handle-based unmapping with attributes */
    qda_unmap.request = QDA_MUNMAP_REQUEST_ATTR;
    qda_unmap.handle = gem_handle;
    qda_unmap.dsp_handle = fd;
    qda_unmap.vaddr = vaddr;
    qda_unmap.size = len;

    ioErr = ioctl(dev, DRM_IOCTL_QDA_MUNMAP, &qda_unmap);
    if (ioErr != 0) {
      FARF(ERROR, "%s: FAILED DRM_IOCTL_QDA_MUNMAP (MEM_UNMAP), ioErr=%d", __func__, ioErr);
    } else {
      FARF(ALWAYS, "%s: MEM_UNMAP successful", __func__);
    }
  } break;
  case MUNMAP:
  case MUNMAP_64: {
    /* Legacy unmapping operation */
    qda_unmap.request = QDA_MUNMAP_REQUEST_LEGACY;
    qda_unmap.vaddrout = vaddr;
    qda_unmap.size = len;

    ioErr = ioctl(dev, DRM_IOCTL_QDA_MUNMAP, &qda_unmap);
    if (ioErr != 0) {
      FARF(ERROR, "%s: FAILED DRM_IOCTL_QDA_MUNMAP (MUNMAP_64), ioErr=%d", __func__, ioErr);
    } else {
      FARF(ALWAYS, "%s: MUNMAP_64 successful", __func__);
    }
  } break;
  default:
    FARF(ERROR, "ERROR: %s Invalid request passed %d", __func__, req);
    ioErr = AEE_EBADPARM;
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

  if (attr == USERSPACE_ALLOCATION_SUPPORT) {
    *capability = 1;
  }
  
  return ioErr;
}

int ioctl_setmode(int dev, int mode) {
  if (mode == FASTRPC_SESSION_ID1)
    return AEE_SUCCESS;

  return AEE_EUNSUPPORTED;
}

int ioctl_control(int dev, int req, void *c) {
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
	/* Multi-domain context not implemented for QDA yet */
	return AEE_EUNSUPPORTED;
}
