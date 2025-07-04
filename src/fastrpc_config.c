// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef VERIFY_PRINT_ERROR
#define VERIFY_PRINT_ERROR
#endif // VERIFY_PRINT_ERROR

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "AEEStdErr.h"
#include "AEEstd.h"
#include "HAP_farf.h"
#include "apps_std.h"
#include "fastrpc_common.h"
#include "fastrpc_config.h"
#include "apps_std_internal.h"
#include "verify.h"

#define CONFIG_PDDUMP "pddump"
#define CONFIG_RPCTIMEOUT "rpctimeout"
#define CONFIG_PERF_KERNEL "perfkernel"
#define CONFIG_PERF_DSP "perfdsp"
#define CONFIG_COLLECT_RUNTIME_FARF "collectRuntimeFARF"
#define CONFIG_COLLECT_RUNTIME_FARF_USERSPACE "collectUserspaceRuntimeFARF"
#define CONFIG_LOG_IREGION "logiregion"
#define CONFIG_QTF_TRACING "qtftracing"
#define CONFIG_CALLER_LEVEL "callerlevel"
#define CONFIG_ENABLE_UAF "uafenabled"
#define CONFIG_DEBUG_LOGGING "debuglogging"
#define CONFIG_DEBUG_SYSMON_LOGGING "sysmonreservedbit"
#define CONFIG_ERR_CODES "errcodes"
#define MIN_DSP_ERR_RNG 0x80000401
#define MAX_DSP_ERR_RNG 0x80000601
#define CONFIG_LOGPACKET "logPackets"
#define CONFIG_LEAK_DETECT "leak_detect"
#define CONFIG_CALL_STACK_NUM "num_call_stack"
#define CONFIG_SETDMABUFNAME   "setdmabufname"

struct fastrpc_config_param {
  bool pddump;
  int rpc_timeout;
  bool perfkernel;
  bool perfdsp;
  _cstring1_t *paths;
  char *farf_log_filename;
  char *farf_log_filename_userspace;
  bool log_iregion;
  bool qtf_tracing;
  int caller_level;
  bool uaf_enabled;
  bool debug_logging;
  struct err_codes err_codes_to_crash;
  bool sysmonreservedbit;
  bool logPackets;
  int leak_detect;
  int num_call_stack;
  bool setdmabufname;
};

static struct fastrpc_config_param frpc_config;

