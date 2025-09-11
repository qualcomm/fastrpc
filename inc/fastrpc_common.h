// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef FASTRPC_COMMON_H
#define FASTRPC_COMMON_H

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

/* Header file used by all other modules
 * will contain the most critical defines
 * and APIs. ****** SHOULD NOT ****** be used for
 * hlist/any domain specific fastrpc_apps_user.c
 * symbols here.
 */

/* Number of subsystem supported by fastRPC*/
#ifndef NUM_DOMAINS
#define NUM_DOMAINS 8
#endif /*NUM_DOMAINS*/

/* Number of sessions allowed per process */
#ifndef NUM_SESSIONS
#define NUM_SESSIONS 4
#define DOMAIN_ID_MASK 7
#endif /*NUM_SESSIONS*/

/* Default domain id, in case of non domains*/
#ifndef DEFAULT_DOMAIN_ID
#define DEFAULT_DOMAIN_ID 3
#endif /*DEFAULT_DOMAIN_ID*/

/* Macros for invalids/defaults*/
#define INVALID_DOMAIN_ID -1
#define INVALID_HANDLE (remote_handle64)(-1)
#define INVALID_KEY (pthread_key_t)(-1)
#define INVALID_DEVICE (-1)

// Number of domains extended to include sessions
// Domain ID extended (0 - 3): Domain id (0 - 3), session id 0
// Domain ID extended (4 - 7): Domain id (0 - 3), session id 1
#define NUM_DOMAINS_EXTEND (NUM_DOMAINS * NUM_SESSIONS)

// Domain name types
#define DOMAIN_NAME_IN_URI 1      // Domain name with standard module URI
#define DOMAIN_NAME_STAND_ALONE 2 // Stand-alone domain name

#define PROFILE(time_taken, ff)                                               \
	{                                                                     \
		struct timeval tv1, tv2;                                      \
		if(is_systrace_enabled()) {                                   \
			gettimeofday(&tv1, 0);                                \
			ff;                                                   \
			gettimeofday(&tv2, 0);                                \
			*time_taken = (tv2.tv_sec - tv1.tv_sec) * 1000000ULL  \
			              + (tv2.tv_usec - tv1.tv_usec);          \
		} else {                                                      \
			ff;                                                   \
		}                                                             \
	}

#define PROFILE_ALWAYS(time_taken, ff)                                        \
	{                                                                     \
		struct timeval tv1, tv2;                                      \
		uint64_t temp1, temp2, temp3;                                 \
		gettimeofday(&tv1, 0);                                        \
		ff;                                                           \
		gettimeofday(&tv2, 0);                                        \
		__builtin_sub_overflow(tv2.tv_sec, tv1.tv_sec, &temp1);       \
		__builtin_sub_overflow(tv2.tv_usec, tv1.tv_usec, &temp2);     \
		__builtin_mul_overflow(temp1, 1000000ULL, &temp3);            \
		__builtin_add_overflow(temp2, temp3, time_taken);             \
	}

/* Macros to check if the remote call is from static module */
#define FASTRPC_STATIC_HANDLE_LISTENER (3)
#define FASTRPC_MAX_STATIC_HANDLE (10)
#define REVERSE_RPC_SCALAR 0x04020200 // corresponding to next2() method
#define IS_STATIC_HANDLE(handle)                                              \
	((handle) >= 0 && (handle) <= FASTRPC_MAX_STATIC_HANDLE)
#define IS_REVERSE_RPC_CALL(handle, sc)                                       \
	((handle == FASTRPC_STATIC_HANDLE_LISTENER)                           \
	 && (sc == REVERSE_RPC_SCALAR))

// number of times to retry write operation
#define RETRY_WRITE (3)

// Macro to increment a reference count for the active domain
#define FASTRPC_GET_REF(domain)                                               \
	VERIFY(AEE_SUCCESS == (nErr = fastrpc_session_get(domain)));          \
	ref = 1;
// Macro to decrement a reference count after the remote call is complete
#define FASTRPC_PUT_REF(domain)                                               \
	if(ref == 1) {                                                        \
		fastrpc_session_put(domain);                                  \
		ref = 0;                                                      \
	}

/**
 * @brief Process types on remote subsystem
 * Always add new PD types at the end, before MAX_PD_TYPE,
 * for maintaining back ward compatibility
 **/
#define DEFAULT_UNUSED 0   /* pd type not configured for context banks */
#define ROOT_PD 1          /* Root PD */
#define AUDIO_STATICPD 2   /* ADSP Audio Static PD */
#define SENSORS_STATICPD 3 /* ADSP Sensors Static PD */
#define SECURE_STATICPD 4  /* CDSP Secure Static PD */
#define OIS_STATICPD 5     /* ADSP OIS Static PD */
#define CPZ_USERPD 6       /* CDSP CPZ USER PD */
#define USERPD 7           /* DSP User Dynamic PD */
#define GUEST_OS_SHARED 8  /* Legacy Guest OS Shared */
#define MAX_PD_TYPE 9      /* Max PD type */

/*
 * Enum defined for fastrpc User Properties
 * @fastrpc_properties: Object of enum
 * Enum values corresponds to array indices
 *
 */
