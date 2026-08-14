# Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#
# bin/CMakeLists.txt
#
# Produces two deployable artifacts, both placed in <builddir>/bin/:
#
#   test-fastrpc
#     Linked from root_all_tests.c (owns main()) + every per-suite static lib.
#
#   libfastrpc_test_skel.so  (built when clang is in PATH)
#     Hexagon DSP shared library compiled with --target=hexagon.
#     Push to device separately — never linked into any host binary.

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# ============================================================================
# test-fastrpc — unified host binary
# ============================================================================
add_executable(test-fastrpc
    "${CMAKE_SOURCE_DIR}/root_all_tests.c"
)

target_include_directories(test-fastrpc PRIVATE
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/test/feature"
    "${FASTRPC_TEST_IDL_INC_DIR}"
)

target_link_libraries(test-fastrpc PRIVATE
    common_test_dep
    ${ALL_SUITE_LIBS}
)

target_link_options(test-fastrpc PRIVATE
    ${PLATFORM_LINK_FLAGS}
)

set_target_properties(test-fastrpc PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# ============================================================================
# libfastrpc_test_skel.so — Hexagon DSP skel
# ============================================================================
find_program(HEXAGON_CLANG clang)

if(HEXAGON_CLANG)
    set(_gen_dir  "${CMAKE_SOURCE_DIR}/idl/generated")
    set(_impl_dir "${CMAKE_SOURCE_DIR}/idl/impl")
    set(_bin_dir  "${CMAKE_BINARY_DIR}/bin")

    set(_dsp_flags
        --target=hexagon
        -mv${DSP_ARCH}
        -fPIC
        -Wall
        -Werror
        -std=c11
        -D_POSIX_C_SOURCE=200112L
        -I${_gen_dir}
        -I${FASTRPC_INC_DIR}
    )

    add_custom_command(
        OUTPUT  "${_bin_dir}/fastrpc_test_skel.dsp.o"
        COMMAND "${HEXAGON_CLANG}" ${_dsp_flags}
                -c "${_gen_dir}/fastrpc_test_skel.c"
                -o "${_bin_dir}/fastrpc_test_skel.dsp.o"
        DEPENDS "${_gen_dir}/fastrpc_test_skel.c"
        COMMENT "Compiling DSP skel: fastrpc_test_skel.c"
        VERBATIM
    )

    add_custom_command(
        OUTPUT  "${_bin_dir}/fastrpc_test_imp.dsp.o"
        COMMAND "${HEXAGON_CLANG}" ${_dsp_flags}
                -c "${_impl_dir}/fastrpc_test_imp.c"
                -o "${_bin_dir}/fastrpc_test_imp.dsp.o"
        DEPENDS "${_impl_dir}/fastrpc_test_imp.c"
        COMMENT "Compiling DSP imp: fastrpc_test_imp.c"
        VERBATIM
    )

    add_custom_command(
        OUTPUT  "${_bin_dir}/libfastrpc_test_skel.so"
        COMMAND ld.lld
                -shared
                -soname=libfastrpc_test_skel.so
                -z separate-loadable-segments
                "${_bin_dir}/fastrpc_test_skel.dsp.o"
                "${_bin_dir}/fastrpc_test_imp.dsp.o"
                -o "${_bin_dir}/libfastrpc_test_skel.so"
        DEPENDS
            "${_bin_dir}/fastrpc_test_skel.dsp.o"
            "${_bin_dir}/fastrpc_test_imp.dsp.o"
        COMMENT "Linking DSP skel: libfastrpc_test_skel.so"
        VERBATIM
    )

    add_custom_target(fastrpc_test_skel ALL
        DEPENDS "${_bin_dir}/libfastrpc_test_skel.so"
    )

    message(STATUS
        "clang found at ${HEXAGON_CLANG} — "
        "fastrpc_test_skel target enabled (DSP arch: mv${DSP_ARCH})")
else()
    message(STATUS
        "clang not found — "
        "fastrpc_test_skel target will not be available "
        "(host binary test-fastrpc is unaffected)")
endif()
