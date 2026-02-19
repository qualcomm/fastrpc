// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#define FARF_LOW 1

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/types.h>
#include <unistd.h>

#include "AEEQList.h"
#include "AEEStdErr.h"
#include "AEEstd.h"
#include "HAP_farf.h"
#include "apps_std.h"
#include "fastrpc_common.h"
#include "rpcmem.h"
#include "verify.h"
#include "fastrpc_mem.h"
#include <drm/drm.h>
#include "fastrpc_ioctl_drm.h"

#define PAGE_SIZE 4096

#ifndef PAGE_MASK
#define PAGE_MASK ~((uintptr_t)PAGE_SIZE - 1)
#endif

struct dma_heap_allocation_data {
  __u64 len;
  __u32 fd;
  __u32 fd_flags;
  __u64 heap_flags;
};

#define DMA_HEAP_IOC_MAGIC 'H'
#define DMA_HEAP_IOCTL_ALLOC                                                   \
  _IOWR(DMA_HEAP_IOC_MAGIC, 0x0, struct dma_heap_allocation_data)
#define DMA_HEAP_NAME "/dev/dma_heap/system"
static int dmafd = -1;
static int rpcfd = -1;
#ifdef USE_ACCEL_DRIVER
static int qdafd = -1;
#endif
static QList rpclst;
static pthread_mutex_t rpcmt;
struct rpc_info {
  QNode qn;
  void *buf;
  void *aligned_buf;
  int size;
  int fd;           /* DMA-BUF fd for sharing across processes */
  int gem_handle;   /* Original GEM handle for this context */
  int device_fd;    /* Device fd used for this allocation */
  int dma;
};

struct fastrpc_alloc_dma_buf {
  int fd;         /* fd */
  uint32_t flags; /* flags to map with */
  uint64_t size;  /* size */
};

#ifdef USE_ACCEL_DRIVER
/* QDA-specific functions */
static int rpcmem_qda_init(void) {
  qdafd = open(DEFAULT_DEVICE, O_RDWR | O_CLOEXEC);
  if (qdafd >= 0)
    return 0;

  return -1;
}

/* Export GEM handle to shareable DMA-BUF file descriptor using specified device fd */
static int rpcmem_qda_export_handle_to_fd_with_device(int gem_handle, int *dma_fd, int device_fd) {
  struct drm_prime_handle prime_export = {
      .handle = gem_handle,
      .flags = O_CLOEXEC,
      .fd = -1
  };
  
  if (device_fd == -1) {
    return -1;
  }

  int nErr = ioctl(device_fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &prime_export);
  if (nErr) {
    FARF(ERROR, "Error %d: Failed to export GEM handle %d to DMA-BUF fd", 
         errno, gem_handle);
    return -1;
  }
  
  *dma_fd = prime_export.fd;
  return 0;
}

/* Export GEM handle to shareable DMA-BUF file descriptor using global device fd */
static int rpcmem_qda_export_handle_to_fd(int gem_handle, int *dma_fd) {
  return rpcmem_qda_export_handle_to_fd_with_device(gem_handle, dma_fd, qdafd);
}

/* Import DMA-BUF file descriptor to GEM handle using specified device fd */
static int rpcmem_qda_import_fd_to_handle_with_device(int dma_fd, int *gem_handle, int device_fd) {
  struct drm_prime_handle prime_import = {
      .fd = dma_fd,
      .handle = 0
  };
  
  if (device_fd == -1) {
    FARF(ERROR, "Error: No device fd available for PRIME import");
    return -1;
  }

  int nErr = ioctl(device_fd, DRM_IOCTL_PRIME_FD_TO_HANDLE, &prime_import);
  if (nErr) {
    FARF(ERROR, "Error %d: Failed to import DMA-BUF fd %d to GEM handle", 
         errno, dma_fd);
    return -1;
  }
  
  *gem_handle = prime_import.handle;

  return 0;
}

/* Import DMA-BUF file descriptor to GEM handle using global device fd */
static int rpcmem_qda_import_fd_to_handle(int dma_fd, int *gem_handle) {
  return rpcmem_qda_import_fd_to_handle_with_device(dma_fd, gem_handle, qdafd);
}

