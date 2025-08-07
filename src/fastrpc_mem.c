// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause


//#ifndef VERIFY_PRINT_ERROR
//#define VERIFY_PRINT_ERROR
//#endif // VERIFY_PRINT_ERROR
//#ifndef VERIFY_PRINT_INFO
//#define VERIFY_PRINT_INFO
//#endif // VERIFY_PRINT_INFO

#ifndef VERIFY_PRINT_WARN
#define VERIFY_PRINT_WARN
#endif // VERIFY_PRINT_WARN
#ifndef VERIFY_PRINT_ERROR_ALWAYS
#define VERIFY_PRINT_ERROR_ALWAYS
#endif // VERIFY_PRINT_ERROR_ALWAYS

#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <signal.h>

#define FARF_ERROR 1

#include "AEEQList.h"
#include "AEEStdErr.h"
#include "AEEstd.h"
#include "HAP_farf.h"
#include "fastrpc_common.h"
#include "fastrpc_internal.h"
#include "fastrpc_mem.h"
#include "rpcmem.h"
#include "shared.h"
#include "verify.h"

#ifdef LE_ENABLE
#define PROPERTY_VALUE_MAX                                                     \
  92 // as this macro is defined in cutils for Android platforms, defined
     // explicitly for LE platform
#elif (defined _ANDROID) || (defined ANDROID)
// TODO: Bharath #include "cutils/properties.h"
#define PROPERTY_VALUE_MAX 92
#else
#define PROPERTY_VALUE_MAX 92
#endif

#ifndef _WIN32
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <sys/mman.h>
#endif // __WIN32

#ifndef INT_MAX
#define INT_MAX (int)(-1)
#endif

#define INVALID_DOMAIN_ID -1
#define INVALID_HANDLE (remote_handle64)(-1)
#define INVALID_KEY (pthread_key_t)(-1)

#define MAX_DMA_HANDLES 256

// Mask for Map control flags
#define FASTRPC_MAP_FLAGS_MASK (0xFFFF)

struct mem_to_fd {
  QNode qn;
  void *buf;
  size_t size;
  int fd;
  int nova;
  int attr;
  int refcount;
  bool mapped[NUM_DOMAINS_EXTEND]; //! Buffer persistent mapping status
};

struct mem_to_fd_list {
  QList ql;
  pthread_mutex_t mut;
};

struct dma_handle_info {
  int fd;
  int len;
  int used;
  uint32_t attr;
};

/**
 * List to maintain static mappings of a fastrpc session.
 * Access to the list is protected with mutex.
 */
struct static_map {
  QNode qn;
  struct fastrpc_mem_map map;
  int refs;
};

struct static_map_list {
  QList ql;
  pthread_mutex_t mut;
};

static struct static_map_list smaplst[NUM_DOMAINS_EXTEND];
static struct mem_to_fd_list fdlist;
static struct dma_handle_info dhandles[MAX_DMA_HANDLES];
static int dma_handle_count = 0;

static int fastrpc_unmap_fd(void *buf, size_t size, int fd, int attr);
static __inline void try_map_buffer(struct mem_to_fd *tofd);
static __inline int try_unmap_buffer(struct mem_to_fd *tofd);

int fastrpc_mem_init(void) {
  int ii;

  pthread_mutex_init(&fdlist.mut, 0);
  QList_Ctor(&fdlist.ql);
  memset(dhandles, 0, sizeof(dhandles));
  FOR_EACH_EFFECTIVE_DOMAIN_ID(ii) {
    QList_Ctor(&smaplst[ii].ql);
    pthread_mutex_init(&smaplst[ii].mut, 0);
  }
  return 0;
}

int fastrpc_mem_deinit(void) {
  int ii;

  pthread_mutex_destroy(&fdlist.mut);
  FOR_EACH_EFFECTIVE_DOMAIN_ID(ii) {
    pthread_mutex_destroy(&smaplst[ii].mut);
  }
  return 0;
}

