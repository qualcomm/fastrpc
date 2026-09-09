// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "../../fastrpc_utils/test_utils.h"
#include "../../log_capture/log_capture.h"
#include "../allure/unity_allure_output.h"
#include "unity_fixture.h"           /* Include to get Unity types */
#include "unity_fixture_internals.h" /* For UnityFixture (GroupFilter/Group/NameFilter/Name) */
#include "unity_internals.h"
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

/* ANSI colour codes -- only emitted when stdout is a tty (see use_color()). */
#define CLR_RESET "\033[0m"
#define CLR_BOLD "\033[1m"
#define CLR_DIM "\033[2m"
#define CLR_GREEN "\033[32m"
#define CLR_RED "\033[31m"
#define CLR_YELLOW "\033[33m"
#define CLR_CYAN "\033[36m"
#define CLR_BGREEN "\033[1;32m"
#define CLR_BRED "\033[1;31m"
#define CLR_BYELLOW "\033[1;33m"
#define CLR_BCYAN "\033[1;36m"
#define CLR_BWHITE "\033[1;37m"

/* Box inner width: total terminal columns minus the two border characters. */
#define BOX_INNER 77 /* fits inside an 80-column terminal: │<77 chars>│ */

static int use_color(void)
{
    static int cached = -1;
    if (cached == -1)
        cached = isatty(STDOUT_FILENO) ? 1 : 0;
    return cached;
}

/* Convenience wrappers: emit the escape string only when colour is on. */
#define COLOR(esc) (use_color() ? (esc) : "")

/* Strips everything up to and including "fastrpc-test/" for display.
 * Falls back to the last two path components for out-of-tree builds. */
static const char *shorten_path(const char *path)
{
    const char *marker;
    const char *last_slash;
    const char *prev;

    if (!path || path[0] == '\0')
        return "(unknown)";

    marker = strstr(path, "fastrpc-test/");
    if (marker)
        return marker + strlen("fastrpc-test/");

    /* Fallback: return the last two path components. */
    last_slash = strrchr(path, '/');
    if (last_slash && last_slash != path) {
        prev = last_slash - 1;
        while (prev > path && *prev != '/')
            prev--;
        if (*prev == '/')
            return prev + 1;
    }
    return path;
}

static void print_suite_banner(const char *suite_name)
{
    int i, title_len, left_pad, right_pad;
    const char *title = (suite_name && suite_name[0] != '\0') ? suite_name : "FastRPC Test Suite";

    title_len = (int)strlen(title);
    left_pad = (BOX_INNER - title_len) / 2;
    right_pad = BOX_INNER - title_len - left_pad;

    printf("\n");
    printf("%s╔", COLOR(CLR_BCYAN));
    for (i = 0; i < BOX_INNER; i++)
        printf("═");
    printf("╗%s\n", COLOR(CLR_RESET));

    printf("%s║%s", COLOR(CLR_BCYAN), COLOR(CLR_RESET));
    for (i = 0; i < left_pad; i++)
        printf(" ");
    printf("%s%s%s", COLOR(CLR_BWHITE), title, COLOR(CLR_RESET));
    for (i = 0; i < right_pad; i++)
        printf(" ");
    printf("%s║%s\n", COLOR(CLR_BCYAN), COLOR(CLR_RESET));

    printf("%s╚", COLOR(CLR_BCYAN));
    for (i = 0; i < BOX_INNER; i++)
        printf("═");
    printf("╝%s\n\n", COLOR(CLR_RESET));
    fflush(stdout);
}

static void print_test_header(const char *group, const char *name)
{
    int i;
    char label[512];

    snprintf(label, sizeof(label), "  TEST  %s :: %s", group, name);

    printf("%s┌", COLOR(CLR_DIM));
    for (i = 0; i < BOX_INNER; i++)
        printf("─");
    printf("┐%s\n", COLOR(CLR_RESET));

    printf("%s│%s%s%-*s%s%s│%s\n", COLOR(CLR_DIM), COLOR(CLR_RESET), COLOR(CLR_BWHITE), BOX_INNER,
           label, COLOR(CLR_RESET), COLOR(CLR_DIM), COLOR(CLR_RESET));

    printf("%s└", COLOR(CLR_DIM));
    for (i = 0; i < BOX_INNER; i++)
        printf("─");
    printf("┘%s\n", COLOR(CLR_RESET));
    fflush(stdout);
}

