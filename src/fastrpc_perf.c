// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef VERIFY_PRINT_ERROR
#define VERIFY_PRINT_ERROR
#endif // VERIFY_PRINT_ERROR

#ifndef VERIFY_PRINT_WARN
#define VERIFY_PRINT_WARN
#endif // VERIFY_PRINT_WARN

#define FARF_ERROR 1

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "AEEStdErr.h"
#include "AEEstd.h"
#include "HAP_farf.h"
#include "adsp_perf.h"
#include "adsp_perf1.h"
#include "fastrpc_cap.h"
#include "fastrpc_common.h"
#include "fastrpc_internal.h"
#include "fastrpc_perf.h"
#include "fastrpc_trace.h"
#include "remote.h"
#include "rpcmem_internal.h"
#include "verify.h"

#define PERF_MODE 2
#define PERF_OFF 0
#define PERF_KERNEL_MASK (0x1)
#define PERF_ADSP_MASK (0x2)
#define PERF_KEY_STR_MAX (2 * 1024)
#define PERF_MAX_NUM_KEYS 64
#define PERF_KERNEL_NUM_KEYS 9

#define PERF_NS_TO_US(n) ((n) / 1000)

#define IS_KEY_ENABLED(name)                                                  \
	(!strncmp((name), "perf_invoke_count", 17)                            \
	 || !strncmp((name), "perf_mod_invoke", 15)                           \
	 || !strncmp((name), "perf_rsp", 8)                                   \
	 || !strncmp((name), "perf_hdr_sync_flush", 19)                       \
	 || !strncmp((name), "perf_sync_flush", 15)                           \
	 || !strncmp((name), "perf_hdr_sync_inv", 17)                         \
	 || !strncmp((name), "perf_clean_cache", 16)                          \
	 || !strncmp((name), "perf_sync_inv", 13))

#define PERF_CAPABILITY_CHECK (1 << 1)

extern bool fastrpc_config_is_perfkernel_enabled(void);
extern bool fastrpc_config_is_perfdsp_enabled(void);

int perf_v2_kernel = 0;
int perf_v2_dsp = 0;

struct perf_keys {
	int64_t data[PERF_MAX_NUM_KEYS];
	int numKeys;
	int maxLen;
	int enable;
	char *keys;
};

struct fastrpc_perf {
	int count;
	int freq;
	int perf_on;
	int process_trace_enabled;
	struct perf_keys kernel;
	struct perf_keys dsp;
	remote_handle64 adsp_perf_handle;
};
struct fastrpc_perf gperf = { 0 };

void
check_perf_v2_enabled(int domain)
{
	int nErr = 0;
	fastrpc_capability cap = { 0 };
	cap.domain = domain;
	cap.attribute_ID = PERF_V2_DRIVER_SUPPORT;

	nErr = fastrpc_get_cap(cap.domain, cap.attribute_ID, &cap.capability);
	if(nErr == 0) {
		perf_v2_kernel
		    = (cap.capability == PERF_CAPABILITY_CHECK) ? 1 : 0;
	}
	cap.attribute_ID = PERF_V2_DSP_SUPPORT;
	nErr = fastrpc_get_cap(cap.domain, cap.attribute_ID, &cap.capability);
	if(nErr == 0) {
		perf_v2_dsp
		    = (cap.capability == PERF_CAPABILITY_CHECK) ? 1 : 0;
	}
}

bool
is_kernel_perf_enabled()
{
	return perf_v2_kernel;
}
bool
is_dsp_perf_enabled(int domain)
{
	return perf_v2_dsp;
}
bool
is_perf_v2_enabled()
{
	return (perf_v2_kernel == 1 && perf_v2_dsp == 1);
}

inline int
is_systrace_enabled()
{
	return gperf.process_trace_enabled;
}

static int
perf_kernel_getkeys(int dev)
{
	int nErr = 0, numkeys = 0;
	struct fastrpc_perf *p = &gperf;
	char *token;
	char *saveptr;

	VERIFYC(p->kernel.keys, AEE_ERPC);
	VERIFY(0 == (nErr = ioctl_getperf(dev, 1, p->kernel.keys, &numkeys)));
	FARF(RUNTIME_RPC_HIGH, "adsprpc:apps:keys: numkeys %d keys: %s",
	     numkeys, p->kernel.keys);
	p->kernel.numKeys = numkeys;
	token = strtok_r(p->kernel.keys, ":", &saveptr);
	while(token) {
		FARF(RUNTIME_RPC_LOW, "key: %s", token);
		token = strtok_r(NULL, ":", &saveptr);
	}
bail:
	if(nErr) {
		VERIFY_WPRINTF(
		    "Warning: %s: Failed to get kernel keys, nErr 0x%x\n",
		    __func__, nErr);
	}
	return nErr;
}
/*
C: PERF_COUNT
F: PERF_FLUSH
M: PERF_MAP
CP: PERF_COPY
L: PERF_LINK
G: PERF_GETARGS
P: PERF_PUTARGS
INV: PERF_INVARGS
INVOKE: PERF_INVOKE
*/

