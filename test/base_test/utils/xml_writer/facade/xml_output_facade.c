// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "xml_output_facade.h"
#include "../config/xml_config.h"
#include "../core/xml_data_model.h"
#include "../error/xml_error_handler.h"
#include "../logging/xml_logger.h"
#include "../streaming/xml_stream_writer.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/* Maximum number of attachments that can be queued before a test result
 * is recorded.  DefaultSuite calls profiling_attach_result() up to
 * 15 times (noop + 7 sizes × 2 directions), each producing 2 files = 30.
 * Round up to the next power of two with headroom. */
#define XML_PENDING_ATTACH_MAX 64

typedef struct {
    char file_path[512];
    char name[256];
    char type[64];
} xml_pending_attachment_t;

/**
 * @brief Internal writer structure
 */
struct xml_test_writer {
    /* Configuration */
    xml_config_t *config;

    /* Output */
    char *output_path;
    FILE *output_file;
    xml_stream_writer_t *stream_writer;

    /* Data model */
    xml_test_suites_t *test_suites;
    xml_test_suite_t *current_suite;

    /* State */
    bool run_started;
    bool suite_started;

    /* Statistics */
    uint32_t total_tests;
    uint32_t total_failures;
    uint32_t total_errors;
    uint32_t total_skipped;

    /* Error handling */
    xml_error_handler_t *error_handler;
    xml_logger_t *logger;

    /* Environment name injected as a "host" label into every result JSON.
     * Set via xml_test_writer_set_environment(); empty string means unset. */
    char environment_name[256];

    /* Pending attachments queued during a test body execution.
     * xml_test_writer_begin_test() sets in_test_body=1 to open the window.
     * xml_test_writer_attach_file() enqueues here while in_test_body==1.
     * flush_pending_attachments() drains the queue onto the freshly-recorded
     * result inside every record_pass/failure/skip/error call, then clears
     * in_test_body so post-test attachments (log capture) go directly. */
    xml_pending_attachment_t pending_attachments[XML_PENDING_ATTACH_MAX];
    int pending_attachment_count;
    int in_test_body; /* 1 while test body is executing, 0 otherwise */
};

static int create_directory_recursive(const char *path)
{
    char tmp[512];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);

    if (tmp[len - 1] == '/') {
        tmp[len - 1] = 0;
    }

    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }

    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        return -1;
    }

    return 0;
}

static char *generate_output_path(const char *output_dir, const char *base_name)
{
    char *timestamp = xml_get_filename_timestamp();
    if (!timestamp)
        return NULL;

    size_t path_len = strlen(output_dir) + strlen(base_name) + strlen(timestamp) + 16;
    char *path = malloc(path_len);
    if (!path) {
        free(timestamp);
        return NULL;
    }

    snprintf(path, path_len, "%s/%s_%s.xml", output_dir, base_name, timestamp);
    free(timestamp);

    return path;
}

/* Flush any pending attachments onto the last recorded test result.
 * Called at the end of every xml_test_writer_record_*() function.
 * Also clears in_test_body so subsequent attach_file() calls (e.g. from
 * log_capture_stop()) go directly onto the recorded result. */
static void flush_pending_attachments(xml_test_writer_t *writer)
{
    if (!writer)
        return;
    /* Always clear the test-body gate so post-record attach_file() calls
     * (log capture) append directly to the freshly-recorded result. */
    writer->in_test_body = 0;

    if (writer->pending_attachment_count == 0)
        return;
    if (!writer->current_suite)
        return;

    xml_test_result_t *last_test = writer->current_suite->tests_tail;
    if (!last_test)
        return;

    int i;
    for (i = 0; i < writer->pending_attachment_count; i++) {
        xml_pending_attachment_t *pa = &writer->pending_attachments[i];
        char attachment_info[1024];
        snprintf(attachment_info, sizeof(attachment_info), "\n[[ATTACHMENT:%s|%s|%s]]",
                 pa->file_path, pa->name, pa->type);

        if (last_test->message) {
            size_t new_len = strlen(last_test->message) + strlen(attachment_info) + 1;
            char *new_message = realloc(last_test->message, new_len);
            if (new_message) {
                strcat(new_message, attachment_info);
                last_test->message = new_message;
            }
        } else {
            last_test->message = strdup(attachment_info);
        }
    }
    writer->pending_attachment_count = 0;
}

