# Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#
# Platform: aarch64-linux-gnu  (clang/LLVM cross-compilation)
#
# Loaded automatically by the root CMakeLists.txt when
# TARGET_PLATFORM=linux (the default).
#
# Replaces:  cross/aarch64-linux-gnu.ini
#
# Do NOT include this file directly.  Use:
#   cmake --preset linux
#   cmake --build --preset linux

# ----------------------------------------------------------------------------
# System identity  (mirrors [host_machine] in the .ini)
# ----------------------------------------------------------------------------
set(CMAKE_SYSTEM_NAME      Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# ----------------------------------------------------------------------------
# Toolchain binaries  (mirrors [binaries] in the .ini)
# ----------------------------------------------------------------------------
set(CMAKE_C_COMPILER   clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_AR           llvm-ar    CACHE FILEPATH "Archiver")
set(CMAKE_RANLIB       llvm-ranlib CACHE FILEPATH "Ranlib")
set(CMAKE_LINKER       ld.lld     CACHE FILEPATH "Linker")
set(CMAKE_STRIP        llvm-strip  CACHE FILEPATH "Strip")

# ----------------------------------------------------------------------------
# Compile / link flags  (mirrors [built-in options] in the .ini)
#
# --target=aarch64-linux-gnu is the clang way to specify the cross triple.
# Equivalent to CC="clang --target=aarch64-linux-gnu" in the old Makefile.
# _INIT variables are set before project() runs so they seed the cache
# without overwriting flags the user may add later.
# ----------------------------------------------------------------------------
set(CMAKE_C_FLAGS_INIT          "--target=aarch64-linux-gnu")
set(CMAKE_EXE_LINKER_FLAGS_INIT "--target=aarch64-linux-gnu")

# ----------------------------------------------------------------------------
# Sysroot search behaviour
# Prevent CMake from accidentally resolving headers/libs from the build host.
# ----------------------------------------------------------------------------
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
