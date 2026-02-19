// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef FASTRPC_DEVICE_DISCOVERY_H
#define FASTRPC_DEVICE_DISCOVERY_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

/**
 * @brief Initialize device discovery subsystem
 * 
 * This function:
 * - Discovers all available DSP devices in /dev/accel/
 * - Creates device-to-domain mapping via IOCTL queries
 * - Starts inotify monitoring for SSR detection
 * 
 * This function is thread-safe and can be called multiple times.
 * Subsequent calls will return success immediately if already initialized.
 * 
 * @return 0 on success, error code on failure
 */
int fastrpc_discovery_init(void);

/**
 * @brief Cleanup device discovery subsystem
 * 
 * Stops inotify monitoring thread and frees all resources.
 * Should be called during library cleanup.
 */
void fastrpc_discovery_deinit(void);

/**
 * @brief Get device path for a given domain
 * 
 * Returns the device path (e.g., "/dev/accel/accel1") for the
 * specified domain. The mapping is automatically updated on SSR
 * via inotify monitoring.
 * 
 * This function performs lazy initialization if not already done.
 * 
 * @param domain_id Domain ID (ADSP_DOMAIN_ID, CDSP_DOMAIN_ID, etc.)
 * @param dev_path  Output buffer for device path (min 64 bytes)
 * @param path_len  Size of dev_path buffer
 * 
 * @return 0 on success, error code if domain not found
 */
int fastrpc_discovery_get_device_path(int domain_id, char *dev_path, 
                                      size_t path_len);

/**
 * @brief Check if a domain has a valid device mapping
 * 
 * @param domain_id Domain ID to check
 * @return true if device is available, false otherwise
 */
bool fastrpc_discovery_is_device_available(int domain_id);

/**
 * @brief Force rediscovery of all devices
 * 
 * Useful for testing or manual recovery scenarios.
 * Normally not needed as inotify handles automatic rediscovery.
 * 
 * @return 0 on success, error code on failure
 */
int fastrpc_discovery_refresh(void);

/**
 * @brief Get statistics about device discovery
 * 
 * @param domain_id Domain ID
 * @param access_count Output: number of times device was accessed (can be NULL)
 * @param last_discovery Output: timestamp of last discovery (can be NULL)
 * 
 * @return 0 on success, error code on failure
 */
int fastrpc_discovery_get_stats(int domain_id, uint64_t *access_count,
                                struct timespec *last_discovery);

#endif /* FASTRPC_DEVICE_DISCOVERY_H */
