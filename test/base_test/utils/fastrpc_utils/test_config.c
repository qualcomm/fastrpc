// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "test_config.h"
#include "test_domain.h"
#include "test_utils.h"
#include "unity_fixture.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

test_config_t g_test_config = {
    .domain_id = TEST_DOMAIN_DEFAULT_ID,
    .domain_ids = { 0 },
    .domain_count = 0,
    .unsigned_pd = 1,
    .silent_mode = 0,
    .list_mode = TEST_LIST_NONE,
    .any_tags = { NULL }, /* populated by --any-tags */
    .any_tag_count = 0,
    .all_tags = { NULL }, /* populated by --all-tags          */
    .all_tag_count = 0,
};
static int add_explicit_domain(int domain_id)
{
    for (int i = 0; i < g_test_config.domain_count; i++) {
        if (g_test_config.domain_ids[i] == domain_id)
            return 0;
    }

    if (g_test_config.domain_count >= test_domain_count())
        return -1;

    g_test_config.domain_ids[g_test_config.domain_count++] = domain_id;
    return 0;
}

static int parse_domain_id(const char *value, int *domain_id)
{
    char *end;
    long parsed;

    if (!value || !domain_id || value[0] == '\0')
        return -1;

    errno = 0;
    parsed = strtol(value, &end, 10);
    if (errno != 0 || !end || *end != '\0')
        return -1;

    if (parsed < INT_MIN || parsed > INT_MAX)
        return -1;

    if (test_domain_is_supported((int)parsed) > 0) {
        *domain_id = (int)parsed;
        return 0;
    }

    return -1;
}

static int parse_boolean(const char *value, int *parsed_value)
{
    char *end;
    long parsed;

    if (!value || !parsed_value || value[0] == '\0')
        return -1;

    errno = 0;
    parsed = strtol(value, &end, 10);
    if (errno != 0 || !end || *end != '\0' || (parsed != 0 && parsed != 1))
        return -1;

    *parsed_value = (int)parsed;
    return 0;
}

static int set_list_mode(test_list_mode_t mode)
{
    if (g_test_config.list_mode != TEST_LIST_NONE && g_test_config.list_mode != mode) {
        fprintf(stderr, "[test_config] only one test listing mode may be selected\n");
        return -1;
    }

    g_test_config.list_mode = mode;
    return 0;
}

static int parse_list_mode(const char *value)
{
    if (strcmp(value, "tests") == 0)
        return set_list_mode(TEST_LIST_TESTS);
    if (strcmp(value, "groups") == 0)
        return set_list_mode(TEST_LIST_GROUPS);
    if (strcmp(value, "tags") == 0)
        return set_list_mode(TEST_LIST_TAGS);

    fprintf(stderr, "[test_config] invalid -l value '%s'; expected tests, groups, or tags\n",
            value);
    return -1;
}

static int arguments_request_help(int argc, const char **argv)
{
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
            return 1;
    }
    return 0;
}

static int validate_unity_arguments(int argc, const char **argv)
{
    for (int i = 1; i < argc; i++) {
        const char *argument = argv[i];

        if (strcmp(argument, "-v") == 0 || strcmp(argument, "-s") == 0 ||
            strcmp(argument, "-h") == 0 || strcmp(argument, "--help") == 0)
            continue;

        if (strcmp(argument, "-g") == 0 || strcmp(argument, "-G") == 0 ||
            strcmp(argument, "-n") == 0 || strcmp(argument, "-N") == 0) {
            if (++i >= argc || argv[i][0] == '\0' || argv[i][0] == '-')
                return -1;
            continue;
        }

        if (strcmp(argument, "-r") == 0) {
            char *end;
            unsigned long repeat;

            if (++i >= argc || argv[i][0] == '\0' || argv[i][0] == '-')
                return -1;
            errno = 0;
            repeat = strtoul(argv[i], &end, 10);
            if (errno != 0 || !end || *end != '\0' || repeat == 0 || repeat > UINT_MAX)
                return -1;
            continue;
        }

        return -1;
    }
    return 0;
}