/* Prints error codes (run-length encoded), result (PASS/FAIL/SKIP),
 * and for failures: location and word-wrapped message. */
static void print_test_footer(int passed, int ignored, const char *file, unsigned int fail_line,
                              const char *message, const int *codes, int code_count)
{
    if (code_count > 0) {
        int i;
        int first = 1;

        printf("  %sError codes :%s ", COLOR(CLR_DIM), COLOR(CLR_RESET));

        for (i = 0; i < code_count;) {
            int val = codes[i];
            int run = 1;

            while (i + run < code_count && codes[i + run] == val)
                run++;

            if (!first)
                printf("%s, %s", COLOR(CLR_DIM), COLOR(CLR_RESET));

            printf("%s0x%08x%s %s(%s)%s", COLOR(CLR_CYAN), (unsigned int)val, COLOR(CLR_RESET),
                   COLOR(CLR_DIM), test_utils_err_str(val), COLOR(CLR_RESET));

            if (run > 1)
                printf(" %s\xc3\x97%d%s", /* UTF-8 multiplication sign ×N */
                       COLOR(CLR_DIM), run, COLOR(CLR_RESET));

            first = 0;
            i += run;
        }
        printf("\n");
    }

    if (ignored) {
        printf("  %sResult      :%s %s\xe2\x8a\x98 SKIP%s\n", /* ⊘ */
               COLOR(CLR_DIM), COLOR(CLR_RESET), COLOR(CLR_BYELLOW), COLOR(CLR_RESET));
    } else if (passed) {
        printf("  %sResult      :%s %s\xe2\x9c\x94 PASS%s\n", /* ✔ */
               COLOR(CLR_DIM), COLOR(CLR_RESET), COLOR(CLR_BGREEN), COLOR(CLR_RESET));
    } else {
        printf("  %sResult      :%s %s\xe2\x9c\x98 FAIL%s\n", /* ✘ */
               COLOR(CLR_DIM), COLOR(CLR_RESET), COLOR(CLR_BRED), COLOR(CLR_RESET));

        if (file && file[0] != '\0') {
            printf("  %sLocation    :%s %s%s:%u%s\n", COLOR(CLR_DIM), COLOR(CLR_RESET),
                   COLOR(CLR_YELLOW), shorten_path(file), fail_line, COLOR(CLR_RESET));
        }

        if (message && message[0] != '\0') {
            const int wrap_col = BOX_INNER - 16;
            const int cont_ind = 20;
            int col = 0;
            const char *p = message;

            printf("  %sMessage     :%s ", COLOR(CLR_DIM), COLOR(CLR_RESET));

            while (*p) {
                if (col >= wrap_col && *p == ' ') {
                    printf("\n%*s", cont_ind, "");
                    col = 0;
                    p++;
                    continue;
                }
                putchar((unsigned char)*p);
                col++;
                p++;
            }
            printf("\n");
        }
    }

    printf("\n");
    fflush(stdout);
}

static void print_summary(uint32_t total, uint32_t failures, uint32_t ignored)
{
    uint32_t passed = total - failures - ignored;
    int i;
    char counts[256];
    int visible_len;

    printf("\n");

    printf("%s┌", COLOR(CLR_BOLD));
    for (i = 0; i < BOX_INNER; i++)
        printf("─");
    printf("┐%s\n", COLOR(CLR_RESET));

    printf("%s│  TEST SUITE SUMMARY%-*s│%s\n", COLOR(CLR_BOLD), BOX_INNER - 20, "",
           COLOR(CLR_RESET));

    printf("%s├", COLOR(CLR_BOLD));
    for (i = 0; i < BOX_INNER; i++)
        printf("─");
    printf("┤%s\n", COLOR(CLR_RESET));

    visible_len
        = snprintf(counts, sizeof(counts), "  Total: %u   Passed: %u   Failed: %u   Skipped: %u",
                   total, passed, failures, ignored);

    printf("%s│%s"
           "  Total: %u   "
           "%sPassed: %u%s   "
           "%sFailed: %u%s   "
           "%sSkipped: %u%s"
           "%*s"
           "%s│%s\n",
           COLOR(CLR_BOLD), COLOR(CLR_RESET), total, COLOR(CLR_BGREEN), passed, COLOR(CLR_RESET),
           (failures > 0) ? COLOR(CLR_BRED) : COLOR(CLR_BGREEN), failures, COLOR(CLR_RESET),
           (ignored > 0) ? COLOR(CLR_BYELLOW) : COLOR(CLR_DIM), ignored, COLOR(CLR_RESET),
           BOX_INNER - visible_len, "", COLOR(CLR_BOLD), COLOR(CLR_RESET));

    printf("%s└", COLOR(CLR_BOLD));
    for (i = 0; i < BOX_INNER; i++)
        printf("─");
    printf("┘%s\n", COLOR(CLR_RESET));

    if (failures == 0) {
        printf("\n%s  \xe2\x9c\x94  All %u tests passed.%s\n\n", /* ✔ */
               COLOR(CLR_BGREEN), total, COLOR(CLR_RESET));
    } else {
        printf("\n%s  \xe2\x9c\x98  %u test(s) failed.%s\n\n", /* ✘ */
               COLOR(CLR_BRED), failures, COLOR(CLR_RESET));
    }
    fflush(stdout);
}

