// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef VERIFY_PRINT_ERROR
#define VERIFY_PRINT_ERROR
#endif
#ifndef VERIFY_PRINT_WARN
#define VERIFY_PRINT_WARN
#endif //VERIFY_PRINT_WARN
#define FARF_ERROR 1
#define FARF_HIGH 0
#define FARF_MEDIUM 0

#include <stdlib.h>
#include <AEEstd.h>
#include <AEEStdErr.h>
#include <HAP_farf.h>
#include "verify.h"
#include "fastrpc_context.h"
#include "fastrpc_internal.h"
#include "fastrpc_apps_user.h"

/* Global context table */
static fastrpc_context_table gctx;

/* Add context entry to global context table */
static inline void fastrpc_context_add_to_table(fastrpc_context *ctx) {
	pthread_mutex_lock(&gctx.mut);
	HASH_ADD(hh, gctx.table, ctxid, sizeof(ctx->ctxid), ctx);
	pthread_mutex_unlock(&gctx.mut);
}

/* Remove context entry from global context table */
static inline void fastrpc_context_remove_from_table(fastrpc_context *ctx) {
	pthread_mutex_lock(&gctx.mut);
	HASH_DELETE(hh, gctx.table, ctx);
	pthread_mutex_unlock(&gctx.mut);
}

/* Validate that context is present in global hash-table */
static inline fastrpc_context *fastrpc_context_validate(uint64_t ctxid) {
	fastrpc_context *ctx = NULL;

	// Search in hash-table
	pthread_mutex_lock(&gctx.mut);
	HASH_FIND(hh, gctx.table, &ctxid, sizeof(ctxid), ctx);
	pthread_mutex_unlock(&gctx.mut);

	return ctx;
}

/*
 * Initialize domains lists in context struct and validate.
 *
 * Effective domain ids passed by user are validated on the following
 * basis:
 *		Context can either be created on multiple sessions on one domain
 *		or multiple domains, but not both.
 *		For example, context cannot be created on 2 sessions on CDSP +
 *		1 session on ADSP.
 *
 * returns 0 on success
 */
static int fastrpc_context_init_domains(fastrpc_context_create *create,
	fastrpc_context *ctx) {
	int nErr = AEE_SUCCESS, i = 0, j = 0;
	unsigned int effec_domain_id = 0, num_domain_ids = create->num_domain_ids;
	unsigned int *domain_ids = ctx->domains;
	bool multisess = true, multidom = true;

	for (i = 0; i < num_domain_ids; i++) {
		effec_domain_id = create->effec_domain_ids[i];
		VERIFYC(IS_VALID_EFFECTIVE_DOMAIN_ID(effec_domain_id),
					AEE_EBADDOMAIN);

		ctx->effec_domain_ids[i] = effec_domain_id;
		domain_ids[i] = GET_DOMAIN_FROM_EFFEC_DOMAIN_ID(effec_domain_id);

		/*
		 * If client is trying to create context on multiple sessions on
		 * same domain, validate that all domain ids are the same.
		 */
		if (domain_ids[i] != domain_ids[0])
			multisess = false;
	}
	if (multisess)
		goto bail;

	/*
	 * If client is trying to create context on multiple domains, validate
	 * that every domain id in the list is unique.
	 */
	for (i = 0; i < num_domain_ids; i++) {
		for (j = i + 1; j < num_domain_ids; j++) {
			if (domain_ids[j] == domain_ids[i]) {
				multidom = false;
				break;
			}
		}
	}
	if (multidom)
		goto bail;

	/* Context on multiple sessions on multiple domains is not allowed */
	nErr = AEE_EBADPARM;
	FARF(ALWAYS, "Error 0x%x: %s: context cannot be both multi-session & multi-domain",
			nErr, __func__);
	return nErr;
bail:
	if (nErr)
		FARF(ALWAYS, "Error 0x%x: %s failed", nErr, __func__);

	return nErr;
}

/**
 * Deinit context struct and free its resources
 *
 * @param[in] ctx :	Context object
 *
 * Returns 0 on success
 */
