// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef VERIFY_PRINT_ERROR
#define VERIFY_PRINT_ERROR
#endif // VERIFY_PRINT_ERROR

#ifndef VERIFY_PRINT_ERROR_ALWAYS
#define VERIFY_PRINT_ERROR_ALWAYS
#endif // VERIFY_PRINT_ERROR_ALWAYS

#ifndef VERIFY_PRINT_WARN
#define VERIFY_PRINT_WARN
#endif // VERIFY_PRINT_WARN

#define FARF_ERROR 1

#include "AEEStdErr.h"
#include "AEEstd.h"
#include "HAP_farf.h"
#include "adsp_current_process.h"
#include "adsp_current_process1.h"
#include "adspmsgd_adsp.h"
#include "adspmsgd_adsp1.h"
#include "adspmsgd_internal.h"
#include "apps_std_internal.h"
#include "fastrpc_common.h"
#include "fastrpc_internal.h"
#include "rpcmem.h"
#include "verify.h"
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <unistd.h>

#define EVENT_SIZE (sizeof(struct inotify_event))
#define EVENT_BUF_LEN (1024 * (EVENT_SIZE + 16))
#ifndef AEE_EUNSUPPORTED
#define AEE_EUNSUPPORTED 20 // API is not supported 	50
#endif
#define DEFAULT_ADSPMSGD_MEMORY_SIZE 8192
#define INVALID_HANDLE (remote_handle64)(-1)
#define ERRNO (errno == 0 ? -1 : errno)
#define ADSPMSGD_FILTER                                                        \
  0x1f001f // Filter passed to adspmsgd init API to push DSP messages to logcat

#define MAX_FARF_FILE_SIZE (511)

/*
 * Depending on the operating system, the default DSP_SEARCH_PATH gets fetched.
 * e.g:- _WIN32    :  DSP_SEARCH_PATH=";c:\\Program
 * Files\\Qualcomm\\RFSA\\aDSP;"; LE_ENABLE :
 * DSP_SEARCH_PATH=";/usr/lib/rfsa/adsp;/dsp;"; This is the maximum possible
 * length of the DSP_SEARCH_PATH.
 */
#define ENV_PATH_LEN 256

struct log_config_watcher_params {
  int fd;
  int event_fd; // Duplicate fd to quit the poll
  _cstring1_t *paths;
  int *wd;
  uint32_t numPaths;
  pthread_attr_t attr;
  pthread_t thread;
  unsigned char stopThread;
  int asidToWatch;
  char *fileToWatch;
  char *asidFileToWatch;
  char *pidFileToWatch;
  bool adspmsgdEnabled;
  bool file_watcher_init_flag;
  /* Adds "int domain;" (hash key) + "UT_hash_handle hh;" so this
   * struct can be stored as a node of the log_config_watcher hash
   * table below, replacing the old
   * "log_config_watcher[NUM_DOMAINS_EXTEND]" flat array. */
  ADD_DOMAIN_HASH();
};

/* Sparse, hash-table-backed replacement for the old flat
 * "log_config_watcher[NUM_DOMAINS_EXTEND]" array -- keyed by effective
 * domain id, using the same fastrpc_hash_table.h idiom used elsewhere
 * (fastrpc_apps_user.c, adspmsgd.c, dspsignal.c). Nodes are calloc'd
 * lazily, on first use, instead of being densely pre-allocated up to a
 * fixed compile-time ceiling. */
DECLARE_HASH_TABLE(log_config, struct log_config_watcher_params)
static pthread_once_t log_config_table_once = PTHREAD_ONCE_INIT;

static void log_config_table_init_once(void) {
  HASH_TABLE_INIT(struct log_config_watcher_params);
}

/* Fetch-or-create accessor for a domain's file-watcher bookkeeping. */
static struct log_config_watcher_params *log_config_watcher_get(int domain) {
  int nErr = AEE_SUCCESS;
  struct log_config_watcher_params *me = NULL;