static void *remote_register_fd_attr(int fd, size_t size, int attr) {
  int nErr = AEE_SUCCESS;
  void *po = NULL;
  void *buf = (void *)-1;
  struct mem_to_fd *tofd = 0;

  VERIFY(AEE_SUCCESS == (nErr = fastrpc_init_once()));
  VERIFYC(fd >= 0, AEE_EBADPARM);

  VERIFYC(NULL != (tofd = calloc(1, sizeof(*tofd))), AEE_ENOMEMORY);
  QNode_CtorZ(&tofd->qn);
  VERIFYM((void *)-1 != (buf = mmap(0, size, PROT_NONE,
                                    MAP_ANONYMOUS | MAP_PRIVATE, -1, 0)),
          AEE_ERPC, "Error %x: mmap failed for fd %x, size %x\n", nErr, fd,
          size);
  tofd->buf = buf;
  tofd->size = size;
  tofd->fd = fd;
  tofd->nova = 1;
  tofd->attr = attr;

  pthread_mutex_lock(&fdlist.mut);
  QList_AppendNode(&fdlist.ql, &tofd->qn);
  pthread_mutex_unlock(&fdlist.mut);

  tofd = 0;
  po = buf;
  buf = (void *)-1;
bail:
  if (buf != (void *)-1)
    munmap(buf, size);
  if (tofd) {
    free(tofd);
    tofd = NULL;
  }
  if (nErr != AEE_SUCCESS) {
    if (0 == check_rpc_error(nErr)) {
      FARF(ERROR, "Error 0x%x: remote register fd fails for fd %d, size %zu\n",
           nErr, fd, size);
    }
  }
  return po;
}

void *remote_register_fd(int fd, int size) {
  if (size < 0) {
    FARF(ERROR, "Error: %s failed for invalid size %d", __func__, size);
    return NULL;
  }
  return remote_register_fd_attr(fd, size, 0);
}

void *remote_register_fd2(int fd, size_t size) {
  return remote_register_fd_attr(fd, size, 0);
}

static int remote_register_buf_common(void *buf, size_t size, int fd,
                                      int attr) {
  int nErr = 0;

  VERIFY(AEE_SUCCESS == (nErr = fastrpc_init_once()));

  VERIFYC(NULL != buf, AEE_EBADPARM);
  VERIFYC(size != 0, AEE_EBADPARM);

  if (fd != -1) {
    struct mem_to_fd *tofd;
    int fdfound = 0;
    QNode *pn, *pnn;

    pthread_mutex_lock(&fdlist.mut);
    QLIST_NEXTSAFE_FOR_ALL(&fdlist.ql, pn, pnn) {
      tofd = STD_RECOVER_REC(struct mem_to_fd, qn, pn);
      if (tofd->buf == buf && tofd->size == size && tofd->fd == fd) {
        fdfound = 1;
        if (attr)
          tofd->attr = attr;
        tofd->refcount++;
        break;
      }
    }
    pthread_mutex_unlock(&fdlist.mut);
    if (!fdfound) {
      VERIFYC(NULL != (tofd = calloc(1, sizeof(*tofd))), AEE_ENOMEMORY);
      QNode_CtorZ(&tofd->qn);
      tofd->buf = buf;
      tofd->size = size;
      tofd->fd = fd;
      if (attr)
        tofd->attr = attr;
      tofd->refcount++;
      if (tofd->attr & FASTRPC_ATTR_TRY_MAP_STATIC) {
        try_map_buffer(tofd);
      }
      pthread_mutex_lock(&fdlist.mut);
      QList_AppendNode(&fdlist.ql, &tofd->qn);
      pthread_mutex_unlock(&fdlist.mut);
    }
  } else {
    QNode *pn, *pnn;
    struct mem_to_fd *freefd = NULL;
    pthread_mutex_lock(&fdlist.mut);
    struct mem_to_fd *addr_match_fd = NULL;
    QLIST_NEXTSAFE_FOR_ALL(&fdlist.ql, pn, pnn) {
      struct mem_to_fd *tofd = STD_RECOVER_REC(struct mem_to_fd, qn, pn);
      if (tofd->buf == buf) {
        if (tofd->size == size) {
          tofd->refcount--;
          if (tofd->refcount <= 0) {
            QNode_DequeueZ(&tofd->qn);
            freefd = tofd;
            tofd = NULL;
          }
          break;
        } else {
          addr_match_fd = tofd;
        }
      }
    }
    pthread_mutex_unlock(&fdlist.mut);
    if (freefd) {
      if (freefd->attr & FASTRPC_ATTR_KEEP_MAP) {
        fastrpc_unmap_fd(freefd->buf, freefd->size, freefd->fd, freefd->attr);
      }
      if (freefd->attr & FASTRPC_ATTR_TRY_MAP_STATIC) {
        try_unmap_buffer(freefd);
      }
      if (freefd->nova) {
        munmap(freefd->buf, freefd->size);
      }
      free(freefd);
      freefd = NULL;
    } else if (addr_match_fd) {
      /**
       * When buf deregister size mismatch with register size, deregister buf
       * fails leaving stale fd in fdlist. Bad fd can be attached to other
       * shared buffers in next invoke calls.
       */
      FARF(ERROR,
           "FATAL: Size mismatch between deregister buf (%p) size (%zu) and "
           "registered buf size (%zu) fd %d, bad fd can be attached to other "
           "shared buffers. Use same buffer size as registered buffer",
           buf, size, addr_match_fd->size, addr_match_fd->fd);
      raise(SIGABRT);
    }
  }
bail:
  if (nErr != AEE_SUCCESS) {
    if (0 == check_rpc_error(nErr)) {
      FARF(ERROR,
           "Error 0x%x: remote_register_buf failed buf %p, size %zu, fd 0x%x",
           nErr, buf, size, fd);
    }
  }
  return nErr;
}

