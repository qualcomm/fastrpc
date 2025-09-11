// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef VERIFY_PRINT_ERROR
#define VERIFY_PRINT_ERROR
#endif /* VERIFY_PRINT_ERROR */

#define FARF_ERROR 1

#include <assert.h>
#include <dlfcn.h>
#include <inttypes.h>
#include <poll.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include "AEEQList.h"
#include "AEEStdErr.h"
#include "AEEstd.h"
#include "HAP_farf.h"
#include "fastrpc_async.h"
#include "fastrpc_common.h"
#include "fastrpc_perf.h"
#include "platform_libs.h"
#include "verify.h"

#define GET_DOMAIN_FROM_JOBID(jobid) (jobid & FASTRPC_ASYNC_DOMAIN_MASK)
#define GET_HASH_FROM_JOBID(jobid)                                            \
	((jobid & FASTRPC_ASYNC_HASH_MASK) >> FASTRPC_ASYNC_JOB_POS)
#define EVENT_COMPLETE 0xff

struct fastrpc_async {
	QList ql[FASTRPC_ASYNC_QUEUE_LIST_LEN];
	pthread_mutex_t mut;
	pthread_t thread;
	int init_done;
	int deinit_started;
};

struct fastrpc_async_job_node {
	QNode qn;
	fastrpc_async_descriptor_t async_desc;
	bool isjobdone;
	struct pollfd pfd;
	int result;
};

pthread_mutex_t async_mut = PTHREAD_MUTEX_INITIALIZER;
static struct fastrpc_async lasyncinfo[NUM_DOMAINS_EXTEND];

extern void set_thread_context(int domain);
static int get_remote_async_response(int domain, fastrpc_async_jobid *jobid,
                                     int *result);

int
fastrpc_search_async_job(fastrpc_async_jobid jobid,
                         struct fastrpc_async_job_node **async_node)
{
	int nErr = AEE_SUCCESS;
	int domain, hash;
	struct fastrpc_async *me = NULL;
	QNode *pn, *pnn;
	bool jobfound = false;
	struct fastrpc_async_job_node *lasync_node;

	domain = GET_DOMAIN_FROM_JOBID(jobid);
	hash = GET_HASH_FROM_JOBID(jobid);
	VERIFYC(IS_VALID_EFFECTIVE_DOMAIN_ID(domain), AEE_EBADPARM);
	me = &lasyncinfo[domain];
	VERIFYC(me->init_done == 1, AEE_EBADPARM);
	pthread_mutex_lock(&me->mut);
	QLIST_NEXTSAFE_FOR_ALL(&me->ql[hash], pn, pnn)
	{
		lasync_node
		    = STD_RECOVER_REC(struct fastrpc_async_job_node, qn, pn);
		if(lasync_node->async_desc.jobid == jobid) {
			jobfound = true;
			break;
		}
	}
	pthread_mutex_unlock(&me->mut);
	VERIFYC(jobfound, AEE_EBADPARM);
	*async_node = lasync_node;
bail:
	return nErr;
}

int
fastrpc_async_get_status(fastrpc_async_jobid jobid, int timeout_us,
                         int *result)
{
	int nErr = AEE_SUCCESS;
	int domain;
	struct fastrpc_async *me = NULL;
	struct fastrpc_async_job_node *lasync_node = NULL;
	eventfd_t event = 0;

	VERIFYC(result != NULL, AEE_EBADPARM);
	VERIFY(AEE_SUCCESS
	       == (nErr = fastrpc_search_async_job(jobid, &lasync_node)));
	domain = GET_DOMAIN_FROM_JOBID(jobid);
	me = &lasyncinfo[domain];
	pthread_mutex_lock(&me->mut);
	if(lasync_node->isjobdone) { // If job is done, return result
		*result = lasync_node->result;
		goto unlock_bail;
	} else if(timeout_us == 0) { // If timeout 0, then return PENDING
		nErr = AEE_EBUSY;
		goto unlock_bail;
	}
	// If valid timeout(+ve/-ve), create poll event and wait on poll
	if(-1 == (lasync_node->pfd.fd = eventfd(0, 0))) {
		nErr = AEE_EFAILED;
		FARF(ERROR,
		     "Error 0x%x: %s failed to create poll event for jobid "
		     "0x%" PRIx64 " (%s)\n",
		     nErr, __func__, jobid, strerror(errno));
		goto unlock_bail;
	}
	lasync_node->pfd.events = POLLIN;
	lasync_node->pfd.revents = 0;
	pthread_mutex_unlock(&me->mut);
	while(1) {
		VERIFYC(0 < poll(&lasync_node->pfd, 1, timeout_us),
		        AEE_EFAILED);
		VERIFYC(0 == eventfd_read(lasync_node->pfd.fd, &event),
		        AEE_EFAILED);
		if(event) {
			break;
		}
	}
	VERIFYC(lasync_node->isjobdone, AEE_EBUSY);
	*result = lasync_node->result;
	goto bail;
unlock_bail:
	pthread_mutex_unlock(&me->mut);
bail:
	if(nErr) {
		FARF(ERROR,
		     "Error 0x%x: %s failed for jobid 0x%" PRIx64 " (%s)\n",
		     nErr, __func__, jobid, strerror(errno));
	}
	return nErr;
}