  pthread_once(&log_config_table_once, log_config_table_init_once);
  GET_HASH_NODE(struct log_config_watcher_params, domain, me);
  if (!me) {
    ALLOC_AND_ADD_NEW_NODE_TO_TABLE(struct log_config_watcher_params, domain,
                                    me);
  }
bail:
  return me;
}

extern const char *__progname;
void set_runtime_logmask(uint32_t);

const char *get_domain_str(int domain);

static int parseLogConfig(int dom, unsigned int mask, char *filenames) {
  _cstring1_t *filesToLog = NULL;
  int filesToLogLen = 0;
  char *tempFiles = NULL;
  int nErr = AEE_SUCCESS;
  char *saveptr = NULL;
  char *path = NULL;
  char delim[] = {','};
  int maxPathLen = 0;
  int i = 0;
  remote_handle64 handle;
  struct log_config_watcher_params *lcw = log_config_watcher_get(dom);

  VERIFYC(NULL != lcw, AEE_ENOMEMORY);
  VERIFYC(filenames != NULL, AEE_ERPC);

  VERIFYC(NULL !=
              (tempFiles = malloc(sizeof(char) * (strlen(filenames) + 1))),
          AEE_ENOMEMORY);
  strlcpy(tempFiles, filenames, strlen(filenames) + 1);

  // Get the number of folders and max size needed
  path = strtok_r(tempFiles, delim, &saveptr);
  while (path != NULL) {
    maxPathLen = STD_MAX(maxPathLen, (int)strlen(path)) + 1;
    filesToLogLen++;
    path = strtok_r(NULL, delim, &saveptr);
  }

  VERIFY_IPRINTF("%s: #files: %d max_len: %d\n",
                 lcw->fileToWatch, filesToLogLen,
                 maxPathLen);

  // Allocate memory
  VERIFYC(NULL != (filesToLog = malloc(sizeof(_cstring1_t) * filesToLogLen)),
          AEE_ENOMEMORY);
  for (i = 0; i < filesToLogLen; ++i) {
    VERIFYC(NULL != (filesToLog[i].data = malloc(sizeof(char) * maxPathLen)),
            AEE_ENOMEMORY);
    filesToLog[i].dataLen = maxPathLen;
  }

  // Get the number of folders and max size needed
  strlcpy(tempFiles, filenames, strlen(filenames) + 1);
  i = 0;
  path = strtok_r(tempFiles, delim, &saveptr);
  while (path != NULL) {
    VERIFYC((filesToLog != NULL) && (filesToLog[i].data != NULL) &&
                filesToLog[i].dataLen >= (int)strlen(path),
            AEE_ERPC);
    strlcpy(filesToLog[i].data, path, filesToLog[i].dataLen);
    VERIFY_IPRINTF("%s: %s\n", lcw->fileToWatch,
                   filesToLog[i].data);
    path = strtok_r(NULL, delim, &saveptr);
    i++;
  }

  handle = get_adsp_current_process1_handle(dom);
  if (handle != INVALID_HANDLE) {
    if (AEE_SUCCESS != (nErr = adsp_current_process1_set_logging_params2(
                            handle, mask, filesToLog, filesToLogLen))) {
      VERIFY(AEE_SUCCESS == (nErr = adsp_current_process1_set_logging_params(
                                 handle, mask, filesToLog, filesToLogLen)));
    }
  } else {
    if (AEE_SUCCESS != (nErr = adsp_current_process_set_logging_params2(
                            mask, filesToLog, filesToLogLen))) {
      VERIFY(AEE_SUCCESS == (nErr = adsp_current_process_set_logging_params(
                                 mask, filesToLog, filesToLogLen)));
    }
  }

bail:
  if (filesToLog) {
    for (i = 0; i < filesToLogLen; ++i) {
      if (filesToLog[i].data != NULL) {
        free(filesToLog[i].data);
        filesToLog[i].data = NULL;
      }
    }
    free(filesToLog);
    filesToLog = NULL;
  }

  if (tempFiles) {
    free(tempFiles);
    tempFiles = NULL;
  }
  if (nErr != AEE_SUCCESS) {
    VERIFY_EPRINTF("Error 0x%x: parse log config failed. domain %d, mask %x, "
                   "filename %s\n",
                   nErr, dom, mask, filenames);
  }
  return nErr;
}