static int fastrpc_context_deinit(fastrpc_context *ctx) {
	unsigned int domain = 0;

	if (!ctx)
		return AEE_EBADCONTEXT;

	pthread_mutex_lock(&ctx->mut);
	for (unsigned int i = 0; i < ctx->num_domain_ids; i++) {
		domain = ctx->effec_domain_ids[i];

		// If no session was opened on domain, continue
		if (!ctx->devs[i])
			continue;

		fastrpc_session_close(domain, INVALID_DEVICE);
	}
	free(ctx->devs);

	free(ctx->domains);

	free(ctx->effec_domain_ids);

	pthread_mutex_unlock(&ctx->mut);

	pthread_mutex_destroy(&ctx->mut);
	free(ctx);
	return 0;
}

/**
 * Allocate context struct and initialize it
 *
 * @param[in] num_domain_ids :	Number of effective domain ids on which
 *								context is being created.
 *
 * Returns valid context struct on success, NULL on failure
 */
static fastrpc_context *fastrpc_context_init(unsigned int num_domain_ids) {
	fastrpc_context *ctx = NULL;
	int nErr = AEE_SUCCESS;

	// Allocate memory for context struct and its members
	VERIFYC(NULL != (ctx = (fastrpc_context *)calloc(1,
		sizeof(fastrpc_context))), AEE_ENOMEMORY);

	pthread_mutex_init(&ctx->mut, 0);

	VERIFYC(NULL != (ctx->effec_domain_ids = (unsigned int *)calloc(
		num_domain_ids, sizeof(*ctx->effec_domain_ids))), AEE_ENOMEMORY);
	VERIFYC(NULL != (ctx->domains = (unsigned int *)calloc(num_domain_ids,
		sizeof(*ctx->domains))), AEE_ENOMEMORY);
	VERIFYC(NULL != (ctx->devs = (int *)calloc(num_domain_ids,
		sizeof(*ctx->devs))), AEE_ENOMEMORY);

	ctx->num_domain_ids = num_domain_ids;
bail:
	if (nErr) {
		FARF(ALWAYS, "Error 0x%x: %s failed for num domain ids %u\n",
			nErr, __func__, num_domain_ids);
		fastrpc_context_deinit(ctx);
		ctx = NULL;
	}
	return ctx;
}

int fastrpc_destroy_context(uint64_t ctxid) {
	int nErr = AEE_SUCCESS, dev = -1;
	fastrpc_context *ctx = NULL;

	VERIFYC(gctx.init, AEE_ENOTINITIALIZED);

	FARF(ALWAYS, "%s called for context 0x%"PRIx64"", __func__, ctxid);

	VERIFYC(NULL != (ctx = fastrpc_context_validate(ctxid)),
			AEE_EBADCONTEXT);
	fastrpc_context_remove_from_table(ctx);

	/*
	 * Deregister context id from kernel. The device fd for ioctl can
	 * be that of any of the domains on which context was created.
	 * Use the first domain's device fd.
	 */
	VERIFYC(-1 != (dev = get_device_fd(ctx->effec_domain_ids[0])),
			AEE_EINVALIDDEVICE);
	VERIFY(AEE_SUCCESS == (nErr = ioctl_mdctx_manage(dev,
			FASTRPC_MDCTX_REMOVE, NULL, NULL, 0, &ctx->ctxid)));

	VERIFY(AEE_SUCCESS == (nErr = fastrpc_context_deinit(ctx)));
	FARF(ALWAYS, "%s done for context 0x%"PRIx64"", __func__, ctxid);
bail:
	if (nErr) {
		FARF(ALWAYS, "Error 0x%x: %s failed for ctx 0x%"PRIx64"",
			nErr, __func__, ctxid);
	}
	return nErr;
}

