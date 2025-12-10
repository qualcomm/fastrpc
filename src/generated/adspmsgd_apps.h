// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef _ADSPMSGD_APPS_H
#define _ADSPMSGD_APPS_H
#include <AEEStdDef.h>
#include <remote.h>

#ifndef __QAIC_HEADER
#define __QAIC_HEADER(ff) ff
#endif //__QAIC_HEADER

#ifndef __QAIC_HEADER_EXPORT
#define __QAIC_HEADER_EXPORT
#endif // __QAIC_HEADER_EXPORT

#ifndef __QAIC_HEADER_ATTRIBUTE
#define __QAIC_HEADER_ATTRIBUTE
#endif // __QAIC_HEADER_ATTRIBUTE

#ifndef __QAIC_IMPL
#define __QAIC_IMPL(ff) ff
#endif //__QAIC_IMPL

#ifndef __QAIC_IMPL_EXPORT
#define __QAIC_IMPL_EXPORT
#endif // __QAIC_IMPL_EXPORT

#ifndef __QAIC_IMPL_ATTRIBUTE
#define __QAIC_IMPL_ATTRIBUTE
#endif // __QAIC_IMPL_ATTRIBUTE
#ifdef __cplusplus
extern "C" {
#endif
enum adspmsgd_apps_Level {
   LOW,
   MEDIUM,
   HIGH,
   ERROR,
   FATAL,
   _32BIT_PLACEHOLDER_adspmsgd_apps_Level = 0x7fffffff
};
typedef enum adspmsgd_apps_Level adspmsgd_apps_Level;
typedef struct _adspmsgd_apps_octetSeq__seq_octet _adspmsgd_apps_octetSeq__seq_octet;
typedef _adspmsgd_apps_octetSeq__seq_octet adspmsgd_apps_octetSeq;
struct _adspmsgd_apps_octetSeq__seq_octet {
   unsigned char* data;
   int dataLen;
};
__QAIC_HEADER_EXPORT int __QAIC_HEADER(adspmsgd_apps_log)(const unsigned char* log_message_buffer, int log_message_bufferLen) __QAIC_HEADER_ATTRIBUTE;
#ifdef __cplusplus
}
#endif
#endif //_ADSPMSGD_APPS_H
