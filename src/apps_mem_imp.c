// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef VERIFY_PRINT_ERROR
#define VERIFY_PRINT_ERROR
#endif /* VERIFY_PRINT_ERROR */

#include "AEEQList.h"
#include "AEEStdErr.h"
#include "AEEstd.h"
#include "apps_mem.h"
#include "fastrpc_apps_user.h"
#include "fastrpc_cap.h"
#include "fastrpc_common.h"
#include "fastrpc_hash_table.h"
#include "fastrpc_mem.h"
#include "fastrpc_trace.h"
#include "remote64.h"
#include "rpcmem_internal.h"
#include "verify.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#define ADSP_MMAP_HEAP_ADDR 4
#define ADSP_MMAP_REMOTE_HEAP_ADDR 8
#define ADSP_MMAP_ADD_PAGES 0x1000
#define ADSP_MMAP_ADD_PAGES_LLC 0x3000
#define FASTRPC_ALLOC_HLOS_FD                                                 \
	0x10000 /* Flag to allocate HLOS FD to be shared with DSP */

typedef struct {
	QList mem_list;
	pthread_mutex_t mem_mut;
	int init;
	ADD_DOMAIN_HASH();
} apps_mem_info;

DECLARE_HASH_TABLE(apps_mem, apps_mem_info);

struct mem_info {
	QNode qn;
	uint64_t vapps;
	uint64_t vadsp;
	int32_t size;
	int32_t mapped;
	uint32_t rflags;
};

/* Delete and free all nodes in hash-table */
void
apps_mem_table_deinit(void)
{
	HASH_TABLE_CLEANUP(apps_mem_info);
}

/* Initialize hash-table */
int
apps_mem_table_init(void)
{
	HASH_TABLE_INIT(apps_mem_info);
	return 0;
}

/*
These should be called in some static constructor of the .so that
uses rpcmem.

I moved them into fastrpc_apps_user.c because there is no gurantee in
the order of when constructors are called.
*/

int
apps_mem_init(int domain)
{
	int nErr = AEE_SUCCESS;
	apps_mem_info *me = NULL;

	GET_HASH_NODE(apps_mem_info, domain, me);
	if(!me) {
		ALLOC_AND_ADD_NEW_NODE_TO_TABLE(apps_mem_info, domain, me);
	}
	QList_Ctor(&me->mem_list);
	pthread_mutex_init(&me->mem_mut, 0);
	me->init = 1;
bail:
	return nErr;
}

void
apps_mem_deinit(int domain)
{
	QNode *pn;
	apps_mem_info *me = NULL;

	GET_HASH_NODE(apps_mem_info, domain, me);
	if(!me) {
		FARF(ALWAYS,
		     "Warning: %s: unable to find hash-node for domain %d",
		     __func__, domain);
		return;
	}

	if(me->init) {
		while((pn = QList_PopZ(&me->mem_list)) != NULL) {
			struct mem_info *mfree
			    = STD_RECOVER_REC(struct mem_info, qn, pn);
			if(mfree->vapps) {
				if(mfree->mapped) {
					munmap((void *)(uintptr_t)mfree->vapps,
					       mfree->size);
				} else {
					rpcmem_free_internal(
					    (void *)(uintptr_t)mfree->vapps);
				}
			}
			free(mfree);
			mfree = NULL;
		}
		pthread_mutex_destroy(&me->mem_mut);
		me->init = 0;
	}
}

