// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef TEST_UTILS_H
#define TEST_UTILS_H

#include "AEEStdErr.h"
#include "remote.h"
#include "test_config.h"
#include "test_error.h"

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Override at compile time: -DSKEL_SEARCH_PATH=\"/my/path\"
 * fastrpc uses ';' as the path delimiter (not ':'). */
#ifndef SKEL_SEARCH_PATH
#define SKEL_SEARCH_PATH "/usr/lib/rfsa/adsp;/usr/share;/usr/local/share"
#endif

/* Buffer size for the generated FastRPC URI plus its domain suffix. */
#define TEST_UTILS_DOMAIN_URI_BUFFER_SIZE 512U

extern test_config_t g_test_config;

/* Parses FastRPC-specific arguments, writes them into g_test_config, and
 * returns a filtered (argc, argv) pair for Unity. With no -d, discovers all
 * available DSP domains unless a metadata listing was requested. Returns 0
 * on success or -1 for invalid arguments/discovery failure. */
int test_config_init(int argc, const char **argv, int *out_argc, const char ***out_argv);

/* Builds a fully-qualified fastrpc_base_test URI for g_test_config.domain_id into buf.
 * Returns 0 on success, or -1 for an unknown domain, invalid buffer, or a
 * buffer too small for the URI.  On failure, buf is cleared when possible. */
int test_utils_domain_uri(char *buf, size_t buflen);
int test_utils_domain_uri_for(int domain_id, char *buf, size_t buflen);

/* Returns the plain domain name string (e.g. "cdsp", "adsp") for
 * g_test_config.domain_id, or NULL for an unknown ID. */
const char *test_utils_domain_name(void);
const char *test_utils_domain_name_for(int domain_id);

/* Sets DSP_LIBRARY_PATH, appending SKEL_SEARCH_PATH to any existing value.
 * Must be called before any fastrpc API. Returns 0 on success, -1 on failure. */
int test_utils_setup_dsp_lib_path(void);

void test_config_free_argv(const char **argv);

/* Calls remote_session_control(DSPRPC_CONTROL_UNSIGNED_MODULE) for the given domain.
 * Must be called before remote_handle_open when the skel is unsigned. */
int test_utils_enable_unsigned_pd(int domain);

/* ---- Error code reporting ---- */

/* Call after any FastRPC/DSP API call to store the code for the test footer.
 * The runner collects all codes and prints them consolidated after the test body. */
#define REPORT_ERROR_CODE(ret)                                                                     \
    do {                                                                                           \
        test_utils_set_last_error_code(ret);                                                       \
        test_utils_accumulate_error_code(ret);                                                     \
    } while (0)

void test_utils_set_last_error_code(int err);
int test_utils_get_last_error_code(void); /* Returns TEST_UTILS_INVALID_ERROR_CODE if none set */
void test_utils_reset_last_error_code(void);

void test_utils_accumulate_error_code(int err);
const int *test_utils_get_accumulated_error_codes(int *count);
int test_utils_error_codes_truncated(void);
void test_utils_reset_accumulated_error_codes(void);

/* Per-test setup helper: sets DSP_LIBRARY_PATH and optionally enables
 * unsigned PD. Returns the first setup error, or 0 on success. */
int test_utils_setup_test(void);

#ifdef __cplusplus
}
#endif

#endif /* TEST_UTILS_H */