void remote_register_buf(void *buf, int size, int fd) {
  remote_register_buf_common(buf, (size_t)size, fd, 0);
}

void remote_register_buf_attr(void *buf, int size, int fd, int attr) {
  remote_register_buf_common(buf, (size_t)size, fd, attr);
}

void remote_register_buf_attr2(void *buf, size_t size, int fd, int attr) {
  remote_register_buf_common(buf, size, fd, attr);
}

int remote_register_dma_handle_attr(int fd, uint32_t len, uint32_t attr) {
  int nErr = AEE_SUCCESS, i;
  int fd_found = 0;

  VERIFY(AEE_SUCCESS == (nErr = fastrpc_init_once()));

  if (attr && attr != FASTRPC_ATTR_NOMAP) {
    FARF(ERROR, "Error: %s failed, unsupported attribute 0x%x", __func__, attr);
    return AEE_EBADPARM;
  }
  VERIFYC(fd >= 0, AEE_EBADPARM);
  VERIFYC(len >= 0, AEE_EBADPARM);

  pthread_mutex_lock(&fdlist.mut);
  for (i = 0; i < dma_handle_count; i++) {
    if (dhandles[i].used && dhandles[i].fd == fd) {
      /* If fd already present in handle list, then just update attribute only
       * if its zero */
      if (!dhandles[i].attr) {
        dhandles[i].attr = attr;
      }
      fd_found = 1;
      dhandles[i].used++;
      break;
    }
  }
  pthread_mutex_unlock(&fdlist.mut);

  if (fd_found) {
    return AEE_SUCCESS;
  }

  pthread_mutex_lock(&fdlist.mut);
  for (i = 0; i < dma_handle_count; i++) {
    if (!dhandles[i].used) {
      dhandles[i].fd = fd;
      dhandles[i].len = len;
      dhandles[i].used = 1;
      dhandles[i].attr = attr;
      break;
    }
  }
  if (i == dma_handle_count) {
    if (dma_handle_count >= MAX_DMA_HANDLES) {
      FARF(ERROR, "Error: %s: DMA handle list is already full (count %d)",
           __func__, dma_handle_count);
      nErr = AEE_EINVHANDLE;
    } else {
      dhandles[dma_handle_count].fd = fd;
      dhandles[dma_handle_count].len = len;
      dhandles[dma_handle_count].used = 1;
      dhandles[dma_handle_count].attr = attr;
      dma_handle_count++;
    }
  }
  pthread_mutex_unlock(&fdlist.mut);

bail:
  if (nErr) {
    if (0 == check_rpc_error(nErr)) {
      FARF(ERROR, "Error 0x%x: %s failed for fd 0x%x, len %d, attr 0x%x", nErr,
           __func__, fd, len, attr);
    }
  }
  return nErr;
}

int remote_register_dma_handle(int fd, uint32_t len) {
  return remote_register_dma_handle_attr(fd, len, 0);
}

void unregister_dma_handle(int fd, uint32_t *len, uint32_t *attr) {
  int i, last_used = 0;

  *len = 0;
  *attr = 0;

  pthread_mutex_lock(&fdlist.mut);
  for (i = 0; i < dma_handle_count; i++) {
    if (dhandles[i].used) {
      if (dhandles[i].fd == fd) {
        dhandles[i].used--;
        *len = dhandles[i].len;
        *attr = dhandles[i].attr;
        if (i == (dma_handle_count - 1) && !dhandles[i].used) {
          dma_handle_count = last_used + 1;
        }
        break;
      } else {
        last_used = i;
      }
    }
  }
  pthread_mutex_unlock(&fdlist.mut);
}

int fdlist_fd_from_buf(void *buf, int bufLen, int *nova, void **base, int *attr,
                       int *ofd) {
  QNode *pn;
  int fd = -1;
  pthread_mutex_lock(&fdlist.mut);
  QLIST_FOR_ALL(&fdlist.ql, pn) {
    if (fd != -1) {
      break;
    } else {
      struct mem_to_fd *tofd = STD_RECOVER_REC(struct mem_to_fd, qn, pn);
      if (STD_BETWEEN(buf, tofd->buf, (unsigned long)tofd->buf + tofd->size)) {
        if (STD_BETWEEN((unsigned long)buf + bufLen - 1, tofd->buf,
                        (unsigned long)tofd->buf + tofd->size)) {
          fd = tofd->fd;
          *nova = tofd->nova;
          *base = tofd->buf;
          *attr = tofd->attr;
        } else {
          pthread_mutex_unlock(&fdlist.mut);
          FARF(ERROR,
               "Error 0x%x: Mismatch in buffer address(%p) or size(%x) to the "
               "registered FD(0x%x), address(%p) and size(%zu)\n",
               AEE_EBADPARM, buf, bufLen, tofd->fd, tofd->buf, tofd->size);
          return AEE_EBADPARM;
        }
      }
    }
  }
  *ofd = fd;
  pthread_mutex_unlock(&fdlist.mut);
  return 0;
}

