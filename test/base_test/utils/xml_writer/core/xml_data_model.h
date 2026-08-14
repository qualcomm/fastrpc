// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef XML_DATA_MODEL_H
#define XML_DATA_MODEL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/**
 * @file xml_data_model.h
 * @brief Core data models for test results
 *
 * Pure data structures with no XML knowledge.
 * Provides separation between business data and XML representation.
 */

/* Test status enumeration */
typedef enum {
    XML_TEST_STATUS_PASS,
    XML_TEST_STATUS_FAIL,
    XML_TEST_STATUS_SKIP,
    XML_TEST_STATUS_ERROR
} xml_test_status_t;

/* Maximum number of Allure classification labels per suite */
#define XML_SUITE_MAX_LABELS 16

/**
 * @brief Allure classification label (name/value pair)
 */
typedef struct {
    char name[64];
    char value[256];
} xml_allure_label_t;

/* Forward declarations */
typedef struct xml_test_result xml_test_result_t;
typedef struct xml_test_suite xml_test_suite_t;
typedef struct xml_test_suites xml_test_suites_t;

/**
 * @brief Individual test result data
 */
struct xml_test_result {
    char *name;               /* Test name */
    char *classname;          /* Test class/suite name */
    char *file;               /* Source file path */
    uint32_t line;            /* Line number */
    double duration_ms;       /* Execution time in milliseconds */
    double start_time_ms;     /* Start time in milliseconds since epoch */
    xml_test_status_t status; /* Test status */
    char *message;            /* Failure/skip message */
    char *stack_trace;        /* Stack trace for failures */
    time_t timestamp;         /* Test execution timestamp */
    xml_test_result_t *next;  /* Linked list next pointer */

    /* Per-test Allure label overrides (set via TEST_LABELS) */
    xml_allure_label_t result_labels[XML_SUITE_MAX_LABELS];
    int result_label_count;
};

/**
 * @brief Test suite containing multiple test results
 */
struct xml_test_suite {
    char *name;                    /* Suite name */
    char *timestamp;               /* ISO 8601 timestamp */
    uint32_t test_count;           /* Total number of tests */
    uint32_t failure_count;        /* Number of failures */
    uint32_t error_count;          /* Number of errors */
    uint32_t skipped_count;        /* Number of skipped tests */
    double total_duration_ms;      /* Total execution time */
    xml_test_result_t *tests;      /* Linked list of test results */
    xml_test_result_t *tests_tail; /* Tail pointer for efficient append */
    xml_test_suite_t *next;        /* Linked list next pointer */

    /* Allure hierarchical classification labels (set via TEST_GROUP_META) */
    xml_allure_label_t suite_labels[XML_SUITE_MAX_LABELS];
    int suite_label_count;
};

/**
 * @brief Root container for all test suites
 */
struct xml_test_suites {
    char *name;                    /* Test run name */
    char *timestamp;               /* ISO 8601 timestamp */
    uint32_t total_tests;          /* Total number of tests */
    uint32_t total_failures;       /* Total number of failures */
    uint32_t total_errors;         /* Total number of errors */
    uint32_t total_skipped;        /* Total number of skipped tests */
    double total_duration_ms;      /* Total execution time */
    xml_test_suite_t *suites;      /* Linked list of test suites */
    xml_test_suite_t *suites_tail; /* Tail pointer for efficient append */
};

/* ========== Test Result Operations ========== */

/**
 * @brief Create a new test result
 */
xml_test_result_t *xml_test_result_create(const char *name, const char *classname, const char *file,
                                          uint32_t line, double duration_ms,
                                          xml_test_status_t status);

/**
 * @brief Add a per-test Allure label override to a result
 *
 * When present, these labels replace the suite-level labels for this
 * specific test in the Allure JSON output.
 *
 * @param result Target result
 * @param name   Label name  (e.g. "layer", "epic", "feature", "story")
 * @param value  Label value
 */
void xml_test_result_add_label(xml_test_result_t *result, const char *name, const char *value);

/**
 * @brief Set failure information for a test result
 */
void xml_test_result_set_failure(xml_test_result_t *result, const char *message,
                                 const char *stack_trace);

/**
 * @brief Set skip information for a test result
 */
void xml_test_result_set_skip(xml_test_result_t *result, const char *message);

/**
 * @brief Destroy a test result and free memory
 */
void xml_test_result_destroy(xml_test_result_t *result);

/* ========== Test Suite Operations ========== */

/**
 * @brief Create a new test suite
 */
xml_test_suite_t *xml_test_suite_create(const char *name);

/**
 * @brief Add a test result to a suite
 */
void xml_test_suite_add_test(xml_test_suite_t *suite, xml_test_result_t *test);

/**
 * @brief Add an Allure classification label to a suite
 *
 * Labels are inherited by every test result in the suite when
 * write_allure_json_result() serialises them.
 *
 * @param suite  Target suite
 * @param name   Label name  (e.g. "layer", "epic", "feature", "story")
 * @param value  Label value (e.g. "unit", "FastRPC Remote API", ...)
 */
void xml_test_suite_add_label(xml_test_suite_t *suite, const char *name, const char *value);

/**
 * @brief Finalize suite statistics
 */
void xml_test_suite_finalize(xml_test_suite_t *suite);

/**
 * @brief Destroy a test suite and all its tests
 */
void xml_test_suite_destroy(xml_test_suite_t *suite);

/* ========== Test Suites Operations ========== */

/**
 * @brief Create a new test suites container
 */
xml_test_suites_t *xml_test_suites_create(const char *name);

/**
 * @brief Add a test suite to the container
 */
void xml_test_suites_add_suite(xml_test_suites_t *suites, xml_test_suite_t *suite);

/**
 * @brief Finalize overall statistics
 */
void xml_test_suites_finalize(xml_test_suites_t *suites);

/**
 * @brief Destroy test suites container and all contents
 */
void xml_test_suites_destroy(xml_test_suites_t *suites);

/* ========== Utility Functions ========== */

/**
 * @brief Get ISO 8601 formatted timestamp
 */
char *xml_get_iso_timestamp(void);

/**
 * @brief Get filename-safe timestamp
 */
char *xml_get_filename_timestamp(void);

/**
 * @brief Convert test status to string
 */
const char *xml_test_status_to_string(xml_test_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* XML_DATA_MODEL_H */
