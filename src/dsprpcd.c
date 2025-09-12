// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef VERIFY_PRINT_ERROR
#define VERIFY_PRINT_ERROR
#endif
#define VERIFY_PRINT_INFO 0

#include "AEEStdErr.h"
#include "HAP_farf.h"
#include "verify.h"
#include "fastrpc_common.h"
#include <dlfcn.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#ifndef ADSP_LISTENER_VERSIONED
#define ADSP_LISTENER_VERSIONED   "libadsp_default_listener.so.1"
#define ADSP_LISTENER_UNVERSIONED "libadsp_default_listener.so"
#endif
#ifndef CDSP_LISTENER_VERSIONED
#define CDSP_LISTENER_VERSIONED   "libcdsp_default_listener.so.1"
#define CDSP_LISTENER_UNVERSIONED "libcdsp_default_listener.so"
#endif
#ifndef SDSP_LISTENER_VERSIONED
#define SDSP_LISTENER_VERSIONED   "libsdsp_default_listener.so.1"
#define SDSP_LISTENER_UNVERSIONED "libsdsp_default_listener.so"
#endif
#ifndef GDSP_LISTENER_VERSIONED
#define GDSP_LISTENER_VERSIONED   "libcdsp_default_listener.so.1"
#define GDSP_LISTENER_UNVERSIONED "libcdsp_default_listener.so"
#endif

typedef int (*dsp_default_listener_start_t)(int argc, char *argv[]);

/**
 * Attempts to load a shared library using dlopen.
 * If the versioned name fails, falls back to the unversioned name.
 */
static void *try_dlopen(const char *versioned, const char *unversioned) {
  void *handle = dlopen(versioned, RTLD_NOW);
  if (!handle && unversioned) {
    VERIFY_IPRINTF("dlopen failed for %s: %s; attempting fallback %s",
                   versioned, dlerror(), unversioned);
    handle = dlopen(unversioned, RTLD_NOW);
  }
  return handle;
}

int main(int argc, char *argv[]) {
  int nErr = 0;
  void *dsphandler = NULL;
  const char* lib_versioned;
  const char* lib_unversioned;
  const char* dsp_name;
  dsp_default_listener_start_t listener_start;

  #ifdef USE_ADSP
    lib_versioned = ADSP_LISTENER_VERSIONED;
    lib_unversioned = ADSP_LISTENER_UNVERSIONED;
    dsp_name = "ADSP";
  #elif defined(USE_SDSP)
    lib_versioned = SDSP_LISTENER_VERSIONED;
    lib_unversioned = SDSP_LISTENER_UNVERSIONED;
    dsp_name = "SDSP";
  #elif defined(USE_CDSP)
    lib_versioned = CDSP_LISTENER_VERSIONED;
    lib_unversioned = CDSP_LISTENER_UNVERSIONED;
    dsp_name = "CDSP";
  #elif defined(USE_GDSP)
    lib_versioned = GDSP_LISTENER_VERSIONED;
    lib_unversioned = GDSP_LISTENER_UNVERSIONED;
    dsp_name = "GDSP";
  #else
    goto bail;
  #endif
  VERIFY_EPRINTF("%s daemon starting", dsp_name);
  
  while (1) {
        dsphandler = try_dlopen(lib_versioned, lib_unversioned);
        if (NULL != dsphandler) {
            if (NULL != (listener_start = (dsp_default_listener_start_t)dlsym(
                              dsphandler, "adsp_default_listener_start"))) {
                VERIFY_IPRINTF("adsp_default_listener_start called");
                nErr = listener_start(argc, argv);
            }
            if (0 != dlclose(dsphandler)) {
              VERIFY_EPRINTF("dlclose failed for %s", lib_versioned);
            }
        } else {
            VERIFY_EPRINTF("%s daemon error %s", dsp_name, dlerror());
        }

        if (nErr == AEE_ECONNREFUSED) {
            VERIFY_EPRINTF("fastRPC device is not accessible, daemon exiting...");
            break;
        }

        VERIFY_EPRINTF("%s daemon will restart after 100ms...", dsp_name);
        usleep(100000);
  }

  bail:
    VERIFY_EPRINTF("daemon exiting %x", nErr);
    return nErr;
}