static void
get_perf_kernel(int dev, remote_handle handle, uint32_t sc)
{
	int nErr = 0, numkeys = 0;
	struct fastrpc_perf *p = &gperf;
	char *token;

	VERIFYC(dev != -1, AEE_ERPC);

	VERIFY(0 == (nErr = ioctl_getperf(dev, 0, p->kernel.data, &numkeys)));
	token = p->kernel.keys;

	VERIFYC(token, AEE_ERPC);
	switch(numkeys) {
	case PERF_KERNEL_NUM_KEYS:
		FARF(ALWAYS,
		     "RPCPERF-K  H:0x%x SC:0x%x C:%" PRId64 " F:%" PRId64
		     " ns M:%" PRId64 " ns CP:%" PRId64 " ns L:%" PRId64
		     " ns G:%" PRId64 " ns P:%" PRId64 " ns INV:%" PRId64
		     " ns INVOKE:%" PRId64 " ns\n",
		     handle, sc, p->kernel.data[0], p->kernel.data[1],
		     p->kernel.data[2], p->kernel.data[3], p->kernel.data[4],
		     p->kernel.data[5], p->kernel.data[6], p->kernel.data[7],
		     p->kernel.data[8]);
		break;
	default:
		FARF(ALWAYS, "RPCPERF-K  H:0x%x SC:0x%x \n", handle, sc);
		break;
	}
bail:
	if(nErr)
		VERIFY_WPRINTF("Warning: %s: Failed to get perf data from "
		               "kernel, nErr 0x%x\n",
		               __func__, nErr);
	return;
}

static void
get_perf_adsp(remote_handle handle, uint32_t sc)
{
	int nErr = 0;
	struct perf_keys *pdsp = &gperf.dsp;
	int ii;
	char *token;

	char *keystr = pdsp->keys;
	if(gperf.adsp_perf_handle != INVALID_HANDLE) {
		VERIFY(0
		       == (nErr = adsp_perf1_get_usecs(gperf.adsp_perf_handle,
		                                       pdsp->data,
		                                       PERF_MAX_NUM_KEYS)));
	} else {
		VERIFY(0
		       == (nErr = adsp_perf_get_usecs(pdsp->data,
		                                      PERF_MAX_NUM_KEYS)));
	}
	VERIFYC(pdsp->maxLen < PERF_KEY_STR_MAX, AEE_ERPC);
	VERIFYC(pdsp->numKeys < PERF_MAX_NUM_KEYS, AEE_ERPC);
	FARF(ALWAYS, "\nFastRPC dsp perf for handle 0x%x sc 0x%x\n", handle,
	     sc);
	for(ii = 0; ii < pdsp->numKeys; ii++) {
		token = keystr;
		keystr += strlen(token) + 1;
		VERIFYC(token, AEE_ERPC);
		if(!pdsp->data[ii])
			continue;
		if(!strncmp(token, "perf_invoke_count", 17)) {
			FARF(ALWAYS, "fastrpc.dsp.%-20s : %" PRId64 " \n",
			     token, pdsp->data[ii]);
		} else {
			FARF(ALWAYS, "fastrpc.dsp.%-20s : %" PRId64 " us\n",
			     token, pdsp->data[ii]);
		}
	}
bail:
	if(nErr)
		VERIFY_WPRINTF("Warning: %s: Failed to get perf data from "
		               "dsp, nErr 0x%x\n",
		               __func__, nErr);
	return;
}

void
fastrpc_perf_update(int dev, remote_handle handle, uint32_t sc)
{
	struct fastrpc_perf *p = &gperf;

	if(!(p->perf_on && !IS_STATIC_HANDLE(handle) && p->freq > 0))
		return;

	p->count++;
	if(p->count % p->freq != 0)
		return;

	if(p->kernel.enable && !perf_v2_kernel)
		get_perf_kernel(dev, handle, sc);

	if(p->dsp.enable && !perf_v2_dsp)
		get_perf_adsp(handle, sc);

	return;
}

