# Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#
# bin/CMakeLists.txt
#
# Produces two deployable artifacts, all placed in <builddir>/bin/:
#
#   fastrpc_base_test
#     Linked from root_all_tests.c (owns main()) + every per-suite static lib.
#
#   libfastrpc_base_test_skel.so
#     Hexagon DSP shared library compiled with --target=hexagon.
#     Push to device separately — never linked into any host binary.

# A complete base_test build always includes the DSP skeleton.  The compiler
# and linker are validated during configure so a successful build cannot omit
# the device-side artifact.

# HEXAGON_CLANG and HEXAGON_LD are declared as cache inputs by the root
# CMakeLists.txt.  Keep the resolved paths local so an automatically found
# tool does not overwrite the user's cache.

set(_hexagon_clang "${HEXAGON_CLANG}")
if(_hexagon_clang STREQUAL "")
    find_program(_hexagon_clang NAMES hexagon-clang
        NO_CMAKE_FIND_ROOT_PATH)
endif()

if(NOT _hexagon_clang OR _hexagon_clang STREQUAL "_hexagon_clang-NOTFOUND")
    message(FATAL_ERROR
        "Hexagon DSP compiler was not found. Install or configure the Hexagon SDK, "
        "or set -DHEXAGON_CLANG=/path/to/hexagon-clang.")
endif()

if(NOT EXISTS "${_hexagon_clang}" OR IS_DIRECTORY "${_hexagon_clang}")
    message(FATAL_ERROR
        "HEXAGON_CLANG is not an executable file: ${_hexagon_clang}")
endif()

set(_hexagon_clang_dir "")
get_filename_component(_hexagon_clang_dir "${_hexagon_clang}" DIRECTORY)

set(_hexagon_ld "${HEXAGON_LD}")
if(_hexagon_ld STREQUAL "")
    find_program(_hexagon_ld NAMES ld.lld hexagon-ld.lld
        HINTS "${_hexagon_clang_dir}" NO_CMAKE_FIND_ROOT_PATH)
endif()

if(NOT _hexagon_ld OR _hexagon_ld STREQUAL "_hexagon_ld-NOTFOUND")
    message(FATAL_ERROR
        "Hexagon DSP linker was not found for ${_hexagon_clang}. Set "
        "-DHEXAGON_LD=/path/to/ld.lld (or hexagon-ld.lld).")
endif()

if(NOT EXISTS "${_hexagon_ld}" OR IS_DIRECTORY "${_hexagon_ld}")
    message(FATAL_ERROR
        "HEXAGON_LD is not an executable file: ${_hexagon_ld}")
endif()

set(_hexagon_probe_dir "${CMAKE_BINARY_DIR}/CMakeFiles")
file(MAKE_DIRECTORY "${_hexagon_probe_dir}")
set(_hexagon_probe_c  "${_hexagon_probe_dir}/base_test_hexagon_probe.c")
set(_hexagon_probe_o  "${_hexagon_probe_dir}/base_test_hexagon_probe.o")
set(_hexagon_probe_so "${_hexagon_probe_dir}/base_test_hexagon_probe.so")
file(WRITE "${_hexagon_probe_c}" "int base_test_hexagon_probe(void) { return 0; }\n")

execute_process(
    COMMAND "${_hexagon_clang}" --target=hexagon -mv${DSP_ARCH}
            -fsyntax-only -std=c11 "${_hexagon_probe_c}"
    RESULT_VARIABLE _hexagon_clang_target_status
    OUTPUT_VARIABLE _hexagon_clang_target_output
    ERROR_VARIABLE  _hexagon_clang_target_error
)
if(NOT _hexagon_clang_target_status EQUAL 0)
    file(REMOVE "${_hexagon_probe_c}" "${_hexagon_probe_o}" "${_hexagon_probe_so}")
    message(FATAL_ERROR
        "HEXAGON_CLANG cannot compile for Hexagon mv${DSP_ARCH}: "
        "${_hexagon_clang}\n${_hexagon_clang_target_error}")
endif()

execute_process(
    COMMAND "${_hexagon_clang}" --target=hexagon -mv${DSP_ARCH} -fPIC
            -c "${_hexagon_probe_c}" -o "${_hexagon_probe_o}"
    RESULT_VARIABLE _hexagon_clang_compile_status
    OUTPUT_VARIABLE _hexagon_clang_compile_output
    ERROR_VARIABLE  _hexagon_clang_compile_error
)
if(NOT _hexagon_clang_compile_status EQUAL 0)
    file(REMOVE "${_hexagon_probe_c}" "${_hexagon_probe_o}" "${_hexagon_probe_so}")
    message(FATAL_ERROR
        "HEXAGON_CLANG failed to produce a Hexagon object for mv${DSP_ARCH}: "
        "${_hexagon_clang}\n${_hexagon_clang_compile_error}")
endif()

execute_process(
    COMMAND "${_hexagon_ld}" -shared -soname=base_test_hexagon_probe.so
            -z separate-loadable-segments "${_hexagon_probe_o}"
            -o "${_hexagon_probe_so}"
    RESULT_VARIABLE _hexagon_ld_target_status
    OUTPUT_VARIABLE _hexagon_ld_target_output
    ERROR_VARIABLE  _hexagon_ld_target_error
)
if(NOT _hexagon_ld_target_status EQUAL 0)
    file(REMOVE "${_hexagon_probe_c}" "${_hexagon_probe_o}" "${_hexagon_probe_so}")
    message(FATAL_ERROR
        "HEXAGON_LD cannot link a Hexagon object: ${_hexagon_ld}\n"
        "${_hexagon_ld_target_error}")
