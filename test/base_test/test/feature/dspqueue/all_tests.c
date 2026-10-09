// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file feature/dspqueue/all_tests.c
 * @brief Suite runner for the "dspqueue" feature test suite.
 *
 * Replaces the previously auto-generated gen_feature_dspqueue_all_tests.c.
 * This file is now static and checked into the repository.
 *
 * The function name uses the _feature suffix to avoid a link-time collision
 * with run_dspqueue_tests() defined in unit/dspqueue/all_tests.c.
 *
 * To add a new test group to this suite:
 *   1. Create feature/dspqueue/test_<name>.c with TEST_GROUP / TEST /
 *      TEST_SETUP / TEST_TEAR_DOWN / TEST_GROUP_RUNNER.
 *   2. Add a forward declaration for TEST_<Name>_GROUP_RUNNER below.
 *   3. Add a RUN_TEST_GROUP(<Name>) call in run_dspqueue_feature_tests().
 *   4. Add the new source file to the 'dspqueue' feature suite entry in
 *      feature/CMakeLists.txt.
 */

#include "test_utils.h"
#include "unity_fixture.h"

/* ---- Group runner forward declarations ---------------------------------- */
void TEST_DspQueueEchoFlow_GROUP_RUNNER(void);
void TEST_DspQueueDataProcessingFlow_GROUP_RUNNER(void);
void TEST_DspQueueBufferManagement_GROUP_RUNNER(void);

/* ---- Suite entry point -------------------------------------------------- */

/**
 * @brief Run all test groups in the "dspqueue" feature suite.
 *
 * Called by run_all_tests() in root_all_tests.c.
 */
void run_dspqueue_feature_tests(void)
{
    if (test_suite_setup("dspqueue") != 0)
        return;

    RUN_TEST_GROUP(DspQueueEchoFlow);
    RUN_TEST_GROUP(DspQueueDataProcessingFlow);
    RUN_TEST_GROUP(DspQueueBufferManagement);
}
