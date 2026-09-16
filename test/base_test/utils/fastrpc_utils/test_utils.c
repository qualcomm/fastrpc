// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "test_utils.h"
#include "AEEStdErr.h"
#include "fastrpc_common.h"
#include "fastrpc_test.h"
#include "remote.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define REMOTEPROC_CLASS_PATH "/sys/class/remoteproc"

typedef struct {
    int domain_id;
    const char *remoteproc_names[2];
    const char *devnodes[2];
} domain_probe_t;

/* Keep domain aliases and device nodes aligned with fastrpc-healthcheck. */
static const domain_probe_t domain_probes[] = {
    { ADSP_DOMAIN_ID, { "adsp", NULL },
      { "/dev/fastrpc-adsp", "/dev/fastrpc-adsp-secure" } },
    { MDSP_DOMAIN_ID, { "mdsp", NULL },
      { "/dev/fastrpc-mdsp", "/dev/fastrpc-mdsp-secure" } },
    { SDSP_DOMAIN_ID, { "sdsp", "slpi" },
      { "/dev/fastrpc-sdsp", "/dev/fastrpc-sdsp-secure" } },
    { CDSP_DOMAIN_ID, { "cdsp", NULL },
      { "/dev/fastrpc-cdsp", "/dev/fastrpc-cdsp-secure" } },
    { CDSP1_DOMAIN_ID, { "cdsp1", NULL },
      { "/dev/fastrpc-cdsp1", "/dev/fastrpc-cdsp1-secure" } },
    { GDSP0_DOMAIN_ID, { "gpdsp0", "gdsp0" },
      { "/dev/fastrpc-gdsp0", "/dev/fastrpc-gdsp0-secure" } },
    { GDSP1_DOMAIN_ID, { "gpdsp1", "gdsp1" },
      { "/dev/fastrpc-gdsp1", "/dev/fastrpc-gdsp1-secure" } },
};

test_config_t g_test_config = {
    .domain_id = DEFAULT_DOMAIN_ID,
    .domain_ids = { 0 },
    .domain_count = 0,
    .unsigned_pd = 1,
    .silent_mode = 0,
    .list_mode = TEST_LIST_NONE,
    .logs_spec = NULL,    /* NULL = use registry defaults     */
    .any_tags = { NULL }, /* populated by --any-tags / --tags */
    .any_tag_count = 0,
    .all_tags = { NULL }, /* populated by --all-tags          */
    .all_tag_count = 0,
};

static int read_text_file(const char *path, char *buf, size_t buflen)
{
    FILE *file;
    size_t length;

    file = fopen(path, "r");
    if (!file)
        return -1;

    length = fread(buf, 1, buflen - 1, file);
    if (ferror(file)) {
        fclose(file);
        return -1;
    }

    fclose(file);
    buf[length] = '\0';
    buf[strcspn(buf, "\r\n")] = '\0';
    return 0;
}

static int remoteproc_name_matches(const char *name, const char *key)
{
    const char *match;
    size_t key_len;

    match = strstr(name, key);
    if (!match)
        return 0;

    key_len = strlen(key);
    if (!isdigit((unsigned char)key[key_len - 1]) &&
        isdigit((unsigned char)match[key_len]))
        return 0;

    return 1;
}

static int domain_has_devnode(const domain_probe_t *probe)
{
    struct stat st;

    for (size_t i = 0; i < 2; i++) {
        if (stat(probe->devnodes[i], &st) == 0 && S_ISCHR(st.st_mode))
            return 1;
    }

    return 0;
}

