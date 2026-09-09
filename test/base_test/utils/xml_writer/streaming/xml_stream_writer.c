// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "xml_stream_writer.h"
#include <stdlib.h>
#include <string.h>

xml_stream_writer_t *xml_stream_writer_create(FILE *output, size_t buffer_size)
{
    if (!output)
        return NULL;

    xml_stream_writer_t *writer = calloc(1, sizeof(xml_stream_writer_t));
    if (!writer)
        return NULL;

    writer->output = output;
    writer->buffer_size = buffer_size > 0 ? buffer_size : XML_STREAM_DEFAULT_BUFFER_SIZE;
    writer->buffer = malloc(writer->buffer_size);

    if (!writer->buffer) {
        free(writer);
        return NULL;
    }

    writer->buffer_pos = 0;
    writer->depth = 0;
    writer->element_open = false;
    writer->pretty_print = false;
    writer->indent_size = 2;
    writer->needs_newline = false;
    writer->bytes_written = 0;
    writer->error_occurred = false;

    return writer;
}

xml_stream_writer_t *xml_stream_writer_create_pretty(FILE *output, size_t buffer_size,
                                                     int indent_size)
{
    xml_stream_writer_t *writer = xml_stream_writer_create(output, buffer_size);
    if (writer) {
        writer->pretty_print = true;
        writer->indent_size = indent_size > 0 ? indent_size : 2;
    }
    return writer;
}

void xml_stream_writer_destroy(xml_stream_writer_t *writer)
{
    if (!writer)
        return;

    /* Flush any remaining data */
    xml_stream_writer_flush(writer);

    /* Free element stack */
    for (int i = 0; i < writer->depth; i++) {
        free(writer->element_stack[i]);
    }

    free(writer->buffer);
    free(writer);
}

int xml_stream_writer_start_document(xml_stream_writer_t *writer)
{
    if (!writer)
        return -1;

    const char *declaration = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    return xml_stream_writer_write_raw(writer, declaration);
}

int xml_stream_writer_end_document(xml_stream_writer_t *writer)
{
    if (!writer)
        return -1;

    /* Close any remaining open elements */
    while (writer->depth > 0) {
        if (xml_stream_writer_end_element(writer) < 0) {
            return -1;
        }
    }

    /* Write final newline if pretty printing */
    if (writer->pretty_print) {
        xml_stream_writer_write_raw(writer, "\n");
    }

    return xml_stream_writer_flush(writer);
}

int xml_stream_writer_start_element(xml_stream_writer_t *writer, const char *name)
{
    if (!writer || !name)
        return -1;

    /* Close previous element if needed */
    if (writer->element_open) {
        if (xml_stream_writer_write_raw(writer, ">") < 0) {
            return -1;
        }
        writer->element_open = false;
        writer->needs_newline = true;
    }

    /* Write newline and indentation if pretty printing */
    if (writer->pretty_print && writer->needs_newline) {
        if (xml_stream_writer_write_raw(writer, "\n") < 0) {
            return -1;
        }
        if (xml_stream_writer_write_indent(writer) < 0) {
            return -1;
        }
    }

    /* Write element start */
    if (xml_stream_writer_write_raw(writer, "<") < 0
        || xml_stream_writer_write_raw(writer, name) < 0) {
        return -1;
    }

    /* Push element name onto stack */
    if (writer->depth >= XML_STREAM_MAX_DEPTH) {
        writer->error_occurred = true;
        return -1;
    }

    writer->element_stack[writer->depth] = strdup(name);
    writer->element_open = true;
    writer->depth++;
    writer->needs_newline = false;

    return 0;
}

int xml_stream_writer_end_element(xml_stream_writer_t *writer)
{
    if (!writer || writer->depth <= 0)
        return -1;

    writer->depth--;
    char *element_name = writer->element_stack[writer->depth];

    if (writer->element_open) {
        /* Self-closing tag */
        if (xml_stream_writer_write_raw(writer, "/>") < 0) {
            free(element_name);
            return -1;
        }
        writer->element_open = false;
    } else {
        /* Full closing tag */
        if (writer->pretty_print && writer->needs_newline) {
            if (xml_stream_writer_write_raw(writer, "\n") < 0) {
                free(element_name);
                return -1;
            }
            if (xml_stream_writer_write_indent(writer) < 0) {
                free(element_name);
                return -1;
            }
        }

        if (xml_stream_writer_write_raw(writer, "</") < 0
            || xml_stream_writer_write_raw(writer, element_name) < 0
            || xml_stream_writer_write_raw(writer, ">") < 0) {
            free(element_name);
            return -1;
        }
    }

    free(element_name);
    writer->needs_newline = true;

    /* Flush if buffer is getting full */
    if (writer->buffer_pos > writer->buffer_size * 3 / 4) {
        return xml_stream_writer_flush(writer);
    }

    return 0;
}

int xml_stream_writer_empty_element(xml_stream_writer_t *writer, const char *name)
{
    if (xml_stream_writer_start_element(writer, name) < 0) {
        return -1;
    }
    return xml_stream_writer_end_element(writer);
}

int xml_stream_writer_write_attribute(xml_stream_writer_t *writer, const char *name,
                                      const char *value)
{
    if (!writer || !name || !writer->element_open)
        return -1;

    if (xml_stream_writer_write_raw(writer, " ") < 0
        || xml_stream_writer_write_raw(writer, name) < 0
        || xml_stream_writer_write_raw(writer, "=\"") < 0) {
        return -1;
    }

    if (value) {
        if (xml_stream_writer_write_escaped(writer, value) < 0) {
            return -1;
        }
    }

    return xml_stream_writer_write_raw(writer, "\"");
}

