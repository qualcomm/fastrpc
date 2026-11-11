// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "../fastrpc_utils/test_config.h"
#include "../fastrpc_utils/test_utils.h"
#include "unity_fixture.h"           /* Include to get Unity types */
#include "unity_fixture_internals.h" /* For UnityFixture (GroupFilter/Group/NameFilter/Name) */
#include "unity_internals.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#ifndef SIZE_MAX
#define SIZE_MAX ((size_t)-1)
#endif

#define DOMAIN_OUTPUT_SEPARATOR "============================================================"
#define TEST_INDENT "  "
#define DETAIL_INDENT "    "
#define UNKNOWN_TEST_PART "(unknown)"

static const char *shorten_path(const char *path);

static const char *test_identity_part(const char *value)
{
    return value ? value : UNKNOWN_TEST_PART;
}

static void print_test_header(const char *group, const char *name)
{
    printf(TEST_INDENT "TEST %s.%s\n", test_identity_part(group), test_identity_part(name));
}

static void print_test_footer(int passed, int ignored, const char *file, unsigned int fail_line,
                              const char *message, const int *codes, int code_count,
                              int codes_truncated)
{
    int i;

    if (ignored)
        printf(DETAIL_INDENT "Result: SKIP\n");
    else if (passed)
        printf(DETAIL_INDENT "Result: PASS\n");
    else
        printf(DETAIL_INDENT "Result: FAIL\n");

    if (code_count > 0) {
        printf(DETAIL_INDENT "Error codes:\n");
        for (i = 0; i < code_count; i++)
            printf(DETAIL_INDENT "  [%d] 0x%08x\n", i, (unsigned int)codes[i]);
        if (codes_truncated)
            printf(DETAIL_INDENT "  [..] additional error codes truncated\n");
    } else if (codes_truncated) {
        printf(DETAIL_INDENT "Error codes: truncated\n");
    }

    if (!passed && !ignored) {
        if (file && file[0] != '\0')
            printf(DETAIL_INDENT "Location: %s:%u\n", shorten_path(file), fail_line);
        if (message && message[0] != '\0')
            printf(DETAIL_INDENT "Message: %s\n", message);
    }

    printf("\n");
    fflush(stdout);
}

typedef struct unity_test_outcome {
    int passed;
    int ignored;
    const char *file;
    unsigned int failure_line;
    const char *message;
    const int *error_codes;
    int error_count;
    int error_codes_truncated;
} unity_test_outcome_t;

static void print_test_outcome(const unity_test_outcome_t *outcome)
{
    print_test_footer(outcome->passed, outcome->ignored, outcome->file,
                      outcome->failure_line, outcome->message, outcome->error_codes,
                      outcome->error_count, outcome->error_codes_truncated);
}

static unsigned int count_passed(unsigned int total, unsigned int failures,
                                 unsigned int ignored)
{
    if (failures > total || ignored > total - failures)
        return 0;
    return total - failures - ignored;
}

static void print_summary(const char *title, const char *domain_name, int domain_id,
                          unsigned int total, unsigned int failures, unsigned int ignored)
{
    unsigned int passed = count_passed(total, failures, ignored);

    printf("\n%s\n", title);
    if (domain_name) {
        printf("Name: %s\n", domain_name);
        printf("ID: %d\n", domain_id);
    }
    printf("Tests: %u\n", total);
    printf("Passed: %u\n", passed);
    printf("Failed: %u\n", failures);
    printf("Skipped: %u\n", ignored);
    printf("Result: %s\n", failures == 0 ? "PASS" : "FAIL");
    fflush(stdout);
}

static void print_overall_summary(unsigned int total, unsigned int failures,
                                  unsigned int ignored)
{
    print_summary("OVERALL SUMMARY", NULL, 0, total, failures, ignored);
}

void unity_test_case_print_domain_begin(const char *domain_name, int domain_id)
{
    printf("\nBEGIN DOMAIN\n");
    printf("Name: %s\n", domain_name ? domain_name : "<invalid>");
    printf("ID: %d\n\n", domain_id);
    fflush(stdout);
}

void unity_test_case_print_domain_summary(const char *domain_name, int domain_id,
                                          unsigned int total, unsigned int failures,
                                          unsigned int ignored)
{
    print_summary("DOMAIN SUMMARY", domain_name, domain_id, total, failures, ignored);
}

void unity_test_case_print_domain_end(void)
{
    printf("\nEND DOMAIN\n%s\n", DOMAIN_OUTPUT_SEPARATOR);
    fflush(stdout);
}