int
fastrpc_remove_async_job(fastrpc_async_jobid jobid, bool dsp_invoke_done)
{
	int nErr = AEE_SUCCESS;
	struct fastrpc_async *me = NULL;
	struct fastrpc_async_job_node *lasync_node = NULL;
	int domain = -1;

	VERIFY(AEE_SUCCESS
	       == (nErr = fastrpc_search_async_job(jobid, &lasync_node)));
	domain = GET_DOMAIN_FROM_JOBID(jobid);
	me = &lasyncinfo[domain];
	pthread_mutex_lock(&me->mut);
	if(dsp_invoke_done && !lasync_node->isjobdone) {
		pthread_mutex_unlock(&me->mut);
		nErr = AEE_EBUSY;
		goto bail;
	}
	QNode_DequeueZ(&lasync_node->qn);
	pthread_mutex_unlock(&me->mut);
	if(lasync_node->async_desc.type == FASTRPC_ASYNC_POLL
	   && lasync_node->pfd.fd != -1) {
		close(lasync_node->pfd.fd);
		lasync_node->pfd.fd = -1;
	}
	free(lasync_node);
	lasync_node = NULL;
bail:
	if(nErr) {
		FARF(ERROR,
		     "Error 0x%x: %s failed for domain %d and jobid 0x%" PRIx64
		     " (%s)\n",
		     nErr, __func__, domain, jobid, strerror(errno));
	}
	return nErr;
}

int
fastrpc_release_async_job(fastrpc_async_jobid jobid)
{
	return fastrpc_remove_async_job(jobid, true);
}

int
fastrpc_save_async_job(int domain, struct fastrpc_async_job *async_job,
                       fastrpc_async_descriptor_t *desc)
{
	int nErr = AEE_SUCCESS;
	struct fastrpc_async *me = &lasyncinfo[domain];
	struct fastrpc_async_job_node *lasync_job = 0;
	int hash = -1;

	VERIFYC(me->init_done == 1, AEE_EINVALIDJOB);
	VERIFYC(NULL != (lasync_job = calloc(1, sizeof(*lasync_job))),
	        AEE_ENOMEMORY);
	QNode_CtorZ(&lasync_job->qn);
	lasync_job->async_desc.jobid = async_job->jobid;
	lasync_job->async_desc.type = desc->type;
	lasync_job->async_desc.cb.fn = desc->cb.fn;
	lasync_job->async_desc.cb.context = desc->cb.context;
	lasync_job->isjobdone = false;
	lasync_job->result = -1;
	lasync_job->pfd.fd = -1;
	hash = GET_HASH_FROM_JOBID(lasync_job->async_desc.jobid);
	pthread_mutex_lock(&me->mut);
	QList_AppendNode(&me->ql[hash], &lasync_job->qn);
	pthread_mutex_unlock(&me->mut);
	FARF(RUNTIME_RPC_HIGH,
	     "adsprpc: %s : Saving job with jobid 0x%" PRIx64 "", __func__,
	     lasync_job->async_desc.jobid);
bail:
	if(nErr) {
		FARF(ERROR, "Error 0x%x: %s failed for domain %d (%s)\n", nErr,
		     __func__, domain, strerror(errno));
	}
	return nErr;
}