// Read log config given the filename
static int readLogConfigFromPath(int dom, const char *base, const char *file) {
  int nErr = 0;
  apps_std_FILE fp = -1;
  uint64_t len;
  unsigned char *buf = NULL;
  int readlen = 0, eof;
  unsigned int mask = 0;
  char *path = NULL;
  char *filenames = NULL;
  bool fileExists = false;
  int buf_addr = 0;
  remote_handle64 handle;
  uint64_t farf_logmask = 0;
  struct log_config_watcher_params *lcw = log_config_watcher_get(dom);

  VERIFYC(NULL != lcw, AEE_ENOMEMORY);
  len = snprintf(0, 0, "%s/%s", base, file) + 1;
  VERIFYC(NULL != (path = malloc(sizeof(char) * len)), AEE_ENOMEMORY);
  snprintf(path, (int)len, "%s/%s", base, file);
  VERIFY(AEE_SUCCESS == (nErr = apps_std_fileExists(path, &fileExists)));
  if (fileExists == false) {
    FARF(RUNTIME_RPC_HIGH, "%s: Couldn't find file: %s\n",
         lcw->fileToWatch, path);
    nErr = AEE_ENOSUCHFILE;
    goto bail;
  }
  if (lcw->adspmsgdEnabled == false) {
    handle = get_adspmsgd_adsp1_handle(dom);
    if (handle != INVALID_HANDLE) {
      if ((nErr = adspmsgd_init(handle, ADSPMSGD_FILTER)) ==
          (int)(AEE_EUNSUPPORTED + DSP_AEE_EOFFSET))
        adspmsgd_adsp1_init2(handle);
    } else if ((nErr = adspmsgd_adsp_init2()) ==
               (int)(AEE_EUNSUPPORTED + DSP_AEE_EOFFSET)) {
      nErr = adspmsgd_adsp_init(0, RPCMEM_HEAP_DEFAULT, 0,
                                DEFAULT_ADSPMSGD_MEMORY_SIZE, &buf_addr);
    }
    if (nErr != AEE_SUCCESS) {
      VERIFY_EPRINTF("adspmsgd not supported. nErr=%x\n", nErr);
    } else {
      lcw->adspmsgdEnabled = true;
    }
    VERIFY_EPRINTF("Found %s. adspmsgd enabled \n",
                   lcw->fileToWatch);
  }

  VERIFY(AEE_SUCCESS == (nErr = apps_std_fopen(path, "r", &fp)));
  VERIFY(AEE_SUCCESS == (nErr = apps_std_flen(fp, &len)));

  VERIFYM(len <= MAX_FARF_FILE_SIZE, AEE_ERPC,
          "len greater than %d for path %s (%s)\n", nErr, MAX_FARF_FILE_SIZE,
          path, strerror(ERRNO));
  VERIFYC(NULL != (buf = calloc(1, sizeof(unsigned char) * (len + 1))),
          AEE_ENOMEMORY); // extra 1 unsigned char for null character
  VERIFYC(NULL != (filenames = malloc(sizeof(unsigned char) * len)), AEE_ENOMEMORY);
  VERIFY(AEE_SUCCESS == (nErr = apps_std_fread(fp, buf, len, &readlen, &eof)));
  VERIFYC((int)len == readlen, AEE_ERPC);

  FARF(RUNTIME_RPC_HIGH, "%s: Config file %s contents: %s\n",
       lcw->fileToWatch, path, buf);

  // Parse farf file to get logmasks.
  len = sscanf((const char *)buf, "0x%" SCNx64 " %511s", &farf_logmask, filenames);

  if (farf_logmask == LLONG_MAX || farf_logmask == (uint64_t)LLONG_MIN ||
      farf_logmask == 0) {
    VERIFY_EPRINTF("Error : Invalid FARF logmask!");
  }
  /*
   * Parsing logmask to get userspace and kernel space masks.
   * Example: For farf_logmask = 0x001f001f001f001f, this enables all Runtime
   * levels
   *
   * i.e.: 0x    001f001f        001f001f
   *           |__________|    |__________|
   *             Userspace       DSP space
   */
  mask = farf_logmask & 0xffffffff;
  set_runtime_logmask(farf_logmask >> 32);
  switch (len) {
  case 1:
    FARF(RUNTIME_RPC_HIGH, "%s: Setting log mask:0x%x",
         lcw->fileToWatch, mask);
    handle = get_adsp_current_process1_handle(dom);
    if (handle != INVALID_HANDLE) {
      if (AEE_SUCCESS != (nErr = adsp_current_process1_set_logging_params2(
                              handle, mask, NULL, 0))) {
        VERIFY(AEE_SUCCESS == (nErr = adsp_current_process1_set_logging_params(
                                   handle, mask, NULL, 0)));
      }
    } else {
      if (AEE_SUCCESS !=
          (nErr = adsp_current_process_set_logging_params2(mask, NULL, 0))) {
        VERIFY(AEE_SUCCESS ==
               (nErr = adsp_current_process_set_logging_params(mask, NULL, 0)));
      }
    }
    break;
  case 2:
    VERIFY(AEE_SUCCESS == (nErr = parseLogConfig(dom, mask, filenames)));
    FARF(RUNTIME_RPC_HIGH, "%s: Setting log mask:0x%x, filename:%s",
         lcw->fileToWatch, mask, filenames);
    break;
  default:
    VERIFY_EPRINTF("Error : %s: No valid data found in config file %s",
                   lcw->fileToWatch, path);
    nErr = AEE_EUNSUPPORTED;
    goto bail;
  }

bail:
  if (buf != NULL) {
    free(buf);
    buf = NULL;
  }

  if (filenames != NULL) {
    free(filenames);
    filenames = NULL;
  }

  if (fp != -1) {
    apps_std_fclose(fp);
  }

  if (path != NULL) {
    free(path);
    path = NULL;
  }

  if (nErr != AEE_SUCCESS && nErr != AEE_ENOSUCHFILE) {
    VERIFY_EPRINTF("Error 0x%x: fopen failed for %s/%s. (%s)\n", nErr, base,
                   file, strerror(ERRNO));
  }
  return nErr;
}

