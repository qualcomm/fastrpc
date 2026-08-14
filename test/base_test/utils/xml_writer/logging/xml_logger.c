// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "xml_logger.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* Global logger instance */
static xml_logger_t *g_logger = NULL;

/* ========== Logger Operations ========== */

xml_logger_t *xml_logger_create(FILE *output, xml_log_level_t min_level)
{
    xml_logger_t *logger = calloc(1, sizeof(xml_logger_t));
    if (!logger)
        return NULL;

    logger->output = output ? output : stderr;
    logger->min_level = min_level;
    logger->structured_output = false;
    logger->enabled = true;
    logger->auto_flush = true;
    logger->log_count = 0;

    return logger;
}

xml_logger_t *xml_logger_create_file(const char *path, xml_log_level_t min_level)
{
    if (!path)
        return NULL;

    FILE *file = fopen(path, "a");
    if (!file)
        return NULL;

    xml_logger_t *logger = xml_logger_create(file, min_level);
    if (!logger) {
        fclose(file);
        return NULL;
    }

    return logger;
}

void xml_logger_destroy(xml_logger_t *logger)
{
    if (!logger)
        return;

    xml_logger_flush(logger);

    /* Don't close stdout/stderr */
    if (logger->output != stdout && logger->output != stderr) {
        fclose(logger->output);
    }

    free(logger);
}

xml_logger_t *xml_logger_get_global(void)
{
    if (!g_logger) {
        g_logger = xml_logger_create(stderr, XML_LOG_INFO);
    }
    return g_logger;
}

void xml_logger_set_global(xml_logger_t *logger)
{
    if (g_logger && g_logger != logger) {
        xml_logger_destroy(g_logger);
    }
    g_logger = logger;
}

/* ========== Logging Operations ========== */

void xml_logger_log(xml_logger_t *logger, xml_log_level_t level, const char *component,
                    const char *file, int line, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    xml_logger_vlog(logger, level, component, file, line, format, args);
    va_end(args);
}

void xml_logger_vlog(xml_logger_t *logger, xml_log_level_t level, const char *component,
                     const char *file, int line, const char *format, va_list args)
{
    if (!logger || !logger->enabled || level < logger->min_level) {
        return;
    }

    /* Create log entry */
    xml_log_entry_t entry;
    entry.level = level;
    entry.timestamp = time(NULL);
    entry.line = line;
    entry.metadata_count = 0;

    /* Copy component */
    if (component) {
        strncpy(entry.component, component, sizeof(entry.component) - 1);
        entry.component[sizeof(entry.component) - 1] = '\0';
    } else {
        entry.component[0] = '\0';
    }

    /* Copy file */
    if (file) {
        strncpy(entry.file, file, sizeof(entry.file) - 1);
        entry.file[sizeof(entry.file) - 1] = '\0';
    } else {
        entry.file[0] = '\0';
    }

    /* Format message */
    vsnprintf(entry.message, XML_LOG_MAX_MESSAGE_LEN, format, args);
    entry.message[XML_LOG_MAX_MESSAGE_LEN - 1] = '\0';

    /* Log the entry */
    xml_logger_log_entry(logger, &entry);
}

void xml_logger_log_entry(xml_logger_t *logger, xml_log_entry_t *entry)
{
    if (!logger || !entry || !logger->enabled)
        return;

    if (entry->level < logger->min_level)
        return;

    if (logger->structured_output) {
        /* Structured JSON-like format */
        fprintf(logger->output, "{");
        fprintf(logger->output, "\"timestamp\":\"%ld\",", entry->timestamp);
        fprintf(logger->output, "\"level\":\"%s\",", xml_log_level_to_string(entry->level));
        fprintf(logger->output, "\"component\":\"%s\",", entry->component);
        fprintf(logger->output, "\"message\":\"%s\",", entry->message);
        fprintf(logger->output, "\"file\":\"%s\",", entry->file);
        fprintf(logger->output, "\"line\":%d", entry->line);

        if (entry->metadata_count > 0) {
            fprintf(logger->output, ",\"metadata\":{");
            for (int i = 0; i < entry->metadata_count; i++) {
                fprintf(logger->output, "\"%s\":\"%s\"", entry->metadata[i].key,
                        entry->metadata[i].value);
                if (i < entry->metadata_count - 1) {
                    fprintf(logger->output, ",");
                }
            }
            fprintf(logger->output, "}");
        }

        fprintf(logger->output, "}\n");
    } else {
        /* Human-readable format */
        char time_buf[32];
        struct tm *tm_info = localtime(&entry->timestamp);
        strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);

        fprintf(logger->output, "[%s] [%s] [%s] %s", time_buf,
                xml_log_level_to_string(entry->level), entry->component, entry->message);

        if (entry->level >= XML_LOG_ERROR) {
            fprintf(logger->output, " (%s:%d)", entry->file, entry->line);
        }

        fprintf(logger->output, "\n");
    }

    logger->log_count++;

    if (logger->auto_flush) {
        xml_logger_flush(logger);
    }
}