void
fastrpc_async_respond_all_pending_jobs(int domain)
{
	int i = 0;
	struct fastrpc_async *me = &lasyncinfo[domain];
	struct fastrpc_async_job_node *lasync_node = NULL;
	QNode *pn;

	for(i = 0; i < FASTRPC_ASYNC_QUEUE_LIST_LEN; i++) {
		pthread_mutex_lock(&me->mut);
		while(!QList_IsEmpty(&me->ql[i])) {
			pn = QList_GetFirst(&me->ql[i]);
			lasync_node = STD_RECOVER_REC(
			    struct fastrpc_async_job_node, qn, pn);
			if(!lasync_node) {
				continue;
			}
			QNode_DequeueZ(&lasync_node->qn);
			lasync_node->result = -ECONNRESET;
			pthread_mutex_unlock(&me->mut);
			if(lasync_node->async_desc.type
			   == FASTRPC_ASYNC_CALLBACK) {
				FARF(RUNTIME_RPC_HIGH,
				     "adsprpc: %s callback jobid 0x%" PRIx64
				     " and result 0x%x",
				     __func__, lasync_node->async_desc.jobid,
				     lasync_node->result);
				lasync_node->async_desc.cb.fn(
				    lasync_node->async_desc.jobid,
				    lasync_node->async_desc.cb.context,
				    lasync_node->result);
			} else if(lasync_node->async_desc.type
			          == FASTRPC_ASYNC_POLL) {
				FARF(RUNTIME_RPC_HIGH,
				     "adsprpc: %s poll jobid 0x%" PRIx64
				     " and result 0x%x",
				     __func__, lasync_node->async_desc.jobid,
				     lasync_node->result);
				if(lasync_node->pfd.fd != -1) {
					eventfd_write(
					    lasync_node->pfd.fd,
					    (eventfd_t)EVENT_COMPLETE);
				}
			}
			free(lasync_node);
			lasync_node = NULL;
			pthread_mutex_lock(&me->mut);
		}
		pthread_mutex_unlock(&me->mut);
	}
}

static void *
async_fastrpc_thread(void *arg)
{
	int nErr = AEE_SUCCESS;
	struct fastrpc_async *me = (struct fastrpc_async *)arg;
	int domain = (int)(me - &lasyncinfo[0]);
	struct fastrpc_async_job_node *lasync_node = NULL;
	int result = -1;
	fastrpc_async_jobid jobid = -1;
	QNode *pn, *pnn;

	int hash = -1;
	bool isjobfound = false;

	/// TODO: Do we really need this line?
	set_thread_context(domain);
	do {
		nErr = get_remote_async_response(domain, &jobid, &result);
		VERIFY(nErr == AEE_SUCCESS);
		FARF(RUNTIME_RPC_HIGH,
		     "adsprpc: %s received async response for jobid 0x%" PRIx64
		     " and result 0x%x",
		     __func__, jobid, result);
		isjobfound = false;
		hash = GET_HASH_FROM_JOBID(jobid);
		pthread_mutex_lock(&me->mut);
		QLIST_NEXTSAFE_FOR_ALL(&me->ql[hash], pn, pnn)
		{
			lasync_node = STD_RECOVER_REC(
			    struct fastrpc_async_job_node, qn, pn);
			if(lasync_node->async_desc.jobid == jobid) {
				lasync_node->isjobdone = true;
				lasync_node->result = result;
				isjobfound = true;
				switch(lasync_node->async_desc.type) {
				case FASTRPC_ASYNC_NO_SYNC:
					FARF(RUNTIME_RPC_HIGH,
					     "adsprpc: %s nosync jobid "
					     "0x%" PRIx64 " and result 0x%x",
					     __func__,
					     lasync_node->async_desc.jobid,
					     result);
					QNode_DequeueZ(&lasync_node->qn);
					pthread_mutex_unlock(&me->mut);
					free(lasync_node);
					lasync_node = NULL;
					break;
				case FASTRPC_ASYNC_POLL:
					FARF(
					    RUNTIME_RPC_HIGH,
					    "adsprpc: %s poll jobid 0x%" PRIx64
					    " and result 0x%x",
					    __func__,
					    lasync_node->async_desc.jobid,
					    result);
					if(lasync_node->pfd.fd != -1) {
						eventfd_write(
						    lasync_node->pfd.fd,
						    (eventfd_t)EVENT_COMPLETE);
					}
					pthread_mutex_unlock(&me->mut);
					break;
				case FASTRPC_ASYNC_CALLBACK:
					pthread_mutex_unlock(&me->mut);
					FARF(RUNTIME_RPC_HIGH,
					     "adsprpc: %s callback jobid "
					     "0x%" PRIx64 " and result 0x%x",
					     __func__,
					     lasync_node->async_desc.jobid,
					     result);
					lasync_node->async_desc.cb.fn(
					    lasync_node->async_desc.jobid,
					    lasync_node->async_desc.cb.context,
					    result);
					break;
				default:
					pthread_mutex_unlock(&me->mut);
					FARF(RUNTIME_RPC_HIGH,
					     "adsprpc: %s Invalid job type "
					     "for jobid 0x%" PRIx64 "",
					     __func__,
					     lasync_node->async_desc.jobid);
					break;
				}
				break;
			}
		}
		if(!isjobfound)
			pthread_mutex_unlock(&me->mut);
	} while(1);
bail:
	if(nErr != AEE_SUCCESS) {
		VERIFY_EPRINTF(
		    "Error 0x%x: %s AsyncFastRPC worker thread exited for "
		    "domain %d (errno %s), async_domain_deinit started %d",
		    nErr, __func__, domain, strerror(errno),
		    me->deinit_started);
	}
	dlerror();
	return (void *)(uintptr_t)nErr;
}

