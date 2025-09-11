// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#define _GNU_SOURCE
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "AEEStdErr.h"
#include "HAP_farf_internal.h"
#include "fastrpc_common.h"
#include "fastrpc_config.h"
#include "fastrpc_trace.h"
#include "rpcmem_internal.h"
#include "verify.h"

#define PROPERTY_VALUE_MAX 72
/* Create persist buffer of size 1 MB */
#define DEBUG_BUF_SIZE 1024 * 1024

/* Prepend index, pid, tid, domain */
#define PREPEND_DBGBUF_FARF_SIZE 30

/* trace to update start of persist buffer. */
#define DEBUF_BUF_TRACE "frpc_dbgbuf:"

#define IS_PERSIST_BUF_DATA(len, level)                                       \
	((len > 0) && (len < MAX_FARF_LEN)                                    \
	 && (level == HAP_LEVEL_RPC_CRITICAL || level == HAP_LEVEL_CRITICAL))

typedef struct persist_buffer {
	/* Debug logs to be printed on dsp side */
	char *buf;

	/* Buffer index */
	unsigned int size;

	/* Lock to protect global logger buffer */
	pthread_mutex_t mut;
} persist_buffer;

static persist_buffer persist_buf;
static uint32_t fastrpc_logmask;
static FILE *log_userspace_file_fd; // file descriptor to save userspace
                                    // runtime farf logs

void
set_runtime_logmask(uint32_t mask)
{
	fastrpc_logmask = mask;
}

#if defined(__LE_TVM__) || defined(__ANDROID__)
#include <android/log.h>
extern const char *__progname;

static inline int
hap_2_android_log_level(int hap_level)
{
	switch(hap_level) {
	case HAP_LEVEL_LOW:
	case HAP_LEVEL_RPC_LOW:
	case HAP_LEVEL_CRITICAL:
	case HAP_LEVEL_RPC_CRITICAL:
		return ANDROID_LOG_DEBUG;
	case HAP_LEVEL_MEDIUM:
	case HAP_LEVEL_HIGH:
	case HAP_LEVEL_RPC_MEDIUM:
	case HAP_LEVEL_RPC_HIGH:
		return ANDROID_LOG_INFO;
	case HAP_LEVEL_ERROR:
	case HAP_LEVEL_RPC_ERROR:
		return ANDROID_LOG_ERROR;
	case HAP_LEVEL_FATAL:
	case HAP_LEVEL_RPC_FATAL:
		return ANDROID_LOG_FATAL;
	}
	return ANDROID_LOG_UNKNOWN;
}
#elif defined(USE_SYSLOG)
#include <syslog.h>

int
hap_2_syslog_level(int log_type)
{
	switch(log_type) {
	case HAP_LEVEL_LOW:
	case HAP_LEVEL_RPC_LOW:
	case HAP_LEVEL_RPC_CRITICAL:
	case HAP_LEVEL_CRITICAL:
		return LOG_DEBUG;
	case HAP_LEVEL_MEDIUM:
	case HAP_LEVEL_HIGH:
	case HAP_LEVEL_RPC_MEDIUM:
	case HAP_LEVEL_RPC_HIGH:
		return LOG_INFO;
	case HAP_LEVEL_ERROR:
	case HAP_LEVEL_RPC_ERROR:
		return LOG_ERR;
	case HAP_LEVEL_FATAL:
	case HAP_LEVEL_RPC_FATAL:
		return LOG_CRIT;
	}
	return 0;
}
#endif

static unsigned long long
GetTime(void)
{
	struct timeval tv;
	struct timezone tz;

	gettimeofday(&tv, &tz);

	/* Integer overflow check. */
	if(tv.tv_sec > (ULLONG_MAX - tv.tv_usec) / 1000000ULL) {
		return 0;
	}
	return tv.tv_sec * 1000000ULL + tv.tv_usec;
}

