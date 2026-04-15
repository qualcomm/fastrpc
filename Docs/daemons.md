# FastRPC Daemon Architecture

## Overview

FastRPC daemons (`adsprpcd`, `cdsprpcd`, `sdsprpcd`, `gdsprpcd`) are
optional system services that run on the CPU side (HLOS - High-Level
Operating System, i.e., Linux) and act as "default listeners" for DSP
static PDs (Protection Domains: isolated execution environments on the
DSP, such as root PD, audio PD, sensors PD). Each daemon corresponds to a
specific DSP type (ADSP, CDSP, SDSP, GDSP) available on the SoC and
handles reverse-RPC requests that require file-system access, memory
allocation, or privileged services the DSP cannot perform itself.

## Role in the Architecture

FastRPC can operate in two modes:

### Without Daemons (Basic Mode)
- Application processes can directly open FastRPC sessions to DSP via `/dev/fastrpc-*` devices
- Regular RPC calls work normally - applications can invoke DSP functions
- All standard FastRPC features are available:
  - Remote procedure calls (synchronous and asynchronous)
  - Memory mapping for zero-copy operation
  - Buffer passing between CPU and DSP
  - Dynamic library loading on DSP

**Limitations without daemons (for static PDs):**

- Static PDs cannot call back to system services on APPS (no reverse RPC
  for system services)
- Static PDs cannot request additional memory from APPS heap
- Static PDs cannot access files on the APPS filesystem
- Logging FARF (Fast Assert/Relay Framework - DSP logging system)
  messages from DSP to syslog is not available

**Limitations without daemons (for dynamic PDs):**

- Exception details of DSP dynamic PD are not logged in syslog without
  running the corresponding root daemon for the DSP (for example,
  adsprpcd for aDSP and cdsprpcd for CDSP)

### With Daemons (Full-Featured Mode)
The daemon acts as a privileged system process that:

1. **Registers as default listener**: Opens a persistent session and
   registers with the DSP static PD as the default handler for
   system-level callbacks
2. **Implements reverse RPC handlers for static PDs**: Provides skel
   implementations for DSP-to-APPS interfaces:
   - `adspmsgd` - DSP message logging interface
   - `apps_std` - File I/O operations (fopen, fread, fwrite, etc.)
   - `apps_mem` - Dynamic memory allocation from APPS (remote heap
     growth, CMA donation)
3. **Forwards DSP logs**: Receives FARF messages from DSP and writes
   them to syslog/dmesg
4. **Manages lifecycle**: Re-attaches after SSR (SubSystem Restart) and
   PDR (Process Domain Restart) events
5. **Caches DSP capabilities**: Only daemons can query and cache DSP
   capabilities in the kernel

**What works with daemons enabled:**
- All basic FastRPC operations (same as without daemon)
- Dynamic PD exception messages forwarded to system logs - critical for
  debugging crashes (exception logging is only supported for dynamic PDs
  by root PD daemons)
- Static PD FARF log output visible in dmesg/syslog
- Static PDs can open files on APPS filesystem (e.g., loading
  configuration files)
- Static PDs can request additional heap memory from APPS (audio PD use
  case)

**Important note**: Application code doesn't need to know if daemons are
running. The difference is observable only in static PD logging and
capabilities, not in the application RPC interface. Dynamic PDs
communicate directly with their APPS process and are not affected by
daemon presence.

## Protection Domains

Daemons can connect to different static protection domains (PDs) on the DSP:

### rootpd (Root PD / Guest OS)
- **Usage**: `cdsprpcd` or `cdsprpcd rootpd cdsp`
- **Purpose**:
  - Attaches to the DSP Root PD (Guest OS - typically QuRT, the Qualcomm
    Real-Time OS running on the DSP) and acts as the default listener for
    reverse-RPC
  - Provides file operations for components running in Root PD (dlopen is
    not supported on root PD due to security constraints; only file
    operations are permitted)
  - Routes dynamic PD exception logs to syslog (the primary client-facing
    use case)
  - Other use cases involve internal functionalities required by the
    FastRPC framework operating within the root PD on the DSP
  - Helps create/maintain Root PD process context (FastRPC does this automatically)
