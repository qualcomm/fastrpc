// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "xml_data_model.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

xml_test_result_t *xml_test_result_create(const char *name, const char *classname, const char *file,
                                          uint32_t line, double duration_ms,
                                          xml_test_status_t status)
{
    xml_test_result_t *result = calloc(1, sizeof(xml_test_result_t));
    if (!result)
        return NULL;

    result->name = name ? strdup(name) : NULL;
    result->classname = classname ? strdup(classname) : NULL;
    result->file = file ? strdup(file) : NULL;
    result->line = line;
    result->duration_ms = duration_ms;
    result->status = status;
    result->timestamp = time(NULL);

    /* Calculate start_time_ms as current time in milliseconds */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    result->start_time_ms = (double)(tv.tv_sec) * 1000.0 + (double)(tv.tv_usec) / 1000.0;

    result->message = NULL;
    result->stack_trace = NULL;
    result->next = NULL;
    result->result_label_count = 0;
    memset(result->result_labels, 0, sizeof(result->result_labels));

    return result;
}

void xml_test_result_add_label(xml_test_result_t *result, const char *name, const char *value)
{
    if (!result || !name || !value)
        return;
    if (result->result_label_count >= XML_SUITE_MAX_LABELS)
        return;

    xml_allure_label_t *lbl = &result->result_labels[result->result_label_count++];
    strncpy(lbl->name, name, sizeof(lbl->name) - 1);
    strncpy(lbl->value, value, sizeof(lbl->value) - 1);
    lbl->name[sizeof(lbl->name) - 1] = '\0';
    lbl->value[sizeof(lbl->value) - 1] = '\0';
}

void xml_test_result_set_failure(xml_test_result_t *result, const char *message,
                                 const char *stack_trace)
{
    if (!result)
        return;

    result->status = XML_TEST_STATUS_FAIL;

    if (result->message) {
        free(result->message);
    }
    result->message = message ? strdup(message) : NULL;

    if (result->stack_trace) {
        free(result->stack_trace);
    }
    result->stack_trace = stack_trace ? strdup(stack_trace) : NULL;
}

void xml_test_result_set_skip(xml_test_result_t *result, const char *message)
{
    if (!result)
        return;

    result->status = XML_TEST_STATUS_SKIP;

    if (result->message) {
        free(result->message);
    }
    result->message = message ? strdup(message) : NULL;
}

void xml_test_result_destroy(xml_test_result_t *result)
{
    if (!result)
        return;

    free(result->name);
    free(result->classname);
    free(result->file);
    free(result->message);
    free(result->stack_trace);
    free(result);
}

xml_test_suite_t *xml_test_suite_create(const char *name)
{
    xml_test_suite_t *suite = calloc(1, sizeof(xml_test_suite_t));
    if (!suite)
        return NULL;

    suite->name = name ? strdup(name) : NULL;
    suite->timestamp = xml_get_iso_timestamp();
    suite->test_count = 0;
    suite->failure_count = 0;
    suite->error_count = 0;
    suite->skipped_count = 0;
    suite->total_duration_ms = 0.0;
    suite->tests = NULL;
    suite->tests_tail = NULL;
    suite->next = NULL;
    suite->suite_label_count = 0;
    memset(suite->suite_labels, 0, sizeof(suite->suite_labels));

    return suite;
}

void xml_test_suite_add_test(xml_test_suite_t *suite, xml_test_result_t *test)
{
    if (!suite || !test)
        return;

    if (suite->tests_tail) {
        suite->tests_tail->next = test;
        suite->tests_tail = test;
    } else {
        suite->tests = test;
        suite->tests_tail = test;
    }

    suite->test_count++;
    suite->total_duration_ms += test->duration_ms;

    switch (test->status) {
    case XML_TEST_STATUS_FAIL:
        suite->failure_count++;
        break;
    case XML_TEST_STATUS_ERROR:
        suite->error_count++;
        break;
    case XML_TEST_STATUS_SKIP:
        suite->skipped_count++;
        break;
    default:
        break;
    }
}

