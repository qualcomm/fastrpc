# DSP Check: How Readiness Is Determined

This document explains how the **dsp_check** tool determines Digital Signal Processor (DSP)
readiness and FastRPC offload capability. It is intended for **users**, not internal
developers, and focuses on **what is checked**, **why it matters**, and **how to interpret
the final output**.

---

## 1. What Is dsp_check?

`dsp_check` is a diagnostic utility that evaluates whether DSPs on a system are:

- Available and running
- Ready for FastRPC offload from user space

The tool gathers information from the kernel, firmware, and user‑space libraries and
presents a concise summary table that users can rely on to understand FastRPC readiness.

---

## 2. High‑Level Decision Model

Each DSP is evaluated in two stages.

### DSP Online

A DSP is considered **Online** if:

- It exists on the platform
- It is currently running
- Firmware is present

### DSP Offload‑Capable

An Online DSP is considered **Offload‑Capable** only if:

- Required DSP runtime modules are available
- FastRPC user‑space libraries are present
- Firmware and runtime components are compatible
- Required FastRPC device nodes exist

**Note:** A DSP being Online does not automatically mean it is ready for FastRPC offload.

---

## 3. DSP Discovery (Kernel‑Level)

DSPs are discovered using the Linux `remoteproc` framework, exposed under:

/sys/class/remoteproc/

For each detected DSP, the tool reads:

- The DSP name (to identify which DSP it represents)
- The DSP state (to determine whether it is running)

A DSP is considered present if it has a matching `remoteproc` entry, and running if the
state contains `running`.

---

## 4. Firmware Detection

### 4.1 Firmware Location

For each running DSP, the firmware file used by the kernel is identified. Firmware paths
are normalized under the standard location:


/lib/firmware/

This firmware base path is printed once at the top of the output so users know where DSP
firmware is being loaded from.

---

### 4.2 Firmware Build ID

If available, the tool extracts a firmware build ID embedded in the firmware binary. The
build ID is later used to verify compatibility with DSP runtime components.

---

## 5. DSP Base Path and Runtime Components

### 5.1 DSP Base Path

DSP runtime components (such as shells and skeleton libraries) are located using
machine‑specific YAML configuration under:


/usr/share/qcom/conf.d/

From this configuration, a DSP base path is derived, for example:


/usr/share/qcom////dsp

This path is printed once so users know exactly where DSP runtime binaries are being
sourced from.

---

### 5.2 Required DSP Modules

Within the DSP base path, the tool checks for:

- FastRPC shell binaries
- Skeleton (`*_skel.so`) libraries
- Supporting DSP runtime libraries

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

**Note:** On platforms that do not support unsigned PDs, unsigned binaries must be signed
before they can be executed.

---

## 7. Firmware and Runtime Compatibility

If both a firmware build ID and a DSP runtime (shell) build ID are available, `dsp_check`
verifies that they match.

- Matching build IDs indicate compatibility
- A mismatch blocks FastRPC offload

The first successfully matched build ID is printed as a machine‑level summary for
reference.

---

## 8. FastRPC User‑Space Libraries

Even with a running DSP and valid firmware, FastRPC offload requires user‑space libraries
on the CPU side.

The tool checks standard library locations including:

- `LD_LIBRARY_PATH`
- `/usr/lib`

If required FastRPC libraries are missing, FastRPC support is reported as unavailable.

---

## 9. DMA‑BUF Heap Availability

FastRPC relies on DMA‑BUF for shared memory allocation. The tool checks for the presence of
the system DMA‑BUF heap at:


/dev/dma_heap/system

This result is printed once and applies globally to all DSPs.

---

## 10. How to Read the Summary Table

The final summary table presents one row per DSP.

| Column           | Meaning |
|------------------|--------|
| DSP              | DSP identifier |
| State            | Online or Offline |
| SignedPD         | Signed PD support |
| UnsignedPD*      | Unsigned PD support |
| FastRPC Support  | Yes or No (with reason) |

If FastRPC support is reported as **No**, the reason shown explains exactly which
requirement is missing.

---

## 11. Design Philosophy

The `dsp_check` tool is intentionally conservative and transparent:

- Online does not imply offload‑capable
- Missing or incompatible components are reported explicitly
- Kernel availability and user‑space readiness are evaluated independently

This design helps users quickly diagnose configuration issues and make informed deployment
decisions.

---

## 12. Summary

Use `dsp_check` to understand:

- Which DSPs are running
- Which DSPs are ready for FastRPC offload
- What technical blockers prevent offload when it is unavailable