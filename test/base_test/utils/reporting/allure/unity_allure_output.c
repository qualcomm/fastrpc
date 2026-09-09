// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "unity_allure_output.h"
#include "../../fastrpc_utils/test_utils.h"
#include "../../xml_writer/facade/xml_output_facade.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

/*
 * FASTRPC_TEST_VERSION is injected at compile time by utils/CMakeLists.txt via
 *   -DFASTRPC_TEST_VERSION="${PROJECT_VERSION}"
 * The fallback "unknown" is only reached if the macro is somehow absent.
 */
#ifndef FASTRPC_TEST_VERSION
#define FASTRPC_TEST_VERSION "unknown"
#endif

/* Internal state */
static struct {
    xml_test_writer_t *writer;
    int initialized;
    int enabled;
    int suite_open;
    char current_suite[256];
    uint32_t suite_tests;
    uint32_t suite_failures;
    uint32_t suite_skipped;
    double suite_time_ms;
    /* Composed environment name: "<device>-<fastrpc_version>".
     * Populated by unity_allure_output_set_environment(); empty until then. */
    char environment_name[256];
} allure_state_v2 = {
    .writer = NULL,
    .initialized = 0,
    .enabled = 0,
    .suite_open = 0,
    .suite_tests = 0,
    .suite_failures = 0,
    .suite_skipped = 0,
    .suite_time_ms = 0.0,
    .environment_name = { 0 },
};

int unity_allure_output_init(const char *base_name)
{
    const char *output_dir = UNITY_ALLURE_OUTPUT_DIR;
    const char *basename = base_name ? base_name : UNITY_ALLURE_OUTPUT_BASENAME;

    if (allure_state_v2.initialized) {
        return 0;
    }

    allure_state_v2.writer = xml_test_writer_create_with_name(output_dir, basename);
    if (!allure_state_v2.writer) {
        fprintf(stderr, "[unity_allure_v2] Failed to create XML writer\n");
        return -1;
    }

    if (xml_test_writer_begin_run(allure_state_v2.writer, "FastRPC Tests") < 0) {
        fprintf(stderr, "[unity_allure_v2] Failed to begin test run\n");
        xml_test_writer_destroy(allure_state_v2.writer);
        allure_state_v2.writer = NULL;
        return -1;
    }

    allure_state_v2.initialized = 1;
    allure_state_v2.enabled = 1;
    allure_state_v2.suite_open = 0;

    fprintf(stderr, "[unity_allure_v2] Initialized: %s\n",
            xml_test_writer_get_path(allure_state_v2.writer));

    return 0;
}

int unity_allure_output_close(void)
{
    if (!allure_state_v2.initialized || !allure_state_v2.writer) {
        return -1;
    }

    if (allure_state_v2.suite_open) {
        fprintf(stderr, "[unity_allure_v2] Warning: Suite still open at close, closing it\n");
        unity_allure_output_end_suite(allure_state_v2.suite_tests, allure_state_v2.suite_failures,
                                      allure_state_v2.suite_skipped, allure_state_v2.suite_time_ms);
    }

    xml_test_writer_end_run(allure_state_v2.writer);

    fprintf(stderr, "[unity_allure_v2] Results saved: %s\n",
            xml_test_writer_get_path(allure_state_v2.writer));
    fprintf(stderr, "[unity_allure_v2] Total: %u tests, %u failures, %u skipped\n",
            xml_test_writer_get_test_count(allure_state_v2.writer),
            xml_test_writer_get_failure_count(allure_state_v2.writer),
            xml_test_writer_get_skip_count(allure_state_v2.writer));

    xml_test_writer_destroy(allure_state_v2.writer);
    allure_state_v2.writer = NULL;
    allure_state_v2.initialized = 0;
    allure_state_v2.enabled = 0;

    return 0;
}

