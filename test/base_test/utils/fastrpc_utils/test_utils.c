// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "test_utils.h"
#include "AEEStdErr.h"
#include "fastrpc_common.h"
#include "fastrpc_test.h"
#include "remote.h"

#include <errno.h>
#include <limits.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

test_config_t g_test_config = {
    .domain_id = DEFAULT_DOMAIN_ID, /* CDSP = 3; override with -d <id> */
    .unsigned_pd = 1,
    .logs_spec = NULL,    /* NULL = use registry defaults     */
    .any_tags = { NULL }, /* populated by --any-tags / --tags */
    .any_tag_count = 0,
    .all_tags = { NULL }, /* populated by --all-tags          */
    .all_tag_count = 0,
};

void test_config_init(int argc, const char **argv, int *out_argc, const char ***out_argv)
{
    const char **filtered = malloc((size_t)argc * sizeof(const char *));
    if (!filtered) {
        fprintf(stderr, "[test_config] malloc failed — using original argv\n");
        *out_argc = argc;
        *out_argv = argv;
        return;
    }

    int fi = 0;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            g_test_config.domain_id = atoi(argv[++i]);
            printf("[test_config] domain_id = %d\n", g_test_config.domain_id);
        } else if (strcmp(argv[i], "-u") == 0 && i + 1 < argc) {
            g_test_config.unsigned_pd = atoi(argv[++i]);
            printf("[test_config] unsigned_pd = %d\n", g_test_config.unsigned_pd);
        } else if (strcmp(argv[i], "--logs") == 0 && i + 1 < argc) {
            g_test_config.logs_spec = argv[++i];
            printf("[test_config] logs_spec = %s\n", g_test_config.logs_spec);
        } else if (strcmp(argv[i], "--any-tags") == 0 && i + 1 < argc) {
            /*
             * --any-tags <tag>  (repeatable, up to TEST_CONFIG_MAX_TAGS)
             *
             * OR-mode filter: a test runs if its TEST_CASE_TAGS() annotation
             * contains AT LEAST ONE of the --any-tags values.
             *
             * Consumed here; never forwarded to UnityMain().
             */
            if (g_test_config.any_tag_count < TEST_CONFIG_MAX_TAGS) {
                g_test_config.any_tags[g_test_config.any_tag_count++] = argv[++i];
                printf("[test_config] any-tag filter[%d] = %s\n", g_test_config.any_tag_count - 1,
                       g_test_config.any_tags[g_test_config.any_tag_count - 1]);
            } else {
                fprintf(stderr,
                        "[test_config] Warning: --any-tags limit (%d) reached, "
                        "ignoring '%s'\n",
                        TEST_CONFIG_MAX_TAGS, argv[++i]);
            }
        } else if (strcmp(argv[i], "--all-tags") == 0 && i + 1 < argc) {
            /*
             * --all-tags <tag>  (repeatable, up to TEST_CONFIG_MAX_TAGS)
             *
             * AND-mode filter: a test runs only if its TEST_CASE_TAGS()
             * annotation contains ALL of the --all-tags values.
             *
             * Consumed here; never forwarded to UnityMain().
             */
            if (g_test_config.all_tag_count < TEST_CONFIG_MAX_TAGS) {
                g_test_config.all_tags[g_test_config.all_tag_count++] = argv[++i];
                printf("[test_config] all-tag filter[%d] = %s\n", g_test_config.all_tag_count - 1,
                       g_test_config.all_tags[g_test_config.all_tag_count - 1]);
            } else {
                fprintf(stderr,
                        "[test_config] Warning: --all-tags limit (%d) reached, "
                        "ignoring '%s'\n",
                        TEST_CONFIG_MAX_TAGS, argv[++i]);
            }
        } else if (strcmp(argv[i], "--tags") == 0 && i + 1 < argc) {
            /*
             * --tags <tag>  — backward-compatible alias for --any-tags.
             *
             * Existing commands that use --tags continue to work unchanged.
             * The value is stored in any_tags[], identical to --any-tags.
             */
            if (g_test_config.any_tag_count < TEST_CONFIG_MAX_TAGS) {
                g_test_config.any_tags[g_test_config.any_tag_count++] = argv[++i];
                printf("[test_config] any-tag filter[%d] = %s (via --tags)\n",
                       g_test_config.any_tag_count - 1,
                       g_test_config.any_tags[g_test_config.any_tag_count - 1]);
            } else {
                fprintf(stderr,
                        "[test_config] Warning: --tags limit (%d) reached, "
                        "ignoring '%s'\n",
                        TEST_CONFIG_MAX_TAGS, argv[++i]);
            }
        } else {
            filtered[fi++] = argv[i];
        }
    }

    *out_argc = fi;
    *out_argv = filtered;
}