// Function to read and parse config file
int fastrpc_read_config_file_from_path(const char *base, const char *file) {
  int nErr = 0;
  apps_std_FILE fp = -1;
  uint64_t len;
  unsigned char *buf = NULL;
  int eof;
  char *path = NULL, *param = NULL, *saveptr = NULL;
  bool fileExists = false;
  char *delim = "=", *delim2 = ",";
  uint64_t logFileNameLen = 0;
  frpc_config.err_codes_to_crash.num_err_codes = 0;

  len = snprintf(0, 0, "%s/%s", base, file) + 1;
  VERIFYC(NULL != (path = calloc(1, sizeof(char) * len)), AEE_ENOMEMORY);
  snprintf(path, (int)len, "%s/%s", base, file);
  VERIFY(AEE_SUCCESS == (nErr = apps_std_fileExists(path, &fileExists)));
  if (fileExists == false) {
    nErr = AEE_ENOSUCHFILE;
    goto bail;
  }

  VERIFY(AEE_SUCCESS == (nErr = apps_std_fopen(path, "r", &fp)));
  VERIFY(AEE_SUCCESS == (nErr = apps_std_flen(fp, &len)));
  // Allocate buffer for reading each line
  VERIFYC(NULL != (buf = calloc(1, sizeof(unsigned char) * (len + 1))),
          AEE_ENOMEMORY); // extra 1 unsigned char for null character

  do {
    // Read each line at a time
    VERIFY(AEE_SUCCESS == (nErr = apps_std_fgets(fp, buf, len, &eof)));
    if (eof) {
      break;
    }
    param = strtok_r((char *)buf, delim, &saveptr);
    if (param == NULL) {
      continue;
    }

    if (strncmp(param, CONFIG_ERR_CODES, strlen(CONFIG_ERR_CODES)) ==
        0) {
      int ii = 0, num_err_codes = 0;
      unsigned int err_code = 0;

      FARF(ALWAYS, "%s: panic error codes applicable only to cdsp domain\n",
           __func__);
      do {
        param = strtok_r(NULL, delim2, &saveptr);
        if (param != NULL) {
          err_code = strtol(param, NULL, 16);
          if (err_code >= MIN_DSP_ERR_RNG && err_code <= MAX_DSP_ERR_RNG) {
            frpc_config.err_codes_to_crash.err_code[ii] = err_code;
            FARF(ALWAYS, "%s : panic error codes : 0x%x\n", __func__,
                 frpc_config.err_codes_to_crash.err_code[ii]);
            ii++;
            if (ii >= MAX_PANIC_ERR_CODES) {
              FARF(ERROR, "%s : Max panic error codes limit reached\n",
                   __func__, MAX_PANIC_ERR_CODES);
              break;
            }
          } else {
            FARF(ALWAYS,
                 "%s : panic error code read from debugcnfig : 0x%x is not in "
                 "dsp error codes range\n",
                 __func__, err_code);
          }
        }
      } while (param != NULL);
      num_err_codes = ii;
      frpc_config.err_codes_to_crash.num_err_codes = num_err_codes;
    } else if (strncmp(param, CONFIG_PDDUMP, strlen(CONFIG_PDDUMP)) ==
               0) {
      param = strtok_r(NULL, delim, &saveptr);
      if (param != NULL && atoi(param)) {
        frpc_config.pddump = true;
        FARF(ALWAYS, "fastrpc config enabling PD dump\n");
      }
    } else if (strncmp(param, CONFIG_RPCTIMEOUT,
                           strlen(CONFIG_RPCTIMEOUT)) == 0) {
      param = strtok_r(NULL, delim, &saveptr);
      if (param != NULL) {
        frpc_config.rpc_timeout = atoi(param);
        FARF(ALWAYS, "fastrpc config set rpc timeout with %d\n",
             frpc_config.rpc_timeout);
      }
    } else if (strncmp(param, CONFIG_PERF_KERNEL,
                           strlen(CONFIG_PERF_KERNEL)) == 0) {
      param = strtok_r(NULL, delim, &saveptr);
      if (param != NULL && atoi(param)) {
        frpc_config.perfkernel = true;
        FARF(ALWAYS, "fastrpc config enabling profiling on kernel\n");
      }
    } else if (strncmp(param, CONFIG_PERF_DSP,
                           strlen(CONFIG_PERF_DSP)) == 0) {
      param = strtok_r(NULL, delim, &saveptr);
      if (param != NULL && atoi(param)) {
        frpc_config.perfdsp = true;
        FARF(ALWAYS, "fastrpc config enabling profiling on dsp\n");
      }
    } else if (strncmp(param, CONFIG_COLLECT_RUNTIME_FARF,
                           strlen(CONFIG_COLLECT_RUNTIME_FARF)) == 0) {
      param = strtok_r(NULL, delim, &saveptr);
      if (param != NULL) {
        logFileNameLen = strlen(param) + 1;
        VERIFYC(NULL != (frpc_config.farf_log_filename =
                             (char *)malloc(sizeof(char) * logFileNameLen)),
                AEE_ENOMEMORY);
        strlcpy(frpc_config.farf_log_filename, param, logFileNameLen);
        FARF(ALWAYS,
             "fastrpc config enabling farf logs collection into file %s",
             frpc_config.farf_log_filename);
      }
    } else if (strncmp(param, CONFIG_COLLECT_RUNTIME_FARF_USERSPACE,
                           strlen(CONFIG_COLLECT_RUNTIME_FARF_USERSPACE)) ==
               0) {
      param = strtok_r(NULL, delim, &saveptr);
      if (param != NULL) {
        logFileNameLen = strlen(param) + 1;
        VERIFYC(NULL != (frpc_config.farf_log_filename_userspace =
                             (char *)malloc(sizeof(char) * logFileNameLen)),
                AEE_ENOMEMORY);
        strlcpy(frpc_config.farf_log_filename_userspace, param,
                    logFileNameLen);
        FARF(ALWAYS,
             "fastrpc config enabling userspace farf logs collection into file "
             "%s",
             frpc_config.farf_log_filename_userspace);
      }
    } else if (strncmp(param, CONFIG_LOG_IREGION,
                           strlen(CONFIG_LOG_IREGION)) == 0) {
      param = strtok_r(NULL, delim, &saveptr);
      if (param != NULL) {
        frpc_config.log_iregion = true;
        FARF(ALWAYS, "fastrpc config enabling iregion logging\n");
      }
    } else if (strncmp(param, CONFIG_QTF_TRACING,
                           strlen(CONFIG_QTF_TRACING)) == 0) {
      param = strtok_r(NULL, delim, &saveptr);
      if (param != NULL && atoi(param)) {
        frpc_config.qtf_tracing = true;
        FARF(ALWAYS, "fastrpc config enabling QTF tracing\n");
      }
    } else if (strncmp(param, CONFIG_CALLER_LEVEL,
                           strlen(CONFIG_CALLER_LEVEL)) == 0) {
      param = strtok_r(NULL, delim, &saveptr);
      if (param != NULL && atoi(param)) {
        frpc_config.caller_level = atoi(param);
        FARF(ALWAYS, "fastrpc config setting heap caller level with %d\n",
             frpc_config.caller_level);
      }
    } else if (strncmp(param, CONFIG_ENABLE_UAF,
                           strlen(CONFIG_ENABLE_UAF)) == 0) {
      param = strtok_r(NULL, delim, &saveptr);
      if (param != NULL && atoi(param)) {
        frpc_config.uaf_enabled = true;
        FARF(ALWAYS, "fastrpc config enabling uaf on heap\n");
      }
    } else if (strncmp(param, CONFIG_DEBUG_LOGGING,
                           strlen(CONFIG_DEBUG_LOGGING)) == 0) {
      param = strtok_r(NULL, delim, &saveptr);
      if (param != NULL) {
        frpc_config.debug_logging = true;
        FARF(ALWAYS, "fastrpc config enabling debug logging\n");
      }
    } else if (strncmp(param, CONFIG_DEBUG_SYSMON_LOGGING,
                           strlen(CONFIG_DEBUG_SYSMON_LOGGING)) == 0) {
      param = strtok_r(NULL, delim, &saveptr);
      if (param != NULL) {
        frpc_config.sysmonreservedbit = true;
        FARF(ALWAYS, "fastrpc config enabling sysmon logging \n");
      }
    } else if (strncmp(param, CONFIG_LOGPACKET,
                           strlen(CONFIG_LOGPACKET)) == 0) {
      param = strtok_r(NULL, delim, &saveptr);
      if (param != NULL) {
        frpc_config.logPackets = true;
        FARF(ALWAYS, "fastrpc config enabling Log packets\n");
      }
    } else if (strncmp(param, CONFIG_LEAK_DETECT,
                           strlen(CONFIG_LEAK_DETECT)) == 0) {
      param = strtok_r(NULL, delim, &saveptr);
      if (param != NULL && atoi(param)) {
        frpc_config.leak_detect = atoi(param);
        FARF(ALWAYS, "fastrpc config enabling leak detect with %d\n",
             frpc_config.leak_detect);
      }
    } else if (strncmp(param, CONFIG_CALL_STACK_NUM,
                           strlen(CONFIG_CALL_STACK_NUM)) == 0) {
      param = strtok_r(NULL, delim, &saveptr);
      if (param != NULL && atoi(param)) {
        frpc_config.num_call_stack = atoi(param);
        FARF(ALWAYS, "fastrpc config setting call stack num with %d\n",
             frpc_config.num_call_stack);
      }
 		} else if (strncmp(param, CONFIG_SETDMABUFNAME,
					strlen(CONFIG_SETDMABUFNAME)) == 0) {
			param = strtok_r (NULL, delim, &saveptr);
			if (param != NULL && atoi(param))
				frpc_config.setdmabufname = true;
    }
    param = NULL;
  } while (!eof);
bail:
  if (buf != NULL) {
    free(buf);
    buf = NULL;
  }
  if (fp != -1) {
    apps_std_fclose(fp);
  }
  if (path != NULL) {
    free(path);
    path = NULL;
  }
  if (nErr != AEE_SUCCESS && nErr != AEE_ENOSUCHFILE) {
    FARF(ALWAYS, "Error 0x%x: failed for %s/%s with errno(%s)\n", nErr, base,
         file, strerror(errno));
  }
  return nErr;
}