int xml_stream_writer_write_attribute_int(xml_stream_writer_t *writer, const char *name,
                                          int64_t value)
{
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%lld", (long long)value);
    return xml_stream_writer_write_attribute(writer, name, buffer);
}

int xml_stream_writer_write_attribute_uint(xml_stream_writer_t *writer, const char *name,
                                           uint64_t value)
{
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%llu", (unsigned long long)value);
    return xml_stream_writer_write_attribute(writer, name, buffer);
}

int xml_stream_writer_write_attribute_double(xml_stream_writer_t *writer, const char *name,
                                             double value)
{
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%.3f", value);
    return xml_stream_writer_write_attribute(writer, name, buffer);
}

int xml_stream_writer_write_text(xml_stream_writer_t *writer, const char *text)
{
    if (!writer || !text)
        return -1;

    /* Close element tag if open */
    if (writer->element_open) {
        if (xml_stream_writer_write_raw(writer, ">") < 0) {
            return -1;
        }
        writer->element_open = false;
        writer->needs_newline = false;
    }

    return xml_stream_writer_write_escaped(writer, text);
}

int xml_stream_writer_write_cdata(xml_stream_writer_t *writer, const char *data)
{
    if (!writer || !data)
        return -1;

    /* Close element tag if open */
    if (writer->element_open) {
        if (xml_stream_writer_write_raw(writer, ">") < 0) {
            return -1;
        }
        writer->element_open = false;
    }

    if (xml_stream_writer_write_raw(writer, "<![CDATA[") < 0
        || xml_stream_writer_write_raw(writer, data) < 0
        || xml_stream_writer_write_raw(writer, "]]>") < 0) {
        return -1;
    }

    return 0;
}

int xml_stream_writer_write_comment(xml_stream_writer_t *writer, const char *comment)
{
    if (!writer || !comment)
        return -1;

    /* Close element tag if open */
    if (writer->element_open) {
        if (xml_stream_writer_write_raw(writer, ">") < 0) {
            return -1;
        }
        writer->element_open = false;
    }

    if (xml_stream_writer_write_raw(writer, "<!-- ") < 0
        || xml_stream_writer_write_raw(writer, comment) < 0
        || xml_stream_writer_write_raw(writer, " -->") < 0) {
        return -1;
    }

    return 0;
}

int xml_stream_writer_flush(xml_stream_writer_t *writer)
{
    if (!writer || writer->buffer_pos == 0)
        return 0;

    size_t written = fwrite(writer->buffer, 1, writer->buffer_pos, writer->output);
    if (written != writer->buffer_pos) {
        writer->error_occurred = true;
        return -1;
    }

    writer->bytes_written += written;
    writer->buffer_pos = 0;

    return 0;
}

uint64_t xml_stream_writer_get_bytes_written(xml_stream_writer_t *writer)
{
    return writer ? writer->bytes_written + writer->buffer_pos : 0;
}

bool xml_stream_writer_has_error(xml_stream_writer_t *writer)
{
    return writer ? writer->error_occurred : true;
}

int xml_stream_writer_write_raw(xml_stream_writer_t *writer, const char *str)
{
    if (!writer || !str)
        return -1;

    size_t len = strlen(str);

    /* If string is larger than buffer, flush and write directly */
    if (len > writer->buffer_size) {
        if (xml_stream_writer_flush(writer) < 0) {
            return -1;
        }

        size_t written = fwrite(str, 1, len, writer->output);
        if (written != len) {
            writer->error_occurred = true;
            return -1;
        }

        writer->bytes_written += written;
        return 0;
    }

    /* If string doesn't fit in buffer, flush first */
    if (writer->buffer_pos + len > writer->buffer_size) {
        if (xml_stream_writer_flush(writer) < 0) {
            return -1;
        }
    }

    /* Copy to buffer */
    memcpy(writer->buffer + writer->buffer_pos, str, len);
    writer->buffer_pos += len;

    return 0;
}

int xml_stream_writer_write_escaped(xml_stream_writer_t *writer, const char *text)
{
    if (!writer || !text)
        return -1;

    const char *p = text;
    const char *start = text;

    while (*p) {
        const char *replacement = NULL;

        switch (*p) {
        case '<':
            replacement = "&lt;";
            break;
        case '>':
            replacement = "&gt;";
            break;
        case '&':
            replacement = "&amp;";
            break;
        case '"':
            replacement = "&quot;";
            break;
        case '\'':
            replacement = "&apos;";
            break;
        default:
            break;
        }

        if (replacement) {
            /* Write text before special character */
            if (p > start) {
                size_t len = p - start;
                char *temp = strndup(start, len);
                if (!temp)
                    return -1;
                int result = xml_stream_writer_write_raw(writer, temp);
                free(temp);
                if (result < 0)
                    return -1;
            }

            /* Write replacement */
            if (xml_stream_writer_write_raw(writer, replacement) < 0) {
                return -1;
            }

            start = p + 1;
        }

        p++;
    }

    /* Write remaining text */
    if (p > start) {
        size_t len = p - start;
        char *temp = strndup(start, len);
        if (!temp)
            return -1;
        int result = xml_stream_writer_write_raw(writer, temp);
        free(temp);
        return result;
    }

    return 0;
}

int xml_stream_writer_write_indent(xml_stream_writer_t *writer)
{
    if (!writer || !writer->pretty_print)
        return 0;

    int total_spaces = writer->depth * writer->indent_size;

    for (int i = 0; i < total_spaces; i++) {
        if (xml_stream_writer_write_raw(writer, " ") < 0) {
            return -1;
        }
    }

    return 0;
}