endif()
file(REMOVE "${_hexagon_probe_c}" "${_hexagon_probe_o}" "${_hexagon_probe_so}")

message(STATUS
    "Hexagon DSP toolchain validated: compiler=${_hexagon_clang}, "
    "linker=${_hexagon_ld}, arch=mv${DSP_ARCH}")

set(_gen_dir  "${FASTRPC_BASE_TEST_IDL_INC_DIR}")
set(_impl_dir "${CMAKE_SOURCE_DIR}/idl/impl")
set(_bin_dir  "${CMAKE_BINARY_DIR}/bin")

set(_dsp_flags
    --target=hexagon
    -mv${DSP_ARCH}
    -fPIC
    -Wall
    -std=c11
    -D_POSIX_C_SOURCE=200112L
    -I${_gen_dir}
    -I${FASTRPC_INC_DIR}
)
if(BASE_TEST_WERROR)
    list(APPEND _dsp_flags -Werror)
endif()

add_custom_command(
    OUTPUT  "${_bin_dir}/fastrpc_base_test_skel.dsp.o"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_bin_dir}"
    COMMAND "${_hexagon_clang}" ${_dsp_flags}
            -c "${FASTRPC_BASE_TEST_SKEL_SOURCE}"
            -o "${_bin_dir}/fastrpc_base_test_skel.dsp.o"
    DEPENDS
        "${FASTRPC_BASE_TEST_SKEL_SOURCE}"
        "${FASTRPC_BASE_TEST_IDL_HEADER}"
    COMMENT "Compiling DSP skel: fastrpc_base_test_skel.c"
    VERBATIM
)

add_custom_command(
    OUTPUT  "${_bin_dir}/fastrpc_base_test_imp.dsp.o"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_bin_dir}"
    COMMAND "${_hexagon_clang}" ${_dsp_flags}
            -c "${_impl_dir}/fastrpc_base_test_imp.c"
            -o "${_bin_dir}/fastrpc_base_test_imp.dsp.o"
    DEPENDS
        "${_impl_dir}/fastrpc_base_test_imp.c"
        "${FASTRPC_BASE_TEST_IDL_HEADER}"
    COMMENT "Compiling DSP imp: fastrpc_base_test_imp.c"
    VERBATIM
)

add_custom_command(
    OUTPUT  "${_bin_dir}/libfastrpc_base_test_skel.so"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_bin_dir}"
    COMMAND "${_hexagon_ld}"
            -shared
            -soname=libfastrpc_base_test_skel.so
            -z separate-loadable-segments
            "${_bin_dir}/fastrpc_base_test_skel.dsp.o"
            "${_bin_dir}/fastrpc_base_test_imp.dsp.o"
            -o "${_bin_dir}/libfastrpc_base_test_skel.so"
    DEPENDS
        "${_bin_dir}/fastrpc_base_test_skel.dsp.o"
        "${_bin_dir}/fastrpc_base_test_imp.dsp.o"
    COMMENT "Linking DSP skel: libfastrpc_base_test_skel.so"
    VERBATIM
)

add_custom_target(fastrpc_base_test_skel ALL
    DEPENDS "${_bin_dir}/libfastrpc_base_test_skel.so"
)
add_dependencies(fastrpc_base_test_skel fastrpc_base_test_idl)

message(STATUS
    "fastrpc_base_test_skel target enabled (DSP arch: mv${DSP_ARCH})")

unset(_hexagon_clang)
unset(_hexagon_clang_dir)
unset(_hexagon_ld)
unset(_hexagon_probe_dir)
unset(_hexagon_probe_c)
unset(_hexagon_probe_o)
unset(_hexagon_probe_so)
unset(_hexagon_clang_target_status)
unset(_hexagon_clang_compile_status)
unset(_hexagon_ld_target_status)
unset(_hexagon_clang_target_output)
unset(_hexagon_clang_target_error)
unset(_hexagon_clang_compile_output)
unset(_hexagon_clang_compile_error)
unset(_hexagon_ld_target_output)
unset(_hexagon_ld_target_error)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# ============================================================================
# fastrpc_base_test — unified host binary
# ============================================================================
add_executable(fastrpc_base_test
    "${CMAKE_SOURCE_DIR}/root_all_tests.c"
    ${REGULAR_CATEGORY_DISPATCH_SOURCES}
)

# Keep the generated category dispatchers in the executable's dependency graph
# even when a category is empty; this also makes archive extraction explicit.
add_dependencies(fastrpc_base_test fastrpc_base_test_idl)

if(NOT REGULAR_CATEGORY_DISPATCH_SOURCES)
    message(FATAL_ERROR "No regular test category dispatchers were generated")
endif()

set_property(TARGET fastrpc_base_test APPEND PROPERTY LINK_DEPENDS
    ${REGULAR_CATEGORY_DISPATCH_SOURCES})

target_include_directories(fastrpc_base_test PRIVATE
    "${CMAKE_SOURCE_DIR}"
    "${FASTRPC_BASE_TEST_IDL_INC_DIR}"
)

target_link_libraries(fastrpc_base_test PRIVATE
    common_test_dep
)

if(ALL_SUITE_LIBS)
    target_link_libraries(fastrpc_base_test PRIVATE ${ALL_SUITE_LIBS})
endif()

target_link_options(fastrpc_base_test PRIVATE
    ${PLATFORM_LINK_FLAGS}
)

target_link_libraries(fastrpc_base_test PRIVATE
    base_test_warnings
)

set_target_properties(fastrpc_base_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
