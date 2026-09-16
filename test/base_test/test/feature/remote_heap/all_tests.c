// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file feature/remote_heap/all_tests.c
 * @brief Suite runner for the "remote_heap" feature test suite.
 *
 * To add a new test group to this suite:
 *   1. Create feature/remote_heap/test_<name>.c with TEST_GROUP / TEST /
 *      TEST_SETUP / TEST_TEAR_DOWN / TEST_GROUP_RUNNER.
 *   2. Add a forward declaration for TEST_<Name>_GROUP_RUNNER below.
 *   3. Add a RUN_TEST_GROUP(<Name>) call in run_remote_heap_feature_tests().
 *   4. Add the new source file to the 'remote_heap' feature suite entry in
 *      feature/CMakeLists.txt.
 */

#include "test_utils.h"
#include "unity_fixture.h"

/* ---- Group runner forward declarations ---------------------------------- */
void TEST_RemoteHeapSessionLifecycle_GROUP_RUNNER(void);
void TEST_RemoteHeapMallocFree_GROUP_RUNNER(void);
void TEST_RemoteHeapDynamicLoad_GROUP_RUNNER(void);

/* ---- Suite entry point -------------------------------------------------- */

/**
 * @brief Run all test groups in the "remote_heap" feature suite.
 *
 * Called by run_all_tests() in root_all_tests.c.
 */
void run_remote_heap_feature_tests(void)
{
    if (test_suite_setup("remote_heap") != 0)
        return;

    RUN_TEST_GROUP(RemoteHeapSessionLifecycle);
    RUN_TEST_GROUP(RemoteHeapMallocFree);
    RUN_TEST_GROUP(RemoteHeapDynamicLoad);
}
