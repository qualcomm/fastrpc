// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef FASTRPC_ANDROID_H
#define FASTRPC_ANDROID_H

#include "fastrpc_common.h"

/* Platform property system functions */
int property_get_int32(const char *name, int value);
int property_get(const char *name, int *def, int *value);

/* Platform-specific property getter functions */
int platform_get_property_int(fastrpc_properties UserPropKey, int defValue);
int platform_get_property_string(fastrpc_properties UserPropKey, char *value, char *defValue);

#endif /*FASTRPC_ANDROID_H*/
