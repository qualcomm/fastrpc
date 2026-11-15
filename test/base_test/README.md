# FastRPC `base_test`

`base_test` is a device-side AArch64 test runner for FastRPC. It calls the
QAIC-generated interface from the CPU and exercises the corresponding test
skeleton on a Hexagon DSP; it is not a host-side emulator.

## Prerequisites

- A supported AArch64 Linux or Android cross-toolchain.
- CMake 3.21 or newer and Ninja (for the standalone CMake build).
- QAIC, available as an executable file (`QAIC_EXECUTABLE` or `qaic` on `PATH`).
- A host AArch64 cross-toolchain selected with the standard `CC`, `CXX`, `AS`,
  `LD`, `AR`, `RANLIB`, and `STRIP` variables.
- A Hexagon compiler and linker for the DSP skeleton (`HEXAGON_CLANG` and
  `HEXAGON_LD`, or `hexagon-clang` and `ld.lld` on `PATH`). A host GCC does not
  replace the Hexagon compiler for the DSP-side library.
- A device with FastRPC drivers, a running DSP, and access to `/dev/fastrpc-*`.
- The Unity submodule:

  From the workspace root:

  ```sh
  git submodule update --init --recursive
  ```

Linux Autotools builds also require `libyaml` and `libbsd`. Android builds
require an Android NDK and `ANDROID_NDK_HOME`.

Cross-compilation uses the standard CMake toolchain-file mechanism. The
Linux toolchain file reuses the cross-tool variables from the root build when
they are set; the Android NDK toolchain selects its own tools based on the
configured ABI and API level.

For Android, set the NDK location:

```sh
export ANDROID_NDK_HOME=/path/to/android-ndk
```

Set the DSP-only tools explicitly when they are not on `PATH`:

```sh
export QAIC_EXECUTABLE=/path/to/qaic
export HEXAGON_CLANG=/path/to/hexagon-clang
export HEXAGON_LD=/path/to/ld.lld
# export DSP_ARCH=73  # optional; 73 is the default
```

Explicit `-D...` CMake values and `--with-qaic`/`--with-dsp-arch` Autotools
options take precedence over environment variables. `CMAKE_C_COMPILER`,
`CMAKE_CXX_COMPILER`, `CMAKE_AR`, `CMAKE_RANLIB`, `CMAKE_LINKER`, and
`CMAKE_STRIP` may be supplied explicitly when a CMake cache value is preferred.
Warnings are enabled with `-Wall -Wextra` but are not errors by default. Pass
`-DBASE_TEST_WERROR=ON` to a CMake configure command to make warnings fatal for
the CPU test targets and Hexagon DSP objects; omit it (or set it to `OFF`) to
keep warnings non-fatal.

## Build for Linux

### Option 1: `./gitcompile`

From the repository root:

```sh
./gitcompile --host=aarch64-linux-gnu \
  --enable-base-test
```

This builds FastRPC and then invokes the base-test CMake build. Run
`make base-test` separately when `GITCOMPILE_NO_MAKE=1` was used.

### Option 2: CMake

Build the FastRPC library first, then configure and build the test suite:

```sh
./gitcompile --host=aarch64-linux-gnu

cmake -S test/base_test -B test/base_test/builddir -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/test/base_test/cmake/toolchain/aarch64-linux-gnu.cmake"
cmake --build test/base_test/builddir
```

To build against an installed FastRPC rather than the in-tree build, pass its
installation prefix to CMake:

```sh
cmake -S test/base_test -B test/base_test/builddir -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/test/base_test/cmake/toolchain/aarch64-linux-gnu.cmake" \
  -DFASTRPC_PREFIX=/path/to/fastrpc-prefix
cmake --build test/base_test/builddir
```

The prefix must contain the public headers under `include/fastrpc/` and
`libcdsprpc` under `lib/` (or a platform-specific library directory).

## Build for Android

Set `ANDROID_NDK_HOME` as shown above before using either option.

### Option 1: `./gitcompile`

```sh
./gitcompile --host=aarch64-linux-android \
  --enable-base-test
```