/* Declare the original UnityTestRunner function from unity_fixture.c */
void UnityTestRunner(unityfunction *setup, unityfunction *test_body, unityfunction *teardown,
                     const char *printable_name, const char *group, const char *name,
                     const char *file, unsigned int line);

/* Save pointer to original UnityTestRunner */
typedef void (*UnityTestRunnerFunc)(unityfunction *, unityfunction *, unityfunction *, const char *,
                                    const char *, const char *, const char *, unsigned int);
static UnityTestRunnerFunc OriginalUnityTestRunner = UnityTestRunner;

/* Now include our header which redefines UnityTestRunner macro */
#include "unity_fixture_file_output.h"

/* =========================================================================
 * Label registry — self-registering linked list
 *
 * Each TEST_GROUP_META() in a test_*.c file emits a constructor that calls
 * unity_label_registry_add() at program startup.  The list is then queried
 * by UnityTestRunnerWithFileOutput when it opens a new suite.
 * ========================================================================= */

static unity_group_labels_t *s_label_registry_head = NULL;

void unity_label_registry_add(unity_group_labels_t *entry)
{
    if (!entry)
        return;
    entry->next = s_label_registry_head;
    s_label_registry_head = entry;
}

const unity_group_labels_t *unity_label_registry_find(const char *group)
{
    if (!group)
        return NULL;
    const unity_group_labels_t *e = s_label_registry_head;
    while (e) {
        if (strcmp(e->group, group) == 0)
            return e;
        e = e->next;
    }
    return NULL;
}

/* =========================================================================
 * Per-test label override — set by TEST_LABELS() inside a test body
 *
 * Unity runs tests sequentially (single-threaded), so a plain global is
 * safe.  The override is read in UnityTestRunnerWithFileOutput after
 * OriginalUnityTestRunner() returns, then immediately cleared.
 * ========================================================================= */

static struct {
    int active;
    const char *layer;
    const char *epic;
    const char *feature;
    const char *story;
} s_test_label_override = { 0, NULL, NULL, NULL, NULL };

void unity_test_labels_set(const char *layer, const char *epic, const char *feature,
                           const char *story)
{
    s_test_label_override.active = 1;
    s_test_label_override.layer = layer;
    s_test_label_override.epic = epic;
    s_test_label_override.feature = feature;
    s_test_label_override.story = story;
}

static void test_label_override_clear(void)
{
    s_test_label_override.active = 0;
    s_test_label_override.layer = NULL;
    s_test_label_override.epic = NULL;
    s_test_label_override.feature = NULL;
    s_test_label_override.story = NULL;
}

/* =========================================================================
 * Per-test-case tag registry — self-registering linked list
 *
 * Each TEST_CASE_TAGS() in a test_*.c file emits a constructor that calls
 * unity_test_case_tag_registry_add() at program startup.  The list is
 * queried by unity_test_case_tag_filter_passes() inside
 * UnityTestRunnerWithFileOutput before each test is dispatched.
 *
 * Filtering semantics:
 *   --any-tags A B  — run if the test's tags contain A OR B  (OR filter).
 *   --all-tags A B  — run only if the test's tags contain A AND B (AND filter).
 *   Both active     — test must pass BOTH filters independently.
 *   Neither active  — every test runs (filter inactive).
 * ========================================================================= */

static unity_test_case_tags_t *s_test_case_tag_registry_head = NULL;

