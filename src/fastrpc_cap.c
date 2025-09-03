// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>

#define FARF_ERROR 1

#include "remote.h"
#include "AEEStdErr.h"
#include "verify.h"
#include "HAP_farf.h"
#include "fastrpc_cap.h"
#include "fastrpc_common.h"
#include "fastrpc_internal.h"


#define BUF_SIZE 50

const char * RPROC_SUBSYSTEM_NAME[] = {"adsp", "mss", "spss", "cdsp", "cdsp1", "gdsp0", "gdsp1", "reserved"};

static inline uint32_t fastrpc_check_if_dsp_present_pil(uint32_t domain) {
	uint32_t domain_supported = 0;
	struct stat sb;
	// mark rest of the list as reserved to avoid out of bound access
	const char *SUBSYSTEM_DEV_NAME[] = {"/dev/subsys_adsp", "", "/dev/subsys_slpi", "/dev/subsys_cdsp", "/dev/subsys_cdsp1", "/dev/subsys_gdsp0", "/dev/subsys_gdsp1", "reserved", "reserved", "reserved", "reserved", "reserved", "reserved", "reserved", "reserved", "reserved"};

	// If device file is present, then target supports that DSP
	if (!stat(SUBSYSTEM_DEV_NAME[domain], &sb)) {
		domain_supported = 1;
	}
	return domain_supported;
}

/*
 * Function to check whether particular remote subsystem is present or not.
 * Return 0 if subsystem is not available otherwise 1.
*/
static inline uint32_t fastrpc_check_if_dsp_present_rproc(uint32_t domain) {
	char *dir_base_path = "/sys/class/remoteproc/remoteproc";
	const char *search_string = NULL;
	uint32_t domain_supported = 0;
	int dir_index = 0, nErr = AEE_SUCCESS;
	char *buffer = NULL;

	if (domain < ADSP_DOMAIN_ID || domain > GDSP1_DOMAIN_ID) {
		FARF(ERROR, "%s Invalid domain 0x%x ", __func__, domain);
		return 0;
	}

	VERIFYC(NULL != (buffer = malloc(BUF_SIZE)), AEE_ENOMEMORY);
	search_string =  RPROC_SUBSYSTEM_NAME[domain];

	while (1) {
		memset(buffer, 0, BUF_SIZE);
		snprintf(buffer, BUF_SIZE, "%s%d", dir_base_path, dir_index);
		strlcat(buffer, "/name", BUF_SIZE);
		int fd = open(buffer, O_RDONLY);
		if (fd == -1) {
			break;
		}
		FILE *file = fdopen(fd, "r");
		if (file != NULL) {
			memset(buffer, 0, BUF_SIZE);
			if (fgets(buffer, BUF_SIZE, file) != NULL) {
				buffer[BUF_SIZE - 1] = '\0';
				if (strstr(buffer, search_string) != NULL) {
					domain_supported = 1;
					fclose(file);
					break;
				}
			}
			fclose(file);
		}
		close(fd);
		dir_index++;
	}
bail :
	if (buffer){
		free(buffer);
	}
	if (nErr) {
		 FARF(ERROR, "Error 0x%x: %s failed for domain %d\n", nErr, __func__, domain);
	}
	return domain_supported;
}

int fastrpc_get_cap(uint32_t domain, uint32_t attributeID, uint32_t *capability) {
   int nErr = AEE_SUCCESS, dev = -1, dom = GET_DOMAIN_FROM_EFFEC_DOMAIN_ID(domain);

   VERIFYC(IS_VALID_EFFECTIVE_DOMAIN_ID(domain), AEE_EBADPARM);
   VERIFYC(capability != NULL, AEE_EBADPARM);
   VERIFYC(attributeID < FASTRPC_MAX_ATTRIBUTES, AEE_EBADPARM);   //Check if the attribute ID is less than max attributes accepted by userspace

   *capability = 0;

   if (attributeID == DOMAIN_SUPPORT) {
      *capability = fastrpc_check_if_dsp_present_pil(dom);
      if (*capability == 0) {
        *capability = fastrpc_check_if_dsp_present_rproc(dom);
      }
      if (*capability == 0) {
        FARF(ALWAYS, "Warning! %s domain %d is not present\n", __func__, dom);
      }
      goto bail;
   }
   if(attributeID == ASYNC_FASTRPC_SUPPORT) {
      if(!is_async_fastrpc_supported() ) {
        *capability = 0;
        goto bail;
      }
   }

   VERIFY(AEE_SUCCESS == (nErr = fastrpc_session_open(dom, &dev)));
   errno = 0;
   nErr = ioctl_getdspinfo(dev, dom, attributeID, capability);
   if(nErr) {
      goto bail;
   }
   if (attributeID == STATUS_NOTIFICATION_SUPPORT) {
      *capability = (*capability == STATUS_NOTIF_V2) ? 1 : 0;
   }

bail:
   if(dev != -1)
      fastrpc_session_close(dom, dev);
   if (nErr) {
      FARF(ERROR, "Warning 0x%x: %s failed to get attribute %u for domain %u (errno %s)", nErr, __func__, attributeID, domain, strerror(errno));
   }
   return nErr;
}

