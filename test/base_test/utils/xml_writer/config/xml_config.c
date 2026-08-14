// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "xml_config.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* Global configuration manager */
static xml_config_manager_t *g_config_manager = NULL;

/* ========== Configuration Operations ========== */

xml_config_t *xml_config_create_default(void)
{
    xml_config_t *config = calloc(1, sizeof(xml_config_t));
    if (!config)
        return NULL;

    /* Output settings */
    config->output_directory = strdup("/data/local/tmp/test-results");
    config->output_filename_pattern = strdup("{basename}_{timestamp}.xml");
    config->pretty_print = true;
    config->indent_size = 2;
    config->encoding = strdup("UTF-8");

    /* Performance settings */
    config->stream_buffer_size = 4096;
    config->cache_max_size = 10485760; /* 10 MB */
    config->enable_caching = true;

    /* Validation settings */
    config->validate_on_write = false;
    config->strict_validation = false;

    /* Logging settings */
    config->log_level = XML_LOG_INFO;
    config->log_output_path = NULL;
    config->structured_logging = false;

    /* Feature flags */
    config->enable_compression = false;
    config->enable_metrics = true;

    /* Environment */
    config->environment = XML_ENV_DEVELOPMENT;

    return config;
}

xml_config_t *xml_config_create_for_env(xml_environment_t env)
{
    xml_config_t *config = xml_config_create_default();
    if (!config)
        return NULL;

    config->environment = env;

    switch (env) {
    case XML_ENV_DEVELOPMENT:
        config->pretty_print = true;
        config->log_level = XML_LOG_DEBUG;
        config->validate_on_write = true;
        config->strict_validation = true;
        break;

    case XML_ENV_TESTING:
        config->pretty_print = true;
        config->log_level = XML_LOG_INFO;
        config->validate_on_write = true;
        config->strict_validation = false;
        break;

    case XML_ENV_STAGING:
        config->pretty_print = false;
        config->log_level = XML_LOG_WARN;
        config->validate_on_write = false;
        config->enable_compression = true;
        break;

    case XML_ENV_PRODUCTION:
        config->pretty_print = false;
        config->log_level = XML_LOG_ERROR;
        config->validate_on_write = false;
        config->enable_compression = true;
        config->cache_max_size = 52428800; /* 50 MB */
        break;
    }

    return config;
}

void xml_config_destroy(xml_config_t *config)
{
    if (!config)
        return;

    free(config->output_directory);
    free(config->output_filename_pattern);
    free(config->encoding);
    free(config->log_output_path);
    free(config);
}

xml_config_t *xml_config_clone(const xml_config_t *config)
{
    if (!config)
        return NULL;

    xml_config_t *clone = calloc(1, sizeof(xml_config_t));
    if (!clone)
        return NULL;

    /* Copy all fields */
    *clone = *config;

    /* Deep copy strings */
    clone->output_directory = config->output_directory ? strdup(config->output_directory) : NULL;
    clone->output_filename_pattern
        = config->output_filename_pattern ? strdup(config->output_filename_pattern) : NULL;
    clone->encoding = config->encoding ? strdup(config->encoding) : NULL;
    clone->log_output_path = config->log_output_path ? strdup(config->log_output_path) : NULL;

    return clone;
}

/* ========== Configuration Manager Operations ========== */

xml_config_manager_t *xml_config_manager_create(void)
{
    xml_config_manager_t *manager = calloc(1, sizeof(xml_config_manager_t));
    if (!manager)
        return NULL;

    /* Create configurations for all environments */
    for (int i = 0; i < 4; i++) {
        manager->configs[i] = xml_config_create_for_env((xml_environment_t)i);
        if (!manager->configs[i]) {
            xml_config_manager_destroy(manager);
            return NULL;
        }
    }

    /* Default to development environment */
    manager->current_env = XML_ENV_DEVELOPMENT;
    manager->active_config = manager->configs[XML_ENV_DEVELOPMENT];

    /* Apply environment variable overrides */
    xml_config_apply_env_overrides(manager->active_config);

    return manager;
}

void xml_config_manager_destroy(xml_config_manager_t *manager)
{
    if (!manager)
        return;

    for (int i = 0; i < 4; i++) {
        xml_config_destroy(manager->configs[i]);
    }

    free(manager);
}

xml_config_manager_t *xml_config_manager_get_global(void)
{
    if (!g_config_manager) {
        g_config_manager = xml_config_manager_create();
    }
    return g_config_manager;
}

xml_config_t *xml_config_manager_get_config(xml_config_manager_t *manager)
{
    return manager ? manager->active_config : NULL;
}

void xml_config_manager_set_environment(xml_config_manager_t *manager, xml_environment_t env)
{
    if (!manager || env < 0 || env > XML_ENV_PRODUCTION)
        return;

    manager->current_env = env;
    manager->active_config = manager->configs[env];
}

