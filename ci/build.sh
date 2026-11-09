#!/usr/bin/bash
# Copyright (c) Qualcomm Technologies, Inc.
# SPDX-License-Identifier: BSD-3-Clause

set -euo pipefail

echo "Configuring APT for amd64 + arm64 (ports)..."
# Detect Ubuntu codename
CODENAME="$(. /etc/os-release; echo "${VERSION_CODENAME}")"
: "${CODENAME:?Failed to read VERSION_CODENAME from /etc/os-release}"
echo "Detected Ubuntu codename: ${CODENAME}"

# 1) Enable ARM64 multiarch
sudo dpkg --add-architecture arm64

# 2) Overwrite main sources to be amd64-only (archive + security)
sudo tee /etc/apt/sources.list > /dev/null <<EOF
deb [arch=amd64] http://archive.ubuntu.com/ubuntu ${CODENAME} main restricted universe multiverse
deb [arch=amd64] http://archive.ubuntu.com/ubuntu ${CODENAME}-updates main restricted universe multiverse
deb [arch=amd64] http://archive.ubuntu.com/ubuntu ${CODENAME}-backports main restricted universe multiverse
deb [arch=amd64] http://security.ubuntu.com/ubuntu ${CODENAME}-security main restricted universe multiverse
EOF

# 3) Add Ubuntu Ports for arm64 only
sudo tee /etc/apt/sources.list.d/arm64-ports.list > /dev/null <<EOF
deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports ${CODENAME} main restricted universe multiverse
deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports ${CODENAME}-updates main restricted universe multiverse
deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports ${CODENAME}-backports main restricted universe multiverse
deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports ${CODENAME}-security main restricted universe multiverse
EOF

# 4) Remove deb822 sources that may still request arm64 from security.ubuntu.com
sudo rm -f /etc/apt/sources.list.d/ubuntu.sources || true

# 5) Clean and update indices (amd64 from archive/security; arm64 from ports)
sudo apt-get clean
echo "Updating apt indices..."
sudo apt-get update -y

echo "Installing dependencies..."
sudo apt-get install -y --no-install-recommends \
  abi-compliance-checker abi-dumper elfutils \
  automake autoconf libtool pkg-config \
  gcc-aarch64-linux-gnu g++-aarch64-linux-gnu binutils-aarch64-linux-gnu \
  libyaml-dev \
  libyaml-0-2:arm64 libyaml-dev:arm64 \
  libbsd-dev:arm64

echo "Sanity checks for ARM64 yaml pkg-config..."
ls -l /usr/include/yaml.h
ls -l /usr/lib/aarch64-linux-gnu/pkgconfig/yaml-0.1.pc

echo "Compiling for ARM64..."
export CC=aarch64-linux-gnu-gcc
export CXX=aarch64-linux-gnu-g++
export AS=aarch64-linux-gnu-as
export LD=aarch64-linux-gnu-ld
export RANLIB=aarch64-linux-gnu-ranlib
export STRIP=aarch64-linux-gnu-strip
export PKG_CONFIG_PATH=/usr/lib/aarch64-linux-gnu/pkgconfig

# These flags ensure complete debug info (useful for ABI analysis as in ref_build.sh)
export CFLAGS="-g -Og -fno-eliminate-unused-debug-types"
export CXXFLAGS="-g -Og -fno-eliminate-unused-debug-types"

./gitcompile --host=aarch64-linux-gnu
