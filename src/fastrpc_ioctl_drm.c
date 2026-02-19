// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "AEEStdErr.h"
#include "HAP_farf.h"
#include "fastrpc_async.h"
#include "fastrpc_internal.h"
#include "fastrpc_notif.h"
#include "remote.h"
#include "fastrpc_ioctl_drm.h"
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <string.h>

/* check async support */
int is_async_fastrpc_supported(void) {
  /* async not supported by QDA driver yet */
  return 0;
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

/* Helper function to convert fastrpc invoke args to qda invoke args */
static void convert_invoke_args(struct fastrpc_invoke_args *fastrpc_args, 
                               struct qda_invoke_args *qda_args, 
                               int num_args) {
  int i;

  for (i = 0; i < num_args; i++) {
    qda_args[i].ptr = fastrpc_args[i].ptr;
    qda_args[i].length = fastrpc_args[i].length;
    qda_args[i].fd = fastrpc_args[i].fd;
    qda_args[i].attr = fastrpc_args[i].attr;
  }
}

int ioctl_init(int dev, uint32_t flags, int attr, unsigned char *shell, int shelllen,
               int shellfd, char *mem, int memlen, int memfd, int tessiglen) {
  int ioErr = 0;
  struct qda_init_create init = {0};

  switch (flags) {
  case FASTRPC_INIT_ATTACH:
    ioErr = ioctl(dev, DRM_IOCTL_QDA_INIT_ATTACH, NULL);
    break;
  case FASTRPC_INIT_ATTACH_SENSORS:
    /* Sensors not supported in QDA, fallback to regular attach */
    ioErr = ioctl(dev, DRM_IOCTL_QDA_INIT_ATTACH, NULL);
    break;
  case FASTRPC_INIT_CREATE_STATIC:
    /* Static PD creation not directly supported, use regular create */
    init.file = (uint64_t)shell;
    init.filelen = shelllen;
    init.filefd = shellfd;
    init.attrs = attr;
    init.siglen = tessiglen;
    ioErr = ioctl(dev, DRM_IOCTL_QDA_INIT_CREATE, &init);
    break;
  case FASTRPC_INIT_CREATE:
    init.file = (uint64_t)shell;
    init.filelen = shelllen;
    init.filefd = shellfd;
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

int ioctl_invoke(int dev, int req, remote_handle handle, uint32_t sc, void *pra,
                 int *fds, unsigned int *attrs, void *job, unsigned int *crc,
                 uint64_t *perf_kernel, uint64_t *perf_dsp) {
  int ioErr = AEE_SUCCESS;
  struct qda_invoke invoke = {0};
  struct fastrpc_invoke_args *fastrpc_args = (struct fastrpc_invoke_args *)pra;
  struct qda_invoke_args *qda_args = NULL;
  int num_args = 0;

  /* Calculate number of arguments from scalars */
  num_args = ((sc >> 16) & 0xff) + ((sc >> 8) & 0xff);
  
  if (num_args > 0 && fastrpc_args) {
    qda_args = (struct qda_invoke_args *)malloc(num_args * sizeof(struct qda_invoke_args));
    if (!qda_args) {
      return AEE_ENOMEMORY;
    }
    convert_invoke_args(fastrpc_args, qda_args, num_args);
  }

  invoke.handle = handle;
  invoke.sc = sc;
  invoke.args = (uint64_t)qda_args;
  
  if (req >= INVOKE && req <= INVOKE_FD) {
    ioErr = ioctl(dev, DRM_IOCTL_QDA_INVOKE, &invoke);
  } else {
    ioErr = AEE_EUNSUPPORTED;
  }

  if (qda_args) {
    free(qda_args);
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

  switch (req) {
  case MEM_MAP: {
    /* FD-based mapping with attributes */
    qda_map.request = QDA_MAP_REQUEST_ATTR;
    qda_map.flags = flags;
    qda_map.fd = fd;
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
    qda_map.fd = fd;
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

  switch (req) {
  case MEM_UNMAP:
  case MUNMAP_FD: {
    /* FD-based unmapping with attributes */
    qda_unmap.request = QDA_MUNMAP_REQUEST_ATTR;
    qda_unmap.fd = fd;
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
