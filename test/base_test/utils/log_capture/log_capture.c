// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "log_capture.h"
#include "../reporting/allure/unity_allure_output.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* =========================================================================
 * Source Registry
 *
 * This table is the single extension point for adding new log sources.
 *
 * Each row describes one source completely:
 *   name               - identifier used in --logs CLI flag and the
 *                        enable/disable API (must be unique, no spaces)
 *   file_suffix        - embedded in the output filename so each source's
 *                        log is clearly labelled
 *   sh_command         - passed verbatim to: sh -c '<sh_command>'
 *                        Use one-shot commands (dmesg, journalctl snapshot)
 *                        where possible; streaming commands that never exit
 *                        on their own will be killed by stop_log_process()
 *   enabled_by_default - true  => active unless explicitly disabled
 *                        false => inactive unless explicitly enabled
 *   description        - shown by log_capture_list_sources() / --help
 *
 * To add a new source: append one row before the sentinel {NULL,...}.
 * No other file needs to change.  The new source is immediately available
 * via log_capture_enable_source("<name>") and --logs <name>.
 * ========================================================================= */
static const log_source_descriptor_t s_source_registry[] = {
    {
        .name = "journalctl",
        .file_suffix = "journalctl",
        .sh_command = "journalctl --no-pager 2>&1",
        .enabled_by_default = true,
        .description = "systemd journal (one-shot snapshot)",
    },
    {
        .name = "dmesg",
        .file_suffix = "dmesg",
        .sh_command = "dmesg 2>&1",
        .enabled_by_default = true,
        .description = "kernel ring buffer (one-shot snapshot)",
    },
    {
        .name = "logcat",
        .file_suffix = "logcat",
        .sh_command = "logcat -d -v time 2>&1",
        .enabled_by_default = false,
        .description = "Android logcat (one-shot dump, Android only)",
    },
    /* ---- sentinel: marks end of table ---- */
    { NULL, NULL, NULL, false, NULL },
};

/* =========================================================================
 * Internal process state (one slot per configured source)
 * ========================================================================= */
typedef struct {
    pid_t pid;                                    /**< Child process ID */
    int pipe_read_fd;                             /**< Read-end of pipe from child stdout */
    int log_file_fd;                              /**< Open fd to the log file */
    char log_file_path[LOG_CAPTURE_MAX_PATH_LEN]; /**< Path to log file */
    log_source_config_t config;                   /**< Copy of source configuration */
    bool active;                                  /**< Whether this slot is running */
} log_source_process_t;

static int create_directory_recursive(const char *path);
static int start_log_process(log_source_process_t *process);
static int stop_log_process(log_source_process_t *process);

static struct {
    bool initialized;
    char output_dir[LOG_CAPTURE_MAX_PATH_LEN];
    log_capture_config_t config;
    log_capture_session_t session;
    log_source_process_t processes[LOG_CAPTURE_MAX_SOURCES];
} g_log_capture = {
    .initialized = false,
    .session = {
        .is_active        = false,
        .num_active_sources = 0,
    },
};

static int create_directory_recursive(const char *path)
{
    char tmp[LOG_CAPTURE_MAX_PATH_LEN];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (tmp[len - 1] == '/') {
        tmp[len - 1] = 0;
    }

    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }

    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        return -1;
    }

    return 0;
}