int fastrpc_mmap(int domain, int fd, void *vaddr, int offset, size_t length,
                 enum fastrpc_map_flags flags) {
  struct fastrpc_map map = {0};
  int nErr = 0, dev = -1, iocErr = 0, attrs = 0, ref = 0;
  uint64_t vaddrout = 0;
  struct static_map *mNode = NULL, *tNode = NULL;
  QNode *pn, *pnn;

  VERIFY(AEE_SUCCESS == (nErr = fastrpc_init_once()));

  FARF(RUNTIME_RPC_HIGH,
       "%s: domain %d fd %d addr %p length 0x%zx flags 0x%x offset 0x%x",
       __func__, domain, fd, vaddr, length, flags, offset);

  /**
   * Mask is applied on "flags" parameter to extract map control flags
   * and SMMU mapping control attributes. Currently no attributes are
   * suppported. It allows future extension of the fastrpc_mmap API
   * for SMMU mapping control attributes.
   */
  attrs = flags & (~FASTRPC_MAP_FLAGS_MASK);
  flags = flags & FASTRPC_MAP_FLAGS_MASK;
  VERIFYC(fd >= 0 && offset == 0 && attrs == 0, AEE_EBADPARM);
  VERIFYC(flags >= 0 && flags < FASTRPC_MAP_MAX &&
              flags != FASTRPC_MAP_RESERVED,
          AEE_EBADPARM);

  // Get domain and open session if not already open
  if (domain == -1) {
    domain = get_current_domain();
  }
  VERIFYC(IS_VALID_EFFECTIVE_DOMAIN_ID(domain), AEE_EBADPARM);
  FASTRPC_GET_REF(domain);
  VERIFY(AEE_SUCCESS == (nErr = fastrpc_session_dev(domain, &dev)));
  VERIFYC(-1 != dev, AEE_ERPC);

  /* Search for mapping in current session static map list */
  pthread_mutex_lock(&smaplst[domain].mut);
  QLIST_NEXTSAFE_FOR_ALL(&smaplst[domain].ql, pn, pnn) {
    tNode = STD_RECOVER_REC(struct static_map, qn, pn);
    if (tNode->map.fd == fd) {
      break;
    }
  }
  pthread_mutex_unlock(&smaplst[domain].mut);

  // Raise error if map found already
  if (tNode) {
    VERIFYM(tNode->map.fd != fd, AEE_EALREADY, "Error: Map already present.");
  }

  // map not found, allocate memory for adding to static map list.
  VERIFYC(NULL != (mNode = calloc(1, sizeof(*mNode))), AEE_ENOMEMORY);

  // Map buffer to DSP process and return limited errors to user
  map.version = 0;
  map.m.fd = fd;
  map.m.offset = offset;
  map.m.flags = flags;
  map.m.vaddrin = (uintptr_t)vaddr;
  map.m.length = (size_t)length;
  map.m.attrs = attrs;
  map.m.vaddrout = 0;
  mNode->map = map.m;
  iocErr = ioctl_mmap(dev, MEM_MAP, flags, attrs, fd, offset, length,
                      (uint64_t)vaddr, &vaddrout);
  if (!iocErr) {
    mNode->map.vaddrout = vaddrout;
    mNode->refs = 1;
    pthread_mutex_lock(&smaplst[domain].mut);
    QList_AppendNode(&smaplst[domain].ql, &mNode->qn);
    pthread_mutex_unlock(&smaplst[domain].mut);
    mNode = NULL;
  } else if (errno == ENOTTY ||
             iocErr == (int)(DSP_AEE_EOFFSET | AEE_EUNSUPPORTED)) {
    nErr = AEE_EUNSUPPORTED;
    goto bail;
  } else {
    nErr = AEE_EFAILED;
    goto bail;
  }
bail:
  FASTRPC_PUT_REF(domain);
  if (nErr) {
    if (iocErr == 0) {
      errno = 0;
    }
    FARF(ERROR,
         "Error 0x%x: %s failed to map buffer fd %d, addr %p, length 0x%zx, "
         "domain %d, flags 0x%x, ioctl ret 0x%x, errno %s",
         nErr, __func__, fd, vaddr, length, domain, flags, iocErr,
         strerror(errno));
  }
  if (mNode) {
    free(mNode);
    mNode = NULL;
  }
  return nErr;
}

