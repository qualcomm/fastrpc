// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef XML_OUTPUT_FACADE_H
#define XML_OUTPUT_FACADE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/**
 * @file xml_output_facade.h
 * @brief High-level facade for XML test result output
 *
 * This provides a simple, business-focused API that hides all XML
 * implementation details. Perfect for test frameworks that just want
 * to record test results without worrying about XML specifics.
 */

/* Forward declarations */
typedef struct xml_test_writer xml_test_writer_t;

/* ========== Lifecycle Operations ========== */

/**
 * @brief Create a new test result writer
 *
 * @param output_path Path where XML file will be written
 * @return New writer instance or NULL on error
 */
xml_test_writer_t *xml_test_writer_create(const char *output_path);

/**
 * @brief Create a writer with custom base name
 *
 * @param output_dir Output directory
 * @param base_name Base name for output file (timestamp will be appended)
 * @return New writer instance or NULL on error
 */
xml_test_writer_t *xml_test_writer_create_with_name(const char *output_dir, const char *base_name);

/**
 * @brief Destroy a test writer
 *
 * Automatically finalizes and closes the XML output.
 */
void xml_test_writer_destroy(xml_test_writer_t *writer);

/* ========== Test Run Operations ========== */

/**
 * @brief Begin a test run
 *
 * @param writer The writer instance
 * @param name Name of the test run (e.g., "FastRPC Tests")
 * @return 0 on success, -1 on error
 */
int xml_test_writer_begin_run(xml_test_writer_t *writer, const char *name);

/**
 * @brief End the test run
 *
 * Finalizes all statistics and closes the XML document.
 *
 * @return 0 on success, -1 on error
 */
int xml_test_writer_end_run(xml_test_writer_t *writer);

/* ========== Test Suite Operations ========== */

/**
 * @brief Begin a test suite
 *
 * @param writer The writer instance
 * @param name Name of the test suite (e.g., "RemoteHandleOpen")
 * @return 0 on success, -1 on error
 */
int xml_test_writer_begin_suite(xml_test_writer_t *writer, const char *name);

/**
 * @brief End the current test suite
 *
 * @return 0 on success, -1 on error
 */
int xml_test_writer_end_suite(xml_test_writer_t *writer);

/* ========== Test Result Recording ========== */

/**
 * @brief Record a passing test
 *
 * @param writer The writer instance
 * @param name Test name
 * @param duration_ms Test duration in milliseconds
 * @return 0 on success, -1 on error
 */
int xml_test_writer_record_pass(xml_test_writer_t *writer, const char *name, double duration_ms);

/**
 * @brief Record a failing test
 *
 * @param writer The writer instance
 * @param name Test name
 * @param file Source file where test is defined
 * @param line Line number
 * @param message Failure message
 * @param duration_ms Test duration in milliseconds
 * @return 0 on success, -1 on error
 */
int xml_test_writer_record_failure(xml_test_writer_t *writer, const char *name, const char *file,
                                   uint32_t line, const char *message, double duration_ms);

/**
 * @brief Record a skipped test
 *
 * @param writer The writer instance
 * @param name Test name
 * @param reason Reason for skipping
 * @return 0 on success, -1 on error
 */
int xml_test_writer_record_skip(xml_test_writer_t *writer, const char *name, const char *reason);

/**
 * @brief Record a test with error
 *
 * @param writer The writer instance
 * @param name Test name
 * @param file Source file
 * @param line Line number
 * @param message Error message
 * @param duration_ms Test duration in milliseconds
 * @return 0 on success, -1 on error
 */
int xml_test_writer_record_error(xml_test_writer_t *writer, const char *name, const char *file,
                                 uint32_t line, const char *message, double duration_ms);

/**
 * @brief Attach a file to the current test case
 *
 * Adds a file attachment to the most recently recorded test case.
 * The file path is stored in the XML output for Allure to process.
 *
 * @param writer The writer instance
 * @param file_path Path to the file to attach
 * @param attachment_name Display name for the attachment (NULL = use filename)
 * @param attachment_type MIME type or description (e.g., "text/plain", "application/log")
 * @return 0 on success, -1 on error
 */
