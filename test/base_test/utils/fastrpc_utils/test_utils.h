// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef TEST_UTILS_H
#define TEST_UTILS_H

#include "AEEStdErr.h"
#include "fastrpc_common.h"
#include "remote.h"
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Override at compile time: -DSKEL_SEARCH_PATH=\"/my/path\"
 * fastrpc uses ';' as the path delimiter (not ':'). */
#ifndef SKEL_SEARCH_PATH
#define SKEL_SEARCH_PATH "/usr/share;/usr/local/share"
#endif

/* Safe buffer size for any domain URI suffix (longest: "&_dom=cdsp1" = 13 bytes). */
#define MAX_DOMAIN_URI_SIZE_SAFE 16

/* Maximum number of tag values accepted per tag flag on the command line.
 * Increasing this constant is the only change needed to support more tags. */
#define TEST_CONFIG_MAX_TAGS 32

/* Process-wide settings populated by main() from CLI args.
 *
 *   domain_id   : DSP domain (default DEFAULT_DOMAIN_ID / CDSP=3).
 *                 Override with -d <id>.
 *
 *   unsigned_pd : Call remote_session_control(DSPRPC_CONTROL_UNSIGNED_MODULE)
 *                 before opening handles.  Default 1; pass -u 0 to skip for
 *                 signed skels.
 *
 *   logs_spec   : Comma-separated list of log source names to enable, or one
 *                 of the special tokens below.  Populated by --logs <spec>.
 *
 *                   NULL / ""            use each source's enabled_by_default
 *                                        setting from the registry (default)
 *                   "none"               disable all log sources
 *                   "all"                enable every registered source
 *                   "journalctl"         enable only journalctl
 *                   "journalctl,dmesg"   enable exactly these two sources
 *
 *                 Applied by UnityFixtureFileOutputBegin() after
 *                 log_capture_init().  Use log_capture_list_sources() to
 *                 print the full list of available source names.
 *
 *   any_tags[]   : OR-mode inclusion filter.  Populated by --any-tags <tag>
 *                 (repeatable, up to TEST_CONFIG_MAX_TAGS values).
 *                 Also populated by --tags <tag> (backward-compatible alias).
 *
 *                 When any_tag_count > 0, a test passes this filter if its
 *                 TEST_CASE_TAGS() annotation contains AT LEAST ONE of the
 *                 listed tags.
 *
 *   all_tags[]   : AND-mode inclusion filter.  Populated by --all-tags <tag>
 *                 (repeatable, up to TEST_CONFIG_MAX_TAGS values).
 *
 *                 When all_tag_count > 0, a test passes this filter only if
 *                 its TEST_CASE_TAGS() annotation contains ALL of the listed
 *                 tags simultaneously.
 *
 *   Filter activation rules:
 *     - Both filters inactive (counts == 0): every test runs.
 *     - Only --any-tags active: test runs if it matches ANY listed tag.
 *     - Only --all-tags active: test runs if it matches ALL listed tags.
 *     - Both active: test must pass BOTH filters independently.
 *
 *   Tags are compared case-sensitively (strcmp).
 *
 *   Recommended tag vocabulary:
 *     Functionality  — "Remote", "DspQueue", "RpcMem", "Profiling"
 *     Classification — "unit", "feature"
 *     Polarity       — "positive", "negative"
 */
typedef struct {
    int domain_id;
    int unsigned_pd;
    const char *logs_spec;                      /**< --logs <spec>; NULL = registry defaults  */
    const char *any_tags[TEST_CONFIG_MAX_TAGS]; /**< --any-tags / --tags values (OR filter)   */
    int any_tag_count;                          /**< number of active --any-tags entries       */
    const char *all_tags[TEST_CONFIG_MAX_TAGS]; /**< --all-tags values (AND filter)            */
    int all_tag_count;                          /**< number of active --all-tags entries       */
} test_config_t;

extern test_config_t g_test_config;

