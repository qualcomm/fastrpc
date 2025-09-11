// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef FASTRPC_PROCESS_ATTRIBUTES_H
#define FASTRPC_PROCESS_ATTRIBUTES_H

/*
 * Process shared buffer id header bit-map:
 * bits 0-7  : process param unique id
 * bits 8-31 : size of the process param
 */
#define PROC_ATTR_BUF_ID_POS 24

/* Mask to get param_id payload size */
#define PROC_ATTR_BUF_ID_SIZE_MASK ((1 << PROC_ATTR_BUF_ID_POS) - 1)

/*
 * enum represents the unique id corresponding to hlos id.
 * Do not modify the existing id. Add new ids for sending any new parameters
 * from hlos and ensure that it is matching with dsp param id.
 */

enum proc_param_id {
	/* HLOS process id */
	HLOS_PID_ID = 0,

	/* Thread parameters */
	THREAD_PARAM_ID,

	/* Process attributes */
	PROC_ATTR_ID,

	/* Panic error codes */
	PANIC_ERR_CODES_ID,

	/* HLOS process effective domain id */
	HLOS_PROC_EFFEC_DOM_ID,

	/*Get list of the .so's present in the custom DSP_LIBRARY_PATH set by
	   user*/
	CUSTOM_DSP_SEARCH_PATH_LIBS_ID,

	/* HLOS process session id */
	HLOS_PROC_SESS_ID,

	/* Maximum supported ids to unpack from proc attr shared buf */
	PROC_ATTR_BUF_MAX_ID = HLOS_PROC_SESS_ID + 1
};

#endif // FASTRPC_PROCESS_ATTRIBUTES_H
