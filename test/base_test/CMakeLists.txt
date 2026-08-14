# Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#
# ============================================================================
# base_test — CMake build root
# ============================================================================
#
# QUICK START
# -----------
#   git submodule update --init          # initialises vendor/unity
#
#   # Linux aarch64 (default)
#   cmake -B builddir \
#     -DFASTRPC_ROOT=/path/to/fastrpc \
#     -G Ninja
#   cmake --build builddir
#
#   # Android aarch64 (API 35)
#   cmake -B builddir \
#     -DTARGET_PLATFORM=android \
#     -DFASTRPC_ROOT=/path/to/fastrpc \
#     -DANDROID_NDK_HOME=/path/to/android-ndk \
#     -G Ninja
#   cmake --build builddir
#
# RUNNING TESTS
# -------------
#   ./bin/test-fastrpc                        # run all tests
#   ./bin/test-fastrpc -g DspHeapStress       # run one group
#   ./bin/test-fastrpc -v                     # verbose output
#
# ADDING A NEW FEATURE SUITE
# ---------------------------
#   1. Create test/feature/<name>/ with test_*.c, all_tests.c, and any
#      utility *.c / *.h files.
#   2. Define void run_<name>_feature_tests(void) in all_tests.c.
#   3. Add the extern declaration and call in root_all_tests.c.
#   4. Add the suite name to _feat_suites in test/feature/CMakeLists.txt.
#
# ADDING A NEW UNIT SUITE
# -----------------------
#   1. Create test/unit/<name>/ with test_*.c and all_tests.c.
#   2. Define void run_<name>_tests(void) in all_tests.c.
#   3. Add the extern declaration and call in root_all_tests.c.
#   4. Add the suite name to _unit_suites in test/unit/CMakeLists.txt.
#
# ADDING A NEW TEST TO AN EXISTING SUITE
# ---------------------------------------
#   1. Add test_<name>.c to test/feature/<suite>/ or test/unit/<suite>/.
#   2. Declare TEST_<Group>_GROUP_RUNNER in all_tests.c and call
#      RUN_TEST_GROUP(<Group>).
#   3. cmake --build builddir  (CONFIGURE_DEPENDS re-globs automatically).
# ============================================================================

cmake_minimum_required(VERSION 3.21)

# ============================================================================
# TARGET_PLATFORM — read before project() so the compiler is configured first
# ============================================================================
set(TARGET_PLATFORM "linux" CACHE STRING
    "Target platform to cross-compile for.  Choices: linux (default), android")

set(_valid_platforms linux android)
if(NOT TARGET_PLATFORM IN_LIST _valid_platforms)
    message(FATAL_ERROR
        "TARGET_PLATFORM='${TARGET_PLATFORM}' is not recognised.\n"
        "Valid choices: linux, android\n"
        "Example: cmake -B builddir -DTARGET_PLATFORM=android ...")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/cmake/platform/${TARGET_PLATFORM}.cmake")

# ============================================================================
# Project declaration
# ============================================================================
project(base_test
    VERSION     1.0.0
    LANGUAGES   C
    DESCRIPTION "FastRPC base test suite (user_heap, dspqueue)"
)

# ============================================================================
# Build type default
# ============================================================================
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING
        "Build type (Debug, Release, RelWithDebInfo, MinSizeRel)" FORCE)
endif()

# ============================================================================
# Language standard
# ============================================================================
set(CMAKE_C_STANDARD          11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS        OFF)

# ============================================================================
# User options
# ============================================================================
set(FASTRPC_ROOT "" CACHE PATH
    "Path to the fastrpc repository root (must contain inc/ and src/.libs/)")

set(ANDROID_NDK_HOME "" CACHE PATH
    "Path to the Android NDK root (required when TARGET_PLATFORM=android)")

set(DSP_ARCH "73" CACHE STRING
    "Hexagon DSP architecture version number (e.g. 68, 73, 75)")

# ============================================================================
# Resolve FASTRPC_ROOT
# ============================================================================
if(FASTRPC_ROOT STREQUAL "")
    # Default: three levels up — the fastrpc repo root when this project
    # lives at <fastrpc_root>/test/base_test.
    get_filename_component(_default_root
        "${CMAKE_SOURCE_DIR}/../.."
        ABSOLUTE
    )
    set(FASTRPC_ROOT "${_default_root}" CACHE PATH
        "Path to the fastrpc repository root (must contain inc/ and src/.libs/)"
        FORCE
    )
    message(STATUS "FASTRPC_ROOT not set — auto-detected: ${FASTRPC_ROOT}")
endif()

if(NOT EXISTS "${FASTRPC_ROOT}/src/.libs/libcdsprpc.so.1.0.0")
    message(FATAL_ERROR
        "libcdsprpc.so not found at: ${FASTRPC_ROOT}/src/.libs/libcdsprpc.so\n"
        "Build the fastrpc library first, or set -DFASTRPC_ROOT= correctly.\n"
        "  Linux:   ./gitcompile --host=aarch64-linux-gnu\n"
        "  Android: ./gitcompile --host=aarch64-linux-android")