static int
perf_dsp_enable(int domain)
{
	int nErr = 0;
	int numKeys = 0, maxLen = 0;
	char *keys = NULL;
	int ii;

	keys = (char *)rpcmem_alloc_internal(0, RPCMEM_HEAP_DEFAULT,
	                                     PERF_KEY_STR_MAX);
	VERIFYC(gperf.dsp.keys = keys, AEE_ERPC);
	memset(keys, 0, PERF_KEY_STR_MAX);

	VERIFY(0
	       == (nErr = adsp_perf_get_keys(keys, PERF_KEY_STR_MAX, &maxLen,
	                                     &numKeys)));
	if((gperf.adsp_perf_handle = get_adsp_perf1_handle(domain))
	   != INVALID_HANDLE) {
		nErr
		    = adsp_perf1_get_keys(gperf.adsp_perf_handle, keys,
		                          PERF_KEY_STR_MAX, &maxLen, &numKeys);
		if(nErr) {
			FARF(ALWAYS,
			     "Warning 0x%x: %s: adsp_perf1 domains not "
			     "supported for domain %d\n",
			     nErr, __func__, domain);
			fastrpc_update_module_list(DOMAIN_LIST_DEQUEUE, domain,
			                           _const_adsp_perf1_handle,
			                           NULL, NULL);
			gperf.adsp_perf_handle = INVALID_HANDLE;
			VERIFY(0
			       == (nErr
			           = adsp_perf_get_keys(keys, PERF_KEY_STR_MAX,
			                                &maxLen, &numKeys)));
		}
	}
	VERIFYC(maxLen < PERF_KEY_STR_MAX && maxLen >= 0, AEE_ERPC);
	VERIFYC(numKeys < PERF_MAX_NUM_KEYS && numKeys >= 0, AEE_ERPC);
	gperf.dsp.maxLen = maxLen;
	gperf.dsp.numKeys = numKeys;
	for(ii = 0; ii < numKeys; ii++) {
		char *name = keys;
		keys += strlen(name) + 1;
		if(IS_KEY_ENABLED(name)) {
			if(gperf.adsp_perf_handle != INVALID_HANDLE) {
				VERIFY(0
				       == (nErr = adsp_perf1_enable(
					       gperf.adsp_perf_handle, ii)));
			} else {
				VERIFY(0 == (nErr = adsp_perf_enable(ii)));
			}
		}
	}
	FARF(RUNTIME_RPC_HIGH, "keys enable done maxLen %d numKeys %d", maxLen,
	     numKeys);
bail:
	if(nErr) {
		VERIFY_WPRINTF(
		    "Warning: %s: Failed to enable perf on dsp, nErr 0x%x\n",
		    __func__, nErr);
	}
	return nErr;
}

int
fastrpc_perf_init(int dev, int domain)
{
	int nErr = 0;
	struct fastrpc_perf *p = &gperf;
	struct perf_keys *pk = &gperf.kernel;
	struct perf_keys *pd = &gperf.dsp;

	pk->enable = fastrpc_get_property_int(FASTRPC_PERF_KERNEL, 0)
	             || fastrpc_config_is_perfkernel_enabled();
	pd->enable = fastrpc_get_property_int(FASTRPC_PERF_ADSP, 0)
	             || fastrpc_config_is_perfdsp_enabled();

	p->perf_on = (pk->enable || pd->enable) ? PERF_MODE : PERF_OFF;
	p->freq = fastrpc_get_property_int(FASTRPC_PERF_FREQ, 1000);
	VERIFYC(p->freq > 0, AEE_ERPC);
	p->process_trace_enabled
	    = fastrpc_get_property_int(FASTRPC_ENABLE_SYSTRACE, 0);
	if(p->perf_on) {
		check_perf_v2_enabled(domain);
	}
	p->count = 0;
	if(pk->enable) {
		VERIFY(0 == (nErr = ioctl_setmode(dev, PERF_MODE)));
		if(!perf_v2_kernel) {
			VERIFYC(NULL
			            != (pk->keys = (char *)calloc(
					    sizeof(char), PERF_KEY_STR_MAX)),
			        AEE_ENOMEMORY);
			VERIFY(0 == (nErr = perf_kernel_getkeys(dev)));
		}
	}

	if(pd->enable && (!perf_v2_dsp))
		perf_dsp_enable(domain);
bail:
	if(nErr) {
		FARF(ERROR,
		     "fastrpc perf init failed, nErr 0x%x (kernel %d, dsp %d) "
		     "with "
		     "frequency %d",
		     nErr, pk->enable, pd->enable, p->freq);
		p->perf_on = 0;
	} else {
		FARF(ALWAYS,
		     "%s: enabled systrace 0x%x and RPC traces (kernel %d, "
		     "dsp %d) with "
		     "frequency %d",
		     __func__, p->process_trace_enabled, pk->enable,
		     pd->enable, p->freq);
	}
	return nErr;
}

void
fastrpc_perf_deinit(void)
{
	struct fastrpc_perf *p = &gperf;
	if(p->kernel.keys) {
		free(p->kernel.keys);
		p->kernel.keys = NULL;
	}
	if(p->dsp.keys) {
		rpcmem_free_internal(p->dsp.keys);
		p->dsp.keys = NULL;
	}
	return;
}
