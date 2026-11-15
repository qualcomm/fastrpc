// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef TEST_ERROR_H
#define TEST_ERROR_H

#include <limits.h>

#define TEST_UTILS_MAX_ERROR_CODES 32
#define TEST_UTILS_ERROR_CODES_TRUNCATED 1
#define TEST_UTILS_ERROR_CODES_COMPLETE 0
#define TEST_UTILS_INVALID_ERROR_CODE INT_MIN

#ifdef __cplusplus
extern "C" {
#endif

void test_utils_set_last_error_code(int err);
int test_utils_get_last_error_code(void); /* Returns TEST_UTILS_INVALID_ERROR_CODE if none set */
void test_utils_reset_last_error_code(void);

void test_utils_accumulate_error_code(int err);
const int *test_utils_get_accumulated_error_codes(int *count);
int test_utils_error_codes_truncated(void);
void test_utils_reset_accumulated_error_codes(void);

#ifdef __cplusplus
}
#endif

#endif /* TEST_ERROR_H */