endif()

set(FASTRPC_INC_DIR "${FASTRPC_ROOT}/inc")

# ============================================================================
# Platform flags
# ============================================================================
if(CMAKE_SYSTEM_NAME STREQUAL "Android")
    set(IS_ANDROID TRUE)
else()
    set(IS_ANDROID FALSE)
endif()

set(PLATFORM_C_DEFS _POSIX_C_SOURCE=200112L)
if(IS_ANDROID)
    list(APPEND PLATFORM_C_DEFS ANDROID)
endif()

if(IS_ANDROID)
    set(PLATFORM_LINK_FLAGS -pie)
else()
    set(PLATFORM_LINK_FLAGS -Wl,--dynamic-linker=/lib/ld-linux-aarch64.so.1)
endif()

# ============================================================================
# Global warning flags
# ============================================================================
add_compile_options(-Wall -Wextra -Werror)

# ============================================================================
# Unity
# ============================================================================
set(UNITY_EXTENSION_FIXTURE ON CACHE BOOL
    "Build Unity with the fixture extension" FORCE)
set(UNITY_EXTENSION_MEMORY  ON CACHE BOOL
    "Build Unity with the memory extension"  FORCE)

add_subdirectory(vendor/unity EXCLUDE_FROM_ALL)

set(UNITY_COMPILE_DEFS
    UNITY_OUTPUT_COLOR
    UNITY_INCLUDE_PRINT_FORMATTED
)
target_compile_definitions(unity PUBLIC ${UNITY_COMPILE_DEFS})

add_library(unity_dep INTERFACE)
target_link_libraries(unity_dep INTERFACE unity)

# ============================================================================
# IDL  →  fastrpc_test_stub target + FASTRPC_TEST_IDL_INC_DIR variable
# ============================================================================
add_subdirectory(idl)

# ============================================================================
# Utils + reporting  →  utils_dep INTERFACE target
# ============================================================================
add_subdirectory(utils)

# ============================================================================
# libcdsprpc
# ============================================================================
find_library(LIBCDSPRPC_PATH
    NAMES cdsprpc
    PATHS "${FASTRPC_ROOT}/src/.libs"
    NO_DEFAULT_PATH
    NO_CMAKE_FIND_ROOT_PATH
)

if(LIBCDSPRPC_PATH)
    add_library(cdsprpc SHARED IMPORTED GLOBAL)
    set_target_properties(cdsprpc PROPERTIES
        IMPORTED_LOCATION "${LIBCDSPRPC_PATH}"
    )
else()
    message(WARNING
        "find_library could not locate libcdsprpc.so — "
        "falling back to raw -L/-l linker flags")
    add_library(cdsprpc INTERFACE)
    target_link_options(cdsprpc INTERFACE
        "-L${FASTRPC_ROOT}/src/.libs"
        "-lcdsprpc"
    )
endif()

add_library(fastrpc_dep INTERFACE)
target_include_directories(fastrpc_dep INTERFACE "${FASTRPC_INC_DIR}")
target_link_libraries(fastrpc_dep INTERFACE cdsprpc)

# ============================================================================
# libm
# ============================================================================
find_library(LIBM_PATH m)
add_library(libm_dep INTERFACE)
if(LIBM_PATH)
    target_link_libraries(libm_dep INTERFACE "${LIBM_PATH}")
endif()

# ============================================================================
# common_test_iface — shared include dirs + compile flags
# ============================================================================
add_library(common_test_iface INTERFACE)

target_include_directories(common_test_iface INTERFACE
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/test/feature"
    "${CMAKE_SOURCE_DIR}/test/unit"
    "${FASTRPC_TEST_IDL_INC_DIR}"
)

target_compile_definitions(common_test_iface INTERFACE
    ${PLATFORM_C_DEFS}
    ${UNITY_COMPILE_DEFS}
)

target_compile_options(common_test_iface INTERFACE
    "-include${CMAKE_SOURCE_DIR}/utils/reporting/unity/unity_fixture_file_output.h"
)

# ============================================================================
# common_test_dep — aggregate dependency for every host binary
# ============================================================================
add_library(common_test_dep INTERFACE)
target_link_libraries(common_test_dep INTERFACE
    common_test_iface
    unity_dep
    utils_dep
    fastrpc_dep
    fastrpc_test_stub
    libm_dep
)

# ============================================================================
# Per-suite static libraries + per-group CTest executables
# ============================================================================
set(ALL_SUITE_LIBS "")

add_subdirectory(test/unit)
add_subdirectory(test/feature)

# ============================================================================
# Main deployable artifacts  →  <builddir>/bin/
# ============================================================================
add_subdirectory(bin)

# ============================================================================
# CTest
# ============================================================================
enable_testing()
