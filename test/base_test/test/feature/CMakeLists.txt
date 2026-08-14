# Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#
# test/feature/CMakeLists.txt
#
# For each suite in _feat_suites:
#   1. Discovers all *.c files (test_*.c, utility *.c, all_tests.c) via
#      file(GLOB CONFIGURE_DEPENDS).
#   2. Builds a per-suite static library  feature_<name>_all.
#
# The suite library is linked into the unified test-fastrpc binary by
# bin/CMakeLists.txt.  The entry point is run_<name>_feature_tests()
# defined in all_tests.c and called from root_all_tests.c.
#
# Propagates ALL_SUITE_LIBS to the parent scope so bin/CMakeLists.txt can
# link all suite libraries into test-fastrpc.

# Suite names — add a new entry here when creating a new test/feature/<name>/ dir.
set(_feat_suites dspqueue)

set(_new_suite_libs "")

foreach(_suite IN LISTS _feat_suites)
    set(_suite_dir "${CMAKE_SOURCE_DIR}/test/feature/${_suite}")

    # Discover all *.c sources for this suite
    file(GLOB _suite_srcs
        CONFIGURE_DEPENDS
        "${_suite_dir}/*.c"
    )

    if(NOT _suite_srcs)
        message(WARNING "test/feature/${_suite}: no .c files found — skipping suite")
        continue()
    endif()

    add_library("feature_${_suite}_all" STATIC
        ${_suite_srcs}
    )

    set_target_properties("feature_${_suite}_all" PROPERTIES
        POSITION_INDEPENDENT_CODE OFF
    )

    target_include_directories("feature_${_suite}_all" PRIVATE
        "${_suite_dir}"
    )

    target_link_libraries("feature_${_suite}_all" PRIVATE
        common_test_dep
    )

    list(APPEND _new_suite_libs "feature_${_suite}_all")
endforeach()

set(ALL_SUITE_LIBS ${ALL_SUITE_LIBS} ${_new_suite_libs} PARENT_SCOPE)
