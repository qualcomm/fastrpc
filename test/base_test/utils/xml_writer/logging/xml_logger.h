// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef XML_LOGGER_H
#define XML_LOGGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

/**
 * @file xml_logger.h
 * @brief Structured logging system for XML operations
 */

#define XML_LOG_MAX_MESSAGE_LEN 512
#define XML_LOG_MAX_METADATA 16

/* Log levels */
typedef enum {
    XML_LOG_TRACE,
    XML_LOG_DEBUG,
    XML_LOG_INFO,
    XML_LOG_WARN,
    XML_LOG_ERROR,
    XML_LOG_FATAL
} xml_log_level_t;

/**
 * @brief Log entry structure
 */
typedef struct xml_log_entry {
    xml_log_level_t level;                 /* Log level */
    char message[XML_LOG_MAX_MESSAGE_LEN]; /* Log message */
    char component[64];                    /* Component name */
    char file[256];                        /* Source file */
    int line;                              /* Line number */
    time_t timestamp;                      /* When log was created */
    struct {
        char key[64];
        char value[256];
    } metadata[XML_LOG_MAX_METADATA]; /* Additional metadata */
    int metadata_count;               /* Number of metadata entries */
} xml_log_entry_t;

/**
 * @brief Logger structure
 */
typedef struct xml_logger {
    xml_log_level_t min_level; /* Minimum level to log */
    FILE *output;              /* Output stream */
    bool structured_output;    /* Use structured format (JSON-like) */
    bool enabled;              /* Whether logging is enabled */
    bool auto_flush;           /* Auto-flush after each log */
    uint64_t log_count;        /* Total logs written */
} xml_logger_t;

/* ========== Logger Operations ========== */

/**
 * @brief Create a new logger
 */
xml_logger_t *xml_logger_create(FILE *output, xml_log_level_t min_level);

/**
 * @brief Create a logger with file output
 */
xml_logger_t *xml_logger_create_file(const char *path, xml_log_level_t min_level);

/**
 * @brief Destroy a logger
 */
void xml_logger_destroy(xml_logger_t *logger);

/**
 * @brief Get the global logger instance
 */
xml_logger_t *xml_logger_get_global(void);

/**
 * @brief Set the global logger instance
 */
void xml_logger_set_global(xml_logger_t *logger);

/* ========== Logging Operations ========== */

/**
 * @brief Log a message
 */
void xml_logger_log(xml_logger_t *logger, xml_log_level_t level, const char *component,
                    const char *file, int line, const char *format, ...);

/**
 * @brief Log a message with va_list
 */
void xml_logger_vlog(xml_logger_t *logger, xml_log_level_t level, const char *component,
                     const char *file, int line, const char *format, va_list args);

/**
 * @brief Log a structured entry
 */
void xml_logger_log_entry(xml_logger_t *logger, xml_log_entry_t *entry);

/**
 * @brief Flush the logger output
 */
void xml_logger_flush(xml_logger_t *logger);

/* ========== Log Entry Operations ========== */

/**
 * @brief Create a log entry
 */
xml_log_entry_t *xml_log_entry_create(xml_log_level_t level, const char *component,
                                      const char *message, const char *file, int line);

/**
 * @brief Add metadata to a log entry
 */
void xml_log_entry_add_metadata(xml_log_entry_t *entry, const char *key, const char *value);

/**
 * @brief Destroy a log entry
 */
void xml_log_entry_destroy(xml_log_entry_t *entry);

/* ========== Utility Functions ========== */

/**
 * @brief Get log level as string
 */
const char *xml_log_level_to_string(xml_log_level_t level);

/**
 * @brief Parse log level from string
 */
xml_log_level_t xml_log_level_from_string(const char *str);

/**
 * @brief Set minimum log level
 */
void xml_logger_set_min_level(xml_logger_t *logger, xml_log_level_t level);

/**
 * @brief Enable/disable structured output
 */
void xml_logger_set_structured(xml_logger_t *logger, bool structured);

/* ========== Convenience Macros ========== */

#define XML_LOG(logger, level, component, format, ...)                                             \
    xml_logger_log(logger, level, component, __FILE__, __LINE__, format, ##__VA_ARGS__)

#define XML_LOG_TRACE(logger, component, format, ...)                                              \
    XML_LOG(logger, XML_LOG_TRACE, component, format, ##__VA_ARGS__)

#define XML_LOG_DEBUG(logger, component, format, ...)                                              \
    XML_LOG(logger, XML_LOG_DEBUG, component, format, ##__VA_ARGS__)

#define XML_LOG_INFO(logger, component, format, ...)                                               \
    XML_LOG(logger, XML_LOG_INFO, component, format, ##__VA_ARGS__)

#define XML_LOG_WARN(logger, component, format, ...)                                               \
    XML_LOG(logger, XML_LOG_WARN, component, format, ##__VA_ARGS__)

#define XML_LOG_ERROR(logger, component, format, ...)                                              \
    XML_LOG(logger, XML_LOG_ERROR, component, format, ##__VA_ARGS__)

#define XML_LOG_FATAL(logger, component, format, ...)                                              \
    XML_LOG(logger, XML_LOG_FATAL, component, format, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* XML_LOGGER_H */