uint32_t get_dsp_dma_reverse_rpc_map_capability(int domain) {
	int nErr = 0;
    uint32_t capability = 0;

	nErr= fastrpc_get_cap(domain, MAP_DMA_HANDLE_REVERSERPC, &capability);
	if (nErr == 0) {
		return capability;
	}
    return 0;
}

static int check_status_notif_version2_capability(int domain)
{
	int nErr = 0;
	struct remote_dsp_capability cap = {0};
	cap.domain = GET_DOMAIN_FROM_EFFEC_DOMAIN_ID(domain);
	cap.attribute_ID = STATUS_NOTIFICATION_SUPPORT;

	nErr= fastrpc_get_cap(cap.domain, cap.attribute_ID, &cap.capability);
	if (nErr == 0) {
		return cap.capability;
	} else {
		FARF(RUNTIME_RPC_HIGH, "%s: Capability not found. nErr: 0x%x", __func__, nErr);
	}
    return 0;
}

int is_status_notif_version2_supported(int domain)
{
    static int status_notif_version2_capability = -1;

	if(status_notif_version2_capability == -1) {
		status_notif_version2_capability = check_status_notif_version2_capability(domain);
	}
	return status_notif_version2_capability;
}

static int check_userspace_allocation_capability(void)
{
	int nErr = 0;
	struct remote_dsp_capability cap = {0};
    
	cap.domain = DEFAULT_DOMAIN_ID;
	cap.attribute_ID = USERSPACE_ALLOCATION_SUPPORT;

	nErr= fastrpc_get_cap(cap.domain, cap.attribute_ID, &cap.capability);
	if (nErr == 0) {
		return cap.capability;
	} else {
		FARF(RUNTIME_RPC_HIGH, "%s: Capability not found. nErr: 0x%x", __func__, nErr);
	}
    return 0;
}

int is_userspace_allocation_supported(void)
{
    static int userspace_allocation_capability = -1;

	if(userspace_allocation_capability == -1) {
		userspace_allocation_capability = check_userspace_allocation_capability();
	}
	return userspace_allocation_capability;
}

int is_proc_sharedbuf_supported_dsp(int domain)
{
	int nErr = AEE_SUCCESS;
	static int proc_sharedbuf_capability = -1;
	struct remote_dsp_capability cap = {domain, PROC_SHARED_BUFFER_SUPPORT, 0};

	if (proc_sharedbuf_capability == -1) {
		nErr = fastrpc_get_cap(cap.domain, cap.attribute_ID, &cap.capability);
		if (nErr == AEE_SUCCESS) {
			proc_sharedbuf_capability = cap.capability;
		} else {
			FARF(RUNTIME_RPC_HIGH, "Error 0x%x: %s: capability not found", nErr, __func__);
			proc_sharedbuf_capability = 0;
		}
	}
	return proc_sharedbuf_capability;
}

int check_error_code_change_present() {
	int nErr = 0;
	struct remote_dsp_capability cap = {0};
    static int driver_error_code_capability = -1;
	cap.domain = DEFAULT_DOMAIN_ID;
	cap.attribute_ID = DRIVER_ERROR_CODE_CHANGE;

    if(driver_error_code_capability == -1) {
        nErr= fastrpc_get_cap(cap.domain, cap.attribute_ID, &cap.capability);
        if (nErr == 0) {
            driver_error_code_capability = cap.capability;
        }
    }
    return driver_error_code_capability;
}
