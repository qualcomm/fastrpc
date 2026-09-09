// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef XML_ERROR_HANDLER_H
#define XML_ERROR_HANDLER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

/**
 * @file xml_error_handler.h
 * @brief Comprehensive error handling system for XML operations
 */

#define XML_ERROR_MAX_MESSAGE_LEN 512
#define XML_ERROR_MAX_CONTEXT_LEN 1024
#define XML_ERROR_MAX_HISTORY 32

/* Error codes */
typedef enum {
    XML_ERROR_NONE = 0,
    XML_ERROR_MEMORY,
    XML_ERROR_IO,
    XML_ERROR_PARSE,
    XML_ERROR_VALIDATION,
    XML_ERROR_ENCODING,
    XML_ERROR_SCHEMA,
    XML_ERROR_TEMPLATE,
    XML_ERROR_TRANSFORM,
    XML_ERROR_INVALID_ARGUMENT,
    XML_ERROR_BUFFER_OVERFLOW,
    XML_ERROR_DEPTH_EXCEEDED,
    XML_ERROR_SECURITY
} xml_error_code_t;

/* Forward declaration */
typedef struct xml_error xml_error_t;

/**
 * @brief Error structure with detailed diagnostic information
 */
struct xml_error {
    xml_error_code_t code;                   /* Error code */
    char message[XML_ERROR_MAX_MESSAGE_LEN]; /* Error message */
    char file[256];                          /* Source file where error occurred */
    int line;                                /* Line number */
    char function[128];                      /* Function name */
    char context[XML_ERROR_MAX_CONTEXT_LEN]; /* Additional context */
    time_t timestamp;                        /* When error occurred */
    xml_error_t *cause;                      /* Underlying cause (chained errors) */
};

/**
 * @brief Error handler structure
 */
typedef struct xml_error_handler {
    xml_error_t *current_error;                        /* Current error */
    xml_error_t *error_history[XML_ERROR_MAX_HISTORY]; /* Error history */
    int error_count;                                   /* Number of errors in history */
    bool enabled;                                      /* Whether error handling is enabled */
} xml_error_handler_t;

/* ========== Error Handler Operations ========== */

/**
 * @brief Create a new error handler
 */
xml_error_handler_t *xml_error_handler_create(void);

/**
 * @brief Destroy an error handler
 */
void xml_error_handler_destroy(xml_error_handler_t *handler);

/**
 * @brief Get the global error handler instance
 */
xml_error_handler_t *xml_error_handler_get_global(void);

/* ========== Error Creation ========== */

/**
 * @brief Create a new error
 */
xml_error_t *xml_error_create(xml_error_code_t code, const char *message, const char *file,
                              int line, const char *function);

/**
 * @brief Create an error with context
 */
xml_error_t *xml_error_create_with_context(xml_error_code_t code, const char *message,
                                           const char *context, const char *file, int line,
                                           const char *function);

/**
 * @brief Wrap an existing error with additional context
 */
xml_error_t *xml_error_wrap(xml_error_t *cause, const char *message, const char *file, int line,
                            const char *function);

/**
 * @brief Destroy an error
 */
void xml_error_destroy(xml_error_t *error);

/* ========== Error Handling ========== */

/**
 * @brief Set the current error
 */
void xml_error_handler_set_error(xml_error_handler_t *handler, xml_error_t *error);

/**
 * @brief Get the current error
 */
xml_error_t *xml_error_handler_get_error(xml_error_handler_t *handler);

/**
 * @brief Clear the current error
 */
void xml_error_handler_clear_error(xml_error_handler_t *handler);

/**
 * @brief Clear all errors including history
 */
void xml_error_handler_clear_all(xml_error_handler_t *handler);

/**
 * @brief Check if an error has occurred
 */
bool xml_error_handler_has_error(xml_error_handler_t *handler);

/* ========== Error Reporting ========== */

/**
 * @brief Print error diagnostic information
 */
void xml_error_print_diagnostic(xml_error_t *error, FILE *output);

/**
 * @brief Get error code as string
 */
const char *xml_error_code_to_string(xml_error_code_t code);

/**
 * @brief Get formatted error message
 */
char *xml_error_get_formatted_message(xml_error_t *error);

/* ========== Convenience Macros ========== */

#define XML_ERROR_CREATE(code, msg) xml_error_create(code, msg, __FILE__, __LINE__, __FUNCTION__)

#define XML_ERROR_CREATE_CTX(code, msg, ctx)                                                       \
    xml_error_create_with_context(code, msg, ctx, __FILE__, __LINE__, __FUNCTION__)

#define XML_ERROR_WRAP(cause, msg) xml_error_wrap(cause, msg, __FILE__, __LINE__, __FUNCTION__)

#define XML_ERROR_SET(handler, error) xml_error_handler_set_error(handler, error)

#define XML_ERROR_RETURN_IF(condition, handler, code, msg)                                         \
    do {                                                                                           \
        if (condition) {                                                                           \
            xml_error_t *__err = XML_ERROR_CREATE(code, msg);                                      \
            XML_ERROR_SET(handler, __err);                                                         \
            return -1;                                                                             \
        }                                                                                          \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif /* XML_ERROR_HANDLER_H */
