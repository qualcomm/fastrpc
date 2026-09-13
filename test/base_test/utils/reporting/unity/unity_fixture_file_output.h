// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file unity_fixture_file_output.h
 * @brief Unity Fixture integration for formatted output, filtering, and logs
 *
 * This module extends Unity Fixture with formatted console output, tag-based
 * filtering, error-code summaries, and per-test log capture.
 *
 * Usage:
 *   1. Include this header after unity_fixture.h
 *   2. Call UnityFixtureFileOutputBegin() before UnityMain()
 *   3. Call UnityFixtureFileOutputEnd() after UnityMain()
 *
 * IMPORTANT: This header redefines UnityTestRunner to add the custom behavior.
 * It must be included AFTER unity_fixture.h in all test files.
 */

#ifndef UNITY_FIXTURE_FILE_OUTPUT_H
#define UNITY_FIXTURE_FILE_OUTPUT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "unity_fixture.h"

/* Redefine UnityTestRunner to use the custom test runner. */
#ifdef UnityTestRunner
#undef UnityTestRunner
#endif
#define UnityTestRunner UnityTestRunnerWithFileOutput

/**
 * @brief Initialize the custom Unity Fixture output
 *
 * Call this before UnityMain() to enable formatted output and log capture.
 *
 * @param output_dir Log output directory (NULL = default)
 * @param base_name Reserved; pass NULL
 * @return 0
 */
int UnityFixtureFileOutputBegin(const char *output_dir, const char *base_name);

/**
 * @brief Finalize the custom Unity Fixture output
 *
 * Call this after UnityMain() to stop log capture and print the summary.
 *
 * @return 0
 */
int UnityFixtureFileOutputEnd(void);

/**
 * @brief Custom test runner with formatted output, filtering, and logs
 *
 * Drop-in replacement for UnityTestRunner.
 */
void UnityTestRunnerWithFileOutput(unityfunction *setup, unityfunction *test_body,
                                   unityfunction *teardown, const char *printable_name,
                                   const char *group, const char *name, const char *file,
                                   unsigned int line);

/**
 * @brief Wrapper for UnityMain with automatic custom output setup
 *
 * Use this instead of UnityMain() for automatic setup and cleanup.
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @param run_all_tests Test runner function
 * @return Exit code (number of failures)
 */
int UnityMainWithFileOutput(int argc, const char *argv[], void (*run_all_tests)(void));

/* =========================================================================
 * TEST_CASE_TAGS — per-test-case tag annotation for CLI inclusion filtering
 *
 * PLACEMENT
 * ---------
 * Place this immediately after the closing brace of a TEST() body to
 * annotate that specific test case with one or more descriptive tags.
 * Both the group and name tokens must match the TEST() declaration exactly.
 *
 *   TEST(RemoteHandleOpen, ValidUriSucceeds) {
 *       // ... test body ...
 *   }
 *   TEST_CASE_TAGS(RemoteHandleOpen, ValidUriSucceeds,
 *                  "Remote", "unit", "positive");
 *
 * FILTERING SEMANTICS
 * -------------------
 * Two independent tag filters are available, controlled by separate flags:
 *
 *   --any-tags <tag>  (repeatable)   OR  logic — run if ANY tag matches
 *   --all-tags <tag>  (repeatable)   AND logic — run only if ALL tags match
 *   --tags <tag>      (repeatable)   Backward-compatible alias for --any-tags
 *
 * When neither flag is specified, all tests run regardless of annotations.
 *
 * When both flags are specified, a test must pass BOTH filters:
 *
 *   ./test-fastrpc --any-tags Remote --all-tags unit --all-tags negative
 *   → runs tests tagged "Remote" that are also tagged both "unit" AND
 *     "negative" (i.e. negative unit tests for the Remote subsystem)
 *
 * The filter is applied inside UnityTestRunnerWithFileOutput() AFTER
 * Unity's own -G / -g / -N / -n filter, so both can be combined freely.
 *
 * Skipped-by-tag tests increment Unity.NumberOfTests and Unity.TestIgnores
 * directly (no '!' output) so they appear in the summary totals without
 * producing console noise.
 *
 * TAG VOCABULARY (recommended, not enforced by the macro)
 * --------------------------------------------------------
 * Four orthogonal categories cover every test in the suite:
 *
 *   Functionality  — which subsystem / API is under test
 *                    "Remote"    remote_handle_* and remote_session_control
 *                    "DspQueue"  dspqueue_* APIs
 *                    "RpcMem"    rpcmem_* APIs
 *                    "Profiling" RPC performance measurement tests
 *
 *   Classification — nature of the test
 *                    "unit"      function-level unit test
 *                    "feature"   end-to-end / integration test
 *                    "profiling" performance / benchmarking test
 *
 *   Polarity       — expected behavior direction
 *                    "positive"  validates correct / happy-path behavior
 *                    "negative"  validates error handling / rejection
 *
 *   API name       — the specific function under test (enables per-API
 *                    filtering with --any-tags <api_name>)
 *                    e.g. "remote_handle_open", "remote_handle64_open",
 *                         "fastrpc_mmap", "dspqueue_create", "rpcmem_alloc"
 *                    When a test file covers multiple API variants (e.g.
 *                    32-bit and 64-bit), each test case carries the tag
 *                    of the specific function it exercises.
 *
 * A test carries one tag from each category:
 *
 *   TEST_CASE_TAGS(RemoteHandleOpen, NullUriFails,
 *                  "Remote", "unit", "negative", "remote_handle_open");
 *
 * IMPLEMENTATION NOTES
 * --------------------
 * The macro expands to:
 *   1. A static NULL-terminated const char* array of tag strings.
 *   2. A static struct unity_test_case_tags instance (group, name, tags[]).
 *   3. A __attribute__((constructor)) function that registers the struct
 *      into the global tag registry at program startup — zero overhead
 *      at test-run time.
 *
 * Symbol names are mangled with both group and name to guarantee
 * uniqueness across all translation units in the binary.
 * Tags are compared case-sensitively (strcmp).
 * ========================================================================= */