static void rpcmem_qda_deinit(void) {
  if (qdafd != -1) {
    close(qdafd);
    qdafd = -1;
  }
}

static int rpcmem_qda_alloc(size_t size, int *fd_out) {
  struct drm_qda_gem_create gem_create = {
      .size = size,
      .handle = 0
  };

  int nErr = ioctl(qdafd, DRM_IOCTL_QDA_GEM_CREATE, &gem_create);
  if (nErr) {
    FARF(ERROR, "Error %d: Unable to allocate memory using QDA GEM qdafd %d, size %zu", 
         errno, qdafd, size);
    return -1;
  }

  *fd_out = gem_create.handle;
  return 0;
}

static void *rpcmem_qda_mmap(size_t size, int handle) {
  // Get mmap offset for the GEM handle
  struct drm_qda_gem_mmap_offset mmap_offset = {
      .handle = handle,
      .offset = 0
  };

  int nErr = ioctl(qdafd, DRM_IOCTL_QDA_GEM_MMAP_OFFSET, &mmap_offset);
  if (nErr) {
    FARF(ERROR, "Error %d: Unable to get mmap offset for GEM handle %d", errno, handle);
    return NULL;
  }

  void *ptr = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, qdafd, mmap_offset.offset);
  if (ptr == MAP_FAILED) {
    FARF(ERROR, "mmap failed - errno=%d (%s), size=%zu, qdafd=%d, offset=0x%llx", 
         errno, strerror(errno), size, qdafd, mmap_offset.offset);
    return NULL;
  }

  return ptr;
}

static int rpcmem_qda_is_available(void) {
  return (qdafd != -1);
}

/* 
 * Import a shared DMA-BUF fd from another process (F1) into current process (F2)
 * This creates a new GEM handle in F2's context and maps the buffer for CPU access
 */
static void *rpcmem_qda_import_shared_buffer(int shared_dma_fd, size_t size, int *imported_fd_out) {
  int imported_gem_handle = -1;
  int nErr;
  void *buf = NULL;
  
  if (!rpcmem_qda_is_available()) {
    FARF(ERROR, "QDA driver not available for import");
    return NULL;
  }

  /* Import the DMA-BUF fd to get a GEM handle in our context */
  nErr = rpcmem_qda_import_fd_to_handle(shared_dma_fd, &imported_gem_handle);
  if (nErr) {
    FARF(ERROR, "Failed to import shared DMA-BUF fd %d", shared_dma_fd);
    return NULL;
  }

  /* Map the imported GEM handle for CPU access */
  buf = rpcmem_qda_mmap(size, imported_gem_handle);
  if (!buf) {
    FARF(ERROR, "Failed to mmap imported GEM handle %d", imported_gem_handle);
    return NULL;
  }

  /* Return the original shared DMA-BUF fd for consistency */
  if (imported_fd_out) {
    *imported_fd_out = shared_dma_fd;
  }
  
  return buf;
}
#endif /* USE_ACCEL_DRIVER */

void rpcmem_init() {
  QList_Ctor(&rpclst);
  pthread_mutex_init(&rpcmt, 0);
  pthread_mutex_lock(&rpcmt);

#ifdef USE_ACCEL_DRIVER
  /* Try QDA DRM device first */
  if (rpcmem_qda_init() == 0) {
    /* QDA initialization successful */
    pthread_mutex_unlock(&rpcmt);
    // return;
  }
#endif

  dmafd = open(DMA_HEAP_NAME, O_RDONLY | O_CLOEXEC);
  if (dmafd < 0) {
    FARF(ALWAYS, "Warning %d: Unable to open %s, falling back to fastrpc ioctl\n", errno, DMA_HEAP_NAME);
    /*
     * Application should link proper library as DEFAULT_DOMAIN_ID
     * is used to open rpc device node and not the uri passed by
     * user.
     */
    rpcfd = open_device_node(DEFAULT_DOMAIN_ID);
    if (rpcfd < 0)
      FARF(ALWAYS, "Warning %d: Unable to open fastrpc dev node for domain: %d\n", errno, DEFAULT_DOMAIN_ID);
  }
  pthread_mutex_unlock(&rpcmt);
}

