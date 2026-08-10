// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef FASTRPC_PLATFORM_H
#define FASTRPC_PLATFORM_H

#include "fastrpc_common.h"

int fastrpc_platform_get_property_int(fastrpc_properties key, int defValue);
int fastrpc_platform_get_property_string(fastrpc_properties key, char *value, char *defValue);

#endif /* FASTRPC_PLATFORM_H */