int fastrpc_munmap(int domain, int fd, void *vaddr, size_t length) {
  int nErr = 0, dev = -1, iocErr = 0, locked = 0, ref = 0;
  struct static_map *mNode = NULL;
  QNode *pn, *pnn;

  VERIFY(AEE_SUCCESS == (nErr = fastrpc_init_once()));

  FARF(RUNTIME_RPC_HIGH, "%s: domain %d fd %d vaddr %p length 0x%zx", __func__,
       domain, fd, vaddr, length);
  if (domain == -1) {
    domain = get_current_domain();
  }
  VERIFYC(fd >= 0 && IS_VALID_EFFECTIVE_DOMAIN_ID(domain),
          AEE_EBADPARM);
  FASTRPC_GET_REF(domain);
  VERIFY(AEE_SUCCESS == (nErr = fastrpc_session_dev(domain, &dev)));
  /**
   * Search for mapping in current static map list using only file descriptor.
   * Virtual address and length can be used for precise find with additional
   * flags in future.
   */
  pthread_mutex_lock(&smaplst[domain].mut);
  locked = 1;
  QLIST_NEXTSAFE_FOR_ALL(&smaplst[domain].ql, pn, pnn) {
    mNode = STD_RECOVER_REC(struct static_map, qn, pn);
    if (mNode->map.fd == fd) {
      FARF(RUNTIME_RPC_HIGH, "%s: unmap found for fd %d domain %d", __func__,
           fd, domain);
      break;
    }
  }
  VERIFYC(mNode && mNode->map.fd == fd, AEE_ENOSUCHMAP);
  if (mNode->refs > 1) {
    FARF(ERROR, "%s: Attempt to unmap FD %d with %d outstanding references",
         __func__, fd, mNode->refs - 1);
    nErr = AEE_EBADPARM;
    goto bail;
  }
  mNode->refs = 0;
  locked = 0;
  pthread_mutex_unlock(&smaplst[domain].mut);

  iocErr = ioctl_munmap(dev, MEM_UNMAP, 0, 0, fd, mNode->map.length,
                        mNode->map.vaddrout);
  pthread_mutex_lock(&smaplst[domain].mut);
  locked = 1;
  if (iocErr == 0) {
    QNode_DequeueZ(&mNode->qn);
    free(mNode);
    mNode = NULL;
  } else if (errno == ENOTTY || errno == EINVAL) {
    nErr = AEE_EUNSUPPORTED;
  } else {
    mNode->refs = 1;
    nErr = AEE_EFAILED;
  }
bail:
  if (locked == 1) {
    locked = 0;
    pthread_mutex_unlock(&smaplst[domain].mut);
  }
  FASTRPC_PUT_REF(domain);
  if (nErr) {
    if (iocErr == 0) {
      errno = 0;
    }
    FARF(ERROR,
         "Error 0x%x: %s failed fd %d, vaddr %p, length 0x%zx, domain %d, "
         "ioctl ret 0x%x, errno %s",
         nErr, __func__, fd, vaddr, length, domain, iocErr, strerror(errno));
  }
  return nErr;
}

int remote_mem_map(int domain, int fd, int flags, uint64_t vaddr, size_t size,
                   uint64_t *raddr) {
  int nErr = 0;
  int dev = -1, ref = 0;
  uint64_t vaddrout = 0;

  VERIFY(AEE_SUCCESS == (nErr = fastrpc_init_once()));

  FARF(RUNTIME_RPC_HIGH,
       "%s: domain %d fd %d addr 0x%llx size 0x%zx flags 0x%x", __func__,
       domain, fd, vaddr, size, flags);

  VERIFYC(fd >= 0, AEE_EBADPARM);
  VERIFYC(size >= 0, AEE_EBADPARM);
  VERIFYC(flags >= 0 && flags < REMOTE_MAP_MAX_FLAG && raddr != NULL,
          AEE_EBADPARM);
  if (domain == -1) {
    domain = get_current_domain();
  }
  VERIFYC(IS_VALID_EFFECTIVE_DOMAIN_ID(domain), AEE_EBADPARM);

  FASTRPC_GET_REF(domain);
  VERIFY(AEE_SUCCESS == (nErr = fastrpc_session_dev(domain, &dev)));

  nErr = ioctl_mmap(dev, MMAP_64, flags, 0, fd, 0, size, vaddr, &vaddrout);
  *raddr = vaddrout;
bail:
  FASTRPC_PUT_REF(domain);
  if (nErr) {
    nErr = convert_kernel_to_user_error(nErr, errno);
    if (0 == check_rpc_error(nErr)) {
      FARF(ERROR,
           "Error 0x%x: %s failed to map buffer fd %d addr 0x%llx size 0x%zx "
           "domain %d flags %d errno %s",
           nErr, __func__, fd, vaddr, size, domain, flags, strerror(errno));
    }
  }
  return nErr;
}