void rpcmem_deinit() {
  pthread_mutex_lock(&rpcmt);

  rpcmem_qda_deinit();

  if (dmafd != -1) {
    close(dmafd);
    dmafd = -1;
  }
  
  if (rpcfd != -1) {
    close(rpcfd);
    rpcfd = -1;
  }
  
  pthread_mutex_unlock(&rpcmt);
  pthread_mutex_destroy(&rpcmt);
}

int rpcmem_set_dmabuf_name(const char *name, int fd, int heapid,
			void *buf, uint32_t rpcflags) {
        // Dummy call where DMABUF is not used
        return 0;
}

int rpcmem_to_fd_internal(void *po) {
  struct rpc_info *rinfo, *rfree = 0;
  QNode *pn, *pnn;

  pthread_mutex_lock(&rpcmt);
  QLIST_NEXTSAFE_FOR_ALL(&rpclst, pn, pnn) {
    rinfo = STD_RECOVER_REC(struct rpc_info, qn, pn);
    if (rinfo->aligned_buf == po) {
      rfree = rinfo;
      break;
    }
  }
  pthread_mutex_unlock(&rpcmt);

  if (rfree)
    return rfree->fd;

  return -1;
}

int rpcmem_to_fd(void *po) { return rpcmem_to_fd_internal(po); }

int rpcmem_to_handle_internal(void *po) {
  struct rpc_info *rinfo, *rfree = 0;
  QNode *pn, *pnn;

  pthread_mutex_lock(&rpcmt);
  QLIST_NEXTSAFE_FOR_ALL(&rpclst, pn, pnn) {
    rinfo = STD_RECOVER_REC(struct rpc_info, qn, pn);
    if (rinfo->aligned_buf == po) {
      rfree = rinfo;
      break;
    }
  }
  pthread_mutex_unlock(&rpcmt);

  if (rfree)
    return rfree->gem_handle;

  return -1;
}

int rpcmem_to_handle(void *po) { return rpcmem_to_handle_internal(po); }

