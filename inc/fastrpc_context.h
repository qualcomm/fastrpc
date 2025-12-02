// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef FASTRPC_CONTEXT_H
#define FASTRPC_CONTEXT_H

#include "remote.h"
#include "uthash.h"
#include <pthread.h>
#include <stdbool.h>

typedef struct fastrpc_context {
	/* Hash table handle */
	UT_hash_handle hh;

	/* Array of effective domain ids on which context is created */
	unsigned int *effec_domain_ids;

	/* Array of domains on which context is created */
	unsigned int *domains;

	/* Array of device fds opened for each session */
	int *devs;

	/* Number of effective domain ids on which context is created */
	unsigned int num_domain_ids;

	/* Kernel-generated context id - key for hash-table */
	uint64_t ctxid;

	/* Mutex for context */
	pthread_mutex_t mut;
} fastrpc_context;

typedef struct fastrpc_context_table {
	/* Hash-table */
	fastrpc_context *table;

	/* Mutex for hash-table */
	pthread_mutex_t mut;

	/* Flag to indicate context table init is done */
	bool init;
} fastrpc_context_table;

/**
 * Initialize the fastrpc-context table
 *
 * Function expected to be called during libxdsprpc load.
 *
 * Returns 0 on success
 */
int fastrpc_context_table_init(void);

/**
 * Deinitialize the fastrpc-context table
 *
 * Function expected to be called during libxdsprpc unload.
 *
 * Returns 0 on success
 */
int fastrpc_context_table_deinit(void);

/**
 * Create session(s) on one or more remote domains and return a
 * multi-domain context.
 *
 * @param[in]  create : Context-create request struct
 *
 * Returns 0 on success
 */
int fastrpc_create_context(fastrpc_context_create *create);

/**
 * Destroy fastrpc multi-domain context
 *
 * This function cleans up the sessions that are part of the
 * multi-domain context and frees the resources associated with
 * the context.
 *
 * @param[in]  uctx : Multi-domain context
 *
 * Returns 0 on success
 */
int fastrpc_destroy_context(uint64_t uctx);

/**
 * Get info about a multi-domain context like number of domains & list of
 * effective domain ids on which it was created
 *
 * @param[in]  req : Request payload
 *
 * Returns 0 on success
 */
int fastrpc_context_get_domains(uint64_t uctx, unsigned int **effec_domain_ids,
                                unsigned int *num_domain_ids);

#endif // FASTRPC_CONTEXT_H