/**
 * @brief Per-test-case tag registry entry.
 *
 * One instance is emitted per TEST_CASE_TAGS() invocation.
 * Instances self-register into a global linked list at program startup
 * via __attribute__((constructor)).
 */
struct unity_test_case_tags {
    const char *group;                 /**< TEST_GROUP name (string literal) */
    const char *name;                  /**< TEST case name  (string literal) */
    const char *const *tags;           /**< NULL-terminated array of tag strings */
    struct unity_test_case_tags *next; /**< intrusive singly-linked list */
};
typedef struct unity_test_case_tags unity_test_case_tags_t;

/**
 * @brief Register a test-case tag entry into the global registry.
 *
 * Called automatically by the constructor emitted by TEST_CASE_TAGS().
 * Do not call directly.
 */
void unity_test_case_tag_registry_add(unity_test_case_tags_t *entry);

/**
 * @brief Look up the tag entry for a (group, name) pair.
 *
 * @param group  TEST_GROUP name.
 * @param name   TEST case name.
 * @return Pointer to the entry, or NULL if not registered.
 */
const unity_test_case_tags_t *unity_test_case_tag_registry_find(const char *group,
                                                                const char *name);

/**
 * @brief Check whether a test case passes all active tag filters.
 *
 * Returns 1 (run) when all active filters pass:
 *   - --any-tags filter passes when any_tag_count == 0 (inactive) OR the
 *     test's tag list contains at least one of the any_tags[] values.
 *   - --all-tags filter passes when all_tag_count == 0 (inactive) OR the
 *     test's tag list contains every one of the all_tags[] values.
 *
 * Returns 0 (skip) when:
 *   - Any active filter fails, OR
 *   - Any filter is active AND the test has no TEST_CASE_TAGS() annotation.
 *
 * When 0 is returned, the caller increments Unity.NumberOfTests and
 * Unity.TestIgnores directly without printing '!' so the summary totals
 * stay accurate without console noise.
 *
 * @param group  TEST_GROUP name.
 * @param name   TEST case name.
 * @return 1 if the test should run, 0 if it should be skipped.
 */
int unity_test_case_tag_filter_passes(const char *group, const char *name);

/* Print sorted metadata entries matching the active Unity and tag filters. */
int unity_test_case_registry_print_tests(void);
int unity_test_case_registry_print_groups(void);
int unity_test_case_registry_print_tags(void);

/**
 * @brief TEST_CASE_TAGS — annotate a single test case with descriptive tags.
 *
 * See the block comment above for full usage, semantics, and tag vocabulary.
 */
#define TEST_CASE_TAGS(group, name, ...)                                                           \
    static const char *const _utct_tags_##group##_##name##_list[] = { __VA_ARGS__, NULL };         \
    static unity_test_case_tags_t _utct_entry_##group##_##name                                     \
        = { #group, #name, _utct_tags_##group##_##name##_list, NULL };                             \
    __attribute__((constructor)) static void _utct_register_##group##_##name(void)                 \
    {                                                                                              \
        unity_test_case_tag_registry_add(&_utct_entry_##group##_##name);                           \
    }

#ifdef __cplusplus
}
#endif

#endif /* UNITY_FIXTURE_FILE_OUTPUT_H */
