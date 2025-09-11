// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef PTHREAD_RW_MUTEX_H
#define PTHREAD_RW_MUTEX_H

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

#endif // PTHREAD_RW_MUTEX_H