// Function to get panic error codes.
struct err_codes *fastrpc_config_get_errcodes(void) {
  if (frpc_config.err_codes_to_crash.num_err_codes != 0)
    return (&(frpc_config.err_codes_to_crash));
  return NULL;
}

// Function to get rpc timeout.
int fastrpc_config_get_rpctimeout(void) { return frpc_config.rpc_timeout; }

// Function to get if PD dump feature is enabled.
bool fastrpc_config_is_pddump_enabled(void) { return frpc_config.pddump; }

// Functions to get if profiling mode is enabled.
bool fastrpc_config_is_perfkernel_enabled(void) {
  return frpc_config.perfkernel;
}
bool fastrpc_config_is_perfdsp_enabled(void) { return frpc_config.perfdsp; }

// Function to get the file name to collect runtime farf logs.
char *fastrpc_config_get_runtime_farf_file(void) {
  return frpc_config.farf_log_filename;
}
// Function to get the file name to collect userspace runtime farf logs.
char *fastrpc_config_get_userspace_runtime_farf_file(void) {
  return frpc_config.farf_log_filename_userspace;
}
// Function to get if iregion logging feature is enabled.
bool fastrpc_config_is_log_iregion_enabled(void) {
  return frpc_config.log_iregion;
}
// Function to get if debug logging feature is enabled.
bool fastrpc_config_is_debug_logging_enabled(void) {
  return frpc_config.debug_logging;
}

