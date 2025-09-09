// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef LISTENER_H
#define LISTENER_H

#include <semaphore.h>
#include <dlfcn.h>

#define   MSG(a, b, c)              printf(__FILE_LINE__ ":" c )
#define   MSG_1(a, b, c, d)         printf(__FILE_LINE__ ":" c , d)
#define   MSG_2(a, b, c, d, e)      printf(__FILE_LINE__ ":" c , d, e)
#define   MSG_3(a, b, c, d, e, f)   printf(__FILE_LINE__ ":" c , d, e, f)
#define   MSG_4(a, b, c, d, e, f,g) printf(__FILE_LINE__ ":" c , d, e, f, g)

#define DLW_RTLD_NOW RTLD_NOW
#define dlw_Open dlopen
#define dlw_Sym dlsym
#define dlw_Close dlclose
#define dlw_Error dlerror

/*
 * API to initialize globals and internal data structures used in listener modules
 */
int listener_init(void);
/*
 * API to de-initialize globals and internal data structures used in listener modules
 */
void listener_deinit(void);
/*
 * API to initialize domain specific data strcutures used in listener modules
 */
int listener_domain_init(int domain, int update_requested, sem_t *r_sem);
/*
 * API to de-initialize domain specific data strcutures used in listener modules
 */
void listener_domain_deinit(int domain);

#endif