int unity_allure_output_start_suite(const char *suite_name)
{
    if (!allure_state_v2.enabled || !allure_state_v2.writer) {
        return -1;
    }

    if (allure_state_v2.suite_open) {
        fprintf(stderr,
                "[unity_allure_v2] Warning: Starting new suite while previous suite still open\n");
        unity_allure_output_end_suite(allure_state_v2.suite_tests, allure_state_v2.suite_failures,
                                      allure_state_v2.suite_skipped, allure_state_v2.suite_time_ms);
    }

    strncpy(allure_state_v2.current_suite, suite_name, sizeof(allure_state_v2.current_suite) - 1);
    allure_state_v2.current_suite[sizeof(allure_state_v2.current_suite) - 1] = '\0';

    if (xml_test_writer_begin_suite(allure_state_v2.writer, suite_name) < 0) {
        return -1;
    }

    allure_state_v2.suite_tests = 0;
    allure_state_v2.suite_failures = 0;
    allure_state_v2.suite_skipped = 0;
    allure_state_v2.suite_time_ms = 0.0;
    allure_state_v2.suite_open = 1;

    return 0;
}

int unity_allure_output_end_suite(uint32_t tests, uint32_t failures, uint32_t skipped,
                                  double time_ms)
{
    if (!allure_state_v2.enabled || !allure_state_v2.writer || !allure_state_v2.suite_open) {
        return -1;
    }

    allure_state_v2.suite_tests = tests;
    allure_state_v2.suite_failures = failures;
    allure_state_v2.suite_skipped = skipped;
    allure_state_v2.suite_time_ms = time_ms;

    if (xml_test_writer_end_suite(allure_state_v2.writer) < 0) {
        return -1;
    }

    allure_state_v2.suite_open = 0;

    return 0;
}

int unity_allure_output_write_test(const char *suite_name, const char *test_name, const char *file,
                                   uint32_t line, double time_ms, const char *status,
                                   const char *message)
{
    if (!allure_state_v2.enabled || !allure_state_v2.writer) {
        return -1;
    }

    char enriched_message[512];
    int last_err = test_utils_get_last_error_code();

    if (last_err != INT_MIN) {
        const char *err_str = test_utils_err_str(last_err);
        if (message && message[0] != '\0') {
            snprintf(enriched_message, sizeof(enriched_message), "[error_code: 0x%08x (%s)] %s",
                     (unsigned int)last_err, err_str, message);
        } else {
            snprintf(enriched_message, sizeof(enriched_message), "[error_code: 0x%08x (%s)]",
                     (unsigned int)last_err, err_str);
        }
        message = enriched_message;
    }

    if (!allure_state_v2.suite_open) {
        fprintf(stderr,
                "[unity_allure_v2] Warning: Writing test without open suite, opening one\n");
        unity_allure_output_start_suite(suite_name ? suite_name : "Unknown");
    }

    allure_state_v2.suite_tests++;
    allure_state_v2.suite_time_ms += time_ms;

    int result = 0;
    if (strcmp(status, "PASS") == 0) {
        result = xml_test_writer_record_pass(allure_state_v2.writer, test_name, time_ms);
    } else if (strcmp(status, "FAIL") == 0) {
        allure_state_v2.suite_failures++;
        result = xml_test_writer_record_failure(allure_state_v2.writer, test_name, file, line,
                                                message, time_ms);
    } else if (strcmp(status, "IGNORE") == 0) {
        allure_state_v2.suite_skipped++;
        result = xml_test_writer_record_skip(allure_state_v2.writer, test_name, message);
    } else {
        result = xml_test_writer_record_error(allure_state_v2.writer, test_name, file, line,
                                              message, time_ms);
    }

    return result;
}

void unity_allure_output_flush(void)
{
    if (allure_state_v2.writer) {
        xml_test_writer_flush(allure_state_v2.writer);
    }
}

int unity_allure_output_is_enabled(void) { return allure_state_v2.enabled; }

const char *unity_allure_output_get_path(void)
{
    if (allure_state_v2.initialized && allure_state_v2.writer) {
        return xml_test_writer_get_path(allure_state_v2.writer);
    }
    return NULL;
}

int unity_allure_output_add_suite_label(const char *name, const char *value)
{
    if (!allure_state_v2.enabled || !allure_state_v2.writer)
        return -1;
    return xml_test_writer_add_suite_label(allure_state_v2.writer, name, value);
}

int unity_allure_output_add_last_result_label(const char *name, const char *value)
{
    if (!allure_state_v2.enabled || !allure_state_v2.writer)
        return -1;
    return xml_test_writer_add_last_result_label(allure_state_v2.writer, name, value);
}