int xml_test_writer_attach_file(xml_test_writer_t *writer, const char *file_path,
                                const char *attachment_name, const char *attachment_type);

/* ========== Utility Operations ========== */

/**
 * @brief Add an Allure classification label to the currently open suite.
 *
 * Must be called after xml_test_writer_begin_suite() and before
 * xml_test_writer_end_suite().  Labels are inherited by every test
 * result recorded in the suite.
 *
 * @param writer  The writer instance
 * @param name    Label name  ("layer", "epic", "feature", "story", ...)
 * @param value   Label value
 * @return 0 on success, -1 on error
 */
int xml_test_writer_add_suite_label(xml_test_writer_t *writer, const char *name, const char *value);

/**
 * @brief Add a per-test Allure label override to the most recently recorded result.
 *
 * Must be called after xml_test_writer_record_pass/failure/skip/error().
 * When present, these labels replace the suite-level classification labels
 * for that specific test in the Allure JSON output.
 *
 * @param writer  The writer instance
 * @param name    Label name  ("layer", "epic", "feature", "story", ...)
 * @param value   Label value
 * @return 0 on success, -1 on error
 */
int xml_test_writer_add_last_result_label(xml_test_writer_t *writer, const char *name,
                                          const char *value);

/**
 * @brief Flush any buffered output
 *
 * @return 0 on success, -1 on error
 */
int xml_test_writer_flush(xml_test_writer_t *writer);

/**
 * @brief Get the output file path
 *
 * @return Path to output file or NULL if not available
 */
const char *xml_test_writer_get_path(xml_test_writer_t *writer);

/**
 * @brief Get total number of tests recorded
 */
uint32_t xml_test_writer_get_test_count(xml_test_writer_t *writer);

/**
 * @brief Get number of failures
 */
uint32_t xml_test_writer_get_failure_count(xml_test_writer_t *writer);

/**
 * @brief Get number of errors
 */
uint32_t xml_test_writer_get_error_count(xml_test_writer_t *writer);

/**
 * @brief Get number of skipped tests
 */
uint32_t xml_test_writer_get_skip_count(xml_test_writer_t *writer);

/**
 * @brief Check if any errors occurred during writing
 */
bool xml_test_writer_has_error(xml_test_writer_t *writer);

/**
 * @brief Get last error message
 */
const char *xml_test_writer_get_error_message(xml_test_writer_t *writer);

/* ========== Configuration ========== */

/**
 * @brief Enable/disable pretty printing
 */
void xml_test_writer_set_pretty_print(xml_test_writer_t *writer, bool enable);

/**
 * @brief Set indent size for pretty printing
 */
void xml_test_writer_set_indent_size(xml_test_writer_t *writer, int size);

/**
 * @brief Enable/disable validation
 */
void xml_test_writer_set_validation(xml_test_writer_t *writer, bool enable);

/* ========== Environment ========== */

/**
 * @brief Set the environment name to embed in every test result JSON.
 */
void xml_test_writer_set_environment(xml_test_writer_t *writer, const char *env_name);

/* ========== Test body scope ========== */

/**
 * @brief Signal that a test body is about to execute.
 *
 * While in_test_body is set, xml_test_writer_attach_file() queues
 * attachments into a pending buffer instead of appending them to
 * tests_tail (which still points to the previous test's result).
 * The queue is flushed onto the correct result by the next call to
 * xml_test_writer_record_pass/failure/skip/error().
 *
 * Call this immediately before invoking the test body (before
 * OriginalUnityTestRunner).  The flag is automatically cleared by
 * flush_pending_attachments() inside every record_*() function.
 */
void xml_test_writer_begin_test(xml_test_writer_t *writer);

#ifdef __cplusplus
}
#endif

#endif /* XML_OUTPUT_FACADE_H */