int remote_mem_unmap(int domain, uint64_t raddr, size_t size) {
  int nErr = 0, dev = -1, ref = 0;

  VERIFY(AEE_SUCCESS == (nErr = fastrpc_init_once()));

  VERIFYC(size >= 0, AEE_EBADPARM);
  VERIFYC(raddr != 0, AEE_EBADPARM);
  FARF(RUNTIME_RPC_HIGH, "%s: domain %d addr 0x%llx size 0x%zx", __func__,
       domain, raddr, size);
  if (domain == -1) {
    domain = get_current_domain();
  }
  VERIFYC(IS_VALID_EFFECTIVE_DOMAIN_ID(domain), AEE_EBADPARM);

  FASTRPC_GET_REF(domain);
  VERIFY(AEE_SUCCESS == (nErr = fastrpc_session_dev(domain, &dev)));

  nErr = ioctl_munmap(dev, MUNMAP_64, 0, 0, -1, size, raddr);
bail:
  FASTRPC_PUT_REF(domain);
  if (nErr) {
    nErr = convert_kernel_to_user_error(nErr, errno);
    if (0 == check_rpc_error(nErr)) {
      FARF(ERROR,
           "Error 0x%x: %s failed to unmap buffer addr 0x%llx size 0x%zx "
           "domain %d errno %s",
           nErr, __func__, raddr, size, domain, strerror(errno));
    }
  }
  return nErr;
}

int remote_mmap64_internal(int fd, uint32_t flags, uint64_t vaddrin,
                           int64_t size, uint64_t *vaddrout) {
  int dev, domain = DEFAULT_DOMAIN_ID, nErr = AEE_SUCCESS, ref = 0;
  uint64_t vaout = 0;

  VERIFY(AEE_SUCCESS == (nErr = fastrpc_init_once()));

  domain = get_current_domain();
  VERIFYC(IS_VALID_EFFECTIVE_DOMAIN_ID(domain), AEE_EBADDOMAIN);
  FASTRPC_GET_REF(domain);
  VERIFY(AEE_SUCCESS == (nErr = fastrpc_session_dev(domain, &dev)));
  VERIFYM(-1 != dev, AEE_ERPC, "Invalid device\n");
  nErr = ioctl_mmap(dev, MMAP_64, flags, 0, fd, 0, size, vaddrin, &vaout);
  *vaddrout = vaout;
bail:
  FASTRPC_PUT_REF(domain);
  if (nErr != AEE_SUCCESS) {
    nErr = convert_kernel_to_user_error(nErr, errno);
    FARF(ERROR,
         "Error 0x%x: %s failed for fd 0x%x of size %lld (flags 0x%x, vaddrin "
         "0x%llx) errno %s\n",
         nErr, __func__, fd, size, flags, vaddrin, strerror(errno));
  }
  return nErr;
}

int remote_mmap64(int fd, uint32_t flags, uint64_t vaddrin, int64_t size,
                  uint64_t *vaddrout) {
  int nErr = AEE_SUCCESS, log = 1;

  VERIFY(AEE_SUCCESS == (nErr = fastrpc_init_once()));

  if (flags != 0) {
    nErr = AEE_EBADPARM;
    goto bail;
  }
  VERIFYC(size >= 0, AEE_EBADPARM);
  VERIFYC(fd >= 0, AEE_EBADPARM);
  VERIFYC(NULL != vaddrout, AEE_EBADPARM);
  nErr = remote_mmap64_internal(fd, flags, vaddrin, size, vaddrout);
  if (nErr == AEE_EBADDOMAIN)
    nErr = AEE_ERPC; // override error code for user
  log = 0;           // so that we wont print error message twice
bail:
  if ((nErr != AEE_SUCCESS) && (log == 1)) {
    FARF(ERROR,
         "Error 0x%x: %s failed for fd 0x%x of size %lld (flags 0x%x, vaddrin "
         "0x%llx)\n",
         nErr, __func__, fd, size, flags, vaddrin);
  }
  return nErr;
}

int remote_mmap(int fd, uint32_t flags, uint32_t vaddrin, int size,
                uint32_t *vaddrout) {
  uint64_t vaddrout_64 = 0;
  int nErr = 0;

  VERIFYC(NULL != vaddrout, AEE_EBADPARM);
  nErr =
      remote_mmap64(fd, flags, (uintptr_t)vaddrin, (int64_t)size, &vaddrout_64);
  *vaddrout = (uint32_t)vaddrout_64;
bail:
  return nErr;
}