/* Unity uses this callback for framework-owned output. */
#define UNITY_DIAGNOSTIC_BUF_SIZE 2048

static int suppress_unity_output;
static int capture_unity_output;
static char diagnostic_buf[UNITY_DIAGNOSTIC_BUF_SIZE];
static size_t diagnostic_len;
static int runner_active;
static int s_show_pass_results;
static unsigned int s_summary_total;
static unsigned int s_summary_failures;
static unsigned int s_summary_ignored;

void fastrpc_unity_output_char(int character)
{
    if (capture_unity_output && diagnostic_len < sizeof(diagnostic_buf) - 1)
        diagnostic_buf[diagnostic_len++] = (char)character;
    if (!suppress_unity_output)
        (void)putchar(character);
}

/* Strips everything up to and including "test/base_test/" for display.
 * Falls back to the last two path components for out-of-tree builds. */
static const char *shorten_path(const char *path)
{
    const char *marker;
    const char *last_slash;
    const char *prev;

    if (!path || path[0] == '\0')
        return "(unknown)";

    marker = strstr(path, "test/base_test/");
    if (marker)
        return marker + strlen("test/base_test/");

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

static int extract_unity_diagnostic(const char *file, unsigned int *out_line,
                                    const char **out_message)
{
    static char message_buf[UNITY_DIAGNOSTIC_BUF_SIZE];
    size_t file_len;
    const char *prefix_pos, *line_start, *after_line, *fail_kw, *ignore_kw;
    const char *keyword, *msg_start, *line_end;
    char *endptr;
    unsigned long line_value;
    size_t msg_len;

    if (!file || file[0] == '\0' || diagnostic_len == 0)
        return 0;

    diagnostic_buf[diagnostic_len] = '\0';
    file_len = strlen(file);
    prefix_pos = strstr(diagnostic_buf, file);
    if (!prefix_pos || prefix_pos[file_len] != ':')
        return 0;

    line_start = prefix_pos + file_len + 1;
    line_value = strtoul(line_start, &endptr, 10);
    if (endptr == line_start || *endptr != ':' || line_value > UINT_MAX)
        return 0;
    after_line = endptr;

    fail_kw = strstr(after_line, ":FAIL:");
    ignore_kw = strstr(after_line, ":IGNORE:");
    keyword = fail_kw && (!ignore_kw || fail_kw < ignore_kw) ? fail_kw : ignore_kw;
    if (!keyword)
        return 0;

    msg_start = keyword + (keyword == fail_kw ? strlen(":FAIL:") : strlen(":IGNORE:"));
    if (keyword == fail_kw && *msg_start == ':')
        msg_start++;
    if (*msg_start == ' ')
        msg_start++;
    line_end = strchr(msg_start, '\n');
    if (!line_end)
        line_end = msg_start + strlen(msg_start);

    msg_len = (size_t)(line_end - msg_start);
    if (msg_len >= sizeof(message_buf))
        msg_len = sizeof(message_buf) - 1;
    memcpy(message_buf, msg_start, msg_len);
    message_buf[msg_len] = 0;

    *out_line = (unsigned int)line_value;
    *out_message = message_buf[0] != 0 ? message_buf : NULL;
    return 1;
}

static void print_suite_banner(const char *suite_name)
{
    const char *title = (suite_name && suite_name[0] != '\0') ? suite_name : "FastRPC Test Suite";

    printf("%s\n\n", title);
    fflush(stdout);
}

/* Declare the original UnityTestRunner function from unity_fixture.c */
void UnityTestRunner(unityfunction *setup, unityfunction *test_body, unityfunction *teardown,
                     const char *printable_name, const char *group, const char *name,
                     const char *file, unsigned int line);

/* Save pointer to original UnityTestRunner */
typedef void (*unity_test_runner_fn)(unityfunction *, unityfunction *, unityfunction *,
                                     const char *, const char *, const char *, const char *,
                                     unsigned int);
static unity_test_runner_fn original_unity_test_runner = UnityTestRunner;

static void invoke_original_unity_test_runner(
    unityfunction *setup, unityfunction *test_body, unityfunction *teardown,
    const char *printable_name, const char *group, const char *name, const char *file,
    unsigned int line)
{
    original_unity_test_runner(setup, test_body, teardown, printable_name, group, name, file,
                               line);
}

/* Now include our header which redefines UnityTestRunner macro */
#include "base_test_runner.h"

/* =========================================================================
 * Unified test metadata registry — dynamically sized sorted vector
 *
 * TEST_CASE_METADATA() emits constructors that register descriptors at program
 * startup. Entries are kept in a vector and sorted lazily by (group, name), so
 * runtime lookups use binary search instead of walking a linked list. The
 * registry is queried by filtering, listing, and domain-support code.
 *
 * Filtering semantics:
 *   --any-tags A B  — run if the test's tags contain A OR B  (OR filter).
 *   --all-tags A B  — run only if the test's tags contain A AND B (AND filter).
 *   Both active     — test must pass BOTH filters independently.
 *   Neither active  — every test runs (filter inactive).
 * ========================================================================= */

typedef struct unity_test_case_dispatch {
    char *group;
    char *name;
} unity_test_case_dispatch_t;

static unity_test_case_metadata_t **s_test_case_metadata_registry;
static size_t s_test_case_metadata_count;
static size_t s_test_case_metadata_capacity;
static int s_test_case_metadata_sorted = 1;
static int s_test_case_metadata_invalid;

static unity_test_case_dispatch_t *s_test_case_dispatch;
static size_t s_test_case_dispatch_count;
static size_t s_test_case_dispatch_capacity;
static int s_test_case_dispatch_sorted = 1;
static int s_test_case_dispatch_invalid;

static int is_test_selected(const char *group, const char *name);
static int test_passes_unity_and_tags(const char *group, const char *name);
static int test_passes_listing_filters(const unity_test_case_metadata_t *entry);
static void print_metadata_skip(const char *group, const char *name);

static int metadata_entry_compare(const void *left, const void *right)
{
    const unity_test_case_metadata_t *const *left_entry = left;
    const unity_test_case_metadata_t *const *right_entry = right;
    int group_result = strcmp((*left_entry)->group, (*right_entry)->group);

    if (group_result != 0)
        return group_result;
    return strcmp((*left_entry)->name, (*right_entry)->name);
}

static void metadata_prepare(void)
{
    if (!s_test_case_metadata_sorted && s_test_case_metadata_count > 1) {
        qsort(s_test_case_metadata_registry, s_test_case_metadata_count,
              sizeof(*s_test_case_metadata_registry), metadata_entry_compare);
        s_test_case_metadata_sorted = 1;
    }
}

static ssize_t metadata_find_index(const char *group, const char *name)
{
    size_t low = 0;
    size_t high;

    if (!group || !name)
        return -1;
    metadata_prepare();
    high = s_test_case_metadata_count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        unity_test_case_metadata_t *entry = s_test_case_metadata_registry[middle];
        int result = strcmp(group, entry->group);

        if (result == 0)
            result = strcmp(name, entry->name);
        if (result == 0)
            return (ssize_t)middle;
        if (result < 0)
            high = middle;
        else
            low = middle + 1;
    }
    return -1;
}