- **Services provided**:
  - Exception logging via `adspmsgd` framework (see [adspmsgd.md](adspmsgd.md))
  - Remote file system access via `apps_std` interface
- **If not running**:
  - Root PD features that rely on file I/O from HLOS will fail
  - Preload features may not work
  - Root PD-side reverse-RPC requests will time out
  - Exception logs from Root PD are lost
- **Typical use case**: Production systems, development, any scenario requiring error visibility

### audiopd (Audio PD - ADSP only)
- **Usage**: `adsprpcd audiopd adsp`
- **Purpose**:
  - Attaches to the Audio static PD and serves reverse-RPC for audio
    workloads
  - Performs dynamic loading of audio shared objects (.so)
  - Reads calibration/config files on behalf of audio PD
  - Handles remote heap growth / CMA donation flows needed by audio
- **Services provided**:
  - Dynamic memory allocation via `apps_mem` interface (see
    [apps_mem.md](apps_mem.md))
  - Remote file system access for audio configs/calibration
  - Dynamic module loading for audio algorithms
- **If not running**:
  - Audio dynamic module loading fails (dlopen/dlsym won't resolve)
  - Audio features/algorithms that depend on dynamically loaded code
    won't start
  - Remote heap allocation for audio PD is unavailable
- **Use case**: Audio PD when DSP heap needs dynamic expansion from APPS
  memory or dynamic loading of audio modules

## Implementation

The daemon binary (`src/dsprpcd.c`) dynamically loads the DSP-specific listener library:
- `libadsp_default_listener.so` → implements `adsp_default_listener_start()`
- `libcdsp_default_listener.so` → implements listener for CDSP
- Similar pattern for SDSP, GDSP

The listener implementation (`src/adsp_default_listener.c`):
1. Waits for `/dev/fastrpc-{dsp}` device availability
2. Opens remote handle to the specified PD
3. Registers as default listener via `adsp_default_listener_register()`
4. Polls for events (exceptions, file I/O requests)
5. Auto-restarts on errors except device unavailability

## Resource Usage

**Per daemon:**
- **1 FastRPC session slot** - Each daemon holds one persistent session
  open to the DSP. The user-space library enforces a per-process limit
  (`NUM_SESSIONS`, default 4 in `inc/fastrpc_common.h`). Since the
  daemon runs as a separate process, it uses 1 of its own 4-session
  quota and does not reduce the session pool available to application
  processes.
- **Memory footprint** (approximately):
  - Listener cache: 1 MB (`ADSP_LISTENER_MEM_CACHE_SIZE=1048576` in
    `src/adsp_default_listener.c`)
  - adspmsgd buffer: 256 KB (`DEFAULT_MEMORY_SIZE` in `src/adspmsgd.c`)
  - Additional overhead for threads, handles, and internal structures
  - Total: ~1-2 MB per daemon
- **CPU usage**: Event-driven, blocks waiting for DSP callbacks. Negligible CPU consumption except when processing DSP exceptions or log messages.

**When to run:**
- **Development/debugging**: Always recommended - dynamic PD exception logs disappear if root PD daemon is not run for that DSP
- **Production**: Recommended to run root PD daemon if dynamic PD exception visibility is needed
- **Resource-constrained**: Can safely omit if not using that DSP and logs aren't critical
- **Note**: Omitting the root PD daemon does not break application functionality, only dynamic PD exception visibility. Static PD daemons (like audiopd) are still needed if their specific functionality (file operations, memory allocation) is required.

## Systemd Integration

Service files in `files/*.service` define unit dependencies that ensure daemons start only when the corresponding FastRPC device is available:

```ini
[Unit]
After=dev-fastrpc-cdsp.device
ConditionPathExists=/dev/fastrpc-cdsp

[Service]
ExecStart=/usr/bin/cdsprpcd
Restart=on-failure
```

The daemon implementation (`src/adsp_default_listener.c`) actively waits for device node availability via `fastrpc_wait_for_device()` and auto-restarts on errors (except when the device is unavailable), making it suitable for early boot integration.

## References

- [adspmsgd.md](adspmsgd.md) - DSP message logging framework
- [apps_mem.md](apps_mem.md) - DSP memory allocation interface
- `src/dsprpcd.c` - Daemon entry point
- `src/adsp_default_listener.c` - Listener implementation
