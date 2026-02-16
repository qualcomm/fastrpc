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
#define ADSP_AVS_PATH "ADSP_AVS_CFG_PATH"
#define MAX_NON_PRELOAD_LIBS_LEN 2048
#define FILE_EXT ".so"

// Search path used by fastRPC for acdb path
#ifndef ADSP_AVS_CFG_PATH
#define ADSP_AVS_CFG_PATH ";/etc/acdbdata/;"
#endif

int fopen_from_dirlist(const char *dirList, const char *delim, 
    const char *mode, const char *name, apps_std_FILE *psout);

#endif /*__APPS_STD_INTERNAL_H__*/
