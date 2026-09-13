# Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#
# Generates the FastRPC test interface with QAIC, compiles the AArch64 RPC
# stub into a static library, and exposes the generated files to sibling
# directories.
#
# Exports (visible to parent and sibling CMakeLists.txt files):
#   fastrpc_test_idl         IDL generation target
#   fastrpc_test_stub        STATIC library target
#   FASTRPC_TEST_IDL_INC_DIR generated include directory
#   FASTRPC_TEST_IDL_HEADER  generated interface header
#   FASTRPC_TEST_SKEL_SOURCE generated DSP skeleton source

set(_idl_source "${CMAKE_CURRENT_SOURCE_DIR}/fastrpc_test.idl")
set(_idl_output_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(_idl_header "${_idl_output_dir}/fastrpc_test.h")
set(_idl_stub "${_idl_output_dir}/fastrpc_test_stub.c")
set(_idl_skel "${_idl_output_dir}/fastrpc_test_skel.c")

add_custom_command(
    OUTPUT
        "${_idl_header}"
        "${_idl_stub}"
        "${_idl_skel}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_idl_output_dir}"
    COMMAND "${QAIC_EXECUTABLE}"
            "-I=${FASTRPC_ROOT}/idl"
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

add_custom_target(fastrpc_test_idl
    DEPENDS
        "${_idl_header}"
        "${_idl_stub}"
        "${_idl_skel}"
)

add_library(fastrpc_test_stub STATIC
    "${_idl_stub}"
)
add_dependencies(fastrpc_test_stub fastrpc_test_idl)

set_target_properties(fastrpc_test_stub PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

target_include_directories(fastrpc_test_stub PRIVATE
    "${_idl_output_dir}"
    "${FASTRPC_INC_DIR}"
    "${FASTRPC_ROOT}/inc"
)

target_compile_definitions(fastrpc_test_stub PRIVATE
    ${PLATFORM_C_DEFS}
)

set(FASTRPC_TEST_IDL_INC_DIR "${_idl_output_dir}" PARENT_SCOPE)
set(FASTRPC_TEST_IDL_HEADER "${_idl_header}" PARENT_SCOPE)
set(FASTRPC_TEST_SKEL_SOURCE "${_idl_skel}" PARENT_SCOPE)
