// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "fastrpc_android.h"
#include "HAP_farf.h"
#include <string.h>

#define PROPERTY_VALUE_MAX 92

/* Android P (vendor) property names */
const char *ANDROIDP_DEBUG_VAR_NAME[] = {"vendor.fastrpc.process.attrs",
                                         "vendor.fastrpc.debug.trace",
                                         "vendor.fastrpc.debug.testsig",
                                         "vendor.fastrpc.perf.kernel",
                                         "vendor.fastrpc.perf.adsp",
                                         "vendor.fastrpc.perf.freq",
                                         "vendor.fastrpc.debug.systrace",
                                         "vendor.fastrpc.debug.pddump",
                                         "persist.vendor.fastrpc.process.attrs",
                                         "ro.build.type"};

int NO_ANDROIDP_DEBUG_VAR_NAME_ARRAY_ELEMENTS =
    sizeof(ANDROIDP_DEBUG_VAR_NAME) / sizeof(char *);

/* Stub implementations for property functions - to be implemented with actual Android property API */
int property_get_int32(const char *name, int value) {
    return 0;
}

int property_get(const char *name, int *def, int *value) {
    return 0;
}

int platform_get_property_int(fastrpc_properties UserPropKey, int defValue) {
  if (((int)UserPropKey > NO_ANDROIDP_DEBUG_VAR_NAME_ARRAY_ELEMENTS)) {
    FARF(
        ERROR,
        "%s: Index %d out-of-bound for ANDROIDP_DEBUG_VAR_NAME array of len %d",
        __func__, UserPropKey, NO_ANDROIDP_DEBUG_VAR_NAME_ARRAY_ELEMENTS);
    return defValue;
  }
  return (int)property_get_int32(ANDROIDP_DEBUG_VAR_NAME[UserPropKey],
                                 defValue);
}

int platform_get_property_string(fastrpc_properties UserPropKey, char *value,
                                char *defValue) {
  int len = 0;
  if (((int)UserPropKey > NO_ANDROIDP_DEBUG_VAR_NAME_ARRAY_ELEMENTS)) {
    FARF(
        ERROR,
        "%s: Index %d out-of-bound for ANDROIDP_DEBUG_VAR_NAME array of len %d",
        __func__, UserPropKey, NO_ANDROIDP_DEBUG_VAR_NAME_ARRAY_ELEMENTS);
    return len;
  }
  return property_get(ANDROIDP_DEBUG_VAR_NAME[UserPropKey], (int *)value,
                      (int *)defValue);
}
