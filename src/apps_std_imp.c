// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifdef _WIN32
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif // _CRT_SECURE_NO_WARNINGS

#pragma warning(disable : 4996)
#define strtok_r strtok_s
#define S_ISDIR(mode) (mode & S_IFDIR)
#endif //_WIN32

#ifndef VERIFY_PRINT_ERROR
#define VERIFY_PRINT_ERROR
#endif // VERIFY_PRINT_ERROR
#ifndef VERIFY_PRINT_ERROR_ALWAYS
#define VERIFY_PRINT_ERROR_ALWAYS
#endif // VERIFY_PRINT_ERROR_ALWAYS
#define FARF_ERROR 1
#define FARF_LOW 1

#ifndef VERIFY_PRINT_WARN
#define VERIFY_PRINT_WARN
#endif // VERIFY_PRINT_WARN

#define FARF_CRITICAL 1

#include "AEEQList.h"
#include "AEEStdErr.h"
#include "AEEstd.h"
#include "HAP_farf.h"
#include "apps_std.h"
#include "apps_std_internal.h"
#include "fastrpc_internal.h"
#include "fastrpc_trace.h"
#include "platform_libs.h"
#include "remote.h"
#include "rpcmem_internal.h"
#include "verify.h"
#include <dirent.h>
#include <limits.h>
#include <dlfcn.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifndef _WIN32
#include <unistd.h>
#endif // _WiN32

#ifndef C_ASSERT
#define C_ASSERT(test)                                                         \
  switch (0) {                                                                 \
  case 0:                                                                      \
  case test:;                                                                  \
  }
#endif // C_ASSERT

#define APPS_FD_BASE 100
#define ERRNO (errno ? errno : nErr ? nErr : -1)
#define APPS_STD_STREAM_FILE 1
#define APPS_STD_STREAM_BUF 2

#define ION_HEAP_ID_QSEECOM 27

#define OEM_CONFIG_FILE_NAME "oemconfig.so"
#define TESTSIG_FILE_NAME "testsig"
#define RPC_VERSION_FILE_NAME "librpcversion_skel.so"

#define FREEIF(pv)                                                             \
  do {                                                                         \
    if (pv) {                                                                  \
      void *tmp = (void *)pv;                                                  \
      pv = 0;                                                                  \
      free(tmp);                                                               \
      tmp = 0;                                                                 \
    }                                                                          \
  } while (0)

struct apps_std_buf_info {
  char *fbuf;
  int flen;
  int pos;
};

struct apps_std_info {
  QNode qn;
  int type;
  union {
    FILE *stream;
    struct apps_std_buf_info binfo;
  } u;
  apps_std_FILE fd;
};

/*
 * Member of the linked list of valid directory handles
 */
struct apps_std_dir_info {
  QNode qn;
  uint64_t handle;
};

static QList apps_std_qlst;

/* Linked list that tracks list of all valid dir handles */
static QList apps_std_dirlist;
static pthread_mutex_t apps_std_mt;
extern const char *SUBSYSTEM_NAME[];
struct mem_io_to_fd {
  QNode qn;
  int size;
  int fd;
  int fdfile;
  FILE *stream;
  void *buf;
};

struct mem_io_fd_list {
  QList ql;
  pthread_mutex_t mut;
};

static struct mem_io_fd_list fdlist;

int setenv(const char *name, const char *value, int overwrite);
int unsetenv(const char *name);

int apps_std_get_dirinfo(const apps_std_DIR *dir,
                         struct apps_std_dir_info **pDirinfo) {
  int nErr = AEE_SUCCESS;
  QNode *pn = NULL, *pnn = NULL;
  struct apps_std_dir_info *dirinfo = 0;
  bool match = false;

  pthread_mutex_lock(&apps_std_mt);
  QLIST_NEXTSAFE_FOR_ALL(&apps_std_dirlist, pn, pnn) {
    dirinfo = STD_RECOVER_REC(struct apps_std_dir_info, qn, pn);
    if (dirinfo && dirinfo->handle == dir->handle) {
      match = true;
      break;
    }
  }
  pthread_mutex_unlock(&apps_std_mt);

  if (match) {
    *pDirinfo = dirinfo;
  } else {
    nErr = ESTALE;
    VERIFY_EPRINTF(
        "Error 0x%x: %s: stale directory handle 0x%llx passed by DSP\n", nErr,
        __func__, dir->handle);
    goto bail;
  }
bail:
  return nErr;
}

int apps_std_init(void) {
  QList_Ctor(&apps_std_qlst);
  QList_Ctor(&apps_std_dirlist);
  pthread_mutex_init(&apps_std_mt, 0);
  pthread_mutex_init(&fdlist.mut, 0);
  QList_Ctor(&fdlist.ql);
  return AEE_SUCCESS;
}

void apps_std_deinit(void) {
  pthread_mutex_destroy(&apps_std_mt);
  pthread_mutex_destroy(&fdlist.mut);
}

PL_DEFINE(apps_std, apps_std_init, apps_std_deinit);

static void apps_std_FILE_free(struct apps_std_info *sfree) {

  FARF(RUNTIME_RPC_LOW, "Entering %s", __func__);
  pthread_mutex_lock(&apps_std_mt);
  QNode_Dequeue(&sfree->qn);
  pthread_mutex_unlock(&apps_std_mt);

  FREEIF(sfree);
  FARF(RUNTIME_RPC_LOW, "Exiting %s", __func__);
  return;
}

static int apps_std_FILE_alloc(FILE *stream, apps_std_FILE *fd) {
  struct apps_std_info *sinfo = 0, *info;
  QNode *pn = 0;
  apps_std_FILE prevfd = APPS_FD_BASE - 1;
  int nErr = AEE_SUCCESS;

  FARF(RUNTIME_RPC_LOW, "Entering %s", __func__);
  VERIFYC(0 != (sinfo = calloc(1, sizeof(*sinfo))), ENOMEM);
  QNode_CtorZ(&sinfo->qn);
  sinfo->type = APPS_STD_STREAM_FILE;
  pthread_mutex_lock(&apps_std_mt);
  pn = QList_GetFirst(&apps_std_qlst);
  if (pn) {
    info = STD_RECOVER_REC(struct apps_std_info, qn, pn);
    prevfd = info->fd;
    QLIST_FOR_REST(&apps_std_qlst, pn) {
      info = STD_RECOVER_REC(struct apps_std_info, qn, pn);
      if (info->fd != prevfd + 1) {
        sinfo->fd = prevfd + 1;
        QNode_InsPrev(pn, &sinfo->qn);
        break;
      }
      prevfd = info->fd;
    }
  }
  if (!QNode_IsQueuedZ(&sinfo->qn)) {
    sinfo->fd = prevfd + 1;
    QList_AppendNode(&apps_std_qlst, &sinfo->qn);
  }
  pthread_mutex_unlock(&apps_std_mt);

  sinfo->u.stream = stream;
  *fd = sinfo->fd;

bail:
  if (nErr) {
    FREEIF(sinfo);
    VERIFY_EPRINTF("Error 0x%x: apps_std_FILE_alloc failed, errno %s \n", nErr,
                   strerror(nErr));
  }
  FARF(RUNTIME_RPC_LOW, "Exiting %s fd 0x%x err %d", __func__, *fd, nErr);
  return nErr;
}

static int apps_std_FILE_get(apps_std_FILE fd, struct apps_std_info **info) {
  struct apps_std_info *sinfo = 0;
  QNode *pn, *pnn;
  int nErr = EBADF;

  FARF(RUNTIME_RPC_LOW, "Entering %s", __func__);
  pthread_mutex_lock(&apps_std_mt);
  QLIST_NEXTSAFE_FOR_ALL(&apps_std_qlst, pn, pnn) {
    sinfo = STD_RECOVER_REC(struct apps_std_info, qn, pn);
    if (sinfo->fd == fd) {
      *info = sinfo;
      nErr = AEE_SUCCESS;
      break;
    }
  }
  pthread_mutex_unlock(&apps_std_mt);
  if (nErr) {
    VERIFY_EPRINTF(
        "Error 0x%x: apps_std_FILE_get failed for fd 0x%x, errno %s \n", nErr,
        fd, strerror(nErr));
  }
  FARF(RUNTIME_RPC_LOW, "Exiting %s fd 0x%x err %d", __func__, fd, nErr);
  return nErr;
}

static void apps_std_FILE_set_buffer_stream(struct apps_std_info *sinfo,
                                            char *fbuf, int flen, int pos) {
  pthread_mutex_lock(&apps_std_mt);
  fclose(sinfo->u.stream);
  sinfo->type = APPS_STD_STREAM_BUF;
  sinfo->u.binfo.fbuf = fbuf;
  sinfo->u.binfo.flen = flen;
  sinfo->u.binfo.pos = pos;
  pthread_mutex_unlock(&apps_std_mt);
}