void test_utils_domain_uri(char *buf, size_t buflen)
{
    /* To add a new DSP type: add one case here and in test_utils_domain_name(). */
    const char *domain_suffix;
    switch (g_test_config.domain_id) {
    case ADSP_DOMAIN_ID:
        domain_suffix = ADSP_DOMAIN;
        break;
    case MDSP_DOMAIN_ID:
        domain_suffix = MDSP_DOMAIN;
        break;
    case SDSP_DOMAIN_ID:
        domain_suffix = SDSP_DOMAIN;
        break;
    case CDSP_DOMAIN_ID:
        domain_suffix = CDSP_DOMAIN;
        break;
    case CDSP1_DOMAIN_ID:
        domain_suffix = CDSP1_DOMAIN;
        break;
    case GDSP0_DOMAIN_ID:
        domain_suffix = GDSP0_DOMAIN;
        break;
    case GDSP1_DOMAIN_ID:
        domain_suffix = GDSP1_DOMAIN;
        break;
    default:
        domain_suffix = "";
        break;
    }
    snprintf(buf, buflen, "%s%s", fastrpc_test_URI, domain_suffix);
}

const char *test_utils_domain_name(void)
{
    /* To add a new DSP type: add one case here and in test_utils_domain_uri(). */
    switch (g_test_config.domain_id) {
    case ADSP_DOMAIN_ID:
        return ADSP_DOMAIN_NAME;
    case MDSP_DOMAIN_ID:
        return MDSP_DOMAIN_NAME;
    case SDSP_DOMAIN_ID:
        return SDSP_DOMAIN_NAME;
    case CDSP_DOMAIN_ID:
        return CDSP_DOMAIN_NAME;
    case CDSP1_DOMAIN_ID:
        return CDSP1_DOMAIN_NAME;
    case GDSP0_DOMAIN_ID:
        return GDSP0_DOMAIN_NAME;
    case GDSP1_DOMAIN_ID:
        return GDSP1_DOMAIN_NAME;
    default:
        return CDSP_DOMAIN_NAME; /* safe fallback */
    }
}

