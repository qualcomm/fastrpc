// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "remote.h"
#include "test_config.h"
#include "test_error.h"
#include "test_utils.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int test_utils_setup_dsp_lib_path(void)
{
    static _Thread_local int done;
    const char *existing;
    size_t existing_len;
    size_t search_len;
    size_t separator_len;
    size_t total_len;
    char *path;

    if (done)
        return 0;

    existing = getenv("DSP_LIBRARY_PATH");
    if (existing == NULL)
        existing = "";

    existing_len = strlen(existing);
    search_len = strlen(SKEL_SEARCH_PATH);
    separator_len = existing_len != 0 ? 1 : 0;

    if (existing_len > SIZE_MAX - separator_len ||
        existing_len + separator_len > SIZE_MAX - search_len ||
        existing_len + separator_len + search_len > SIZE_MAX - 1) {
        fprintf(stderr, "[test_utils] DSP_LIBRARY_PATH is too long\n");
        return -1;
    }

    total_len = existing_len + separator_len + search_len + 1;
    path = malloc(total_len);
    if (path == NULL) {
        fprintf(stderr, "[test_utils] unable to allocate DSP_LIBRARY_PATH\n");
        return -1;
    }

    memcpy(path, existing, existing_len);
    if (separator_len != 0)
        path[existing_len] = ';';
    memcpy(path + existing_len + separator_len, SKEL_SEARCH_PATH, search_len + 1);

    if (setenv("DSP_LIBRARY_PATH", path, 1) != 0) {
        fprintf(stderr, "[test_utils] setenv DSP_LIBRARY_PATH failed: %s\n", strerror(errno));
        free(path);
        return -1;
    }

    printf("[test_utils] DSP_LIBRARY_PATH=%s\n", path);
    free(path);
    done = 1;
    return 0;
}

int test_utils_enable_unsigned_pd(int domain)
{
    struct remote_rpc_control_unsigned_module attr;
    memset(&attr, 0, sizeof(attr));
    attr.domain = domain;
    attr.enable = 1;

    return remote_session_control(DSPRPC_CONTROL_UNSIGNED_MODULE, &attr, sizeof(attr));
}
int test_utils_setup_test(void)
{
    int ret = test_utils_setup_dsp_lib_path();

    if (ret != 0) {
        REPORT_ERROR_CODE(ret);
        return ret;
    }

    if (!g_test_config.unsigned_pd)
        return 0;

    ret = test_utils_enable_unsigned_pd(g_test_config.domain_id);
    REPORT_ERROR_CODE(ret);
    return ret;
}