static int discover_domains(int domains[TEST_CONFIG_MAX_DOMAINS])
{
    int running[TEST_CONFIG_MAX_DOMAINS] = { 0 };
    DIR *dir;
    struct dirent *entry;
    int count = 0;

    dir = opendir(REMOTEPROC_CLASS_PATH);
    if (!dir) {
        fprintf(stderr, "[test_config] unable to scan %s: %s\n",
                REMOTEPROC_CLASS_PATH, strerror(errno));
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {
        char path[PATH_MAX];
        char name[128];
        char state[64];

        if (strncmp(entry->d_name, "remoteproc", strlen("remoteproc")) != 0)
            continue;

        snprintf(path, sizeof(path), "%s/%s/name", REMOTEPROC_CLASS_PATH, entry->d_name);
        if (read_text_file(path, name, sizeof(name)) != 0)
            continue;

        snprintf(path, sizeof(path), "%s/%s/state", REMOTEPROC_CLASS_PATH, entry->d_name);
        if (read_text_file(path, state, sizeof(state)) != 0)
            continue;

        for (char *p = name; *p; p++)
            *p = (char)tolower((unsigned char)*p);
        for (char *p = state; *p; p++)
            *p = (char)tolower((unsigned char)*p);

        if (strstr(state, "running") == NULL)
            continue;

        for (size_t i = 0; i < TEST_CONFIG_MAX_DOMAINS; i++) {
            for (size_t j = 0; j < 2 && domain_probes[i].remoteproc_names[j]; j++) {
                if (remoteproc_name_matches(name, domain_probes[i].remoteproc_names[j])) {
                    running[i] = 1;
                    break;
                }
            }
        }
    }

    closedir(dir);

    for (size_t i = 0; i < TEST_CONFIG_MAX_DOMAINS; i++) {
        if (running[i] && domain_has_devnode(&domain_probes[i]))
            domains[count++] = domain_probes[i].domain_id;
    }

    return count;
}

static int add_explicit_domain(int domain_id)
{
    for (int i = 0; i < g_test_config.domain_count; i++) {
        if (g_test_config.domain_ids[i] == domain_id)
            return 0;
    }

    if (g_test_config.domain_count >= TEST_CONFIG_MAX_DOMAINS)
        return -1;

    g_test_config.domain_ids[g_test_config.domain_count++] = domain_id;
    return 0;
}

static int parse_domain_id(const char *value, int *domain_id)
{
    char *end;
    long parsed;

    errno = 0;
    parsed = strtol(value, &end, 10);
    if (errno != 0 || *value == '\0' || *end != '\0')
        return -1;

    for (size_t i = 0; i < TEST_CONFIG_MAX_DOMAINS; i++) {
        if (domain_probes[i].domain_id == parsed) {
            *domain_id = (int)parsed;
            return 0;
        }
    }

    return -1;
}

static int set_list_mode(test_list_mode_t mode)
{
    if (g_test_config.list_mode != TEST_LIST_NONE && g_test_config.list_mode != mode) {
        fprintf(stderr, "[test_config] only one test listing mode may be selected\n");
        return -1;
    }

    g_test_config.list_mode = mode;
    return 0;
}

static int parse_list_mode(const char *value)
{
    if (strcmp(value, "tests") == 0)
        return set_list_mode(TEST_LIST_TESTS);
    if (strcmp(value, "groups") == 0)
        return set_list_mode(TEST_LIST_GROUPS);
    if (strcmp(value, "tags") == 0)
        return set_list_mode(TEST_LIST_TAGS);

    fprintf(stderr, "[test_config] invalid -l value '%s'; expected tests, groups, or tags\n",
            value);
    return -1;
}

int test_config_init(int argc, const char **argv, int *out_argc, const char ***out_argv)
{
    const char **filtered = malloc((size_t)argc * sizeof(const char *));
    int any_tag_via_alias[TEST_CONFIG_MAX_TAGS] = { 0 };
    int domains_explicit = 0;
    int logs_explicit = 0;
    int unsigned_pd_explicit = 0;
    int silent_explicit = 0;

    if (!filtered) {
        fprintf(stderr, "[test_config] malloc failed\n");
        return -1;
    }

    int fi = 0;

    g_test_config.domain_count = 0;
    g_test_config.unsigned_pd = 1;
    g_test_config.silent_mode = 0;
    g_test_config.list_mode = TEST_LIST_NONE;
    g_test_config.logs_spec = NULL;
    g_test_config.any_tag_count = 0;
    g_test_config.all_tag_count = 0;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-l") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "[test_config] -l requires tests, groups, or tags\n");
                free(filtered);
                return -1;
            }
            if (parse_list_mode(argv[++i]) != 0) {
                free(filtered);
                return -1;
            }
        } else if (strcmp(argv[i], "--list-tests") == 0) {
            if (set_list_mode(TEST_LIST_TESTS) != 0) {
                free(filtered);
                return -1;
            }
        } else if (strcmp(argv[i], "--list-groups") == 0) {
            if (set_list_mode(TEST_LIST_GROUPS) != 0) {
                free(filtered);
                return -1;
            }
        } else if (strcmp(argv[i], "--list-tags") == 0) {
            if (set_list_mode(TEST_LIST_TAGS) != 0) {
                free(filtered);
                return -1;
            }
        } else if (strcmp(argv[i], "-d") == 0) {
            int domain_id;

            if (i + 1 >= argc) {
                fprintf(stderr, "[test_config] -d requires a domain ID\n");
                free(filtered);
                return -1;
            }

            if (parse_domain_id(argv[++i], &domain_id) != 0 ||
                add_explicit_domain(domain_id) != 0) {
                fprintf(stderr, "[test_config] invalid domain ID: %s\n", argv[i]);
                free(filtered);
                return -1;
            }
            domains_explicit = 1;
        } else if (strcmp(argv[i], "-u") == 0 && i + 1 < argc) {
            g_test_config.unsigned_pd = atoi(argv[++i]);
            unsigned_pd_explicit = 1;
        } else if (strcmp(argv[i], "--silent") == 0) {
            /*
             * --silent  — suppress the per-test header/dot/footer block for
             * tests that pass; failed and skipped tests still print their
             * full block. Implemented by buffering each test's stdout output
             * and discarding it on a pass (see UnityTestRunnerWithFileOutput
             * in unity_fixture_file_output.c). Consumed here; never
             * forwarded to UnityMain().
             */
            g_test_config.silent_mode = 1;
            silent_explicit = 1;
        } else if (strcmp(argv[i], "--logs") == 0 && i + 1 < argc) {
            g_test_config.logs_spec = argv[++i];
            logs_explicit = 1;
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
                int tag_index = g_test_config.any_tag_count++;

                g_test_config.any_tags[tag_index] = argv[++i];
                any_tag_via_alias[tag_index] = 1;
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

    if (g_test_config.list_mode != TEST_LIST_NONE) {
        *out_argc = fi;
        *out_argv = filtered;
        return 0;
    }

    if (unsigned_pd_explicit)
        printf("[test_config] unsigned_pd = %d\n", g_test_config.unsigned_pd);
    if (silent_explicit)
        printf("[test_config] silent_mode = %d\n", g_test_config.silent_mode);
    if (logs_explicit)
        printf("[test_config] logs_spec = %s\n", g_test_config.logs_spec);
    for (int i = 0; i < g_test_config.any_tag_count; i++) {
        printf("[test_config] any-tag filter[%d] = %s%s\n", i, g_test_config.any_tags[i],
               any_tag_via_alias[i] ? " (via --tags)" : "");
    }
    for (int i = 0; i < g_test_config.all_tag_count; i++)
        printf("[test_config] all-tag filter[%d] = %s\n", i, g_test_config.all_tags[i]);

    if (g_test_config.domain_count == 0) {
        g_test_config.domain_count = discover_domains(g_test_config.domain_ids);
        if (g_test_config.domain_count <= 0) {
            fprintf(stderr,
                    "[test_config] no running DSP domains with FastRPC device nodes found\n");
            free(filtered);
            return -1;
        }
    }

    printf("[test_config] %s domains:",
           domains_explicit ? "selected" : "discovered");
    for (int i = 0; i < g_test_config.domain_count; i++)
        printf(" %s(%d)", test_utils_domain_name_for(g_test_config.domain_ids[i]),
               g_test_config.domain_ids[i]);
    putchar('\n');

    *out_argc = fi;
    *out_argv = filtered;
    return 0;
}

void test_utils_domain_uri_for(int domain_id, char *buf, size_t buflen)
{
    /* To add a new DSP type: add one case here and in test_utils_domain_name(). */
    const char *domain_suffix;
    switch (domain_id) {
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

void test_utils_domain_uri(char *buf, size_t buflen)
{
    test_utils_domain_uri_for(g_test_config.domain_id, buf, buflen);
}

const char *test_utils_domain_name_for(int domain_id)
{
    /* To add a new DSP type: add one case here and in test_utils_domain_uri(). */
    switch (domain_id) {
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

const char *test_utils_domain_name(void)
{
    return test_utils_domain_name_for(g_test_config.domain_id);
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
