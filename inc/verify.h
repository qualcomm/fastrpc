// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef VERIFY_H
#define VERIFY_H

extern const char *__progname;

#ifndef _WIN32
#define C_ASSERT(test)                                                        \
	switch(0) {                                                           \
	case 0:                                                               \
	case test:;                                                           \
	}
#endif // _WIN32

#ifndef __V_STR__
#define __V_STR__(x) #x ":"
#endif //__STR__
#ifndef __V_TOSTR__
#define __V_TOSTR__(x) __V_STR__(x)
#endif // __TOSTR__
#ifndef __V_FILE_LINE__
#define __V_FILE_LINE__ __FILE__ ":" __V_TOSTR__(__LINE__)
#endif /*__FILE_LINE__*/

#ifdef __ANDROID__
/*android */

#if (defined VERIFY_PRINT_INFO) || (defined VERIFY_PRINT_ERROR)               \
    || (defined VERIFY_PRINT_ERROR_ALWAYS) || (defined VERIFY_PRINT_WARN)
#include <android/log.h>
#endif

#ifdef VERIFY_PRINT_INFO
#define VERIFY_IPRINTF(format, ...)                                           \
	__android_log_print(ANDROID_LOG_DEBUG, __progname,                    \
	                    __V_FILE_LINE__ format, ##__VA_ARGS__)
#endif

#ifdef VERIFY_PRINT_ERROR
#define VERIFY_EPRINTF(format, ...)                                           \
	__android_log_print(ANDROID_LOG_ERROR, __progname,                    \
	                    __V_FILE_LINE__ format, ##__VA_ARGS__)
#endif

#define VERIFY_EPRINTF_ALWAYS(format, ...)                                    \
	__android_log_print(ANDROID_LOG_ERROR, __progname,                    \
	                    __V_FILE_LINE__ format, ##__VA_ARGS__)

#ifdef VERIFY_PRINT_WARN
#define VERIFY_WPRINTF(format, ...)                                           \
	__android_log_print(ANDROID_LOG_WARN, __progname,                     \
	                    __V_FILE_LINE__ format, ##__VA_ARGS__)
#endif

/* end android */
#elif (defined __hexagon__) || (defined __qdsp6__)
/* q6 */

#ifdef VERIFY_PRINT_INFO
#define FARF_VERIFY_LOW 1
#define FARF_VERIFY_LOW_LEVEL HAP_LEVEL_LOW
#define VERIFY_IPRINTF(args...) FARF(VERIFY_LOW, args)
#endif

#ifdef VERIFY_PRINT_ERROR
#define FARF_VERIFY_ERROR 1
#define FARF_VERIFY_ERROR_LEVEL HAP_LEVEL_ERROR
#define VERIFY_EPRINTF(args...) FARF(VERIFY_ERROR, args)
#endif

#if (defined VERIFY_PRINT_INFO) || (defined VERIFY_PRINT_ERROR)
#include "HAP_farf.h"
#endif

/* end q6 */
#elif (defined USE_SYSLOG)
/* syslog */
#if (defined VERIFY_PRINT_INFO) || (defined VERIFY_PRINT_ERROR)               \
    || (defined VERIFY_PRINT_ERROR_ALWAYS) || (defined VERIFY_PRINT_WARN)
#include <syslog.h>
#endif

#ifdef VERIFY_PRINT_INFO
#define VERIFY_IPRINTF(format, ...)                                           \
	syslog(LOG_USER | LOG_INFO, __V_FILE_LINE__ format, ##__VA_ARGS__)
#endif

#ifdef VERIFY_PRINT_ERROR
#define VERIFY_EPRINTF(format, ...)                                           \
	syslog(LOG_USER | LOG_ERR, __V_FILE_LINE__ format, ##__VA_ARGS__)
#endif

#define VERIFY_EPRINTF_ALWAYS(format, ...)                                    \
	syslog(LOG_ERR, __V_FILE_LINE__ format, ##__VA_ARGS__)

#ifdef VERIFY_PRINT_WARN
#define VERIFY_WPRINTF(format, ...)                                           \
	syslog(LOG_USER | LOG_ERR, __V_FILE_LINE__ format, ##__VA_ARGS__)
#endif

/* end syslog */
#else
/* generic */

#if (defined VERIFY_PRINT_INFO) || (defined VERIFY_PRINT_ERROR)               \
    || (defined VERIFY_PRINT_ERROR_ALWAYS) || (defined VERIFY_PRINT_WARN)
#include <stdio.h>
#endif

#ifdef VERIFY_PRINT_INFO
#define VERIFY_IPRINTF(format, ...)                                           \
	printf(__V_FILE_LINE__ format "\n", ##__VA_ARGS__)
#endif

#ifdef VERIFY_PRINT_ERROR
#define VERIFY_EPRINTF(format, ...)                                           \
	printf(__V_FILE_LINE__ format "\n", ##__VA_ARGS__)
#endif

#define VERIFY_EPRINTF_ALWAYS(format, ...)                                    \
	printf(__V_FILE_LINE__ format, ##__VA_ARGS__)

#ifdef VERIFY_PRINT_WARN
#define VERIFY_WPRINTF(format, ...)                                           \
	printf(__V_FILE_LINE__ format "\n", ##__VA_ARGS__)
#endif

/* end generic */
#endif

#ifndef VERIFY_PRINT_INFO
#define VERIFY_IPRINTF(format, ...) (void)0
#endif

#ifndef VERIFY_PRINT_ERROR
#define VERIFY_EPRINTF(format, ...) (void)0
#endif

#ifndef VERIFYC
#define VERIFYC(val, err_code)                                                \
	do {                                                                  \
		VERIFY_IPRINTF(":info: calling: %s", #val);                   \
		if(0 == (val)) {                                              \
			nErr = err_code;                                      \
			VERIFY_EPRINTF(":error: %d: %s", nErr, #val);         \
			goto bail;                                            \
		} else {                                                      \
			VERIFY_IPRINTF(":info: passed: %s", #val);            \
		}                                                             \
	} while(0)
#endif // VERIFYC

#ifndef VERIFY
#define VERIFY(val)                                                           \
	do {                                                                  \
		VERIFY_IPRINTF(":info: calling: %s", #val);                   \
		if(0 == (val)) {                                              \
			nErr = nErr == 0 ? -1 : nErr;                         \
			VERIFY_EPRINTF(":error: %d: %s", nErr, #val);         \
			goto bail;                                            \
		} else {                                                      \
			VERIFY_IPRINTF(":info: passed: %s", #val);            \
		}                                                             \
	} while(0)
#endif // VERIFY

#ifndef VERIFYM
#define VERIFYM(val, err_code, format, ...)                                   \
	do {                                                                  \
		VERIFY_IPRINTF(":info: calling: " #val "\n");                 \
		if(0 == (val)) {                                              \
			nErr = err_code;                                      \
			VERIFY_EPRINTF_ALWAYS(":Error: 0x%x: " #val "\n",     \
			                      nErr);                          \
			goto bail;                                            \
		} else {                                                      \
			VERIFY_IPRINTF(":info: passed: " #val "\n");          \
		}                                                             \
	} while(0)
#endif // VERIFYM

#endif // VERIFY_H
