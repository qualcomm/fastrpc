// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef FASTRPC_HASH_TABLE_H
#define FASTRPC_HASH_TABLE_H

#include "uthash.h"

/* Add members to a struct to hash it using effective domain id */
#define ADD_DOMAIN_HASH() \
	int domain; \
	UT_hash_handle hh;

/* Declare hash-table struct and variable with given name */
#define DECLARE_HASH_TABLE(name, type) \
	typedef struct { \
		type *tbl; \
		pthread_mutex_t mut; \
	} name##_table; \
	static name##_table info;

/* Initialize hash-table and associated members */
#define HASH_TABLE_INIT(type) \
	do {\
		pthread_mutex_init(&info.mut, 0); \
	} while(0)

/* Delete & all entries in hash-table */
#define HASH_TABLE_CLEANUP(type) \
	do { \
		type *me = NULL, *tmp = NULL; \
		\
		pthread_mutex_lock(&info.mut); \
		HASH_ITER(hh, info.tbl, me, tmp) { \
			HASH_DEL(info.tbl, me); \
			free(me); \
		} \
		pthread_mutex_unlock(&info.mut); \
		pthread_mutex_destroy(&info.mut); \
	} while(0)

/* Declare a function to get hash-node of given type */
#define GET_HASH_NODE(type, domain, me) \
	do {\
		pthread_mutex_lock(&info.mut); \
		HASH_FIND_INT(info.tbl, &domain, me); \
		pthread_mutex_unlock(&info.mut); \
	} while(0)

/* Allocate new node of given type, set key and add to table */
#define ALLOC_AND_ADD_NEW_NODE_TO_TABLE(type, domain, me) \
	do { \
		pthread_mutex_lock(&info.mut); \
		HASH_FIND_INT(info.tbl, &domain, me); \
		if (!me) { \
			me = (type *)calloc(1, sizeof(type)); \
			if (!me) { \
				pthread_mutex_unlock(&info.mut); \
				nErr = AEE_ENOMEMORY; \
				goto bail; \
			} \
			me->domain = domain; \
			HASH_ADD_INT(info.tbl, domain, me); \
		} \
		pthread_mutex_unlock(&info.mut); \
	} while(0)

/*
 * The macros above all assume a single, file-scope singleton table
 * named "info" (DECLARE_HASH_TABLE() defines it, the rest use it
 * implicitly) -- i.e. at most one hash table per translation unit.
 *
 * The "_OBJ" variants below are the same idiom, generalized to take
 * an explicit table+mutex variable instead of assuming "info". This
 * supports two cases the singleton form can't: (a) more than one
 * hash table declared in the same .c file, and (b) a hash table
 * embedded as a field inside another struct, so that each instance
 * of that struct gets its own independent table (e.g. a per-buffer
 * "which domains is this buffer mapped on" set, where every buffer
 * needs its own table, not one shared global).
 */

/* Declare the hash-table struct type only (no singleton instance).
 * Use this instead of DECLARE_HASH_TABLE() when the table needs to
 * be its own named variable (a local, a global, or embedded as a
 * struct field) rather than the implicit per-file "info" singleton. */
#define DECLARE_HASH_TABLE_TYPE(name, type) \
	typedef struct { \
		type *tbl; \
		pthread_mutex_t mut; \
	} name##_table;

/* Initialize an explicit hash-table variable */
#define HASH_TABLE_OBJ_INIT(tblvar) \
	do {\
		pthread_mutex_init(&(tblvar).mut, 0); \
	} while(0)

/* Delete all entries in an explicit hash-table variable and destroy
 * its mutex */
#define HASH_TABLE_OBJ_CLEANUP(type, tblvar) \
	do { \
		type *me = NULL, *tmp = NULL; \
		\
		pthread_mutex_lock(&(tblvar).mut); \
		HASH_ITER(hh, (tblvar).tbl, me, tmp) { \
			HASH_DEL((tblvar).tbl, me); \
			free(me); \
		} \
		pthread_mutex_unlock(&(tblvar).mut); \
		pthread_mutex_destroy(&(tblvar).mut); \
	} while(0)

/* Look up a node of given type in an explicit hash-table variable */
#define GET_HASH_NODE_OBJ(tblvar, domain, me) \
	do {\
		pthread_mutex_lock(&(tblvar).mut); \
		HASH_FIND_INT((tblvar).tbl, &domain, me); \
		pthread_mutex_unlock(&(tblvar).mut); \
	} while(0)

/* Allocate new node of given type, set key and add to an explicit
 * hash-table variable */
#define ALLOC_AND_ADD_NEW_NODE_TO_TABLE_OBJ(type, tblvar, domain, me) \
	do { \
		pthread_mutex_lock(&(tblvar).mut); \
		HASH_FIND_INT((tblvar).tbl, &domain, me); \
		if (!me) { \
			me = (type *)calloc(1, sizeof(type)); \
			if (!me) { \
				pthread_mutex_unlock(&(tblvar).mut); \
				nErr = AEE_ENOMEMORY; \
				goto bail; \
			} \
			me->domain = domain; \
			HASH_ADD_INT((tblvar).tbl, domain, me); \
		} \
		pthread_mutex_unlock(&(tblvar).mut); \
	} while(0)

#endif // FASTRPC_HASH_TABLE_H
