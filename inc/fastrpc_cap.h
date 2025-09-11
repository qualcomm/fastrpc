// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef __FASTRPC_CAP_H__
#define __FASTRPC_CAP_H__

// Status notification version 2 capability value
#define STATUS_NOTIF_V2 3

/**
 * @brief reads specific capability from DSP/Kernel and returns
 *  attributeID - specifies which capability is of interest
 *  domain - specific domain ID (DSP or the session running on DSP)
 *  capability - integer return value. in case of boolean - 0/1 is returned.
 **/
int fastrpc_get_cap(uint32_t domain, uint32_t attributeID,
                    uint32_t *capability);
/**
 * @brief Checks whether DMA reverse RPC capability is supported
 * by DSP.
 **/
uint32_t get_dsp_dma_reverse_rpc_map_capability(int domain);
/**
 * @brief Checks kernel if error codes returned are latest to the user
 **/
int check_error_code_change_present(void);
/**
 * @brief Lastest version of status notification features is enabled
 **/
int is_status_notif_version2_supported(int domain);
/**
 * @brief Checks if user space DMA allocation is supported
 **/
int is_userspace_allocation_supported(void);
/**
 * @brief API to check if DSP process supports the configuration buffer
 *(procbuf)
 **/
int is_proc_sharedbuf_supported_dsp(int domain);

#endif /*__FASTRPC_CAP_H__*/