int remote_munmap64(uint64_t vaddrout, int64_t size) {
  int dev, domain = DEFAULT_DOMAIN_ID, nErr = AEE_SUCCESS, ref = 0;

  VERIFY(AEE_SUCCESS == (nErr = fastrpc_init_once()));

  domain = get_current_domain();
  VERIFYC(IS_VALID_EFFECTIVE_DOMAIN_ID(domain), AEE_ERPC);

  /* Don't open session in unmap. Return success if device already closed */
  FASTRPC_GET_REF(domain);
  VERIFY(AEE_SUCCESS == (nErr = fastrpc_session_dev(domain, &dev)));
  nErr = ioctl_munmap(dev, MUNMAP_64, 0, 0, -1, size, vaddrout);
bail:
  FASTRPC_PUT_REF(domain);
  if (nErr != AEE_SUCCESS) {
    nErr = convert_kernel_to_user_error(nErr, errno);
    FARF(ERROR,
         "Error 0x%x: %s failed for size %lld (vaddrout 0x%llx) errno %s\n",
         nErr, __func__, size, vaddrout, strerror(errno));
  }
  return nErr;
}

int remote_munmap(uint32_t vaddrout, int size) {
  return remote_munmap64((uintptr_t)vaddrout, (int64_t)size);
}

static int fastrpc_unmap_fd(void *buf, size_t size, int fd, int attr) {
  int nErr = 0;
  int ii, dev = -1;

  FOR_EACH_EFFECTIVE_DOMAIN_ID(ii) {
    nErr = fastrpc_session_get(ii);
    if(!nErr)
      continue;
    nErr = fastrpc_session_dev(ii, &dev);
    if(!nErr) {
      fastrpc_session_put(ii);
      continue;
    }
    nErr = ioctl_munmap(dev, MUNMAP_FD, attr, buf, fd, size, 0);
    if (nErr)
      FARF(RUNTIME_RPC_LOW,
            "unmap_fd: device found %d for domain %d returned %d", dev, ii,
            nErr);
    fastrpc_session_put(ii); 
  }
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR, "Error 0x%x: %s failed for size %zu fd %d errno %s\n", nErr,
         __func__, size, fd, strerror(errno));
  }
  return nErr;
}

/**
 * Map buffer on all domains with open remote session
 *
 * Args:
 *  @tofd : Data structure of the buffer to map
 *
 * Returns : None
 */
static __inline void try_map_buffer(struct mem_to_fd *tofd) {
  int nErr = 0, domain = 0, errcnt = 0;

  FARF(RUNTIME_RPC_HIGH, "%s: fd %d", __func__, tofd->fd);

  /**
   * Tries to create static mapping on remote process of all open sessions.
   * Ignore errors in case of failure
   */
  FOR_EACH_EFFECTIVE_DOMAIN_ID(domain) {
    nErr = fastrpc_mmap(domain, tofd->fd, tofd->buf, 0, tofd->size,
                        FASTRPC_MAP_STATIC);
    if (!nErr) {
      tofd->mapped[domain] = true;
    } else {
      errcnt++;
    }
  }
  if (errcnt) {
    FARF(ERROR, "Error 0x%x: %s failed for fd %d buf %p size 0x%zx errcnt %d",
         nErr, __func__, tofd->fd, tofd->buf, tofd->size, errcnt);
  }
}

/**
 * Unmap buffer on all domains with open remote session
 *
 * Args:
 *  @tofd : Data structure of the buffer to map
 *
 * Returns : None
 */
static __inline int try_unmap_buffer(struct mem_to_fd *tofd) {
  int nErr = 0, domain = 0, errcnt = 0;

  FARF(RUNTIME_RPC_HIGH, "%s: fd %d", __func__, tofd->fd);

  /* Remove static mapping of a buffer for all domains */
  FOR_EACH_EFFECTIVE_DOMAIN_ID(domain) {
    if (tofd->mapped[domain] == false) {
      continue;
    }
    nErr = fastrpc_munmap(domain, tofd->fd, tofd->buf, tofd->size);
    if (!nErr) {
      tofd->mapped[domain] = false;
    } else {
      errcnt++;
      //@TODO: Better way to handle error? probably prevent same FD getting
      //re-used with FastRPC library.
    }
  }
  if (errcnt) {
    FARF(ERROR, "Error 0x%x: %s failed for fd %d buf %p size 0x%zx errcnt %d",
         nErr, __func__, tofd->fd, tofd->buf, tofd->size, errcnt);
  }
  return errcnt;
}

