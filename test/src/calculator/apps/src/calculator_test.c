// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <inttypes.h>
#include <unistd.h>

#include "AEEStdErr.h"
#include "calculator.h"
#include "calculator_test.h"
#include "fastrpc_common.h"
#include "rpcmem.h"
#include "remote.h"

static domain_t supported_domains[] = {{ADSP_DOMAIN_ID, ADSP_DOMAIN},
                                       {MDSP_DOMAIN_ID, MDSP_DOMAIN},
                                       {SDSP_DOMAIN_ID, SDSP_DOMAIN},
                                       {CDSP_DOMAIN_ID, CDSP_DOMAIN},
                                       {CDSP1_DOMAIN_ID,CDSP1_DOMAIN},
                                       {GDSP0_DOMAIN_ID,GDSP0_DOMAIN},
                                       {GDSP1_DOMAIN_ID,GDSP1_DOMAIN}};

domain_t *get_domain(int domain_id) {
    int i = 0;
    int size = sizeof(supported_domains) / sizeof(domain_t);

    for (i = 0; i < size; i++) {
        if (supported_domains[i].id == domain_id)
            return &supported_domains[i];
    }

    return NULL;
}

bool is_unsignedpd_supported(int domain_id)
{
    int nErr = AEE_SUCCESS;
    if (&remote_handle_control)
    {
        struct remote_dsp_capability dsp_capability_domain = {domain_id, UNSIGNED_PD_SUPPORT, 0};
        nErr = remote_handle_control(DSPRPC_GET_DSP_INFO, &dsp_capability_domain, sizeof(struct remote_dsp_capability));
        if ((nErr & 0xFF) == (AEE_EUNSUPPORTEDAPI & 0xFF))
        {
            printf("\nFastRPC Capability API is not supported on this device. Falling back to signed pd.\n");
            return false;
        }
        if (nErr)
        {
            printf("\nERROR 0x%x: FastRPC Capability API failed. Falling back to signed pd.", nErr);
            return false;
        }
        if (dsp_capability_domain.capability == 1)
        {
            return true;
        }
    }
    else
    {
        nErr = AEE_EUNSUPPORTEDAPI;
        printf("remote_dsp_capability interface is not supported on this device. Falling back to signed pd.\n");
        return false;
    }
    return false;
}

static int* allocate_and_populate_buffer(size_t size, int num_elements) {
    int* buffer;
    int heapid = RPCMEM_HEAP_ID_SYSTEM;
#if defined(SLPI) || defined(MDSP)
    heapid = RPCMEM_HEAP_ID_CONTIG;
#endif
    if (0 == (buffer = (int *)rpcmem_alloc(heapid, RPCMEM_DEFAULT_FLAGS, size))) {
        printf("ERROR: memory alloc failed for size %zu\n", size);
    } else {
        printf("Allocated %zu bytes from DMA heap at address %p\n", size, (void*)buffer);
        printf("Populating buffer with values from 0 to %u...\n", (unsigned int)(num_elements - 1));
        for (unsigned int ii = 0; ii < num_elements; ++ii) {
            buffer[ii] = ii;
        }
    }
    return buffer;
}

static void free_buffer(int* buffer) {
    if (buffer) {
        rpcmem_free(buffer);
    }
}

/* Sentinel context value passed when registering for status notifications */
#define STATUS_CONTEXT ((void *)0x12345678)

static int pd_status_notifier_callback(void *context, int domain, int session,
                                       remote_rpc_status_flags_t status)
{
    int nErr = AEE_SUCCESS;
    switch (status) {
    case FASTRPC_USER_PD_UP:
        printf("PD is up\n");
        break;
    case FASTRPC_USER_PD_EXIT:
        printf("PD closed\n");
        break;
    case FASTRPC_USER_PD_FORCE_KILL:
        printf("PD force kill\n");
        break;
    case FASTRPC_USER_PD_EXCEPTION:
        printf("PD exception\n");
        break;
    case FASTRPC_DSP_SSR:
        printf("DSP SSR\n");
        break;
    default:
        nErr = AEE_EBADITEM;
        break;
    }
    return nErr;
}

