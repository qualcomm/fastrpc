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
    "${CMAKE_CURRENT_SOURCE_DIR}/test_runner"
    "${FASTRPC_BASE_TEST_IDL_INC_DIR}"
    "${FASTRPC_INC_DIR}"
    "${CMAKE_SOURCE_DIR}/vendor/unity/src"
    "${CMAKE_SOURCE_DIR}/vendor/unity/extras/fixture/src"
    "${CMAKE_SOURCE_DIR}/vendor/unity/extras/memory/src"
)

# ----------------------------------------------------------------------------
# fastrpc_base_test_utils
# ----------------------------------------------------------------------------
add_library(fastrpc_base_test_utils STATIC
    test_runner/base_test_runner.c
    fastrpc_utils/test_config.c
    fastrpc_utils/test_domain.c
    fastrpc_utils/test_error.c
    fastrpc_utils/test_utils.c
)
add_dependencies(fastrpc_base_test_utils fastrpc_base_test_idl)

target_include_directories(fastrpc_base_test_utils PRIVATE
    ${_utils_all_inc}
)

target_compile_definitions(fastrpc_base_test_utils PRIVATE
    ${PLATFORM_C_DEFS}
    ${UNITY_COMPILE_DEFS}
    FASTRPC_BASE_TEST_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(fastrpc_base_test_utils PRIVATE
    base_test_warnings
)

# ----------------------------------------------------------------------------
# utils_dep — exported INTERFACE target
# ----------------------------------------------------------------------------
add_library(utils_dep INTERFACE)

target_link_libraries(utils_dep INTERFACE
    fastrpc_base_test_utils
    base_test_warnings
)

target_include_directories(utils_dep INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/fastrpc_utils"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_runner"
)
