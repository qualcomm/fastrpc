// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef PLS_H
#define PLS_H

#include "AEEStdDef.h"
#include "HAP_farf.h"
#include "verify.h"
#include <stdlib.h>

struct PLS;

struct plskey {
	uintptr_t type;
	uintptr_t key;
};

struct PLS {
	uintptr_t next;
	struct plskey key;
	void (*dtor)(void *data);
	uint64_t data[1];
};

struct pls_table {
	atomic_uintptr_t lst;
	atomic_uint uRefs;
	uint32_t primThread;
};

unsigned int
atomic_CompareOrAdd(atomic_uint *puDest, unsigned int uCompare,
                    unsigned int nAdd)
{
	unsigned int previous;
	unsigned int current;
	unsigned int result;

	do {
		current = atomic_load(puDest);
		previous = current;
		result = current;
		if(current != uCompare) {
			atomic_compare_exchange_strong(puDest, &previous,
			                               current + nAdd);
			if(previous == current) {
				result = current + nAdd;
			}
		}
	} while(previous != current);

	return result;
}

/**
 * initialize on every thread and stick the pls_thread_deinit
 * function into the threads tls
 */
static __inline int
pls_thread_init(struct pls_table *me, uintptr_t tid)
{
	if(tid == me->primThread) {
		return 0;
	}
	while(1) {
		unsigned int current;

		current = atomic_load(&me->uRefs);
		if(current == 0)
			return -1;

		if(atomic_compare_exchange_strong(&me->uRefs, &current,
		                                  current + 1))
			return 0;
	}
}

/* call this constructor before the first thread creation with the
 * first threads id
 */
static __inline void
pls_ctor(struct pls_table *me, uintptr_t primThread)
{
	me->uRefs = 1;
	me->primThread = primThread;
}

static __inline struct pls_table *
pls_thread_deinit(struct pls_table *me)
{
	if(me && 0 != me->uRefs && 1 == atomic_fetch_sub(&me->uRefs, 1)) {
		struct PLS *lst, *next;
		lst = (struct PLS *)atomic_exchange(&me->lst, 0);
		while(lst) {
			next = (struct PLS *)lst->next;
			if(lst->dtor) {
				FARF(HIGH, "pls dtor %p", lst->dtor);
				lst->dtor((void *)lst->data);
			}
			free(lst);
			lst = next;
		}
		return me;
	}
	return 0;
}

/**
 * adds a new key to the local storage, overriding
 * any previous value at the key.  Overriding the key
 * does not cause the destructor to run.
 *
 * @param type, type part of the key to be used for lookup,
          these should be static addresses.
 * @param key, the key to be used for lookup
 * @param size, the size of the data
 * @param ctor, constructor that takes a context and memory of size
 * @param ctx, constructor context passed as the first argument to ctor
 * @param dtor, destructor to run at pls shutdown
 * @param ppo, output data
 * @retval, 0 for success
 */
static __inline int
pls_add(struct pls_table *me, uintptr_t type, uintptr_t key, int size,
        int (*ctor)(void *ctx, void *data), void *ctx,
        void (*dtor)(void *data), void **ppo)
{
	int nErr = 0;
	struct PLS *pls = 0;
	uintptr_t prev;
	VERIFY(me->uRefs != 0);
	VERIFY(0
	       != (pls = (struct PLS *)calloc(1, size + sizeof(*pls)
	                                             - sizeof(pls->data))));
	if(ctor) {
		VERIFY(0 == ctor(ctx, (void *)pls->data));
	}
	pls->dtor = dtor;
	pls->key.type = type;
	pls->key.key = key;
	do {
		pls->next = atomic_load(&me->lst);
		prev = pls->next;
		atomic_compare_exchange_strong(&me->lst, &prev,
		                               (uintptr_t)pls);
	} while(prev != pls->next);
	if(ppo) {
		*ppo = (void *)pls->data;
	}
	FARF(HIGH, "pls added %p", dtor);
bail:
	if(nErr && pls) {
		free(pls);
	}
	return nErr;
}

static __inline int pls_lookup(struct pls_table *me, uintptr_t type,
                               uintptr_t key, void **ppo);

/**
 * like add, but will only add 1 item if two threads try to add at the same
 * time.  returns item if its already there, otherwise tries to add. ctor may
 * be called twice callers should avoid calling pls_add which will override the
 * singleton
 */
static __inline int
pls_add_lookup_singleton(struct pls_table *me, uintptr_t type, uintptr_t key,
                         int size, int (*ctor)(void *ctx, void *data),
                         void *ctx, void (*dtor)(void *data), void **ppo)
{
	int nErr = 0;
	struct PLS *pls = 0;
	uintptr_t prev;
	if(0 == pls_lookup(me, type, key, ppo)) {
		return 0;
	}
	VERIFY(me->uRefs != 0);
	VERIFY(0
	       != (pls = (struct PLS *)calloc(1, size + sizeof(*pls)
	                                             - sizeof(pls->data))));
	if(ctor) {
		VERIFY(0 == ctor(ctx, (void *)pls->data));
	}
	pls->dtor = dtor;
	pls->key.type = type;
	pls->key.key = key;
	do {
		pls->next = atomic_load(&me->lst);
		if(0 == pls_lookup(me, type, key, ppo)) {
			if(pls->dtor) {
				pls->dtor((void *)pls->data);
			}
			free(pls);
			return 0;
		}

		prev = pls->next;
		atomic_compare_exchange_strong(&me->lst, &prev,
		                               (uintptr_t)pls);
	} while(prev != pls->next);
	if(ppo) {
		*ppo = (void *)pls->data;
	}
bail:
	if(nErr && pls) {
		free(pls);
	}
	return nErr;
}

/**
 * finds the last data pointer added for key to the local storage
 *
 * @param key, the key to be used for lookup
 * @param ppo, output data
 * @retval, 0 for success
 */

static __inline int
pls_lookup(struct pls_table *me, uintptr_t type, uintptr_t key, void **ppo)
{
	struct PLS *lst;
	for(lst = (struct PLS *)atomic_load(&me->lst);
	    me->uRefs != 0 && lst != 0; lst = (struct PLS *)lst->next) {
		if(lst->key.type == type && lst->key.key == key) {
			if(ppo) {
				*ppo = lst->data;
			}
			return 0;
		}
	}
	return -1;
}

#endif // PLS_H