/* Helper to escape JSON string */
static void write_json_escaped_string(FILE *f, const char *str)
{
    if (!str) {
        fprintf(f, "\"\"");
        return;
    }

    fprintf(f, "\"");
    for (const char *p = str; *p; p++) {
        switch (*p) {
        case '"':
            fprintf(f, "\\\"");
            break;
        case '\\':
            fprintf(f, "\\\\");
            break;
        case '\n':
            fprintf(f, "\\n");
            break;
        case '\r':
            fprintf(f, "\\r");
            break;
        case '\t':
            fprintf(f, "\\t");
            break;
        case '\b':
            fprintf(f, "\\b");
            break;
        case '\f':
            fprintf(f, "\\f");
            break;
        default:
            if ((unsigned char)*p < 0x20) {
                /* Control characters - escape as \uXXXX */
                fprintf(f, "\\u%04x", (unsigned char)*p);
            } else {
                fputc(*p, f);
            }
            break;
        }
    }
    fprintf(f, "\"");
}

/* Helper to generate UUID */
static void generate_uuid(char *buffer, size_t size)
{
    snprintf(buffer, size, "%08x-%04x-%04x-%04x-%012lx", rand(), rand() & 0xFFFF, rand() & 0xFFFF,
             rand() & 0xFFFF, (unsigned long)rand() << 32 | rand());
}

/* Helper to generate MD5-like hash for historyId */
static void generate_history_id(const char *suite_name, const char *test_name, char *buffer,
                                size_t size)
{
    unsigned long hash = 5381;
    const char *str = suite_name;
    while (*str)
        hash = ((hash << 5) + hash) + *str++;
    str = test_name;
    while (*str)
        hash = ((hash << 5) + hash) + *str++;
    snprintf(buffer, size, "%016lx%016lx", hash, hash ^ 0xDEADBEEF);
}

