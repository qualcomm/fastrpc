// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef XML_STREAM_WRITER_H
#define XML_STREAM_WRITER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/**
 * @file xml_stream_writer.h
 * @brief Streaming XML writer for memory-efficient output
 *
 * Provides a streaming approach to XML generation that minimizes
 * memory usage by writing directly to output streams with buffering.
 */

/* Default buffer size for streaming */
#define XML_STREAM_DEFAULT_BUFFER_SIZE 4096

/* Maximum element depth to prevent stack overflow */
#define XML_STREAM_MAX_DEPTH 256

/* Forward declaration */
typedef struct xml_stream_writer xml_stream_writer_t;

/**
 * @brief Streaming XML writer structure
 */
struct xml_stream_writer {
    FILE *output;                              /* Output file stream */
    char *buffer;                              /* Write buffer */
    size_t buffer_size;                        /* Buffer size */
    size_t buffer_pos;                         /* Current position in buffer */
    int depth;                                 /* Current element depth */
    bool element_open;                         /* Whether an element tag is open */
    bool pretty_print;                         /* Enable pretty printing */
    int indent_size;                           /* Spaces per indent level */
    bool needs_newline;                        /* Track if newline is needed */
    char *element_stack[XML_STREAM_MAX_DEPTH]; /* Stack of open elements */
    uint64_t bytes_written;                    /* Total bytes written */
    bool error_occurred;                       /* Error flag */
};

/* ========== Lifecycle Operations ========== */

/**
 * @brief Create a new streaming XML writer
 *
 * @param output Output file stream (must be opened for writing)
 * @param buffer_size Size of internal buffer (0 = use default)
 * @return New writer instance or NULL on error
 */
xml_stream_writer_t *xml_stream_writer_create(FILE *output, size_t buffer_size);

/**
 * @brief Create a writer with pretty printing enabled
 */
xml_stream_writer_t *xml_stream_writer_create_pretty(FILE *output, size_t buffer_size,
                                                     int indent_size);

/**
 * @brief Destroy a streaming XML writer
 *
 * Flushes any remaining buffered data and frees resources.
 * Does NOT close the output stream.
 */
void xml_stream_writer_destroy(xml_stream_writer_t *writer);

/* ========== Document Operations ========== */

/**
 * @brief Start an XML document
 *
 * Writes the XML declaration: <?xml version="1.0" encoding="UTF-8"?>
 */
int xml_stream_writer_start_document(xml_stream_writer_t *writer);

/**
 * @brief End an XML document
 *
 * Closes any remaining open elements and flushes the buffer.
 */
int xml_stream_writer_end_document(xml_stream_writer_t *writer);

/* ========== Element Operations ========== */

/**
 * @brief Start an XML element
 *
 * @param writer The writer instance
 * @param name Element name
 * @return 0 on success, -1 on error
 */
int xml_stream_writer_start_element(xml_stream_writer_t *writer, const char *name);

/**
 * @brief End the current XML element
 *
 * Closes the most recently opened element.
 */
int xml_stream_writer_end_element(xml_stream_writer_t *writer);

/**
 * @brief Write an empty element (self-closing tag)
 *
 * Example: <element attr="value"/>
 */
int xml_stream_writer_empty_element(xml_stream_writer_t *writer, const char *name);

/* ========== Attribute Operations ========== */

/**
 * @brief Write an attribute for the current element
 *
 * Must be called after start_element and before any text or child elements.
 *
 * @param writer The writer instance
 * @param name Attribute name
 * @param value Attribute value (will be escaped)
 * @return 0 on success, -1 on error
 */
int xml_stream_writer_write_attribute(xml_stream_writer_t *writer, const char *name,
                                      const char *value);

/**
 * @brief Write an attribute with integer value
 */
int xml_stream_writer_write_attribute_int(xml_stream_writer_t *writer, const char *name,
                                          int64_t value);

/**
 * @brief Write an attribute with unsigned integer value
 */
int xml_stream_writer_write_attribute_uint(xml_stream_writer_t *writer, const char *name,
                                           uint64_t value);

/**
 * @brief Write an attribute with double value
 */
int xml_stream_writer_write_attribute_double(xml_stream_writer_t *writer, const char *name,
                                             double value);

/* ========== Text Content Operations ========== */

/**
 * @brief Write text content
 *
 * Text is automatically escaped for XML special characters.
 *
 * @param writer The writer instance
 * @param text Text content to write
 * @return 0 on success, -1 on error
 */
int xml_stream_writer_write_text(xml_stream_writer_t *writer, const char *text);

/**
 * @brief Write CDATA section
 *
 * Example: <![CDATA[content]]>
 */
int xml_stream_writer_write_cdata(xml_stream_writer_t *writer, const char *data);

/**
 * @brief Write XML comment
 *
 * Example: <!-- comment -->
 */
int xml_stream_writer_write_comment(xml_stream_writer_t *writer, const char *comment);

/* ========== Buffer Operations ========== */

/**
 * @brief Flush the internal buffer to the output stream
 *
 * @return 0 on success, -1 on error
 */
int xml_stream_writer_flush(xml_stream_writer_t *writer);

/**
 * @brief Get the number of bytes written
 */
uint64_t xml_stream_writer_get_bytes_written(xml_stream_writer_t *writer);

/**
 * @brief Check if an error has occurred
 */
bool xml_stream_writer_has_error(xml_stream_writer_t *writer);

/* ========== Internal Helper Functions ========== */

/**
 * @brief Write raw string to buffer (internal use)
 */
int xml_stream_writer_write_raw(xml_stream_writer_t *writer, const char *str);

/**
 * @brief Write escaped text to buffer (internal use)
 */
int xml_stream_writer_write_escaped(xml_stream_writer_t *writer, const char *text);

/**
 * @brief Write indentation (internal use)
 */
int xml_stream_writer_write_indent(xml_stream_writer_t *writer);

#ifdef __cplusplus
}
#endif

#endif /* XML_STREAM_WRITER_H */