void *rpcmem_alloc_internal2(int heapid, uint32_t flags, size_t size) {
  struct rpc_info *rinfo;
  int nErr = 0, fd = -1, prime_fd = -1;
  struct dma_heap_allocation_data dmabuf = {
      .len = size,
      .fd_flags = O_RDWR | O_CLOEXEC,
  };

#ifdef USE_ACCEL_DRIVER
  if ((qdafd == -1 && dmafd == -1 && rpcfd == -1) || size <= 0) {
    FARF(ERROR,
           "Error: Unable to allocate memory qdafd %d, dmaheap fd %d, rpcfd %d, size "
           "%zu, flags %u",
           qdafd, dmafd, rpcfd, size, flags);
    return NULL;
  }
#else
  if ((dmafd == -1 && rpcfd == -1) || size <= 0) {
    FARF(ERROR,
           "Error: Unable to allocate memory dmaheap fd %d, rpcfd %d, size "
           "%zu, flags %u",
           dmafd, rpcfd, size, flags);
    return NULL;
  }
#endif

  VERIFY(0 != (rinfo = calloc(1, sizeof(*rinfo))));

#ifdef USE_ACCEL_DRIVER
  if (rpcmem_qda_is_available()) {
    /* Use QDA DRM GEM allocation */
    nErr = rpcmem_qda_alloc(size, &fd);
    if (nErr) {
      FARF(ERROR,
           "Error: Unable to allocate memory using QDA GEM, heapid %d, size "
           "%zu, flags %u",
           heapid, size, flags);
      goto bail;
    }
    /* Store GEM handle and device fd for proper context tracking */
    rinfo->gem_handle = fd;
    rinfo->device_fd = qdafd;
  }
#else
  if (dmafd != -1) {
    nErr = ioctl(dmafd, DMA_HEAP_IOCTL_ALLOC, &dmabuf);
    if (nErr) {
      FARF(ERROR,
           "Error %d: Unable to allocate memory dmaheap fd %d, heapid %d, size "
           "%zu, flags %u",
           errno, dmafd, heapid, size, flags);
      goto bail;
    }
    fd = dmabuf.fd;
    rinfo->device_fd = dmafd;
  } else {
    struct fastrpc_ioctl_alloc_dma_buf buf;

    buf.size = size + PAGE_SIZE;
    buf.fd = -1;
    buf.flags = 0;

    nErr = ioctl(rpcfd, FASTRPC_IOCTL_ALLOC_DMA_BUFF, (unsigned long)&buf);
    if (nErr) {
      FARF(ERROR,
           "Error %d: Unable to allocate memory fastrpc fd %d, heapid %d, size "
           "%zu, flags %u",
           errno, rpcfd, heapid, size, flags);
      goto bail;
    }
    fd = buf.fd;
    rinfo->device_fd = rpcfd;
  }
#endif
  /* Map the buffer for CPU access */
#ifdef USE_ACCEL_DRIVER
  if (rpcmem_qda_is_available()) {
    rinfo->buf = rpcmem_qda_mmap(size, fd);
    if (!rinfo->buf) {
      FARF(ERROR, "rpcmem_qda_mmap returned NULL for size=%zu, handle=%d", size, fd);
      goto bail;
    }
    /* 
     * AUTOMATIC PRIME SHARING: Since rpcmem_alloc_internal() is always called in F1 context,
     * we automatically export the GEM handle to DMA-BUF fd for cross-process sharing.
     * This eliminates the need for explicit rpcmem_share_buffer() calls in most cases.
     */
    nErr = rpcmem_qda_export_handle_to_fd(fd, &prime_fd);
    if (nErr) {
      FARF(ERROR, "Error: Failed to export GEM handle %u to prime fd", fd);
      goto bail;
    }
    /* Store the DMA-BUF fd for cross-process sharing, keep GEM handle for context tracking */
    rinfo->fd = prime_fd;        /* DMA-BUF fd for sharing */
    rinfo->gem_handle = fd;      /* Original GEM handle for this context */
  }
#else
  {
    rinfo->buf = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (rinfo->buf == MAP_FAILED) {
      FARF(ERROR, "Standard mmap failed - errno=%d (%s), size=%zu, fd=%d", 
           errno, strerror(errno), size, fd);
      rinfo->buf = NULL;
      goto bail;
    }
    /* For non-QDA allocations, fd is already the shareable fd */
    rinfo->fd = fd;
    rinfo->gem_handle = -1;  /* Not applicable for non-QDA */
  }
#endif
  rinfo->aligned_buf =
      (void *)(((uintptr_t)rinfo->buf /*+ PAGE_SIZE*/) & PAGE_MASK);
  rinfo->aligned_buf = rinfo->buf;
  rinfo->size = size;
  pthread_mutex_lock(&rpcmt);
  QList_AppendNode(&rpclst, &rinfo->qn);
  pthread_mutex_unlock(&rpcmt);
  
#ifdef USE_ACCEL_DRIVER
  if (rpcmem_qda_is_available()) {
    FARF(ALWAYS, "Allocated memory from QDA GEM handle %d ptr %p orig ptr %p\n",
         rinfo->fd, rinfo->aligned_buf, rinfo->buf);
  }
#else
  {
    FARF(ALWAYS, "Allocated memory from DMA heap fd %d ptr %p orig ptr %p\n",
         rinfo->fd, rinfo->aligned_buf, rinfo->buf);
  }
#endif
  remote_register_buf(rinfo->buf, rinfo->size, rinfo->fd);
  return rinfo->aligned_buf;
bail:
  if (nErr) {
    if (rinfo) {
      if (rinfo->buf) {
        free(rinfo->buf);
      }
      free(rinfo);
    }
  }
  return NULL;
}

void rpcmem_free_internal(void *po) {
  struct rpc_info *rinfo, *rfree = 0;
  QNode *pn, *pnn;

  pthread_mutex_lock(&rpcmt);
  QLIST_NEXTSAFE_FOR_ALL(&rpclst, pn, pnn) {
    rinfo = STD_RECOVER_REC(struct rpc_info, qn, pn);
    if (rinfo->aligned_buf == po) {
      rfree = rinfo;
      QNode_Dequeue(&rinfo->qn);
      break;
    }
  }
  pthread_mutex_unlock(&rpcmt);
  
  if (rfree) {
    remote_register_buf(rfree->buf, rfree->size, -1);
    munmap(rfree->buf, rfree->size);
    close(rfree->fd);
    free(rfree);
  } else {
    FARF(ALWAYS, "rpcmem_free_internal - Buffer po=%p NOT FOUND in tracking list", po);
  }

  return;
}

