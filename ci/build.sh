#!/usr/bin/bash
# Copyright (c) Qualcomm Technologies, Inc.
# SPDX-License-Identifier: BSD-3-Clause-Clear

set -euo pipefail

echo "Installing dependencies..."
sudo apt-get update -y
sudo apt-get install -y --no-install-recommends \
  automake autoconf libtool pkg-config \
  gcc-aarch64-linux-gnu g++-aarch64-linux-gnu binutils-aarch64-linux-gnu \
  libyaml-dev \
  libyaml-0-2:arm64 libyaml-dev:arm64 \
  libbsd-dev:arm64

echo "Compiling for ARM64..."
export CC=aarch64-linux-gnu-gcc
export CXX=aarch64-linux-gnu-g++
export AS=aarch64-linux-gnu-as
export LD=aarch64-linux-gnu-ld
export RANLIB=aarch64-linux-gnu-ranlib
export STRIP=aarch64-linux-gnu-strip
export PKG_CONFIG_PATH=/usr/lib/aarch64-linux-gnu/pkgconfig

./gitcompile --host=aarch64-linux-gnu
