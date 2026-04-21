// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef FASTRPC_MEM_H
#define FASTRPC_MEM_H

/**
 * Function to initialize fastrpc_mem module. Call only once during library initialization.
 */
int fastrpc_mem_init(void);

/**
 * Function to deinitialize fastrpc_mem module. Call only once while closing library.
 */
int fastrpc_mem_deinit(void);

/**
 * fastrpc_mem_open() initializes fastrpc_mem module data of a fastrpc session associated with domain.
 * Call once during session open for a domain.
 */
int fastrpc_mem_open(int domain);

/**
 * fastrpc_mem_close() deinitializes fastrpc_mem module data of a fastrpc session associated with domain.
 * Call once during session close for a domain.
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
int fdlist_fd_from_buf(void* buf, int bufLen, int* nova, void** base, int* attr, int* ofd);

int remote_mmap64_internal(int fd, uint32_t flags, uint64_t vaddrin, int64_t size, uint64_t* vaddrout);

/*
 * Get information about an existing mapped buffer, optionally incrementing/decrementing its
 * reference count.
 *
 * @param domain The DSP being used
 * @param fd Buffer file descriptor in current process
 * @param ref 1 to increment reference count, -1 to decrement it, 0 to keep refcount unchanged
 * @param va Output: Pointer to buffer virtual address in current process. Set to NULL if the buffer
 *           doesn't have a valid accesible mapping.
 * @param size Output: Buffer size in bytes
 *
 * @return 0 on success, error code on failure.
 *         - AEE_ENOSUCHMAP: Unknown FD
*/
int fastrpc_buffer_ref(int domain, int fd, int ref, void **va, size_t *size);


/*
 * Register or deregister a buffer with fastrpc framework, managing its reference count
 * and associated metadata.
 *
 * @param buf Pointer to the buffer to be registered or deregistered
 * @param size Size of the buffer in bytes
 * @param fd File descriptor associated with the buffer; use -1 to deregister

 * This function internally calls `remote_register_buf_common` with default attributes.
 * - If `fd` is not -1, the buffer is registered and its reference count is incremented.
 * - If `fd` is -1, the buffer is deregistered and its reference count is decremented.
 *   If the reference count reaches zero, the buffer is unmapped and its metadata is freed.
 *
 * @return void
 */
void remote_register_buf(void *buf, int size, int fd);

/*
 * Internal function to map a buffer and return the DSP virtual address.
 * Creates a mapping on the DSP and stores the mapping information in the static map list.
 *
 * @param domain The DSP domain ID (-1 for current domain)
 * @param fd File descriptor of the buffer
 * @param vaddr Virtual address of the buffer on CPU side
 * @param offset Offset from the beginning of the buffer (must be 0)
 * @param length Size of buffer in bytes
 * @param flags Mapping flags
 * @param raddr Output: DSP virtual address of the mapped buffer
 *
 * @return 0 on success, error code on failure
 */
int fastrpc_mmap_internal(int domain, int fd, void *vaddr, int offset, size_t length, uint32_t flags, uint64_t *raddr);

/*
 * Internal function to unmap a buffer using the DSP virtual address.
 * Looks up the mapping in the static map list using the DSP virtual address,
 * then performs the unmap operation and removes the mapping from the list.
 *
 * @param domain The DSP domain ID (-1 for current domain)
 * @param raddr DSP virtual address of the mapped buffer
 * @param length Size of buffer in bytes to unmap
 *
 * @return 0 on success, error code on failure
 */
int fastrpc_munmap_internal(int domain, uint64_t raddr, size_t length);

#define ADSP_MMAP_HEAP_ADDR 4
#define ADSP_MMAP_REMOTE_HEAP_ADDR 8
#define ADSP_MMAP_ADD_PAGES 0x1000
#define ADSP_MMAP_ADD_PAGES_LLC 0x3000
#define FASTRPC_ALLOC_HLOS_FD                                                  \
  0x10000 /* Flag to allocate HLOS FD to be shared with DSP */

#endif //FASTRPC_MEM_H