__QAIC_IMPL_EXPORT int
__QAIC_IMPL(apps_std_fopen)(const char *name, const char *mode,
                            apps_std_FILE *psout) __QAIC_IMPL_ATTRIBUTE {
  int nErr = AEE_SUCCESS;
  FILE *stream = NULL;
  uint64_t tdiff = 0;

  if (name) {
    FASTRPC_ATRACE_BEGIN_L("%s for %s in %s mode", __func__, name, mode);
  }
  FARF(RUNTIME_RPC_LOW, "Entering %s", __func__);
  errno = 0;
  VERIFYC(name != NULL, AEE_EBADPARM);
  PROFILE_ALWAYS(&tdiff, stream = fopen(name, mode););
  if (stream) {
    FASTRPC_ATRACE_END_L("%s done, fopen for %s in mode %s done in %" PRIu64
                         " us, fd 0x%x error_code 0x%x",
                         __func__, name, mode, tdiff, *psout, nErr);
    return apps_std_FILE_alloc(stream, psout);
  } else {
    nErr = ERRNO;
  }
bail:
  if (nErr != AEE_SUCCESS) {
    // Ignoring this error, as fopen happens on all ADSP_LIBRARY_PATHs
    VERIFY_IPRINTF("Error 0x%x: %s failed for %s (%s)\n", nErr, __func__, name,
                   strerror(ERRNO));
  }
  FARF(RUNTIME_RPC_LOW, "Exiting %s name %s mode %s err %d", __func__, name,
       mode, nErr);
  FASTRPC_ATRACE_END();
  return nErr;
}

__QAIC_IMPL_EXPORT int
__QAIC_IMPL(apps_std_fopen_fd)(const char *name, const char *mode, int *fd,
                               int *len) __QAIC_IMPL_ATTRIBUTE {
  int nErr = AEE_SUCCESS;
  struct stat statbuf;
  void *source = NULL;
  int sz = 0, fdfile = 0;
  struct mem_io_to_fd *tofd = 0;
  int domain = get_current_domain();
  FILE *stream = NULL;
  bool fopen_fail = false, mmap_pass = false;
  uint64_t fopen_time = 0, read_time = 0, rpc_alloc_time = 0, mmap_time = 0;

  FASTRPC_ATRACE_BEGIN_L("%s for %s in %s mode", __func__, name, mode);
  FARF(RUNTIME_RPC_LOW, "Entering %s", __func__);
  errno = 0;
  VERIFYC(name != NULL, AEE_EBADPARM);
  PROFILE_ALWAYS(&fopen_time, stream = fopen(name, mode););
  if (!stream) {
    fopen_fail = true;
    nErr = ERRNO;
    goto bail;
  }
  VERIFYC(-1 != (fdfile = fileno(stream)), ERRNO);
  VERIFYC(0 == fstat(fdfile, &statbuf), ERRNO);
  PROFILE_ALWAYS(
      &rpc_alloc_time,
      source = rpcmem_alloc_internal(0, RPCMEM_HEAP_DEFAULT, statbuf.st_size););
  VERIFYC(0 != source, AEE_ENORPCMEMORY);
  PROFILE_ALWAYS(&read_time, sz = read(fdfile, source, statbuf.st_size););
  if (sz < 0) {
    nErr = AEE_EFILE;
    goto bail;
  }
  *fd = rpcmem_to_fd(source);
  *len = statbuf.st_size;
  PROFILE_ALWAYS(&mmap_time, nErr = fastrpc_mmap(domain, *fd, source, 0, *len,
                                                 FASTRPC_MAP_FD));
  VERIFY(AEE_SUCCESS == nErr);
  VERIFYC(NULL != (tofd = calloc(1, sizeof(*tofd))), AEE_ENOMEMORY);
  QNode_CtorZ(&tofd->qn);
  tofd->size = *len;
  tofd->fd = *fd;
  tofd->fdfile = fdfile;
  tofd->stream = stream;
  tofd->buf = source;
  pthread_mutex_lock(&fdlist.mut);
  QList_AppendNode(&fdlist.ql, &tofd->qn);
  pthread_mutex_unlock(&fdlist.mut);
bail:
  if (nErr != AEE_SUCCESS) {
    if (stream) {
      fclose(stream);
    }
    // Ignore fopen error, as fopen happens on all ADSP_LIBRARY_PATHs
    if (!fopen_fail) {
      FARF(ERROR, "Error 0x%x: %s failed for %s (%s)\n", nErr, __func__, name,
           strerror(ERRNO));
    }
    if (mmap_pass) {
      fastrpc_munmap(domain, *fd, source, *len);
    }
    FREEIF(tofd);
    if (source) {
      rpcmem_free_internal(source);
      source = NULL;
    }
  }
  FARF(RUNTIME_RPC_LOW, "Exiting %s name %s mode %s err %d", __func__, name,
       mode, nErr);
  FASTRPC_ATRACE_END_L("%s: done for %s with fopen:%" PRIu64 "us, read:%" PRIu64
                       "us, rpc_alloc:%" PRIu64 "us, mmap:%" PRIu64 "us",
                       __func__, name, fopen_time, read_time, rpc_alloc_time,
                       mmap_time);
  FARF(RUNTIME_RPC_CRITICAL,
       "%s: done for %s with fopen:%" PRIu64 "us, read:%" PRIu64
       "us, rpc_alloc:%" PRIu64 "us, mmap:%" PRIu64 "us, fd 0x%x error_code 0x%x",
       __func__, name, fopen_time, read_time, rpc_alloc_time, mmap_time, *fd, nErr);
  return nErr;
}
__QAIC_IMPL_EXPORT int
__QAIC_IMPL(apps_std_freopen)(apps_std_FILE sin, const char *name,
                              const char *mode,
                              apps_std_FILE *psout) __QAIC_IMPL_ATTRIBUTE {
  int nErr = AEE_SUCCESS;
  struct apps_std_info *sinfo = 0;
  FILE *stream;

  if (name) {
    FASTRPC_ATRACE_BEGIN_L("%s for %s (fd 0x%x) in %s mode", __func__, name,
                           sin, mode);
  }
  FARF(RUNTIME_RPC_LOW, "Entering %s", __func__);
  errno = 0;
  VERIFYC(name != NULL, AEE_EBADPARM);
  VERIFY(0 == (nErr = apps_std_FILE_get(sin, &sinfo)));
  VERIFYC(sinfo->type == APPS_STD_STREAM_FILE, EBADF);
  stream = freopen(name, mode, sinfo->u.stream);
  if (stream) {
    FARF(RUNTIME_RPC_HIGH, "freopen success: %s %x\n", name, stream);
    return apps_std_FILE_alloc(stream, psout);
  } else {
    nErr = ERRNO;
  }
bail:
  if (nErr != AEE_SUCCESS) {
    VERIFY_EPRINTF(
        "Error 0x%x: freopen for %s mode %s sin %x failed. errno: %s\n", nErr,
        name, mode, sin, strerror(nErr));
  }
  FARF(RUNTIME_RPC_LOW, "Exiting %s name %s mode %s sin %x err %d", __func__,
       name, mode, sin, nErr);
  FASTRPC_ATRACE_END();
  return nErr;
}

__QAIC_IMPL_EXPORT int
__QAIC_IMPL(apps_std_fflush)(apps_std_FILE sin) __QAIC_IMPL_ATTRIBUTE {
  int nErr = AEE_SUCCESS;
  struct apps_std_info *sinfo = 0;

  FASTRPC_ATRACE_BEGIN_L("%s for file with fd 0x%x", __func__, sin);
  FARF(RUNTIME_RPC_LOW, "Entering %s", __func__);
  VERIFY(0 == (nErr = apps_std_FILE_get(sin, &sinfo)));
  if (sinfo->type == APPS_STD_STREAM_FILE) {
    VERIFYC(0 == fflush(sinfo->u.stream), ERRNO);
  }
bail:
  if (nErr != AEE_SUCCESS) {
    VERIFY_EPRINTF("Error 0x%x: fflush for %x failed. errno: %s\n", nErr, sin,
                   strerror(nErr));
  }
  FARF(RUNTIME_RPC_LOW, "Exiting %s sin %x err %d", __func__, sin, nErr);
  FASTRPC_ATRACE_END();
  return nErr;
}

__QAIC_IMPL_EXPORT int
__QAIC_IMPL(apps_std_fclose)(apps_std_FILE sin) __QAIC_IMPL_ATTRIBUTE {
  int nErr = AEE_SUCCESS;
  struct apps_std_info *sinfo = 0;
  uint64_t tdiff = 0;

  FASTRPC_ATRACE_BEGIN_L("%s for file with fd 0x%x", __func__, sin);
  FARF(RUNTIME_RPC_LOW, "Entering %s", __func__);
  errno = 0;
  VERIFY(0 == (nErr = apps_std_FILE_get(sin, &sinfo)));
  if (sinfo->type == APPS_STD_STREAM_FILE) {
      PROFILE_ALWAYS(&tdiff,
      nErr = fclose(sinfo->u.stream);
      );
      VERIFYC(nErr == AEE_SUCCESS, ERRNO);
  } else {
    if (sinfo->u.binfo.fbuf) {
      rpcmem_free_internal(sinfo->u.binfo.fbuf);
      sinfo->u.binfo.fbuf = NULL;
    }
  }
  apps_std_FILE_free(sinfo);
bail:
  if (nErr != AEE_SUCCESS) {
    VERIFY_EPRINTF("Error 0x%x: freopen for %x failed. errno: %s\n", nErr, sin,
                   strerror(nErr));
  }
  FARF(RUNTIME_RPC_LOW, "Exiting %s sin %x err %d", __func__, sin, nErr);
  FASTRPC_ATRACE_END_L("%s fd 0x%x in %"PRIu64" us error_code 0x%x ",
      __func__, sin, tdiff, nErr);
  return nErr;
}