int test_config_init(int argc, const char **argv, int *out_argc, const char ***out_argv)
{
    const char **filtered = malloc(((size_t)argc + 1U) * sizeof(*filtered));
    int domains_explicit = 0;
    int unsigned_pd_explicit = 0;
    int silent_explicit = 0;

    if (!filtered) {
        fprintf(stderr, "[test_config] malloc failed\n");
        return -1;
    }

    int fi = 0;

    g_test_config.domain_count = 0;
    g_test_config.domain_id = TEST_DOMAIN_DEFAULT_ID;
    g_test_config.unsigned_pd = 1;
    g_test_config.silent_mode = 0;
    g_test_config.list_mode = TEST_LIST_NONE;
    g_test_config.any_tag_count = 0;
    g_test_config.all_tag_count = 0;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-l") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "[test_config] -l requires tests, groups, or tags\n");
                free(filtered);
                return -1;
            }
            if (parse_list_mode(argv[++i]) != 0) {
                free(filtered);
                return -1;
            }
        } else if (strcmp(argv[i], "--list-tests") == 0) {
            if (set_list_mode(TEST_LIST_TESTS) != 0) {
                free(filtered);
                return -1;
            }
        } else if (strcmp(argv[i], "--list-groups") == 0) {
            if (set_list_mode(TEST_LIST_GROUPS) != 0) {
                free(filtered);
                return -1;
            }
        } else if (strcmp(argv[i], "--list-tags") == 0) {
            if (set_list_mode(TEST_LIST_TAGS) != 0) {
                free(filtered);
                return -1;
            }
        } else if (strcmp(argv[i], "-d") == 0) {
            int domain_id;

            if (i + 1 >= argc) {
                fprintf(stderr, "[test_config] -d requires a domain ID\n");
                free(filtered);
                return -1;
            }

            if (parse_domain_id(argv[++i], &domain_id) != 0 ||
                add_explicit_domain(domain_id) != 0) {
                fprintf(stderr, "[test_config] invalid domain ID: %s\n", argv[i]);
                free(filtered);
                return -1;
            }
            domains_explicit = 1;
        } else if (strcmp(argv[i], "-u") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "[test_config] -u requires 0 or 1\n");
                free(filtered);
                return -1;
            }
            if (parse_boolean(argv[++i], &g_test_config.unsigned_pd) != 0) {
                fprintf(stderr, "[test_config] invalid unsigned-PD value: %s\n", argv[i]);
                free(filtered);
                return -1;
            }
            unsigned_pd_explicit = 1;
        } else if (strcmp(argv[i], "--silent") == 0) {
            /* --silent suppresses custom output for passing tests. It is
             * consumed here and never forwarded to UnityMain(). */
            g_test_config.silent_mode = 1;
            UnityFixture.Silent = 1;
            silent_explicit = 1;
        } else if (strcmp(argv[i], "--any-tags") == 0) {
            /*
             * --any-tags <tag>  (repeatable, up to TEST_CONFIG_MAX_TAGS)
             *
             * OR-mode filter: a test runs if its metadata annotation
             * contains AT LEAST ONE of the --any-tags values.
             *
             * Consumed here; never forwarded to UnityMain().
             */
            if (i + 1 >= argc) {
                fprintf(stderr, "[test_config] --any-tags requires a tag\n");
                free(filtered);
                return -1;
            }
            if (g_test_config.any_tag_count < TEST_CONFIG_MAX_TAGS) {
                g_test_config.any_tags[g_test_config.any_tag_count++] = argv[++i];
            } else {
                fprintf(stderr,
                        "[test_config] Warning: --any-tags limit (%d) reached, "
                        "ignoring '%s'\n",
                        TEST_CONFIG_MAX_TAGS, argv[++i]);
            }
        } else if (strcmp(argv[i], "--all-tags") == 0) {
            /*
             * --all-tags <tag>  (repeatable, up to TEST_CONFIG_MAX_TAGS)
             *
             * AND-mode filter: a test runs only if its metadata annotation
             * contains ALL of the --all-tags values.
             *
             * Consumed here; never forwarded to UnityMain().
             */
            if (i + 1 >= argc) {
                fprintf(stderr, "[test_config] --all-tags requires a tag\n");
                free(filtered);
                return -1;
            }
            if (g_test_config.all_tag_count < TEST_CONFIG_MAX_TAGS) {
                g_test_config.all_tags[g_test_config.all_tag_count++] = argv[++i];
            } else {
                fprintf(stderr,
                        "[test_config] Warning: --all-tags limit (%d) reached, "
                        "ignoring '%s'\n",
                        TEST_CONFIG_MAX_TAGS, argv[++i]);
            }
        } else {
            filtered[fi++] = argv[i];
        }
    }

    filtered[fi] = NULL;
    if (validate_unity_arguments(fi, filtered) != 0) {
        fprintf(stderr, "[test_config] invalid Unity option or missing option value\n");
        free(filtered);
        return -1;
    }

    if (g_test_config.list_mode != TEST_LIST_NONE || arguments_request_help(fi, filtered)) {
        *out_argc = fi;
        *out_argv = filtered;
        return 0;
    }

    if (unsigned_pd_explicit)
        printf("[test_config] unsigned_pd = %d\n", g_test_config.unsigned_pd);
    if (silent_explicit)
        printf("[test_config] silent_mode = %d\n", g_test_config.silent_mode);
    for (int i = 0; i < g_test_config.any_tag_count; i++)
        printf("[test_config] any-tag filter[%d] = %s\n", i, g_test_config.any_tags[i]);
    for (int i = 0; i < g_test_config.all_tag_count; i++)
        printf("[test_config] all-tag filter[%d] = %s\n", i, g_test_config.all_tags[i]);

    if (g_test_config.domain_count == 0) {
        g_test_config.domain_count = test_domain_discover(g_test_config.domain_ids);
        if (g_test_config.domain_count <= 0) {
            fprintf(stderr,
                    "[test_config] no running DSP domains with FastRPC device nodes found\n");
            free(filtered);
            return -1;
        }
    }

    printf("[test_config] %s domains:",
           domains_explicit ? "selected" : "discovered");
    for (int i = 0; i < g_test_config.domain_count; i++) {
        const char *domain_name = test_utils_domain_name_for(g_test_config.domain_ids[i]);

        printf(" %s(%d)", domain_name ? domain_name : "<invalid>",
               g_test_config.domain_ids[i]);
    }
    putchar('\n');

    *out_argc = fi;
    *out_argv = filtered;
    return 0;
}

void test_config_free_argv(const char **argv)
{
    free((void *)argv);
}