int test_utils_setup_dsp_lib_path(void)
{
    static int done = 0;
    if (done)
        return 0;

    char buf[512];
    const char *existing = getenv("DSP_LIBRARY_PATH");

    if (existing && existing[0] != '\0') {
        snprintf(buf, sizeof(buf), "%s;%s", existing, SKEL_SEARCH_PATH);
    } else {
        snprintf(buf, sizeof(buf), "%s", SKEL_SEARCH_PATH);
    }

    if (setenv("DSP_LIBRARY_PATH", buf, 1) != 0) {
        fprintf(stderr, "[test_utils] setenv DSP_LIBRARY_PATH failed: %s\n", strerror(errno));
        return -1;
    }

    printf("[test_utils] DSP_LIBRARY_PATH=%s\n", buf);
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

const char *test_utils_err_str(int err)
{
    switch (err) {
    case AEE_SUCCESS:
        return "AEE_SUCCESS";
    case AEE_EUNKNOWN:
        return "AEE_EUNKNOWN";
    case AEE_EFAILED:
        return "AEE_EFAILED";
    case AEE_ENOMEMORY:
        return "AEE_ENOMEMORY";
    case AEE_EBADPARM:
        return "AEE_EBADPARM";
    case AEE_EINVALIDFORMAT:
        return "AEE_EINVALIDFORMAT";
    case AEE_EUNSUPPORTED:
        return "AEE_EUNSUPPORTED";
    case AEE_ENOSUCH:
        return "AEE_ENOSUCH";
    case AEE_ECONNREFUSED:
        return "AEE_ECONNREFUSED";
    case AEE_EINVHANDLE:
        return "AEE_EINVHANDLE";
    case AEE_EBUSY:
        return "AEE_EBUSY";
    case AEE_EINVALIDDOMAIN:
        return "AEE_EINVALIDDOMAIN";
    case AEE_EINVALIDDEVICE:
        return "AEE_EINVALIDDEVICE";
    case AEE_ENOTINITIALIZED:
        return "AEE_ENOTINITIALIZED";
    case AEE_EUNSIGNEDMOD:
        return "AEE_EUNSIGNEDMOD";
    case AEE_ENOSUCHFILE:
        return "AEE_ENOSUCHFILE";
    case AEE_ENOSUCHMOD:
        return "AEE_ENOSUCHMOD";
    case AEE_EBADDOMAIN:
        return "AEE_EBADDOMAIN";
    case AEE_ERPC:
        return "AEE_ERPC";
    case AEE_ENORPCMEMORY:
        return "AEE_ENORPCMEMORY";
    default:
        return "(unknown error)";
    }
}

/* ------------------------------------------------------------------------- */
/* Segfault protection for negative tests                                     */
/* ------------------------------------------------------------------------- */

sigjmp_buf test_utils_segfault_jmp_buf;
volatile sig_atomic_t test_utils_segfault_expected = 0;

void test_utils_segfault_handler(int sig)
{
    /*
     * Re-install ourselves first. signal() is one-shot on Linux — it resets
     * to SIG_DFL after delivery. Without this, a second SIGSEGV/SIGABRT
     * (e.g. from a UAF thread still running after siglongjmp) would hit
     * SIG_DFL and kill the process. Re-installing here keeps the handler
     * alive for the next fault without needing SA_NODEFER.
     */
    signal(sig, test_utils_segfault_handler);

    if (test_utils_segfault_expected) {
        /*
         * Clear the flag BEFORE siglongjmp so that any subsequent fault
         * (e.g. from still-running UAF threads while Unity unwinds) falls
         * through to the SIG_DFL path below instead of jumping into an
         * already-unwound stack frame.
         */
        test_utils_segfault_expected = 0;
        siglongjmp(test_utils_segfault_jmp_buf, 1);
    }
    /* Unexpected signal — restore default and re-raise for a core dump */
    signal(sig, SIG_DFL);
    raise(sig);
}

void test_utils_install_segfault_handler(void)
{
    /*
     * Catch both SIGSEGV (invalid memory access) and SIGABRT (glibc abort,
     * e.g. "double free detected in tcache") so that negative tests which
     * intentionally trigger either condition do not crash the whole binary.
     */
    signal(SIGSEGV, test_utils_segfault_handler);
    signal(SIGABRT, test_utils_segfault_handler);
    test_utils_segfault_expected = 0;
}

/* Stores the most recent error code reported via REPORT_ERROR_CODE().
 * INT_MIN means "no code set yet for this test". */
static volatile int s_last_error_code = INT_MIN;

void test_utils_set_last_error_code(int err) { s_last_error_code = err; }

int test_utils_get_last_error_code(void) { return s_last_error_code; }

void test_utils_reset_last_error_code(void) { s_last_error_code = INT_MIN; }

/* Accumulation buffer; excess beyond TEST_UTILS_MAX_ERROR_CODES is silently dropped. */
static int s_accumulated_codes[TEST_UTILS_MAX_ERROR_CODES];
static int s_accumulated_count = 0;

void test_utils_accumulate_error_code(int err)
{
    if (s_accumulated_count < TEST_UTILS_MAX_ERROR_CODES) {
        s_accumulated_codes[s_accumulated_count++] = err;
    }
}

const int *test_utils_get_accumulated_error_codes(int *count)
{
    if (count) {
        *count = s_accumulated_count;
    }
    return s_accumulated_codes;
}

void test_utils_reset_accumulated_error_codes(void) { s_accumulated_count = 0; }

int test_suite_setup(const char *tag)
{
    int ret = test_utils_setup_dsp_lib_path();
    if (ret != 0) {
        printf("[%s] FATAL: test_utils_setup_dsp_lib_path() failed\n", tag);
        return -1;
    }

    if (g_test_config.unsigned_pd) {
        ret = test_utils_enable_unsigned_pd(g_test_config.domain_id);
        if (ret != 0) {
            printf("[%s] enable_unsigned_pd returned 0x%x (%s) - continuing\n", tag, ret,
                   test_utils_err_str(ret));
        }
    } else {
        printf("[%s] unsigned PD disabled via -u 0, skipping\n", tag);
    }

    return 0;
}