__QAIC_IMPL_EXPORT int
__QAIC_IMPL(apps_mem_request_map64)(int heapid, uint32_t lflags,
                                    uint32_t rflags, uint64_t vin, int64_t len,
                                    uint64_t *vapps,
                                    uint64_t *vadsp) __QAIC_IMPL_ATTRIBUTE
{
	struct mem_info *minfo = 0;
	int nErr = 0, unsigned_module = 0, ualloc_support = 0;
	void *buf = 0;
	uint64_t pbuf;
	int fd = -1;
	int domain = get_current_domain();
	apps_mem_info *me = NULL;

	GET_HASH_NODE(apps_mem_info, domain, me);
	VERIFYC(me, AEE_ERESOURCENOTFOUND);
	VERIFY(
	    AEE_SUCCESS
	    == (nErr = get_unsigned_pd_attribute(domain, &unsigned_module)));
	FASTRPC_ATRACE_BEGIN_L(
	    "%s called with rflag 0x%x, lflags 0x%x, len 0x%llx, "
	    "heapid %d and unsigned PD %d",
	    __func__, rflags, lflags, len, heapid, unsigned_module);
	if(unsigned_module) {
		ualloc_support = is_userspace_allocation_supported();
	}
	(void)vin;
	VERIFYC(len >= 0, AEE_EBADPARM);
	VERIFYC(NULL != (minfo = malloc(sizeof(*minfo))), AEE_ENOMEMORY);
	QNode_CtorZ(&minfo->qn);
	*vadsp = 0;
	if(rflags == ADSP_MMAP_HEAP_ADDR
	   || rflags == ADSP_MMAP_REMOTE_HEAP_ADDR) {
		VERIFY(AEE_SUCCESS
		       == (nErr = remote_mmap64_internal(-1, rflags, 0, len,
		                                         (uint64_t *)vadsp)));
		*vapps = 0;
		minfo->vapps = 0;
	} else if(rflags == FASTRPC_ALLOC_HLOS_FD) {
		VERIFYC(
		    NULL != (buf = rpcmem_alloc_internal(heapid, lflags, len)),
		    AEE_ENORPCMEMORY);
		VERIFYC(0 < (fd = rpcmem_to_fd_internal(buf)), AEE_EBADFD);
		rpcmem_set_dmabuf_name("dsp", fd, heapid, buf, lflags);
		/* Using FASTRPC_MAP_FD_DELAYED as only HLOS mapping is reqd at
		 * this point
		 */
		VERIFY(AEE_SUCCESS
		       == (nErr = fastrpc_mmap(domain, fd, buf, 0, len,
		                               FASTRPC_MAP_FD_DELAYED)));
		pbuf = (uint64_t)buf;
		*vapps = pbuf;
		minfo->vapps = *vapps;

		/* HLOS fd will be used to map memory on DSP later if required.
		 * fd here will act as the unique key between the memory mapped
		 * on HLOS and DSP.
		 */
		*vadsp = (uint64_t)fd;
	} else {
		/* Memory for unsignedPD's user-heap will be allocated in
		 * userspace for security reasons. Memory for signedPD's
		 * user-heap will be allocated in kernel.
		 */
		if(((rflags != ADSP_MMAP_ADD_PAGES)
		    && (rflags != ADSP_MMAP_ADD_PAGES_LLC))
		   || (((rflags == ADSP_MMAP_ADD_PAGES)
		        || (rflags == ADSP_MMAP_ADD_PAGES_LLC))
		       && (unsigned_module && ualloc_support))) {
			VERIFYC(NULL
			            != (buf = rpcmem_alloc_internal(
					    heapid, lflags, len)),
			        AEE_ENORPCMEMORY);
			fd = rpcmem_to_fd_internal(buf);
			VERIFYC(fd > 0, AEE_EBADPARM);
			rpcmem_set_dmabuf_name("dsp", fd, heapid, buf, lflags);
		}
		VERIFY(AEE_SUCCESS
		       == (nErr
		           = remote_mmap64_internal(fd, rflags, (uint64_t)buf,
		                                    len, (uint64_t *)vadsp)));
		pbuf = (uint64_t)buf;
		*vapps = pbuf;
		minfo->vapps = *vapps;
	}
	minfo->vadsp = *vadsp;
	minfo->size = len;
	minfo->mapped = 0;
	minfo->rflags = rflags;
	pthread_mutex_lock(&me->mem_mut);
	QList_AppendNode(&me->mem_list, &minfo->qn);
	pthread_mutex_unlock(&me->mem_mut);
bail:
	if(nErr) {
		if(buf) {
			rpcmem_free_internal(buf);
			buf = NULL;
		}
		if(minfo) {
			free(minfo);
			minfo = NULL;
		}
		VERIFY_EPRINTF("Error 0x%x: apps_mem_request_mmap64 failed "
		               "for fd 0x%x of "
		               "size %lld (lflags 0x%x, rflags 0x%x)\n",
		               nErr, fd, len, lflags, rflags);
	}
	FASTRPC_ATRACE_END();
	return nErr;
}