static bool is_status_notification_supported(int domain_id)
{
    int nErr = AEE_SUCCESS;
    struct remote_dsp_capability cap = {domain_id, STATUS_NOTIFICATION_SUPPORT, 0};

    if (&remote_handle_control) {
        nErr = remote_handle_control(DSPRPC_GET_DSP_INFO, &cap, sizeof(cap));
        if (nErr) {
            printf("WARNING 0x%x: remote_handle_control failed for STATUS_NOTIFICATION_SUPPORT\n",
                   nErr);
            return false;
        }
        return (cap.capability == 1);
    }
    return false;
}

static int request_status_notifications_enable(int domain_id, void *context,
                                               int (*notif_callback_fn)(void *context,
                                                                        int domain,
                                                                        int session,
                                                                        remote_rpc_status_flags_t status))
{
    int nErr = AEE_SUCCESS;
    struct remote_rpc_notif_register notif;
    bool status_notification_support;

    notif.context     = context;
    notif.domain      = domain_id;
    notif.notifier_fn = notif_callback_fn;

    status_notification_support = is_status_notification_supported(domain_id);
    if (status_notification_support) {
        nErr = remote_session_control(FASTRPC_REGISTER_STATUS_NOTIFICATIONS,
                                      (void *)&notif, sizeof(notif));
        if (nErr != AEE_SUCCESS) {
            printf("ERROR 0x%x: remote_session_control failed to enable status notifications\n",
                   nErr);
        }
    } else {
        nErr = AEE_EUNSUPPORTEDAPI;
    }
    return nErr;
}

int calculator_sum_test(const char *calculator_URI_domain, const int *test, int num) {
    int nErr = AEE_SUCCESS;
    int64_t result = 0;
    remote_handle64 handleSum = -1;
    int retry = 10;

    printf("\n--- Test Case: Calculator Sum Test ---\n");

    do {
        if (AEE_SUCCESS == (nErr = calculator_open(calculator_URI_domain, &handleSum))) {
            printf("\nCall calculator_sum on the DSP\n");
            nErr = calculator_sum(handleSum, test, num, &result);
        }

        if (!nErr) {
            printf("Sum = %" PRId64 "\n", result);
            break;
        } else {
            if (nErr == AEE_ECONNRESET && errno == ECONNRESET) {
                retry--;
                sleep(5);
            } else if (nErr == (AEE_ENOSUCH + DSP_AEE_EOFFSET) || (nErr == (AEE_EBADSTATE + DSP_AEE_EOFFSET))) {
                retry -= 2;
            } else {
                break;
            }
        }

        if (handleSum != -1) {
            if (AEE_SUCCESS != (nErr = calculator_close(handleSum))) {
                printf("ERROR 0x%x: Failed to close handle\n", nErr);
            }
            handleSum = -1;
        }
    } while(retry);

    if (nErr) {
        printf("Retry attempt unsuccessful. Timing out....\n");
        printf("ERROR 0x%x: Failed to compute sum\n", nErr);
    }

    if (handleSum != -1) {
        int close_ret = calculator_close(handleSum);
        if (close_ret != AEE_SUCCESS) {
            printf("ERROR 0x%x: Failed to close handleSum in cleanup\n", close_ret);
        }
    }
    return nErr;
}


int calculator_max_test(const char *calculator_URI_domain, const int *test, int num) {
    int nErr = AEE_SUCCESS;
    int resultMax = 0;
    remote_handle64 handleMax = -1;

    printf("\n--- Test Case: Calculator Max Test ---\n");

    if (AEE_SUCCESS == (nErr = calculator_open(calculator_URI_domain, &handleMax))) {
        printf("\nCall calculator_max on the DSP\n");
        if (AEE_SUCCESS == (nErr = calculator_max(handleMax, test, num, &resultMax))) {
            printf("Max value = %d\n", resultMax);
        } else {
            printf("ERROR 0x%x: Failed to find max\n", nErr);
        }
    } else {
        printf("ERROR 0x%x: Failed to open handle for calculator_max test\n", nErr);
    }

    if (handleMax != -1) {
        int close_ret = calculator_close(handleMax);
        if (close_ret != AEE_SUCCESS) {
            printf("ERROR 0x%x: Failed to close handleMax in cleanup\n", close_ret);
        }
    }
    return nErr;
}