__QAIC_IMPL_EXPORT int
__QAIC_IMPL(apps_std_fclose_fd)(int fd) __QAIC_IMPL_ATTRIBUTE {
  int nErr = AEE_SUCCESS;
  int domain = get_current_domain();
  uint64_t tdiff = 0;

  FASTRPC_ATRACE_BEGIN_L("%s for file with fd 0x%x", __func__, fd);
  FARF(RUNTIME_RPC_LOW, "Entering %s", __func__);
  errno = 0;
  QNode *pn, *pnn;
  struct mem_io_to_fd *freefd = NULL;
  pthread_mutex_lock(&fdlist.mut);
  QLIST_NEXTSAFE_FOR_ALL(&fdlist.ql, pn, pnn) {
    struct mem_io_to_fd *tofd = STD_RECOVER_REC(struct mem_io_to_fd, qn, pn);
    if (tofd->fd == fd) {
      QNode_DequeueZ(&tofd->qn);
      freefd = tofd;
      tofd = NULL;
      break;
    }
  }
  pthread_mutex_unlock(&fdlist.mut);
  if (freefd) {
    VERIFY(AEE_SUCCESS ==
           (nErr = fastrpc_munmap(domain, fd, freefd->buf, freefd->size)));
    if (freefd->buf) {
      rpcmem_free_internal(freefd->buf);
      freefd->buf = NULL;
    }
    PROFILE_ALWAYS(&tdiff,
    nErr = fclose(freefd->stream);
    );
    VERIFYC(nErr == AEE_SUCCESS, ERRNO);
  }
bail:
  FREEIF(freefd);
  if (nErr != AEE_SUCCESS) {
    VERIFY_EPRINTF("Error 0x%x: %s for %x failed. errno: %s\n", nErr, __func__,
                   fd, strerror(nErr));
  }
  FARF(RUNTIME_RPC_LOW, "Exiting %s fd %x err %d", __func__, fd, nErr);
  FASTRPC_ATRACE_END_L("%s fd 0x%x in %"PRIu64" us error_code 0x%x",
   __func__, fd, tdiff, nErr);
  return nErr;
}
__QAIC_IMPL_EXPORT int
__QAIC_IMPL(apps_std_fread)(apps_std_FILE sin, unsigned char *buf, int bufLen,
                            int *bytesRead, int *bEOF) __QAIC_IMPL_ATTRIBUTE {
  int out = 0, nErr = AEE_SUCCESS;
  struct apps_std_info *sinfo = 0;
  uint64_t tdiff = 0;

  FASTRPC_ATRACE_BEGIN_L("%s requested for %d bytes with fd 0x%x", __func__,
                         bufLen, sin);
  FARF(RUNTIME_RPC_LOW, "Entering %s", __func__);
  errno = 0;
  VERIFY(0 == (nErr = apps_std_FILE_get(sin, &sinfo)));
  if (sinfo->type == APPS_STD_STREAM_FILE) {
    PROFILE_ALWAYS(&tdiff, out = fread(buf, 1, bufLen, sinfo->u.stream););
    *bEOF = false;
    if (out <= bufLen) {
      int err;
      if (0 == out && (0 != (err = ferror(sinfo->u.stream)))) {
        nErr = ERRNO;
        VERIFY_EPRINTF("Error 0x%x: fread returning %d bytes in %"PRIu64" us, requested was %d "
                       "bytes, errno is %x\n",
                       nErr, out, tdiff, bufLen, err);
        return nErr;
      }
      *bEOF = feof(sinfo->u.stream);
      clearerr(sinfo->u.stream);
    }
    *bytesRead = out;
  } else {
    unsigned int read = STD_MIN(bufLen,
                    sinfo->u.binfo.flen - sinfo->u.binfo.pos);
    memcpy(buf, sinfo->u.binfo.fbuf + sinfo->u.binfo.pos, read);
    *bytesRead = read;
    sinfo->u.binfo.pos += read;
    *bEOF = sinfo->u.binfo.pos == sinfo->u.binfo.flen ? true : false;
  }
  FARF(RUNTIME_RPC_HIGH, "fread returning %d %d\n", out, bufLen);
bail:
  FARF(RUNTIME_RPC_LOW,
       "Exiting %s returning %d bytes, requested was %d bytes for %x, err 0x%x",
       __func__, out, bufLen, sin, nErr);
  FASTRPC_ATRACE_END_L("%s done, read %d bytes in %"PRIu64" us requested %d bytes,"
      "fd 0x%x", __func__, out, tdiff, bufLen, sin);
  return nErr;
}

__QAIC_IMPL_EXPORT int
__QAIC_IMPL(apps_std_fwrite)(apps_std_FILE sin, const unsigned char *buf, int bufLen,
                             int *bytesRead, int *bEOF) __QAIC_IMPL_ATTRIBUTE {
  int out = 0, nErr = AEE_SUCCESS;
  struct apps_std_info *sinfo = 0;

  FASTRPC_ATRACE_BEGIN_L("%s requested for %d bytes with fd 0x%x", __func__,
                         bufLen, sin);
  FARF(RUNTIME_RPC_LOW, "Entering %s", __func__);
  errno = 0;
  VERIFY(0 == (nErr = apps_std_FILE_get(sin, &sinfo)));
  if (sinfo->type == APPS_STD_STREAM_FILE) {
    out = fwrite(buf, 1, bufLen, sinfo->u.stream);
    *bEOF = false;
    if (out <= bufLen) {
      int err;
      if (0 == out && (0 != (err = ferror(sinfo->u.stream)))) {
        nErr = ERRNO;
        VERIFY_EPRINTF("Error 0x%x: fwrite returning %d bytes, requested was "
                       "%d bytes, errno is %x\n",
                       nErr, out, bufLen, err);
        return nErr;
      }
      *bEOF = feof(sinfo->u.stream);
      clearerr(sinfo->u.stream);
    }
    *bytesRead = out;
  } else {
    nErr = AEE_EFILE;
  }
bail:
  FARF(RUNTIME_RPC_LOW,
       "Exiting %s returning %d bytes, requested was %d bytes for %x, err %d",
       __func__, out, bufLen, sin, nErr);
  FASTRPC_ATRACE_END();
  return nErr;
}

__QAIC_IMPL_EXPORT int
__QAIC_IMPL(apps_std_fgetpos)(apps_std_FILE sin, unsigned char *pos, int posLen,
                              int *posLenReq) __QAIC_IMPL_ATTRIBUTE {
  int nErr = AEE_SUCCESS;
  fpos_t fpos;
  struct apps_std_info *sinfo = 0;

  FASTRPC_ATRACE_BEGIN_L("%s for file with fd 0x%x with posLen %d", __func__,
                         sin, posLen);
  FARF(RUNTIME_RPC_LOW, "Entering %s", __func__);
  errno = 0;
  VERIFY(0 == (nErr = apps_std_FILE_get(sin, &sinfo)));
  if (sinfo->type == APPS_STD_STREAM_FILE) {
    if (0 == fgetpos(sinfo->u.stream, &fpos)) {
      memmove(pos, &fpos, STD_MIN((int)sizeof(fpos), posLen));
      *posLenReq = sizeof(fpos);
    } else {
      nErr = ERRNO;
    }
  } else {
    nErr = EBADF;
  }
bail:
  if (nErr != AEE_SUCCESS) {
    VERIFY_EPRINTF("Error 0x%x: fgetpos failed for %x, errno is %s\n", nErr,
                   sin, strerror(nErr));
  }
  FARF(RUNTIME_RPC_LOW, "Exiting %s for %x, err %d", __func__, sin, nErr);
  FASTRPC_ATRACE_END();
  return nErr;
}

__QAIC_IMPL_EXPORT int
__QAIC_IMPL(apps_std_fsetpos)(apps_std_FILE sin, const unsigned char *pos,
                              int posLen) __QAIC_IMPL_ATTRIBUTE {
  int nErr = AEE_SUCCESS;
  fpos_t fpos;
  struct apps_std_info *sinfo = 0;

  FASTRPC_ATRACE_BEGIN_L("%s for file with fd 0x%x with posLen %d", __func__,
                         sin, posLen);
  FARF(RUNTIME_RPC_LOW, "Entering %s", __func__);
  errno = 0;
  VERIFY(0 == (nErr = apps_std_FILE_get(sin, &sinfo)));
  if (sinfo->type == APPS_STD_STREAM_FILE) {
    if (sizeof(fpos) != posLen) {
      nErr = EBADF;
      goto bail;
    }
    memmove(&fpos, pos, sizeof(fpos));
    VERIFYC(0 == fsetpos(sinfo->u.stream, &fpos), ERRNO);
  } else {
    nErr = EBADF;
  }
bail:
  if (nErr != AEE_SUCCESS) {
    VERIFY_EPRINTF("Error 0x%x: fsetpos failed for %x, errno is %s\n", nErr,
                   sin, strerror(nErr));
  }
  FARF(RUNTIME_RPC_LOW, "Exiting %s for %x, err %d", __func__, sin, nErr);
  FASTRPC_ATRACE_END();
  return nErr;
}

