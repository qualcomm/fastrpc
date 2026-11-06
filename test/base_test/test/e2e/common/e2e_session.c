// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "e2e_session.h"
#include "AEEStdErr.h"
#include "fastrpc_base_test.h"
#include "test_utils.h"

void test_e2e_session_init(test_e2e_session_t *session)
{
    if (session)
        session->handle = TEST_E2E_INVALID_HANDLE;
}

int test_e2e_session_open(test_e2e_session_t *session)
{
    char uri[TEST_UTILS_DOMAIN_URI_BUFFER_SIZE];
    int ret;

    if (!session)
        return AEE_EBADPARM;

    test_e2e_session_init(session);
    if (test_utils_domain_uri(uri, sizeof(uri)) != 0)
        ret = AEE_EBADDOMAIN;
    else
        ret = fastrpc_base_test_open(uri, &session->handle);

    REPORT_ERROR_CODE(ret);
    return ret;
}

int test_e2e_session_close(test_e2e_session_t *session)
{
    int ret = AEE_SUCCESS;

    if (!session)
        return AEE_EBADPARM;

    if (session->handle != TEST_E2E_INVALID_HANDLE) {
        ret = fastrpc_base_test_close(session->handle);
        REPORT_ERROR_CODE(ret);
    }
    session->handle = TEST_E2E_INVALID_HANDLE;
    return ret;
}