__QAIC_IMPL_EXPORT int
__QAIC_IMPL(apps_mem_request_map)(int heapid, uint32_t lflags, uint32_t rflags,
                                  uint32_t vin, int32_t len, uint32_t *vapps,
                                  uint32_t *vadsp) __QAIC_IMPL_ATTRIBUTE
{
	uint64_t vin1, vapps1, vadsp1;
	int64_t len1;
	int nErr = AEE_SUCCESS;
	vin1 = (uint64_t)vin;
	len1 = (int64_t)len;
	nErr = apps_mem_request_map64(heapid, lflags, rflags, vin1, len1,
	                              &vapps1, &vadsp1);
	*vapps = (uint32_t)vapps1;
	*vadsp = (uint32_t)vadsp1;
	return nErr;
}

__QAIC_IMPL_EXPORT int
__QAIC_IMPL(apps_mem_request_unmap64)(uint64_t vadsp,
                                      int64_t len) __QAIC_IMPL_ATTRIBUTE
{
	int nErr = AEE_SUCCESS, fd = -1;
	struct mem_info *minfo, *mfree = 0;
	QNode *pn, *pnn;
	int domain = get_current_domain();
	apps_mem_info *me = NULL;

	FASTRPC_ATRACE_BEGIN_L("%s called with vadsp 0x%llx, len 0x%llx",
	                       __func__, vadsp, len);
	GET_HASH_NODE(apps_mem_info, domain, me);
	VERIFYC(me, AEE_ERESOURCENOTFOUND);

	pthread_mutex_lock(&me->mem_mut);
	QLIST_NEXTSAFE_FOR_ALL(&me->mem_list, pn, pnn)
	{
		minfo = STD_RECOVER_REC(struct mem_info, qn, pn);
		if(minfo->vadsp == vadsp) {
			mfree = minfo;
			break;
		}
	}
	pthread_mutex_unlock(&me->mem_mut);

	/* If apps_mem_request_map64 was called with flag
	 * FASTRPC_ALLOC_HLOS_FD, use fastrpc_munmap else use remote_munmap64
	 * to unmap.
	 */
	if(mfree && mfree->rflags == FASTRPC_ALLOC_HLOS_FD) {
		fd = (int)vadsp;
		VERIFY(AEE_SUCCESS
		       == (nErr = fastrpc_munmap(domain, fd, 0, len)));
	} else if(mfree || fastrpc_get_pd_type(domain) == AUDIO_STATICPD) {
		/*
		 * Map info not available for Audio static PD after daemon
		 * reconnect, So continue to unmap to avoid driver global maps
		 * leak.
		 */
		VERIFY(AEE_SUCCESS
		       == (nErr = remote_munmap64((uint64_t)vadsp, len)));
		if(!mfree)
			goto bail;
	}
	VERIFYC(mfree, AEE_ENOSUCHMAP);

	/* Dequeue done after unmap to prevent leaks in case unmap fails */
	pthread_mutex_lock(&me->mem_mut);
	QNode_Dequeue(&mfree->qn);
	pthread_mutex_unlock(&me->mem_mut);

	if(mfree->mapped) {
		munmap((void *)(uintptr_t)mfree->vapps, mfree->size);
	} else {
		if(mfree->vapps)
			rpcmem_free_internal((void *)(uintptr_t)mfree->vapps);
	}
	free(mfree);
	mfree = NULL;
bail:
	if(nErr != AEE_SUCCESS) {
		VERIFY_EPRINTF("Error 0x%x: apps_mem_request_unmap64 failed "
		               "for size %lld "
		               "(vadsp 0x%llx)\n",
		               nErr, len, vadsp);
	}
	FASTRPC_ATRACE_END();
	return nErr;
}

__QAIC_IMPL_EXPORT int
__QAIC_IMPL(apps_mem_request_unmap)(uint32_t vadsp,
                                    int32_t len) __QAIC_IMPL_ATTRIBUTE
{
	uint64_t vadsp1 = (uint64_t)vadsp;
	int64_t len1 = (int64_t)len;
	int nErr = apps_mem_request_unmap64(vadsp1, len1);
	return nErr;
}