/* Write a single test result as Allure 2.x JSON */
static void write_allure_json_result(xml_test_writer_t *writer, xml_test_suite_t *suite,
                                     xml_test_result_t *test, const char *output_dir)
{
    char uuid[64];
    char history_id[64];
    char filename[512];

    /* Generate UUID and historyId */
    generate_uuid(uuid, sizeof(uuid));
    generate_history_id(suite->name, test->name, history_id, sizeof(history_id));

    /* Create output file */
    snprintf(filename, sizeof(filename), "%s/%s-result.json", output_dir, uuid);
    FILE *f = fopen(filename, "w");
    if (!f) {
        XML_LOG_ERROR(writer->logger, "allure_json", "Failed to create file: %s", filename);
        return;
    }

    /* Write JSON */
    fprintf(f, "{\n");
    fprintf(f, "  \"uuid\": \"%s\",\n", uuid);
    fprintf(f, "  \"historyId\": \"%s\",\n", history_id);
    fprintf(f, "  \"testCaseId\": \"%s\",\n", history_id);
    fprintf(f, "  \"fullName\": \"%s.%s\",\n", suite->name, test->name);
    fprintf(f, "  \"name\": \"%s\",\n", test->name);

    /* Labels — per-test overrides take precedence over suite-level labels.
     * If TEST_LABELS() was called in the test body, result_label_count > 0
     * and those labels replace the suite classification labels entirely.
     * The fixed framework/language/suite/testClass labels are always emitted. */
    fprintf(f, "  \"labels\": [\n");
    fprintf(f, "    {\"name\": \"suite\",     \"value\": \"%s\"},\n", suite->name);
    fprintf(f, "    {\"name\": \"testClass\", \"value\": \"%s\"},\n", test->classname);
    fprintf(f, "    {\"name\": \"package\",   \"value\": \"%s\"},\n", suite->name);
    fprintf(f, "    {\"name\": \"framework\", \"value\": \"unity\"},\n");
    fprintf(f, "    {\"name\": \"language\",  \"value\": \"c\"}");

    /* Environment label — present when set_environment() was called.
     * Allure Report 3 uses this "host" label for its label-based
     * environment matching configured in allurerc.mjs. */
    if (writer->environment_name[0] != '\0') {
        fprintf(f, ",\n    {\"name\": \"host\", \"value\": \"%s\"}", writer->environment_name);
    }

    if (test->result_label_count > 0) {
        /* Per-test override: emit result-level labels (from TEST_LABELS) */
        for (int i = 0; i < test->result_label_count; i++) {
            fprintf(f, ",\n    {\"name\": \"%s\", \"value\": \"%s\"}", test->result_labels[i].name,
                    test->result_labels[i].value);
        }
    } else {
        /* Suite-level classification labels (from TEST_GROUP_META) */
        for (int i = 0; i < suite->suite_label_count; i++) {
            fprintf(f, ",\n    {\"name\": \"%s\", \"value\": \"%s\"}", suite->suite_labels[i].name,
                    suite->suite_labels[i].value);
        }
    }
    fprintf(f, "\n  ],\n");

    /* Status */
    const char *status;
    switch (test->status) {
    case XML_TEST_STATUS_PASS:
        status = "passed";
        break;
    case XML_TEST_STATUS_FAIL:
        status = "failed";
        break;
    case XML_TEST_STATUS_ERROR:
        status = "broken";
        break;
    case XML_TEST_STATUS_SKIP:
        status = "skipped";
        break;
    default:
        status = "unknown";
        break;
    }
    fprintf(f, "  \"status\": \"%s\",\n", status);

    /* Status details for failures */
    if (test->status == XML_TEST_STATUS_FAIL || test->status == XML_TEST_STATUS_ERROR) {
        fprintf(f, "  \"statusDetails\": {\n");

        /* Extract message without attachments */
        const char *msg = test->message;
        char *clean_msg = NULL;
        if (msg && strstr(msg, "[[ATTACHMENT:")) {
            const char *attach_start = strstr(msg, "[[ATTACHMENT:");
            size_t msg_len = attach_start - msg;
            clean_msg = malloc(msg_len + 1);
            if (clean_msg) {
                strncpy(clean_msg, msg, msg_len);
                clean_msg[msg_len] = '\0';
                msg = clean_msg;
            }
        }

        /* Write message with proper JSON escaping */
        if (msg && strlen(msg) > 0) {
            fprintf(f, "    \"message\": ");
            write_json_escaped_string(f, msg);
            fprintf(f, ",\n");
        }

        /* Write trace with proper JSON escaping */
        fprintf(f, "    \"trace\": ");
        char trace_buf[2048];
        snprintf(trace_buf, sizeof(trace_buf), "File: %s:%u\n%s", test->file, test->line,
                 msg ? msg : "");
        write_json_escaped_string(f, trace_buf);
        fprintf(f, "\n  },\n");

        if (clean_msg)
            free(clean_msg);
    }

    /* Timestamps (convert from ms to Unix timestamp in ms) */
    long long start_time = (long long)(test->start_time_ms);
    long long stop_time = start_time + (long long)(test->duration_ms);
    fprintf(f, "  \"start\": %lld,\n", start_time);
    fprintf(f, "  \"stop\": %lld,\n", stop_time);

    /* Attachments */
    if (test->message && strstr(test->message, "[[ATTACHMENT:")) {
        fprintf(f, "  \"attachments\": [\n");

        const char *attach_start = strstr(test->message, "[[ATTACHMENT:");
        int first = 1;
        while (attach_start) {
            const char *attach_end = strstr(attach_start, "]]");
            if (attach_end) {
                const char *path_start = attach_start + 13;
                const char *pipe1 = strchr(path_start, '|');

                if (pipe1 && pipe1 < attach_end) {
                    char file_path[1024];
                    size_t path_len = pipe1 - path_start;
                    if (path_len < sizeof(file_path)) {
                        strncpy(file_path, path_start, path_len);
                        file_path[path_len] = '\0';

                        const char *pipe2 = strchr(pipe1 + 1, '|');
                        char attach_name[256];
                        char attach_type[64] = "text/plain";

                        if (pipe2 && pipe2 < attach_end) {
                            size_t name_len = pipe2 - (pipe1 + 1);
                            if (name_len < sizeof(attach_name)) {
                                strncpy(attach_name, pipe1 + 1, name_len);
                                attach_name[name_len] = '\0';
                            }

                            size_t type_len = attach_end - (pipe2 + 1);
                            if (type_len < sizeof(attach_type)) {
                                strncpy(attach_type, pipe2 + 1, type_len);
                                attach_type[type_len] = '\0';
                            }
                        } else {
                            strncpy(attach_name, file_path, sizeof(attach_name) - 1);
                        }

                        if (!first)
                            fprintf(f, ",\n");
                        first = 0;

                        /* Allure resolves "source" relative to the results
                         * directory passed to `allure generate`.  Absolute
                         * on-device paths are meaningless on the host.
                         *
                         * Strip the output_dir prefix so that:
                         *   <output_dir>/attachments/foo.json -> attachments/foo.json
                         *   <output_dir>/foo.log             -> foo.log
                         * Fall back to bare basename if the path does not
                         * start with output_dir (e.g. already relative). */
                        const char *rel_source = file_path;
                        size_t dir_len = strlen(output_dir);
                        if (strncmp(file_path, output_dir, dir_len) == 0
                            && file_path[dir_len] == '/') {
                            rel_source = file_path + dir_len + 1;
                        } else {
                            const char *slash = strrchr(file_path, '/');
                            if (slash)
                                rel_source = slash + 1;
                        }

                        fprintf(f, "    {\n");
                        fprintf(f, "      \"name\": \"%s\",\n", attach_name);
                        fprintf(f, "      \"source\": \"%s\",\n", rel_source);
                        fprintf(f, "      \"type\": \"%s\"\n", attach_type);
                        fprintf(f, "    }");
                    }
                }

                attach_start = strstr(attach_end + 2, "[[ATTACHMENT:");
            } else {
                break;
            }
        }

        fprintf(f, "\n  ]\n");
    } else {
        fprintf(f, "  \"attachments\": []\n");
    }

    fprintf(f, "}\n");
    fclose(f);

    XML_LOG_DEBUG(writer->logger, "allure_json", "Created result file: %s", filename);
}