void rpcmem_free(void *po) { rpcmem_free_internal(po); }

void *rpcmem_alloc3(int heapid, uint32_t flags, int size) {
  return rpcmem_alloc_internal2(heapid, flags, size);
}

void *rpcmem_alloc_internal(int heapid, uint32_t flags, size_t size) {
  struct rpc_info *rinfo;
  int nErr = 0, fd = -1;
  struct dma_heap_allocation_data dmabuf = {
      .len = size,
      .fd_flags = O_RDWR | O_CLOEXEC,
  };
  struct stat st;

  // Validate inputs
  if (size <= 0) {
    FARF(ERROR, "Error: Invalid size %zu for rpcmem_alloc_internal", size);
    return NULL;
  }

  // Ensure DMA heap is available
  if (dmafd == -1) {
    pthread_mutex_lock(&rpcmt);
    dmafd = open(DMA_HEAP_NAME, O_RDONLY | O_CLOEXEC);
    pthread_mutex_unlock(&rpcmt);
    if (dmafd < 0) {
      FARF(ERROR, "Error %d: Unable to open %s for rpcmem_alloc_internal", errno, DMA_HEAP_NAME);
      return NULL;
    }
  }

  // Verify dmafd is still valid
  if (fstat(dmafd, &st) < 0) {
    // Try to reopen
    pthread_mutex_lock(&rpcmt);
    dmafd = open(DMA_HEAP_NAME, O_RDONLY | O_CLOEXEC);
    pthread_mutex_unlock(&rpcmt);
    if (dmafd < 0) {
      return NULL;
    }
  } else {
    FARF(ALWAYS, "dmafd %d is VALID (st_dev=%lu, st_ino=%lu, st_mode=0x%x)", 
         dmafd, (unsigned long)st.st_dev, (unsigned long)st.st_ino, st.st_mode);
  }

  VERIFY(0 != (rinfo = calloc(1, sizeof(*rinfo))));

  nErr = ioctl(dmafd, DMA_HEAP_IOCTL_ALLOC, &dmabuf);
  if (nErr) {
    FARF(ERROR,
         "Error %d: Unable to allocate memory from system heap, heapid %d, size %zu, flags %u, dmafd=%d",
         errno, heapid, size, flags, dmafd);
    goto bail;
  }
  fd = dmabuf.fd;

  // Map the buffer for CPU access
  rinfo->buf = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (rinfo->buf == MAP_FAILED) {
    FARF(ERROR, "Error %d: mmap failed for system heap allocation, size %zu, fd=%d", errno, size, fd);
    rinfo->buf = NULL;
    goto bail;
  }
  
  // Store buffer information
  rinfo->fd = fd;
  rinfo->aligned_buf = (void *)(((uintptr_t)rinfo->buf) & PAGE_MASK);
  rinfo->aligned_buf = rinfo->buf;  // Already aligned
  rinfo->size = size;
  rinfo->gem_handle = -1;  // Not applicable for DMA heap
  rinfo->device_fd = dmafd;
  rinfo->dma = 1;  // Mark as DMA heap allocation

  pthread_mutex_lock(&rpcmt);
  QList_AppendNode(&rpclst, &rinfo->qn);
  pthread_mutex_unlock(&rpcmt);
  
  FARF(RUNTIME_RPC_HIGH, 
       "Allocated memory from system heap (rpcmem_alloc_internal) fd %d ptr %p size %zu\n",
       rinfo->fd, rinfo->aligned_buf, size);
  
#ifdef USE_ACCEL_DRIVER
  /* 
   * PRIME sharing fix: If QDA is available, import the DMA-BUF fd to create a 
   * valid GEM handle in current context for driver lookup to succeed.
   * This allows system heap allocations to work with QDA driver.
   */
  if (rpcmem_qda_is_available()) {
    int domain = get_current_domain();
    int dev = get_device_fd(domain);
    
    if (dev > 0 && rinfo->fd > 0) {
      int imported_gem_handle = -1;
      int import_ret = rpcmem_qda_import_fd_to_handle_with_device(rinfo->fd, &imported_gem_handle, dev);
      if (import_ret == 0 && imported_gem_handle >= 0) {
        /* Successfully imported - update GEM handle for driver operations */
        rinfo->gem_handle = imported_gem_handle;
        rinfo->device_fd = dev;
      } else {
        FARF(ALWAYS, "Buffer import failed (ret=%d), "
             "continuing with original system heap buffer (gem_handle=-1)", import_ret);
        /* Continue with gem_handle=-1 */
      }
    }
  }
#endif

  remote_register_buf(rinfo->buf, rinfo->size, rinfo->gem_handle);

  return rinfo->aligned_buf;

bail:
  if (rinfo) {
    if (rinfo->buf && rinfo->buf != MAP_FAILED) {
      munmap(rinfo->buf, size);
    }
    if (fd >= 0) {
      close(fd);
    }
    free(rinfo);
  }
  return NULL;
}