int run_test(int domain_id, bool is_unsignedpd_enabled) {
    int nErr = AEE_SUCCESS;
    int test_status;
    int num = 1000;
    int *test = NULL;
    int len = sizeof(*test) * num;
    char *calculator_URI_domain = NULL;
    int calculator_URI_domain_len = 0;

    domain_t *my_domain = get_domain(domain_id);
    if (my_domain == NULL) {
        printf("ERROR: Invalid domain id %d\n", domain_id);
        return AEE_EBADPARM;
    }

    if (!is_unsignedpd_enabled) {
        printf("ERROR: Signed PD is not supported for this test.\n");
        return AEE_EFAILED;
    }
    if (!is_unsignedpd_supported(domain_id)) {
        printf("ERROR: Unsigned PD is not supported on domain %d.\n", domain_id);
        return AEE_EFAILED;
    }

    test = allocate_and_populate_buffer(len, num);
    if (!test) {
        nErr = AEE_ENORPCMEMORY;
        goto bail;
    }

    if (&remote_session_control) {
        struct remote_rpc_control_unsigned_module data;
        data.domain = domain_id;
        data.enable = 1;
        if (AEE_SUCCESS != (nErr = remote_session_control(DSPRPC_CONTROL_UNSIGNED_MODULE,
                                                          (void *)&data, sizeof(data)))) {
            printf("ERROR 0x%x: remote_session_control failed\n", nErr);
            goto bail;
        }
    } else {
        nErr = AEE_EUNSUPPORTED;
        printf("ERROR 0x%x: remote_session_control interface is not supported on this device\n",
               nErr);
        goto bail;
    }

    calculator_URI_domain_len = strlen(calculator_URI) + MAX_DOMAIN_URI_SIZE;
    if ((calculator_URI_domain = (char *)malloc(calculator_URI_domain_len)) == NULL) {
        nErr = AEE_ENOMEMORY;
        printf("ERROR: unable to allocate memory for calculator_URI_domain of size: %d\n",
               calculator_URI_domain_len);
        goto bail;
    }
    nErr = snprintf(calculator_URI_domain, calculator_URI_domain_len, "%s%s",
                    calculator_URI, my_domain->uri);
    if (nErr < 0) {
        printf("ERROR 0x%x returned from snprintf\n", nErr);
        nErr = AEE_EFAILED;
        goto bail;
    }

    nErr = request_status_notifications_enable(domain_id, STATUS_CONTEXT,
                                               pd_status_notifier_callback);
    if (nErr != AEE_SUCCESS) {
        if (nErr != AEE_EUNSUPPORTEDAPI) {
            printf("ERROR 0x%x: request_status_notifications_enable failed\n", nErr);
            goto bail;
        }
        printf("WARNING: DSP status notifications not supported on this device, continuing\n");
        nErr = AEE_SUCCESS;
    }

    test_status = calculator_sum_test(calculator_URI_domain, test, num);
    if (test_status != AEE_SUCCESS) {
        printf("calculator_sum_test FAILED with error 0x%x\n", test_status);
        nErr |= test_status;
    } else {
        printf("calculator_sum_test PASSED\n");
    }

    test_status = calculator_max_test(calculator_URI_domain, test, num);
    if (test_status != AEE_SUCCESS) {
        printf("calculator_max_test FAILED with error 0x%x\n", test_status);
        nErr |= test_status;
    } else {
        printf("calculator_max_test PASSED\n");
    }

bail:
    free(calculator_URI_domain);
    free_buffer(test);
    return nErr;
}
