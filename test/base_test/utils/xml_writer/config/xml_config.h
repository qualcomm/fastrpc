// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef XML_CONFIG_H
#define XML_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../logging/xml_logger.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @file xml_config.h
 * @brief Configuration management for XML output system
 */

/* Environment types */
typedef enum {
    XML_ENV_DEVELOPMENT,
    XML_ENV_TESTING,
    XML_ENV_STAGING,
    XML_ENV_PRODUCTION
} xml_environment_t;

/**
 * @brief XML output configuration
 */
typedef struct xml_config {
    /* Output settings */
    char *output_directory;
    char *output_filename_pattern;
    bool pretty_print;
    int indent_size;
    char *encoding;

    /* Performance settings */
    size_t stream_buffer_size;
    size_t cache_max_size;
    bool enable_caching;

    /* Validation settings */
    bool validate_on_write;
    bool strict_validation;

    /* Logging settings */
    xml_log_level_t log_level;
    char *log_output_path;
    bool structured_logging;

    /* Feature flags */
    bool enable_compression;
    bool enable_metrics;

    /* Environment */
    xml_environment_t environment;
} xml_config_t;

/**
 * @brief Configuration manager
 */
typedef struct xml_config_manager {
    xml_config_t *configs[4]; /* One for each environment */
    xml_environment_t current_env;
    xml_config_t *active_config;
} xml_config_manager_t;

/* ========== Configuration Operations ========== */

/**
 * @brief Create a default configuration
 */
xml_config_t *xml_config_create_default(void);

/**
 * @brief Create configuration for specific environment
 */
xml_config_t *xml_config_create_for_env(xml_environment_t env);

/**
 * @brief Destroy a configuration
 */
void xml_config_destroy(xml_config_t *config);

/**
 * @brief Clone a configuration
 */
xml_config_t *xml_config_clone(const xml_config_t *config);

/* ========== Configuration Manager Operations ========== */

/**
 * @brief Create a configuration manager
 */
xml_config_manager_t *xml_config_manager_create(void);

/**
 * @brief Destroy a configuration manager
 */
void xml_config_manager_destroy(xml_config_manager_t *manager);

/**
 * @brief Get the global configuration manager
 */
xml_config_manager_t *xml_config_manager_get_global(void);

/**
 * @brief Get active configuration
 */
xml_config_t *xml_config_manager_get_config(xml_config_manager_t *manager);

/**
 * @brief Set environment
 */
void xml_config_manager_set_environment(xml_config_manager_t *manager, xml_environment_t env);

/**
 * @brief Override a configuration setting
 */
void xml_config_manager_override(xml_config_manager_t *manager, const char *key, const char *value);

/* ========== Configuration Loading ========== */

/**
 * @brief Load configuration from environment variables
 */
xml_config_t *xml_config_load_from_env(void);

/**
 * @brief Apply environment variable overrides
 */
void xml_config_apply_env_overrides(xml_config_t *config);

/* ========== Utility Functions ========== */

/**
 * @brief Get environment as string
 */
const char *xml_environment_to_string(xml_environment_t env);

/**
 * @brief Parse environment from string
 */
xml_environment_t xml_environment_from_string(const char *str);

/**
 * @brief Print configuration
 */
void xml_config_print(const xml_config_t *config, FILE *output);

#ifdef __cplusplus
}
#endif

#endif /* XML_CONFIG_H */
