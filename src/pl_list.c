// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "platform_libs.h"

PL_DEP(gpls)
PL_DEP(listener_android)

struct platform_lib *(*pl_list[])(void)
    = { PL_ENTRY(gpls), PL_ENTRY(listener_android), 0 };