__QAIC_IMPL_EXPORT int
__QAIC_IMPL(apps_std_ftell)(apps_std_FILE sin, int *pos) __QAIC_IMPL_ATTRIBUTE {
  int nErr = AEE_SUCCESS;
  struct apps_std_info *sinfo = 0;

  FASTRPC_ATRACE_BEGIN_L("%s for file with fd 0x%x", __func__, sin);
  FARF(RUNTIME_RPC_LOW, "Entering %s", __func__);
  errno = 0;
  VERIFY(0 == (nErr = apps_std_FILE_get(sin, &sinfo)));
  if (sinfo->type == APPS_STD_STREAM_FILE) {
    VERIFYC((*pos = ftell(sinfo->u.stream)) >= 0, ERRNO);
  } else {
    *pos = sinfo->u.binfo.pos;
  }
bail:
  if (nErr != AEE_SUCCESS) {
    VERIFY_EPRINTF("Error 0x%x: ftell failed for %x, errno is %s\n", nErr, sin,
                   strerror(nErr));
  }
  FARF(RUNTIME_RPC_LOW, "Exiting %s for %x, err %d", __func__, sin, nErr);
  FASTRPC_ATRACE_END();
  return nErr;
}

__QAIC_IMPL_EXPORT int
__QAIC_IMPL(apps_std_fseek)(apps_std_FILE sin, int offset,
                            apps_std_SEEK whence) __QAIC_IMPL_ATTRIBUTE {
  int nErr = AEE_SUCCESS;
  int op = (int)whence;
  struct apps_std_info *sinfo = 0;
  uint64_t tdiff = 0;

  FASTRPC_ATRACE_BEGIN_L("%s for file with fd 0x%x for op %d on offset %d",
                         __func__, sin, whence, offset);
  FARF(RUNTIME_RPC_LOW, "Entering %s op %d", __func__, op);
  errno = 0;
  C_ASSERT(APPS_STD_SEEK_SET == SEEK_SET);
  C_ASSERT(APPS_STD_SEEK_CUR == SEEK_CUR);
  C_ASSERT(APPS_STD_SEEK_END == SEEK_END);
  VERIFY(0 == (nErr = apps_std_FILE_get(sin, &sinfo)));
  if (sinfo->type == APPS_STD_STREAM_FILE) {
    PROFILE(&tdiff,
            VERIFYC(0 == fseek(sinfo->u.stream, offset, whence), ERRNO););
  } else {
    switch (op) {
    case APPS_STD_SEEK_SET:
      VERIFYC(offset <= sinfo->u.binfo.flen, AEE_EFILE);
      sinfo->u.binfo.pos = offset;
      break;
    case APPS_STD_SEEK_CUR:
      VERIFYC(offset + sinfo->u.binfo.pos <= sinfo->u.binfo.flen, AEE_EFILE);
      sinfo->u.binfo.pos += offset;
      break;
    case APPS_STD_SEEK_END:
      VERIFYC(offset <= INT_MAX - sinfo->u.binfo.flen, AEE_EFILE);
      sinfo->u.binfo.pos += offset + sinfo->u.binfo.flen;
      break;
    }
  }
bail:
  if (nErr != AEE_SUCCESS) {
    VERIFY_EPRINTF("Error 0x%x: fseek failed for %x, errno is %s\n", nErr, sin,
                   strerror(nErr));
  }
  FARF(RUNTIME_RPC_LOW, "Exiting %s for %x offset %d, err %d", __func__, sin,
       offset, nErr);
  FASTRPC_ATRACE_END_L("%s done for fd 0x%x, op %d on offset %d, time %" PRIu64
                       " us, err %d",
                       __func__, sin, whence, offset, tdiff, nErr);
  return nErr;
}

__QAIC_IMPL_EXPORT int
__QAIC_IMPL(apps_std_rewind)(apps_std_FILE sin) __QAIC_IMPL_ATTRIBUTE {
  int nErr = AEE_SUCCESS;
  struct apps_std_info *sinfo = 0;

  FASTRPC_ATRACE_BEGIN_L("%s for file with fd 0x%x", __func__, sin);
  FARF(RUNTIME_RPC_LOW, "Entering %s", __func__);
  VERIFY(0 == (nErr = apps_std_FILE_get(sin, &sinfo)));
  if (sinfo->type == APPS_STD_STREAM_FILE) {
    rewind(sinfo->u.stream);
  } else {
    sinfo->u.binfo.pos = 0;
  }
  if (errno != 0)
    nErr = ERRNO;
bail:
  if (nErr != AEE_SUCCESS) {
    VERIFY_EPRINTF("Error 0x%x: rewind failed for %x, errno is %s\n", nErr, sin,
                   strerror(nErr));
  }
  FARF(RUNTIME_RPC_LOW, "Exiting %s for %x, err %d", __func__, sin, nErr);
  FASTRPC_ATRACE_END();
  return nErr;
}

__QAIC_IMPL_EXPORT int
__QAIC_IMPL(apps_std_feof)(apps_std_FILE sin, int *bEOF) __QAIC_IMPL_ATTRIBUTE {
  int nErr = AEE_SUCCESS;
  struct apps_std_info *sinfo = 0;

  FASTRPC_ATRACE_BEGIN_L("%s for file with fd 0x%x", __func__, sin);
  FARF(RUNTIME_RPC_LOW, "Entering %s", __func__);
  VERIFY(0 == (nErr = apps_std_FILE_get(sin, &sinfo)));
  if (sinfo->type == APPS_STD_STREAM_FILE) {
    *bEOF = feof(sinfo->u.stream);
    clearerr(sinfo->u.stream);
  } else {
    nErr = EBADF;
  }
bail:
  if (nErr != AEE_SUCCESS) {
    VERIFY_EPRINTF("Error 0x%x: feof failed for %x, errno is %s\n", nErr, sin,
                   strerror(nErr));
  }
  FARF(RUNTIME_RPC_LOW, "Exiting %s for %x, err %d", __func__, sin, nErr);
  FASTRPC_ATRACE_END();
  return nErr;
}

__QAIC_IMPL_EXPORT int __QAIC_IMPL(apps_std_ferror)(apps_std_FILE sin, int *err)
    __QAIC_IMPL_ATTRIBUTE {
  int nErr = AEE_SUCCESS;
  struct apps_std_info *sinfo = 0;

  FASTRPC_ATRACE_BEGIN_L("%s for file with fd 0x%x", __func__, sin);
  VERIFY(0 == (nErr = apps_std_FILE_get(sin, &sinfo)));
  if (sinfo->type == APPS_STD_STREAM_FILE) {
    *err = ferror(sinfo->u.stream);
  } else {
    nErr = EBADF;
  }
bail:
  if (nErr != AEE_SUCCESS) {
    VERIFY_EPRINTF("Error 0x%x: ferror failed for %x, errno is %s\n", nErr, sin,
                   strerror(nErr));
  }
  FASTRPC_ATRACE_END();
  return nErr;
}

__QAIC_IMPL_EXPORT int
__QAIC_IMPL(apps_std_clearerr)(apps_std_FILE sin) __QAIC_IMPL_ATTRIBUTE {
  int nErr = AEE_SUCCESS;
  struct apps_std_info *sinfo = 0;

  FASTRPC_ATRACE_BEGIN_L("%s for file with fd 0x%x", __func__, sin);
  VERIFY(0 == (nErr = apps_std_FILE_get(sin, &sinfo)));
  if (sinfo->type == APPS_STD_STREAM_FILE) {
    clearerr(sinfo->u.stream);
  } else {
    nErr = EBADF;
  }
bail:
  if (nErr != AEE_SUCCESS) {
    VERIFY_EPRINTF("Error 0x%x: clearerr failed for %x, errno is %s\n", nErr,
                   sin, strerror(nErr));
  }
  FASTRPC_ATRACE_END();
  return nErr;
}

__QAIC_IMPL_EXPORT int
__QAIC_IMPL(apps_std_flen)(apps_std_FILE sin,
                           uint64_t *len) __QAIC_IMPL_ATTRIBUTE {
  int nErr = AEE_SUCCESS;
  struct apps_std_info *sinfo = 0;

  FASTRPC_ATRACE_BEGIN_L("%s for file with fd 0x%x", __func__, sin);
  VERIFY(0 == (nErr = apps_std_FILE_get(sin, &sinfo)));
  if (sinfo->type == APPS_STD_STREAM_FILE) {
    struct stat st_buf;
    errno = 0;
    int fd = fileno(sinfo->u.stream);
    C_ASSERT(sizeof(st_buf.st_size) <= sizeof(*len));
    if (fd == -1) {
      nErr = ERRNO;
      VERIFY_EPRINTF("Error 0x%x: flen failed for %x, errno is %s\n", nErr, sin,
                     strerror(ERRNO));
      return nErr;
    }
    errno = 0;
    if (0 != fstat(fd, &st_buf)) {
      nErr = ERRNO;
      VERIFY_EPRINTF("Error 0x%x: flen failed for %x, errno is %s\n", nErr, sin,
                     strerror(ERRNO));
      return nErr;
    }
    *len = st_buf.st_size;
  } else {
    *len = sinfo->u.binfo.flen;
  }
bail:
  FASTRPC_ATRACE_END();
  return nErr;
}

__QAIC_IMPL_EXPORT int
__QAIC_IMPL(apps_std_print_string)(const char *str) __QAIC_IMPL_ATTRIBUTE {
  printf("%s\n", str);
  return AEE_SUCCESS;
}

