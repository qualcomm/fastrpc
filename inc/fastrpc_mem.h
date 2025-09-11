// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef FASTRPC_MEM_H
#define FASTRPC_MEM_H

/**
 * Function to initialize fastrpc_mem module. Call only once during library
 * initialization.
 */
int fastrpc_mem_init(void);

/**
 * Function to deinitialize fastrpc_mem module. Call only once while closing
 * library.
 */
int fastrpc_mem_deinit(void);

/**
 * fastrpc_mem_open() initializes fastrpc_mem module data of a fastrpc session
 * associated with domain. Call once during session open for a domain.
 */
int fastrpc_mem_open(int domain);

/**
 * fastrpc_mem_close() deinitializes fastrpc_mem module data of a fastrpc
 * session associated with domain. Call once during session close for a domain.
 */
int fastrpc_mem_close(int domain);

/*
 * internal API to unregister the dma handles already registerd by clients.
 * should not be called by clients. Used internally for work around purpose.
 */
void unregister_dma_handle(int fd, uint32_t *len, uint32_t *attr);
/*
 * returns a list of FDs registered by the clients.
 * used while making a remote call.
 */
int fdlist_fd_from_buf(void *buf, int bufLen, int *nova, void **base,
                       int *attr, int *ofd);

int remote_mmap64_internal(int fd, uint32_t flags, uint64_t vaddrin,
                           int64_t size, uint64_t *vaddrout);

/*
 * Get information about an existing mapped buffer, optionally
 * incrementing/decrementing its reference count.
 *
 * @param domain The DSP being used
 * @param fd Buffer file descriptor in current process
 * @param ref 1 to increment reference count, -1 to decrement it, 0 to keep
 * refcount unchanged
 * @param va Output: Pointer to buffer virtual address in current process. Set
 * to NULL if the buffer doesn't have a valid accesible mapping.
 * @param size Output: Buffer size in bytes
 *
 * @return 0 on success, error code on failure.
 *         - AEE_ENOSUCHMAP: Unknown FD
 */
int fastrpc_buffer_ref(int domain, int fd, int ref, void **va, size_t *size);

/*
 * Register or deregister a buffer with fastrpc framework, managing its
 reference count
 * and associated metadata.
 *
 * @param buf Pointer to the buffer to be registered or deregistered
 * @param size Size of the buffer in bytes
 * @param fd File descriptor associated with the buffer; use -1 to deregister

 * This function internally calls `remote_register_buf_common` with default
 attributes.
 * - If `fd` is not -1, the buffer is registered and its reference count is
 incremented.
 * - If `fd` is -1, the buffer is deregistered and its reference count is
 decremented.
 *   If the reference count reaches zero, the buffer is unmapped and its
 metadata is freed.
 *
 * @return void
 */
void remote_register_buf(void *buf, int size, int fd);

#endif // FASTRPC_MEM_H