static int start_log_process(log_source_process_t *process)
{
    pid_t pid;
    int pipefd[2];

    if (pipe(pipefd) < 0) {
        fprintf(stderr, "Failed to create pipe: %s\n", strerror(errno));
        return -1;
    }

    pid = fork();
    if (pid < 0) {
        fprintf(stderr, "Failed to fork log capture process: %s\n", strerror(errno));
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        close(pipefd[0]);

        if (dup2(pipefd[1], STDOUT_FILENO) < 0 || dup2(pipefd[1], STDERR_FILENO) < 0) {
            _exit(1);
        }

        close(pipefd[1]);

        fprintf(stderr, "[DEBUG] Log capture starting...\n");
        fflush(stderr);

        /* Build full command with arguments */
        if (strlen(process->config.args) > 0) {
            char full_cmd[LOG_CAPTURE_MAX_COMMAND_LEN * 2];
            snprintf(full_cmd, sizeof(full_cmd), "%s %s", process->config.command,
                     process->config.args);

            fprintf(stderr, "[DEBUG] Executing: %s\n", full_cmd);
            fflush(stderr);

            execl("/bin/sh", "sh", "-c", full_cmd, (char *)NULL);

            fprintf(stderr, "[DEBUG] exec failed: %s\n", strerror(errno));
            fflush(stderr);
        } else {
            fprintf(stderr, "[DEBUG] Executing: %s (no args)\n", process->config.command);
            fflush(stderr);

            execl(process->config.command, process->config.command, (char *)NULL);

            fprintf(stderr, "[DEBUG] exec failed: %s\n", strerror(errno));
            fflush(stderr);
        }

        _exit(1);
    }

    close(pipefd[1]);

    int fd = open(process->log_file_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        fprintf(stderr, "Failed to open log file %s: %s\n", process->log_file_path,
                strerror(errno));
        close(pipefd[0]);
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        return -1;
    }

    char marker[2048]; /* Increased size to accommodate long paths */
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

    const char *test_name = strrchr(process->log_file_path, '/');
    test_name = test_name ? test_name + 1 : process->log_file_path;

    snprintf(marker, sizeof(marker),
             "\n========================================\n"
             "TEST START: %s\n"
             "Time: %s\n"
             "========================================\n\n",
             test_name, timestamp);
    write(fd, marker, strlen(marker));

    process->pid = pid;
    process->active = true;

    /* Set pipe read-end to non-blocking so we can drain any early output
     * without blocking, then keep it open for stop_log_process() to drain
     * the remaining data when the test ends. */
    int flags = fcntl(pipefd[0], F_GETFL, 0);
    fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);

    /* Drain any output the child may have already produced (e.g. startup
     * messages) into the log file, then leave the pipe open. */
    struct timespec ts = { 0, 100000000L }; /* 100ms */
    nanosleep(&ts, NULL);

    char buffer[4096];
    ssize_t bytes_read;
    while ((bytes_read = read(pipefd[0], buffer, sizeof(buffer))) > 0) {
        write(fd, buffer, bytes_read);
    }

    /* Keep both fds: pipe_read_fd for draining in stop_log_process(),
     * log_file_fd for writing the end-marker and final data.
     * Do NOT close pipefd[0] here — that would make the child's writes
     * hit SIGPIPE and the pipe would carry no data to stop_log_process(). */
    process->pipe_read_fd = pipefd[0];
    process->log_file_fd = fd; /* transfer ownership; do not close fd here */

    return 0;
}

/**
 * @brief Stop a log capture process
 *
 * Sequence:
 *  1. Send SIGTERM to the child so it stops producing output.
 *  2. Close the write-end of the pipe (already closed in the parent after
 *     fork; the child's copy is the only remaining writer — killing the child
 *     causes EOF on the read-end).
 *  3. Drain any remaining data from the pipe into the log file.
 *  4. Write the end-marker, fsync, and close the log file fd.
 *  5. Close the pipe read-end.
 *  6. Reap the child with a bounded wait: WNOHANG after SIGTERM+200ms,
 *     then SIGKILL + bounded WNOHANG loop to avoid an infinite block.
 */