// Read log config given the watch descriptor
static int readLogConfigFromEvent(int dom, struct inotify_event *event) {
  int i = 0;
  struct log_config_watcher_params *lcw = log_config_watcher_get(dom);

  if (!lcw)
    return AEE_ENOMEMORY;

  // Ensure we are looking at the right file
  for (i = 0; i < (int)lcw->numPaths; ++i) {
    if (lcw->wd[i] == event->wd) {
      if (strcmp(lcw->fileToWatch, event->name) == 0) {
        return readLogConfigFromPath(dom, lcw->paths[i].data,
                                     lcw->fileToWatch);
      } else if (strcmp(lcw->asidFileToWatch,
                            event->name) == 0) {
        return readLogConfigFromPath(dom, lcw->paths[i].data,
                                     lcw->asidFileToWatch);
      } else if (strcmp(lcw->pidFileToWatch,
                            event->name) == 0) {
        return readLogConfigFromPath(dom, lcw->paths[i].data,
                                     lcw->pidFileToWatch);
      }
    }
  }
  VERIFY_IPRINTF("%s: Watch descriptor %d not valid for current process",
                 lcw->fileToWatch, event->wd);
  return AEE_SUCCESS;
}

// Read log config given the watch descriptor
static int resetLogConfigFromEvent(int dom, struct inotify_event *event) {
  int i = 0;
  remote_handle64 handle;
  struct log_config_watcher_params *lcw = log_config_watcher_get(dom);

  if (!lcw)
    return AEE_ENOMEMORY;

  // Ensure we are looking at the right file
  for (i = 0; i < (int)lcw->numPaths; ++i) {
    if (lcw->wd[i] == event->wd) {
      if ((strcmp(lcw->fileToWatch, event->name) == 0) ||
          (strcmp(lcw->asidFileToWatch, event->name) ==
           0) ||
          (strcmp(lcw->pidFileToWatch, event->name) ==
           0)) {
        if (lcw->adspmsgdEnabled == true) {
          adspmsgd_stop(dom);
          lcw->adspmsgdEnabled = false;
          handle = get_adspmsgd_adsp1_handle(dom);
          if (handle != INVALID_HANDLE) {
            adspmsgd_adsp1_deinit(handle);
          } else {
            adspmsgd_adsp_deinit();
          }
        }
        handle = get_adsp_current_process1_handle(dom);
        if (handle != INVALID_HANDLE) {
          return adsp_current_process1_set_logging_params(handle, 0, NULL, 0);
        } else {
          return adsp_current_process_set_logging_params(0, NULL, 0);
        }
      }
    }
  }
  VERIFY_IPRINTF("%s: Watch descriptor %d not valid for current process",
                 lcw->fileToWatch, event->wd);
  return AEE_SUCCESS;
}