static void write_allure_xml(xml_test_writer_t *writer)
{
    if (!writer || !writer->test_suites)
        return;

    xml_test_suites_t *suites = writer->test_suites;

    /* Extract output directory from writer's output path */
    char output_dir[512];
    strncpy(output_dir, writer->output_path, sizeof(output_dir) - 1);
    output_dir[sizeof(output_dir) - 1] = '\0';
    char *last_slash = strrchr(output_dir, '/');
    if (last_slash)
        *last_slash = '\0';

    /* Seed random for UUID generation */
    srand(time(NULL));

    /* Write each test as a separate JSON file */
    xml_test_suite_t *suite = suites->suites;
    while (suite) {
        xml_test_result_t *test = suite->tests;
        while (test) {
            write_allure_json_result(writer, suite, test, output_dir);
            test = test->next;
        }
        suite = suite->next;
    }

    /* Also write the old XML format for backward compatibility */
    if (!writer->stream_writer)
        return;

    xml_stream_writer_t *w = writer->stream_writer;

    /* Start document */
    xml_stream_writer_start_document(w);

    /* Write test-suite element (legacy format) */
    xml_stream_writer_start_element(w, "test-suite");
    xml_stream_writer_write_attribute(w, "name", suites->name);
    xml_stream_writer_write_attribute(w, "start", suites->timestamp);

    /* Write each test suite as a nested test-suite */
    xml_test_suite_t *xml_suite = suites->suites;
    while (xml_suite) {
        /* Each test case in Allure */
        xml_test_result_t *xml_test = xml_suite->tests;
        while (xml_test) {
            xml_stream_writer_start_element(w, "test-case");
            xml_stream_writer_write_attribute(w, "name", xml_test->name);

            /* Map status */
            const char *allure_status;
            switch (xml_test->status) {
            case XML_TEST_STATUS_PASS:
                allure_status = "passed";
                break;
            case XML_TEST_STATUS_FAIL:
                allure_status = "failed";
                break;
            case XML_TEST_STATUS_ERROR:
                allure_status = "broken";
                break;
            case XML_TEST_STATUS_SKIP:
                allure_status = "skipped";
                break;
            default:
                allure_status = "unknown";
                break;
            }
            xml_stream_writer_write_attribute(w, "status", allure_status);

            /* Write time in milliseconds */
            char time_str[32];
            snprintf(time_str, sizeof(time_str), "%.0f", xml_test->duration_ms);
            xml_stream_writer_write_attribute(w, "time", time_str);

            /* Write labels */
            xml_stream_writer_start_element(w, "labels");

            xml_stream_writer_start_element(w, "label");
            xml_stream_writer_write_attribute(w, "name", "suite");
            xml_stream_writer_write_attribute(w, "value", xml_suite->name);
            xml_stream_writer_end_element(w); /* label */

            xml_stream_writer_start_element(w, "label");
            xml_stream_writer_write_attribute(w, "name", "testClass");
            xml_stream_writer_write_attribute(w, "value", xml_test->classname);
            xml_stream_writer_end_element(w); /* label */

            xml_stream_writer_end_element(w); /* labels */

            /* Write failure/error message if present */
            if (xml_test->status == XML_TEST_STATUS_FAIL
                || xml_test->status == XML_TEST_STATUS_ERROR) {
                xml_stream_writer_start_element(w, "failure");

                /* Extract actual message without attachment info */
                const char *msg = xml_test->message;
                char *clean_msg = NULL;
                if (msg && strstr(msg, "[[ATTACHMENT:")) {
                    const char *attach_start = strstr(msg, "[[ATTACHMENT:");
                    size_t msg_len = attach_start - msg;
                    clean_msg = malloc(msg_len + 1);
                    if (clean_msg) {
                        strncpy(clean_msg, msg, msg_len);
                        clean_msg[msg_len] = '\0';
                        msg = clean_msg;
                    }
                }

                xml_stream_writer_start_element(w, "message");
                xml_stream_writer_write_text(w, msg ? msg : "Test failed");
                xml_stream_writer_end_element(w); /* message */

                xml_stream_writer_start_element(w, "stack-trace");
                char details[1024];
                snprintf(details, sizeof(details), "File: %s:%u\n%s", xml_test->file,
                         xml_test->line, msg ? msg : "");
                xml_stream_writer_write_text(w, details);
                xml_stream_writer_end_element(w); /* stack-trace */

                xml_stream_writer_end_element(w); /* failure */

                if (clean_msg)
                    free(clean_msg);
            }

            /* Write attachments */
            if (xml_test->message && strstr(xml_test->message, "[[ATTACHMENT:")) {
                xml_stream_writer_start_element(w, "attachments");

                /* Extract and write all attachments */
                const char *attach_start = strstr(xml_test->message, "[[ATTACHMENT:");
                while (attach_start) {
                    const char *attach_end = strstr(attach_start, "]]");
                    if (attach_end) {
                        /* Parse attachment: [[ATTACHMENT:path|name|type]] */
                        const char *path_start = attach_start + 13; /* Skip "[[ATTACHMENT:" */
                        const char *pipe1 = strchr(path_start, '|');

                        if (pipe1 && pipe1 < attach_end) {
                            /* Extract file path */
                            size_t path_len = pipe1 - path_start;
                            char file_path[1024];
                            if (path_len < sizeof(file_path)) {
                                strncpy(file_path, path_start, path_len);
                                file_path[path_len] = '\0';

                                /* Extract name */
                                const char *pipe2 = strchr(pipe1 + 1, '|');
                                char attach_name[256];
                                if (pipe2 && pipe2 < attach_end) {
                                    size_t name_len = pipe2 - (pipe1 + 1);
                                    if (name_len < sizeof(attach_name)) {
                                        strncpy(attach_name, pipe1 + 1, name_len);
                                        attach_name[name_len] = '\0';
                                    } else {
                                        strncpy(attach_name, file_path, sizeof(attach_name) - 1);
                                    }

                                    /* Extract type */
                                    const char *type_start = pipe2 + 1;
                                    size_t type_len = attach_end - type_start;
                                    char attach_type[64];
                                    if (type_len < sizeof(attach_type)) {
                                        strncpy(attach_type, type_start, type_len);
                                        attach_type[type_len] = '\0';
                                    } else {
                                        strcpy(attach_type, "text/plain");
                                    }

                                    /* Write attachment element */
                                    xml_stream_writer_start_element(w, "attachment");
                                    xml_stream_writer_write_attribute(w, "title", attach_name);
                                    xml_stream_writer_write_attribute(w, "source", file_path);
                                    xml_stream_writer_write_attribute(w, "type", attach_type);
                                    xml_stream_writer_end_element(w); /* attachment */
                                }
                            }
                        }

                        /* Look for next attachment */
                        attach_start = strstr(attach_end + 2, "[[ATTACHMENT:");
                    } else {
                        break;
                    }
                }

                xml_stream_writer_end_element(w); /* attachments */
            }

            xml_stream_writer_end_element(w); /* test-case */
            xml_test = xml_test->next;
        }

        xml_suite = xml_suite->next;
    }

    xml_stream_writer_end_element(w); /* test-suite */
    xml_stream_writer_end_document(w);
}

