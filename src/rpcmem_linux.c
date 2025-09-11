/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *    * Neither the name of The Linux Foundation nor the names of its
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define FARF_LOW 1

#include <errno.h>
#include <fcntl.h>
#include <linux/types.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include "AEEQList.h"
#include "AEEStdErr.h"
#include "AEEstd.h"
#include "HAP_farf.h"
#include "apps_std.h"
#include "fastrpc_common.h"
#include "fastrpc_ioctl.h"
#include "fastrpc_mem.h"
#include "rpcmem.h"
#include "verify.h"

#define PAGE_SIZE 4096

#ifndef PAGE_MASK
#define PAGE_MASK ~((uintptr_t)PAGE_SIZE - 1)
#endif

struct dma_heap_allocation_data {
	__u64 len;
	__u32 fd;
	__u32 fd_flags;
	__u64 heap_flags;
};

#define DMA_HEAP_IOC_MAGIC 'H'
#define DMA_HEAP_IOCTL_ALLOC                                                  \
	_IOWR(DMA_HEAP_IOC_MAGIC, 0x0, struct dma_heap_allocation_data)
#define DMA_HEAP_NAME "/dev/dma_heap/system"
static int dmafd = -1;
static int rpcfd = -1;
static QList rpclst;
static pthread_mutex_t rpcmt;
struct rpc_info {
	QNode qn;
	void *buf;
	void *aligned_buf;
	int size;
	int fd;
	int dma;
};

struct fastrpc_alloc_dma_buf {
	int fd;         /* fd */
	uint32_t flags; /* flags to map with */
	uint64_t size;  /* size */
};

void
rpcmem_init()
{
	QList_Ctor(&rpclst);
	pthread_mutex_init(&rpcmt, 0);
	pthread_mutex_lock(&rpcmt);

	dmafd = open(DMA_HEAP_NAME, O_RDONLY | O_CLOEXEC);
	if(dmafd < 0) {
		FARF(ALWAYS,
		     "Warning %d: Unable to open %s, falling back to fastrpc "
		     "ioctl\n",
		     errno, DMA_HEAP_NAME);
		/*
		 * Application should link proper library as DEFAULT_DOMAIN_ID
		 * is used to open rpc device node and not the uri passed by
		 * user.
		 */
		rpcfd = open_device_node(DEFAULT_DOMAIN_ID);
		if(rpcfd < 0)
			FARF(ALWAYS,
			     "Warning %d: Unable to open fastrpc dev node for "
			     "domain: %d\n",
			     errno, DEFAULT_DOMAIN_ID);
	}
	pthread_mutex_unlock(&rpcmt);
}

void
rpcmem_deinit()
{
	pthread_mutex_lock(&rpcmt);
	if(dmafd != -1)
		close(dmafd);
	if(rpcfd != -1)
		close(rpcfd);
	pthread_mutex_unlock(&rpcmt);
	pthread_mutex_destroy(&rpcmt);
}

int
rpcmem_set_dmabuf_name(const char *name, int fd, int heapid, void *buf,
                       uint32_t rpcflags)
{
	// Dummy call where DMABUF is not used
	return 0;
}

int
rpcmem_to_fd_internal(void *po)
{
	struct rpc_info *rinfo, *rfree = 0;
	QNode *pn, *pnn;

	pthread_mutex_lock(&rpcmt);
	QLIST_NEXTSAFE_FOR_ALL(&rpclst, pn, pnn)
	{
		rinfo = STD_RECOVER_REC(struct rpc_info, qn, pn);
		if(rinfo->aligned_buf == po) {
			rfree = rinfo;
			break;
		}
	}
	pthread_mutex_unlock(&rpcmt);

	if(rfree)
		return rfree->fd;

	return -1;
}

int
rpcmem_to_fd(void *po)
{
	return rpcmem_to_fd_internal(po);
}