static void *file_watcher_thread(void *arg) {
  int dom = (int)(uintptr_t)arg;
  int ret = 0, current_errno = 0, env_list_len = 0;
  int length = 0;
  int nErr = AEE_SUCCESS;
  int i = 0;
  char buffer[EVENT_BUF_LEN];
  struct log_config_watcher_params *lcw = log_config_watcher_get(dom);
  struct pollfd pfd[2];
  const char *fileExtension = ".farf";
  int len = 0;
  remote_handle64 handle;
  int file_found = 0;
  char *data_paths = NULL;
  const char *dsp_search_path = NULL;

  if (!lcw)
    return NULL;
  pfd[0] = (struct pollfd){lcw->fd, POLLIN, 0};
  pfd[1] = (struct pollfd){lcw->event_fd, POLLIN, 0};
  FARF(ALWAYS, "%s starting for domain %d\n", __func__, dom);
  dsp_search_path = get_dsp_search_path();
  // Check for the presence of the <process_name>.farf file at bootup
  for (i = 0; i < (int)lcw->numPaths; ++i) {
    if (0 == readLogConfigFromPath(dom, lcw->paths[i].data,
                                   lcw->fileToWatch)) {
      file_found = 1;
      VERIFY_IPRINTF("%s: Log config File %s found.\n",
                     lcw->fileToWatch,
                     lcw->paths[i].data);
      break;
    }
  }
  if (!file_found) {
    // Allocate single buffer for all the paths.
    data_paths = calloc(1, sizeof(char) * ENV_PATH_LEN);
    if (data_paths) {
      current_errno = errno;
      // Get DSP_LIBRARY_PATH env variable path set by the user.
      ret = apps_std_getenv(DSP_LIBRARY_PATH, data_paths, ENV_PATH_LEN,
                            &env_list_len);
      errno = current_errno;
      if (ret != 0)
        strlcpy(data_paths, dsp_search_path, ENV_PATH_LEN);
      VERIFY_WPRINTF("%s: Couldn't find file %s, errno (%s) at %s\n", __func__,
                     lcw->fileToWatch, strerror(errno),
                     data_paths);
    } else {
      VERIFY_WPRINTF(
          "%s: Calloc failed for %d bytes. Couldn't find file %s, errno (%s)\n",
          __func__, ENV_PATH_LEN, lcw->fileToWatch,
          strerror(errno));
    }
  }

  while (lcw->stopThread == 0) {
    // Block forever
    ret = poll(pfd, 2, -1);
    if (ret < 0) {
      VERIFY_EPRINTF("Error : %s: Error polling for file change. Runtime FARF "
                     "will not work for this process. errno=%x !",
                     lcw->fileToWatch, errno);
      break;
    } else if (pfd[1].revents & POLLIN) { // Check for exit
      VERIFY_WPRINTF("Warning: %s received exit for domain %d, file %s\n",
                     __func__, dom, lcw->fileToWatch);
      break;
    } else {
      length = read(lcw->fd, buffer, EVENT_BUF_LEN);
      i = 0;
      while (i < length) {
        struct inotify_event *event = (struct inotify_event *)&buffer[i];
        if (event->len) {
          // Get the asiD for the current process
          // Do it once only
          if (lcw->asidToWatch == -1) {
            handle = get_adsp_current_process1_handle(dom);
            if (handle != INVALID_HANDLE) {
              VERIFY(
                  AEE_SUCCESS ==
                  (nErr = adsp_current_process1_getASID(
                       handle,
                       (unsigned int *)&lcw->asidToWatch)));
            } else {
              VERIFY(
                  AEE_SUCCESS ==
                  (nErr = adsp_current_process_getASID(
                       (unsigned int *)&lcw->asidToWatch)));
            }
            len = strlen(fileExtension) + strlen(__TOSTR__(INT_MAX));
            VERIFYC(NULL != (lcw->asidFileToWatch =
                                 malloc(sizeof(char) * len)),
                    AEE_ENOMEMORY);
            snprintf(lcw->asidFileToWatch, len, "%d%s",
                     lcw->asidToWatch, fileExtension);
            VERIFY_IPRINTF("%s: Watching ASID file %s\n",
                           lcw->fileToWatch,
                           lcw->asidFileToWatch);
          }

          VERIFY_IPRINTF("%s: %s %d.\n", lcw->fileToWatch,
                         event->name, event->mask);
          if ((event->mask & IN_CREATE) || (event->mask & IN_MODIFY)) {
            VERIFY_IPRINTF("%s: File %s created.\n",
                           lcw->fileToWatch, event->name);
            if (0 != readLogConfigFromEvent(dom, event)) {
              VERIFY_EPRINTF("Error : %s: Error reading config file %s",
                             lcw->fileToWatch,
                             lcw->paths[i].data);
            }
          } else if (event->mask & IN_DELETE) {
            VERIFY_IPRINTF("%s: File %s deleted.\n",
                           lcw->fileToWatch, event->name);
            if (0 != resetLogConfigFromEvent(dom, event)) {
              VERIFY_EPRINTF(
                  "Error : %s: Error resetting FARF runtime log config",
                  lcw->fileToWatch);
            }
          }
        }

        i += EVENT_SIZE + event->len;
      }
    }
  }
bail:
  if (data_paths) {
    free(data_paths);
    data_paths = NULL;
  }

  if (nErr != AEE_SUCCESS) {
    VERIFY_EPRINTF("Error 0x%x: %s exited. Runtime FARF will not work for this "
                   "process. filename %s (errno %s)\n",
                   nErr, __func__, lcw->fileToWatch,
                   strerror(errno));
  } else {
    FARF(ALWAYS, "%s exiting for domain %d\n", __func__, dom);
  }
  return NULL;
}

