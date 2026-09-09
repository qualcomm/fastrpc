# Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#
# Compiles the QAIC-generated AArch64 RPC stub into a static library and
# exposes the generated headers directory to the parent scope.
#
# Exports (visible to parent and sibling CMakeLists.txt files):
#   fastrpc_test_stub        STATIC library target
#   FASTRPC_TEST_IDL_INC_DIR path variable (set in PARENT_SCOPE)

set(FASTRPC_TEST_IDL_INC_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/generated"
    PARENT_SCOPE
)

add_library(fastrpc_test_stub STATIC
    generated/fastrpc_test_stub.c
)

set_target_properties(fastrpc_test_stub PROPERTIES
    POSITION_INDEPENDENT_CODE OFF
)

target_include_directories(fastrpc_test_stub PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/generated"
    "${FASTRPC_INC_DIR}"
    "${FASTRPC_ROOT}/inc"
)

target_compile_definitions(fastrpc_test_stub PRIVATE
    ${PLATFORM_C_DEFS}
)