void
async_thread_exit_handler(int sig)
{
	FARF(ALWAYS, "Async FastRPC worker thread exiting with signal %d\n",
	     sig);
	pthread_exit(0);
}

void
fastrpc_async_domain_deinit(int domain)
{
	struct fastrpc_async *me = &lasyncinfo[domain];
	int err = 0;

	pthread_mutex_lock(&async_mut);
	if(!me->init_done) {
		goto fasync_deinit_done;
	}
	FARF(ALWAYS,
	     "%s: Waiting for AsyncRPC worker thread to join for domain %d\n",
	     __func__, domain);
	if(me->thread) {
		me->deinit_started = 1;
		err = fastrpc_exit_async_thread(domain);
		if(err) {
			pthread_kill(me->thread, SIGUSR1);
		}
		pthread_join(me->thread, 0);
		me->thread = 0;
	}
	FARF(ALWAYS, "fastrpc async thread joined for domain %d", domain);
	fastrpc_async_respond_all_pending_jobs(domain);
	pthread_mutex_destroy(&me->mut);
	me->init_done = 0;
fasync_deinit_done:
	pthread_mutex_unlock(&async_mut);
	return;
}

int
fastrpc_async_domain_init(int domain)
{
	struct fastrpc_async *me = &lasyncinfo[domain];
	int nErr = AEE_EUNKNOWN, i = 0;
	struct sigaction siga;
	uint32_t capability = 0;

	pthread_mutex_lock(&async_mut);
	if(me->init_done) {
		nErr = AEE_SUCCESS;
		goto bail;
	}
	VERIFY(AEE_SUCCESS
	       == (nErr = fastrpc_get_cap(domain, ASYNC_FASTRPC_SUPPORT,
	                                  &capability)));
	VERIFYC(capability == 1, AEE_EUNSUPPORTED);
	me->thread = 0;
	pthread_mutex_init(&me->mut, 0);
	for(i = 0; i < FASTRPC_ASYNC_QUEUE_LIST_LEN; i++) {
		QList_Ctor(&me->ql[i]);
	}
	VERIFY(AEE_SUCCESS
	       == (nErr = pthread_create(&me->thread, 0, async_fastrpc_thread,
	                                 (void *)me)));
	memset(&siga, 0, sizeof(siga));
	siga.sa_flags = 0;
	siga.sa_handler = async_thread_exit_handler;
	VERIFY(AEE_SUCCESS == (nErr = sigaction(SIGUSR1, &siga, NULL)));
	me->init_done = 1;
	me->deinit_started = 0;
	FARF(ALWAYS, "%s: AsyncRPC worker thread launched for domain %d\n",
	     __func__, domain);
bail:
	pthread_mutex_unlock(&async_mut);
	if(nErr != AEE_SUCCESS) {
		VERIFY_EPRINTF("Error 0x%x: %s failed for domain %d (%s)\n",
		               nErr, __func__, domain, strerror(errno));
		fastrpc_async_domain_deinit(domain);
	}
	return nErr;
}