/* Function to append CRITICAL debug logs to persist_buf */
static void
print_dbgbuf_data(char *data, int size)
{
	int len = 0;

	pthread_mutex_lock(&persist_buf.mut);
	if(persist_buf.buf) {
		if(((persist_buf.size + PREPEND_DBGBUF_FARF_SIZE + size)
		    > DEBUG_BUF_SIZE)) {
			persist_buf.size = strlen(DEBUF_BUF_TRACE) + 1;
		}
		len = snprintf(persist_buf.buf + persist_buf.size,
		               DEBUG_BUF_SIZE - persist_buf.size,
		               "%llu:%d:%d:dom:%d: %s\n", GetTime(), getpid(),
		               gettid(), get_current_domain(), data);
		persist_buf.size += (len + 1);
	}
	pthread_mutex_unlock(&persist_buf.mut);
}

void
HAP_debug_v2(int level, const char *file, int line, const char *format, ...)
{
	char *buf = NULL;
	int len = 0;
	va_list argp;

	buf = (char *)malloc(sizeof(char) * MAX_FARF_LEN);
	if(buf == NULL) {
		return;
	}
	va_start(argp, format);
	len = vsnprintf(buf, MAX_FARF_LEN, format, argp);
	va_end(argp);
	/* If level is set to HAP_LEVEL_DEBUG append the farf message to
	 * persist buffer. */
	if(persist_buf.buf && IS_PERSIST_BUF_DATA(len, level)) {
		print_dbgbuf_data(buf, len);
	}
	HAP_debug(buf, level, file, line);
	if(buf) {
		free(buf);
		buf = NULL;
	}
}

void
HAP_debug_runtime(int level, const char *file, int line, const char *format,
                  ...)
{
	int len = 0;
	va_list argp;
	char *buf = NULL, *log = NULL;

	/*
	 * Adding logs to persist buffer when level is set to
	 * RUNTIME_RPC_CRITICAL and fastrpc_log mask is disabled.
	 */
	if(((1 << level) & (fastrpc_logmask))
	   || ((level == HAP_LEVEL_RPC_CRITICAL) && persist_buf.buf)
	   || log_userspace_file_fd != NULL) {
		buf = (char *)malloc(sizeof(char) * MAX_FARF_LEN);
		if(buf == NULL) {
			return;
		}
		va_start(argp, format);
		len = vsnprintf(buf, MAX_FARF_LEN, format, argp);
		va_end(argp);
		log = (char *)malloc(sizeof(char) * MAX_FARF_LEN);
		if(log == NULL) {
			return;
		}
		snprintf(log, MAX_FARF_LEN, "%d:%d:%s:%s:%d: %s", getpid(),
		         gettid(), __progname, file, line, buf);
	}

	print_dbgbuf_data(log, len);
	if(((1 << level) & (fastrpc_logmask))) {
		if(log_userspace_file_fd != NULL) {
			fputs(log, log_userspace_file_fd);
			fputs("\n", log_userspace_file_fd);
		}
		HAP_debug(buf, level, file, line);
	}
	if(buf) {
		free(buf);
	}
	if(log) {
		free(log);
	}
}

#ifdef __LE_TVM__
static char
android_log_level_to_char(int level)
{
	char log_level;

	switch(level) {
	case ANDROID_LOG_DEBUG:
	case ANDROID_LOG_DEFAULT:
		log_level = 'D';
		break;
	case ANDROID_LOG_INFO:
		log_level = 'I';
		break;
	case ANDROID_LOG_WARN:
		log_level = 'W';
		break;
	case ANDROID_LOG_ERROR:
		log_level = 'E';
		break;
	case ANDROID_LOG_FATAL:
		log_level = 'F';
		break;
	default:
		log_level = 'D';
	}

	return log_level;
}

static char *
get_filename(const char *str, const char delim)
{
	char *iter = NULL, *token = str;

	for(iter = str; *iter != '\0'; iter++) {
		if(*iter == delim)
			token = iter + 1;
	}
	return token;
}

int
get_newlines(const char *str)
{
	char *iter = NULL;
	int newlines = 0;

	for(iter = str; *iter != '\0'; iter++) {
		if(*iter == '\n')
			newlines++;
	}
	return newlines;
}
#endif

