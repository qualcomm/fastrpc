// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause
#ifndef _RPCMEM_PRIME_H
#define _RPCMEM_PRIME_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * DOC: RPCMEM PRIME Buffer Sharing API
 *
 * This header provides functions for sharing rpcmem buffers between different
 * processes or file descriptor contexts using the DRM PRIME mechanism.
 * 
 * The PRIME mechanism allows GEM handles to be exported as DMA-BUF file
 * descriptors that can be shared across process boundaries and then imported
 * back as GEM handles in the target process.
 */

/**
 * rpcmem_export_handle_to_fd - Export GEM handle to shareable DMA-BUF fd
 * @handle: GEM handle to export
 * @fd_out: Output parameter for the DMA-BUF file descriptor
 * 
 * This function exports a GEM handle to a DMA-BUF file descriptor that can
 * be shared between different processes or file descriptors.
 * 
 * Returns: 0 on success, negative error code on failure
 */
int rpcmem_export_handle_to_fd(int handle, int *fd_out);

/**
 * rpcmem_import_fd_to_handle - Import DMA-BUF fd to GEM handle in current context
 * @fd: DMA-BUF file descriptor to import
 * @handle_out: Output parameter for the GEM handle
 * 
 * This function imports a DMA-BUF file descriptor to a GEM handle that is
 * valid in the current file descriptor context.
 * 
 * Returns: 0 on success, negative error code on failure
 */
int rpcmem_import_fd_to_handle(int fd, int *handle_out);

/**
 * rpcmem_share_buffer - Share an rpcmem buffer with another process/context
 * @buf: Buffer pointer returned by rpcmem_alloc
 * @fd_out: Output parameter for shareable DMA-BUF file descriptor
 * 
 * This function takes an rpcmem buffer and exports its underlying GEM handle
 * to a DMA-BUF file descriptor that can be shared across processes.
 * 
 * Returns: 0 on success, negative error code on failure
 */
int rpcmem_share_buffer(void *buf, int *fd_out);

/**
 * rpcmem_import_shared_buffer - Import a shared buffer from DMA-BUF fd
 * @fd: DMA-BUF file descriptor from another process
 * @size: Size of the buffer for mapping
 * @buf_out: Output parameter for the mapped buffer pointer
 * 
 * This function imports a DMA-BUF file descriptor from another process and
 * maps it to a buffer pointer that can be used with FastRPC.
 * 
 * Returns: 0 on success, negative error code on failure
 */
int rpcmem_import_shared_buffer(int fd, size_t size, void **buf_out);

#ifdef __cplusplus
}
#endif

#endif /* _RPCMEM_PRIME_H */
