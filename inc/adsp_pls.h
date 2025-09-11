// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ADSP_PLS_H
#define ADSP_PLS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
/**
 * internal header
 */

/**
 * @file adsp_pls
 *
 * adsp process local storage is local storage for the fastrpc hlos
 * process context.

 * When used from within a fastrpc started thread this will attach
 * desturctors to the lifetime of the hlos process that is making the
 * rpc calls.  Users can use this to store context for the lifetime of
 * the calling process on the hlos.
 */

/**
 * adds a new key to the local storage, overriding
 * any previous value at the key.  Overriding the key
 * does not cause the destructor to run.
 *
 * @param type, type part of the key to be used for lookup,
 *        these should be static addresses, like the address of a function.
 * @param key, the key to be used for lookup
 * @param size, the size of the data
 * @param ctor, constructor that takes a context and memory of size
 * @param ctx, constructor context passed as the first argument to ctor
 * @param dtor, destructor to run at pls shutdown
 * @param ppo, output data
 * @retval, 0 for success
 */
int adsp_pls_add(uintptr_t type, uintptr_t key, int size,
                 int (*ctor)(void *ctx, void *data), void *ctx,
                 void (*dtor)(void *), void **ppo);

/**
 * Like add, but will only add 1 item, and return the same item on the
 * next add.  If two threads try to call this function at teh same time
 * they will both receive the same value as a result, but the constructors
 * may be called twice.
 * item if its already there, otherwise tries to add.
 * ctor may be called twice
 * callers should avoid calling pls_add which will override the singleton
 */
int adsp_pls_add_lookup(uintptr_t type, uintptr_t key, int size,
                        int (*ctor)(void *ctx, void *data), void *ctx,
                        void (*dtor)(void *), void **ppo);

/**
 * finds the last data pointer added for key to the local storage
 *
 * @param key, the key to be used for lookup
 * @param ppo, output data
 * @retval, 0 for success
 */
int adsp_pls_lookup(uintptr_t type, uintptr_t key, void **ppo);

/**
 * force init/deinit
 */
int gpls_init(void);
void gpls_deinit(void);

#ifdef __cplusplus
}
#endif
#endif // ADSP_PLS_H