typedef enum {
	FASTRPC_PROCESS_ATTRS = 0,   // to spawn a User process as Critical
	FASTRPC_DEBUG_TRACE = 1,     // to enable logs on remote invocation
	FASTRPC_DEBUG_TESTSIG = 2,   // to get process test signature
	FASTRPC_PERF_KERNEL = 3,     // to enable rpc perf on fastrpc kernel
	FASTRPC_PERF_ADSP = 4,       // to enable rpc perf on DSP
	FASTRPC_PERF_FREQ = 5,       // to set performance frequency
	FASTRPC_ENABLE_SYSTRACE = 6, // to enable tracing using Systrace
	FASTRPC_DEBUG_PDDUMP = 7, // to enable pd dump debug data collection on
	                          // rooted device for signed/unsigned pd
	FASTRPC_PROCESS_ATTRS_PERSISTENT = 8, // to set proc attr as persistent
	FASTRPC_BUILD_TYPE = 9 // Fetch build type of firmware image. It gives
	                       // the details if its debug or prod build
} fastrpc_properties;

/**
 * @enum fastrpc_internal_attributes
 * @brief Internal attributes in addition to remote_dsp_attributes.
 * To be used for internal development purposes, cients are
 * not allowed to request these.
 */
enum fastrpc_internal_attributes {
	PERF_V2_DSP_SUPPORT = 128, /**<  Perf logging V2 DSP support */
	MAP_DMA_HANDLE_REVERSERPC
	= 129, /**<  Map DMA handle in reverse RPC call */
	DSPSIGNAL_DSP_SUPPORT
	= 130, /**<  New "dspsignal" signaling supported on DSP */
	PROC_SHARED_BUFFER_SUPPORT = 131, /**<  sharedbuf capability support */
	PERF_V2_DRIVER_SUPPORT = 256, /**<  Perf logging V2 kernel support */
	DRIVER_ERROR_CODE_CHANGE
	= 257, /**<  Fastrpc Driver error code change */
	USERSPACE_ALLOCATION_SUPPORT
	= 258, /**<  Userspace memory allocation support */
	DSPSIGNAL_DRIVER_SUPPORT
	= 259, /**<  dspsignal signaling supported in CPU driver */
	FASTRPC_MAX_ATTRIBUTES
	= DSPSIGNAL_DRIVER_SUPPORT
	  + 1, /**<  Max DSP/Kernel attributes supported */
};

/* Utility function to get pd type of a DSP domain
 * @domain: DSP domain ID
 * @return -1 if device not opened and pd type if opened
 */
int fastrpc_get_pd_type(int domain);
/**
 * @brief API to initialize the global data strcutures in fastRPC library
 */
int fastrpc_init_once(void);
/**
 * @brief API to get the recently used domain from a thread context
 * Uses pthread_key to associate a domain to the recently used domain
 */
int get_current_domain(void);
/**
 * @brief returns the file descriptor of the fastrpc device node
 */
int fastrpc_session_open(int domain, int *dev);
/**
 * @brief closes the remote session/file descriptor of the fastrpc device node
 */
void fastrpc_session_close(int domain, int dev);
/**
 * @brief increments the reference count of the domain
 *  used to identify whether there are any active remote calls for a specific
 * domain
 */
int fastrpc_session_get(int domain);
/**
 * @brief decrements the reference count of the domain.
 *  used to identify whether there are any active remote calls for a specific
 * domain
 */
int fastrpc_session_put(int domain);
/**
 * @brief returns the device node opened for specific domain
 */
int fastrpc_session_dev(int domain, int *dev);

/*
 * API to get User property values where value type corresponds to integer
 * @UserPropertyKey: [in] Value(enum type) that corresponds to array index of
 * User property string
 * @defValue: [in] default value returned when user property is not set
 * On user property set, returns user value. Else, returns default value
 *
 */
int fastrpc_get_property_int(fastrpc_properties UserPropertyKey, int defValue);

/*
 * API to get User property values where value type corresponds to string
 * @UserPropertyKey: [in] Value(enum type) that corresponds to array index of
 * User property string
 * @value: [out] Pointer to the User set string
 * @defValue: [in] default value returned when user property is not set
 * returns the length of the value which will never be
 * greater than PROPERTY_VALUE_MAX - 1 and will always be zero terminated.
 *
 */
int fastrpc_get_property_string(fastrpc_properties UserPropertyKey,
                                char *value, char *defValue);

/*
 * This function is used to check the current process is exiting or not.
 *
 * @retval: TRUE if process is exiting.
 *          FALSE if process is not exiting.
 */
bool is_process_exiting(int domain);

/* Opens device node based on the domain
 * This function takes care of the backward compatibility to open
 * approriate device for following configurations of the device nodes
 * 1. 4 different device nodes
 * 2. 1 device node (adsprpc-smd)
 * 3. 2 device nodes (adsprpc-smd, adsprpc-smd-secure)
 * Algorithm
 * For ADSP, SDSP, MDSP domains:
 *   Open secure device node fist
 *     if no secure device, open actual device node
 *     if still no device, open default node
 *     if failed to open the secure node due to permission,
 *     open default node
 * For CDSP domain:
 *   Open secure device node fist
 *    If the node does not exist or if no permission, open actual device node
 *    If still no device, open default node
 *    If no permission to access the default node, access thorugh HAL.
 */
int open_device_node(int domain_id);

#endif // FASTRPC_COMMON_H