static unity_test_case_metadata_t *metadata_find_mutable(const char *group, const char *name)
{
    ssize_t index = metadata_find_index(group, name);

    return index < 0 ? NULL : s_test_case_metadata_registry[index];
}

static int metadata_reserve(size_t needed)
{
    size_t capacity;
    unity_test_case_metadata_t **entries;

    if (needed <= s_test_case_metadata_capacity)
        return 0;
    if (s_test_case_metadata_capacity == 0)
        capacity = 16;
    else if (s_test_case_metadata_capacity > SIZE_MAX / 2)
        capacity = needed;
    else
        capacity = s_test_case_metadata_capacity * 2;
    if (capacity < needed)
        capacity = needed;
    if (capacity < needed || capacity > SIZE_MAX / sizeof(*entries))
        return -1;
    entries = realloc(s_test_case_metadata_registry, capacity * sizeof(*entries));
    if (!entries)
        return -1;
    s_test_case_metadata_registry = entries;
    s_test_case_metadata_capacity = capacity;
    return 0;
}

static int metadata_validate_entry(const unity_test_case_metadata_t *entry)
{
    if (!entry || !entry->group || !entry->name || entry->group[0] == '\0' ||
        entry->name[0] == '\0')
        return 0;

    if (entry->has_tags) {
        if (!entry->tags || entry->tag_count == 0)
            return 0;
        for (size_t i = 0; i < entry->tag_count; i++) {
            if (!entry->tags[i] || entry->tags[i][0] == '\0')
                return 0;
            for (size_t j = i + 1; j < entry->tag_count; j++) {
                if (strcmp(entry->tags[i], entry->tags[j]) == 0)
                    return 0;
            }
        }
    } else if (entry->tags || entry->tag_count != 0) {
        return 0;
    }

    if (entry->has_domains) {
        if (!entry->domains || entry->domain_count == 0)
            return 0;
        for (size_t i = 0; i < entry->domain_count; i++) {
            if (!test_utils_domain_name_for(entry->domains[i]))
                return 0;
            for (size_t j = i + 1; j < entry->domain_count; j++) {
                if (entry->domains[i] == entry->domains[j])
                    return 0;
            }
        }
    } else if (entry->domains || entry->domain_count != 0) {
        return 0;
    }

    return 1;
}