void deinitFileWatcher(int dom) {
  int i = 0;
  uint64_t stop = 10;
  remote_handle64 handle;
  ssize_t sz = 0;
  struct log_config_watcher_params *lcw = log_config_watcher_get(dom);

  if (!lcw)
    return;

  if (lcw->file_watcher_init_flag) {
    lcw->stopThread = 1;
    if (0 <= lcw->event_fd) {
      for (i = 0; i < RETRY_WRITE; i++) {
        VERIFY_IPRINTF(
            "Writing to file_watcher_thread event_fd %d for domain %d\n",
            lcw->event_fd, dom);
        sz = write(lcw->event_fd, &stop, sizeof(uint64_t));
        if ((sz < (ssize_t)sizeof(uint64_t)) || (sz == -1 && errno == EAGAIN)) {
          VERIFY_WPRINTF("Warning: Written %zd bytes on event_fd %d for domain "
                         "%d (errno = %s): Retrying ...\n",
                         sz, lcw->event_fd, dom,
                         strerror(errno));
          continue;
        } else {
          break;
        }
      }
    }
    if (sz != sizeof(uint64_t) && 0 <= lcw->event_fd) {
      VERIFY_EPRINTF("Error: Written %zd bytes on event_fd %d for domain %d: "
                     "Cannot set exit flag to watcher thread (errno = %s)\n",
                     sz, lcw->event_fd, dom,
                     strerror(errno));
      // When deinitFileWatcher fail to write dupfd, file watcher thread hangs
      // on poll. Abort in this case.
      raise(SIGABRT);
    }
  }
  if (lcw->thread) {
    pthread_join(lcw->thread, NULL);
    lcw->thread = 0;
  }
  if (lcw->fileToWatch) {
    free(lcw->fileToWatch);
    lcw->fileToWatch = 0;
  }
  if (lcw->asidFileToWatch) {
    free(lcw->asidFileToWatch);
    lcw->asidFileToWatch = 0;
  }
  if (lcw->pidFileToWatch) {
    free(lcw->pidFileToWatch);
    lcw->pidFileToWatch = 0;
  }
  if (lcw->wd) {
    for (i = 0; i < (int)lcw->numPaths; ++i) {
      // On success, inotify_add_watch() returns a nonnegative integer watch
      // descriptor
      if (lcw->wd[i] >= 0) {
        inotify_rm_watch(lcw->fd,
                         lcw->wd[i]);
      }
    }
    free(lcw->wd);
    lcw->wd = NULL;
  }
  if (lcw->paths) {
    for (i = 0; i < (int)lcw->numPaths; ++i) {
      if (lcw->paths[i].data) {
        free(lcw->paths[i].data);
        lcw->paths[i].data = NULL;
      }
    }
    free(lcw->paths);
    lcw->paths = NULL;
  }
  if (lcw->fd != 0) {
    close(lcw->fd);
    VERIFY_IPRINTF("Closed file watcher fd %d for domain %d\n",
                   lcw->fd, dom);
    lcw->fd = 0;
  }
  if (lcw->adspmsgdEnabled == true) {
    adspmsgd_stop(dom);
    handle = get_adspmsgd_adsp1_handle(dom);
    if (handle != INVALID_HANDLE) {
      adspmsgd_adsp1_deinit(handle);
    } else {
      adspmsgd_adsp_deinit();
    }
    lcw->adspmsgdEnabled = false;
  }
  if (lcw->file_watcher_init_flag &&
      (lcw->event_fd != -1)) {
    close(lcw->event_fd);
    VERIFY_IPRINTF("Closed file watcher eventfd %d for domain %d\n",
                   lcw->event_fd, dom);
    lcw->event_fd = -1;
  }
  lcw->file_watcher_init_flag = false;
  lcw->numPaths = 0;
}

