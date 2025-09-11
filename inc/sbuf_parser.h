// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef SBUF_PARSER_H
#define SBUF_PARSER_H

#include "sbuf.h"

/**
 * Greedy Recursive Descent Parser in C
 *
 * Stop using strstr or regular expressions.  This simple Recursive Descent
 * Parser can be used to handle complex grammars.
 *
 * For example:
 *   parsing a query string form a uri
 *   input: "file:///foo/bar_far.so.1?_blah1&_bar=barval5&_barfar"
 *   expected output:
 *      parsed query: _blah1 =
 *      parsed query: _bar = barval5
 *      parsed query: _barfar =
 *
 *   static int qmark(struct sbuf *buf) {
 *      return sbuf_char(buf, '?');
 *   }
 *   static int notandoreq(struct sbuf *buf) {
 *      return sbuf_notchars(buf, "&=");
 *   }
 *   static int notand(struct sbuf *buf) {
 *      return sbuf_notchar(buf, '&');
 *   }
 *
 *   const char *name;
 *   int nameLen;
 *   const char *value;
 *   int valueLen;
 *   const char *data = "file:///foo/bar_far.so.1?_blah1&_bar=barval5&_barfar";
 *
 *   //initialize
 *   sbuf_parser_init(&buf, data, strlen(data));
 *
 *   //parse until question mark
 *   assert(sbuf_until(&buf, sbuf_any, qmark));
 *
 *   //parse each query
 *   while(!sbuf_end(&buf)) {
 *      //record where the name starts
 *      name = sbuf_cur(&buf);
 *
 *      //name is valid until '=' or '&'
 *      assert(sbuf_many1(&buf, notandoreq));
 *      nameLen = sbuf_cur(&buf) - name;
 *
 *      value = 0;
 *      valueLen = 0;
 *      //if the next char is a '=' then we also get a value
 *      if(sbuf_char(&buf, '=')) {
 *         value = sbuf_cur(&buf);
 *
 *         //value is until the next query that starts with '&'
 *         assert(sbuf_many1(&buf, notand));
 *         valueLen = sbuf_cur(&buf) - value;
 *      }
 *      //expect '&' or end
 *      sbuf_char(&buf, '&');
 *      printf("parsed query: %.*s = %.*s\n", nameLen, name, valueLen, value);
 *   }
 *
 */

//! init
static __inline void
sbuf_parser_init(struct sbuf *buf, const char *data, int dataLen)
{
	sbuf_init(buf, 0, (void *)data, dataLen);
}

//! current postiion
static __inline char *
sbuf_cur(struct sbuf *buf)
{
	return (char *)sbuf_head(buf);
}

//! look at the next character if the buffer is still valid
static __inline int
sbuf_peek(struct sbuf *buf, char *c)
{
	if(!sbuf_valid(buf)) {
		return 0;
	}
	*c = *sbuf_cur(buf);
	return 1;
}

//! returns true if the buffer is ended
static __inline int
sbuf_end(struct sbuf *buf)
{
	return sbuf_left(buf) == 0;
}

//! consume 1 char if its in string chars
static __inline int
sbuf_chars(struct sbuf *buf, const char *chars)
{
	int i = 0;
	char c;
	if(!sbuf_peek(buf, &c)) {
		return 0;
	}
	for(i = 0; chars[i] != 0; ++i) {
		if(c == chars[i]) {
			sbuf_advance(buf, 1);
			return 1;
		}
	}
	return 0;
}

//! consume 1 char only if its not in string chars
static __inline int
sbuf_notchars(struct sbuf *buf, const char *chars)
{
	int i = 0;
	char c;
	if(!sbuf_peek(buf, &c)) {
		return 0;
	}
	for(i = 0; chars[i] != 0; ++i) {
		if(c == chars[i]) {
			return 0;
		}
	}
	sbuf_advance(buf, 1);
	return 1;
}

//! consume only char t
static __inline int
sbuf_char(struct sbuf *buf, const char t)
{
	char str[2] = { t, 0 };
	return sbuf_chars(buf, str);
}

//! consume any char except for t
static __inline int
sbuf_notchar(struct sbuf *buf, const char t)
{
	char str[2] = { t, 0 };
	return sbuf_notchars(buf, str);
}

/**
 * consume any char
 */
static __inline int
sbuf_any(struct sbuf *buf)
{
	return sbuf_notchars(buf, "");
}

/**
 * range is pairs of characters
 *
 * pairs are inclusive, start must be less then or equal then the end
 *
 * for example: AZaz09--..
 *   matches uppercase and lowercase letters, numbers, dashes and dots
 *
 */
static __inline int
sbuf_range(struct sbuf *buf, const char *chars)
{
	int i, j;
	char c;
	if(!sbuf_peek(buf, &c)) {
		return 0;
	}
	for(i = 0, j = 1; chars[i] != 0 && chars[j] != 0; i += 2, j += 2) {
		if(c >= chars[i] && c <= chars[j]) {
			sbuf_advance(buf, 1);
			return 1;
		}
	}
	return 0;
}

/**
 * greedly consume and match the entire string
 * empty string always succeeds without consuming any data
 */
static __inline int
sbuf_string(struct sbuf *buf, const char *str)
{
	int i = 0;
	for(i = 0; str[i] != 0; ++i) {
		if(!sbuf_char(buf, str[i])) {
			return 0;
		}
	}
	return 1;
}

/**
 * consumes until fails
 */
static __inline int
sbuf_many(struct sbuf *buf, int (*consume)(struct sbuf *buf))
{
	if(!sbuf_valid(buf)) {
		return 0;
	}
	while(consume(buf)) {
		;
	}
	return 1;
}
/**
 * consumes until fails, must consume at least 1
 */
static __inline int
sbuf_many1(struct sbuf *buf, int (*consume)(struct sbuf *buf))
{
	if(!consume(buf)) {
		return 0;
	}
	sbuf_many(buf, consume);
	return 1;
}
/**
 * runs 'consume' until 'stop' succeeds
 * 'stop' must fail in such a way that it doesn't consume any data
 */
static __inline int
sbuf_until(struct sbuf *buf, int (*consume)(struct sbuf *buf),
           int (*stop)(struct sbuf *buf))
{
	while(!stop(buf)) {
		if(!consume(buf)) {
			return 0;
		}
	}
	return 1;
}

/**
 * allows for backtracking,
 * @param parser, runs parser and only consume if it succeeds
 */
static __inline int
sbuf_try(struct sbuf *buf, int (*parser)(struct sbuf *buf))
{
	struct sbuf tryp;
	sbuf_parser_init(&tryp, sbuf_cur(buf), sbuf_left(buf));
	if(parser(&tryp)) {
		sbuf_advance(buf, sbuf_cur(&tryp) - sbuf_cur(buf));
		return 1;
	}
	return 0;
}
#endif // SBUF_PARSER_H