static void metadata_report_error(const unity_test_case_metadata_t *entry,
                                  const char *field)
{
    fprintf(stderr, "[metadata] conflicting or invalid %s for %s.%s\n",
            field, entry && entry->group ? entry->group : "<null>",
            entry && entry->name ? entry->name : "<null>");
    s_test_case_metadata_invalid = 1;
}

static void metadata_mark_invalid(unity_test_case_metadata_t *entry, const char *field)
{
    if (entry)
        entry->invalid = 1;
    metadata_report_error(entry, field);
}

static void metadata_register_fields(unity_test_case_metadata_t *entry)
{
    if (metadata_find_mutable(entry->group, entry->name) != NULL) {
        metadata_mark_invalid(entry, "duplicate descriptor");
        return;
    }
    if (metadata_reserve(s_test_case_metadata_count + 1) != 0) {
        metadata_mark_invalid(entry, "registry allocation");
        return;
    }
    s_test_case_metadata_registry[s_test_case_metadata_count++] = entry;
    s_test_case_metadata_sorted = 0;
}

void unity_test_case_metadata_register(unity_test_case_metadata_t *entry)
{
    if (!metadata_validate_entry(entry)) {
        metadata_mark_invalid(entry, "metadata");
        return;
    }
    metadata_register_fields(entry);
}

const unity_test_case_metadata_t *unity_test_case_metadata_find(const char *group,
                                                                 const char *name)
{
    return metadata_find_mutable(group, name);
}

int unity_test_case_metadata_validate(void)
{
    return s_test_case_metadata_invalid ? -1 : 0;
}

static int dispatch_entry_compare(const void *left, const void *right)
{
    const unity_test_case_dispatch_t *left_entry = left;
    const unity_test_case_dispatch_t *right_entry = right;
    int group_result = strcmp(left_entry->group, right_entry->group);

    if (group_result != 0)
        return group_result;
    return strcmp(left_entry->name, right_entry->name);
}

static void dispatch_prepare(void)
{
    if (!s_test_case_dispatch_sorted && s_test_case_dispatch_count > 1) {
        qsort(s_test_case_dispatch, s_test_case_dispatch_count,
              sizeof(*s_test_case_dispatch), dispatch_entry_compare);
        s_test_case_dispatch_sorted = 1;
    }
}

static ssize_t dispatch_find_index(const char *group, const char *name)
{
    size_t low = 0;
    size_t high;

    if (!group || !name)
        return -1;
    dispatch_prepare();
    high = s_test_case_dispatch_count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        unity_test_case_dispatch_t *entry = &s_test_case_dispatch[middle];
        int result = strcmp(group, entry->group);

        if (result == 0)
            result = strcmp(name, entry->name);
        if (result == 0)
            return (ssize_t)middle;
        if (result < 0)
            high = middle;
        else
            low = middle + 1;
    }
    return -1;
}

static unity_test_case_dispatch_t *dispatch_find(const char *group, const char *name)
{
    ssize_t index = dispatch_find_index(group, name);

    return index < 0 ? NULL : &s_test_case_dispatch[index];
}

static int dispatch_reserve(size_t needed)
{
    size_t capacity;
    unity_test_case_dispatch_t *entries;

    if (needed <= s_test_case_dispatch_capacity)
        return 0;
    if (s_test_case_dispatch_capacity == 0)
        capacity = 16;
    else if (s_test_case_dispatch_capacity > SIZE_MAX / 2)
        capacity = needed;
    else
        capacity = s_test_case_dispatch_capacity * 2;
    if (capacity < needed)
        capacity = needed;
    if (capacity < needed || capacity > SIZE_MAX / sizeof(*entries))
        return -1;
    entries = realloc(s_test_case_dispatch, capacity * sizeof(*entries));
    if (!entries)
        return -1;
    s_test_case_dispatch = entries;
    s_test_case_dispatch_capacity = capacity;
    return 0;
}

