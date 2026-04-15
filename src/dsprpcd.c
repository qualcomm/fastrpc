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
#include <getopt.h>

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
/* GDSP uses CDSP libraries (not a typo) */
#ifndef GDSP_LISTENER_VERSIONED
#define GDSP_LISTENER_VERSIONED   "libcdsp_default_listener.so.1"
#define GDSP_LISTENER_UNVERSIONED "libcdsp_default_listener.so"
#endif

typedef int (*dsp_default_listener_start_t)(int argc, char *argv[]);

/**
 * Prints help information about the daemon.
 * @param program_name The name of the program
 * @param dsp_name The DSP name (ADSP, CDSP, etc.)
 */
static void print_help(const char *program_name, const char *dsp_name) {
  printf("Usage: %s [OPTION]...\n", program_name);
  printf("Daemon that establishes a connection to %s.\n", dsp_name);
#ifdef USE_ADSP
  printf("If audiopd is passed as an argument to this daemon, it will connect to audio PD on %s.\n", dsp_name);
#endif
  printf("If no argument is provided or rootpd is passed, it will connect to root PD on %s.\n\n", dsp_name);

  printf("Functionality:\n");
#ifdef USE_ADSP
  printf("  rootpd:\n");
  printf("    - Exception logging: Facilitates transfer of %s process exception logs\n", dsp_name);
  printf("      to the HLOS (High-Level Operating System) logging infrastructure for\n");
  printf("      effective monitoring and debugging\n");
  printf("    - Remote file system access\n");
  printf("  audiopd:\n");
  printf("    - Memory requirements for audio PD dynamic loading\n");
  printf("    - Remote file system access\n\n");
#elif defined(USE_SDSP)
  printf("  rootpd:\n");
  printf("    - Remote file system access\n\n");
#else
  printf("  rootpd:\n");
  printf("    - Exception logging: Facilitates transfer of %s process exception logs\n", dsp_name);
  printf("      to the HLOS (High-Level Operating System) logging infrastructure for\n");
  printf("      effective monitoring and debugging\n");
  printf("    - Remote file system access\n\n");
#endif

  printf("Options:\n");
  printf("  -h, --help              display this help and exit\n");
  printf("  <pd_name> <domain>      start daemon for specific PD and domain\n");
#ifdef USE_ADSP
  printf("                            example: %s rootpd adsp\n", program_name);
#elif defined(USE_SDSP)
  printf("                            example: %s rootpd sdsp\n", program_name);
#elif defined(USE_CDSP)
  printf("                            example: %s rootpd cdsp (or cdsp1)\n", program_name);
#elif defined(USE_GDSP)
  printf("                            example: %s rootpd gdsp0 (or gdsp1)\n", program_name);
#endif
#ifndef USE_GDSP
  printf("  <pd_name>               start daemon for specific PD\n");
  printf("                            example: %s rootpd\n", program_name);
  printf("  (no arguments)          start daemon for root PD (default domain)\n\n");
#else
  printf("\n");
#endif

  printf("Note that this daemon runs continuously and automatically restarts on errors.\n");
  printf("It exits only when the fastRPC device node is not accessible.\n");
}

// Result struct for dlopen.
struct dlopen_result {
  void *handle;
  const char *loaded_lib_name;
};

/**
 * Attempts to load a shared library using dlopen.
 * If the versioned name fails, falls back to the unversioned name.
 * Returns both the handle and the name of the library successfully loaded.
 */
static struct dlopen_result try_dlopen(const char *versioned, const char *unversioned) {
  struct dlopen_result result = { NULL, NULL };

  result.handle = dlopen(versioned, RTLD_NOW);
  if (result.handle) {
    result.loaded_lib_name = versioned;
    return result;
  }

  if (unversioned) {
    VERIFY_IPRINTF("dlopen failed for %s: %s; attempting fallback %s",
                   versioned, dlerror(), unversioned);
    result.handle = dlopen(unversioned, RTLD_NOW);
    if (result.handle) {
      result.loaded_lib_name = unversioned;
      return result;
    }
  }
  return result;
}

int main(int argc, char *argv[]) {
  int nErr = 0;
  struct dlopen_result dlres = { NULL, NULL };
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
    VERIFY_EPRINTF("daemon exiting %x (no DSP type defined)", nErr);
    return nErr;
  #endif

  // Parse command-line options
  static struct option long_options[] = {
    {"help", no_argument, 0, 'h'},
    {0, 0, 0, 0}
  };

  int opt;
  while ((opt = getopt_long(argc, argv, "h", long_options, NULL)) != -1) {
    switch (opt) {
      case 'h':
        print_help(argv[0], dsp_name);
        return 0;
      default:
        fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
        return 1;
    }
  }

  VERIFY_EPRINTF("%s daemon starting", dsp_name);
  
  while (1) {
        dlres = try_dlopen(lib_versioned, lib_unversioned);
        if (NULL != dlres.handle) {
            if (NULL != (listener_start = (dsp_default_listener_start_t)dlsym(
                              dlres.handle, "adsp_default_listener_start"))) {
                VERIFY_IPRINTF("adsp_default_listener_start called");
                nErr = listener_start(argc, argv);
            }
            if (0 != dlclose(dlres.handle)) {
              VERIFY_EPRINTF("dlclose failed for %s", dlres.loaded_lib_name);
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

  VERIFY_EPRINTF("daemon exiting %x", nErr);
  return nErr;
}
