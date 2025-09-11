// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "AEEStdErr.h"
#include "AEEstd.h"
#include "HAP_farf_internal.h"
#include "apps_std_internal.h"
#include "fastrpc_common.h"
#include "fastrpc_config.h"
#include "fastrpc_internal.h" //TODO: Bharath
#include "fastrpc_procbuf.h"
#include "fastrpc_process_attributes.h"
#include "rpcmem_internal.h"
#include "verify.h"

/* size of buffer used to share the inital config params to dsp */
#define PROC_SHAREDBUF_SIZE (4 * 1024)
#define WORD_SIZE 4

extern struct handle_list *hlist;

int
proc_sharedbuf_init(int dev, int domain)
{
	int proc_sharedbuf_size = PROC_SHAREDBUF_SIZE,
	    sharedbuf_kernel_support = 1;
	int nErr = AEE_SUCCESS, ioErr = 0;
	void *proc_sharedbuf = NULL;
	struct fastrpc_proc_sharedbuf_info sharedbuf_info;

	errno = 0;
	VERIFYC(NULL
	            != (proc_sharedbuf
	                = rpcmem_alloc_internal(0, RPCMEM_HEAP_DEFAULT,
	                                        (size_t)proc_sharedbuf_size)),
	        AEE_ENORPCMEMORY);
	hlist[domain].proc_sharedbuf = proc_sharedbuf;
	VERIFYC(-1
	            != (sharedbuf_info.buf_fd
	                = rpcmem_to_fd_internal(proc_sharedbuf)),
	        AEE_ERPC);
	sharedbuf_info.buf_size = proc_sharedbuf_size;

	ioErr = ioctl_sharedbuf(dev, &sharedbuf_info);
	if(ioErr) {
		if(errno == ENOTTY) {
			sharedbuf_kernel_support = 0;
			FARF(ERROR,
			     "Error 0x%x: %s: sharedbuff capability not "
			     "supported by kernel (errno %d, %s).",
			     nErr, __func__, errno, strerror(errno));
		} else {
			nErr = convert_kernel_to_user_error(nErr, errno);
		}
	}
bail:
	if(proc_sharedbuf && (nErr || !sharedbuf_kernel_support)) {
		rpcmem_free_internal(proc_sharedbuf);
		proc_sharedbuf = NULL;
		hlist[domain].proc_sharedbuf = NULL;
	}
	if(nErr != AEE_SUCCESS) {
		FARF(ERROR,
		     "Error 0x%x: %s failed for domain %d, errno %s, ioErr "
		     "%d\n",
		     nErr, __func__, domain, strerror(errno), ioErr);
	}
	return nErr;
}

/*
 * Function to pack the shared buffer with list of shared objects in custom
 * DSP_LIBRARY_PATH.
 * @lib_names   : List of the shared objects present in the custom
 * DSP_SEARCH_PATH set by user.
 * @buffer_size : Number of characters to be packed.
 * Updated lib_names at the end (example) : lib1.so;lib2.so;lib3.so;
 */

static int
get_non_preload_lib_names(char **lib_names, size_t *buffer_size, int domain)
{
	int nErr = AEE_SUCCESS, env_list_len = 0, concat_len = 0;
	char *data_paths = NULL;
	char *saveptr = NULL;
	size_t dsp_search_path_len = strlen(DSP_LIBRARY_PATH) + 1;

	VERIFYC(*lib_names != NULL, AEE_ENOMEMORY);
	VERIFYC(NULL
	            != (data_paths
	                = calloc(1, sizeof(char) * dsp_search_path_len)),
	        AEE_ENOMEMORY);
	VERIFYC(AEE_SUCCESS
	            == apps_std_getenv(DSP_LIBRARY_PATH, data_paths,
	                               dsp_search_path_len, &env_list_len),
	        AEE_EGETENV);

	char *path = strtok_r(data_paths, ";", &saveptr);
	while(path != NULL) {
		struct dirent *entry;
		DIR *dir = opendir(path);
		VERIFYC(NULL != dir, AEE_EBADPARM);

		while((entry = readdir(dir)) != NULL) {
			if(entry->d_type == DT_REG) {
				char *file = entry->d_name;
				if(strstr(file, FILE_EXT) != NULL) {
					if(concat_len + strlen(file)
					   > MAX_NON_PRELOAD_LIBS_LEN) {
						FARF(ALWAYS,
						     "ERROR: Failed to pack "
						     "library names in custom "
						     "DSP_LIBRARY_PATH as "
						     "required buffer size "
						     "exceeds Max limit (%d).",
						     MAX_NON_PRELOAD_LIBS_LEN);
						nErr = AEE_EBUFFERTOOSMALL;
						closedir(dir);
						goto bail;
					}
					strlcat(*lib_names, file,
					        MAX_NON_PRELOAD_LIBS_LEN);
					concat_len = strlcat(
					    *lib_names, ";",
					    MAX_NON_PRELOAD_LIBS_LEN);
				}
			}
		}
		if(dir != NULL) {
			closedir(dir);
		}
		path = strtok_r(NULL, ";", &saveptr);
	}
	*buffer_size = strlen(*lib_names) + 1;

bail:
	if(data_paths) {
		free(data_paths);
		data_paths = NULL;
	}
	if(nErr && (nErr != AEE_EGETENV)) {
		FARF(ERROR, "Error 0x%x: %s Failed for domain %d (%s)\n", nErr,
		     __func__, domain, strerror(errno));
	}
	return nErr;
}