int fastrpc_mem_open(int domain) {
  int nErr = 0;
  QNode *pn, *pnn;
  struct mem_to_fd *tofd = NULL;

  /**
   * Initialize fastrpc session specific informaiton of the fastrpc_mem module
   */
  FARF(RUNTIME_RPC_HIGH, "%s for domain %d", __func__, domain);
  VERIFYC(IS_VALID_EFFECTIVE_DOMAIN_ID(domain), AEE_EBADPARM);

  /* Map buffers with TRY_MAP_STATIC attribute that were allocated
   * and registered before a session was opened on a given domain.
   */
  pthread_mutex_lock(&fdlist.mut);
  QLIST_NEXTSAFE_FOR_ALL(&fdlist.ql, pn, pnn) {
    tofd = STD_RECOVER_REC(struct mem_to_fd, qn, pn);
    if (tofd->attr & FASTRPC_ATTR_TRY_MAP_STATIC &&
        tofd->mapped[domain] == false) {
      nErr = fastrpc_mmap(domain, tofd->fd, tofd->buf, 0, tofd->size,
                          FASTRPC_MAP_STATIC);
      if (!nErr) {
        tofd->mapped[domain] = true;
      }
    }
  }
  nErr = 0; // Try mapping is optional. Ignore error
  pthread_mutex_unlock(&fdlist.mut);
bail:
  if (nErr) {
    FARF(ERROR, "Error 0x%x: %s failed for domain %d", nErr, __func__, domain);
  }
  return nErr;
}

int fastrpc_mem_close(int domain) {
  int nErr = 0;
  struct static_map *mNode;
  struct mem_to_fd *tofd = NULL;
  QNode *pn, *pnn;

  FARF(RUNTIME_RPC_HIGH, "%s for domain %d", __func__, domain);
  VERIFYC(IS_VALID_EFFECTIVE_DOMAIN_ID(domain), AEE_EBADPARM);

  /**
   * Destroy fastrpc session specific information of the fastrpc_mem module.
   * Remove all static mappings of a session
   */
  pthread_mutex_lock(&smaplst[domain].mut);
  do {
    mNode = NULL;
    QLIST_NEXTSAFE_FOR_ALL(&smaplst[domain].ql, pn, pnn) {
      mNode = STD_RECOVER_REC(struct static_map, qn, pn);
      QNode_DequeueZ(&mNode->qn);
      free(mNode);
      mNode = NULL;
    }
  } while (mNode);
  pthread_mutex_unlock(&smaplst[domain].mut);

  // Remove mapping status of static buffers
  pthread_mutex_lock(&fdlist.mut);
  QLIST_NEXTSAFE_FOR_ALL(&fdlist.ql, pn, pnn) {
    tofd = STD_RECOVER_REC(struct mem_to_fd, qn, pn);
    /* This function is called only when remote session is being closed.
     * So no need to do "fastrpc_munmap" here.
     */
    if (tofd->mapped[domain]) {
      tofd->mapped[domain] = false;
    }
  }
  pthread_mutex_unlock(&fdlist.mut);
bail:
  return nErr;
}

int fastrpc_buffer_ref(int domain, int fd, int ref, void **va, size_t *size) {

  int nErr = 0;
  struct static_map *map = NULL;
  QNode *pn, *pnn;

  if (!IS_VALID_EFFECTIVE_DOMAIN_ID(domain)) {
    FARF(ERROR, "%s: invalid domain %d", __func__, domain);
    return AEE_EBADPARM;
  }
  pthread_mutex_lock(&smaplst[domain].mut);

  // Find buffer in the domain's static mapping list
  QLIST_NEXTSAFE_FOR_ALL(&smaplst[domain].ql, pn, pnn) {
    struct static_map *m = STD_RECOVER_REC(struct static_map, qn, pn);
    if (m->map.fd == fd) {
      map = m;
      break;
    }
  }
  VERIFYC(map != NULL, AEE_ENOSUCHMAP);
  VERIFYC(map->refs > 0, AEE_ERPC);

  // Populate output
  if (va) {
    *va = (void *)map->map.vaddrin;
  }
  if (size) {
    *size = map->map.length;
  }

  // Handle refcount
  if (ref == 1) {
    map->refs++;
  } else if (ref == -1) {
    if (map->refs == 1) {
      FARF(ERROR,
           "%s: Attempting to remove last reference to buffer %d on domain %d",
           __func__, fd, domain);
      nErr = AEE_EBADPARM;
      goto bail;
    }
    map->refs--;
  } else {
    VERIFYC(ref == 0, AEE_ERPC);
  }

bail:
  pthread_mutex_unlock(&smaplst[domain].mut);
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR, "Error 0x%x: %s failed (domain %d, fd %d, ref %d)", nErr,
         __func__, domain, fd, ref);
  }
  return nErr;
}