xml_test_writer_t *xml_test_writer_create(const char *output_path)
{
    if (!output_path)
        return NULL;

    xml_test_writer_t *writer = calloc(1, sizeof(xml_test_writer_t));
    if (!writer)
        return NULL;

    writer->config = xml_config_load_from_env();
    if (!writer->config) {
        free(writer);
        return NULL;
    }

    writer->error_handler = xml_error_handler_create();
    writer->logger = xml_logger_create(stderr, writer->config->log_level);

    writer->output_path = strdup(output_path);

    writer->run_started = false;
    writer->suite_started = false;
    writer->total_tests = 0;
    writer->total_failures = 0;
    writer->total_errors = 0;
    writer->total_skipped = 0;

    XML_LOG_INFO(writer->logger, "xml_writer", "Created XML test writer: %s", output_path);

    return writer;
}

xml_test_writer_t *xml_test_writer_create_with_name(const char *output_dir, const char *base_name)
{
    if (!output_dir || !base_name)
        return NULL;

    /* Create output directory */
    if (create_directory_recursive(output_dir) != 0) {
        return NULL;
    }

    /* Generate output path */
    char *output_path = generate_output_path(output_dir, base_name);
    if (!output_path)
        return NULL;

    xml_test_writer_t *writer = xml_test_writer_create(output_path);
    free(output_path);

    return writer;
}