// Function to get if debug logging feature is enabled for sysmon reserved bit.
bool fastrpc_config_is_sysmon_reserved_bit_enabled(void) {
  return frpc_config.sysmonreservedbit;
}

// Function to get if QTF tracing is enabled.
bool fastrpc_config_is_qtf_tracing_enabled(void) {
  return frpc_config.qtf_tracing;
}

// Function to get heap caller level.
int fastrpc_config_get_caller_level(void) { return frpc_config.caller_level; }

// Function to get if uaf should be enabled in heap
bool fastrpc_config_is_uaf_enabled(void) { return frpc_config.uaf_enabled; }

// Function to get if Log packet is enabled.
bool fastrpc_config_is_logpacket_enabled(void) {
  return frpc_config.logPackets;
}

// Function to get if leak detect is enabled
int fastrpc_config_get_leak_detect(void) { return frpc_config.leak_detect; }

// Function to return the call stack num
int fastrpc_config_get_caller_stack_num(void) {
  return frpc_config.num_call_stack;
}

bool fastrpc_config_is_setdmabufname_enabled(void) {
	return frpc_config.setdmabufname;
}

// Fastrpc config init function
int fastrpc_config_init() {
  int nErr = AEE_SUCCESS, i = 0;
  const char *file_extension = ".debugconfig";
  char *name = NULL;
  char *data_paths = NULL;
  char *config_file = NULL;
  _cstring1_t *paths = NULL;
  uint32_t len = 0;
  uint16_t maxPathLen = 0;
  uint32_t numPaths = 0;
  int file_found = 0;

  VERIFYC(NULL != (name = std_basename(__progname)), AEE_EINVALIDPROCNAME);
  len = strlen(name) + strlen(file_extension) + 1;
  VERIFYC(NULL != (config_file = calloc(1, sizeof(char) * len)), AEE_ENOMEMORY);
  // Prepare config filename
  snprintf(config_file, len, "%s%s", name, file_extension);
  FARF(RUNTIME_RPC_HIGH, "Reading configuration file: %s\n", config_file);

  // Get the required size for PATH
  apps_std_get_search_paths_with_env(ADSP_LIBRARY_PATH, ";", NULL, 0, &numPaths,
                                     &maxPathLen);
  maxPathLen += +1;

  // Allocate memory for the PATH's
  VERIFYC(NULL != (paths = calloc(1, sizeof(_cstring1_t) * numPaths)),
          AEE_ENOMEMORY);
  for (i = 0; i < (int)numPaths; ++i) {
    VERIFYC(NULL != (paths[i].data = calloc(1, sizeof(char) * maxPathLen)),
            AEE_ENOMEMORY);
    paths[i].dataLen = maxPathLen;
  }

  // Allocate single buffer for all the PATHs
  VERIFYC(NULL !=
              (data_paths = calloc(1, sizeof(char) * maxPathLen * numPaths)),
          AEE_ENOMEMORY);

  // Get the paths
  VERIFY(AEE_SUCCESS ==
         (nErr = apps_std_get_search_paths_with_env(
              ADSP_LIBRARY_PATH, ";", paths, numPaths, &len, &maxPathLen)));
  maxPathLen += 1;
  for (i = 0; i < (int)numPaths; ++i) {
    strlcat(data_paths, paths[i].data,
                sizeof(char) * maxPathLen * numPaths);
    strlcat(data_paths, ", ", sizeof(char) * maxPathLen * numPaths);
    if (0 == fastrpc_read_config_file_from_path(paths[i].data, config_file)) {
      file_found = 1;
      FARF(ALWAYS, "Read fastrpc config file %s found at %s\n", config_file,
           paths[i].data);
      break;
    }
  }
  if (!file_found) {
    FARF(RUNTIME_RPC_HIGH, "%s: Couldn't find file %s, errno (%s) at %s\n", __func__,
         config_file, strerror(errno), data_paths);
  }
bail:
  if (nErr) {
    FARF(ERROR, "%s: failed for process %s", __func__, name);
  }
  if (paths) {
    for (i = 0; i < (int)numPaths; ++i) {
      if (paths[i].data) {
        free(paths[i].data);
        paths[i].data = NULL;
      }
    }
    free(paths);
    paths = NULL;
  }
  if (config_file) {
    free(config_file);
    config_file = NULL;
  }
  if (data_paths) {
    free(data_paths);
  }
  return nErr;
}