static char *dispatch_strdup(const char *value)
{
    size_t length;
    char *copy;

    length = strlen(value) + 1;
    copy = malloc(length);
    if (copy)
        memcpy(copy, value, length);
    return copy;
}

void unity_test_case_dispatch_observe(const char *group, const char *name)
{
    unity_test_case_dispatch_t *entry;

    if (!group || !name)
        return;
    if (dispatch_find(group, name)) {
        fprintf(stderr, "[metadata] test %s.%s was dispatched more than once\n",
                group, name);
        s_test_case_dispatch_invalid = 1;
        return;
    }
    if (dispatch_reserve(s_test_case_dispatch_count + 1) != 0) {
        s_test_case_dispatch_invalid = 1;
        return;
    }

    entry = &s_test_case_dispatch[s_test_case_dispatch_count];
    entry->group = dispatch_strdup(group);
    entry->name = dispatch_strdup(name);
    if (!entry->group || !entry->name) {
        free(entry->group);
        free(entry->name);
        entry->group = NULL;
        entry->name = NULL;
        s_test_case_dispatch_invalid = 1;
        return;
    }
    s_test_case_dispatch_count++;
    s_test_case_dispatch_sorted = 0;
}

void unity_test_case_dispatch_reset(void)
{
    for (size_t i = 0; i < s_test_case_dispatch_count; i++) {
        free(s_test_case_dispatch[i].group);
        free(s_test_case_dispatch[i].name);
    }
    free(s_test_case_dispatch);
    s_test_case_dispatch = NULL;
    s_test_case_dispatch_count = 0;
    s_test_case_dispatch_capacity = 0;
    s_test_case_dispatch_sorted = 1;
    s_test_case_dispatch_invalid = 0;
}

int unity_test_case_dispatch_validate(void)
{
    int invalid = s_test_case_dispatch_invalid;

    dispatch_prepare();
    for (size_t i = 0; i < s_test_case_dispatch_count; i++) {
        unity_test_case_dispatch_t *dispatch = &s_test_case_dispatch[i];
        if (!unity_test_case_metadata_find(dispatch->group, dispatch->name)) {
            fprintf(stderr, "[metadata] dispatched test %s.%s has no metadata\n",
                    dispatch->group, dispatch->name);
            invalid = 1;
        }
    }
    metadata_prepare();
    for (size_t i = 0; i < s_test_case_metadata_count; i++) {
        unity_test_case_metadata_t *entry = s_test_case_metadata_registry[i];
        if (!entry->invalid && !dispatch_find(entry->group, entry->name) &&
            unity_test_case_selected(entry->group, entry->name)) {
            fprintf(stderr, "[metadata] selected test %s.%s was not dispatched\n",
                    entry->group, entry->name);
            invalid = 1;
        }
    }
    return invalid ? -1 : 0;
}

void unity_test_case_ignore(const char *printable_name, const char *group,
                           const char *name)
{
    if (!is_test_selected(group, name) || !unity_test_case_tag_filter_passes(group, name))
        return;
    if (!unity_test_case_domain_supported(group, name, g_test_config.domain_id)) {
        Unity.NumberOfTests++;
        Unity.TestIgnores++;
        print_metadata_skip(group, name);
        return;
    }

    suppress_unity_output = 1;
    UnityIgnoreTest(printable_name, group, name);
    suppress_unity_output = 0;
    if (runner_active) {
        print_test_header(group, name);
        printf(DETAIL_INDENT "Result: SKIP\n");
        printf(DETAIL_INDENT "Reason: test is explicitly ignored\n\n");
        fflush(stdout);
    }
}

static void print_metadata_skip(const char *group, const char *name)
{
    print_test_header(group, name);
    printf(DETAIL_INDENT "Result: SKIP\n");
    if (!unity_test_case_tag_filter_passes(group, name))
        printf(DETAIL_INDENT "Reason: test does not match the selected tag filters\n");
    else if (!unity_test_case_domain_supported(group, name, g_test_config.domain_id))
        printf(DETAIL_INDENT "Reason: test is not supported by the selected domain\n");
    else
        printf(DETAIL_INDENT "Reason: test is not selected\n");
    printf("\n");
    fflush(stdout);
}