void unity_test_case_tag_registry_add(unity_test_case_tags_t *entry)
{
    if (!entry)
        return;
    entry->next = s_test_case_tag_registry_head;
    s_test_case_tag_registry_head = entry;
}

const unity_test_case_tags_t *unity_test_case_tag_registry_find(const char *group, const char *name)
{
    if (!group || !name)
        return NULL;
    const unity_test_case_tags_t *e = s_test_case_tag_registry_head;
    while (e) {
        if (strcmp(e->group, group) == 0 && strcmp(e->name, name) == 0)
            return e;
        e = e->next;
    }
    return NULL;
}

/**
 * unity_test_case_tag_filter_passes
 *
 * Returns 1 (run) when all active filters pass:
 *
 *   --any-tags filter (OR):
 *     Passes when any_tag_count == 0 (inactive), OR when the test's
 *     TEST_CASE_TAGS() list contains at least one of the any_tags[] values.
 *
 *   --all-tags filter (AND):
 *     Passes when all_tag_count == 0 (inactive), OR when the test's
 *     TEST_CASE_TAGS() list contains every one of the all_tags[] values.
 *
 * Returns 0 (skip) when:
 *   - Either active filter fails, OR
 *   - Any filter is active AND the test has no TEST_CASE_TAGS() annotation.
 *
 * Tags are compared case-sensitively (strcmp).
 */
int unity_test_case_tag_filter_passes(const char *group, const char *name)
{
    /* Both filters inactive — every test runs. */
    if (g_test_config.any_tag_count == 0 && g_test_config.all_tag_count == 0)
        return 1;

    const unity_test_case_tags_t *entry = unity_test_case_tag_registry_find(group, name);

    /* No annotation → skip when any filter is active. */
    if (!entry || !entry->tags)
        return 0;

    /* --any-tags filter (OR): at least one any_tag must appear in the test. */
    if (g_test_config.any_tag_count > 0) {
        int any_matched = 0;
        for (int fi = 0; fi < g_test_config.any_tag_count && !any_matched; fi++) {
            for (const char *const *tp = entry->tags; *tp != NULL; tp++) {
                if (strcmp(*tp, g_test_config.any_tags[fi]) == 0) {
                    any_matched = 1;
                    break;
                }
            }
        }
        if (!any_matched)
            return 0;
    }

    /* --all-tags filter (AND): every all_tag must appear in the test. */
    if (g_test_config.all_tag_count > 0) {
        for (int fi = 0; fi < g_test_config.all_tag_count; fi++) {
            int found = 0;
            for (const char *const *tp = entry->tags; *tp != NULL; tp++) {
                if (strcmp(*tp, g_test_config.all_tags[fi]) == 0) {
                    found = 1;
                    break;
                }
            }
            if (!found)
                return 0; /* this required tag is absent */
        }
    }

    return 1;
}

/* Track whether file output is active */
static int file_output_active = 0;

/* =========================================================================
 * _apply_logs_spec
 *
 * Translates the --logs <spec> CLI value into enable/disable calls on the
 * already-initialized log capture system.
 *
 * Must be called after log_capture_init() and before log_capture_start().
 *
 * Spec tokens:
 *   NULL / ""            no-op — registry defaults already applied by init
 *   "none"               disable every configured source
 *   "all"                enable every source in the registry
 *   "a,b,c"              disable all, then enable only the named sources
 *
 * Unknown names produce a warning that includes a reminder to use
 * --logs list (or log_capture_list_sources()) to see valid names.
 * ========================================================================= */