__QAIC_IMPL_EXPORT int
__QAIC_IMPL(apps_std_getenv)(const char *name, char *val, int valLen,
                             int *valLenReq) __QAIC_IMPL_ATTRIBUTE {
  int nErr = AEE_SUCCESS;
  errno = 0;
  char *vv = getenv(name);
  if (vv) {
    *valLenReq = strlen(vv) + 1;
    strlcpy(val, vv, STD_MIN(valLen, *valLenReq));
    return AEE_SUCCESS;
  }
  nErr = ERRNO;
  FARF(RUNTIME_RPC_HIGH, "Error 0x%x: apps_std getenv failed: %s %s\n", nErr,
       name, strerror(ERRNO));
  return nErr;
}

__QAIC_IMPL_EXPORT int
__QAIC_IMPL(apps_std_setenv)(const char *name, const char *val,
                             int override) __QAIC_IMPL_ATTRIBUTE {
  int nErr = AEE_SUCCESS;
  errno = 0;
#ifdef _WIN32
  return AEE_EUNSUPPORTED;
#else //_WIN32
  if (0 != setenv(name, val, override)) {
    nErr = ERRNO;
    VERIFY_EPRINTF("Error 0x%x: setenv failed for %s, errno is %s\n", nErr,
                   name, strerror(ERRNO));
    return nErr;
  }
  return AEE_SUCCESS;
#endif //_WIN32
}

__QAIC_IMPL_EXPORT int
__QAIC_IMPL(apps_std_unsetenv)(const char *name) __QAIC_IMPL_ATTRIBUTE {
  int nErr = AEE_SUCCESS;
  errno = 0;
#ifdef _WIN32
  return AEE_EUNSUPPORTED;
#else //_WIN32
  if (0 != unsetenv(name)) {
    nErr = ERRNO;
    VERIFY_EPRINTF("Error 0x%x: unsetenv failed for %s, errno is %s\n", nErr,
                   name, strerror(ERRNO));
    return nErr;
  }
  return AEE_SUCCESS;
#endif //_WIN32
}

#define EMTPY_STR ""
#define ENV_LEN_GUESS 256

static int get_dirlist_from_env(const char *envvarname, char **ppDirList) {
  char *envList = NULL;
  char *envListBuf = NULL;
  char *dirList = NULL;
  char *dirListBuf = NULL;
  char *srcStr = NULL;
  int nErr = AEE_SUCCESS;
  int envListLen = 0;
  int envListPrependLen = 0;
  int listLen = 0;
  int envLenGuess = STD_MAX(ENV_LEN_GUESS, 1 + strlen(DSP_SEARCH_PATH));

  FARF(RUNTIME_RPC_LOW, "Entering %s", __func__);
  VERIFYC(NULL != ppDirList, AEE_ERPC);

  VERIFYC(envListBuf = (char *)malloc(sizeof(char) * envLenGuess),
          AEE_ENOMEMORY);
  envList = envListBuf;
  *envList = '\0';
  if (0 == apps_std_getenv(envvarname, envList, envLenGuess, &envListLen)) {
    if (strncmp(envvarname, ADSP_LIBRARY_PATH,
                    strlen(ADSP_LIBRARY_PATH)) == 0 ||
        strncmp(envvarname, DSP_LIBRARY_PATH,
                    strlen(DSP_LIBRARY_PATH)) == 0) {
      // Calculate total length of env and DSP_SEARCH_PATH
      envListPrependLen = envListLen + strlen(DSP_SEARCH_PATH);
      if (envLenGuess < envListPrependLen) {
        FREEIF(envListBuf);
        VERIFYC(envListBuf =
                    realloc(envListBuf, sizeof(char) * envListPrependLen),
                AEE_ENOMEMORY);
        envList = envListBuf;
        VERIFY(0 == (nErr = apps_std_getenv(envvarname, envList,
                                            envListPrependLen, &listLen)));
      }
      // Append default DSP_SEARCH_PATH to user defined env
      strlcat(envList, DSP_SEARCH_PATH, envListPrependLen);
      envListLen = envListPrependLen;
    } else if (strncmp(envvarname, ADSP_AVS_PATH,
                           strlen(ADSP_AVS_PATH)) == 0) {
      envListPrependLen = envListLen + strlen(ADSP_AVS_CFG_PATH);
      if (envLenGuess < envListPrependLen) {
        FREEIF(envListBuf);
        VERIFYC(envListBuf =
                    realloc(envListBuf, sizeof(char) * envListPrependLen),
                AEE_ENOMEMORY);
        envList = envListBuf;
        VERIFY(0 == (nErr = apps_std_getenv(envvarname, envList,
                                            envListPrependLen, &listLen)));
      }
      strlcat(envList, ADSP_AVS_CFG_PATH, envListPrependLen);
      envListLen = envListPrependLen;
    } else {
      envListLen = listLen;
    }
  } else if (strncmp(envvarname, ADSP_LIBRARY_PATH,
                         strlen(ADSP_LIBRARY_PATH)) == 0 ||
             strncmp(envvarname, DSP_LIBRARY_PATH,
                         strlen(DSP_LIBRARY_PATH)) == 0) {
    envListLen = listLen =
        1 + strlcpy(envListBuf, DSP_SEARCH_PATH, envLenGuess);
  } else if (strncmp(envvarname, ADSP_AVS_PATH,
                         strlen(ADSP_AVS_PATH)) == 0) {
    envListLen = listLen =
        1 + strlcpy(envListBuf, ADSP_AVS_CFG_PATH, envLenGuess);
  }

  /*
   * Allocate mem. to copy envvarname.
   */
  if ('\0' != *envList) {
    srcStr = envList;
  } else {
    envListLen = strlen(EMTPY_STR) + 1;
  }
  VERIFYC(dirListBuf = (char *)malloc(sizeof(char) * envListLen),
          AEE_ENOMEMORY);
  dirList = dirListBuf;
  VERIFYC(srcStr != NULL, AEE_EBADPARM);
  strlcpy(dirList, srcStr, envListLen);
  *ppDirList = dirListBuf;
bail:
  FREEIF(envListBuf);
  if (nErr != AEE_SUCCESS) {
    VERIFY_EPRINTF("Error 0x%x: get dirlist from env failed for %s\n", nErr,
                   envvarname);
  }
  FARF(RUNTIME_RPC_LOW, "Exiting %s for %s, err %d", __func__, envvarname,
       nErr);
  return nErr;
}

__QAIC_IMPL_EXPORT int __QAIC_IMPL(apps_std_fopen_with_env)(
    const char *envvarname, const char *delim, const char *name,
    const char *mode, apps_std_FILE *psout) __QAIC_IMPL_ATTRIBUTE {

  int nErr = AEE_SUCCESS;
  char *dirName = NULL;
  char *pos = NULL;
  char *dirListBuf = NULL;
  char *dirList = NULL;
  char *absName = NULL;
  const char *envVar = NULL;
  uint16_t absNameLen = 0;
  int domain = GET_DOMAIN_FROM_EFFEC_DOMAIN_ID(get_current_domain());

  FARF(RUNTIME_RPC_LOW, "Entering %s", __func__);
  VERIFYC(NULL != mode, AEE_EBADPARM);
  VERIFYC(NULL != delim, AEE_EBADPARM);
  VERIFYC(NULL != name, AEE_EBADPARM);
  VERIFYC(NULL != envvarname, AEE_EBADPARM);
  FASTRPC_ATRACE_BEGIN_L("%s for %s in %s mode from path in environment "
                         "variable %s delimited with %s",
                         __func__, name, mode, envvarname, delim);
  if (strncmp(envvarname, ADSP_LIBRARY_PATH,
                  strlen(ADSP_LIBRARY_PATH)) == 0) {
    if (getenv(DSP_LIBRARY_PATH)) {
      envVar = DSP_LIBRARY_PATH;
    } else {
      envVar = ADSP_LIBRARY_PATH;
    }
  } else {
    envVar = envvarname;
  }

  VERIFY(0 == (nErr = get_dirlist_from_env(envVar, &dirListBuf)));
  VERIFYC(NULL != (dirList = dirListBuf), AEE_EBADPARM);
  FARF(RUNTIME_RPC_HIGH, "%s dirList %s", __func__, dirList);

  while (dirList) {
    pos = strstr(dirList, delim);
    dirName = dirList;
    if (pos) {
      *pos = '\0';
      dirList = pos + strlen(delim);
    } else {
      dirList = 0;
    }

    // Append domain to path
    absNameLen =
        strlen(dirName) + strlen(name) + 2 + strlen("adsp") + 1;
    VERIFYC(NULL != (absName = (char *)malloc(sizeof(char) * absNameLen)),
            AEE_ENOMEMORY);
    if ('\0' != *dirName) {
      strlcpy(absName, dirName, absNameLen);
      strlcat(absName, "/", absNameLen);
      strlcat(absName, SUBSYSTEM_NAME[domain], absNameLen);
      strlcat(absName, "/", absNameLen);
      strlcat(absName, name, absNameLen);
    } else {
      strlcpy(absName, name, absNameLen);
    }

    nErr = apps_std_fopen(absName, mode, psout);
    if (AEE_SUCCESS == nErr) {
      // Success
      FARF(ALWAYS, "Successfully opened file %s", absName);
      goto bail;
    }
    FREEIF(absName);

    // fallback: If not found in domain path /vendor/dsp/adsp try in /vendor/dsp
    absNameLen = strlen(dirName) + strlen(name) + 2;
    VERIFYC(NULL != (absName = (char *)malloc(sizeof(char) * absNameLen)),
            AEE_ENOMEMORY);
    if ('\0' != *dirName) {
      strlcpy(absName, dirName, absNameLen);
      strlcat(absName, "/", absNameLen);
      strlcat(absName, name, absNameLen);
    } else {
      strlcpy(absName, name, absNameLen);
    }

    nErr = apps_std_fopen(absName, mode, psout);
    if (AEE_SUCCESS == nErr) {
      // Success
      if (name != NULL &&
          (strncmp(name, OEM_CONFIG_FILE_NAME,
                       strlen(OEM_CONFIG_FILE_NAME)) != 0) &&
          (strncmp(name, TESTSIG_FILE_NAME,
                       strlen(TESTSIG_FILE_NAME)) != 0))
        FARF(ALWAYS, "Successfully opened file %s", name);
      goto bail;
    }
    FREEIF(absName);
  }
bail:
  FREEIF(absName);
  FREEIF(dirListBuf);
  if (nErr != AEE_SUCCESS) {
    if (ERRNO != ENOENT ||
        (name != NULL &&
         strncmp(name, OEM_CONFIG_FILE_NAME,
                     strlen(OEM_CONFIG_FILE_NAME)) != 0 &&
         strncmp(name, RPC_VERSION_FILE_NAME,
                     strlen(RPC_VERSION_FILE_NAME)) != 0 &&
         strncmp(name, TESTSIG_FILE_NAME, strlen(TESTSIG_FILE_NAME)) !=
             0))
      VERIFY_WPRINTF(" Warning: %s failed with 0x%x for %s (%s)", __func__,
                     nErr, name, strerror(ERRNO));
  }
  FARF(RUNTIME_RPC_LOW,
       "Exiting %s for %s envvarname %s mode %s delim %s, err %d", __func__,
       name, envvarname, mode, delim, nErr);
  if (name && mode && envvarname && delim) {
    FASTRPC_ATRACE_END();
  }
  return nErr;
}