int unity_test_case_domain_supported(const char *group, const char *name, int domain_id)
{
    const unity_test_case_metadata_t *entry = unity_test_case_metadata_find(group, name);

    if (!entry || entry->invalid || !entry->has_domains)
        return entry && entry->invalid ? 0 : 1;

    if (!entry->domains || entry->domain_count == 0)
        return 0;
    for (size_t i = 0; i < entry->domain_count; i++) {
        if (entry->domains[i] == domain_id)
            return 1;
    }
    return 0;
}

/**
 * unity_test_case_tag_filter_passes
 *
 * Returns 1 (run) when all active filters pass:
 *
 *   --any-tags filter (OR):
 *     Passes when any_tag_count == 0 (inactive), OR when the test's
 *     canonical metadata tag list contains at least one of the any_tags[] values.
 *
 *   --all-tags filter (AND):
 *     Passes when all_tag_count == 0 (inactive), OR when the test's
 *     canonical metadata tag list contains every one of the all_tags[] values.
 *
 * Returns 0 (skip) when:
 *   - Either active filter fails, OR
 *   - Any filter is active AND the test has no metadata tag annotation.
 *
 * Tags are compared case-sensitively (strcmp).
 */
int unity_test_case_tag_filter_passes(const char *group, const char *name)
{
    /* Both filters inactive — every test runs. */
    if (g_test_config.any_tag_count == 0 && g_test_config.all_tag_count == 0)
        return 1;

    const unity_test_case_metadata_t *entry = unity_test_case_metadata_find(group, name);

    /* No annotation → skip when any filter is active. */
    if (!entry || entry->invalid || !entry->has_tags || !entry->tags)
        return 0;

    /* --any-tags filter (OR): at least one any_tag must appear in the test. */
    if (g_test_config.any_tag_count > 0) {
        int any_matched = 0;
        for (int fi = 0; fi < g_test_config.any_tag_count && !any_matched; fi++) {
            for (size_t ti = 0; ti < entry->tag_count; ti++) {
                if (strcmp(entry->tags[ti], g_test_config.any_tags[fi]) == 0) {
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
            for (size_t ti = 0; ti < entry->tag_count; ti++) {
                if (strcmp(entry->tags[ti], g_test_config.all_tags[fi]) == 0) {
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

int unity_test_case_selected(const char *group, const char *name)
{
    return test_passes_unity_and_tags(group, name) &&
           unity_test_case_domain_supported(group, name, g_test_config.domain_id);
}

static int test_passes_unity_and_tags(const char *group, const char *name)
{
    return is_test_selected(group, name) && unity_test_case_tag_filter_passes(group, name);
}

void unity_test_case_set_summary(unsigned int total, unsigned int failures,
                                 unsigned int ignored)
{
    s_summary_total = total;
    s_summary_failures = failures;
    s_summary_ignored = ignored;
}

int BaseTestRunnerBegin(void)
{
    runner_active = 1;
    print_suite_banner("FastRPC Tests");
    return 0;
}

int BaseTestRunnerEnd(void)
{
    if (!runner_active) {
        return 0;
    }

    print_overall_summary(s_summary_total, s_summary_failures, s_summary_ignored);

    runner_active = 0;
    return 0;
}

/* Use Unity's own filter path; UnityIgnoreTest only changes counters when selected. */
static int is_test_selected(const char *group, const char *name)
{
    UNITY_COUNTER_TYPE tests_before = Unity.NumberOfTests;
    UNITY_COUNTER_TYPE ignores_before = Unity.TestIgnores;
    int saved_suppress = suppress_unity_output;
    int saved_verbose = UnityFixture.Verbose;
    int saved_silent = UnityFixture.Silent;
    int selected;

    suppress_unity_output = 1;
    UnityFixture.Verbose = 0;
    UnityFixture.Silent = 1;
    UnityIgnoreTest("(selection probe)", group, name);
    selected = Unity.NumberOfTests != tests_before;
    Unity.NumberOfTests = tests_before;
    Unity.TestIgnores = ignores_before;
    UnityFixture.Silent = saved_silent;
    UnityFixture.Verbose = saved_verbose;
    suppress_unity_output = saved_suppress;
    return selected;
}

static int metadata_matches_listing_domains(const unity_test_case_metadata_t *entry)
{
    if (g_test_config.domain_count == 0)
        return 1;

    for (int i = 0; i < g_test_config.domain_count; i++) {
        if (unity_test_case_domain_supported(entry->group, entry->name,
                                             g_test_config.domain_ids[i]))
            return 1;
    }
    return 0;
}

static int test_passes_listing_filters(const unity_test_case_metadata_t *entry)
{
    return entry && !entry->invalid && test_passes_unity_and_tags(entry->group, entry->name) &&
           metadata_matches_listing_domains(entry);
}

static unity_test_case_metadata_t **get_selected_registry_entries(size_t *selected_count)
{
    unity_test_case_metadata_t **entries;
    size_t count = 0;
    size_t index = 0;

    if (!selected_count)
        return NULL;

    metadata_prepare();
    for (size_t i = 0; i < s_test_case_metadata_count; i++) {
        unity_test_case_metadata_t *entry = s_test_case_metadata_registry[i];
        if (entry->invalid)
            continue;
        if (test_passes_listing_filters(entry))
            count++;
    }

    *selected_count = count;
    if (count == 0)
        return NULL;

    entries = malloc(count * sizeof(*entries));
    if (!entries) {
        fprintf(stderr, "Failed to allocate test listing metadata\n");
        return NULL;
    }

    for (size_t i = 0; i < s_test_case_metadata_count; i++) {
        unity_test_case_metadata_t *entry = s_test_case_metadata_registry[i];
        if (test_passes_listing_filters(entry))
            entries[index++] = entry;
    }

    return entries;
}

static int metadata_entry_has_tags(const unity_test_case_metadata_t *entry)
{
    return entry && entry->has_tags && entry->tags && entry->tag_count != 0;
}

static int metadata_tag_seen(const char *const *tags, size_t count, const char *candidate)
{
    for (size_t i = 0; i < count; i++) {
        if (strcmp(tags[i], candidate) == 0)
            return 1;
    }
    return 0;
}

static int metadata_tag_compare(const void *left, const void *right)
{
    const char *const *left_tag = left;
    const char *const *right_tag = right;

    return strcmp(*left_tag, *right_tag);
}

static int collect_registry_tags(const unity_test_case_metadata_t *entry,
                                 const char **tags, size_t capacity, size_t *count)
{
    if (!metadata_entry_has_tags(entry))
        return 0;

    for (size_t i = 0; i < entry->tag_count; i++) {
        if (!metadata_tag_seen(tags, *count, entry->tags[i])) {
            if (*count == capacity)
                return -1;
            tags[(*count)++] = entry->tags[i];
        }
    }
    return 0;
}

int unity_test_case_registry_print_tests(void)
{
    unity_test_case_metadata_t **entries;
    size_t count;

    entries = get_selected_registry_entries(&count);
    if (!entries && count != 0)
        return -1;

    for (size_t i = 0; i < count; i++)
        printf("%s.%s\n", entries[i]->group, entries[i]->name);

    free(entries);
    return 0;
}

int unity_test_case_registry_print_groups(void)
{
    unity_test_case_metadata_t **entries;
    const char *previous_group = NULL;
    size_t count;

    entries = get_selected_registry_entries(&count);
    if (!entries && count != 0)
        return -1;

    for (size_t i = 0; i < count; i++) {
        if (!previous_group || strcmp(previous_group, entries[i]->group) != 0) {
            printf("%s\n", entries[i]->group);
            previous_group = entries[i]->group;
        }
    }

    free(entries);
    return 0;
}

int unity_test_case_registry_print_tags(void)
{
    const char **tags;
    size_t capacity = 0;
    size_t count = 0;

    metadata_prepare();
    for (size_t i = 0; i < s_test_case_metadata_count; i++) {
        const unity_test_case_metadata_t *entry = s_test_case_metadata_registry[i];
        if (!entry->invalid && metadata_entry_has_tags(entry))
            capacity += entry->tag_count;
    }

    if (capacity == 0)
        return 0;

    tags = malloc(capacity * sizeof(*tags));
    if (!tags) {
        fprintf(stderr, "Failed to allocate tag listing metadata\n");
        return -1;
    }

    for (size_t i = 0; i < s_test_case_metadata_count; i++) {
        const unity_test_case_metadata_t *entry = s_test_case_metadata_registry[i];
        if (entry->invalid)
            continue;
        if (collect_registry_tags(entry, tags, capacity, &count) != 0) {
            fprintf(stderr, "Failed to collect tag listing metadata\n");
            free(tags);
            return -1;
        }
    }

    qsort(tags, count, sizeof(*tags), metadata_tag_compare);
    for (size_t i = 0; i < count; i++) {
        if (i == 0 || strcmp(tags[i - 1], tags[i]) != 0)
            printf("%s\n", tags[i]);
    }

    free(tags);
    return 0;
}

void BaseTestRunnerRunTest(unityfunction *setup, unityfunction *test_body,
                           unityfunction *teardown, const char *printable_name,
                           const char *group, const char *name, const char *file,
                           unsigned int line)
{
    uint32_t failures_before, ignores_before;
    unsigned int failure_line = 0;
    int test_passed, test_ignored, diagnostic_found;
    const char *message = NULL;

    if (!runner_active) {
        invoke_original_unity_test_runner(setup, test_body, teardown, printable_name, group, name,
                                           file, line);
        return;
    }

    if (!is_test_selected(group, name)) {
        invoke_original_unity_test_runner(setup, test_body, teardown, printable_name, group, name,
                                           file, line);
        return;
    }

    if (!unity_test_case_tag_filter_passes(group, name))
        return;
    if (!unity_test_case_domain_supported(group, name, g_test_config.domain_id)) {
        Unity.NumberOfTests++;
        Unity.TestIgnores++;
        print_metadata_skip(group, name);
        return;
    }

    /* Metadata and Unity filters are checked separately so tag exclusions do
     * not inflate the executed-test or skipped-test counters. */
    test_utils_reset_last_error_code();
    test_utils_reset_accumulated_error_codes();
    failures_before = Unity.TestFailures;
    ignores_before = Unity.TestIgnores;

    diagnostic_len = 0;
    capture_unity_output = 1;
    suppress_unity_output = 1;
    invoke_original_unity_test_runner(setup, test_body, teardown, printable_name, group, name,
                                      file, line);
    suppress_unity_output = 0;
    capture_unity_output = 0;
    fflush(stdout);
    test_ignored = Unity.TestIgnores > ignores_before;
    test_passed = Unity.TestFailures == failures_before && !test_ignored;
    diagnostic_found = (test_ignored || !test_passed) &&
                       extract_unity_diagnostic(file, &failure_line, &message);
    {
        int code_count = 0;
        const int *codes = test_utils_get_accumulated_error_codes(&code_count);
        unity_test_outcome_t outcome = {
            .passed = test_passed,
            .ignored = test_ignored,
            .file = diagnostic_found ? file : NULL,
            .failure_line = failure_line,
            .message = diagnostic_found ? message : NULL,
            .error_codes = codes,
            .error_count = code_count,
            .error_codes_truncated = test_utils_error_codes_truncated(),
        };
        if (!test_passed || test_ignored || s_show_pass_results) {
            print_test_header(group, name);
            print_test_outcome(&outcome);
        }
    }
}

int BaseTestRunnerMain(int argc, const char *argv[], void (*run_all_tests)(void))
{
    unsigned int total = 0;
    unsigned int failures = 0;
    unsigned int ignored = 0;
    unsigned int repeat;
    int result;

    result = UnityGetCommandLineOptions(argc, argv);
    if (result != 0)
        return result;

    s_show_pass_results = UnityFixture.Verbose && !UnityFixture.Silent &&
                          !g_test_config.silent_mode;
    UnityFixture.Verbose = 0;
    UnityFixture.Silent = 1;

    if (UnityFixture.RepeatCount == 0) {
        fprintf(stderr, "[unity] -r requires a repeat count greater than zero\n");
        UnityBegin(argv[0]);
        Unity.NumberOfTests = 1;
        Unity.TestFailures = 1;
        UnityEnd();
        return 1;
    }

    for (repeat = 0; repeat < UnityFixture.RepeatCount; repeat++) {
        int dispatch_invalid;

        unity_test_case_dispatch_reset();
        UnityBegin(argv[0]);
        run_all_tests();
        dispatch_invalid = unity_test_case_dispatch_validate();
        if (dispatch_invalid != 0)
            Unity.TestFailures++;
        total += Unity.NumberOfTests;
        failures += Unity.TestFailures;
        ignored += Unity.TestIgnores;
        /* Close each Unity lifecycle while keeping the aggregate summary custom. */
        suppress_unity_output = 1;
        (void)UnityEnd();
        suppress_unity_output = 0;
        unity_test_case_set_summary(total, failures, ignored);
    }

    Unity.NumberOfTests = total;
    Unity.TestFailures = failures;
    Unity.TestIgnores = ignored;
    unity_test_case_set_summary(total, failures, ignored);
    return failures == 0 ? 0 : 1;
}