void xml_test_writer_destroy(xml_test_writer_t *writer)
{
    if (!writer)
        return;

    /* End run if still active */
    if (writer->run_started) {
        xml_test_writer_end_run(writer);
    }

    if (writer->stream_writer) {
        xml_stream_writer_destroy(writer->stream_writer);
    }

    if (writer->output_file) {
        fclose(writer->output_file);
    }

    if (writer->test_suites) {
        xml_test_suites_destroy(writer->test_suites);
    }

    xml_config_destroy(writer->config);
    xml_error_handler_destroy(writer->error_handler);
    xml_logger_destroy(writer->logger);
    free(writer->output_path);
    free(writer);
}

int xml_test_writer_begin_run(xml_test_writer_t *writer, const char *name)
{
    if (!writer || writer->run_started)
        return -1;

    XML_LOG_INFO(writer->logger, "xml_writer", "Beginning test run: %s", name);

    writer->test_suites = xml_test_suites_create(name);
    if (!writer->test_suites) {
        XML_LOG_ERROR(writer->logger, "xml_writer", "Failed to create test suites");
        return -1;
    }

    writer->output_file = fopen(writer->output_path, "w");
    if (!writer->output_file) {
        XML_LOG_ERROR(writer->logger, "xml_writer", "Failed to open output file: %s",
                      writer->output_path);
        return -1;
    }

    if (writer->config->pretty_print) {
        writer->stream_writer = xml_stream_writer_create_pretty(
            writer->output_file, writer->config->stream_buffer_size, writer->config->indent_size);
    } else {
        writer->stream_writer
            = xml_stream_writer_create(writer->output_file, writer->config->stream_buffer_size);
    }

    if (!writer->stream_writer) {
        XML_LOG_ERROR(writer->logger, "xml_writer", "Failed to create stream writer");
        fclose(writer->output_file);
        writer->output_file = NULL;
        return -1;
    }

    writer->run_started = true;
    return 0;
}

int xml_test_writer_end_run(xml_test_writer_t *writer)
{
    if (!writer || !writer->run_started)
        return -1;

    if (writer->suite_started) {
        xml_test_writer_end_suite(writer);
    }

    xml_test_suites_finalize(writer->test_suites);

    write_allure_xml(writer);

    xml_stream_writer_flush(writer->stream_writer);

    XML_LOG_INFO(writer->logger, "xml_writer",
                 "Test run complete: %u tests, %u failures, %u errors, %u skipped",
                 writer->total_tests, writer->total_failures, writer->total_errors,
                 writer->total_skipped);

    writer->run_started = false;
    return 0;
}

int xml_test_writer_begin_suite(xml_test_writer_t *writer, const char *name)
{
    if (!writer || !writer->run_started || writer->suite_started)
        return -1;

    XML_LOG_DEBUG(writer->logger, "xml_writer", "Beginning test suite: %s", name);

    writer->current_suite = xml_test_suite_create(name);
    if (!writer->current_suite) {
        XML_LOG_ERROR(writer->logger, "xml_writer", "Failed to create test suite");
        return -1;
    }

    /* Reset the pending attachment queue and test-body gate for the new suite. */
    writer->pending_attachment_count = 0;
    writer->in_test_body = 0;

    writer->suite_started = true;
    return 0;
}

