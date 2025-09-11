// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef HAP_PLS_H
#define HAP_PLS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Process local storage is local storage for the hlos process context.
 *
 * Warning, this API should only be called from within a thread started by
 * FastRPC, and not from any user created threads via the qurt apis.
 *
 * When used from within a FastRPC started thread this will attach
 * desturctors to the lifetime of the HLOS process that is making the
 * rpc calls.  Users can use this to store context for the lifetime of
 * the calling process on the hlos.
 *
 * Recovering instances
 * --------------------
 *
 * To maintain the same instance structure for a caller from the HLOS users
 * can use the HAP_pls_add_lookup api, which will lookup the key, and add it
 * if its not already present.
 * For example:
 *
 *    static int my_instance(struct my_struct* me)  {
 *       return HAP_pls_add_lookup((uintptr_t)my_ctor,   //type, some unique
 * static address 0,                //key, for different type instances
 *                                  sizeof(*me),      //size of our struture
 *                                  my_ctor,          //structure ctor
 *                                  0,                //aditional user context
 * for ctor my_dtor,          //desturctor &me);             //result
 *    }
 *
 * First call to my_instance will initialize the structure by allocating it and
 * calling my_ctor. Second call to my_instance will return the created
 * instance. This API is thread safe, but when two threads try to intialize the
 * structure the first time they may both create an instance, but only 1 will
 * be returned. The destructor will be called when the HLOS process exits.
 *
 * See HAP_pls_add and HAP_pls_add_lookup.
 *
 * Exit Hooks
 * ----------
 *
 * Users can use either HAP_pls_add_lookup or HAP_pls_add to add a destructor
 * that will be called when the HLOS process exits.  The main difference
 * between the two functions is that HAP_pls_add will always add, and the last
 * instance added will be the one returned by HAP_pls_lookup.
 *
 *
 */

/**
 * adds a new type/key to the local storage, overriding
 * any previous value at the key.  Overriding the key
 * does not cause the destructor to run.  Destructors are
 * run when the HLOS process exits.
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
int HAP_pls_add(uintptr_t type, uintptr_t key, int size,
                int (*ctor)(void *ctx, void *data), void *ctx,
                void (*dtor)(void *), void **ppo);

/**
 * Like add, but will only add 1 item, and return the same item on the
 * next add.  If two threads try to call this function at teh same time
 * they will both receive the same value as a result, but the constructors
 * may be called twice.
 * item if its already there, otherwise tries to add.
 * ctor may be called twice
 * callers should avoid calling pls_add for the same type/key which will
 * override the singleton
 */
int HAP_pls_add_lookup(uintptr_t type, uintptr_t key, int size,
                       int (*ctor)(void *ctx, void *data), void *ctx,
                       void (*dtor)(void *), void **ppo);

/**
 * finds the last data pointer added for key to the local storage
 *
 * @param key, the key to be used for lookup
 * @param ppo, output data
 * @retval, 0 for success
 */
int HAP_pls_lookup(uintptr_t type, uintptr_t key, void **ppo);

#ifdef __cplusplus
}
#endif
#endif // HAP_PLS_H
