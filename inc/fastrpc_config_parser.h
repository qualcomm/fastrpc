// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef FASTRPC_YAML_PARSER_H
#define FASTRPC_YAML_PARSER_H

// DEFAULT_DSP_SEARCH_PATHS intentionally left empty - these paths should be provided through configuration files
#ifndef DEFAULT_DSP_SEARCH_PATHS
#define DEFAULT_DSP_SEARCH_PATHS ""
#endif
#define DSP_LIB_KEY "DSP_LIBRARY_PATH"

extern char DSP_LIBS_LOCATION[PATH_MAX];

void configure_dsp_paths();

#endif /*FASTRPC_YAML_PARSER_H*/
