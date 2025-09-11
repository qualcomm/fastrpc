// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#define FARF_ERROR 1
#include "platform_libs.h"
#include "AEEStdErr.h"
#include "AEEstd.h"
#include "HAP_farf.h"
#include "verify.h"
#include <assert.h>
#include <stdio.h>

extern struct platform_lib *(*pl_list[])(void);

int
pl_lib_init(struct platform_lib *(*plf)(void))
{
	int nErr = AEE_SUCCESS;
	struct platform_lib *pl = plf();
	if(!atomic_fetch_add(&pl->uRefs, 1)) {
		if(pl->init) {
			FARF(RUNTIME_RPC_HIGH, "calling init for %s",
			     pl->name);
			nErr = pl->init();
			FARF(RUNTIME_RPC_HIGH, "init for %s returned %x",
			     pl->name, nErr);
		}
		pl->nErr = nErr;
	}
	if(pl->nErr != AEE_SUCCESS) {
		VERIFY_EPRINTF("Error %x: %s init failed", nErr, pl->name);
	}
	return pl->nErr;
}

void
pl_lib_deinit(struct platform_lib *(*plf)(void))
{
	struct platform_lib *pl = plf();
	if(1 == atomic_fetch_sub(&pl->uRefs, 1)) {
		if(pl->deinit && pl->nErr == 0) {
			pl->deinit();
		}
	}
	return;
}

static int
pl_init_lst(struct platform_lib *(*lst[])(void))
{
	int nErr = AEE_SUCCESS;
	int ii;
	for(ii = 0; lst[ii] != 0; ++ii) {
		nErr = pl_lib_init(lst[ii]);
		if(nErr != 0) {
			break;
		}
	}
	if(nErr != AEE_SUCCESS) {
		VERIFY_EPRINTF("Error %x: plinit failed\n", nErr);
	}
	return nErr;
}
int
pl_init(void)
{
	int nErr = pl_init_lst(pl_list);
	return nErr;
}

static void
pl_deinit_lst(struct platform_lib *(*lst[])(void))
{
	int size, ii;
	for(size = 0; lst[size] != 0; ++size) {
		;
	}
	for(ii = size - 1; ii >= 0; --ii) {
		pl_lib_deinit(lst[ii]);
	}
	return;
}

void
pl_deinit(void)
{
	pl_deinit_lst(pl_list);
	return;
}
