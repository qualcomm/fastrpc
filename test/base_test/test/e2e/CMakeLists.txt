# Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#
# Calculator and HAP sample tests use the common fastrpc_base_test IDL and skeleton.
# Their test cases are ordinary base-test suites; no separate QAIC or DSP build
# path is required.

include("${CMAKE_SOURCE_DIR}/cmake/regular_test_category.cmake")

add_library(e2e_support STATIC
    common/e2e_session.c
)
target_include_directories(e2e_support PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/common"
    "${CMAKE_SOURCE_DIR}/utils"
    "${CMAKE_SOURCE_DIR}/utils/fastrpc_utils"
    "${FASTRPC_BASE_TEST_IDL_INC_DIR}"
    "${FASTRPC_INC_DIR}"
)
target_link_libraries(e2e_support PRIVATE
    common_test_dep
)

base_test_add_category(
    CATEGORY             e2e
    CATEGORY_ROOT        "${CMAKE_CURRENT_SOURCE_DIR}"
    SUITE_TARGET_PREFIX  e2e
    SUITE_ENTRY_SUFFIX   e2e_tests
    DISPATCH_FUNCTION    run_e2e_suite_tests
    DISPATCH_SOURCE_VAR  E2E_CATEGORY_DISPATCH_SOURCE
    SUITE_LIBS_VAR       E2E_SUITE_LIBS
    EXCLUDE_DIRS         common
    EXTRA_INCLUDE_DIRS   "${CMAKE_CURRENT_SOURCE_DIR}/common"
    EXTRA_LINK_LIBS      e2e_support
)

set(ALL_SUITE_LIBS ${ALL_SUITE_LIBS} ${E2E_SUITE_LIBS} PARENT_SCOPE)
set(REGULAR_CATEGORY_DISPATCH_SOURCES
    ${REGULAR_CATEGORY_DISPATCH_SOURCES}
    "${E2E_CATEGORY_DISPATCH_SOURCE}"
    PARENT_SCOPE)

# The suites use the generic libfastrpc_base_test_skel.so generated from
# idl/fastrpc_base_test.idl.  Deploy that one artifact for all base-test cases.
