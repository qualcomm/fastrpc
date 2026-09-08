# FastRPC Healthcheck: How Readiness Is Determined

This document explains how the **fastrpc-healthcheck** tool determines Digital Signal Processor (DSP)
readiness and FastRPC offload capability. It is intended for **users**, not internal
developers, and focuses on **what is checked**, **why it matters**, and **how to interpret
the final output**.

The tool supports both **Linux** and **Android** platforms.
While the high‑level checks are the same across platforms, the locations used
to discover firmware and DSP runtime components differ and are documented in
the sections below.

---

## 1. What Is fastrpc-healthcheck?

`fastrpc-healthcheck` is a diagnostic utility that evaluates whether DSPs on a system are:

- Available and running
- Ready for FastRPC offload from user space

The tool gathers information from the kernel, firmware, and user‑space libraries and
presents a concise summary table that users can rely on to understand FastRPC readiness.

---

## 2. High‑Level Decision Model

Each DSP is evaluated in two stages.

### DSP Online

A DSP is considered Online if all of the following conditions are true:

- It exists on the platform
- It is currently running
- Firmware is present

### DSP Offload‑Capable

An Online DSP is considered **Offload‑Capable** only if:

- required FastRPC shell binaries are present
- FastRPC user‑space libraries are present
- Firmware and runtime components are compatible
- Required FastRPC device nodes exist


On targets, missing or mismatched firmware/runtime build IDs
*always* block FastRPC offload.

**Note:** A DSP being Online does not automatically mean it is ready for FastRPC offload.

---

## 3. DSP Discovery (Kernel‑Level)

DSPs are discovered using the Linux `remoteproc` framework, exposed under:

`/sys/class/remoteproc/`

For each detected DSP, the tool reads:

- The DSP name (to identify which DSP it represents)
- The DSP state (to determine whether it is running)

A DSP is considered present if it has a matching `remoteproc` entry, and running if the
state contains `running`.

---

## 4. Firmware Detection

### 4.1 Firmware Location

For each running DSP, the firmware file used by the kernel is identified.
Firmware paths are normalized under a platform‑specific base location:

- **Linux:** `/lib/firmware/`
- **Android:** `/vendor/firmware_mnt/image/`

The firmware base path in use is printed once at the top of the output so users
can see where DSP firmware is being loaded from.


---

### 4.2 Firmware Build ID

If available, the tool extracts a firmware build ID embedded in the firmware binary. The
build ID is later used to verify compatibility with DSP runtime components.

---

## 5. DSP Base Path and Runtime Components

### 5.1 DSP Dynamic Libraries Path

DSP runtime components (such as FastRPC shell libraries) are
located using a platform‑specific base path.

- **Linux:**
  The path is derived from machine‑specific YAML configuration under:

  `/usr/share/qcom/conf.d/`

  For example:

  `/usr/share/qcom/<platform>/<variant>/dsp`

The YAML Configuration Usage Guide is documented separately in `fastrpc / Docs / conf_guideline.md`

- **Android:**
  DSP runtime components are discovered under:

  `/vendor/dsp`

The DSP dynamic libraries base path is printed once at the top of the output so
users know exactly where DSP runtime binaries are being sourced from.

---
### 5.2 Library Search Order

DSP runtime components are searched in the following order:

1. Command-line paths (`--lib-path`)
2. Platform configuration (YAML-derived paths on Linux)
3. Environment variables:
   - `DSP_LIBRARY_PATH`
   - DSP-specific environment variables
4. Platform defaults:
   - Linux: `/usr/lib/dsp`, `/usr/lib/rfsa/adsp`
5. (Android only) DSP-specific directories under `/vendor/dsp`

This priority ensures that user-specified paths override system configuration.

---

### 5.3 Custom Library Paths (Optional)

Users can override DSP library search paths using:

`--lib-path DSP:PATH`

---

### 5.4 Required DSP Modules

Within the DSP base path, the tool checks for:
- FastRPC shell binaries (`fastrpc_shell_*` and/or `fastrpc_shell_unsigned_*`)
If these components are missing, FastRPC offload is not possible even if the DSP is Online.

---

## 6. Signed and Unsigned Process Domains (PDs)

The tool distinguishes between:

- Signed process domains (Signed PDs)
- Unsigned process domains (Unsigned PDs)

Availability is reported separately in the summary table.

| Column        | Meaning |
|--------------|--------|
| SignedPD     | Signed process domains available |
| UnsignedPD*  | Unsigned process domains available |

**Note:** *On platforms that do not support unsigned PDs, shell binaries must be signed.

---

## 7. Firmware and Runtime Compatibility

`fastrpc-healthcheck` enforces strict compatibility between firmware and DSP runtime
components.

For FastRPC offload to be allowed:
- A firmware build ID **must** be present
- A DSP runtime (shell) build ID **must** be present
- The two build IDs **must match**

- Matching build IDs indicate compatibility
- A mismatch blocks FastRPC offload

The first successfully matched build ID is printed as a machine‑level summary for
reference.

---

## 8. FastRPC User‑Space Libraries

Even with a running DSP and valid firmware, FastRPC offload requires user‑space libraries
on the CPU side.

The tool checks standard user‑space library locations including:

- `LD_LIBRARY_PATH`
- **Linux:** `/usr/lib`
- **Android:** `/vendor/lib64`


If required FastRPC libraries are missing, FastRPC support is reported as unavailable.

---

## 9. DMA‑BUF Heap Availability

FastRPC commonly uses DMA‑BUF for shared memory allocation. The tool checks
for the presence of the system DMA‑BUF heap at:

/dev/dma_heap/system

This check is informational; the requirement is optional and is printed once for reference.

---

## 10. How to Read the Summary Table

The final summary table presents one row per DSP.

| Column           | Meaning |
|------------------|--------|
| DSP              | DSP identifier |
| State            | Online or Offline |
| SignedPD         | Signed PD support |
| UnsignedPD       | Unsigned PD support |
| FastRPC Support  | Yes or No (with reason) |

If FastRPC support is reported as **No**, the reason shown explains exactly which
requirement is missing.

---

## 11. Design Philosophy

The `fastrpc-healthcheck` tool is intentionally conservative and transparent:

- Online does not imply offload‑capable
- Missing or incompatible components are reported explicitly
- Kernel availability and user‑space readiness are evaluated independently

This design helps users quickly diagnose configuration issues and make informed deployment
decisions.

---

## 12. Summary

Use `fastrpc-healthcheck` to understand:

- Which DSPs are running
- Which DSPs are ready for FastRPC offload
- What technical blockers prevent offload when it is unavailable