void *rpcmem_alloc(int heapid, uint32_t flags, int size) {
  return rpcmem_alloc_internal(heapid, flags, size);
}

void rpcmem_deinit_internal() { 
  rpcmem_deinit(); 
}

void rpcmem_init_internal() { rpcmem_init(); }

#ifdef USE_ACCEL_DRIVER
/*
 * Public API: Export GEM handle to shareable DMA-BUF fd
 * 
 * @param handle: GEM handle to export
 * @param fd_out: Output parameter for the DMA-BUF file descriptor
 * @return: 0 on success, negative error code on failure
 */
int rpcmem_export_handle_to_fd(int handle, int *fd_out) {
  if (handle < 0 || !fd_out) {
    FARF(ERROR, "Invalid parameters for handle export: handle=%d, fd_out=%p", handle, fd_out);
    return -1;
  }
  
  return rpcmem_qda_export_handle_to_fd(handle, fd_out);
}

/*
 * Public API: Import DMA-BUF fd to GEM handle in current context
 * 
 * @param fd: DMA-BUF file descriptor to import
 * @param handle_out: Output parameter for the GEM handle
 * @return: 0 on success, negative error code on failure
 */
int rpcmem_import_fd_to_handle(int fd, int *handle_out) {
  if (fd < 0 || !handle_out) {
    FARF(ERROR, "Invalid parameters for fd import: fd=%d, handle_out=%p", fd, handle_out);
    return -1;
  }
  
  return rpcmem_qda_import_fd_to_handle(fd, handle_out);
}

/*
 * Public API: Share an rpcmem buffer with another process/context
 * 
 * @param buf: Buffer pointer returned by rpcmem_alloc
 * @param fd_out: Output parameter for shareable DMA-BUF file descriptor
 * @return: 0 on success, negative error code on failure
 */
int rpcmem_share_buffer(void *buf, int *fd_out) {
  struct rpc_info *rinfo, *rfound = NULL;
  QNode *pn, *pnn;
  
  if (!buf || !fd_out) {
    FARF(ERROR, "Invalid parameters for buffer sharing: buf=%p, fd_out=%p", buf, fd_out);
    return -1;
  }
  
  /* Find the rpc_info for this buffer */
  pthread_mutex_lock(&rpcmt);
  QLIST_NEXTSAFE_FOR_ALL(&rpclst, pn, pnn) {
    rinfo = STD_RECOVER_REC(struct rpc_info, qn, pn);
    if (rinfo->aligned_buf == buf) {
      rfound = rinfo;
      break;
    }
  }
  pthread_mutex_unlock(&rpcmt);
  
  if (!rfound) {
    FARF(ERROR, "Buffer %p not found in rpcmem tracking list", buf);
    return -1;
  }
  
  /* For QDA buffers, the fd is already the shareable DMA-BUF fd */
  if (rpcmem_qda_is_available() && rfound->gem_handle != -1) {
    *fd_out = rfound->fd;
    return 0;
  }
  
  /* For non-QDA buffers, fd is already shareable */
  *fd_out = rfound->fd;
  return 0;
}

