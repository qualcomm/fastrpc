// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file unit/dspqueue/all_tests.c
 * @brief Suite runner for the "dspqueue" unit test suite.
 *
 * Replaces the previously auto-generated gen_unit_dspqueue_all_tests.c.
 * This file is now static and checked into the repository.
 *
 * To add a new test group to this suite:
 *   1. Create unit/dspqueue/test_<name>.c with TEST_GROUP / TEST /
 *      TEST_SETUP / TEST_TEAR_DOWN / TEST_GROUP_RUNNER.
 *   2. Add a forward declaration for TEST_<Name>_GROUP_RUNNER below.
 *   3. Add a RUN_TEST_GROUP(<Name>) call in run_dspqueue_tests().
 *   4. Add the new source file to the 'dspqueue' suite entry in
 *      unit/CMakeLists.txt.
 */

#include "test_utils.h"
#include "unity_fixture.h"

/* ---- Group runner forward declarations ---------------------------------- */
void TEST_DspQueueCreate_GROUP_RUNNER(void);
void TEST_DspQueueClose_GROUP_RUNNER(void);
void TEST_DspQueueExport_GROUP_RUNNER(void);
void TEST_DspQueueWrite_GROUP_RUNNER(void);
void TEST_DspQueueRead_GROUP_RUNNER(void);
void TEST_DspQueuePeek_GROUP_RUNNER(void);
void TEST_DspQueueGetStat_GROUP_RUNNER(void);

/* ---- Suite entry point -------------------------------------------------- */

/**
 * @brief Run all test groups in the "dspqueue" unit suite.
 *
 * Called by run_all_tests() in root_all_tests.c.
 */
void run_dspqueue_tests(void)
{
    if (test_suite_setup("dspqueue") != 0)
        return;

    RUN_TEST_GROUP(DspQueueCreate);
    RUN_TEST_GROUP(DspQueueClose);
    RUN_TEST_GROUP(DspQueueExport);
    RUN_TEST_GROUP(DspQueueWrite);
    RUN_TEST_GROUP(DspQueueRead);
    RUN_TEST_GROUP(DspQueuePeek);
    RUN_TEST_GROUP(DspQueueGetStat);
}
