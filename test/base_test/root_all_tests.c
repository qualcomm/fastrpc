// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file root_all_tests.c
 * @brief Root dispatcher — owns main() and calls every suite runner.
 *
 * To add a new unit suite:
 *   1. Create test/unit/<name>/all_tests.c defining
 *      void run_<name>_tests(void).
 *   2. Add the extern declaration and call in run_all_tests() below.
 *   3. Add the suite name to _unit_suites in test/unit/CMakeLists.txt.
 *
 * To add a new feature suite:
 *   1. Create test/feature/<name>/all_tests.c defining
 *      void run_<name>_feature_tests(void).
 *   2. Add the extern declaration and call in run_all_tests() below.
 *   3. Add the suite name to _feat_suites in test/feature/CMakeLists.txt.
 */

#include "reporting/unity/unity_fixture_file_output.h"
#include "test_utils.h"
#include "unity_fixture.h"

#include <stdio.h>

/* ---- Unit suite declarations -------------------------------------------- */
void run_dspqueue_tests(void);

/* ---- Feature suite declarations ----------------------------------------- */
void run_dspqueue_feature_tests(void);

/* ---- Root dispatcher ---------------------------------------------------- */

static void run_all_tests(void)
{
    /* Unit suites */
    run_dspqueue_tests();

    /* Feature suites */
    run_dspqueue_feature_tests();
}

int main(int argc, const char *argv[])
{
    int filtered_argc;
    const char **filtered_argv;
    int result;

    test_config_init(argc, argv, &filtered_argc, &filtered_argv);

    if (UnityFixtureFileOutputBegin(NULL, NULL) != 0) {
        fprintf(stderr, "Warning: Failed to initialize Unity file output\n");
    }

    unity_allure_output_set_environment(NULL, NULL);

    result = UnityMain(filtered_argc, filtered_argv, run_all_tests);

    UnityFixtureFileOutputEnd();

    return result;
}