__QAIC_IMPL_EXPORT int __QAIC_IMPL(apps_std_fopen_with_env_fd)(
    const char *envvarname, const char *delim, const char *name,
    const char *mode, int *fd, int *len) __QAIC_IMPL_ATTRIBUTE {

  int nErr = ENOENT, err = ENOENT;
  char *dirName = NULL;
  char *pos = NULL;
  char *dirListBuf = NULL;
  char *dirList = NULL;
  char *absName = NULL;
  char *errabsName = NULL;
  const char *envVar = NULL;
  uint16_t absNameLen = 0;
  int domain = GET_DOMAIN_FROM_EFFEC_DOMAIN_ID(get_current_domain());

  FARF(RUNTIME_RPC_LOW, "Entering %s", __func__);
  VERIFYC(NULL != mode, AEE_EBADPARM);
  VERIFYC(NULL != delim, AEE_EBADPARM);
  VERIFYC(NULL != name, AEE_EBADPARM);
  VERIFYC(NULL != envvarname, AEE_EBADPARM);
#if 0 //TODO: Bharath
  char *tempName = name;
  tempName += 2;
  if (tempName[0] == '\0') {
    nErr = AEE_EBADPARM;
    goto bail;
  }
#endif
  FASTRPC_ATRACE_BEGIN_L("%s for %s in %s mode from path in environment "
                         "variable %s delimited with %s",
                         __func__, name, mode, envvarname, delim);
  if (strncmp(envvarname, ADSP_LIBRARY_PATH,
                  strlen(ADSP_LIBRARY_PATH)) == 0) {
    if (getenv(DSP_LIBRARY_PATH)) {
      envVar = DSP_LIBRARY_PATH;
    } else {
      envVar = ADSP_LIBRARY_PATH;
    }
  } else {
    envVar = envvarname;
  }

  VERIFY(0 == (nErr = get_dirlist_from_env(envVar, &dirListBuf)));
  VERIFYC(NULL != (dirList = dirListBuf), AEE_EBADPARM);

  while (dirList) {
    pos = strstr(dirList, delim);
    dirName = dirList;
    if (pos) {
      *pos = '\0';
      dirList = pos + strlen(delim);
    } else {
      dirList = 0;
    }

    // Append domain to path
    absNameLen =
        strlen(dirName) + strlen(name) + 2 + strlen("adsp") + 1;
    VERIFYC(NULL != (absName = (char *)malloc(sizeof(char) * absNameLen)),
            AEE_ENOMEMORY);
    if ('\0' != *dirName) {
      strlcpy(absName, dirName, absNameLen);
      strlcat(absName, "/", absNameLen);
      strlcat(absName, SUBSYSTEM_NAME[domain], absNameLen);
      strlcat(absName, "/", absNameLen);
      strlcat(absName, name, absNameLen);
    } else {
      strlcpy(absName, name, absNameLen);
    }

    err = apps_std_fopen_fd(absName, mode, fd, len);
    if (AEE_SUCCESS == err) {
      // Success
      FARF(ALWAYS, "Successfully opened file %s", absName);
      goto bail;
    }
    /* Do not Update nErr if error is no such file, as it may not be
     * genuine error until we find in all path's.
     */
    if (err != ENOENT && (nErr == ENOENT || nErr == AEE_SUCCESS)) {
      nErr = err;
      errabsName = absName;
      absName = NULL;
    }
    FREEIF(absName);

    // fallback: If not found in domain path /vendor/dsp/adsp try in /vendor/dsp
    absNameLen = strlen(dirName) + strlen(name) + 2;
    VERIFYC(NULL != (absName = (char *)malloc(sizeof(char) * absNameLen)),
            AEE_ENOMEMORY);
    if ('\0' != *dirName) {
      strlcpy(absName, dirName, absNameLen);
      strlcat(absName, "/", absNameLen);
      strlcat(absName, name, absNameLen);
    } else {
      strlcpy(absName, name, absNameLen);
    }

    err = apps_std_fopen_fd(absName, mode, fd, len);
    if (AEE_SUCCESS == err) {
      // Success
      FARF(ALWAYS, "Successfully opened file %s", absName);
      nErr = err;
      goto bail;
    }
    /* Do not Update nErr if error is no such file, as it may not be
     * genuine error until we find in all path's.
     */
    if (err != ENOENT && (nErr == ENOENT || nErr == AEE_SUCCESS)) {
      nErr = err;
      errabsName = absName;
      absName = NULL;
    }
    FREEIF(absName);
  }
  /* In case if file is not present in any path update
   * error code to no such file.
   */
  if (err == ENOENT && (nErr == ENOENT || nErr == AEE_SUCCESS))
    nErr = err;
bail:
  if (nErr != AEE_SUCCESS) {
    if (ERRNO != ENOENT ||
        (name != NULL &&
         strncmp(name, OEM_CONFIG_FILE_NAME,
                     strlen(OEM_CONFIG_FILE_NAME)) != 0 &&
         strncmp(name, RPC_VERSION_FILE_NAME,
                     strlen(RPC_VERSION_FILE_NAME)) != 0 &&
         strncmp(name, TESTSIG_FILE_NAME, strlen(TESTSIG_FILE_NAME)) !=
             0)) {
      if (errabsName) {
        VERIFY_WPRINTF(" Warning: %s failed with 0x%x for path %s name %s (%s)",
                       __func__, nErr, errabsName, name, strerror(ERRNO));
      } else {
        VERIFY_WPRINTF(" Warning: %s failed with 0x%x for %s (%s)", __func__,
                       nErr, name, strerror(ERRNO));
      }
    }
  }

  FREEIF(errabsName);
  FREEIF(absName);
  FREEIF(dirListBuf);
  FARF(RUNTIME_RPC_LOW,
       "Exiting %s for %s envvarname %s mode %s delim %s, err %d", __func__,
       name, envvarname, mode, delim, nErr);
  if (name && mode && envvarname && delim) {
    FASTRPC_ATRACE_END();
  }
  return nErr;
}

