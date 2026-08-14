# Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#
# utils/CMakeLists.txt
#
# Builds the shared test utilities and exposes them through one INTERFACE target.
#
# Exports:
#   utils_dep   INTERFACE library target

set(_utils_all_inc
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/fastrpc_utils"
    "${CMAKE_CURRENT_SOURCE_DIR}/log_capture"
    "${CMAKE_CURRENT_SOURCE_DIR}/reporting/unity"
    "${FASTRPC_TEST_IDL_INC_DIR}"
    "${FASTRPC_INC_DIR}"
    "${CMAKE_SOURCE_DIR}/vendor/unity/src"
    "${CMAKE_SOURCE_DIR}/vendor/unity/extras/fixture/src"
    "${CMAKE_SOURCE_DIR}/vendor/unity/extras/memory/src"
)

# ----------------------------------------------------------------------------
# fastrpc_test_utils
# ----------------------------------------------------------------------------
add_library(fastrpc_test_utils STATIC
    reporting/unity/unity_fixture_file_output.c
    log_capture/log_capture.c
    fastrpc_utils/test_utils.c
)
add_dependencies(fastrpc_test_utils fastrpc_test_idl)

set_target_properties(fastrpc_test_utils PROPERTIES
    POSITION_INDEPENDENT_CODE OFF
)

target_include_directories(fastrpc_test_utils PRIVATE
    ${_utils_all_inc}
)

target_compile_definitions(fastrpc_test_utils PRIVATE
    ${PLATFORM_C_DEFS}
    ${UNITY_COMPILE_DEFS}
    FASTRPC_TEST_VERSION="${PROJECT_VERSION}"
)

# ----------------------------------------------------------------------------
# utils_dep — exported INTERFACE target
# ----------------------------------------------------------------------------
add_library(utils_dep INTERFACE)

target_link_libraries(utils_dep INTERFACE
    fastrpc_test_utils
)

target_include_directories(utils_dep INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/fastrpc_utils"
    "${CMAKE_CURRENT_SOURCE_DIR}/log_capture"
    "${CMAKE_CURRENT_SOURCE_DIR}/reporting/unity"
)
