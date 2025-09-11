// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "HAP_farf.h"
#include "HAP_pls.h"
#include "adsp_pls.h"
#include "platform_libs.h"
#include "pls.h"
#include "version.h"

static struct pls_table gpls;
const char pls_version[] = VERSION_STRING;
int
gpls_init(void)
{
	pls_ctor(&gpls, 1);
	return 0;
}

void
gpls_deinit(void)
{
	pls_thread_deinit(&gpls);
}

int
HAP_pls_add(uintptr_t type, uintptr_t key, int size,
            int (*ctor)(void *ctx, void *data), void *ctx,
            void (*dtor)(void *), void **ppo)
{
	return pls_add(&gpls, type, key, size, ctor, ctx, dtor, ppo);
}

int
HAP_pls_add_lookup(uintptr_t type, uintptr_t key, int size,
                   int (*ctor)(void *ctx, void *data), void *ctx,
                   void (*dtor)(void *), void **ppo)
{
	return pls_add_lookup_singleton(&gpls, type, key, size, ctor, ctx,
	                                dtor, ppo);
}

int
HAP_pls_lookup(uintptr_t type, uintptr_t key, void **ppo)
{
	return pls_lookup(&gpls, type, key, ppo);
}

int
adsp_pls_add(uintptr_t type, uintptr_t key, int size,
             int (*ctor)(void *ctx, void *data), void *ctx,
             void (*dtor)(void *), void **ppo)
{
	return pls_add(&gpls, type, key, size, ctor, ctx, dtor, ppo);
}

int
adsp_pls_add_lookup(uintptr_t type, uintptr_t key, int size,
                    int (*ctor)(void *ctx, void *data), void *ctx,
                    void (*dtor)(void *), void **ppo)
{
	return pls_add_lookup_singleton(&gpls, type, key, size, ctor, ctx,
	                                dtor, ppo);
}

int
adsp_pls_lookup(uintptr_t type, uintptr_t key, void **ppo)
{
	return pls_lookup(&gpls, type, key, ppo);
}

PL_DEFINE(gpls, gpls_init, gpls_deinit)
