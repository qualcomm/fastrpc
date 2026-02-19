// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef __RPCMEM_INTERNAL_H__
#define __RPCMEM_INTERNAL_H__

#include "rpcmem.h"

/*
 * returns an file descriptor associated with the address
 */
int rpcmem_to_fd_internal(void *po);

/*
 * returns a unique handle associated with the address
 */
int rpcmem_to_handle_internal(void *po);
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
