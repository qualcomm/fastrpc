// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "AEEStdErr.h"
#include "e2e_session.h"
#include "fastrpc_base_test.h"
#include "test_utils.h"
#include "unity_fixture.h"
#include "test_runner/base_test_runner.h"

TEST_GROUP(HapExampleE2E);

static test_e2e_session_t hap_session;

TEST_SETUP(HapExampleE2E)
{
    int ret = test_utils_setup_test();
    if (ret != AEE_SUCCESS)
        TEST_FAIL_MESSAGE("FastRPC test setup failed");

    ret = test_e2e_session_open(&hap_session);
    if (ret != AEE_SUCCESS)
        TEST_FAIL_MESSAGE("HAP example DSP service is unavailable");
}

TEST_TEAR_DOWN(HapExampleE2E)
{
    int ret = test_e2e_session_close(&hap_session);

    TEST_ASSERT_EQUAL_HEX32(AEE_SUCCESS, ret);
}

TEST(HapExampleE2E, FarfRuntimeLoggingCanBeConfigured)
{
    int ret = fastrpc_base_test_hap_example_farf_runtime(hap_session.handle, 0);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret, "disable FARF runtime logging");

    ret = fastrpc_base_test_hap_example_farf_runtime(hap_session.handle, 1);
    REPORT_ERROR_CODE(ret);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(AEE_SUCCESS, ret, "enable FARF runtime logging");
}
TEST_CASE_METADATA(HapExampleE2E, FarfRuntimeLoggingCanBeConfigured,
                   TEST_METADATA_TAGS("HapExample", "e2e", "positive", "Remote", "FARF"));

TEST_GROUP_RUNNER(HapExampleE2E)
{
    RUN_TEST_CASE(HapExampleE2E, FarfRuntimeLoggingCanBeConfigured);
}
