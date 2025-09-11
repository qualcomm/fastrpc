// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef FASTRPC_HASH_TABLE_H
#define FASTRPC_HASH_TABLE_H

#include "uthash.h"

/* Add members to a struct to hash it using effective domain id */
#define ADD_DOMAIN_HASH()                                                     \
	int domain;                                                           \
	UT_hash_handle hh;

/* Declare hash-table struct and variable with given name */
#define DECLARE_HASH_TABLE(name, type)                                        \
	typedef struct {                                                      \
		type *tbl;                                                    \
		pthread_mutex_t mut;                                          \
	} name##_table;                                                       \
	static name##_table info;

/* Initialize hash-table and associated members */
#define HASH_TABLE_INIT(type)                                                 \
	do {                                                                  \
		pthread_mutex_init(&info.mut, 0);                             \
	} while(0)

/* Delete & all entries in hash-table */
#define HASH_TABLE_CLEANUP(type)                                              \
	do {                                                                  \
		type *me = NULL, *tmp = NULL;                                 \
                                                                              \
		pthread_mutex_lock(&info.mut);                                \
		HASH_ITER(hh, info.tbl, me, tmp)                              \
		{                                                             \
			HASH_DEL(info.tbl, me);                               \
			free(me);                                             \
		}                                                             \
		pthread_mutex_unlock(&info.mut);                              \
		pthread_mutex_destroy(&info.mut);                             \
	} while(0)

/* Declare a function to get hash-node of given type */
#define GET_HASH_NODE(type, domain, me)                                       \
	do {                                                                  \
		pthread_mutex_lock(&info.mut);                                \
		HASH_FIND_INT(info.tbl, &domain, me);                         \
		pthread_mutex_unlock(&info.mut);                              \
	} while(0)

/* Allocate new node of given type, set key and add to table */
#define ALLOC_AND_ADD_NEW_NODE_TO_TABLE(type, domain, me)                     \
	do {                                                                  \
		pthread_mutex_lock(&info.mut);                                \
		HASH_FIND_INT(info.tbl, &domain, me);                         \
		if(!me) {                                                     \
			me = (type *)calloc(1, sizeof(type));                 \
			if(!me) {                                             \
				pthread_mutex_unlock(&info.mut);              \
				nErr = AEE_ENOMEMORY;                         \
				goto bail;                                    \
			}                                                     \
			me->domain = domain;                                  \
			HASH_ADD_INT(info.tbl, domain, me);                   \
		}                                                             \
		pthread_mutex_unlock(&info.mut);                              \
	} while(0)

#endif // FASTRPC_HASH_TABLE_H