static int stop_log_process(log_source_process_t *process)
{
    if (!process->active) {
        return 0;
    }

    /* Step 1: signal the child first so it stops writing. */
    if (process->pid > 0 && kill(process->pid, 0) == 0) {
        kill(process->pid, SIGTERM);
    }

    /* Step 2+3: drain the pipe into the log file. */
    if (process->pipe_read_fd >= 0) {
        /* pipe_read_fd is already O_NONBLOCK from start_log_process(). */
        char buffer[4096];
        ssize_t bytes_read;

        /* Give the child a moment to flush its buffers after SIGTERM. */
        struct timespec drain_ts = { 0, 50000000L }; /* 50ms */
        nanosleep(&drain_ts, NULL);

        if (process->log_file_fd >= 0) {
            while ((bytes_read = read(process->pipe_read_fd, buffer, sizeof(buffer))) > 0) {
                write(process->log_file_fd, buffer, bytes_read);
            }
        }

        close(process->pipe_read_fd);
        process->pipe_read_fd = -1;
    }

    /* Step 4: write end-marker and close the log file. */
    if (process->log_file_fd >= 0) {
        char marker[2048];
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        char timestamp[64];
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

        const char *test_name = strrchr(process->log_file_path, '/');
        test_name = test_name ? test_name + 1 : process->log_file_path;

        snprintf(marker, sizeof(marker),
                 "\n========================================\n"
                 "TEST END: %s\n"
                 "Time: %s\n"
                 "========================================\n\n",
                 test_name, timestamp);
        write(process->log_file_fd, marker, strlen(marker));
        fsync(process->log_file_fd);
        close(process->log_file_fd);
        process->log_file_fd = -1;
    }

    /* Step 6: reap the child — bounded, never blocks forever.
     *
     * After SIGTERM + 200ms grace period, try WNOHANG.  If the child is
     * still alive, escalate to SIGKILL and poll up to 10 × 100ms = 1s.
     * If it still has not exited (extremely unlikely after SIGKILL), give
     * up and mark it as reaped anyway so the rest of the suite can proceed.
     */
    if (process->pid > 0) {
        struct timespec ts = { 0, 200000000L }; /* 200ms grace after SIGTERM */
        nanosleep(&ts, NULL);

        int status;
        pid_t result = waitpid(process->pid, &status, WNOHANG);

        if (result == 0) {
            /* Child still alive — escalate to SIGKILL. */
            kill(process->pid, SIGKILL);

            /* Poll up to 10 × 100ms = 1s for the child to be reaped. */
            struct timespec poll_ts = { 0, 100000000L }; /* 100ms */
            int retries = 10;
            while (retries-- > 0) {
                nanosleep(&poll_ts, NULL);
                result = waitpid(process->pid, &status, WNOHANG);
                if (result != 0)
                    break;
            }

            if (result == 0) {
                /* Give up — log a warning but do not block. */
                fprintf(stderr,
                        "[log_capture] Warning: child pid %d did not exit after "
                        "SIGKILL; skipping reap to avoid hang.\n",
                        (int)process->pid);
            }
        }
    }

    process->active = false;
    process->pid = 0;

    return 0;
}

/*
 * build_log_command_legacy — used only by the backward-compat helpers
 * log_capture_config_default_journalctl() and log_capture_config_default_logcat().
 *
 * Registry-sourced entries (LOG_SOURCE_CUSTOM) already have their command
 * and args pre-filled by log_capture_config_from_registry(), so this
 * function is never called for them.
 */
static void build_log_command_legacy(log_source_type_t type, char *command, size_t cmd_size,
                                     char *args, size_t args_size)
{
    switch (type) {
    case LOG_SOURCE_JOURNALCTL:
        /*
         * follow_mode=false → args is empty  → one-shot snapshot
         *   sh -c 'journalctl --no-pager 2>&1'
         * follow_mode=true  → args is "-f"   → streaming (may hang!)
         *   sh -c 'journalctl -f --no-pager 2>&1'
         */
        snprintf(command, cmd_size, "sh");
        if (args[0] != '\0') {
            char existing_args[LOG_CAPTURE_MAX_COMMAND_LEN];
            strncpy(existing_args, args, sizeof(existing_args) - 1);
            existing_args[sizeof(existing_args) - 1] = '\0';
            snprintf(args, args_size, "-c 'journalctl %s --no-pager 2>&1'", existing_args);
        } else {
            snprintf(args, args_size, "-c 'journalctl --no-pager 2>&1'");
        }
        break;

    case LOG_SOURCE_LOGCAT:
        snprintf(command, cmd_size, "logcat");
        snprintf(args, args_size, "-v time 2>&1");
        break;

    case LOG_SOURCE_DMESG:
        snprintf(command, cmd_size, "sh");
        snprintf(args, args_size, "-c 'dmesg 2>&1'");
        break;

    case LOG_SOURCE_CUSTOM:
        /* command/args already set by caller */
        break;

    default:
        command[0] = '\0';
        args[0] = '\0';
        break;
    }
}

