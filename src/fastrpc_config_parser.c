// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#define _GNU_SOURCE
#ifndef VERIFY_PRINT_WARN
#define VERIFY_PRINT_WARN
#endif // VERIFY_PRINT_WARN
#ifndef VERIFY_PRINT_ERROR_ALWAYS
#define VERIFY_PRINT_ERROR_ALWAYS
#endif // VERIFY_PRINT_ERROR_ALWAYS
#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <yaml.h>

#define FARF_ERROR 1
#define FARF_HIGH 1
#define FARF_MED 1
#define FARF_LOW 1
#define FARF_CRITICAL 1 // Push log's to all hooks and persistent buffer.

#define CONFIG_DIR CONFIG_BASE_DIR "/conf.d/"

#include "AEEQList.h"
#include "AEEStdErr.h"
#include "AEEstd.h"
#include "HAP_farf.h"
#include "apps_std_internal.h"
#include "fastrpc_config_parser.h"

static int compare_strings(const void *a, const void *b) {
  return strcmp(*(const char **)a, *(const char **)b);
}

static void get_dsp_lib_path(const char *machine_name, const char *filepath, char *dsp_lib_paths) {
  yaml_parser_t parser;
  yaml_event_t event;
  int done = 0;
  int in_machines = 0;
  int in_target_machine = 0;
  int found_dsp_path = 0;
  char current_machine[PATH_MAX] = {0};

  FILE *file = fopen(filepath, "r");
  if (!file)
    return;

  if (!yaml_parser_initialize(&parser)) {
    FARF(ALWAYS, "Warning: Failed to initialize YAML parser for file %s\n", filepath);
    fclose(file);
    return;
  }

  yaml_parser_set_input_file(&parser, file);

  while (!done) {
    if (!yaml_parser_parse(&parser, &event)) {
      FARF(ALWAYS, "Warning: YAML parser error in file %s\n", filepath);
      break;
    }

    switch (event.type) {
      case YAML_SCALAR_EVENT: {
        const char *value = (const char *)event.data.scalar.value;

        if (!in_machines && strcmp(value, "machines") == 0) {
          in_machines = 1;
        } else if (in_machines && !in_target_machine) {
          // This is a machine name key
          strlcpy(current_machine, value, sizeof(current_machine));
          if (strcmp(current_machine, machine_name) == 0) {
            in_target_machine = 1;
          }
        } else if (in_target_machine && strcmp(value, DSP_LIB_KEY) == 0) {
          // Next scalar will be the DSP_LIBRARY_PATH value
          yaml_event_delete(&event);
          if (yaml_parser_parse(&parser, &event) && event.type == YAML_SCALAR_EVENT) {
            strlcpy(dsp_lib_paths, (const char *)event.data.scalar.value, PATH_MAX);
			FARF(ALWAYS, "dsp_lib_paths is %s", dsp_lib_paths);
            found_dsp_path = 1;
            done = 1;
          }
        }
        break;
      }
      case YAML_MAPPING_END_EVENT:
        if (in_target_machine) {
          // Exiting the target machine mapping
          in_target_machine = 0;
          if (found_dsp_path) {
            done = 1;
          }
        }
        break;
      case YAML_STREAM_END_EVENT:
        done = 1;
        break;
      default:
        break;
    }

    yaml_event_delete(&event);
  }

  yaml_parser_delete(&parser);
  fclose(file);

  if (!found_dsp_path) {
    FARF(ALWAYS, "Warning: DSP_LIBRARY_PATH not found for machine [%s] in configuration file %s\n", 
         machine_name, filepath);
  }
}

static void parse_config_dir(char *machine_name) {
  DIR *dir = opendir(CONFIG_DIR);
  struct dirent *entry;
  char *file_list[1024];
  int file_count = 0;
  char dsp_lib_paths[PATH_MAX] = {0};

  if (!dir)
    return;

  while ((entry = readdir(dir)) != NULL) {
    if (entry->d_type == DT_REG && 
    (strstr(entry->d_name, ".yaml") || strstr(entry->d_name, ".yml"))) {
       file_list[file_count] = strdup(entry->d_name);
       file_count++;
    }
  }
  closedir(dir);

  qsort(file_list, file_count, sizeof(char *), compare_strings);

  for (int i = 0; i < file_count; i++) {
    char filepath[PATH_MAX];
    snprintf(filepath, sizeof(filepath), "%s%s", CONFIG_DIR, file_list[i]);
    get_dsp_lib_path(machine_name, filepath, dsp_lib_paths);
    free(file_list[i]);
  }

  if (dsp_lib_paths[0] != '\0') {
    strlcpy(DSP_LIBS_LOCATION, CONFIG_BASE_DIR, sizeof(DSP_LIBS_LOCATION));
    //append slash in case user passed config base dir doesn't end with slash '/'
    strlcat(DSP_LIBS_LOCATION, "/", sizeof(DSP_LIBS_LOCATION));
    strlcat(DSP_LIBS_LOCATION, dsp_lib_paths, sizeof(DSP_LIBS_LOCATION));
    strlcat(DSP_LIBS_LOCATION, DEFAULT_DSP_SEARCH_PATHS, sizeof(DSP_LIBS_LOCATION));
  } else {
    FARF(ALWAYS, "Warning: No DSP library path found for machine [%s] in any configuration file\n", 
         machine_name);
  }
}

void configure_dsp_paths() {
  char machine_name[PATH_MAX] = {0};
  FILE *file = fopen(MACHINE_NAME_PATH, "r");

  if (file == NULL)
    return;

  if (fgets(machine_name, sizeof(machine_name), file) != NULL)
    // Remove trailing newline if present
    machine_name[strcspn(machine_name, "\n")] = '\0';

  fclose(file);
  parse_config_dir(machine_name);
}
