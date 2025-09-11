// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef FASTRPC_TRACE_H
#define FASTRPC_TRACE_H

#if ((defined _ANDROID) || (defined ANDROID))                                 \
    || (defined DISABLE_ATRACE) && !defined(LE_ENABLE)
// TODO: Bharath #include "cutils/trace.h" //for systrace support
#endif
#include "HAP_farf.h"

// for systrace support
#ifdef ATRACE_TAG
#undef ATRACE_TAG
#endif
#define ATRACE_TAG (ATRACE_TAG_POWER | ATRACE_TAG_HAL)

/* trace length for ATRACE detailed ATRACE string */
#define SYSTRACE_STR_LEN 100

#if (defined(LE_ENABLE) || defined(__LE_TVM__) || defined(DISABLE_ATRACE))
#define FASTRPC_ATRACE_BEGIN_L(fmt, ...) (void)0
#define FASTRPC_ATRACE_END_L(fmt, ...) (void)0
#define FASTRPC_ATRACE_BEGIN() (void)0
#define FASTRPC_ATRACE_END() (void)0
#else
#define FASTRPC_ATRACE_BEGIN_L(fmt, ...)                                      \
	if(is_systrace_enabled()) {                                           \
		char systrace_string[SYSTRACE_STR_LEN] = { 0 };               \
		FARF(RUNTIME_RPC_HIGH, fmt, ##__VA_ARGS__);                   \
		snprintf(systrace_string, SYSTRACE_STR_LEN, fmt,              \
		         ##__VA_ARGS__);                                      \
		ATRACE_BEGIN(systrace_string);                                \
	} else                                                                \
		(void)0
#define FASTRPC_ATRACE_END_L(fmt, ...)                                        \
	if(is_systrace_enabled()) {                                           \
		FARF(ALWAYS, fmt, ##__VA_ARGS__);                             \
		ATRACE_END();                                                 \
	} else {                                                              \
		FARF(RUNTIME_RPC_CRITICAL, fmt, ##__VA_ARGS__);               \
	}
#define FASTRPC_ATRACE_BEGIN()                                                \
	if(is_systrace_enabled()) {                                           \
		FARF(ALWAYS, "%s begin", __func__);                           \
		ATRACE_BEGIN(__func__);                                       \
	} else                                                                \
		(void)0
#define FASTRPC_ATRACE_END()                                                  \
	if(is_systrace_enabled()) {                                           \
		FARF(ALWAYS, "%s end", __func__);                             \
		ATRACE_END();                                                 \
	} else {                                                              \
		FARF(RUNTIME_RPC_CRITICAL, "%s end", __func__);               \
	}
#endif

// API to get Systrace variable
int is_systrace_enabled(void);

#endif // FASTRPC_TRACE_H