void
HAP_debug(const char *msg, int level, const char *filename, int line)
{
#ifdef __ANDROID__
	__android_log_print(hap_2_android_log_level(level), __progname,
	                    "%s:%d: %s", filename, line, msg);
#elif defined(USE_SYSLOG)
	syslog(hap_2_syslog_level(level), "%s:%d: %s", filename, line, msg);
#elif defined(__LE_TVM__)
	const char delim = '/';
	char *short_filename = NULL;
	int newlines = 0;

	short_filename = get_filename(filename, delim);
	newlines = get_newlines(msg);
	level = hap_2_android_log_level(level);
	if(newlines)
		printf("ADSPRPC: %d %d %c %s: %s:%d: %s", getpid(), gettid(),
		       android_log_level_to_char(level), __progname,
		       short_filename, line, msg);
	else
		printf("ADSPRPC: %d %d %c %s: %s:%d: %s\n", getpid(), gettid(),
		       android_log_level_to_char(level), __progname,
		       short_filename, line, msg);
	fflush(stdout);
#endif
}

void
fastrpc_log_init()
{
	bool debug_build_type = false;
	int nErr = AEE_SUCCESS, fd = -1;
	char build_type[PROPERTY_VALUE_MAX];
	char *logfilename;

	pthread_mutex_init(&persist_buf.mut, 0);
	pthread_mutex_lock(&persist_buf.mut);
	/*
	 * Get build type by reading the target properties,
	 * if buuid type is eng or userdebug allocate 1 MB persist buf.
	 */
	if(fastrpc_get_property_string(FASTRPC_BUILD_TYPE, build_type, NULL)) {
#if !defined(LE_ENABLE)
		if(!strncmp(build_type, "eng", PROPERTY_VALUE_MAX)
		   || !strncmp(build_type, "userdebug", PROPERTY_VALUE_MAX))
			debug_build_type = true;
#else
		if(atoi(build_type))
			debug_build_type = true;
#endif
	}
	if(persist_buf.buf == NULL && debug_build_type) {
		/* Create a debug buffer to append DEBUG FARF level message. */
		persist_buf.buf = (char *)rpcmem_alloc_internal(
		    RPCMEM_HEAP_ID_SYSTEM,
		    RPCMEM_DEFAULT_FLAGS | RPCMEM_TRY_MAP_STATIC,
		    DEBUG_BUF_SIZE * sizeof(char));
		if(persist_buf.buf) {
			fd = rpcmem_to_fd(persist_buf.buf);
			FARF(RUNTIME_RPC_HIGH,
			     "%s: persist_buf.buf created %d size %d",
			     __func__, fd, DEBUG_BUF_SIZE);
			/* Appending header to persist buffer, to identify the
			 * start address through script. */
			strlcpy(persist_buf.buf, DEBUF_BUF_TRACE,
			        DEBUG_BUF_SIZE);
			persist_buf.size = strlen(DEBUF_BUF_TRACE) + 1;
		} else {
			nErr = AEE_ENORPCMEMORY;
			FARF(ERROR,
			     "Error 0x%x: %s allocation failed for "
			     "persist_buf of size %d",
			     nErr, __func__, DEBUG_BUF_SIZE);
		}
	}
	pthread_mutex_unlock(&persist_buf.mut);
	logfilename = fastrpc_config_get_userspace_runtime_farf_file();
	if(logfilename) {
		log_userspace_file_fd = fopen(logfilename, "w");
		if(log_userspace_file_fd == NULL) {
			VERIFY_EPRINTF("Error 0x%x: %s failed to collect "
			               "userspace runtime farf "
			               "logs into file %s with errno %s\n",
			               nErr, __func__, logfilename,
			               strerror(errno));
		} else {
			FARF(RUNTIME_RPC_HIGH, "%s done\n", __func__);
		}
	}
}

void
fastrpc_log_deinit()
{

	pthread_mutex_lock(&persist_buf.mut);
	if(persist_buf.buf) {
		rpcmem_free(persist_buf.buf);
		persist_buf.buf = NULL;
	}
	pthread_mutex_unlock(&persist_buf.mut);
	if(log_userspace_file_fd) {
		fclose(log_userspace_file_fd);
		log_userspace_file_fd = NULL;
	}
	pthread_mutex_destroy(&persist_buf.mut);
}
