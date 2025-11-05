// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef _DSPQUEUE_RPC_H
#define _DSPQUEUE_RPC_H
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
/**
    * Opens the handle in the specified domain.  If this is the first
    * handle, this creates the session.  Typically this means opening
    * the device, aka open("/dev/adsprpc-smd"), then calling ioctl
    * device APIs to create a PD on the DSP to execute our code in,
    * then asking that PD to dlopen the .so and dlsym the skel function.
    *
    * @param uri, <interface>_URI"&_dom=aDSP"
    *    <interface>_URI is a QAIC generated uri, or
    *    "file:///<sofilename>?<interface>_skel_handle_invoke&_modver=1.0"
    *    If the _dom parameter is not present, _dom=DEFAULT is assumed
    *    but not forwarded.
    *    Reserved uri keys:
    *      [0]: first unamed argument is the skel invoke function
    *      _dom: execution domain name, _dom=mDSP/aDSP/DEFAULT
    *      _modver: module version, _modver=1.0
    *      _*: any other key name starting with an _ is reserved
    *    Unknown uri keys/values are forwarded as is.
    * @param h, resulting handle
    * @retval, 0 on success
    */
__QAIC_HEADER_EXPORT int __QAIC_HEADER(dspqueue_rpc_open)(const char* uri, remote_handle64* h) __QAIC_HEADER_ATTRIBUTE;
/** 
    * Closes a handle.  If this is the last handle to close, the session
    * is closed as well, releasing all the allocated resources.

    * @param h, the handle to close
    * @retval, 0 on success, should always succeed
    */
__QAIC_HEADER_EXPORT int __QAIC_HEADER(dspqueue_rpc_close)(remote_handle64 h) __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT AEEResult __QAIC_HEADER(dspqueue_rpc_init_process_state)(remote_handle64 _h, int32_t process_state_fd) __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT AEEResult __QAIC_HEADER(dspqueue_rpc_create_queue)(remote_handle64 _h, uint32_t id, int32_t queue_fd, uint32_t count, uint64_t* queue_id) __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT AEEResult __QAIC_HEADER(dspqueue_rpc_destroy_queue)(remote_handle64 _h, uint64_t queue_id) __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT AEEResult __QAIC_HEADER(dspqueue_rpc_is_imported)(remote_handle64 _h, uint64_t queue_id, int32_t* imported) __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT AEEResult __QAIC_HEADER(dspqueue_rpc_wait_signal)(remote_handle64 _h, int32_t* signal) __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT AEEResult __QAIC_HEADER(dspqueue_rpc_cancel_wait_signal)(remote_handle64 _h) __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT AEEResult __QAIC_HEADER(dspqueue_rpc_signal)(remote_handle64 _h) __QAIC_HEADER_ATTRIBUTE;
#ifndef dspqueue_rpc_URI
#define dspqueue_rpc_URI "file:///libdspqueue_rpc_skel.so?dspqueue_rpc_skel_handle_invoke&_modver=1.0"
#endif /*dspqueue_rpc_URI*/
#ifdef __cplusplus
}
#endif
#endif //_DSPQUEUE_RPC_H
