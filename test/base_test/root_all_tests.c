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

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

/* ---- Unit suite declarations -------------------------------------------- */
void run_dspqueue_tests(void);

/* ---- Feature suite declarations ----------------------------------------- */
void run_dspqueue_feature_tests(void);
void run_remote_heap_feature_tests(void);

/* ---- Root dispatcher ---------------------------------------------------- */

static void run_all_tests(void)
{
    /* Unit suites */
    run_dspqueue_tests();

    /* Feature suites */
    run_dspqueue_feature_tests();
    run_remote_heap_feature_tests();
}

static int run_base_tests(int argc, const char *argv[])
{
    int filtered_argc;
    const char **filtered_argv;
    int result = 0;

    if (test_config_init(argc, argv, &filtered_argc, &filtered_argv) != 0)
        return 2;

    for (int i = 0; i < g_test_config.domain_count; i++) {
        int domain_result;

        g_test_config.domain_id = g_test_config.domain_ids[i];
        printf("\n[test_config] running tests on %s domain (%d)\n",
               test_utils_domain_name(), g_test_config.domain_id);

        if (UnityFixtureFileOutputBegin(NULL, NULL) != 0)
            fprintf(stderr, "Warning: Failed to initialize custom Unity output\n");

        domain_result = UnityMain(filtered_argc, filtered_argv, run_all_tests);
        UnityFixtureFileOutputEnd();

        result += domain_result;
    }

    free((void *)filtered_argv);

    return result;
}

#ifdef BASE_TEST_PLUGIN
/* Legacy fastrpc_test plugin ABI; keep in sync with test/fastrpc_test.c. */
int run_test(int domain_id, bool is_unsignedpd_enabled)
{
    char domain_arg[12];
    char unsigned_pd_arg[2];
    const char *argv[] = {
        "base_test",
        "-d",
        domain_arg,
        "-u",
        unsigned_pd_arg,
    };

    snprintf(domain_arg, sizeof(domain_arg), "%d", domain_id);
    snprintf(unsigned_pd_arg, sizeof(unsigned_pd_arg), "%d", is_unsignedpd_enabled ? 1 : 0);

    return run_base_tests((int)(sizeof(argv) / sizeof(argv[0])), argv);
}
#else
int main(int argc, const char *argv[])
{
    return run_base_tests(argc, argv);
}
#endif