int xml_test_writer_end_suite(xml_test_writer_t *writer)
{
    if (!writer || !writer->suite_started || !writer->current_suite)
        return -1;

    xml_test_suite_finalize(writer->current_suite);

    xml_test_suites_add_suite(writer->test_suites, writer->current_suite);

    XML_LOG_DEBUG(writer->logger, "xml_writer", "Test suite complete: %s (%u tests)",
                  writer->current_suite->name, writer->current_suite->test_count);

    writer->current_suite = NULL;
    writer->suite_started = false;
    return 0;
}

int xml_test_writer_record_pass(xml_test_writer_t *writer, const char *name, double duration_ms)
{
    if (!writer || !writer->suite_started || !name)
        return -1;

    xml_test_result_t *result = xml_test_result_create(name, writer->current_suite->name, "", 0,
                                                       duration_ms, XML_TEST_STATUS_PASS);

    if (!result)
        return -1;

    xml_test_suite_add_test(writer->current_suite, result);
    writer->total_tests++;
    flush_pending_attachments(writer);

    return 0;
}

int xml_test_writer_record_failure(xml_test_writer_t *writer, const char *name, const char *file,
                                   uint32_t line, const char *message, double duration_ms)
{
    if (!writer || !writer->suite_started || !name)
        return -1;

    xml_test_result_t *result
        = xml_test_result_create(name, writer->current_suite->name, file ? file : "", line,
                                 duration_ms, XML_TEST_STATUS_FAIL);

    if (!result)
        return -1;

    xml_test_result_set_failure(result, message, NULL);
    xml_test_suite_add_test(writer->current_suite, result);

    writer->total_tests++;
    writer->total_failures++;
    flush_pending_attachments(writer);

    return 0;
}

int xml_test_writer_record_skip(xml_test_writer_t *writer, const char *name, const char *reason)
{
    if (!writer || !writer->suite_started || !name)
        return -1;

    xml_test_result_t *result = xml_test_result_create(name, writer->current_suite->name, "", 0,
                                                       0.0, XML_TEST_STATUS_SKIP);

    if (!result)
        return -1;

    xml_test_result_set_skip(result, reason);
    xml_test_suite_add_test(writer->current_suite, result);

    writer->total_tests++;
    writer->total_skipped++;
    flush_pending_attachments(writer);

    return 0;
}

int xml_test_writer_record_error(xml_test_writer_t *writer, const char *name, const char *file,
                                 uint32_t line, const char *message, double duration_ms)
{
    if (!writer || !writer->suite_started || !name)
        return -1;

    xml_test_result_t *result
        = xml_test_result_create(name, writer->current_suite->name, file ? file : "", line,
                                 duration_ms, XML_TEST_STATUS_ERROR);

    if (!result)
        return -1;

    xml_test_result_set_failure(result, message, NULL);
    xml_test_suite_add_test(writer->current_suite, result);

    writer->total_tests++;
    writer->total_errors++;
    flush_pending_attachments(writer);

    return 0;
}

int xml_test_writer_add_suite_label(xml_test_writer_t *writer, const char *name, const char *value)
{
    if (!writer || !writer->suite_started || !writer->current_suite)
        return -1;
    xml_test_suite_add_label(writer->current_suite, name, value);
    return 0;
}

int xml_test_writer_add_last_result_label(xml_test_writer_t *writer, const char *name,
                                          const char *value)
{
    if (!writer || !writer->suite_started || !writer->current_suite)
        return -1;
    xml_test_result_t *last = writer->current_suite->tests_tail;
    if (!last)
        return -1;
    xml_test_result_add_label(last, name, value);
    return 0;
}

