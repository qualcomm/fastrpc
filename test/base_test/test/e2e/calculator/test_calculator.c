// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "AEEStdErr.h"
#include "e2e_session.h"
#include "fastrpc_base_test.h"
#include "test_utils.h"
#include "unity_fixture.h"
#include "test_runner/base_test_runner.h"

#include <stdint.h>

TEST_GROUP(CalculatorE2E);

static test_e2e_session_t calculator_session;

static int is_bad_param_error(int ret)
{
    return ret == AEE_EBADPARM ||
           (uint32_t)ret == (uint32_t)(DSP_AEE_EOFFSET + AEE_EBADPARM);
}

TEST_SETUP(CalculatorE2E)
{
    int ret = test_utils_setup_test();
    if (ret != AEE_SUCCESS)
        TEST_FAIL_MESSAGE("FastRPC test setup failed");

    ret = test_e2e_session_open(&calculator_session);
    if (ret != AEE_SUCCESS)
        TEST_FAIL_MESSAGE("calculator DSP service is unavailable");
}

TEST_TEAR_DOWN(CalculatorE2E)
{
    int ret = test_e2e_session_close(&calculator_session);

    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, ret);
}

TEST(CalculatorE2E, SumAndMaximum)
{
    const int values[] = {-7, 0, 4, 9, 2};
    int64_t sum = 0;
    int maximum = 0;

    int ret = fastrpc_base_test_calculator_sum(calculator_session.handle, values,
                                          (int)(sizeof(values) / sizeof(values[0])), &sum);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret, "calculator_sum");
    TEST_ASSERT_EQUAL_INT64(8, sum);

    ret = fastrpc_base_test_calculator_max(calculator_session.handle, values,
                                      (int)(sizeof(values) / sizeof(values[0])), &maximum);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret, "calculator_max");
    TEST_ASSERT_EQUAL_INT(9, maximum);
}
TEST_CASE_METADATA(CalculatorE2E, SumAndMaximum,
                   TEST_METADATA_TAGS("Calculator", "e2e", "positive", "Remote"));


TEST(CalculatorE2E, EmptyInputsAreRejected)
{
    const int empty_input[] = {0};
    int64_t sum = 0;
    int maximum = 0;
    int ret = fastrpc_base_test_calculator_sum(calculator_session.handle, empty_input, 0, &sum);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_TRUE_MESSAGE(is_bad_param_error(ret), "calculator_sum invalid input");

    ret = fastrpc_base_test_calculator_max(calculator_session.handle, empty_input, 0, &maximum);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_TRUE_MESSAGE(is_bad_param_error(ret), "calculator_max invalid input");
}
TEST_CASE_METADATA(CalculatorE2E, EmptyInputsAreRejected,
                   TEST_METADATA_TAGS("Calculator", "e2e", "negative", "Remote"));

TEST_GROUP_RUNNER(CalculatorE2E)
{
    RUN_TEST_CASE(CalculatorE2E, SumAndMaximum);
    RUN_TEST_CASE(CalculatorE2E, EmptyInputsAreRejected);
}
