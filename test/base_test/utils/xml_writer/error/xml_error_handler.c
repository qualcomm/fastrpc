// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "xml_error_handler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Global error handler instance */
static xml_error_handler_t *g_error_handler = NULL;

/* ========== Error Handler Operations ========== */

xml_error_handler_t *xml_error_handler_create(void)
{
    xml_error_handler_t *handler = calloc(1, sizeof(xml_error_handler_t));
    if (!handler)
        return NULL;

    handler->current_error = NULL;
    handler->error_count = 0;
    handler->enabled = true;

    return handler;
}

void xml_error_handler_destroy(xml_error_handler_t *handler)
{
    if (!handler)
        return;

    /* Clear all errors */
    xml_error_handler_clear_all(handler);

    free(handler);
}

xml_error_handler_t *xml_error_handler_get_global(void)
{
    if (!g_error_handler) {
        g_error_handler = xml_error_handler_create();
    }
    return g_error_handler;
}

/* ========== Error Creation ========== */

xml_error_t *xml_error_create(xml_error_code_t code, const char *message, const char *file,
                              int line, const char *function)
{
    xml_error_t *error = calloc(1, sizeof(xml_error_t));
    if (!error)
        return NULL;

    error->code = code;
    error->timestamp = time(NULL);
    error->line = line;
    error->cause = NULL;

    if (message) {
        strncpy(error->message, message, XML_ERROR_MAX_MESSAGE_LEN - 1);
        error->message[XML_ERROR_MAX_MESSAGE_LEN - 1] = '\0';
    }

    if (file) {
        strncpy(error->file, file, sizeof(error->file) - 1);
        error->file[sizeof(error->file) - 1] = '\0';
    }

    if (function) {
        strncpy(error->function, function, sizeof(error->function) - 1);
        error->function[sizeof(error->function) - 1] = '\0';
    }

    error->context[0] = '\0';

    return error;
}

xml_error_t *xml_error_create_with_context(xml_error_code_t code, const char *message,
                                           const char *context, const char *file, int line,
                                           const char *function)
{
    xml_error_t *error = xml_error_create(code, message, file, line, function);
    if (!error)
        return NULL;

    if (context) {
        strncpy(error->context, context, XML_ERROR_MAX_CONTEXT_LEN - 1);
        error->context[XML_ERROR_MAX_CONTEXT_LEN - 1] = '\0';
    }

    return error;
}

xml_error_t *xml_error_wrap(xml_error_t *cause, const char *message, const char *file, int line,
                            const char *function)
{
    xml_error_t *error
        = xml_error_create(cause ? cause->code : XML_ERROR_NONE, message, file, line, function);

    if (error) {
        error->cause = cause;
    }

    return error;
}

void xml_error_destroy(xml_error_t *error)
{
    if (!error)
        return;

    /* Recursively destroy cause chain */
    if (error->cause) {
        xml_error_destroy(error->cause);
    }

    free(error);
}

/* ========== Error Handling ========== */

void xml_error_handler_set_error(xml_error_handler_t *handler, xml_error_t *error)
{
    if (!handler || !handler->enabled) {
        xml_error_destroy(error);
        return;
    }

    /* Clear current error */
    if (handler->current_error) {
        /* Add to history before clearing */
        if (handler->error_count < XML_ERROR_MAX_HISTORY) {
            handler->error_history[handler->error_count++] = handler->current_error;
        } else {
            /* History full, destroy oldest error */
            xml_error_destroy(handler->error_history[0]);

            /* Shift history */
            for (int i = 0; i < XML_ERROR_MAX_HISTORY - 1; i++) {
                handler->error_history[i] = handler->error_history[i + 1];
            }

            handler->error_history[XML_ERROR_MAX_HISTORY - 1] = handler->current_error;
        }
    }

    handler->current_error = error;
}

xml_error_t *xml_error_handler_get_error(xml_error_handler_t *handler)
{
    return handler ? handler->current_error : NULL;
}

void xml_error_handler_clear_error(xml_error_handler_t *handler)
{
    if (!handler)
        return;

    if (handler->current_error) {
        xml_error_destroy(handler->current_error);
        handler->current_error = NULL;
    }
}

void xml_error_handler_clear_all(xml_error_handler_t *handler)
{
    if (!handler)
        return;

    /* Clear current error */
    xml_error_handler_clear_error(handler);

    /* Clear history */
    for (int i = 0; i < handler->error_count; i++) {
        xml_error_destroy(handler->error_history[i]);
        handler->error_history[i] = NULL;
    }

    handler->error_count = 0;
}

bool xml_error_handler_has_error(xml_error_handler_t *handler)
{
    return handler && handler->current_error != NULL;
}

/* ========== Error Reporting ========== */

void xml_error_print_diagnostic(xml_error_t *error, FILE *output)
{
    if (!error || !output)
        return;

    fprintf(output, "\n=== XML Error Diagnostic ===\n");
    fprintf(output, "Error Code: %d (%s)\n", error->code, xml_error_code_to_string(error->code));
    fprintf(output, "Message: %s\n", error->message);
    fprintf(output, "Location: %s:%d in %s()\n", error->file, error->line, error->function);

    char *time_str = ctime(&error->timestamp);
    if (time_str) {
        /* Remove newline from ctime */
        time_str[strlen(time_str) - 1] = '\0';
        fprintf(output, "Timestamp: %s\n", time_str);
    }

    if (error->context[0] != '\0') {
        fprintf(output, "Context:\n%s\n", error->context);
    }

    if (error->cause) {
        fprintf(output, "\nCaused by:\n");
        fprintf(output, "  %s\n", error->cause->message);
        fprintf(output, "  at %s:%d in %s()\n", error->cause->file, error->cause->line,
                error->cause->function);
    }

    fprintf(output, "============================\n\n");
}

const char *xml_error_code_to_string(xml_error_code_t code)
{
    switch (code) {
    case XML_ERROR_NONE:
        return "No Error";
    case XML_ERROR_MEMORY:
        return "Memory Allocation Error";
    case XML_ERROR_IO:
        return "I/O Error";
    case XML_ERROR_PARSE:
        return "Parse Error";
    case XML_ERROR_VALIDATION:
        return "Validation Error";
    case XML_ERROR_ENCODING:
        return "Encoding Error";
    case XML_ERROR_SCHEMA:
        return "Schema Error";
    case XML_ERROR_TEMPLATE:
        return "Template Error";
    case XML_ERROR_TRANSFORM:
        return "Transform Error";
    case XML_ERROR_INVALID_ARGUMENT:
        return "Invalid Argument";
    case XML_ERROR_BUFFER_OVERFLOW:
        return "Buffer Overflow";
    case XML_ERROR_DEPTH_EXCEEDED:
        return "Maximum Depth Exceeded";
    case XML_ERROR_SECURITY:
        return "Security Violation";
    default:
        return "Unknown Error";
    }
}

char *xml_error_get_formatted_message(xml_error_t *error)
{
    if (!error)
        return NULL;

    size_t buffer_size = 1024;
    char *buffer = malloc(buffer_size);
    if (!buffer)
        return NULL;

    snprintf(buffer, buffer_size, "[%s] %s at %s:%d in %s()", xml_error_code_to_string(error->code),
             error->message, error->file, error->line, error->function);

    return buffer;
}