int log_capture_init(const char *output_dir, const log_capture_config_t *config)
{
    if (g_log_capture.initialized) {
        fprintf(stderr, "Log capture already initialized\n");
        return -1;
    }

    if (output_dir != NULL) {
        strncpy(g_log_capture.output_dir, output_dir, sizeof(g_log_capture.output_dir) - 1);
    } else {
        strncpy(g_log_capture.output_dir, LOG_CAPTURE_DEFAULT_OUTPUT_DIR,
                sizeof(g_log_capture.output_dir) - 1);
    }
    g_log_capture.output_dir[sizeof(g_log_capture.output_dir) - 1] = '\0';

    if (create_directory_recursive(g_log_capture.output_dir) != 0) {
        fprintf(stderr, "Failed to create output directory %s: %s\n", g_log_capture.output_dir,
                strerror(errno));
        return -1;
    }

    if (config != NULL) {
        memcpy(&g_log_capture.config, config, sizeof(log_capture_config_t));
    } else {
        /*
         * Default: build configuration from the registry table.
         * Sources with enabled_by_default=true are active; others are off.
         * Individual sources can be toggled after this call via
         * log_capture_enable_source() / log_capture_disable_source().
         */
        log_capture_config_from_registry(&g_log_capture.config);
    }

    for (int i = 0; i < LOG_CAPTURE_MAX_SOURCES; i++) {
        g_log_capture.processes[i].active = false;
        g_log_capture.processes[i].pid = 0;
        g_log_capture.processes[i].pipe_read_fd = -1;
        g_log_capture.processes[i].log_file_fd = -1;
    }

    g_log_capture.initialized = true;
    return 0;
}

