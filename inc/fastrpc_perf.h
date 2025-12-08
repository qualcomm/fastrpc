// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef FASTRPC_PERF_H
#define FASTRPC_PERF_H

#include <stdbool.h>

#include "remote.h"

/**
 * Kernel perf keys
 * C:      PERF_COUNT
 * F:      PERF_FLUSH
 * M:      PERF_MAP
 * CP:     PERF_COPY
 * L:      PERF_LINK
 * G:      PERF_GETARGS
 * P:      PERF_PUTARGS
 * INV:    PERF_INVARGS
 * INVOKE: PERF_INVOKE
 */

#define PERF_KERNEL_KEY_MAX (10)

/**
 * DSP perf keys
 * C:      perf_invoke_count
 * M_H:    perf_map_header
 * M:      perf_map_pages
 * G:      perf_get_args
 * INVOKE: perf_mod_invoke
 * P:      perf_put_args
 * CACHE:  perf_clean_cache
 * UM:     perf_unmap_pages
 * UM_H:   perf_hdr_unmap
 * R:      perf_rsp
 * E_R:    perf_early_rsp
 * J_S_T:  perf_job_start_time
 */

#define PERF_DSP_KEY_MAX (12)

bool is_kernel_perf_enabled();
bool is_dsp_perf_enabled(int domain);
void fastrpc_perf_update(int dev, remote_handle handle, uint32_t sc);
int fastrpc_perf_init(int dev, int domain);
void fastrpc_perf_deinit(void);

#endif // FASTRPC_PERF_H