void xml_config_manager_override(xml_config_manager_t *manager, const char *key, const char *value)
{
    if (!manager || !key || !value)
        return;

    xml_config_t *config = manager->active_config;
    if (!config)
        return;

    /* Handle different configuration keys */
    if (strcmp(key, "output_directory") == 0) {
        free(config->output_directory);
        config->output_directory = strdup(value);
    } else if (strcmp(key, "pretty_print") == 0) {
        config->pretty_print = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
    } else if (strcmp(key, "indent_size") == 0) {
        config->indent_size = atoi(value);
    } else if (strcmp(key, "log_level") == 0) {
        config->log_level = xml_log_level_from_string(value);
    } else if (strcmp(key, "enable_caching") == 0) {
        config->enable_caching = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
    } else if (strcmp(key, "enable_metrics") == 0) {
        config->enable_metrics = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
    }
}

/* ========== Configuration Loading ========== */

xml_config_t *xml_config_load_from_env(void)
{
    xml_config_t *config = xml_config_create_default();
    if (!config)
        return NULL;

    xml_config_apply_env_overrides(config);

    return config;
}

void xml_config_apply_env_overrides(xml_config_t *config)
{
    if (!config)
        return;

    const char *env_val;

    /* Check for environment variable */
    env_val = getenv("XML_ENV");
    if (env_val) {
        config->environment = xml_environment_from_string(env_val);
    }

    /* Output directory */
    env_val = getenv("XML_OUTPUT_DIR");
    if (env_val) {
        free(config->output_directory);
        config->output_directory = strdup(env_val);
    }

    /* Pretty print */
    env_val = getenv("XML_PRETTY_PRINT");
    if (env_val) {
        config->pretty_print = (strcmp(env_val, "true") == 0 || strcmp(env_val, "1") == 0);
    }

    /* Log level */
    env_val = getenv("XML_LOG_LEVEL");
    if (env_val) {
        config->log_level = xml_log_level_from_string(env_val);
    }

    /* Enable caching */
    env_val = getenv("XML_ENABLE_CACHE");
    if (env_val) {
        config->enable_caching = (strcmp(env_val, "true") == 0 || strcmp(env_val, "1") == 0);
    }
}

/* ========== Utility Functions ========== */

const char *xml_environment_to_string(xml_environment_t env)
{
    switch (env) {
    case XML_ENV_DEVELOPMENT:
        return "development";
    case XML_ENV_TESTING:
        return "testing";
    case XML_ENV_STAGING:
        return "staging";
    case XML_ENV_PRODUCTION:
        return "production";
    default:
        return "unknown";
    }
}

xml_environment_t xml_environment_from_string(const char *str)
{
    if (!str)
        return XML_ENV_DEVELOPMENT;

    if (strcasecmp(str, "development") == 0 || strcasecmp(str, "dev") == 0) {
        return XML_ENV_DEVELOPMENT;
    }
    if (strcasecmp(str, "testing") == 0 || strcasecmp(str, "test") == 0) {
        return XML_ENV_TESTING;
    }
    if (strcasecmp(str, "staging") == 0 || strcasecmp(str, "stage") == 0) {
        return XML_ENV_STAGING;
    }
    if (strcasecmp(str, "production") == 0 || strcasecmp(str, "prod") == 0) {
        return XML_ENV_PRODUCTION;
    }

    return XML_ENV_DEVELOPMENT;
}

void xml_config_print(const xml_config_t *config, FILE *output)
{
    if (!config || !output)
        return;

    fprintf(output, "=== XML Configuration ===\n");
    fprintf(output, "Environment: %s\n", xml_environment_to_string(config->environment));
    fprintf(output, "\nOutput Settings:\n");
    fprintf(output, "  Directory: %s\n", config->output_directory);
    fprintf(output, "  Filename Pattern: %s\n", config->output_filename_pattern);
    fprintf(output, "  Pretty Print: %s\n", config->pretty_print ? "yes" : "no");
    fprintf(output, "  Indent Size: %d\n", config->indent_size);
    fprintf(output, "  Encoding: %s\n", config->encoding);

    fprintf(output, "\nPerformance Settings:\n");
    fprintf(output, "  Stream Buffer Size: %zu bytes\n", config->stream_buffer_size);
    fprintf(output, "  Cache Max Size: %zu bytes\n", config->cache_max_size);
    fprintf(output, "  Enable Caching: %s\n", config->enable_caching ? "yes" : "no");

    fprintf(output, "\nValidation Settings:\n");
    fprintf(output, "  Validate on Write: %s\n", config->validate_on_write ? "yes" : "no");
    fprintf(output, "  Strict Validation: %s\n", config->strict_validation ? "yes" : "no");

    fprintf(output, "\nLogging Settings:\n");
    fprintf(output, "  Log Level: %s\n", xml_log_level_to_string(config->log_level));
    fprintf(output, "  Log Output: %s\n",
            config->log_output_path ? config->log_output_path : "stderr");
    fprintf(output, "  Structured Logging: %s\n", config->structured_logging ? "yes" : "no");

    fprintf(output, "\nFeature Flags:\n");
    fprintf(output, "  Enable Compression: %s\n", config->enable_compression ? "yes" : "no");
    fprintf(output, "  Enable Metrics: %s\n", config->enable_metrics ? "yes" : "no");
    fprintf(output, "========================\n");
}
