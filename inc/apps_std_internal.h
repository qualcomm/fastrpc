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

// Locations where shell file can be found
#ifndef ENABLE_UPSTREAM_DRIVER_INTERFACE
#ifndef DSP_MOUNT_LOCATION
#define DSP_MOUNT_LOCATION "/dsp/"
#endif
#ifndef DSP_DOM_LOCATION
#define DSP_DOM_LOCATION "/dsp/xdsp"
#endif
#else /* ENABLE_UPSTREAM_DRIVER_INTERFACE */
#ifndef DSP_MOUNT_LOCATION
#define DSP_MOUNT_LOCATION "/usr/lib/dsp/"
#endif
#ifndef DSP_DOM_LOCATION
#define DSP_DOM_LOCATION "/usr/lib/dsp/xdspn"
#endif
#endif /* ENABLE_UPSTREAM_DRIVER_INTERFACE */

// Search path used by fastRPC to search skel library, .debugconfig and .farf files
#ifndef DSP_SEARCH_PATH
#define DSP_SEARCH_PATH ";/usr/lib/rfsa/adsp;/usr/lib/dsp/;"
#endif

// Search path used by fastRPC for acdb path
#ifndef ADSP_AVS_CFG_PATH
#define ADSP_AVS_CFG_PATH ";/etc/acdbdata/;"
#endif

#endif /*__APPS_STD_INTERNAL_H__*/
