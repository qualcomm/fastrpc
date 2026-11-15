// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/*
 * fastrpc_base_test_imp.c - DSP-side implementation of the fastrpc_base_test interface.
 *
 * The QAIC-generated skeleton dispatches handle lifecycle calls and the
 * calculator and HAP sample operations implemented below.
 */

#include <stdatomic.h>
#include <stdint.h>

#include "AEEStdErr.h"
#include "HAP_farf.h"
#include "fastrpc_base_test.h"

#define MAX_FASTRPC_BASE_TEST_HANDLES 8
#define FARF_RUNTIME_LOGGING_MASK 0x1f

/* Each set bit marks an allocated slot. Handles are slot indices plus one,
 * reserving zero as the invalid/uninitialized handle. This protects slot
 * allocation and release, but does not wait for calls already using a handle
 * when it is closed. */
static _Atomic uint32_t g_handle_bitmap;

static int slot_for_handle(remote_handle64 h, unsigned int *slot)
{
    if (h == 0 || h > MAX_FASTRPC_BASE_TEST_HANDLES)
        return 0;

    *slot = (unsigned int)(h - 1);
    return 1;
}

static int handle_is_valid(remote_handle64 h)
{
    unsigned int slot;

    if (!slot_for_handle(h, &slot))
        return 0;

    return (atomic_load(&g_handle_bitmap) & (1u << slot)) != 0;
}

int fastrpc_base_test_open(const char *uri, remote_handle64 *h)
{
    (void)uri;

    if (h == NULL)
        return AEE_EBADPARM;

    int ret = HAP_setFARFRuntimeLoggingParams(FARF_RUNTIME_LOGGING_MASK, NULL, 0);
    if (ret != AEE_SUCCESS)
        return ret;

    uint32_t observed = atomic_load(&g_handle_bitmap);
    for (;;) {
        unsigned int slot;
        uint32_t desired;

        for (slot = 0; slot < MAX_FASTRPC_BASE_TEST_HANDLES; slot++) {
            if (!(observed & (1u << slot)))
                break;
        }
        if (slot == MAX_FASTRPC_BASE_TEST_HANDLES)
            return AEE_ENOMEMORY;

        desired = observed | (1u << slot);
        if (atomic_compare_exchange_weak(&g_handle_bitmap, &observed, desired)) {
            *h = (remote_handle64)(slot + 1);
            return AEE_SUCCESS;
        }
    }
}

int fastrpc_base_test_close(remote_handle64 h)
{
    unsigned int slot;
    uint32_t mask, previous;

    if (!slot_for_handle(h, &slot))
        return AEE_EBADPARM;

    mask = 1u << slot;
    previous = atomic_fetch_and(&g_handle_bitmap, ~mask);
    if (!(previous & mask))
        return AEE_EBADPARM;

    return AEE_SUCCESS;
}

int fastrpc_base_test_calculator_sum(remote_handle64 h, const int *values, int values_len,
                                int64_t *result)
{
    int64_t sum = 0;

    if (!handle_is_valid(h) || values == NULL || result == NULL || values_len <= 0)
        return AEE_EBADPARM;

    for (int i = 0; i < values_len; i++)
        sum += values[i];
    *result = sum;
    return AEE_SUCCESS;
}

int fastrpc_base_test_calculator_max(remote_handle64 h, const int *values, int values_len,
                                int *result)
{
    if (!handle_is_valid(h) || values == NULL || result == NULL || values_len <= 0)
        return AEE_EBADPARM;

    *result = values[0];
    for (int i = 1; i < values_len; i++) {
        if (values[i] > *result)
            *result = values[i];
    }
    return AEE_SUCCESS;
}

int fastrpc_base_test_hap_example_farf_runtime(remote_handle64 h, int enable)
{
    int ret;

    if (!handle_is_valid(h))
        return AEE_EBADPARM;

    ret = HAP_setFARFRuntimeLoggingParams(enable ? FARF_RUNTIME_LOGGING_MASK : 0, NULL, 0);
    if (ret != AEE_SUCCESS)
        return ret;

    FARF(RUNTIME_LOW, "FARF low logging %s", enable ? "enabled" : "disabled");
    FARF(RUNTIME_MEDIUM, "FARF medium logging %s", enable ? "enabled" : "disabled");
    FARF(RUNTIME_HIGH, "FARF high logging %s", enable ? "enabled" : "disabled");
    FARF(RUNTIME_ERROR, "FARF error logging %s", enable ? "enabled" : "disabled");
    return AEE_SUCCESS;
}
