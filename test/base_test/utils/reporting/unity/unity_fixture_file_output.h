// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file unity_fixture_file_output.h
 * @brief Unity Fixture integration with Allure XML output
 *
 * This module extends Unity Fixture to automatically capture test results
 * to Allure-compatible JUnit XML files.
 *
 * Usage:
 *   1. Include this header after unity_fixture.h
 *   2. Call UnityFixtureFileOutputBegin() before UnityMain()
 *   3. Call UnityFixtureFileOutputEnd() after UnityMain()
 *
 * IMPORTANT: This header redefines UnityTestRunner to capture test results.
 * It must be included AFTER unity_fixture.h in all test files.
 */

#ifndef UNITY_FIXTURE_FILE_OUTPUT_H
#define UNITY_FIXTURE_FILE_OUTPUT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../allure/unity_allure_output.h"
#include "unity_fixture.h"

/* Redefine UnityTestRunner to use our custom version that captures results */
#ifdef UnityTestRunner
#undef UnityTestRunner
#endif
#define UnityTestRunner UnityTestRunnerWithFileOutput

/**
 * @brief Initialize Unity Fixture file output
 *
 * Call this before UnityMain() to enable file output.
 *
 * @param output_dir Output directory (NULL = default)
 * @param base_name Base filename (NULL = default)
 * @return 0 on success, -1 on error
 */
int UnityFixtureFileOutputBegin(const char *output_dir, const char *base_name);

/**
 * @brief Finalize Unity Fixture file output
 *
 * Call this after UnityMain() to close and finalize output files.
 *
 * @return 0 on success, -1 on error
 */
int UnityFixtureFileOutputEnd(void);

/**
 * @brief Custom test runner with file output integration
 *
 * Drop-in replacement for UnityTestRunner that captures results to file.
 */
void UnityTestRunnerWithFileOutput(unityfunction *setup, unityfunction *test_body,
                                   unityfunction *teardown, const char *printable_name,
                                   const char *group, const char *name, const char *file,
                                   unsigned int line);

/**
 * @brief Wrapper for UnityMain with automatic file output
 *
 * Use this instead of UnityMain() for automatic file output integration.
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @param run_all_tests Test runner function
 * @return Exit code (number of failures)
 */
int UnityMainWithFileOutput(int argc, const char *argv[], void (*run_all_tests)(void));

/* =========================================================================
 * TEST_GROUP_META — per-file Allure classification decorator
 *
 * Place this immediately after TEST_GROUP(group) in every test_*.c file.
 * It declares a static struct that self-registers into a process-wide
 * linked list at program startup via __attribute__((constructor)).
 * UnityTestRunnerWithFileOutput looks up the group name in that list
 * when it opens a new suite and injects the labels into the Allure output.
 *
 * Usage (one line per test file, right after TEST_GROUP):
 *
 *   TEST_GROUP(RemoteHandleOpen);
 *   TEST_GROUP_META(RemoteHandleOpen,
 *                   "unit",
 *                   "FastRPC Remote API",
 *                   "Handle Management",
 *                   "remote_handle_open / remote_handle64_open");
 * ========================================================================= */

typedef struct unity_group_labels {
    const char *group;
    const char *layer;
    const char *epic;
    const char *feature;
    const char *story;
    struct unity_group_labels *next; /* intrusive linked list */
} unity_group_labels_t;

/**
 * @brief Register a labels entry into the global list.
 *
 * Called automatically by the constructor emitted by TEST_GROUP_META.
 * Do not call directly.
 */
void unity_label_registry_add(unity_group_labels_t *entry);

/**
 * @brief Look up labels for a group name.
 *
 * @return Pointer to the entry, or NULL if the group was not registered.
 */
const unity_group_labels_t *unity_label_registry_find(const char *group);

/**
 * @brief TEST_GROUP_META — attach Allure classification labels to a group.
 *
 * Expands to a static struct + a constructor function that registers the
 * struct into the global label registry at program startup.  The struct
 * lives in the same translation unit as the tests it describes, so there
 * is no central registry file to maintain.
 *
 * @param group    Unity TEST_GROUP name (must match exactly)
 * @param layer_   Allure layer   (e.g. "unit", "integration")
 * @param epic_    Allure epic    (e.g. "FastRPC Remote API")
 * @param feature_ Allure feature (e.g. "Handle Management")
 * @param story_   Allure story   (e.g. "remote_handle_open")
 */
#define TEST_GROUP_META(group, layer_, epic_, feature_, story_)                                    \
    static unity_group_labels_t _unity_labels_##group                                              \
        = { #group, (layer_), (epic_), (feature_), (story_), NULL };                               \
    __attribute__((constructor)) static void _unity_labels_##group##_register(void)                \
    {                                                                                              \
        unity_label_registry_add(&_unity_labels_##group);                                          \
    }

/* =========================================================================
 * TEST_LABELS — per-test Allure label override
 *
 * Optional.  Place as the FIRST statement inside a TEST() body to override
 * the group-level labels for that specific test case only.  The override is
 * consumed and cleared automatically after the test completes.
 *
 * Usage:
 *
 *   TEST(RemoteHandleInvoke, Handle32AddSucceeds) {
 *       TEST_LABELS("unit",
 *                   "FastRPC Remote API",
 *                   "Handle Invocation",
 *                   "remote_handle_invoke (32-bit)");
 *       // ... test body ...
 *   }
 * ========================================================================= */

/**
 * @brief Set a per-test label override (cleared automatically after the test).
 *
 * Called by the TEST_LABELS macro.  Do not call directly.
 */
void unity_test_labels_set(const char *layer, const char *epic, const char *feature,
                           const char *story);

/**
 * @brief TEST_LABELS — override Allure labels for one specific test.
 */
#define TEST_LABELS(layer_, epic_, feature_, story_)                                               \
    unity_test_labels_set((layer_), (epic_), (feature_), (story_))

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
 * producing console noise. They are NOT written to Allure XML.
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