int xml_test_writer_attach_file(xml_test_writer_t *writer, const char *file_path,
                                const char *attachment_name, const char *attachment_type)
{
    if (!writer || !writer->suite_started || !writer->current_suite || !file_path) {
        return -1;
    }

    const char *display_name = attachment_name ? attachment_name : file_path;
    const char *type = attachment_type ? attachment_type : "text/plain";

    if (writer->in_test_body) {
        /* Test body is executing: the result for this test has not been
         * recorded yet (record_pass/fail/skip is called after the body
         * returns).  Queue the attachment; flush_pending_attachments()
         * will apply it to the correct result once it is recorded. */
        if (writer->pending_attachment_count >= XML_PENDING_ATTACH_MAX) {
            XML_LOG_WARN(writer->logger, "xml_writer",
                         "Pending attachment queue full, dropping: %s", file_path);
            return -1;
        }
        xml_pending_attachment_t *pa
            = &writer->pending_attachments[writer->pending_attachment_count++];
        strncpy(pa->file_path, file_path, sizeof(pa->file_path) - 1);
        strncpy(pa->name, display_name, sizeof(pa->name) - 1);
        strncpy(pa->type, type, sizeof(pa->type) - 1);
        pa->file_path[sizeof(pa->file_path) - 1] = '\0';
        pa->name[sizeof(pa->name) - 1] = '\0';
        pa->type[sizeof(pa->type) - 1] = '\0';
        XML_LOG_DEBUG(writer->logger, "xml_writer", "Queued pending attachment: %s", file_path);
        return 0;
    }

    /* Outside test body (e.g. log_capture_stop() after write_test):
     * attach directly to the most recently recorded result. */
    xml_test_result_t *last_test = writer->current_suite->tests_tail;
    if (!last_test) {
        XML_LOG_WARN(writer->logger, "xml_writer", "No test to attach file to: %s", file_path);
        return -1;
    }

    char attachment_info[1024];
    snprintf(attachment_info, sizeof(attachment_info), "\n[[ATTACHMENT:%s|%s|%s]]", file_path,
             display_name, type);

    if (last_test->message) {
        size_t new_len = strlen(last_test->message) + strlen(attachment_info) + 1;
        char *new_message = realloc(last_test->message, new_len);
        if (new_message) {
            strcat(new_message, attachment_info);
            last_test->message = new_message;
        }
    } else {
        last_test->message = strdup(attachment_info);
    }

    XML_LOG_DEBUG(writer->logger, "xml_writer", "Attached file to test %s: %s", last_test->name,
                  file_path);

    return 0;
}

int xml_test_writer_flush(xml_test_writer_t *writer)
{
    if (!writer || !writer->stream_writer)
        return -1;
    return xml_stream_writer_flush(writer->stream_writer);
}

const char *xml_test_writer_get_path(xml_test_writer_t *writer)
{
    return writer ? writer->output_path : NULL;
}

uint32_t xml_test_writer_get_test_count(xml_test_writer_t *writer)
{
    return writer ? writer->total_tests : 0;
}

uint32_t xml_test_writer_get_failure_count(xml_test_writer_t *writer)
{
    return writer ? writer->total_failures : 0;
}

uint32_t xml_test_writer_get_error_count(xml_test_writer_t *writer)
{
    return writer ? writer->total_errors : 0;
}

uint32_t xml_test_writer_get_skip_count(xml_test_writer_t *writer)
{
    return writer ? writer->total_skipped : 0;
}

bool xml_test_writer_has_error(xml_test_writer_t *writer)
{
    return writer && xml_error_handler_has_error(writer->error_handler);
}

const char *xml_test_writer_get_error_message(xml_test_writer_t *writer)
{
    if (!writer)
        return NULL;

    xml_error_t *error = xml_error_handler_get_error(writer->error_handler);
    return error ? error->message : NULL;
}

void xml_test_writer_set_pretty_print(xml_test_writer_t *writer, bool enable)
{
    if (writer && writer->config) {
        writer->config->pretty_print = enable;
    }
}

void xml_test_writer_set_indent_size(xml_test_writer_t *writer, int size)
{
    if (writer && writer->config && size > 0) {
        writer->config->indent_size = size;
    }
}

void xml_test_writer_set_validation(xml_test_writer_t *writer, bool enable)
{
    if (writer && writer->config) {
        writer->config->validate_on_write = enable;
    }
}

void xml_test_writer_set_environment(xml_test_writer_t *writer, const char *env_name)
{
    if (!writer || !env_name)
        return;
    strncpy(writer->environment_name, env_name, sizeof(writer->environment_name) - 1);
    writer->environment_name[sizeof(writer->environment_name) - 1] = '\0';
}

void xml_test_writer_begin_test(xml_test_writer_t *writer)
{
    if (!writer)
        return;
    /* Discard any stale pending attachments from a previous test that
     * somehow were not flushed (defensive reset), then open the gate. */
    writer->pending_attachment_count = 0;
    writer->in_test_body = 1;
}