/*
 * Internal function to get async response from kernel. Waits in kernel until
 * response is received from DSP
 * @ domain: domain to which Async job is submitted
 * @ async_data: IOCTL structure that is sent to kernel to get async response
 * job information returns 0 on success
 *
 */
static int
get_remote_async_response(int domain, fastrpc_async_jobid *jobid, int *result)
{
	int nErr = AEE_SUCCESS, dev = -1;
	uint64_t *perf_kernel = NULL, *perf_dsp = NULL;
	fastrpc_async_jobid job = -1;
	int res = -1;
	remote_handle handle = -1;
	uint32_t sc = 0;

	VERIFYC(IS_VALID_EFFECTIVE_DOMAIN_ID(domain), AEE_EBADPARM);
	VERIFY(AEE_SUCCESS == (nErr = fastrpc_session_dev(domain, &dev)));
	VERIFYM(-1 != dev, AEE_ERPC, "open dev failed\n");
	if(is_kernel_perf_enabled()) {
		perf_kernel = (uint64_t *)calloc(PERF_KERNEL_KEY_MAX,
		                                 sizeof(uint64_t));
		VERIFYC(perf_kernel != NULL, AEE_ENOMEMORY);
	}
	if(is_dsp_perf_enabled(domain)) {
		perf_dsp
		    = (uint64_t *)calloc(PERF_DSP_KEY_MAX, sizeof(uint64_t));
		VERIFYC(perf_dsp != NULL, AEE_ENOMEMORY);
	}
	nErr = ioctl_invoke2_response(dev, &job, &handle, &sc, &res,
	                              perf_kernel, perf_dsp);
	if(perf_kernel) {
		FARF(ALWAYS,
		     "RPCPERF-K  H:0x%x SC:0x%x C:%" PRIu64 " F:%" PRIu64
		     " ns M:%" PRIu64 " ns CP:%" PRIu64 " ns L:%" PRIu64
		     " ns G:%" PRIu64 " ns P:%" PRIu64 " ns INV:%" PRIu64
		     " ns INVOKE:%" PRIu64 " ns\n",
		     handle, sc, perf_kernel[0], perf_kernel[1],
		     perf_kernel[2], perf_kernel[3], perf_kernel[4],
		     perf_kernel[5], perf_kernel[6], perf_kernel[7],
		     perf_kernel[8]);
	}
	if(perf_dsp) {
		FARF(ALWAYS,
		     "RPCPERF-D  H:0x%x SC:0x%x C:%" PRIu64 " M_H:%" PRIu64
		     " us M:%" PRIu64 " us G:%" PRIu64 " us INVOKE:%" PRIu64
		     " us P:%" PRIu64 " us CACHE:%" PRIu64 " us UM:%" PRIu64
		     " us "
		     "UM_H:%" PRIu64 " us R:%" PRIu64 " us E_R:%" PRIu64
		     " us J_S_T:%" PRIu64 " us\n",
		     handle, sc, perf_dsp[0], perf_dsp[1], perf_dsp[2],
		     perf_dsp[3], perf_dsp[4], perf_dsp[5], perf_dsp[6],
		     perf_dsp[7], perf_dsp[8], perf_dsp[9], perf_dsp[10],
		     perf_dsp[11]);
	}
	*jobid = job;
	*result = res;

bail:
	if(perf_kernel) {
		free(perf_kernel);
		perf_kernel = NULL;
	}
	if(perf_dsp) {
		free(perf_dsp);
		perf_dsp = NULL;
	}
	if(nErr) {
		FARF(ERROR,
		     "Error 0x%x: %s failed to get async response data for "
		     "domain %d errno "
		     "%s",
		     nErr, __func__, domain, strerror(errno));
	}
	return nErr;
}

// Make IOCTL call to exit async thread
int
fastrpc_exit_async_thread(int domain)
{
	int nErr = AEE_SUCCESS, dev;

	VERIFY(AEE_SUCCESS == (nErr = fastrpc_session_dev(domain, &dev)));
	nErr = ioctl_control(dev, DSPRPC_ASYNC_WAKE, NULL);
bail:
	if(nErr)
		FARF(ERROR,
		     "Error 0x%x: %s failed for domain %d (errno: %s), ignore "
		     "if ioctl not "
		     "supported, try pthread kill ",
		     nErr, __func__, domain, strerror(errno));
	return nErr;
}
