// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef TEST_CONFIG_H
#define TEST_CONFIG_H

#ifndef NUM_DOMAINS
#define NUM_DOMAINS 8
#endif

/* Maximum number of tag values accepted per tag flag on the command line. */
#define TEST_CONFIG_MAX_TAGS 32

typedef enum {
    TEST_LIST_NONE = 0,
    TEST_LIST_TESTS,
    TEST_LIST_GROUPS,
    TEST_LIST_TAGS,
} test_list_mode_t;

typedef struct {
    int domain_id;
    int domain_ids[NUM_DOMAINS];
    int domain_count;
    int unsigned_pd;
    int silent_mode; /**< --silent; suppress per-test output for passing tests */
    test_list_mode_t list_mode;
    const char *any_tags[TEST_CONFIG_MAX_TAGS]; /**< --any-tags values (OR filter) */
    int any_tag_count;                          /**< number of active --any-tags entries */
    const char *all_tags[TEST_CONFIG_MAX_TAGS]; /**< --all-tags values (AND filter) */
    int all_tag_count;                          /**< number of active --all-tags entries */
} test_config_t;

#ifdef __cplusplus
extern "C" {
#endif

extern test_config_t g_test_config;

/* Parses FastRPC-specific arguments, writes them into g_test_config, and
 * returns a filtered (argc, argv) pair for Unity. With no -d, discovers all
 * available DSP domains unless a metadata listing was requested. Returns 0
 * on success or -1 for invalid arguments/discovery failure. */
int test_config_init(int argc, const char **argv, int *out_argc, const char ***out_argv);
void test_config_free_argv(const char **argv);

#ifdef __cplusplus
}
#endif

#endif /* TEST_CONFIG_H */