static void _apply_logs_spec(const char *spec)
{
    if (!spec || spec[0] == '\0')
        return; /* NULL or empty — keep registry defaults */

    /* Special token: disable everything */
    if (strcmp(spec, "none") == 0) {
        log_capture_disable_all_sources();
        printf("[log_capture] all sources disabled via --logs none\n");
        return;
    }

    /* Special token: enable every source in the registry */
    if (strcmp(spec, "all") == 0) {
        const log_source_descriptor_t *reg = log_capture_get_registry();
        for (const log_source_descriptor_t *d = reg; d->name != NULL; d++)
            log_capture_enable_source(d->name);
        printf("[log_capture] all sources enabled via --logs all\n");
        return;
    }

    /*
     * Comma-separated list of source names.
     * Strategy: disable everything first, then enable only the named ones.
     * This gives "--logs journalctl" the intuitive meaning of
     * "journalctl only" rather than "journalctl plus whatever was default".
     */
    log_capture_disable_all_sources();

    /*
     * Tokenise in-place using a local copy so we never modify the
     * original argv string.  Uses strchr() instead of strsep() to stay
     * within _POSIX_C_SOURCE=200112L (strsep is a BSD extension not
     * declared under strict POSIX).
     */
    char buf[256];
    strncpy(buf, spec, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *cur = buf;
    while (cur && *cur != '\0') {
        /* Find the next comma delimiter */
        char *comma = strchr(cur, ',');
        if (comma)
            *comma = '\0'; /* terminate this token in-place */

        char *token = cur;

        /* Trim leading spaces */
        while (*token == ' ')
            token++;
        /* Trim trailing spaces */
        char *end = token + strlen(token);
        while (end > token && *(end - 1) == ' ')
            *(--end) = '\0';

        if (*token != '\0') {
            if (log_capture_enable_source(token) == 0) {
                printf("[log_capture] enabled source: %s\n", token);
            } else {
                fprintf(stderr,
                        "[log_capture] Warning: unknown log source '%s' in "
                        "--logs spec (run with --logs list to see available "
                        "sources)\n",
                        token);
            }
        }

        /* Advance past the delimiter, or stop if we were at the last token */
        cur = comma ? comma + 1 : NULL;
    }
}

/* Track current test suite and statistics */
static struct {
    char current_suite[256];
    int suite_open;
    uint32_t suite_tests;
    uint32_t suite_failures;
    uint32_t suite_skipped;
    uint32_t last_test_count;
    struct timeval test_start_time;
} suite_state = {
    .suite_open = 0, .suite_tests = 0, .suite_failures = 0, .suite_skipped = 0, .last_test_count = 0
};

int UnityFixtureFileOutputBegin(const char *output_dir, const char *base_name)
{
    if (unity_allure_output_init(base_name) != 0) {
        return -1;
    }

    if (log_capture_init(output_dir, NULL) != 0) {
        fprintf(stderr, "Warning: Failed to initialize log capture\n");
        /* Continue anyway - log capture is optional */
    } else {
        /*
         * Apply the --logs <spec> value from the CLI (parsed earlier by
         * test_config_init into g_test_config.logs_spec).  This runs after
         * log_capture_init() so the live config is already populated from
         * the registry and enable/disable calls work correctly.
         *
         * Spec semantics:
         *   NULL / ""            no-op: registry defaults already applied
         *   "none"               disable every source
         *   "all"                enable every registered source
         *   "a,b"                disable all, then enable only a and b
         */
        _apply_logs_spec(g_test_config.logs_spec);
    }

    file_output_active = 1;
    suite_state.suite_open = 0;
    suite_state.suite_tests = 0;
    suite_state.suite_failures = 0;
    suite_state.suite_skipped = 0;
    suite_state.last_test_count = 0;

    print_suite_banner("FastRPC Tests");

    return 0;
}

int UnityFixtureFileOutputEnd(void)
{
    if (!file_output_active) {
        return 0;
    }

    if (log_capture_is_initialized()) {
        log_capture_cleanup();
    }

    if (suite_state.suite_open) {
        unity_allure_output_end_suite(suite_state.suite_tests, suite_state.suite_failures,
                                      suite_state.suite_skipped, 0.0);
        suite_state.suite_open = 0;
    }

    print_summary(Unity.NumberOfTests, Unity.TestFailures, Unity.TestIgnores);

    int result = unity_allure_output_close();

    file_output_active = 0;
    return result;
}

/**
 * is_test_selected
 *
 * Mirrors Unity's internal filter logic from unity_fixture.c so that
 * UnityTestRunnerWithFileOutput can determine — before calling
 * OriginalUnityTestRunner — whether a given (group, name) pair would
 * actually be executed under the current filter settings.
 *
 * @param group  Test group name (as passed to UnityTestRunner).
 * @param name   Test case name (as passed to UnityTestRunner).
 * @return       Non-zero if the test would be selected; zero if it would
 *               be skipped by the current filter.
 */
static int is_test_selected(const char *group, const char *name)
{
    /* --- group axis --- */
    int group_ok = 0;
    if (UnityFixture.GroupFilter == NULL && UnityFixture.Group == NULL) {
        group_ok = 1;
    } else {
        if (UnityFixture.GroupFilter && strstr(group, UnityFixture.GroupFilter))
            group_ok = 1;
        if (UnityFixture.Group && strcmp(group, UnityFixture.Group) == 0)
            group_ok = 1;
    }
    if (!group_ok)
        return 0;

    /* --- name axis --- */
    int name_ok = 0;
    if (UnityFixture.NameFilter == NULL && UnityFixture.Name == NULL) {
        name_ok = 1;
    } else {
        if (UnityFixture.NameFilter && strstr(name, UnityFixture.NameFilter))
            name_ok = 1;
        if (UnityFixture.Name && strcmp(name, UnityFixture.Name) == 0)
            name_ok = 1;
    }
    return name_ok;
}

void UnityTestRunnerWithFileOutput(unityfunction *setup, unityfunction *test_body,
                                   unityfunction *teardown, const char *printable_name,
                                   const char *group, const char *name, const char *file,
                                   unsigned int line)
{
    struct timeval start_time, end_time;
    double duration_ms;
    uint32_t failures_before, ignores_before;
    int test_passed, test_ignored;
    const char *status;
    const char *message = NULL;

    if (!file_output_active) {
        OriginalUnityTestRunner(setup, test_body, teardown, printable_name, group, name, file,
                                line);
        return;
    }

    /*
     * Guard: if the current filter settings would cause Unity to skip this
     * test entirely, delegate immediately without touching log capture,
     * Allure output, or suite-state bookkeeping.  This
     * prevents log files from being created for tests that were never
     * selected by -G / -g / -N / -n.
     *
     * The check mirrors Unity's own `testSelected() && groupSelected()`
     * logic in unity_fixture.c so the two are always in sync.
     */
    if (!is_test_selected(group, name)) {
        OriginalUnityTestRunner(setup, test_body, teardown, printable_name, group, name, file,
                                line);
        return;
    }

    /*
     * Tag-based inclusion filter.
     *
     * --any-tags A B  — run if the test's annotation contains A OR B.
     * --all-tags A B  — run only if the annotation contains A AND B.
     * --tags A        — backward-compatible alias for --any-tags A.
     * Both active     — test must pass both filters independently.
     *
     * Non-matching tests are silently counted as ignored so they appear in
     * the summary totals but produce no '!' output on the console.
     *
     * This check runs AFTER the Unity group/name filter so that -G / -g /
     * -N / -n still narrow the set further when combined with tag filters.
     */
    if (!unity_test_case_tag_filter_passes(group, name)) {
        /*
         * Count the test as ignored in Unity's global tallies so the
         * summary line ("N Tests N Failures N Ignored") stays accurate,
         * but emit nothing to stdout — no '!' spam.
         */
        Unity.NumberOfTests++;
        Unity.TestIgnores++;
        suite_state.suite_skipped++;
        return;
    }

    /* Check if we need to start a new suite */
    if (!suite_state.suite_open || strcmp(suite_state.current_suite, group) != 0) {
        if (suite_state.suite_open) {
            unity_allure_output_end_suite(suite_state.suite_tests, suite_state.suite_failures,
                                          suite_state.suite_skipped, 0.0);
        }

        strncpy(suite_state.current_suite, group, sizeof(suite_state.current_suite) - 1);
        suite_state.current_suite[sizeof(suite_state.current_suite) - 1] = '\0';
        unity_allure_output_start_suite(group);

        /* Inject Allure classification labels from TEST_GROUP_META registry */
        {
            const unity_group_labels_t *lbl = unity_label_registry_find(group);
            if (lbl) {
                unity_allure_output_add_suite_label("layer", lbl->layer);
                unity_allure_output_add_suite_label("epic", lbl->epic);
                unity_allure_output_add_suite_label("feature", lbl->feature);
                unity_allure_output_add_suite_label("story", lbl->story);
                unity_allure_output_add_suite_label("parentSuite", lbl->epic);
                unity_allure_output_add_suite_label("subSuite", lbl->feature);
            }
        }

        suite_state.suite_open = 1;
        suite_state.suite_tests = 0;
        suite_state.suite_failures = 0;
        suite_state.suite_skipped = 0;
    }

    test_utils_reset_last_error_code();
    test_utils_reset_accumulated_error_codes();
    test_label_override_clear(); /* ensure no stale override from a prior test */

    failures_before = Unity.TestFailures;
    ignores_before = Unity.TestIgnores;
    gettimeofday(&start_time, NULL);

    /* Print the per-test header box so the test name is always visible
     * before the body runs, even if the body crashes or hangs. */
    print_test_header(group, name);

    /* Start log capture for this test */
    if (log_capture_is_initialized()) {
        if (log_capture_start(printable_name, group) != 0) {
            fprintf(stderr, "Warning: Failed to start log capture for test %s\n", printable_name);
        }
    }

    /* Open the test-body attachment gate BEFORE running the test body.
     * Any attach_file() calls from within the test body (e.g. profiling
     * metrics) will be queued and flushed onto the correct result by
     * unity_allure_output_write_test() below. */
    unity_allure_output_begin_test();

    /* Run the test using the original Unity test runner.
     * Any output produced by the test body (e.g. [dspqueue-feature] lines)
     * will appear between the header box and the footer printed below. */
    OriginalUnityTestRunner(setup, test_body, teardown, printable_name, group, name, file, line);

    /* Guarantee a newline after the test body completes so the footer
     * always starts on a fresh line regardless of what the test printed. */
    putchar('\n');
    fflush(stdout);

    gettimeofday(&end_time, NULL);
    duration_ms = (end_time.tv_sec - start_time.tv_sec) * 1000.0
                  + (end_time.tv_usec - start_time.tv_usec) / 1000.0;

    test_ignored = (Unity.TestIgnores > ignores_before);
    test_passed = (Unity.TestFailures == failures_before) && !test_ignored;

    if (test_ignored) {
        status = "IGNORE";
        message = Unity.CurrentDetail1; /* Unity stores ignore message here */
    } else if (test_passed) {
        status = "PASS";
    } else {
        status = "FAIL";
        message = Unity.CurrentDetail1; /* Unity stores failure message here */
    }

    {
        int code_count = 0;
        const int *codes = test_utils_get_accumulated_error_codes(&code_count);

        /* Print the consolidated per-test footer (result, codes, location,
         * message) — this replaces the old inline REPORT_ERROR_CODE printf
         * and the bare PASS/FAIL line that Unity printed on its own. */
        print_test_footer(test_passed, test_ignored, Unity.TestFile,
                          (unsigned int)Unity.CurrentTestLineNumber, message, codes, code_count);
    }

    /* Record the test result — write_test sets tests_tail and flushes
     * any pending attachments queued by the test body. */
    unity_allure_output_write_test(group, printable_name, file, line, duration_ms, status, message);

    /* If TEST_LABELS() was called inside the test body, apply the per-test
     * label override to the result that was just recorded by write_test.
     * add_last_result_label targets the tail of the current suite's result
     * list, which is exactly the result we just appended. */
    if (s_test_label_override.active) {
        unity_allure_output_add_last_result_label("layer", s_test_label_override.layer);
        unity_allure_output_add_last_result_label("epic", s_test_label_override.epic);
        unity_allure_output_add_last_result_label("feature", s_test_label_override.feature);
        unity_allure_output_add_last_result_label("story", s_test_label_override.story);
        unity_allure_output_add_last_result_label("parentSuite", s_test_label_override.epic);
        unity_allure_output_add_last_result_label("subSuite", s_test_label_override.feature);
        test_label_override_clear();
    }

    /* Stop log capture AFTER write_test so tests_tail is already set.
     * log_capture_attach_to_allure() calls xml_test_writer_attach_file()
     * which finds a valid tests_tail and appends the log files directly —
     * no pending-queue involvement, no duplicate attachments. */
    if (log_capture_is_active()) {
        if (log_capture_stop() != 0) {
            fprintf(stderr, "Warning: Failed to stop log capture for test %s\n", printable_name);
        }
    }

    suite_state.suite_tests++;
    if (!test_passed && !test_ignored)
        suite_state.suite_failures++;
    if (test_ignored)
        suite_state.suite_skipped++;
}

int UnityMainWithFileOutput(int argc, const char *argv[], void (*run_all_tests)(void))
{
    int result;

    if (UnityFixtureFileOutputBegin(NULL, NULL) != 0) {
        fprintf(stderr, "Warning: Failed to initialize file output, continuing without it\n");
    }

    result = UnityMain(argc, argv, run_all_tests);

    UnityFixtureFileOutputEnd();

    return result;
}