int unity_allure_output_attach_file(const char *file_path, const char *attachment_name,
                                    const char *attachment_type)
{
    if (!allure_state_v2.enabled || !allure_state_v2.writer) {
        return -1;
    }

    return xml_test_writer_attach_file(allure_state_v2.writer, file_path, attachment_name,
                                       attachment_type);
}

/* ---------------------------------------------------------------------------
 * get_device_name
 *
 * Retrieves the device hostname at runtime by running `hostname` first,
 * falling back to `uname -n` if `hostname` is unavailable.  Both commands
 * are tried in a single popen() call using shell short-circuit evaluation
 * so only one child process is spawned.
 *
 * The result is stripped of any trailing newline.  If neither command
 * succeeds (or popen itself fails), the buffer is set to "unknown-device".
 * ---------------------------------------------------------------------------*/
static void get_device_name(char *buf, size_t size)
{
    buf[0] = '\0';
    FILE *fp = popen("hostname 2>/dev/null || uname -n 2>/dev/null", "r");
    if (fp) {
        if (fgets(buf, (int)size, fp) != NULL) {
            /* Strip trailing newline left by fgets */
            size_t len = strlen(buf);
            if (len > 0 && buf[len - 1] == '\n')
                buf[len - 1] = '\0';
        }
        pclose(fp);
    }
    if (buf[0] == '\0')
        snprintf(buf, size, "unknown-device");
}

int unity_allure_output_set_environment(const char *device_name, const char *fastrpc_version)
{
    if (!allure_state_v2.initialized || !allure_state_v2.writer)
        return -1;

    /* ---- Resolve device name -------------------------------------------- */
    char dev[128] = { 0 };
    if (device_name && device_name[0] != '\0') {
        strncpy(dev, device_name, sizeof(dev) - 1);
    } else {
        get_device_name(dev, sizeof(dev));
    }

    /* ---- Resolve FastRPC version ----------------------------------------- */
    const char *ver
        = (fastrpc_version && fastrpc_version[0] != '\0') ? fastrpc_version : FASTRPC_TEST_VERSION;

    /* ---- Compose environment name: "<device>-<version>" ------------------ */
    snprintf(allure_state_v2.environment_name, sizeof(allure_state_v2.environment_name), "%s-%s",
             dev, ver);

    /* ---- Write environment.properties ------------------------------------ *
     *
     * This file is placed in the results directory alongside the JSON result
     * files.  It serves two purposes:
     *
     *   1. Allure Report 3 (and Allure 2) display its key-value pairs in the
     *      report's Metadata / Environment section automatically.
     *
     *   2. generate_report.sh reads the "Environment" key from this file and
     *      passes --environment=<value> to `allure generate`, which pins the
     *      entire run to the correct environment in the Allure 3 Environments
     *      feature without requiring label-based matching.
     */
    char props_path[UNITY_ALLURE_MAX_PATH];
    snprintf(props_path, sizeof(props_path), "%s/environment.properties", UNITY_ALLURE_OUTPUT_DIR);

    FILE *f = fopen(props_path, "w");
    if (!f) {
        fprintf(stderr, "[unity_allure_v2] Warning: cannot write %s: %s\n", props_path,
                strerror(errno));
        /* Non-fatal: the "host" label in each JSON result still enables
         * Allure 3 environment matching even without this file. */
    } else {
        /* Two clean keys for human-readable display in the Metadata section.
         * generate_report.sh reads these to compose the environment name. */
        fprintf(f, "Device=%s\n", dev);
        fprintf(f, "FastRPC.Version=%s\n", ver);
        fclose(f);
        fprintf(stderr, "[unity_allure_v2] Wrote %s\n", props_path);
    }

    /* ---- Propagate to the XML writer ------------------------------------- *
     * xml_test_writer_set_environment() stores the name in the writer struct
     * so that write_allure_json_result() can append a
     *   {"name": "host", "value": "<env_name>"}
     * label to every result JSON it produces.
     */
    xml_test_writer_set_environment(allure_state_v2.writer, allure_state_v2.environment_name);

    fprintf(stderr, "[unity_allure_v2] Environment: %s\n", allure_state_v2.environment_name);
    return 0;
}

void unity_allure_output_begin_test(void)
{
    if (!allure_state_v2.initialized || !allure_state_v2.writer)
        return;
    xml_test_writer_begin_test(allure_state_v2.writer);
}