__QAIC_HEADER_EXPORT int __QAIC_IMPL(apps_std_get_search_paths_with_env)(
    const char *envvarname, const char *delim, _cstring1_t *paths, int pathsLen,
    uint32_t *numPaths, uint16_t *maxPathLen) __QAIC_IMPL_ATTRIBUTE {

  char *path = NULL;
  char *pathDomain = NULL;
  int pathDomainLen = 0;
  int nErr = AEE_SUCCESS;
  char *dirListBuf = NULL;
  int i = 0;
  char *saveptr = NULL;
  const char *envVar = NULL;
  struct stat st;
  int domain = GET_DOMAIN_FROM_EFFEC_DOMAIN_ID(get_current_domain());

  FARF(RUNTIME_RPC_LOW, "Entering %s", __func__);
  VERIFYC(NULL != numPaths, AEE_EBADPARM);
  VERIFYC(NULL != delim, AEE_EBADPARM);
  VERIFYC(NULL != maxPathLen, AEE_EBADPARM);

  if (strncmp(envvarname, ADSP_LIBRARY_PATH,
                  strlen(ADSP_LIBRARY_PATH)) == 0) {
    if (getenv(DSP_LIBRARY_PATH)) {
      envVar = DSP_LIBRARY_PATH;
    } else {
      envVar = ADSP_LIBRARY_PATH;
    }
  } else {
    envVar = envvarname;
  }

  VERIFY(AEE_SUCCESS == (nErr = get_dirlist_from_env(envVar, &dirListBuf)));

  *numPaths = 0;
  *maxPathLen = 0;

  // Get the number of folders
  path = strtok_r(dirListBuf, delim, &saveptr);
  while (path != NULL) {
    pathDomainLen = strlen(path) + 1 + strlen("adsp") + 1;
    VERIFYC(pathDomain = (char *)malloc(sizeof(char) * (pathDomainLen)),
            AEE_ENOMEMORY);
    strlcpy(pathDomain, path, pathDomainLen);
    strlcat(pathDomain, "/", pathDomainLen);
    strlcat(pathDomain, SUBSYSTEM_NAME[domain], pathDomainLen);
    // If the path exists, add it to the return
    if ((stat(pathDomain, &st) == 0) && (S_ISDIR(st.st_mode))) {
      *maxPathLen = STD_MAX(*maxPathLen, strlen(pathDomain) + 1);
      if (paths && i < pathsLen && paths[i].data &&
          paths[i].dataLen >= (int)strlen(path)) {
        strlcpy(paths[i].data, pathDomain, paths[i].dataLen);
      }
      i++;
    }
    if ((stat(path, &st) == 0) && (S_ISDIR(st.st_mode))) {
      *maxPathLen = STD_MAX(*maxPathLen, strlen(path) + 1);
      if (paths && i < pathsLen && paths[i].data &&
          paths[i].dataLen >= (int)strlen(path)) {
        strlcpy(paths[i].data, path, paths[i].dataLen);
      }
      i++;
    }
    path = strtok_r(NULL, delim, &saveptr);
    FREEIF(pathDomain);
  }
  *numPaths = i;

bail:
  FREEIF(dirListBuf);
  FREEIF(pathDomain);
  if (nErr != AEE_SUCCESS) {
    VERIFY_EPRINTF("Error 0x%x: apps_std_get_search_paths_with_env failed\n",
                   nErr);
  }
  FARF(RUNTIME_RPC_LOW, "Exiting %s for envvarname %s delim %s, err %d",
       __func__, envvarname, delim, nErr);
  return nErr;
}

__QAIC_IMPL_EXPORT int
__QAIC_IMPL(apps_std_fgets)(apps_std_FILE sin, unsigned char *buf, int bufLen,
                            int *bEOF) __QAIC_IMPL_ATTRIBUTE {
  int nErr = AEE_SUCCESS;
  struct apps_std_info *sinfo = 0;

  FASTRPC_ATRACE_BEGIN_L("%s for file with fd 0x%x for buflen %d", __func__,
                         sin, bufLen);
  VERIFY(0 == (nErr = apps_std_FILE_get(sin, &sinfo)));
  if (sinfo->type == APPS_STD_STREAM_FILE) {
    char *out = fgets((char *)buf, bufLen, sinfo->u.stream);
    *bEOF = false;
    if (!out) {
      int err = 0;
      if (0 != (err = ferror(sinfo->u.stream))) {
        nErr = ERRNO;
        VERIFY_EPRINTF("Error 0x%x: fgets failed for %x, errno is %s\n", nErr,
                       sin, strerror(ERRNO));
        goto bail;
      }
      *bEOF = feof(sinfo->u.stream);
    }
  } else {
    nErr = EBADF;
  }
bail:
  FASTRPC_ATRACE_END();
  return nErr;
}

__QAIC_HEADER_EXPORT int
__QAIC_HEADER(apps_std_fileExists)(const char *path,
                                   bool *exists) __QAIC_HEADER_ATTRIBUTE {
  int nErr = AEE_SUCCESS, err = 0;
  struct stat buffer;

  VERIFYC(path != NULL, AEE_EBADPARM);
  VERIFYC(exists != NULL, AEE_EBADPARM);

  errno = 0;
  *exists = (stat(path, &buffer) == 0);
  err = errno;
bail:
  if (nErr != AEE_SUCCESS || err) {
    FARF(RUNTIME_RPC_HIGH,
         "Warniing 0x%x: fileExists failed for path %s, errno is %s\n", nErr,
         path, strerror(err));
  }
  return nErr;
}

__QAIC_IMPL_EXPORT int
__QAIC_IMPL(apps_std_fsync)(apps_std_FILE sin) __QAIC_IMPL_ATTRIBUTE {
  int nErr = AEE_SUCCESS;
  struct apps_std_info *sinfo = 0;

  VERIFY(0 == (nErr = apps_std_FILE_get(sin, &sinfo)));
  if (sinfo->type == APPS_STD_STREAM_FILE) {
    // This flushes the given sin file stream to user-space buffer.
    // NOTE: this does NOT ensure data is physically sotred on disk
    nErr = fflush(sinfo->u.stream);
    if (nErr != AEE_SUCCESS) {
      nErr = ERRNO;
      VERIFY_EPRINTF("Error 0x%x: apps_std fsync failed,errno is %s\n", nErr,
                     strerror(ERRNO));
    }
  } else {
    nErr = EBADF;
  }

bail:
  return nErr;
}

__QAIC_IMPL_EXPORT int
__QAIC_IMPL(apps_std_fremove)(const char *name) __QAIC_IMPL_ATTRIBUTE {
  int nErr = AEE_SUCCESS;

  if (NULL == name) {
    return EINVAL;
  }
  FASTRPC_ATRACE_BEGIN_L("%s for file %s", __func__, name);
  nErr = remove(name);
  if (nErr != AEE_SUCCESS) {
    nErr = ERRNO;
    VERIFY_EPRINTF("Error 0x%x: failed to remove file %s,errno is %s\n", nErr,
                   name, strerror(ERRNO));
  }
  FASTRPC_ATRACE_END();
  return nErr;
}

static int decrypt_int(char *fbuf, int size) {
  int nErr = 0, fd;
  void *handle = 0;
  int32_t (*l_init)(void);
  int32_t (*l_deinit)(void);
  int32_t (*l_decrypt)(int32_t, int32_t);

  VERIFYC(NULL != (handle = dlopen("liblmclient.so", RTLD_NOW)),
          AEE_EINVHANDLE);
  VERIFYM(NULL != (l_init = dlsym(handle, "license_manager_init")), AEE_ERPC,
          "Error: %s failed symbol license_manager_init not found err 0x%x "
          "errno is %s",
          __func__, nErr, strerror(ERRNO));
  VERIFYM(NULL != (l_deinit = dlsym(handle, "license_manager_deinit")),
          AEE_ERPC,
          "Error: %s failed symbol license_manager_deinit not found err 0x%x "
          "errno is %s",
          __func__, nErr, strerror(ERRNO));
  VERIFYM(NULL != (l_decrypt = dlsym(handle, "license_manager_decrypt")),
          AEE_ERPC,
          "Error: %s failed symbol license_manager_decrypt not found err 0x%x "
          "errno is %s",
          __func__, nErr, strerror(ERRNO));
  VERIFY(0 == (nErr = l_init()));
  VERIFYC(-1 != (fd = rpcmem_to_fd_internal(fbuf)), AEE_ERPC);
  VERIFY(0 == (nErr = l_decrypt(fd, size)));
  VERIFY(0 == (nErr = l_deinit()));
bail:
  if (nErr) {
    VERIFY_EPRINTF("Error 0x%x: dlopen for licmgr failed. errno: %s\n", nErr,
                   dlerror());
  }
  if (handle) {
    dlclose(handle);
  }
  return nErr;
}

__QAIC_IMPL_EXPORT int __QAIC_IMPL(apps_std_fdopen_decrypt)(
    apps_std_FILE sin, apps_std_FILE *psout) __QAIC_IMPL_ATTRIBUTE {
  int fd, nErr = AEE_SUCCESS;
  struct stat st_buf;
  struct apps_std_info *sinfo = 0;
  int sz, pos;
  char *fbuf = 0;

  VERIFY(0 == (nErr = apps_std_FILE_get(sin, &sinfo)));
  if (sinfo->type == APPS_STD_STREAM_FILE) {
    pos = ftell(sinfo->u.stream);
    VERIFYM(-1 != (fd = fileno(sinfo->u.stream)), AEE_EFILE,
            "Error: %s failed file len is not proper err 0x%x errno is %s",
            __func__, nErr, strerror(ERRNO));
    VERIFYM(0 == fstat(fd, &st_buf), AEE_EFILE,
            "Error: %s failed file len is not proper err 0x%x errno is %s",
            __func__, nErr, strerror(ERRNO));
    sz = (int)st_buf.st_size;
    VERIFYC(
        0 != (fbuf = rpcmem_alloc_internal(ION_HEAP_ID_QSEECOM, 1, (size_t)sz)),
        AEE_ENORPCMEMORY);
    VERIFYM(0 == fseek(sinfo->u.stream, 0, SEEK_SET), AEE_EFILE,
            "Error: %s failed as fseek failed err 0x%x errno is %s", __func__,
            nErr, strerror(ERRNO));
    VERIFYM(sz == (int)fread(fbuf, 1, sz, sinfo->u.stream), AEE_EFILE,
            "Error: %s failed as fread failed err 0x%x errno is %s", __func__,
            nErr, strerror(ERRNO));
    VERIFY(0 == (nErr = decrypt_int(fbuf, sz)));
    apps_std_FILE_set_buffer_stream(sinfo, fbuf, sz, pos);
    *psout = sin;
  } else {
    nErr = EBADF;
  }
bail:
  if (nErr) {
    if (fbuf) {
      rpcmem_free_internal(fbuf);
      fbuf = NULL;
    }
  }
  return nErr;
}