/* Internal function to pack the process config parameters in shared buffer
 *
 *                                        shared buffer 4K
 *                                    +-------------------------+
 * proc_sharedbuf addr--------------->| Total no of id's packed |
 *                                    +-------------------------+
 * ID1 addr=proc_sharedbuf addr+1---->| ID1  | data payload size|
 *                                    +-------------------------+
 * ID1 data addr=ID1 addr+1---------->|     ID1 data            |
 *                                    +-------------------------+
 * ID2 addr=ID1 data addr+data size-->| ID2  | data payload size|
 *                                    +-------------------------+
 * ID2 data addr=ID2 addr+1---------->|     ID2 data            |
 *                                    +-------------------------+
 * ......                             |.........................|
 *                                    +-------------------------+
 * ......                             |......|..................|
 *                                    +-------------------------+
 * proc_sharedbuf end addr----------->|.........................|
 *                                    +-------------------------+
 *                                     8bits |    24bits
 *
 * @ domain: domain to retrieve the process shared buffer address
 * @ param_id: unique process parameter id should match with dsp id to unpack
 * @ param_addr: address of the process parameters to write into shared buffer
 * @ param_size: size of the process parameters to pack
 * returns 0 on success
 */

static int
pack_proc_shared_buf_params(int domain, uint32_t param_id, void *param_addr,
                            uint32_t param_size)
{
	uint32_t *buf_start_addr = (uint32_t *)hlist[domain].proc_sharedbuf;
	uint32_t align_param_size = param_size;
	/* Params pack address */
	uint32_t *buf_write_addr
	    = (uint32_t *)hlist[domain].proc_sharedbuf_cur_addr,
	    *buf_last_addr = buf_start_addr + PROC_SHAREDBUF_SIZE;

	if(param_addr == NULL || param_size <= 0 || param_id < 0
	   || param_id >= PROC_ATTR_BUF_MAX_ID) {
		FARF(ERROR,
		     "Error: %s: invalid param %u or size %u or addr 0x%x",
		     __func__, param_id, param_size, param_addr);
		return AEE_EBADPARM;
	}
	if(buf_write_addr == NULL) {
		/*
		 * Total no of param ids (4 bytes) are packed at shared buffer
		 * initial address, so add 4 bytes to start pack process params
		 */
		buf_write_addr
		    = (uint32_t *)((char *)buf_start_addr + WORD_SIZE);
	}
	/* Align the params size in multiple of 4 bytes (word) for easy
	 * unpacking at DSP */
	align_param_size = ALIGN_B(param_size, WORD_SIZE);

	if(buf_last_addr
	   < (uint32_t *)((char *)buf_write_addr + align_param_size
	                  + sizeof(param_id))) {
		FARF(ERROR,
		     "Error: %s: proc shared buffer exhausted to pack "
		     "param_id:%u params",
		     __func__, param_id);
		return AEE_ERPC;
	}
	/* Write param_id */
	*buf_write_addr = param_id;

	/* Shift param_id to first 8 bits */
	*buf_write_addr = (*buf_write_addr) << PROC_ATTR_BUF_ID_POS;

	/* Write param size in last 24 bits */
	*buf_write_addr = (*buf_write_addr)
	                  + (PROC_ATTR_BUF_ID_SIZE_MASK & align_param_size);
	buf_write_addr++;

	memcpy(buf_write_addr, param_addr,
	       STD_MIN(buf_last_addr - buf_write_addr, param_size));
	buf_write_addr
	    = (uint32_t *)((char *)buf_write_addr + align_param_size);
	hlist[domain].proc_sharedbuf_cur_addr = buf_write_addr;

	/* Increase the number of ids in start address */
	(*buf_start_addr)++;
	return AEE_SUCCESS;
}

