# Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#
# Generates the FastRPC test interface with QAIC, compiles the AArch64 RPC
# stub into a static library, and exposes the generated files to sibling
# directories.
#
# Exports (visible to parent and sibling CMakeLists.txt files):
#   fastrpc_base_test_idl         IDL generation target
#   fastrpc_base_test_stub        STATIC library target
#   FASTRPC_BASE_TEST_IDL_INC_DIR generated include directory
#   FASTRPC_BASE_TEST_IDL_HEADER  generated interface header
#   FASTRPC_BASE_TEST_SKEL_SOURCE generated DSP skeleton source

set(_idl_source "${CMAKE_CURRENT_SOURCE_DIR}/fastrpc_base_test.idl")
set(_idl_output_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(_idl_header "${_idl_output_dir}/fastrpc_base_test.h")
set(_idl_stub "${_idl_output_dir}/fastrpc_base_test_stub.c")
set(_idl_skel "${_idl_output_dir}/fastrpc_base_test_skel.c")

add_custom_command(
    OUTPUT
        "${_idl_header}"
        "${_idl_stub}"
        "${_idl_skel}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_idl_output_dir}"
    COMMAND "${QAIC_EXECUTABLE}"
            -I "${FASTRPC_ROOT}/idl"
            "${_idl_source}"
            -st
            -o "${_idl_output_dir}"
    DEPENDS
        "${_idl_source}"
        "${FASTRPC_ROOT}/idl/AEEStdDef.idl"
        "${FASTRPC_ROOT}/idl/remote.idl"
        "${QAIC_EXECUTABLE}"
    COMMENT "Generating FastRPC test stub and skel with QAIC"
    VERBATIM
)

add_custom_target(fastrpc_base_test_idl
    DEPENDS
        "${_idl_header}"
        "${_idl_stub}"
        "${_idl_skel}"
)

add_library(fastrpc_base_test_stub STATIC
    "${_idl_stub}"
)
add_dependencies(fastrpc_base_test_stub fastrpc_base_test_idl)

target_include_directories(fastrpc_base_test_stub PRIVATE
    "${_idl_output_dir}"
    "${FASTRPC_INC_DIR}"
    "${FASTRPC_ROOT}/inc"
)

target_compile_definitions(fastrpc_base_test_stub PRIVATE
    ${PLATFORM_C_DEFS}
)

# QAIC emits aggregate initializers that trigger -Wmissing-braces with GCC.
# Keep the suite's -Werror policy for handwritten tests while silencing this
# warning only for the generated CPU stub.
target_compile_options(fastrpc_base_test_stub PRIVATE -Wno-missing-braces)

set(FASTRPC_BASE_TEST_IDL_INC_DIR "${_idl_output_dir}" PARENT_SCOPE)
set(FASTRPC_BASE_TEST_IDL_HEADER "${_idl_header}" PARENT_SCOPE)
set(FASTRPC_BASE_TEST_SKEL_SOURCE "${_idl_skel}" PARENT_SCOPE)
