// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file unity_allure_output.h
 * @brief Allure-compatible JSON output for Unity test framework
 *
 * This module generates Allure 2.x JSON format test results.
 */

#ifndef UNITY_ALLURE_OUTPUT_H
#define UNITY_ALLURE_OUTPUT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdio.h>

/* Configuration - same as original */
#ifndef UNITY_ALLURE_OUTPUT_DIR
#define UNITY_ALLURE_OUTPUT_DIR "/data/local/tmp/test-results"
#endif

#ifndef UNITY_ALLURE_OUTPUT_BASENAME
#define UNITY_ALLURE_OUTPUT_BASENAME "fastrpc_test"
#endif

#define UNITY_ALLURE_MAX_PATH 512

/* ========== API Functions (Backward Compatible) ========== */

/**
 * @brief Initialize Allure XML output system
 *
 * Creates output directory if needed and opens XML file for writing.
 * Uses the new modular architecture internally.
 *
 * @param base_name Optional base name for output file (NULL = use default)
 * @return 0 on success, -1 on error
 */
int unity_allure_output_init(const char *base_name);

/**
 * @brief Close Allure XML output and finalize
 *
 * Closes all open XML elements and the file.
 *
 * @return 0 on success, -1 on error
 */
int unity_allure_output_close(void);

/**
 * @brief Start a new test suite (test group)
 *
 * Opens a new <testsuite> element with the given name.
 *
 * @param suite_name Name of the test suite/group
 * @return 0 on success, -1 on error
 */
int unity_allure_output_start_suite(const char *suite_name);

/**
 * @brief End the current test suite
 *
 * Closes the current <testsuite> element with statistics.
 *
 * @param tests Number of tests in this suite
 * @param failures Number of failures in this suite
 * @param skipped Number of skipped tests in this suite
 * @param time_ms Total execution time in milliseconds
 * @return 0 on success, -1 on error
 */
int unity_allure_output_end_suite(uint32_t tests, uint32_t failures, uint32_t skipped,
                                  double time_ms);

/**
 * @brief Write a test case result
 *
 * Writes a <testcase> element with the test result.
 *
 * @param suite_name Test suite name (classname in JUnit XML)
 * @param test_name Test case name
 * @param file Source file path
 * @param line Line number
 * @param time_ms Execution time in milliseconds
 * @param status Test status ("PASS", "FAIL", "IGNORE")
 * @param message Optional failure/skip message (NULL if none)
 * @return 0 on success, -1 on error
 */
int unity_allure_output_write_test(const char *suite_name, const char *test_name, const char *file,
                                   uint32_t line, double time_ms, const char *status,
                                   const char *message);

/**
 * @brief Flush output buffer to file
 *
 * Ensures all buffered output is written to disk.
 */
void unity_allure_output_flush(void);

/**
 * @brief Check if Allure output is enabled and initialized
 *
 * @return 1 if enabled, 0 otherwise
 */
int unity_allure_output_is_enabled(void);

/**
 * @brief Get the current output file path
 *
 * @return Pointer to file path string, or NULL if not initialized
 */
const char *unity_allure_output_get_path(void);

/**
 * @brief Add an Allure classification label to the currently open suite.
 *
 * Call this immediately after unity_allure_output_start_suite() and
 * before any unity_allure_output_write_test() calls.
 *
 * @param name   Label name  ("layer", "epic", "feature", "story")
 * @param value  Label value
 * @return 0 on success, -1 on error
 */
int unity_allure_output_add_suite_label(const char *name, const char *value);

/**
 * @brief Add a per-test Allure label override to the most recently written test.
 *
 * Call this immediately after unity_allure_output_write_test().
 * When present, these labels replace the suite-level classification labels
 * for that specific test in the Allure JSON output.
 *
 * @param name   Label name  ("layer", "epic", "feature", "story")
 * @param value  Label value
 * @return 0 on success, -1 on error
 */
int unity_allure_output_add_last_result_label(const char *name, const char *value);

/**
 * @brief Attach a file to the current test case
 *
 * Attaches a file (e.g., log file) to the most recently written test case.
 * The file will be referenced in the Allure report.
 *
 * @param file_path Path to the file to attach
 * @param attachment_name Display name for the attachment (NULL = use filename)
 * @param attachment_type MIME type (e.g., "text/plain", "application/log")
 * @return 0 on success, -1 on error
 */
int unity_allure_output_attach_file(const char *file_path, const char *attachment_name,
                                    const char *attachment_type);

/**
 * @brief Compose and register the Allure environment name.
 *
 * Builds the environment name as "<device_name>-<fastrpc_version>",
 * writes an environment.properties file into UNITY_ALLURE_OUTPUT_DIR
 * (consumed by Allure as legacy Metadata and by generate_report.sh to
 * pass --environment to allure generate), and injects a "host" label
 * into every subsequent test result JSON so Allure Report 3's
 * label-based environment matching works automatically.
 *
 * Call once, immediately after unity_allure_output_init() succeeds.
 *
 * @param device_name     Explicit device/host name, or NULL to auto-detect
 *                        at runtime via `hostname` / `uname -n`.
 * @param fastrpc_version FastRPC version string, or NULL to use the
 *                        compile-time FASTRPC_TEST_VERSION macro.
 * @return 0 on success, -1 on error (non-fatal; reporting continues).
 */
int unity_allure_output_set_environment(const char *device_name, const char *fastrpc_version);

/**
 * @brief Signal that a test body is about to execute.
 *
 * Must be called immediately before the test body runs (before
 * OriginalUnityTestRunner).  While the gate is open, attach_file()
 * queues attachments into a pending buffer so they are not mistakenly
 * appended to the previous test's result.  The gate is automatically
 * cleared when record_pass/failure/skip/error() is called.
 */
void unity_allure_output_begin_test(void);

#ifdef __cplusplus
}
#endif

#endif /* UNITY_ALLURE_OUTPUT_H */
