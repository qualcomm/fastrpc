// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef MUTEX_H
#define MUTEX_H

#if (defined __qdsp6__) || (defined __hexagon__)
#include "qurt_mutex.h"

#define RW_MUTEX_T qurt_mutex_t
#define RW_MUTEX_CTOR(mut) qurt_mutex_init(&(mut))
#define RW_MUTEX_LOCK_READ(mut) qurt_mutex_lock(&(mut))
#define RW_MUTEX_UNLOCK_READ(mut) qurt_mutex_unlock(&(mut))
#define RW_MUTEX_LOCK_WRITE(mut) qurt_mutex_lock(&(mut))
#define RW_MUTEX_UNLOCK_WRITE(mut) qurt_mutex_unlock(&(mut))
#define RW_MUTEX_DTOR(mut) qurt_mutex_destroy(&(mut))

#elif (1 == __linux) || (1 == __linux__) || (1 == __gnu_linux__)              \
    || (1 == linux)

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

/* asserts may be compiled out, this should always be present */
#define ABORT_FAIL(ff)                                                        \
	do {                                                                  \
		if(!(ff)) {                                                   \
			fprintf(stderr,                                       \
			        "assertion \"%s\" failed: file \"%s\", line " \
			        "%d\n",                                       \
			        #ff, __FILE__, __LINE__);                     \
			abort();                                              \
		}                                                             \
	} while(0)

#define RW_MUTEX_T pthread_rwlock_t
#define RW_MUTEX_CTOR(mut) ABORT_FAIL(0 == pthread_rwlock_init(&(mut), 0))
#define RW_MUTEX_LOCK_READ(mut) ABORT_FAIL(0 == pthread_rwlock_rdlock(&(mut)))
#define RW_MUTEX_UNLOCK_READ(mut)                                             \
	ABORT_FAIL(0 == pthread_rwlock_unlock(&(mut)))
#define RW_MUTEX_LOCK_WRITE(mut) ABORT_FAIL(0 == pthread_rwlock_wrlock(&(mut)))
#define RW_MUTEX_UNLOCK_WRITE(mut)                                            \
	ABORT_FAIL(0 == pthread_rwlock_unlock(&(mut)))
#define RW_MUTEX_DTOR(mut) ABORT_FAIL(0 == pthread_rwlock_destroy(&(mut)))

#else

#include "AEEstd.h"

#define RW_MUTEX_T uint32_t
#define RW_MUTEX_CTOR(mut) mut = 0
#define RW_MUTEX_LOCK_READ(mut)                                               \
	do {                                                                  \
		assert(STD_BIT_TEST(&mut, 1) == 0);                           \
		assert(STD_BIT_TEST(&mut, 2) == 0);                           \
		STD_BIT_SET(&mut, 1);                                         \
	} while(0)

#define RW_MUTEX_UNLOCK_READ(mut)                                             \
	do {                                                                  \
		assert(STD_BIT_TEST(&mut, 1));                                \
		assert(STD_BIT_TEST(&mut, 2) == 0);                           \
		STD_BIT_CLEAR(&mut, 1);                                       \
	} while(0)

#define RW_MUTEX_LOCK_WRITE(mut)                                              \
	do {                                                                  \
		assert(STD_BIT_TEST(&mut, 1) == 0);                           \
		assert(STD_BIT_TEST(&mut, 2) == 0);                           \
		STD_BIT_SET(&mut, 2);                                         \
	} while(0)

#define RW_MUTEX_UNLOCK_WRITE(mut)                                            \
	do {                                                                  \
		assert(STD_BIT_TEST(&mut, 1) == 0);                           \
		assert(STD_BIT_TEST(&mut, 2));                                \
		STD_BIT_CLEAR(&mut, 2);                                       \
	} while(0)

#define RW_MUTEX_DTOR(mut) mut = 0

#endif
#endif // MUTEX_H