### Option 2: CMake

Build the FastRPC library first, then configure and build the Android test
suite with the same Android API level used for the library and supported by the
target device and NDK. `android-35` below is an example; replace it if the
FastRPC library was built for a different API level:

```sh
./gitcompile --host=aarch64-linux-android

cmake -S test/base_test -B test/base_test/builddir -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-35
cmake --build test/base_test/builddir
```

## Expected artifacts

After a successful build:

```text
test/base_test/builddir/bin/fastrpc_base_test
test/base_test/builddir/bin/libfastrpc_base_test_skel.so
```

`fastrpc_base_test` is the AArch64 CPU-side runner. `libfastrpc_base_test_skel.so` is
the Hexagon DSP-side skeleton. QAIC-generated sources are placed under
`test/base_test/builddir/idl/generated/`.

## Deployment

The following example stages both artifacts in the shell-accessible temporary
directory on an Android device:

```sh
adb push test/base_test/builddir/bin/fastrpc_base_test /data/local/tmp/
adb push test/base_test/builddir/bin/libfastrpc_base_test_skel.so /data/local/tmp/
```

The FastRPC DSP loader must support the directory used for the skeleton; do
not assume `/data/local/tmp` is in the device's search path. If it is not, push
the skeleton to a supported DSP library directory and use that path below. If
the skeleton is already installed in a search directory, the second push is
unnecessary; installing `libcdsprpc.so` does not replace the DSP skeleton. The
runner, FastRPC library, skeleton, DSP architecture, and target domain must
come from compatible builds.

No device deployment or execution has been verified for these examples.

## Running the test

Run from the repository root, or adjust the paths accordingly:

```sh
adb shell 'DSP_LIBRARY_PATH=/data/local/tmp /data/local/tmp/fastrpc_base_test -d 3'
```

Use `-d` to select a domain. Without `-d`, the runner runs on all available DSP nodes.

### Sample output

```text
BEGIN DOMAIN
Name: cdsp
ID: 3

  TEST CalculatorE2E.SumAndMaximum
    Result: PASS

  TEST CalculatorE2E.EmptyInputsAreRejected
    Result: PASS

  TEST HapExampleE2E.FarfRuntimeLoggingCanBeConfigured
    Result: PASS

DOMAIN SUMMARY
Name: cdsp
ID: 3
Tests: 3
Passed: 3
Failed: 0
Skipped: 0
Result: PASS

END DOMAIN
============================================================

OVERALL SUMMARY
Tests: 3
Passed: 3
Failed: 0
Skipped: 0
Result: PASS
```

## CLI reference

```text
fastrpc_base_test [options]
```

| Option | Description |
|---|---|
| `-d DOMAIN` | Select a DSP domain; repeat to select multiple domains. |
| `-u 0\|1` | Disable or enable unsigned-PD control (default: `1`). |
| `-l tests\|groups\|tags` | List tests, groups, or tags and exit. |
| `--list-tests` | List test cases and exit. |
| `--list-groups` | List test groups and exit. |
| `--list-tags` | List registered tags and exit. |
| `--any-tags TAG` | Select a test matching at least one supplied tag; repeatable. |
| `--all-tags TAG` | Select a test matching every supplied tag; repeatable. |
| `-g GROUP` / `-G TEXT` | Select an exact group, or groups containing `TEXT`. |
| `-n CASE` / `-N TEXT` | Select an exact case, or cases containing `TEXT`. |
| `-r COUNT` | Repeat selected tests `COUNT` times. |
| `-v` | Show passing-test output. |
| `-s`, `--silent` | Hide passing-test output. |
| `-h`, `--help` | Print help and exit. |

Examples:

```sh
# Run CDSP.
./test/base_test/builddir/bin/fastrpc_base_test -d 3

# Run only the named case twice.
./test/base_test/builddir/bin/fastrpc_base_test \
  -d 3 -g CalculatorE2E -n SumAndMaximum -r 2

# Inspect test metadata without opening a DSP session.
./test/base_test/builddir/bin/fastrpc_base_test --list-tests
```
