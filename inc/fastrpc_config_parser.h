// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef FASTRPC_YAML_PARSER_H
#define FASTRPC_YAML_PARSER_H

#include "fastrpc_common.h"

// DEFAULT_DSP_SEARCH_PATHS intentionally left empty - these paths should be provided through configuration files
#ifndef DEFAULT_DSP_SEARCH_PATHS
#define DEFAULT_DSP_SEARCH_PATHS ""
#endif
#define DSP_LIB_KEY "DSP_LIBRARY_PATH"

/*
 * Per-domain YAML keys for the Hexagon architecture version string (e.g. "v75").
 * Indexed by base domain ID (ADSP=0 .. GDSP1=6); entry 7 is NULL (unused).
 */
extern const char *DSP_ARCH_KEY[NUM_DOMAINS];

extern char DSP_LIBS_LOCATION[PATH_MAX];

void configure_dsp_paths();
const char *get_dsp_arch_from_yaml(int domain);

#endif /*FASTRPC_YAML_PARSER_H*/
