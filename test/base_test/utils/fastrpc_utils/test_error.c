// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "test_error.h"

static _Thread_local int s_last_error_code = TEST_UTILS_INVALID_ERROR_CODE;
static _Thread_local int s_accumulated_codes[TEST_UTILS_MAX_ERROR_CODES];
static _Thread_local int s_accumulated_count;
static _Thread_local int s_accumulated_truncated;

void test_utils_set_last_error_code(int err)
{
    s_last_error_code = err;
}

int test_utils_get_last_error_code(void)
{
    return s_last_error_code;
}

void test_utils_reset_last_error_code(void)
{
    s_last_error_code = TEST_UTILS_INVALID_ERROR_CODE;
}

void test_utils_accumulate_error_code(int err)
{
    if (s_accumulated_count < TEST_UTILS_MAX_ERROR_CODES) {
        s_accumulated_codes[s_accumulated_count++] = err;
    } else {
        s_accumulated_truncated = TEST_UTILS_ERROR_CODES_TRUNCATED;
    }
}

const int *test_utils_get_accumulated_error_codes(int *count)
{
    if (count)
        *count = s_accumulated_count;
    return s_accumulated_codes;
}

int test_utils_error_codes_truncated(void)
{
    return s_accumulated_truncated;
}

void test_utils_reset_accumulated_error_codes(void)
{
    s_accumulated_count = 0;
    s_accumulated_truncated = TEST_UTILS_ERROR_CODES_COMPLETE;
}
