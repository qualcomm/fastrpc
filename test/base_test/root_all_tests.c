// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file root_all_tests.c
 * @brief Root dispatcher for the standalone base-test binary.
 */

#include "test_runner/base_test_runner.h"
#include "test_config.h"
#include "test_utils.h"
#include "unity_fixture.h"
#include "unity_internals.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void run_e2e_suite_tests(void);

static void run_all_tests(void)
{
    run_e2e_suite_tests();
}

static int arguments_request_help(int argc, const char *argv[])
{
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
            return 1;
    }
    return 0;
}

static int run_base_tests(int argc, const char *argv[])
{
    int result = 0;
    unsigned int summary_total = 0;
    unsigned int summary_failures = 0;
    unsigned int summary_ignored = 0;
    int filtered_argc;
    const char **filtered_argv;

    if (arguments_request_help(argc, argv)) {
        fprintf(stdout,
                "FastRPC base tests\n\n"
                "  -d DOMAIN         select a DSP domain (repeatable)\n"
                "  -u 0|1            disable/enable unsigned PD\n"
                "  -v               show passing-test output\n"
                "  --silent          suppress passing-test output\n"
                "  --any-tags TAG    select tests matching any tag\n"
                "  --all-tags TAG    select tests matching all tags\n"
                "  -l MODE           list tests, groups, or tags\n"
                "  -r COUNT          repeat selected tests\n");
        return 0;
    }
    if (unity_test_case_metadata_validate() != 0) {
        fprintf(stderr, "[metadata] invalid test metadata; refusing to run\n");
        return 2;
    }
    if (test_config_init(argc, argv, &filtered_argc, &filtered_argv) != 0)
        return 2;

    if (g_test_config.list_mode != TEST_LIST_NONE) {
        if (UnityGetCommandLineOptions(filtered_argc, filtered_argv) != 0) {
            test_config_free_argv(filtered_argv);
            return 2;
        }
        switch (g_test_config.list_mode) {
        case TEST_LIST_TESTS:
            result = unity_test_case_registry_print_tests();
            break;
        case TEST_LIST_GROUPS:
            result = unity_test_case_registry_print_groups();
            break;
        case TEST_LIST_TAGS:
            result = unity_test_case_registry_print_tags();
            break;
        case TEST_LIST_NONE:
            break;
        }
        test_config_free_argv(filtered_argv);
        return result == 0 ? 0 : 2;
    }

    if (BaseTestRunnerBegin() != 0)
        fprintf(stderr, "Warning: Failed to initialize base-test runner\n");

    for (int i = 0; i < g_test_config.domain_count; i++) {
        int domain_result;
        const char *domain_name;

        g_test_config.domain_id = g_test_config.domain_ids[i];
        domain_name = test_utils_domain_name();
        unity_test_case_print_domain_begin(domain_name, g_test_config.domain_id);
        unity_test_case_dispatch_reset();
        domain_result = BaseTestRunnerMain(filtered_argc, filtered_argv, run_all_tests);
        unity_test_case_dispatch_reset();
        summary_total += Unity.NumberOfTests;
        summary_failures += Unity.TestFailures;
        summary_ignored += Unity.TestIgnores;
        unity_test_case_print_domain_summary(domain_name, g_test_config.domain_id,
                                              Unity.NumberOfTests, Unity.TestFailures,
                                              Unity.TestIgnores);
        unity_test_case_print_domain_end();
        if (domain_result != 0)
            result = 1;
    }

    unity_test_case_set_summary(summary_total, summary_failures, summary_ignored);
    BaseTestRunnerEnd();
    test_config_free_argv(filtered_argv);
    return result;
}

int main(int argc, const char *argv[])
{
    return run_base_tests(argc, argv);
}
