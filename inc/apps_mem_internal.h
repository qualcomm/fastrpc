// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef __APPS_MEM_INTERNAL_H__
#define __APPS_MEM_INTERNAL_H__

#include "apps_mem.h"

/**
 * @brief API to initialize the apps_mem module
 * initializes internal data structures and global variables
 **/
int apps_mem_init(int domain);
/**
 * @brief API to de-initialize the apps_mem module
 * de-initializes internal data structures and global variables
 **/
void apps_mem_deinit(int domain);

#endif /*__APPS_MEM_INTERNAL_H__*/