void *
rpcmem_alloc_internal(int heapid, uint32_t flags, size_t size)
{
	struct rpc_info *rinfo;
	int nErr = 0, fd = -1;
	struct dma_heap_allocation_data dmabuf = {
		.len = size,
		.fd_flags = O_RDWR | O_CLOEXEC,
	};

	if((dmafd == -1 && rpcfd == -1) || size <= 0) {
		FARF(ERROR,
		     "Error: Unable to allocate memory dmaheap fd %d, rpcfd "
		     "%d, size "
		     "%zu, flags %u",
		     dmafd, rpcfd, size, flags);
		return NULL;
	}

	VERIFY(0 != (rinfo = calloc(1, sizeof(*rinfo))));

	if(dmafd != -1) {
		nErr = ioctl(dmafd, DMA_HEAP_IOCTL_ALLOC, &dmabuf);
		if(nErr) {
			FARF(ERROR,
			     "Error %d: Unable to allocate memory dmaheap fd "
			     "%d, heapid %d, size "
			     "%zu, flags %u",
			     errno, dmafd, heapid, size, flags);
			goto bail;
		}
		fd = dmabuf.fd;
	} else {
		struct fastrpc_ioctl_alloc_dma_buf buf;

		buf.size = size + PAGE_SIZE;
		buf.fd = -1;
		buf.flags = 0;

		nErr = ioctl(rpcfd, FASTRPC_IOCTL_ALLOC_DMA_BUFF,
		             (unsigned long)&buf);
		if(nErr) {
			FARF(ERROR,
			     "Error %d: Unable to allocate memory fastrpc fd "
			     "%d, heapid %d, size "
			     "%zu, flags %u",
			     errno, rpcfd, heapid, size, flags);
			goto bail;
		}
		fd = buf.fd;
	}
	VERIFY(0
	       != (rinfo->buf = mmap(0, size, PROT_READ | PROT_WRITE,
	                             MAP_SHARED, fd, 0)));
	rinfo->fd = fd;
	rinfo->aligned_buf
	    = (void *)(((uintptr_t)rinfo->buf /*+ PAGE_SIZE*/) & PAGE_MASK);
	rinfo->aligned_buf = rinfo->buf;
	rinfo->size = size;
	pthread_mutex_lock(&rpcmt);
	QList_AppendNode(&rpclst, &rinfo->qn);
	pthread_mutex_unlock(&rpcmt);
	FARF(RUNTIME_RPC_HIGH,
	     "Allocted memory from DMA heap fd %d ptr %p orig ptr %p\n",
	     rinfo->fd, rinfo->aligned_buf, rinfo->buf);
	remote_register_buf(rinfo->buf, rinfo->size, rinfo->fd);
	return rinfo->aligned_buf;
bail:
	if(nErr) {
		if(rinfo) {
			if(rinfo->buf) {
				free(rinfo->buf);
			}
			free(rinfo);
		}
	}
	return NULL;
}

void
rpcmem_free_internal(void *po)
{
	struct rpc_info *rinfo, *rfree = 0;
	QNode *pn, *pnn;

	pthread_mutex_lock(&rpcmt);
	QLIST_NEXTSAFE_FOR_ALL(&rpclst, pn, pnn)
	{
		rinfo = STD_RECOVER_REC(struct rpc_info, qn, pn);
		if(rinfo->aligned_buf == po) {
			rfree = rinfo;
			QNode_Dequeue(&rinfo->qn);
			break;
		}
	}
	pthread_mutex_unlock(&rpcmt);
	if(rfree) {
		remote_register_buf(rfree->buf, rfree->size, -1);
		munmap(rfree->buf, rfree->size);
		close(rfree->fd);
		free(rfree);
	}
	return;
}

void
rpcmem_free(void *po)
{
	rpcmem_free_internal(po);
}

void *
rpcmem_alloc(int heapid, uint32_t flags, int size)
{
	return rpcmem_alloc_internal(heapid, flags, size);
}

void
rpcmem_deinit_internal()
{
	rpcmem_deinit();
}

void
rpcmem_init_internal()
{
	rpcmem_init();
}
