# Troubleshooting Guide

This document maps common FastRPC failure symptoms to their likely causes
and fixes. For general architecture, see [../README.md](../README.md); for
DSP/FastRPC configuration, see [conf_guideline.md](conf_guideline.md); for
daemon roles, see [daemons.md](daemons.md).

If you can't resolve an issue with this guide, please open an issue
following the [bug report template](../.github/ISSUE_TEMPLATE/bug-report.yaml),
including the details listed in the
[Bug Reporting Guidelines](../README.md#bug-reporting-guidelines).

---

## First step: is my board set up correctly at all?

Before diagnosing anything below, run:

```bash
fastrpc-healthcheck
```

This diagnostic reports, per DSP: whether it is online, whether signed/
unsigned PD shells are present, whether firmware and shell build IDs
match, and — if FastRPC offload is not available — the specific missing
requirement (e.g. "Missing FastRPC device node", "Firmware/Shell build
mismatch"). See `fastrpc-healthcheck(1)` (`man fastrpc-healthcheck`) for full
details on what it checks and how to interpret each column. Many of the
issues below will already be flagged by this tool before you need to dig
further.

---

## "Permission denied" opening `/dev/fastrpc-*`

**Symptom:** `remote_handle64_open()` (or any first FastRPC call) fails with
`EACCES`/`AEE_ECONNREFUSED`, or the device node itself is not readable by
your user.

**Cause:** The udev rules install `/dev/fastrpc-*` device nodes owned by
`root:fastrpc` with mode `0640`. Your application's user must belong to
the `fastrpc` group.

**Fix:**

```bash
usermod -aG fastrpc <username>
```

Then start a new login session (group membership changes do not apply to
already-running shells/processes). See
[Setting up the fastrpc group](../README.md#setting-up-the-fastrpc-group)
in the README for the full setup, including `/dev/dma_heap/system` access.

---

## `0x80000414` / `DSP_AEE_EUNSUPPORTED`

**Symptom:** A remote call returns error `0x80000414`.

**Cause:** `0x80000414` is `DSP_AEE_EOFFSET` (`0x80000400`, the base offset
for all DSP-side error codes, see `inc/AEEStdErr.h`) plus `AEE_EUNSUPPORTED`
(`0x014`, "API not implemented"). This means the specific API or feature
you invoked is not implemented or not supported by the DSP firmware you're
running against — it is not a transport or permission failure.

**Fix:** Check that the DSP firmware version on your target actually
supports the feature you're calling (some features are gated to newer
firmware revisions).

Note that some FastRPC library calls internally probe for optional
firmware/kernel capabilities and expect `DSP_AEE_EUNSUPPORTED` (or the
equivalent kernel `ENOTTY`/`EINVAL`) as a normal "not available on this
target" response; the library catches these internally and falls back to
older behavior automatically. So seeing this code in a trace/log during
session setup is not necessarily a problem — check whether your own call
returned it, versus whether it appeared only in an internal fallback path.

For other `DSP_AEE_*` codes, subtract `0x80000400` from the value and look
up the remainder in `inc/AEEStdErr.h`.

---

## "skel not found" / DSP module fails to load

**Symptom:** `remote_handle64_open()` fails, and DSP/kernel logs (or
`fastrpc-healthcheck`) show the shell or skel library could not be located.

**Cause:** FastRPC searches a specific, ordered list of directories for
DSP shell binaries and skel libraries; see
[FastRPC library and skel-file path preference](../README.md#fastrpc-library-and-skel-file-path-preference)
in the README for the exact 4-tier order (env var → Hexagon arch path →
YAML board path → generic fallback). If your skel/shell files are not in
one of these locations, or `DSP_LIBRARY_PATH`/`ADSP_LIBRARY_PATH` points
somewhere else than you expect, the load fails.

**Fix:**
- Confirm where your skel/shell files actually live, and either move them
  into one of the searched locations or set `DSP_LIBRARY_PATH` explicitly
  to that directory.
- If you're iterating on a new library and it's not being picked up, check
  whether the search-path cache (`DSP_SEARCH_PATHS_CACHE`, built once per
  domain in `build_dsp_search_path_cache_for_domain()`) was computed before
  your change — the cache is rebuilt on every domain (re-)initialization,
  so a fresh session picks up path changes automatically, but the arch/YAML
  portion of the path is fixed as of that domain's `domain_init()` call.
- For container deployments, see the container guidance in the same README
  section: set `DSP_LIBRARY_PATH` explicitly inside the container so the
  container's own artifacts are always preferred over host-installed ones.

---

## Signed vs. unsigned PD

**Symptom:** A DSP module fails to load with a connection-refused-style
error, and the log mentions offloading to a signed remote process.

**Cause:** By default, FastRPC attempts to load your shell/skel modules
into a **signed** process domain (PD) on the DSP, which requires the
binaries to carry a valid Qualcomm signature. If your module is only
signed for testing (or not signed at all), loading into the signed PD is
refused.

**Fix:** Request the unsigned PD explicitly before your first RPC call:

```c
struct remote_rpc_control_unsigned_module data;
data.domain = -1;   /* or a specific domain */
data.enable = 1;
remote_session_control(DSPRPC_CONTROL_UNSIGNED_MODULE, &data, sizeof(data));
```

Note this must be called before the session for that domain is opened —
`set_unsigned_pd_attribute()` in `src/fastrpc_apps_user.c` returns
`AEE_EALREADYLOADED` if the session is already up. Not all DSPs/targets
support unsigned PDs; `fastrpc-healthcheck`'s `UnsignedPD` column reports
this per DSP.

---

## Daemon not running / DSP exceptions not logged

**Symptom:** FastRPC calls otherwise work, but DSP exceptions are silent,
or dynamic PDs (audio PD, sensors PD) fail to load modules, or remote file
I/O from the DSP fails.

**Cause:** FastRPC's core RPC path (`remote_handle64_open`/`invoke`/`close`)
works without any daemon. The `adsprpcd`/`cdsprpcd`/`sdsprpcd`/`gdsprpcd`
daemons add: exception logging to the host, remote file I/O for DSP-side
code, and dynamic memory allocation services. Without the relevant daemon
running, these specific capabilities are unavailable, but plain RPC calls
still succeed.

**Fix:** Confirm the daemon for your DSP is running:

```bash
systemctl status cdsprpcd.service   # or adsprpcd/sdsprpcd/gdsprpcd
```

These are started automatically by udev when the corresponding FastRPC
device is detected — no manual `systemctl enable` is required (see the
README). If the daemon isn't running, check `journalctl -u cdsprpcd` for
why it failed to start. For the full daemon architecture and which PD each
daemon attaches to, see [daemons.md](daemons.md).
