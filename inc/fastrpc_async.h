// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef FASTRPC_ASYNC_H
#define FASTRPC_ASYNC_H

#include "fastrpc_internal.h"
#include "remote64.h"

#define POS_TO_MASK(pos) ((1UL << pos) - 1)

#define FASTRPC_ASYNC_JOB_POS 4 // Position in jobid where job starts

// Store jobs in Queue. Index of queue calculated based on hash value of job.
// Always needs to be 2^value
#define FASTRPC_ASYNC_QUEUE_LIST_LEN 16

// Position where hash ends in jobid
#define FASTRPC_ASYNC_HASH_IDX_POS                                            \
	(FASTRPC_ASYNC_JOB_POS + (FASTRPC_ASYNC_QUEUE_LIST_LEN >> 2))

// Position in jobid where timespec starts
#define FASTRPC_ASYNC_TIME_SPEC_POS 48

#define SECONDS_PER_HOUR (3600)

#define FASTRPC_ASYNC_DOMAIN_MASK (POS_TO_MASK(FASTRPC_ASYNC_JOB_POS))
#define FASTRPC_ASYNC_JOB_CNT_MASK                                            \
	(POS_TO_MASK(FASTRPC_ASYNC_TIME_SPEC_POS) & ~FASTRPC_ASYNC_DOMAIN_MASK)
#define FASTRPC_ASYNC_HASH_MASK                                               \
	(POS_TO_MASK(FASTRPC_ASYNC_HASH_IDX_POS) & ~FASTRPC_ASYNC_DOMAIN_MASK)

// Async job structure
struct fastrpc_async_job {
	uint32_t isasyncjob; // set if async job
	/*
	 * Fastrpc async job ID bit-map:
	 *
	 * bits 0-3   :	domain ID
	 * bits 4-47  :	job counter
	 * bits 48-63 :	timespec
	 */
	fastrpc_async_jobid jobid;
	uint32_t reserved; // reserved
};

/*
 * Internal function to save async job information before submitting to DSP
 * @ domain: domain to which Async job is submitted
 * @ asyncjob: Async job structure
 * @ desc: Async desc passed by user
 * returns 0 on success
 *
 */
int fastrpc_save_async_job(int domain, struct fastrpc_async_job *asyncjob,
                           fastrpc_async_descriptor_t *desc);

/*
 * Internal function to remove async job, if async invocation to DSP fails
 * @ jobid: domain to which Async job is submitted
 * @ asyncjob: Async job id
 * @ dsp_invoke_done: DSP invocation is successful
 * returns 0 on success
 *
 */
int fastrpc_remove_async_job(fastrpc_async_jobid jobid, bool dsp_invoke_done);

/*
 * API to initialize async module data strcutures and globals
 */
int fastrpc_async_domain_init(int domain);

/*
 * API to de-initialize async module data strcutures and globals
 */
void fastrpc_async_domain_deinit(int domain);

#endif // FASTRPC_ASYNC_H
