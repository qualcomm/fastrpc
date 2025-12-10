// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef _ADSP_LISTENER_H
#define _ADSP_LISTENER_H
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
#define _const_adsp_listener_handle 3
typedef struct _adsp_listener_buffer__seq_uint8 _adsp_listener_buffer__seq_uint8;
typedef _adsp_listener_buffer__seq_uint8 adsp_listener_buffer;
struct _adsp_listener_buffer__seq_uint8 {
   uint8_t* data;
   int dataLen;
};
typedef uint32_t adsp_listener_remote_handle;
typedef uint32_t adsp_listener_invoke_ctx;
__QAIC_HEADER_EXPORT int __QAIC_HEADER(adsp_listener_next_invoke)(adsp_listener_invoke_ctx prevCtx, int prevResult, const adsp_listener_buffer* outBufs, int outBufsLen, adsp_listener_invoke_ctx* ctx, adsp_listener_remote_handle* handle, uint32_t* sc, adsp_listener_buffer* inBuffers, int inBuffersLen, int* inBufLenReq, int inBufLenReqLen, int* routBufLenReq, int routBufLenReqLen) __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT int __QAIC_HEADER(adsp_listener_invoke_get_in_bufs)(adsp_listener_invoke_ctx ctx, adsp_listener_buffer* inBuffers, int inBuffersLen) __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT int __QAIC_HEADER(adsp_listener_init)(void) __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT int __QAIC_HEADER(adsp_listener_init2)(void) __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT int __QAIC_HEADER(adsp_listener_next2)(adsp_listener_invoke_ctx prevCtx, int prevResult, const uint8_t* prevbufs, int prevbufsLen, adsp_listener_invoke_ctx* ctx, adsp_listener_remote_handle* handle, uint32_t* sc, uint8_t* bufs, int bufsLen, int* bufsLenReq) __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT int __QAIC_HEADER(adsp_listener_get_in_bufs2)(adsp_listener_invoke_ctx ctx, int offset, uint8_t* bufs, int bufsLen, int* bufsLenReq) __QAIC_HEADER_ATTRIBUTE;
#ifdef __cplusplus
}
#endif
#endif //_ADSP_LISTENER_H
