# base_test

Feature test suite for the Qualcomm FastRPC library.

Tests run on the **host CPU** and exercise the DSP over FastRPC.  The build
produces two deployable artifacts: a host test binary (`test-fastrpc`) and a
Hexagon DSP shared library (`libfastrpc_test_skel.so`).  Both must be present
on the target device for the tests to run.

---

## Table of Contents

1. [Repository layout](#repository-layout)
2. [Prerequisites](#prerequisites)
3. [Step 1 — Build the fastrpc library](#step-1--build-the-fastrpc-library)
4. [Step 2 — Generate IDL stubs with QAIC](#step-2--generate-idl-stubs-with-qaic)
5. [Step 3 — Build with CMake](#step-3--build-with-cmake)
6. [Build artifacts](#build-artifacts)
7. [Running the tests](#running-the-tests)
8. [Adding tests](#adding-tests)
9. [Contributing — rules and policies](#contributing--rules-and-policies)
   - [Linux kernel coding style](#linux-kernel-coding-style)

---

## Repository layout

```
test/base_test/
├── CMakeLists.txt                  # build root
├── root_all_tests.c                # main() — dispatches all suite runners
│
├── cmake/
│   └── platform/
│       ├── linux.cmake             # aarch64-linux-gnu toolchain
│       └── android.cmake           # aarch64-linux-android NDK toolchain
│
├── idl/
│   ├── fastrpc_test.idl            # interface definition (source of truth)
│   ├── inc/                        # IDL include files (AEEStdDef, remote)
│   ├── generated/                  # QAIC output — do not edit by hand
│   │   ├── fastrpc_test.h
│   │   ├── fastrpc_test_stub.c     # CPU-side RPC stub (compiled into host binary)
│   │   └── fastrpc_test_skel.c     # DSP-side dispatch skeleton (compiled into .so)
│   └── impl/
│       └── fastrpc_test_imp.c      # DSP-side method implementations
│
├── utils/                          # shared test infrastructure
│   ├── fastrpc_utils/              # DSP domain helpers, error strings, CLI parsing
│   ├── log_capture/                # kernel / logcat log capture
│   ├── reporting/
│   │   ├── allure/                 # Allure XML result writer
│   │   └── unity/                  # Unity fixture output hooks
│   └── xml_writer/                 # low-level XML streaming library
│
├── test/
│   ├── feature/
│   │   └── dspqueue/              # DSPQueue tests
│   │       └── all_tests.c        # suite entry point
│   └── unit/
│       └── dspqueue/
|           └── all_tests.c
│
├── bin/
│   └── CMakeLists.txt              # links test-fastrpc + builds DSP skel
│
├── scripts/
│   └── generate_report.sh          # post-run Allure report helper
│
└── vendor/
    └── unity/                      # ThrowTheSwitch/Unity (git submodule)
```

---

## Prerequisites

| Tool | Version | Purpose |
|------|---------|---------|
| CMake | ≥ 3.21 | Build system |
| Ninja | any | Recommended generator |
| clang / clang++ | any recent | Cross-compiler for aarch64 |
| llvm-ar, llvm-ranlib, ld.lld, llvm-strip | matching clang | LLVM toolchain utilities |
| QAIC | Hexagon SDK | IDL stub/skel code generation |
| Android NDK | r25c or later | Android target only |

Initialise the Unity submodule before the first build:

```bash
git submodule update --init
```

---

## Step 1 — Build the fastrpc library

`base_test` links against `libcdsprpc.so` from the fastrpc repository root.
Build it first:

```bash
# Linux (aarch64)
cd /path/to/fastrpc
./gitcompile --host=aarch64-linux-gnu

# Android (aarch64)
./gitcompile --host=aarch64-linux-android
```

The build expects the library at:

```
<fastrpc_root>/src/.libs/libcdsprpc.so
```

---

## Step 2 — Generate IDL stubs with QAIC

The files under `idl/generated/` are produced by the **QAIC** compiler from
`idl/fastrpc_test.idl`.  They are checked into the repository so a Hexagon SDK
installation is not required for a normal build.  Regenerate them only when
`fastrpc_test.idl` changes.

```bash
# From the idl/ directory
qaic -st \
     -I inc/ \
     fastrpc_test.idl
mv fastrpc_test.h         generated/
mv fastrpc_test_stub.c    generated/
mv fastrpc_test_skel.c    generated/
```

> **Note:** Do not edit the files in `idl/generated/` by hand.  All interface
> changes must be made in `fastrpc_test.idl` and regenerated with QAIC.

The three generated files serve distinct roles:

| File | Compiled into | Purpose |
|------|--------------|---------|
| `fastrpc_test.h` | both | Shared API declarations |
| `fastrpc_test_stub.c` | `test-fastrpc` (host) | Marshals CPU→DSP calls over FastRPC |
| `fastrpc_test_skel.c` | `libfastrpc_test_skel.so` (DSP) | Dispatches incoming RPC to `fastrpc_test_imp.c` |

---

## Step 3 — Build with CMake

### Linux — aarch64-linux-gnu

```bash
cmake -B builddir \
      -DFASTRPC_ROOT=/path/to/fastrpc \
      -G Ninja

cmake --build builddir
```

### Android — aarch64-linux-android (API 35)

```bash
cmake -B builddir \
      -DTARGET_PLATFORM=android \
      -DFASTRPC_ROOT=/path/to/fastrpc \
      -DANDROID_NDK_HOME=/path/to/android-ndk \
      -G Ninja

cmake --build builddir
```

### CMake options

| Option | Default | Description |
|--------|---------|-------------|
| `TARGET_PLATFORM` | `linux` | Target platform: `linux` or `android` |
| `FASTRPC_ROOT` | auto-detected | Path to the fastrpc repository root |
| `ANDROID_NDK_HOME` | — | Path to the Android NDK root (android only) |
| `DSP_ARCH` | `73` | Hexagon DSP architecture version (e.g. `68`, `73`, `75`) |
| `CMAKE_BUILD_TYPE` | `RelWithDebInfo` | `Debug`, `Release`, `RelWithDebInfo`, `MinSizeRel` |

`FASTRPC_ROOT` is auto-detected as `../..` relative to this directory when not
set explicitly, which is correct when `base_test` lives at
`<fastrpc_root>/test/base_test`.

---

## Build artifacts

Both artifacts are placed in `<builddir>/bin/`.

### `test-fastrpc` — host test binary

The CPU-side test executable.  Links together:

- `root_all_tests.c` — `main()`, initialises reporting, dispatches suite runners
- `feature_user_heap_all` — static library containing all `user_heap` test sources
- `fastrpc_test_stub` — QAIC-generated CPU→DSP RPC stub
- `utils` — reporting, log capture, and FastRPC helper utilities
- `unity` — ThrowTheSwitch/Unity test framework (fixture + memory extensions)
- `libcdsprpc.so` — FastRPC runtime (dynamically linked)

Push to the target device and run directly:

```bash
adb push builddir/bin/test-fastrpc /data/local/tmp/
adb shell /data/local/tmp/test-fastrpc
```

### `libfastrpc_test_skel.so` — Hexagon DSP shared library

The DSP-side skel, compiled with `--target=hexagon -mv<DSP_ARCH>`.  Built
automatically when `clang` is found in `PATH`; skipped silently otherwise
(the host binary is unaffected).

Contains:

- `fastrpc_test_skel.c` — QAIC-generated RPC dispatch table
- `fastrpc_test_imp.c` — DSP-side method implementations

Push to the DSP library search path before running the host binary:

```bash
adb push builddir/bin/libfastrpc_test_skel.so /usr/lib/rfsa/adsp/
# or set DSP_LIBRARY_PATH at runtime:
adb shell "DSP_LIBRARY_PATH=/data/local/tmp /data/local/tmp/test-fastrpc"
```

---

## Running the tests

```bash
# Run all tests
./bin/test-fastrpc

# Run a specific test group
./bin/test-fastrpc -g DspHeapStress

# Run a specific test case
./bin/test-fastrpc -g DspHeapStress -n SmallFixedSize

# Verbose output (prints each test name as it runs)
./bin/test-fastrpc -v

# Target a specific DSP domain (default: CDSP = 3)
./bin/test-fastrpc -d 3

# Disable unsigned PD (for signed skels)
./bin/test-fastrpc -u 0

# Filter by tag (OR — run tests that have any of these tags)
./bin/test-fastrpc --any-tags feature positive

# Filter by tag (AND — run tests that have all of these tags)
./bin/test-fastrpc --all-tags UserHeap dsp_heap_stress
```

---

## Adding tests

### Add a new test case to an existing group

1. Open `test/feature/user_heap/test_dsp_heap_stress.c`.
2. Add a `TEST(DspHeapStress, MyNewCase) { ... }` block.
3. Register it in `TEST_GROUP_RUNNER(DspHeapStress)` with
   `RUN_TEST_CASE(DspHeapStress, MyNewCase)`.
4. Optionally annotate with `TEST_CASE_TAGS(DspHeapStress, MyNewCase, "tag1", "tag2")`.
5. Rebuild — no CMake changes needed.

### Add a new test group to the `user_heap` suite

1. Create `test/feature/user_heap/test_<name>.c` following the pattern of
   `test_dsp_heap_stress.c`:
   - `TEST_GROUP(<GroupName>)`
   - `TEST_SETUP` / `TEST_TEAR_DOWN`
   - One or more `TEST(<GroupName>, <CaseName>)` blocks
   - `TEST_GROUP_RUNNER(<GroupName>)` calling each `RUN_TEST_CASE`
2. In `test/feature/user_heap/all_tests.c`:
   - Add `void TEST_<GroupName>_GROUP_RUNNER(void);` (no `extern` — see
     [Linux kernel coding style](#linux-kernel-coding-style))
   - Add `RUN_TEST_GROUP(<GroupName>);` inside `run_user_heap_feature_tests()`
3. Rebuild — CMake picks up the new `test_*.c` automatically via
   `CONFIGURE_DEPENDS` glob.

### Add a new feature suite

1. Create `test/feature/<suite_name>/` containing:
   - `all_tests.c` — defines `void run_<suite_name>_feature_tests(void)`
   - One or more `test_*.c` files
   - Any utility `*.c` / `*.h` files needed by the suite
2. In `root_all_tests.c`:
   - Add `void run_<suite_name>_feature_tests(void);` (no `extern` — see
     [Linux kernel coding style](#linux-kernel-coding-style))
   - Call it inside `run_all_tests()`
3. In `test/feature/CMakeLists.txt`:
   - Append `<suite_name>` to the `_feat_suites` list
4. Rebuild.

### Add a new IDL method

1. Edit `idl/fastrpc_test.idl` — add the method declaration.
2. Regenerate with QAIC (see [Step 2](#step-2--generate-idl-stubs-with-qaic)).
3. Implement the method in `idl/impl/fastrpc_test_imp.c`.
4. Rebuild.

---

## Contributing — rules and policies

### Code style

- **Language standard:** C11 (`-std=c11`).  No compiler extensions
  (`CMAKE_C_EXTENSIONS OFF`).
- **Warnings are errors:** `-Wall -Wextra -Werror`.  Every new file must
  compile cleanly with no warnings.
- **Formatting is enforced by `.clang-format`.**  Run `clang-format -i` on
  every changed `.c`/`.h` file before committing; CI treats a non-conforming
  file as a build failure.  Key points from the config:
  - **Indentation:** 4 spaces.  No tabs (`UseTab: Never`).
  - **Line length:** 100 columns hard limit (`ColumnLimit: 100`).
  - **Braces:** Linux style — the opening brace goes on its own line for
    function bodies, but on the same line as `if`/`for`/`while`/`switch`/
    `struct`/`enum`.  Single-statement bodies without braces are allowed
    (`if (!c) return AEE_EBADPARM;`-style is reformatted to a line break,
    not collapsed onto one line).
  - **Pointers:** right-aligned (`int *result`, not `int* result` or
    `int * result`).
  - **`switch` statements:** every `case` gets its own line and its own
    `break;` line — no collapsing multiple cases or case+break onto a
    single line.
  - **`#include` ordering:** within each block, includes are sorted
    case-sensitively; local (`"..."`) headers before system (`<...>`)
    headers, each block separated by a blank line.
  - **No manual alignment** of trailing `=`, trailing comments, or struct
    members — clang-format decides spacing; don't hand-pad columns.
- **Naming:**
  - Functions and variables: `lower_snake_case`
  - Macros and constants: `UPPER_SNAKE_CASE`
  - Test groups: `UpperCamelCase` (Unity convention)
  - Test cases: `UpperCamelCase` (Unity convention)

### Linux kernel coding style

`base_test` follows the
[Linux kernel coding style](https://docs.kernel.org/process/coding-style.html),
with three deliberate exceptions:

1. **Indentation:** 4 spaces, no tabs (kernel uses 8-column tabs) — enforced
   by `.clang-format` (`IndentWidth: 4`, `UseTab: Never`).
2. **Line length:** 100 columns (kernel prefers 80) — enforced by
   `.clang-format` (`ColumnLimit: 100`).
3. **Comments:** existing Doxygen-style tags (`@brief` / `@param` / `@return`
   / `@file`) are retained rather than converted to kernel-doc format.
4. **Struct/enum typedefs:** existing `typedef struct { ... } foo_t;` /
   `typedef enum { ... } foo_t;` usage is retained as-is; new code may
   continue to use this pattern rather than bare `struct foo` / `enum foo`.

Everything else in the kernel doc applies as written:

- **No `extern` keyword on function prototypes** — only on variables that
  need external linkage.
- **One declaration per line** — no multiple statements or multiple
  assignments/declarations packed onto a single line.
- **`goto` is permitted** for functions with multiple exit paths needing
  common cleanup, using descriptive labels (`out_free_buffer:`,
  `err_free_foo:`), never generic `err1:` / `err2:`.
- **Brace placement, spacing, pointer alignment, and `switch`/`case`
  formatting** are enforced via `.clang-format` (`BreakBeforeBraces: Linux`,
  `PointerAlignment: Right`, `AllowShortCaseLabelsOnASingleLine: false`) —
  see [Code style](#code-style) above.
- **Naming stays `lower_snake_case`** for functions/variables (no camelCase,
  no Hungarian notation) — already a documented rule above, now also
  inherited from kernel style.

### File headers

Every new `.c` and `.h` file must begin with the SPDX licence header:

```c
// Copyright (c), Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
```

Every new `CMakeLists.txt` must begin with:

```cmake
# Copyright (c), Qualcomm Innovation Center, Inc. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
```

### Test design rules

- **One `TEST_GROUP` per file.**  Each `test_*.c` file defines exactly one
  `TEST_GROUP` and its `TEST_GROUP_RUNNER`.
- **Independent tests.**  Every test must be able to run in isolation.
  `TEST_SETUP` opens a fresh DSP session; `TEST_TEAR_DOWN` closes it
  unconditionally.  Tests must not share mutable state across cases.
- **Always call `REPORT_ERROR_CODE(ret)`** immediately after any FastRPC API
  call.  This records the return value for the Allure report.
- **Use `TEST_IGNORE_MESSAGE`** (not `TEST_FAIL`) when a DSP session cannot be
  opened.  Hardware absence is not a test failure.
- **Annotate every test case** with `TEST_CASE_TAGS` so tag-based filtering
  works.  Minimum required tags: feature classification (e.g. `"feature"`),
  polarity (`"positive"` or `"negative"`), and a functional tag
  (e.g. `"dsp_heap_stress"`).
- **Annotate every test group** with `TEST_GROUP_META` to populate Allure
  labels (layer, epic, feature, story).
- **No `printf` in `TEST_SETUP` / `TEST_TEAR_DOWN`** beyond the session open/
  close lines already established.  Diagnostic output belongs in the test body.
- **Elapsed time must be reported** for any test that calls a timed DSP method.
  Use the `PRIu64` format specifier for `uint64_t` values.

### IDL rules

- **Do not edit `idl/generated/` by hand.**  All changes must go through
  `fastrpc_test.idl` and be regenerated with QAIC.
- **Bump `IDL_VERSION`** in `fastrpc_test.idl` whenever a method is added,
  removed, or its signature changes.  The skel performs a version check at
  `open()` time and returns `AEE_ESTUBSKELVERMISMATCH` on mismatch.
- **DSP implementations** in `fastrpc_test_imp.c` must use only
  `stdlib` / `string.h` / `math.h` / `clock_gettime()`.  No HAP or QuRT APIs.

### CMake rules

- **Do not add `include_directories()` or `add_compile_options()` at the
  directory level.**  Use `target_*` commands exclusively so that flags are
  scoped to the targets that need them.
- **New suites** are registered by appending to `_feat_suites` in
  `test/feature/CMakeLists.txt`.  No other CMake file needs to change.
- **`CONFIGURE_DEPENDS`** is set on all `file(GLOB ...)` calls.  Adding or
  removing a `test_*.c` file triggers an automatic CMake re-run on the next
  build; no manual CMake invocation is needed.

### Git hygiene

- **Never commit build artefacts** (`builddir/`, `*.o`, `*.so`, `*.a`).
- **Never commit the generated IDL files** if they were regenerated solely to
  reformat whitespace.  Only commit `idl/generated/` changes that result from
  a real IDL version bump.
- **One logical change per commit.**  Separate test additions from
  infrastructure changes.
- **Commit message format:**

  ```
  component: short imperative summary (≤ 72 chars)

  Optional body explaining why, not what.  Wrap at 72 columns.
  ```

  Examples:
  ```
  user_heap: add RapidCycles stress test
  idl: bump IDL_VERSION to 1.2.5, add malloc_free_stress method
  utils: remove csv reporting from base_test
  ```
