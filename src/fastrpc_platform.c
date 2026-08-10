// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "fastrpc_platform.h"
#include "HAP_farf.h"
#include <string.h>

#define PROPERTY_VALUE_MAX 92

#if !defined(LE_ENABLE)

/* Android vendor property names, indexed by fastrpc_properties enum */
static const char *platform_debug_var_name[] = {
    "vendor.fastrpc.process.attrs",
    "vendor.fastrpc.debug.trace",
    "vendor.fastrpc.debug.testsig",
    "vendor.fastrpc.perf.kernel",
    "vendor.fastrpc.perf.adsp",
    "vendor.fastrpc.perf.freq",
    "vendor.fastrpc.debug.systrace",
    "vendor.fastrpc.debug.pddump",
    "persist.vendor.fastrpc.process.attrs",
    "ro.build.type",
};

static int platform_debug_var_name_count =
    sizeof(platform_debug_var_name) / sizeof(char *);

static int property_get_int32(const char *name, int value) { return 0; }
static int property_get(const char *name, int *def, int *value) { return 0; }

int fastrpc_platform_get_property_int(fastrpc_properties key, int defValue) {
  if ((int)key >= platform_debug_var_name_count) {
    FARF(ERROR, "%s: key %d out of range (max %d)", __func__, key,
         platform_debug_var_name_count);
    return defValue;
  }
  return property_get_int32(platform_debug_var_name[key], defValue);
}

int fastrpc_platform_get_property_string(fastrpc_properties key, char *value,
                                         char *defValue) {
  if ((int)key >= platform_debug_var_name_count) {
    FARF(ERROR, "%s: key %d out of range (max %d)", __func__, key,
         platform_debug_var_name_count);
    return 0;
  }
  return property_get(platform_debug_var_name[key], (int *)value,
                      (int *)defValue);
}

#else /* LE_ENABLE */

int fastrpc_platform_get_property_int(fastrpc_properties key, int defValue) {
  return defValue;
}

int fastrpc_platform_get_property_string(fastrpc_properties key, char *value,
                                         char *defValue) {
  if (defValue) {
    strncpy(value, defValue, PROPERTY_VALUE_MAX - 1);
    value[PROPERTY_VALUE_MAX - 1] = '\0';
    return strlen(defValue);
  }
  return 0;
}

#endif /* LE_ENABLE */