int fastrpc_create_context(fastrpc_context_create *create) {
	int nErr = AEE_SUCCESS, dev = -1;
	unsigned int num_domain_ids = 0, effec_domain_id = 0, i = 0;
	fastrpc_context *ctx = NULL;

	VERIFYC(gctx.init, AEE_ENOTINITIALIZED);
	num_domain_ids = create->num_domain_ids;
	FARF(ALWAYS, "%s called for %u domain ids", __func__, num_domain_ids);

	// Basic sanity checks on client inputs
	VERIFYC(create->effec_domain_ids && !create->flags, AEE_EBADPARM);
	VERIFYC(num_domain_ids && num_domain_ids < NUM_DOMAINS_EXTEND,
		AEE_EBADPARM);

	VERIFYC(NULL != (ctx = fastrpc_context_init(num_domain_ids)),
		AEE_EBADCONTEXT);
	VERIFYC(AEE_SUCCESS == (nErr = fastrpc_context_init_domains(create,
		ctx)), AEE_EBADPARM);

	// Create session on each domain id
	for (i = 0; i < num_domain_ids; i++) {
		effec_domain_id = ctx->effec_domain_ids[i];

		// Create session on domain if not already created
		VERIFY(AEE_SUCCESS == (nErr = fastrpc_session_open(effec_domain_id,
			&ctx->devs[i])));
	}

	/*
	 * Generate unique context id from kernel. The device fd for ioctl can
	 * be that of any of the domains on which context is being created.
	 * Use the first domain's device fd.
	 */
	VERIFYC(-1 != (dev = get_device_fd(ctx->effec_domain_ids[0])),
			AEE_EINVALIDDEVICE);
	VERIFY(AEE_SUCCESS == (nErr = ioctl_mdctx_manage(dev,
			FASTRPC_MDCTX_SETUP, ctx, ctx->domains,
			num_domain_ids, &ctx->ctxid)));

	fastrpc_context_add_to_table(ctx);

	// Return context to user
	create->ctx = ctx->ctxid;
	FARF(ALWAYS, "%s done with context 0x%"PRIx64" on %u domain ids",
		__func__, ctx->ctxid, num_domain_ids);
bail:
	if (nErr) {
		FARF(ALWAYS, "Error 0x%x: %s failed for %u domain ids\n",
			nErr, __func__, num_domain_ids);
		fastrpc_context_deinit(ctx);
	}
	return nErr;
}

int fastrpc_context_get_domains(uint64_t ctxid,
	unsigned int **effec_domain_ids, unsigned int *num_domain_ids) {
	int nErr = AEE_SUCCESS;
	fastrpc_context *ctx = NULL;

	VERIFYC(NULL != (ctx = fastrpc_context_validate(ctxid)),
			AEE_EBADCONTEXT);
	VERIFYC(effec_domain_ids && num_domain_ids, AEE_EMEMPTR);
	*effec_domain_ids = ctx->effec_domain_ids;
	*num_domain_ids = ctx->num_domain_ids;
bail:
	if (nErr)
		FARF(ALWAYS, "Error 0x%x: %s failed\n", nErr, __func__);

	return nErr;
}

int fastrpc_context_table_init(void) {
	int nErr = AEE_SUCCESS;

	pthread_mutex_init(&gctx.mut, 0);
	gctx.init = true;
	if (nErr)
		FARF(ERROR, "Error 0x%x: %s failed\n", nErr, __func__);
	else
		FARF(RUNTIME_RPC_HIGH, "%s done", __func__);

	return nErr;
}

int fastrpc_context_table_deinit(void) {
	int nErr = AEE_SUCCESS;
	fastrpc_context *table = gctx.table, *ctx = NULL, *tmp = NULL;
	pthread_mutex_t *mut = &gctx.mut;

	VERIFYC(gctx.init, AEE_ENOTINITIALIZED);
	gctx.init = false;

	/* Delete all contexts */
	pthread_mutex_lock(mut);
	HASH_ITER(hh, table, ctx, tmp) {
		HASH_DELETE(hh, table, ctx);
		fastrpc_context_deinit(ctx);
	}
	pthread_mutex_unlock(mut);

	pthread_mutex_destroy(mut);
bail:
	if (nErr)
		FARF(ERROR, "Error 0x%x: %s failed\n", nErr, __func__);
	else
		FARF(RUNTIME_RPC_HIGH, "%s done", __func__);

	return nErr;
}