int initFileWatcher(int dom) {
  int nErr = AEE_SUCCESS;
  const char *fileExtension = ".farf";
  uint32_t len = 0;
  uint16_t maxPathLen = 0;
  int i = 0;
  char *name = NULL;
  struct log_config_watcher_params *lcw = log_config_watcher_get(dom);

  VERIFYC(NULL != lcw, AEE_ENOMEMORY);
  /* Reset only this watcher's own fields, not the hash-table
   * bookkeeping (the "domain" key + UT_hash_handle) ADD_DOMAIN_HASH()
   * appends at the end of this struct -- zeroing those while the node
   * is already linked into the hash table would corrupt uthash's
   * internal bucket chains. */
  memset(lcw, 0, offsetof(struct log_config_watcher_params, domain));
  lcw->asidToWatch = 0;
  lcw->event_fd = -1;

  VERIFYC(NULL != (name = std_basename(__progname)), AEE_EBADPARM);

  len = strlen(name) + strlen(fileExtension) + 1;
  VERIFYC(NULL != (lcw->fileToWatch =
                       malloc(sizeof(char) * len)),
          AEE_ENOMEMORY);
  snprintf(lcw->fileToWatch, len, "%s%s", name,
           fileExtension);

  len = strlen(fileExtension) + strlen(__TOSTR__(INT_MAX));
  VERIFYC(NULL != (lcw->pidFileToWatch =
                       malloc(sizeof(char) * len)),
          AEE_ENOMEMORY);
  snprintf(lcw->pidFileToWatch, len, "%d%s", getpid(),
           fileExtension);

  VERIFY_IPRINTF("%s: Watching PID file: %s\n",
                 lcw->fileToWatch,
                 lcw->pidFileToWatch);

  lcw->fd = inotify_init();
  if (lcw->fd < 0) {
    nErr = AEE_ERPC;
    VERIFY_EPRINTF("Error 0x%x: inotify_init failed, invalid fd errno = %s\n",
                   nErr, strerror(errno));
    goto bail;
  }

  // Duplicate the fd, so we can use it to quit polling
  lcw->event_fd = eventfd(0, 0);
  if (lcw->event_fd < 0) {
    nErr = AEE_ERPC;
    VERIFY_EPRINTF("Error 0x%x: eventfd in dup failed, invalid fd errno %s\n",
                   nErr, strerror(errno));
    goto bail;
  }
  lcw->file_watcher_init_flag = true;
  VERIFY_IPRINTF("Opened file watcher fd %d eventfd %d for domain %d\n",
                 lcw->fd, lcw->event_fd,
                 dom);

  // Get the required size
  apps_std_get_search_paths_with_env(ADSP_LIBRARY_PATH, ";", NULL, 0,
                                     &lcw->numPaths,
                                     &maxPathLen);

  maxPathLen += +1;

  // Allocate memory
  VERIFYC(NULL != (lcw->paths = malloc(
                       sizeof(_cstring1_t) * lcw->numPaths)),
          AEE_ENOMEMORY);
  VERIFYC(NULL != (lcw->wd =
                       malloc(sizeof(int) * lcw->numPaths)),
          AEE_ENOMEMORY);

  for (i = 0; i < (int)lcw->numPaths; ++i) {
    VERIFYC(NULL != (lcw->paths[i].data =
                         malloc(sizeof(char) * maxPathLen)),
            AEE_ENOMEMORY);
    lcw->paths[i].dataLen = maxPathLen;
  }

  // Get the paths
  VERIFY(AEE_SUCCESS ==
         (nErr = apps_std_get_search_paths_with_env(
              ADSP_LIBRARY_PATH, ";", lcw->paths,
              lcw->numPaths, &len, &maxPathLen)));

  maxPathLen += 1;

  VERIFY_IPRINTF("%s: Watching folders:\n",
                 lcw->fileToWatch);
  for (i = 0; i < (int)lcw->numPaths; ++i) {
    // Watch for creation, deletion and modification of files in path
    VERIFY_IPRINTF("log file watcher: %s: %s\n",
                   lcw->fileToWatch,
                   lcw->paths[i].data);
    if ((lcw->wd[i] = inotify_add_watch(
             lcw->fd, lcw->paths[i].data,
             IN_CREATE | IN_DELETE)) < 0) {
      VERIFY_EPRINTF(
          "Error : Unable to add watcher for folder %s : errno is %s\n",
          lcw->paths[i].data, strerror(ERRNO));
    }
  }

  // Create a thread to watch for file changes
  lcw->asidToWatch = -1;
  lcw->stopThread = 0;
  pthread_create(&lcw->thread, NULL, file_watcher_thread,
                 (void *)(uintptr_t)dom);
bail:
  if (nErr != AEE_SUCCESS) {
    VERIFY_EPRINTF("Error 0x%x: Failed to register with inotify file %s. "
                   "Runtime FARF will not work for the process %s! errno %d",
                   nErr, lcw->fileToWatch, name, errno);
    deinitFileWatcher(dom);
  }

  return nErr;
}