/*
 * Internal function: Import a shared buffer using a specific device fd
 * This allows F2 to import using its own device context
 * 
 * @param fd: DMA-BUF fd received from F1 (the allocator process)
 * @param size: Size of the shared buffer
 * @param device_fd: Device fd to use for import (F2's context)
 * @param buf_out: Output parameter for the mapped buffer pointer
 * @return: 0 on success, negative error code on failure
 */
int rpcmem_import_shared_buffer_with_device(int fd, size_t size, int device_fd, void **buf_out) {
  struct rpc_info *rinfo;
  int imported_fd = -1;
  void *buf = NULL;
  int nErr = 0;
  int imported_gem_handle = -1;
  
  if (size <= 0 || fd < 0 || device_fd < 0 || !buf_out) {
    FARF(ERROR, "Invalid parameters for buffer import: fd=%d, size=%zu, device_fd=%d, buf_out=%p", 
         fd, size, device_fd, buf_out);
    return -1;
  }
  
  *buf_out = NULL;

  /* Import the DMA-BUF fd to get a GEM handle in F2's context */
  nErr = rpcmem_qda_import_fd_to_handle_with_device(fd, &imported_gem_handle, device_fd);
  if (nErr) {
    FARF(ERROR, "Failed to import shared DMA-BUF fd %d using device_fd %d", fd, device_fd);
    return -1;
  }

  /* For imported buffers, try to map using the original DMA-BUF fd first */
  buf = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (buf == MAP_FAILED) {
    
    /* Fallback to GEM mmap approach */
    struct drm_qda_gem_mmap_offset mmap_offset = {
        .handle = imported_gem_handle,
        .offset = 0
    };

    nErr = ioctl(device_fd, DRM_IOCTL_QDA_GEM_MMAP_OFFSET, &mmap_offset);
    if (nErr) {
      FARF(ERROR, "Error %d: Unable to get mmap offset for imported GEM handle %d using device_fd %d", 
           errno, imported_gem_handle, device_fd);
      return -1;
    }

    buf = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, device_fd, mmap_offset.offset);
    if (buf == MAP_FAILED) {
      FARF(ERROR, "GEM mmap failed - errno=%d (%s), size=%zu, device_fd=%d, offset=0x%llx", 
           errno, strerror(errno), size, device_fd, mmap_offset.offset);
      return -1;
    }
  }

  /* Create rpc_info entry to track this imported buffer */
  VERIFY(0 != (rinfo = calloc(1, sizeof(*rinfo))));
  
  rinfo->buf = buf;
  rinfo->aligned_buf = buf;  /* Assume already aligned from import */
  rinfo->size = size;
  rinfo->fd = -1;  /* Store the original DMA-BUF fd (NOT the GEM handle) - this is what should be closed */
  rinfo->gem_handle = imported_gem_handle;  /* Store the imported GEM handle for reference only */
  rinfo->device_fd = device_fd; /* Store F2's device fd used for import */
  
  /* Add to our tracking list */
  pthread_mutex_lock(&rpcmt);
  QList_AppendNode(&rpclst, &rinfo->qn);
  pthread_mutex_unlock(&rpcmt);
  
  /* Register with remote subsystem using the imported GEM handle */
  remote_register_buf(rinfo->buf, rinfo->size, rinfo->gem_handle);
  
  FARF(ALWAYS, "Successfully imported shared buffer fd=%d -> ptr=%p, gem_handle=%d, using device_fd=%d", 
       fd, buf, imported_gem_handle, device_fd);
  *buf_out = buf;
  return 0;

bail:
  if (nErr) {
    FARF(ERROR, "Error 0x%x in rpcmem_import_shared_buffer_with_device", nErr);
  }
  return nErr;
}

/*
 * Public API: Import a shared buffer from another process
 * This function allows F2 to import a DMA-BUF fd shared by F1
 * Uses the global qdafd for backward compatibility
 * 
 * @param fd: DMA-BUF fd received from F1 (the allocator process)
 * @param size: Size of the shared buffer
 * @param buf_out: Output parameter for the mapped buffer pointer
 * @return: 0 on success, negative error code on failure
 */
int rpcmem_import_shared_buffer(int fd, size_t size, void **buf_out) {
  return rpcmem_import_shared_buffer_with_device(fd, size, qdafd, buf_out);
}
#endif /* USE_ACCEL_DRIVER */