int log_capture_start(const char *test_name, const char *suite_name)
{
    if (!g_log_capture.initialized) {
        fprintf(stderr, "Log capture not initialized\n");
        return -1;
    }

    if (g_log_capture.session.is_active) {
        fprintf(stderr, "Log capture already active\n");
        return -1;
    }

    strncpy(g_log_capture.session.test_name, test_name,
            sizeof(g_log_capture.session.test_name) - 1);
    g_log_capture.session.test_name[sizeof(g_log_capture.session.test_name) - 1] = '\0';

    strncpy(g_log_capture.session.suite_name, suite_name,
            sizeof(g_log_capture.session.suite_name) - 1);
    g_log_capture.session.suite_name[sizeof(g_log_capture.session.suite_name) - 1] = '\0';

    struct timeval tv;
    gettimeofday(&tv, NULL);
    g_log_capture.session.start_time_us = (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;

    char timestamp[32];
    log_capture_get_timestamp(timestamp, sizeof(timestamp));

    time_t now = tv.tv_sec;
    struct tm *tm_info = gmtime(&now);
    char iso_timestamp[64];
    strftime(iso_timestamp, sizeof(iso_timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

    char safe_test_name[256];
    char safe_suite_name[256];
    log_capture_sanitize_filename(test_name, safe_test_name, sizeof(safe_test_name));
    log_capture_sanitize_filename(suite_name, safe_suite_name, sizeof(safe_suite_name));

    g_log_capture.session.num_active_sources = 0;
    for (uint32_t i = 0; i < g_log_capture.config.num_sources; i++) {
        log_source_config_t *src_config = &g_log_capture.config.sources[i];

        if (!src_config->enabled) {
            continue;
        }

        log_source_process_t *process = &g_log_capture.processes[i];

        memcpy(&process->config, src_config, sizeof(log_source_config_t));

        /*
         * Registry-sourced entries use LOG_SOURCE_CUSTOM with command/args
         * already filled in by log_capture_config_from_registry().
         * Legacy typed entries (JOURNALCTL, LOGCAT, DMESG) still need the
         * legacy command builder to expand their command/args fields.
         */
        if (src_config->type != LOG_SOURCE_CUSTOM) {
            build_log_command_legacy(src_config->type, process->config.command,
                                     sizeof(process->config.command), process->config.args,
                                     sizeof(process->config.args));
        }

        int path_len = snprintf(process->log_file_path, sizeof(process->log_file_path),
                                "%s/%s_%s_%s_%s.log", g_log_capture.output_dir, safe_suite_name,
                                safe_test_name, src_config->file_suffix, timestamp);

        if (path_len >= (int)sizeof(process->log_file_path)) {
            fprintf(stderr, "Warning: Log file path truncated for test %s\n", test_name);
        }

        strncpy(g_log_capture.session.log_file_paths[g_log_capture.session.num_active_sources],
                process->log_file_path, LOG_CAPTURE_MAX_PATH_LEN - 1);

        /* Record the source name at the same compact index so
         * log_capture_attach_to_allure() can build a clear display name
         * (e.g. "journalctl log") without needing to reverse-map the
         * config slot index. */
        strncpy(g_log_capture.session.source_names[g_log_capture.session.num_active_sources],
                src_config->name, LOG_CAPTURE_MAX_SOURCE_NAME_LEN - 1);
        g_log_capture.session.source_names[g_log_capture.session.num_active_sources]
                                          [LOG_CAPTURE_MAX_SOURCE_NAME_LEN - 1]
            = '\0';

        if (start_log_process(process) == 0) {
            g_log_capture.session.num_active_sources++;
        } else {
            fprintf(stderr, "Warning: Failed to start log source: %s\n", src_config->file_suffix);
        }
    }

    g_log_capture.session.is_active = true;
    return 0;
}

int log_capture_stop(void)
{
    if (!g_log_capture.initialized) {
        return -1;
    }

    if (!g_log_capture.session.is_active) {
        return 0; /* Not an error, just nothing to stop */
    }

    for (int i = 0; i < LOG_CAPTURE_MAX_SOURCES; i++) {
        if (g_log_capture.processes[i].active) {
            stop_log_process(&g_log_capture.processes[i]);
        }
    }

    if (g_log_capture.config.attach_to_allure) {
        log_capture_attach_to_allure();
    }

    g_log_capture.session.is_active = false;
    return 0;
}

const log_capture_session_t *log_capture_get_session(void)
{
    if (!g_log_capture.initialized || !g_log_capture.session.is_active) {
        return NULL;
    }
    return &g_log_capture.session;
}

int log_capture_attach_to_allure(void)
{
    if (!g_log_capture.initialized || !g_log_capture.session.is_active) {
        return -1;
    }

    for (uint32_t i = 0; i < g_log_capture.session.num_active_sources; i++) {
        const char *log_path = g_log_capture.session.log_file_paths[i];
        const char *source_name = g_log_capture.session.source_names[i];

        /*
         * Allure 3 resolves "source" as a filename RELATIVE to the results
         * directory passed to `allure generate`.  It does not accept absolute
         * paths — if an absolute path is stored, Allure marks the attachment
         * as "missed" and it cannot be viewed in the dashboard.
         *
         * The workflow is:
         *   1. Tests run on-device; log files land in /data/local/tmp/test-results/
         *   2. `adb pull /data/local/tmp/test-results/ test-results/` copies
         *      everything (JSON results + .log files) into the same flat directory.
         *   3. `allure generate test-results/` runs on the host.
         *
         * Because both the -result.json files and the .log files are pulled
         * into the same flat directory, the basename alone is sufficient for
         * Allure to locate each attachment.  We must NOT use the absolute
         * on-device path here.
         */
        const char *basename = strrchr(log_path, '/');
        const char *source = basename ? basename + 1 : log_path;

        /*
         * The display name shown in the Allure UI is "<source> log",
         * e.g. "journalctl log" or "dmesg log", using the registry name
         * recorded at the same compact index in log_capture_start().
         */
        char display_name[LOG_CAPTURE_MAX_SOURCE_NAME_LEN + 8];
        if (source_name && source_name[0] != '\0') {
            snprintf(display_name, sizeof(display_name), "%s log", source_name);
        } else {
            snprintf(display_name, sizeof(display_name), "%s", source);
        }

        if (unity_allure_output_attach_file(source, display_name, "text/plain") != 0) {
            fprintf(stderr, "Warning: Failed to attach log file to Allure: %s\n", source);
        }
    }

    return 0;
}

int log_capture_cleanup(void)
{
    if (!g_log_capture.initialized) {
        return 0;
    }

    if (g_log_capture.session.is_active) {
        log_capture_stop();
    }

    for (int i = 0; i < LOG_CAPTURE_MAX_SOURCES; i++) {
        if (g_log_capture.processes[i].active) {
            stop_log_process(&g_log_capture.processes[i]);
        }
    }

    g_log_capture.initialized = false;
    return 0;
}

bool log_capture_is_initialized(void) { return g_log_capture.initialized; }

bool log_capture_is_active(void)
{
    return g_log_capture.initialized && g_log_capture.session.is_active;
}

const char *log_capture_get_output_dir(void)
{
    if (!g_log_capture.initialized) {
        return NULL;
    }
    return g_log_capture.output_dir;
}

/* =========================================================================
 * Registry API
 * ========================================================================= */

const log_source_descriptor_t *log_capture_get_registry(void) { return s_source_registry; }

void log_capture_list_sources(void)
{
    printf("Available log sources:\n");
    printf("  %-20s  %-8s  %s\n", "Name", "Default", "Description");
    printf("  %-20s  %-8s  %s\n", "--------------------", "-------", "-----------");
    for (const log_source_descriptor_t *d = s_source_registry; d->name != NULL; d++) {
        printf("  %-20s  %-8s  %s\n", d->name, d->enabled_by_default ? "on" : "off",
               d->description);
    }
}

/* =========================================================================
 * Runtime enable / disable API
 * ========================================================================= */

int log_capture_enable_source(const char *name)
{
    if (!g_log_capture.initialized || !name)
        return -1;

    for (uint32_t i = 0; i < g_log_capture.config.num_sources; i++) {
        if (strcmp(g_log_capture.config.sources[i].name, name) == 0) {
            g_log_capture.config.sources[i].enabled = true;
            return 0;
        }
    }
    return -1; /* name not found in configured sources */
}

int log_capture_disable_source(const char *name)
{
    if (!g_log_capture.initialized || !name)
        return -1;

    for (uint32_t i = 0; i < g_log_capture.config.num_sources; i++) {
        if (strcmp(g_log_capture.config.sources[i].name, name) == 0) {
            g_log_capture.config.sources[i].enabled = false;
            return 0;
        }
    }
    return -1;
}

int log_capture_disable_all_sources(void)
{
    if (!g_log_capture.initialized)
        return -1;

    for (uint32_t i = 0; i < g_log_capture.config.num_sources; i++)
        g_log_capture.config.sources[i].enabled = false;

    return 0;
}

/* =========================================================================
 * Configuration helpers
 * ========================================================================= */

void log_capture_config_from_registry(log_capture_config_t *config)
{
    memset(config, 0, sizeof(log_capture_config_t));

    config->output_dir = LOG_CAPTURE_DEFAULT_OUTPUT_DIR;
    config->attach_to_allure = true;
    config->capture_on_failure_only = false;
    config->max_log_size_mb = 0; /* unlimited */
    config->num_sources = 0;

    for (const log_source_descriptor_t *d = s_source_registry; d->name != NULL; d++) {

        if (config->num_sources >= LOG_CAPTURE_MAX_SOURCES) {
            fprintf(stderr,
                    "[log_capture] Warning: registry has more sources than "
                    "LOG_CAPTURE_MAX_SOURCES (%d); truncating.\n",
                    LOG_CAPTURE_MAX_SOURCES);
            break;
        }

        uint32_t i = config->num_sources;

        /*
         * All registry sources are executed as:
         *   sh -c '<d->sh_command>'
         * We store "sh" in command and "-c '<sh_command>'" in args so that
         * start_log_process() can build the full command string uniformly.
         */
        config->sources[i].type = LOG_SOURCE_CUSTOM;
        config->sources[i].enabled = d->enabled_by_default;
        config->sources[i].file_suffix = d->file_suffix;

        strncpy(config->sources[i].name, d->name, sizeof(config->sources[i].name) - 1);
        config->sources[i].name[sizeof(config->sources[i].name) - 1] = '\0';

        strncpy(config->sources[i].command, "sh", sizeof(config->sources[i].command) - 1);
        config->sources[i].command[sizeof(config->sources[i].command) - 1] = '\0';

        snprintf(config->sources[i].args, sizeof(config->sources[i].args), "-c '%s'",
                 d->sh_command);

        config->num_sources++;
    }
}

/* ---- Legacy helpers (kept for backward compatibility) ---- */

void log_capture_config_default_journalctl(log_capture_config_t *config, bool follow_mode)
{
    memset(config, 0, sizeof(log_capture_config_t));

    config->output_dir = LOG_CAPTURE_DEFAULT_OUTPUT_DIR;
    config->num_sources = 1;
    config->attach_to_allure = true;
    config->capture_on_failure_only = false;
    config->max_log_size_mb = 0;

    config->sources[0].type = LOG_SOURCE_JOURNALCTL;
    config->sources[0].enabled = true;
    config->sources[0].file_suffix = "journalctl";
    strncpy(config->sources[0].name, "journalctl", sizeof(config->sources[0].name) - 1);

    config->sources[0].command[0] = '\0';
    if (follow_mode) {
        snprintf(config->sources[0].args, sizeof(config->sources[0].args), "-f");
    } else {
        config->sources[0].args[0] = '\0';
    }
}

void log_capture_config_default_logcat(log_capture_config_t *config)
{
    memset(config, 0, sizeof(log_capture_config_t));

    config->output_dir = LOG_CAPTURE_DEFAULT_OUTPUT_DIR;
    config->num_sources = 1;
    config->attach_to_allure = true;
    config->capture_on_failure_only = false;
    config->max_log_size_mb = 0;

    config->sources[0].type = LOG_SOURCE_LOGCAT;
    config->sources[0].enabled = true;
    config->sources[0].file_suffix = "logcat";
    strncpy(config->sources[0].name, "logcat", sizeof(config->sources[0].name) - 1);

    config->sources[0].command[0] = '\0';
    config->sources[0].args[0] = '\0';
}

int log_capture_config_add_source(log_capture_config_t *config, const char *command,
                                  const char *file_suffix)
{
    if (config->num_sources >= LOG_CAPTURE_MAX_SOURCES)
        return -1;

    uint32_t idx = config->num_sources;

    config->sources[idx].type = LOG_SOURCE_CUSTOM;
    config->sources[idx].enabled = true;
    config->sources[idx].file_suffix = file_suffix;

    /* Use file_suffix as the name for enable/disable lookups */
    strncpy(config->sources[idx].name, file_suffix, sizeof(config->sources[idx].name) - 1);
    config->sources[idx].name[sizeof(config->sources[idx].name) - 1] = '\0';

    strncpy(config->sources[idx].command, command, sizeof(config->sources[idx].command) - 1);
    config->sources[idx].command[sizeof(config->sources[idx].command) - 1] = '\0';

    config->sources[idx].args[0] = '\0';

    config->num_sources++;
    return 0;
}

void log_capture_sanitize_filename(const char *input, char *output, size_t output_size)
{
    size_t i, j;

    for (i = 0, j = 0; i < strlen(input) && j < output_size - 1; i++) {
        char c = input[i];

        if (isalnum(c) || c == '-' || c == '_' || c == '.') {
            output[j++] = c;
        } else if (c == ' ') {
            output[j++] = '_';
        }
    }

    output[j] = '\0';
}

int log_capture_get_timestamp(char *buffer, size_t buffer_size)
{
    time_t now;
    struct tm *tm_info;

    time(&now);
    tm_info = localtime(&now);

    if (tm_info == NULL) {
        return -1;
    }

    strftime(buffer, buffer_size, "%Y%m%d_%H%M%S", tm_info);
    return 0;
}
