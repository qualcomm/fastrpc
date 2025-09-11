// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef __FASTRPC_CONFIG_H__
#define __FASTRPC_CONFIG_H__

#include "AEEstd.h"

/* Maximum number of panic error codes allowed in debug config. */
#define MAX_PANIC_ERR_CODES 32

/**
 * @enum err_codes
 * @brief stores all error codes for which panic will be raised
 * , when the error happens on DSP/APSS
 **/
struct err_codes {
	int err_code[MAX_PANIC_ERR_CODES]; /* Panic err codes read from debug
	                                      config. */
	int num_err_codes; /* Number of panic error codes read from debug
	                      config. */
};

/**
 * @file fastrpc_config
 *
 * fastRPC library has few configurable options that can used
 * in debugging. library looks for a ".debugconfig" file in the
 * DSP search path during the process start.
 * Any and all debug configurations mentioned in this file is
 * enabled by fastRPC.
 * Note: all these debug features are enabled only on debug devices
 * and not available on production devices.
 *
 * Debug features available are
 * 1)  Crash on Error codes
 * 2)  PD dump enablement
 * 3)  Remote call timeout
 * 4)  Kernel Perf counters
 * 5)  DSP Perf counters
 * 6)  DSP Runtime FARF
 * 7)  APSS Runtime FARF
 * 8)  DSP Memory Logging
 * 9)  QTF Tracing
 * 10) Heap caller level
 * 11) UAF checks in heap
 * 12) Debug level logs
 * 13) QXDM Log packet
 * 14) Leak detection in Heap
 * 15) Call stack
 *
 **/

/**
 * fastrpc_config_get_errcodes
 *
 * @brief returns the list of error codes from .debugconfig
 * that should induce a crash. Useful in debugging issues where
 * there are errors.
 *
 * @param void
 * @return struct error_codes - list of error codes
 **/
struct err_codes *fastrpc_config_get_errcodes(void);
/**
 * fastrpc_config_get_rpctimeout
 *
 * @brief returns the timeout value configured in .debugconfig file.
 * used by the remote APIs to timeout when the DSP is taking more time
 * than expected.
 *
 * @param void
 * @return integer - timeout value
 **/
int fastrpc_config_get_rpctimeout(void);
/**
 * fastrpc_config_is_pddump_enabled
 *
 * @brief returns whether PD dump is enabled or not. PD dumps are
 * similar to coredumps in application space. dumps are useful in
 * debugging issues, by loading them on gdb/lldb.
 *
 * @param void
 * @return bool - true/false
 **/
bool fastrpc_config_is_pddump_enabled(void);
/**
 * fastrpc_config_is_perfkernel_enabled
 *
 * @brief returns whether performance counters in kernel should be
 * enabled or not
 *
 * @param void
 * @return bool - true/false
 **/
bool fastrpc_config_is_perfkernel_enabled(void);
/**
 * fastrpc_config_is_perfdsp_enabled
 *
 * @brief returns whether performance counters in DSP should be
 * enabled or not
 *
 * @param void
 * @return bool - true/false
 **/
bool fastrpc_config_is_perfdsp_enabled(void);
/**
 * fastrpc_config_get_runtime_farf_file
 *
 * @brief returns the name of the FARF file, where the runtime
 * FARF levels are specified. These log levels are enabled
 * in the user space/DSP
 *
 * @param void
 * @return string - name of the file
 **/
char *fastrpc_config_get_runtime_farf_file(void);
/**
 * fastrpc_config_get_userspace_runtime_farf_file
 *
 * @brief returns the name of the file where runtime FARF logs are
 * stored.
 *
 * @param void
 * @return string - name of the file
 **/
char *fastrpc_config_get_userspace_runtime_farf_file(void);
/**
 * fastrpc_config_is_log_iregion_enabled
 *
 * @brief returns whether the debug logs in memory management module
 * (in DSP) should be enabled or not
 *
 * @param void
 * @return bool - true/false
 **/
bool fastrpc_config_is_log_iregion_enabled(void);
/**
 * fastrpc_config_is_debug_logging_enabled
 *
 * @brief returns whether DSP power (HAP_power) specific logs are enabled or
 *not
 *
 *
 * @param void
 * @return bool - true/false
 **/
bool fastrpc_config_is_debug_logging_enabled(void);
/**
 * fastrpc_config_is_sysmon_reserved_bit_enabled
 *
 * @brief returns whether debug logging for sysmon is enabled or not
 *
 * @param void
 * @return bool - true/false
 **/
bool fastrpc_config_is_sysmon_reserved_bit_enabled(void);
/**
 * fastrpc_config_is_qtf_tracing_enabled
 *
 * @brief returns whether qtf tracing is enabled or not
 *
 * @param void
 * @return bool - true/false
 **/
bool fastrpc_config_is_qtf_tracing_enabled(void);
// Function to get heap caller level.
int fastrpc_config_get_caller_level(void);
/**
 * fastrpc_config_is_uaf_enabled
 *
 * @brief returns whether use after free check should be enabled
 * in user heap (on DSP PD) or not
 *
 * @param void
 * @return bool - true/false
 **/
bool fastrpc_config_is_uaf_enabled(void);
/**
 * fastrpc_config_is_logpacket_enabled
 *
 * @brief returns whether QXDM log packet should be enabled
 * QXDM log packets provide additional information on the fastRPC
 * internal data structures at runtime and they are low latency
 * logs.
 *
 * @param void
 * @return bool - true/false
 **/
bool fastrpc_config_is_logpacket_enabled(void);
/**
 * fastrpc_config_get_leak_detect
 *
 * @brief returns whether the leak detecting feature in user heap
 * (on DSP PD) is enabled or not
 *
 * @param void
 * @return integer - 1/0
 **/
int fastrpc_config_get_leak_detect(void);
// Function to return the call stack num
int fastrpc_config_get_caller_stack_num(void);

/**
 * fastrpc_config_init
 *
 * @brief Initialization routine to initialize the datastructures and global
 * variables in this module.
 *
 * @param void
 * @return integer - 0 for success, non-zero for error cases.
 **/
int fastrpc_config_init();

/*
 * fastrpc_config_is_setdmabufname_enabled() - Check if DMA allocated buffer
 * name attribute debug support has been requested.
 * Returns: True or False, status of debug support
 */
bool fastrpc_config_is_setdmabufname_enabled(void);

#endif /*__FASTRPC_CONFIG_H__*/