__QAIC_IMPL_EXPORT int
__QAIC_HEADER(apps_std_opendir)(const char *name,
                                apps_std_DIR *dir) __QAIC_IMPL_ATTRIBUTE {
  int nErr = 0;
  DIR *odir;
  struct apps_std_dir_info *dirinfo = 0;

  if (NULL == dir) {
    return EINVAL;
  }
  if (name == NULL)
    return AEE_EBADPARM;
  errno = 0;
  odir = opendir(name);
  if (odir != NULL) {
    dir->handle = (uint64_t)odir;
    dirinfo =
        (struct apps_std_dir_info *)calloc(1, sizeof(struct apps_std_dir_info));
    VERIFYC(dirinfo != NULL, ENOMEM);
    dirinfo->handle = dir->handle;
    pthread_mutex_lock(&apps_std_mt);
    QList_AppendNode(&apps_std_dirlist, &dirinfo->qn);
    pthread_mutex_unlock(&apps_std_mt);
  } else {
    nErr = ERRNO;
  }
bail:
  if (nErr) {
    VERIFY_EPRINTF("Error 0x%x: failed to opendir %s,errno is %s\n", nErr, name,
                   strerror(ERRNO));
  }
  return nErr;
}

__QAIC_IMPL_EXPORT int __QAIC_HEADER(apps_std_closedir)(const apps_std_DIR *dir)
    __QAIC_IMPL_ATTRIBUTE {
  int nErr = AEE_SUCCESS;
  struct apps_std_dir_info *dirinfo = 0;

  if ((NULL == dir) || (0 == dir->handle)) {
    return EINVAL;
  }

  errno = 0;
  VERIFY(AEE_SUCCESS == (nErr = apps_std_get_dirinfo(dir, &dirinfo)));

  nErr = closedir((DIR *)dir->handle);
  if (nErr != AEE_SUCCESS) {
    nErr = ERRNO;
    goto bail;
  } else {
    pthread_mutex_lock(&apps_std_mt);
    QNode_Dequeue(&dirinfo->qn);
    pthread_mutex_unlock(&apps_std_mt);
    free(dirinfo);
    dirinfo = NULL;
  }
bail:
  if (nErr != AEE_SUCCESS) {
    VERIFY_EPRINTF("Error 0x%x: failed to closedir, errno is %s\n", nErr,
                   strerror(ERRNO));
  }
  return nErr;
}

__QAIC_IMPL_EXPORT int
__QAIC_HEADER(apps_std_readdir)(const apps_std_DIR *dir,
                                apps_std_DIRENT *dirent,
                                int *bEOF) __QAIC_IMPL_ATTRIBUTE {
  int nErr = AEE_SUCCESS;
  struct apps_std_dir_info *dirinfo = 0;
  struct dirent *odirent;

  if ((NULL == dir) || (0 == dir->handle)) {
    return EINVAL;
  }

  errno = 0;
  VERIFY(AEE_SUCCESS == (nErr = apps_std_get_dirinfo(dir, &dirinfo)));
  *bEOF = 0;
  odirent = readdir((DIR *)dir->handle);
  if (odirent != NULL) {
    dirent->ino = (int)odirent->d_ino;
    strlcpy(dirent->name, odirent->d_name, sizeof(dirent->name));
  } else {
    if (errno == 0) {
      *bEOF = 1;
    } else {
      nErr = ERRNO;
      goto bail;
    }
  }
bail:
  if (nErr != AEE_SUCCESS) {
    VERIFY_EPRINTF("Error 0x%x: failed to readdir,errno is %s\n", nErr,
                   strerror(ERRNO));
  }
  return nErr;
}

__QAIC_IMPL_EXPORT int __QAIC_HEADER(apps_std_mkdir)(const char *name, int mode)
    __QAIC_IMPL_ATTRIBUTE {
  int nErr = AEE_SUCCESS;
  uint64_t tdiff = 0;

  if (NULL == name) {
    return EINVAL;
  }
  FASTRPC_ATRACE_BEGIN();
  errno = 0;
  PROFILE_ALWAYS(&tdiff,
      nErr = mkdir(name, mode);
  );
  if (nErr != AEE_SUCCESS) {
    nErr = ERRNO;
    VERIFY_EPRINTF("Error 0x%x: failed to mkdir %s,errno is %s\n", nErr, name,
                   strerror(ERRNO));
  }
  FASTRPC_ATRACE_END_L("%s done for %s mode %d in %"PRIu64" us error_code 0x%x",
      __func__, name, mode, tdiff, nErr);
  return nErr;
}

__QAIC_IMPL_EXPORT int
__QAIC_HEADER(apps_std_rmdir)(const char *name) __QAIC_IMPL_ATTRIBUTE {
  int nErr = AEE_SUCCESS;

  if (NULL == name) {
    return EINVAL;
  }
  errno = 0;
  nErr = rmdir(name);
  if (nErr != AEE_SUCCESS) {
    nErr = ERRNO;
    VERIFY_EPRINTF("Error 0x%x: failed to rmdir %s,errno is %s\n", nErr, name,
                   strerror(ERRNO));
  }

  return nErr;
}

__QAIC_IMPL_EXPORT int
__QAIC_HEADER(apps_std_stat)(const char *name,
                             apps_std_STAT *ist) __QAIC_IMPL_ATTRIBUTE {
  int nErr = AEE_SUCCESS, nOpenErr = AEE_SUCCESS, fd = -1;
  apps_std_FILE ps;
  struct apps_std_info *sinfo = 0;
  struct stat st;
   uint64_t tdiff = 0;

  if ((NULL == name) || (NULL == ist)) {
    return EINVAL;
  }
  FASTRPC_ATRACE_BEGIN_L("%s for file %s", __func__, name);
  errno = 0;
  VERIFYM(0 == (nOpenErr = apps_std_fopen_with_env(ADSP_LIBRARY_PATH, ";", name,
                                                   "r", &ps)),
          AEE_EFILE, "Error: %s failed as fopen failed err 0x%x", __func__,
          nErr);
  VERIFY(0 == (nErr = apps_std_FILE_get(ps, &sinfo)));
  VERIFYC(-1 != (fd = fileno(sinfo->u.stream)), ERRNO);
  PROFILE_ALWAYS(&tdiff,
  nErr = fstat(fd, &st);;
  );
  VERIFYC(nErr == AEE_SUCCESS, ERRNO);  
  ist->dev = st.st_dev;
  ist->ino = st.st_ino;
  ist->mode = st.st_mode;
  ist->nlink = st.st_nlink;
  ist->rdev = st.st_rdev;
  ist->size = st.st_size;
  ist->atime = (int64_t)st.st_atim.tv_sec;
  ist->atimensec = (int64_t)st.st_atim.tv_nsec;
  ist->mtime = (int64_t)st.st_mtim.tv_sec;
  ist->mtimensec = (int64_t)st.st_mtim.tv_nsec;
  ist->ctime = (int64_t)st.st_ctim.tv_nsec;
  ist->ctimensec = (int64_t)st.st_ctim.tv_nsec;
bail:
  if (nErr != AEE_SUCCESS) {
    VERIFY_EPRINTF(
        "Error 0x%x: %s: failed to stat %s, file open returned 0x%x (%s)\n",
        nErr, __func__, name, nOpenErr, strerror(ERRNO));
    nErr = ERRNO;
  }
  if (nOpenErr == AEE_SUCCESS) {
    apps_std_fclose(ps);
    sinfo = 0;
  }
  if (sinfo) {
    apps_std_FILE_free(sinfo);
  }
  FASTRPC_ATRACE_END_L("%s done for %s in %"PRIu64" us \
      fd 0x%x error_code 0x%x", __func__, name, tdiff, ps, nErr);
  return nErr;
}

__QAIC_HEADER_EXPORT int
__QAIC_HEADER(apps_std_ftrunc)(apps_std_FILE sin,
                               int offset) __QAIC_HEADER_ATTRIBUTE {
  int nErr = 0, fd = -1;
  struct apps_std_info *sinfo = 0;

  FASTRPC_ATRACE_BEGIN_L("%s for file with fd 0x%x for length %d", __func__,
                         sin, offset);
  VERIFY(0 == (nErr = apps_std_FILE_get(sin, &sinfo)));
  errno = 0;
  VERIFYC(-1 != (fd = fileno(sinfo->u.stream)), ERRNO);

  VERIFYC(0 == ftruncate(fd, offset), ERRNO);
bail:
  if (nErr != AEE_SUCCESS) {
    VERIFY_EPRINTF("Error 0x%x: failed to ftrunc file, errno is %s\n", nErr,
                   strerror(ERRNO));
  }
  FASTRPC_ATRACE_END();
  return nErr;
}

__QAIC_IMPL_EXPORT int
__QAIC_IMPL(apps_std_frename)(const char *oldname,
                              const char *newname) __QAIC_IMPL_ATTRIBUTE {
  int nErr = AEE_SUCCESS;

  if (NULL == oldname || NULL == newname)
    return EINVAL;
  FASTRPC_ATRACE_BEGIN_L("%s for file with oldname %s to new name %s", __func__,
                         oldname, newname);
  nErr = rename(oldname, newname);
  if (nErr != AEE_SUCCESS) {
    nErr = ERRNO;
    VERIFY_EPRINTF("Error 0x%x: failed to rename file, errno is %s\n", nErr,
                   strerror(ERRNO));
  }
  FASTRPC_ATRACE_END();
  return nErr;
}
