// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef CALCULATOR_TEST_H
#define CALCULATOR_TEST_H

#include <stdbool.h>

#include "AEEStdDef.h"
#include "remote.h"

#ifdef __cplusplus
extern "C" {
#endif

bool is_unsignedpd_supported(int domain_id);
int calculator_sum_test(const char *calculator_URI_domain, const int *test, int num);
int calculator_max_test(const char *calculator_URI_domain, const int *test, int num);
int run_test(int domain_id, bool is_unsignedpd_enabled);

#ifdef __cplusplus
}
#endif

#endif // CALCULATOR_TEST_H