void xml_test_suite_finalize(xml_test_suite_t *suite)
{
    if (!suite)
        return;

    /* This function exists for future extensibility */
}

void xml_test_suite_add_label(xml_test_suite_t *suite, const char *name, const char *value)
{
    if (!suite || !name || !value)
        return;
    if (suite->suite_label_count >= XML_SUITE_MAX_LABELS)
        return;

    xml_allure_label_t *lbl = &suite->suite_labels[suite->suite_label_count++];
    strncpy(lbl->name, name, sizeof(lbl->name) - 1);
    strncpy(lbl->value, value, sizeof(lbl->value) - 1);
    lbl->name[sizeof(lbl->name) - 1] = '\0';
    lbl->value[sizeof(lbl->value) - 1] = '\0';
}

void xml_test_suite_destroy(xml_test_suite_t *suite)
{
    if (!suite)
        return;

    xml_test_result_t *test = suite->tests;
    while (test) {
        xml_test_result_t *next = test->next;
        xml_test_result_destroy(test);
        test = next;
    }

    free(suite->name);
    free(suite->timestamp);
    free(suite);
}

xml_test_suites_t *xml_test_suites_create(const char *name)
{
    xml_test_suites_t *suites = calloc(1, sizeof(xml_test_suites_t));
    if (!suites)
        return NULL;

    suites->name = name ? strdup(name) : strdup("Test Results");
    suites->timestamp = xml_get_iso_timestamp();
    suites->total_tests = 0;
    suites->total_failures = 0;
    suites->total_errors = 0;
    suites->total_skipped = 0;
    suites->total_duration_ms = 0.0;
    suites->suites = NULL;
    suites->suites_tail = NULL;

    return suites;
}

void xml_test_suites_add_suite(xml_test_suites_t *suites, xml_test_suite_t *suite)
{
    if (!suites || !suite)
        return;

    /* Append to linked list */
    if (suites->suites_tail) {
        suites->suites_tail->next = suite;
        suites->suites_tail = suite;
    } else {
        suites->suites = suite;
        suites->suites_tail = suite;
    }

    /* Update statistics */
    suites->total_tests += suite->test_count;
    suites->total_failures += suite->failure_count;
    suites->total_errors += suite->error_count;
    suites->total_skipped += suite->skipped_count;
    suites->total_duration_ms += suite->total_duration_ms;
}

void xml_test_suites_finalize(xml_test_suites_t *suites)
{
    if (!suites)
        return;

    /* Statistics are updated incrementally, nothing to do here */
}

void xml_test_suites_destroy(xml_test_suites_t *suites)
{
    if (!suites)
        return;

    xml_test_suite_t *suite = suites->suites;
    while (suite) {
        xml_test_suite_t *next = suite->next;
        xml_test_suite_destroy(suite);
        suite = next;
    }

    free(suites->name);
    free(suites->timestamp);
    free(suites);
}

/* ========== Utility Functions ========== */

char *xml_get_iso_timestamp(void)
{
    time_t now = time(NULL);
    struct tm *tm_info = gmtime(&now);

    char *buffer = malloc(32);
    if (!buffer)
        return NULL;

    strftime(buffer, 32, "%Y-%m-%dT%H:%M:%SZ", tm_info);
    return buffer;
}

char *xml_get_filename_timestamp(void)
{
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);

    char *buffer = malloc(32);
    if (!buffer)
        return NULL;

    strftime(buffer, 32, "%Y%m%d_%H%M%S", tm_info);
    return buffer;
}

const char *xml_test_status_to_string(xml_test_status_t status)
{
    switch (status) {
    case XML_TEST_STATUS_PASS:
        return "PASS";
    case XML_TEST_STATUS_FAIL:
        return "FAIL";
    case XML_TEST_STATUS_SKIP:
        return "SKIP";
    case XML_TEST_STATUS_ERROR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}