/* Parses -d <domain_id> and -u <0|1> from argv, writes results into g_test_config,
 * and returns a filtered (argc, argv) pair with those flags removed for UnityMain(). */
void test_config_init(int argc, const char **argv, int *out_argc, const char ***out_argv);

/* Builds a fully-qualified fastrpc_test URI for g_test_config.domain_id into buf.
 * buf must be at least sizeof(fastrpc_test_URI) + MAX_DOMAIN_URI_SIZE_SAFE bytes. */
void test_utils_domain_uri(char *buf, size_t buflen);

/* Returns the plain domain name string (e.g. "cdsp", "adsp") for
 * g_test_config.domain_id. Returns CDSP_DOMAIN_NAME for unknown IDs.
 * Use with FASTRPC_RESERVE_NEW_SESSION / FASTRPC_GET_URI APIs. */
const char *test_utils_domain_name(void);

/* Sets DSP_LIBRARY_PATH, appending SKEL_SEARCH_PATH to any existing value.
 * Must be called before any fastrpc API. Returns 0 on success, -1 on failure. */
int test_utils_setup_dsp_lib_path(void);

/* Calls remote_session_control(DSPRPC_CONTROL_UNSIGNED_MODULE) for the given domain.
 * Must be called before remote_handle_open when the skel is unsigned. */
int test_utils_enable_unsigned_pd(int domain);

/* Returns a human-readable string for a fastrpc/AEE error code. */
const char *test_utils_err_str(int err);

/* ---- Segfault protection for negative tests ---- */

extern sigjmp_buf test_utils_segfault_jmp_buf;
extern volatile sig_atomic_t test_utils_segfault_expected;

void test_utils_segfault_handler(int sig);
void test_utils_install_segfault_handler(
    void); /* Call in TEST_SETUP — catches SIGSEGV and SIGABRT */

/* Wrap code that might segfault; converts a segfault into TEST_FAIL_MESSAGE.
 *   TEST_UTILS_EXPECT_SEGFAULT_BEGIN() {
 *       ret = some_function(NULL);
 *   } TEST_UTILS_EXPECT_SEGFAULT_END("some_function with NULL caused segfault"); */
#define TEST_UTILS_EXPECT_SEGFAULT_BEGIN()                                                         \
    test_utils_segfault_expected = 1;                                                              \
    if (sigsetjmp(test_utils_segfault_jmp_buf, 1) == 0) {

#define TEST_UTILS_EXPECT_SEGFAULT_END(fail_msg)                                                   \
    test_utils_segfault_expected = 0;                                                              \
    }                                                                                              \
    else                                                                                           \
    {                                                                                              \
        test_utils_segfault_expected = 0;                                                          \
        TEST_FAIL_MESSAGE(fail_msg);                                                               \
    }

/* ---- Error code reporting ---- */

/* Call after any FastRPC/DSP API call to store the code for Allure reporting.
 * The runner collects all codes and prints them consolidated after the test body. */
#define REPORT_ERROR_CODE(ret)                                                                     \
    do {                                                                                           \
        test_utils_set_last_error_code(ret);                                                       \
        test_utils_accumulate_error_code(ret);                                                     \
    } while (0)

void test_utils_set_last_error_code(int err);
int test_utils_get_last_error_code(void); /* Returns INT_MIN if none set */
void test_utils_reset_last_error_code(void);

/* Maximum error codes accumulated per test (excess silently dropped). */
#define TEST_UTILS_MAX_ERROR_CODES 32

void test_utils_accumulate_error_code(int err);
const int *test_utils_get_accumulated_error_codes(int *count);
void test_utils_reset_accumulated_error_codes(void);

/* One-time process-level setup: sets DSP_LIBRARY_PATH and optionally enables
 * unsigned PD. Returns 0 on success, -1 if DSP_LIBRARY_PATH setup fails. */
int test_suite_setup(const char *tag);

#ifdef __cplusplus
}
#endif

#endif /* TEST_UTILS_H */
