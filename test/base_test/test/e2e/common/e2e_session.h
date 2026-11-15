// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef BASE_TEST_E2E_SESSION_H
#define BASE_TEST_E2E_SESSION_H

#include "remote.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TEST_E2E_INVALID_HANDLE ((remote_handle64)-1)

typedef struct {
    remote_handle64 handle;
} test_e2e_session_t;

void test_e2e_session_init(test_e2e_session_t *session);
int test_e2e_session_open(test_e2e_session_t *session);
int test_e2e_session_close(test_e2e_session_t *session);

#ifdef __cplusplus
}
#endif

#endif /* BASE_TEST_E2E_SESSION_H */
