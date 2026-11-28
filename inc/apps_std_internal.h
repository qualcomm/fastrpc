// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef __APPS_STD_INTERNAL_H__
#define __APPS_STD_INTERNAL_H__

#include "apps_std.h"

/**
  * @brief Macros used in apps_std
  * defines the search paths where fastRPC library should 
  * look for skel libraries, .debugconfig, .farf files.  
  * Could be overloaded from build system.
  **/
 
#define RETRY_WRITE (3) // number of times to retry write operation

// Environment variable name, that can be used to override the search paths
#define ADSP_LIBRARY_PATH "ADSP_LIBRARY_PATH"
#define DSP_LIBRARY_PATH "DSP_LIBRARY_PATH"
#define MAX_NON_PRELOAD_LIBS_LEN 2048
#define FILE_EXT ".so"

#ifndef VENDOR_DSP_LOCATION
#define VENDOR_DSP_LOCATION "/vendor/dsp/"
#endif
#ifndef VENDOR_DOM_LOCATION
#define VENDOR_DOM_LOCATION "/vendor/dsp/xdsp/"
#endif

int fopen_from_dirlist(const char *dirList, const char *delim,
    const char *mode, const char *name, int domain, apps_std_FILE *psout,
    char **out_path);

#endif /*__APPS_STD_INTERNAL_H__*/
