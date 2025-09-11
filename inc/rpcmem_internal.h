// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef __RPCMEM_INTERNAL_H__
#define __RPCMEM_INTERNAL_H__

#include "rpcmem.h"

/*
 * rpcmem_set_dmabuf_name() - API to set name for DMA allocated buffer.
 *                            Name string updates as follows.
 *                            Heap : dsp_<pid>_<tid>_<remote-flags>
 *                            Non-heap : apps_<pid>_<tid>_<rpcflags>
 *
 * @name    : Pointer to string, "dsp" for heap buffers, "apps" for
 *            non-heap buffers
 * @fd      : File descriptor of buffer
 * @heapid  : Heap ID used for memory allocation
 * @buf     : Pointer to buffer start address
 * @rpcflags: Memory flags describing attributes of allocation
 * Return   : 0 on success, valid non-zero error code on failure
 */
int rpcmem_set_dmabuf_name(const char *name, int fd, int heapid, void *buf,
                           uint32_t rpc_flags);
/*
 * returns an file descriptor associated with the address
 */
int rpcmem_to_fd_internal(void *po);
/*
 * allocates dma memory of size, from specific heap mentioned in heapid.
 * flags are not used for now
 */
void *rpcmem_alloc_internal(int heapid, uint32_t flags, size_t size);
/*
 * frees the allocated memory from dma
 */
void rpcmem_free_internal(void *po);

#endif /*__RPCMEM_INTERNAL_H__*/