/* Internal function to send the process config parameters for packing
 *
 * @ dev: device id
 * @ domain: domain to retrieve the process shared buffer address
 * returns none
 */
void
fastrpc_process_pack_params(int dev, int domain)
{
	int nErr = AEE_SUCCESS,
	    sess_id = GET_SESSION_ID_FROM_DOMAIN_ID(domain);
	struct err_codes *err_codes_to_send = NULL;
	size_t buffer_size = 0;
	char *lib_names = NULL;
	pid_t pid = getpid();

	if(AEE_SUCCESS != proc_sharedbuf_init(dev, domain)) {
		return;
	}
	if(!hlist[domain].proc_sharedbuf) {
		return;
	}
	nErr = pack_proc_shared_buf_params(domain, HLOS_PID_ID, &pid,
	                                   sizeof(pid));
	if(nErr) {
		FARF(ERROR,
		     "Error 0x%x: %s: Failed to pack process id in shared "
		     "buffer",
		     nErr, __func__);
	}
	nErr = pack_proc_shared_buf_params(domain, THREAD_PARAM_ID,
	                                   &hlist[domain].th_params,
	                                   sizeof(hlist[domain].th_params));
	if(nErr) {
		FARF(ERROR,
		     "Error 0x%x: %s: Failed to pack thread parameters in "
		     "shared buffer",
		     nErr, __func__);
	}
	nErr = pack_proc_shared_buf_params(domain, PROC_ATTR_ID,
	                                   &hlist[domain].procattrs,
	                                   sizeof(hlist[domain].procattrs));
	if(nErr) {
		FARF(ERROR,
		     "Error 0x%x: %s: Failed to pack process config "
		     "parameters in shared buffer",
		     nErr, __func__);
	}
	err_codes_to_send = fastrpc_config_get_errcodes();
	if(err_codes_to_send) {
		nErr = pack_proc_shared_buf_params(domain, PANIC_ERR_CODES_ID,
		                                   err_codes_to_send,
		                                   sizeof(*err_codes_to_send));
		if(nErr) {
			FARF(ERROR,
			     "Error 0x%x: %s: Failed to pack panic error "
			     "codes in shared buffer",
			     nErr, __func__);
		}
	}
	nErr = pack_proc_shared_buf_params(domain, HLOS_PROC_EFFEC_DOM_ID,
	                                   &domain, sizeof(domain));
	if(nErr) {
		FARF(ERROR,
		     "Error 0x%x: %s: Failed to pack effective domain id %d "
		     "in shared buffer",
		     nErr, __func__, domain);
	}
	lib_names = (char *)malloc(sizeof(char) * MAX_NON_PRELOAD_LIBS_LEN);
	if(lib_names) {
		if(AEE_SUCCESS
		   == get_non_preload_lib_names(&lib_names, &buffer_size,
		                                domain)) {
			nErr = pack_proc_shared_buf_params(
			    domain, CUSTOM_DSP_SEARCH_PATH_LIBS_ID, lib_names,
			    buffer_size);
			if(nErr) {
				FARF(ERROR,
				     "Error 0x%x: %s: Failed to pack the "
				     "directory list in shared buffer",
				     nErr, __func__);
			}
		}
	}
	nErr = pack_proc_shared_buf_params(domain, HLOS_PROC_SESS_ID, &sess_id,
	                                   sizeof(sess_id));
	if(nErr) {
		FARF(ERROR,
		     "Error 0x%x: %s: Failed to pack session id %d in shared "
		     "buffer",
		     nErr, __func__, sess_id);
	}
	if(lib_names) {
		free(lib_names);
		lib_names = NULL;
	}
}
