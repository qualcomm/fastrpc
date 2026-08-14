// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file log_capture.h
 * @brief Log capture system for fastrpc-test framework
 *
 * This module provides automatic log capture functionality for test execution.
 * It captures logs from various sources (journalctl, dmesg, logcat, etc.)
 * during test execution and saves them as separate files for each test case.
 *
 * Features:
 * - Automatic start/stop of log capture synchronized with test lifecycle
 * - Registry-driven source table: adding a new log source requires only one
 *   new row in the s_source_registry[] table in log_capture.c — no enum
 *   change, no switch-case change, no new config helper needed
 * - Per-source enable/disable at runtime via log_capture_enable_source() /
 *   log_capture_disable_source(), or from the CLI with --logs <spec>
 * - Separate log files per test case, per source
 * - Integration with Allure reporting framework
 * - Configurable log output directory and format
 *
 * Usage:
 *   1. Initialize the log capture system before running tests:
 *      log_capture_init(output_dir, NULL);   // NULL = registry defaults
 *
 *   2. Optionally toggle individual sources after init:
 *      log_capture_enable_source("dmesg");
 *      log_capture_disable_source("logcat");
 *
 *   3. Start capturing logs before each test:
 *      log_capture_start(test_name, suite_name);
 *
 *   4. Stop capturing logs after each test:
 *      log_capture_stop();
 *
 *   5. Cleanup when all tests are complete:
 *      log_capture_cleanup();
 *
 * CLI usage (via --logs flag parsed by test_config_init):
 *   --logs journalctl        enable only journalctl
 *   --logs journalctl,dmesg  enable journalctl and dmesg
 *   --logs all               enable every registered source
 *   --logs none              disable all sources
 *   (omitting --logs uses each source's enabled_by_default setting)
 *
 * Adding a new log source:
 *   Append one row to s_source_registry[] in log_capture.c.  No other
 *   file needs to change.  The new source is immediately available via
 *   log_capture_enable_source("<name>") and --logs <name>.
 */

#ifndef LOG_CAPTURE_H
#define LOG_CAPTURE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ========== Configuration Constants ========== */

/** Maximum length for a shell command string */
#define LOG_CAPTURE_MAX_COMMAND_LEN 512

/** Maximum length for file paths */
#define LOG_CAPTURE_MAX_PATH_LEN 1024

/** Maximum number of concurrent log sources */
#define LOG_CAPTURE_MAX_SOURCES 8

/** Maximum length for a source name identifier */
#define LOG_CAPTURE_MAX_SOURCE_NAME_LEN 32

/** Default output directory for log files */
#ifndef LOG_CAPTURE_DEFAULT_OUTPUT_DIR
#define LOG_CAPTURE_DEFAULT_OUTPUT_DIR "/data/local/tmp/test-results"
#endif

/* =========================================================================
 * Source Registry
 *
 * log_source_descriptor_t describes one built-in log source completely.
 * All descriptors live in a sentinel-terminated table (s_source_registry[])
 * in log_capture.c.  The table is the single extension point for new
 * sources — no enum, no switch-case, no new config helper required.
 * ========================================================================= */

/**
 * @brief Static descriptor for one built-in log source.
 *
 * @field name               Short identifier used in CLI --logs flags and
 *                           enable/disable API calls (e.g. "journalctl",
 *                           "dmesg", "logcat").  Must be unique.
 * @field file_suffix        Suffix embedded in the output filename so each
 *                           source's log is clearly labelled.
 * @field sh_command         Shell command executed as: sh -c '<sh_command>'
 *                           Must write its output to stdout/stderr and exit.
 *                           One-shot commands (dmesg, journalctl snapshot)
 *                           are preferred; streaming commands that never exit
 *                           on their own will be killed by stop_log_process().
 * @field enabled_by_default Whether this source is active unless the caller
 *                           explicitly disables it after log_capture_init().
 * @field description        Human-readable description shown by
 *                           log_capture_list_sources().
 */
typedef struct {
    const char *name;
    const char *file_suffix;
    const char *sh_command;
    bool enabled_by_default;
    const char *description;
} log_source_descriptor_t;

/**
 * @brief Return the built-in source registry table.
 *
 * The table is terminated by a sentinel entry with name == NULL.
 * Iterate until name == NULL to visit every registered source.
 *
 * @return Pointer to the first entry in the registry.
 */
const log_source_descriptor_t *log_capture_get_registry(void);

/**
 * @brief Print all registered log sources and their default-enabled state.
 *
 * Writes a human-readable list to stdout.  Intended for --help output.
 */
void log_capture_list_sources(void);

/* =========================================================================
 * Internal source configuration (used inside log_capture_config_t)
 *
 * Kept for backward compatibility with log_capture_config_add_source() and
 * the legacy log_capture_config_default_journalctl() / _logcat() helpers.
 * New sources should be added to the registry table instead.
 * ========================================================================= */

/**
 * @brief Log source type enumeration (legacy, kept for backward compat)
 */
typedef enum {
    LOG_SOURCE_JOURNALCTL, /**< systemd journal logs */
    LOG_SOURCE_LOGCAT,     /**< Android logcat */
    LOG_SOURCE_DMESG,      /**< Kernel ring buffer */
    LOG_SOURCE_CUSTOM      /**< Custom / registry-built command */
} log_source_type_t;

/**
 * @brief Per-source runtime configuration slot.
 *
 * Populated either from the registry (via log_capture_config_from_registry)
 * or manually via the legacy log_capture_config_default_* helpers.
 */
typedef struct {
    log_source_type_t type;                     /**< Source type (CUSTOM for registry sources) */
    char command[LOG_CAPTURE_MAX_COMMAND_LEN];  /**< Executable ("sh" for registry sources) */
    char args[LOG_CAPTURE_MAX_COMMAND_LEN];     /**< Arguments ("-c '<sh_command>'" for registry) */
    bool enabled;                               /**< Runtime enable/disable toggle */
    const char *file_suffix;                    /**< Suffix in the output filename */
    char name[LOG_CAPTURE_MAX_SOURCE_NAME_LEN]; /**< Registry name for enable/disable lookup */
} log_source_config_t;

/**
 * @brief Top-level log capture configuration.
 */
typedef struct {
    const char *output_dir;                               /**< Output directory for log files */
    log_source_config_t sources[LOG_CAPTURE_MAX_SOURCES]; /**< Configured source slots */
    uint32_t num_sources;                                 /**< Number of populated slots */
    bool attach_to_allure;                                /**< Attach logs to Allure reports */
    bool capture_on_failure_only;                         /**< Only capture logs for failed tests */
    uint32_t max_log_size_mb; /**< Max log file size in MB (0 = unlimited) */
} log_capture_config_t;

/**
 * @brief Log capture session information (read-only snapshot of active capture).
 */
typedef struct {
    char test_name[256];  /**< Current test name */
    char suite_name[256]; /**< Current test suite name */
    char log_file_paths[LOG_CAPTURE_MAX_SOURCES][LOG_CAPTURE_MAX_PATH_LEN];
    char source_names[LOG_CAPTURE_MAX_SOURCES]
                     [LOG_CAPTURE_MAX_SOURCE_NAME_LEN]; /**< Registry name for each active source */
    uint32_t num_active_sources; /**< Number of sources that started successfully */
    bool is_active;              /**< Whether capture is currently running */
    uint64_t start_time_us;      /**< Capture start time in microseconds */
} log_capture_session_t;

/* =========================================================================
 * Core lifecycle API
 * ========================================================================= */

/**
 * @brief Initialize the log capture system.
 *
 * Must be called before any other log capture function.
 * Creates the output directory if it does not exist.
 *
 * When config is NULL the system builds its configuration from the registry
 * table, enabling every source whose enabled_by_default flag is true.
 * Individual sources can then be toggled with log_capture_enable_source() /
 * log_capture_disable_source() before the first log_capture_start() call.
 *
 * @param output_dir  Output directory for log files (NULL = use default).
 * @param config      Log capture configuration (NULL = build from registry).
 * @return 0 on success, -1 on error.
 */
int log_capture_init(const char *output_dir, const log_capture_config_t *config);

/**
 * @brief Start capturing logs for a test case.
 *
 * Launches all enabled log source processes and redirects their output to
 * per-test files.  Call at the beginning of each test case.
 *
 * @param test_name   Name of the test case.
 * @param suite_name  Name of the test suite/group.
 * @return 0 on success, -1 on error.
 */
int log_capture_start(const char *test_name, const char *suite_name);

/**
 * @brief Stop capturing logs for the current test case.
 *
 * Stops all active log capture processes, drains their output, writes
 * end-markers, and optionally attaches logs to the Allure report.
 * Call at the end of each test case.
 *
 * @return 0 on success, -1 on error.
 */
int log_capture_stop(void);

/**
 * @brief Attach captured logs to the Allure report.
 *
 * Attaches all log files from the current session to the Allure report.
 * Called automatically by log_capture_stop() when attach_to_allure is set.
 *
 * @return 0 on success, -1 on error.
 */
int log_capture_attach_to_allure(void);

/**
 * @brief Cleanup the log capture system.
 *
 * Stops any active capture and releases all resources.
 * Call after all tests are complete.
 *
 * @return 0 on success, -1 on error.
 */
int log_capture_cleanup(void);

/**
 * @brief Get the current log capture session information.
 *
 * @return Pointer to session info, or NULL if no active session.
 */
const log_capture_session_t *log_capture_get_session(void);

/** @return true if log_capture_init() has been called successfully. */
bool log_capture_is_initialized(void);

/** @return true if a capture session is currently active. */
bool log_capture_is_active(void);

/** @return Output directory path, or NULL if not initialized. */
const char *log_capture_get_output_dir(void);

/* =========================================================================
 * Runtime enable / disable API
 *
 * These functions operate on the live configuration inside g_log_capture
 * and must be called after log_capture_init() and before the first
 * log_capture_start() call.
 * ========================================================================= */

/**
 * @brief Enable a log source by name.
 *
 * Looks up the source by its registry name (e.g. "dmesg") and sets its
 * enabled flag to true.  If the source was not included in the initial
 * configuration (e.g. because enabled_by_default was false) this call
 * has no effect and returns -1.
 *
 * @param name  Registry name of the source to enable.
 * @return 0 on success, -1 if not initialized or name not found.
 */
int log_capture_enable_source(const char *name);

/**
 * @brief Disable a log source by name.
 *
 * Looks up the source by its registry name and sets its enabled flag to
 * false.  The slot is preserved so the source can be re-enabled later.
 *
 * @param name  Registry name of the source to disable.
 * @return 0 on success, -1 if not initialized or name not found.
 */
int log_capture_disable_source(const char *name);

/**
 * @brief Disable all configured log sources.
 *
 * Convenience function equivalent to calling log_capture_disable_source()
 * for every configured source.  Useful for --logs none.
 *
 * @return 0 on success, -1 if not initialized.
 */
int log_capture_disable_all_sources(void);

/* =========================================================================
 * Configuration helpers
 * ========================================================================= */

/**
 * @brief Build a configuration from the registry with default-enabled sources.
 *
 * Iterates the registry table and populates config->sources[] for every
 * entry.  Sources with enabled_by_default == true are enabled; others are
 * disabled.  This is the configuration used by log_capture_init() when
 * config == NULL.
 *
 * Individual sources can be toggled after log_capture_init() via
 * log_capture_enable_source() / log_capture_disable_source().
 *
 * @param config  Pointer to configuration structure to populate.
 */
void log_capture_config_from_registry(log_capture_config_t *config);

/**
 * @brief Create a configuration with only journalctl enabled (legacy helper).
 *
 * Kept for backward compatibility.  Prefer log_capture_config_from_registry()
 * for new code.
 *
 * @param config       Pointer to configuration structure to populate.
 * @param follow_mode  If true, use 'journalctl -f' (streaming, may hang).
 *                     If false (recommended), use a one-shot snapshot.
 */
void log_capture_config_default_journalctl(log_capture_config_t *config, bool follow_mode);

/**
 * @brief Create a configuration with only logcat enabled (legacy helper).
 *
 * Kept for backward compatibility.  Prefer log_capture_config_from_registry()
 * for new code.
 *
 * @param config  Pointer to configuration structure to populate.
 */
void log_capture_config_default_logcat(log_capture_config_t *config);

/**
 * @brief Add a custom log source to an existing configuration.
 *
 * The command is run via execl() directly (not through sh -c).  To use
 * shell features (pipes, redirects) prefix the command with "sh -c '...'".
 *
 * @param config      Configuration to modify.
 * @param command     Command to execute.
 * @param file_suffix Suffix for the log filename (e.g. "myapp").
 * @return 0 on success, -1 if LOG_CAPTURE_MAX_SOURCES is already reached.
 */
int log_capture_config_add_source(log_capture_config_t *config, const char *command,
                                  const char *file_suffix);

/* =========================================================================
 * Helper utilities
 * ========================================================================= */

/**
 * @brief Sanitize a string for use in filenames.
 *
 * Replaces characters that are invalid in filenames with underscores.
 *
 * @param input        Input string.
 * @param output       Output buffer.
 * @param output_size  Size of output buffer.
 */
void log_capture_sanitize_filename(const char *input, char *output, size_t output_size);

/**
 * @brief Generate a timestamp string for log filenames (YYYYMMDD_HHMMSS).
 *
 * @param buffer       Output buffer.
 * @param buffer_size  Size of output buffer.
 * @return 0 on success, -1 on error.
 */
int log_capture_get_timestamp(char *buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif /* LOG_CAPTURE_H */