void xml_logger_flush(xml_logger_t *logger)
{
    if (logger && logger->output) {
        fflush(logger->output);
    }
}

/* ========== Log Entry Operations ========== */

xml_log_entry_t *xml_log_entry_create(xml_log_level_t level, const char *component,
                                      const char *message, const char *file, int line)
{
    xml_log_entry_t *entry = calloc(1, sizeof(xml_log_entry_t));
    if (!entry)
        return NULL;

    entry->level = level;
    entry->timestamp = time(NULL);
    entry->line = line;
    entry->metadata_count = 0;

    if (component) {
        strncpy(entry->component, component, sizeof(entry->component) - 1);
        entry->component[sizeof(entry->component) - 1] = '\0';
    }

    if (message) {
        strncpy(entry->message, message, XML_LOG_MAX_MESSAGE_LEN - 1);
        entry->message[XML_LOG_MAX_MESSAGE_LEN - 1] = '\0';
    }

    if (file) {
        strncpy(entry->file, file, sizeof(entry->file) - 1);
        entry->file[sizeof(entry->file) - 1] = '\0';
    }

    return entry;
}

void xml_log_entry_add_metadata(xml_log_entry_t *entry, const char *key, const char *value)
{
    if (!entry || !key || !value)
        return;

    if (entry->metadata_count >= XML_LOG_MAX_METADATA)
        return;

    int idx = entry->metadata_count;
    strncpy(entry->metadata[idx].key, key, sizeof(entry->metadata[idx].key) - 1);
    entry->metadata[idx].key[sizeof(entry->metadata[idx].key) - 1] = '\0';

    strncpy(entry->metadata[idx].value, value, sizeof(entry->metadata[idx].value) - 1);
    entry->metadata[idx].value[sizeof(entry->metadata[idx].value) - 1] = '\0';

    entry->metadata_count++;
}

void xml_log_entry_destroy(xml_log_entry_t *entry) { free(entry); }

/* ========== Utility Functions ========== */

const char *xml_log_level_to_string(xml_log_level_t level)
{
    switch (level) {
    case XML_LOG_TRACE:
        return "TRACE";
    case XML_LOG_DEBUG:
        return "DEBUG";
    case XML_LOG_INFO:
        return "INFO";
    case XML_LOG_WARN:
        return "WARN";
    case XML_LOG_ERROR:
        return "ERROR";
    case XML_LOG_FATAL:
        return "FATAL";
    default:
        return "UNKNOWN";
    }
}

xml_log_level_t xml_log_level_from_string(const char *str)
{
    if (!str)
        return XML_LOG_INFO;

    if (strcasecmp(str, "TRACE") == 0)
        return XML_LOG_TRACE;
    if (strcasecmp(str, "DEBUG") == 0)
        return XML_LOG_DEBUG;
    if (strcasecmp(str, "INFO") == 0)
        return XML_LOG_INFO;
    if (strcasecmp(str, "WARN") == 0)
        return XML_LOG_WARN;
    if (strcasecmp(str, "ERROR") == 0)
        return XML_LOG_ERROR;
    if (strcasecmp(str, "FATAL") == 0)
        return XML_LOG_FATAL;

    return XML_LOG_INFO;
}

void xml_logger_set_min_level(xml_logger_t *logger, xml_log_level_t level)
{
    if (logger) {
        logger->min_level = level;
    }
}

void xml_logger_set_structured(xml_logger_t *logger, bool structured)
{
    if (logger) {
        logger->structured_output = structured;
    }
}
