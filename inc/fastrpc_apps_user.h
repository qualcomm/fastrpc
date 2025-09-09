// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef FASTRPC_USER_H
#define FASTRPC_USER_H

#include <assert.h>
#include <fcntl.h>
#include <asm/ioctl.h>
#include <errno.h>
#include <sys/time.h>
#include <stdbool.h>

#include "remote.h"
#include "fastrpc_common.h"

#ifdef __LE_TVM__
#define __CONSTRUCTOR_ATTRIBUTE__
#else
#define __CONSTRUCTOR_ATTRIBUTE__ __attribute__((constructor))
#endif

/* Verify if the handle is configured for staticPD. */
#define IS_STATICPD_HANDLE(h) ((h > 0xff) && (h <= 0x200))

/*
 * Enum defined for static PD handles.
 * The range for constant handles is <0,255>,
 * and the range for staticPD handles is <256,512>
 */
typedef enum {
	 OISPD_HANDLE			=	256,
	 AUDIOPD_HANDLE			=	257,
	 SENSORPD_HANDLE		=	258,
	 ATTACHGUESTOS_HANDLE		=	259,
	 ROOTPD_HANDLE			=	260,
	 SECUREPD_HANDLE		=	261
 } static_pd_handle;
/*
 * API to initialize rpcmem data structures for ION allocation
 */
int rpcmem_init_internal();

/*
 * API to clean-up rpcmem data structures
 */
void rpcmem_deinit_internal();

/*
 * API to allocate ION memory for internal purposes
 * Returns NULL if allocation fails
 *
 */
void* rpcmem_alloc_internal(int heapid, uint32_t flags, size_t size);

/*
 * API to free internally allocated ION memory
 *
 */
void rpcmem_free_internal(void* po);

/*
 * API to get fd of internally allocated ION buffer
 * Returns valid fd on success and -1 on failure
 *
 */
int rpcmem_to_fd_internal(void *po);

// API to get domain from handle
int get_domain_from_handle(remote_handle64 local, int *domain);


/* fastrpc initialization function to call from global functions exported to user */
int __CONSTRUCTOR_ATTRIBUTE__ fastrpc_init_once(void);

/* Utility function to find session opened or not for a given domain
 * @param domain: DSP domain ID
 * @return TRUE if session opened, FALSE otherwise
 */
int is_session_opened(int domain);

/* Utility function to get device file descriptor of a DSP domain
 * @param domain: DSP domain ID
 * @return -1 if device not opened and file descriptor if opened
 */
int get_device_fd(int domain);

/* Utility function to get default or current domain set in tlsKey
 * @return domain id on success, -1 on failure
 */
int get_current_domain(void);

/* Utility function to get state of logger for DSP domain
 * @param domain : DSP domain Id
 * @return 0 on success, valid non-zero error code on failure
 */
int get_logger_state(int domain);

/* Utility function to open a fastrpc session
 * @param domain: DSP domain id
 * @param dev[out]: device id
 * @return 0 on success, error codes on failure
 */
int fastrpc_session_open(int domain, int *dev);

/* Lock and unlock fastrpc session handler */
void fastrpc_session_lock(int domain);
void fastrpc_session_unlock(int domain);

/*
 * API to close reverse handles
 * @handle: handle to close
 * @errStr: Error String (if an error occurs)
 * @errStrLen: Length of error String (if an error occurs)
 * @pdlErr: Error identifier
 * @returns: 0 on success, valid non-zero error code on failure
 *
 */
int close_reverse_handle(remote_handle64 handle, char* errStr, int errStrLen, int* pdlErr);


/*
 * API to get unsigned PD attribute for a given domain
 * @domain: domain id
 * @unsigned_module: PD Attribute for unsigned module
 * @returns: 0 on success, valid non-zero error code on failure
 */
int get_unsigned_pd_attribute(uint32_t domain, int *unsigned_module);

/*
 * Check whether userspace memory allocation is supported or not.
 * returns: 1 if capability supported, 0 otherwise
 *
 */
int is_userspace_allocation_supported();

#endif //FASTRPC_USER_H