__QAIC_IMPL_EXPORT int
__QAIC_IMPL(apps_mem_share_map)(int fd, int size, uint64_t *vapps,
                                uint64_t *vadsp) __QAIC_IMPL_ATTRIBUTE
{
	struct mem_info *minfo = 0;
	int nErr = AEE_SUCCESS;
	void *buf = 0;
	uint64_t pbuf;
	int domain = get_current_domain();
	apps_mem_info *me = NULL;

	GET_HASH_NODE(apps_mem_info, domain, me);
	VERIFYC(me, AEE_ERESOURCENOTFOUND);
	VERIFYC(fd > 0, AEE_EBADPARM);
	VERIFYC(0 != (minfo = malloc(sizeof(*minfo))), AEE_ENOMEMORY);
	QNode_CtorZ(&minfo->qn);
	*vadsp = 0;
	VERIFYC(MAP_FAILED
	            != (buf = (void *)mmap(NULL, size, PROT_READ | PROT_WRITE,
	                                   MAP_SHARED, fd, 0)),
	        AEE_ERPC);
	VERIFY(AEE_SUCCESS
	       == (nErr = remote_mmap64_internal(fd, 0, (uint64_t)buf, size,
	                                         (uint64_t *)vadsp)));
	pbuf = (uint64_t)buf;
	*vapps = pbuf;
	minfo->vapps = *vapps;
	minfo->vadsp = *vadsp;
	minfo->size = size;
	minfo->mapped = 1;
	pthread_mutex_lock(&me->mem_mut);
	QList_AppendNode(&me->mem_list, &minfo->qn);
	pthread_mutex_unlock(&me->mem_mut);
bail:
	if(nErr) {
		if(buf) {
			munmap(buf, size);
		}
		if(minfo) {
			free(minfo);
			minfo = NULL;
		}
		VERIFY_EPRINTF("Error 0x%x: apps_mem_share_map failed for fd "
		               "0x%x of size %d\n",
		               nErr, fd, size);
	}
	return nErr;
}

__QAIC_IMPL_EXPORT int
__QAIC_IMPL(apps_mem_share_unmap)(uint64_t vadsp,
                                  int size) __QAIC_IMPL_ATTRIBUTE
{
	int64_t len1 = (int64_t)size;
	int nErr = AEE_SUCCESS;
	nErr = apps_mem_request_unmap64(vadsp, len1);
	if(nErr != AEE_SUCCESS) {
		VERIFY_EPRINTF("Error 0x%x: apps_mem_share_unmap failed size "
		               "%d (vadsp 0x%llx)\n",
		               nErr, size, vadsp);
	}
	return nErr;
}

__QAIC_IMPL_EXPORT int
__QAIC_IMPL(apps_mem_dma_handle_map)(int fd, int offset,
                                     int size) __QAIC_IMPL_ATTRIBUTE
{
	int nErr = AEE_SUCCESS;
	uint32_t len = 0, attr = 0;
	int flags = FASTRPC_MAP_FD_DELAYED;
	int domain = get_current_domain();

	VERIFYC(fd > 0 && size > 0, AEE_EBADPARM);
	unregister_dma_handle(fd, &len, &attr);
	// If attr is FASTRPC_ATTR_NOMAP, use flags FASTRPC_MAP_FD_NOMAP to
	// skip CPU mapping
	if(attr == FASTRPC_ATTR_NOMAP) {
		VERIFYC(size <= (int)len, AEE_EBADPARM);
		flags = FASTRPC_MAP_FD_NOMAP;
	}
	VERIFY(AEE_SUCCESS
	       == (nErr = fastrpc_mmap(domain, fd, 0, offset, size, flags)));
bail:
	if(nErr) {
		VERIFY_EPRINTF(
		    "Error 0x%x: %s failed for fd 0x%x of size %d\n", nErr,
		    __func__, fd, size);
	}
	return nErr;
}

__QAIC_IMPL_EXPORT int
__QAIC_IMPL(apps_mem_dma_handle_unmap)(int fd, int size) __QAIC_IMPL_ATTRIBUTE
{
	int nErr = AEE_SUCCESS;
	int domain = get_current_domain();

	VERIFYC(fd > 0 && size > 0, AEE_EBADPARM);
	VERIFY(AEE_SUCCESS == (nErr = fastrpc_munmap(domain, fd, 0, size)));
bail:
	if(nErr != AEE_SUCCESS) {
		VERIFY_EPRINTF(
		    "Error 0x%x: %s failed for fd 0x%x of size %d\n", nErr,
		    __func__, fd, size);
	}
	return nErr;
}
