#!/usr/bin/env bash
# Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#
# generate_report.sh — Host-side Allure 3 report generation with history support
#
# Run this script after manually pulling test results from the device:
#
#   adb pull /data/local/tmp/test-results/ test-results/
#   ./scripts/generate_report.sh
#
# What this script does:
#   Step 4.5 Read environment.properties written by the C runtime on the device
#            and extract the environment name ("<device>-<fastrpc-version>").
#            Write a temporary allurerc.mjs with that name as the environment
#            key so the Environments dropdown shows the real device name.
#   Step 5   allure generate test-results/ -o allure-report/ --config=<tmp>
#            allurerc.mjs tells Allure 3 to read from and append to
#            allure-history.jsonl automatically — no manual copy steps needed.
#   Step 6   allure open allure-report/  (skipped with --no-open)
#
# Usage:
#   ./scripts/generate_report.sh [--no-open] [--help]
#
# Options:
#   --no-open   Generate the report but do not launch the browser.
#   --help      Print this message and exit.
#
# How Allure 3 history works (different from Allure 2):
#   History is configured entirely in allurerc.mjs via the 'historyPath' key.
#   On every run, allure generate:
#     1. Reads allure-history.jsonl (if it exists) to load all previous runs.
#     2. Appends a new line with the current run's data to allure-history.jsonl.
#     3. Writes the full report including trend charts to allure-report/.
#   There are no manual inject/persist copy steps — Allure 3 manages the file.
#
# How environment support works:
#   The C runtime writes test-results/environment.properties on the device:
#     Device=<device-name>          # e.g. iq-9075-evk
#     FastRPC.Version=<version>     # e.g. 1.0.0
#   Allure 3 reads this file automatically and displays its key-value pairs in
#   the report's Metadata section.
#
#   The C runtime also injects a {"name":"host","value":"<device>-<version>"}
#   label into every <uuid>-result.json file.
#
#   Allure 3's matchEnvironment() returns the CONFIG KEY of the first matching
#   environments entry — not the label value.  So to show the real device name
#   in the Environments dropdown, the config key must equal the device name.
#   Since device names are dynamic, this script generates a temporary
#   allurerc.mjs with the actual device name as the key, passes it to
#   allure generate via --config, then deletes it.
#
# Directory layout (all paths relative to test/fastrpc-test/):
#
#   test-results/          Raw result files pulled from the device.
#                          Contains JSON result files, environment.properties,
#                          and the legacy XML file.
#                          Ephemeral — wiped before each adb pull.
#                          Listed in .gitignore.
#
#   allure-report/         Generated HTML report.
#                          Ephemeral — overwritten on every run.
#                          Listed in .gitignore.
#
#   allure-history.jsonl   Persistent history file — the ONLY artefact that
#                          survives across runs.  Each line is one complete
#                          run's data.  Commit this file to git so history
#                          accumulates across machines and CI runs.
#                          NOT listed in .gitignore.
#
#   allurerc.mjs           Allure 3 config template — committed to git.
#                          NOT used directly by allure generate; this script
#                          generates a temporary config with the real device
#                          name baked in and passes it via --config.
#
# First-run behaviour:
#   If allure-history.jsonl does not exist yet, Allure 3 creates it on the
#   first run.  No special handling is needed.

set -euo pipefail

# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------
NO_OPEN=0
for arg in "$@"; do
    case "$arg" in
        --no-open) NO_OPEN=1 ;;
        --help)
            sed -n '/^# Usage:/,/^[^#]/{ /^[^#]/d; s/^# \{0,1\}//; p }' "$0"
            exit 0
            ;;
        *)
            echo "ERROR: Unknown option: $arg" >&2
            exit 1
            ;;
    esac
done

# ---------------------------------------------------------------------------
# Resolve paths relative to the project root (test/fastrpc-test/), so the
# script works correctly regardless of the caller's working directory.
# ---------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

RESULTS_DIR="test-results"
REPORT_DIR="allure-report"
HISTORY_FILE="allure-history.jsonl"

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
log()  { echo "[generate_report] $*"; }
warn() { echo "[generate_report] WARNING: $*" >&2; }
die()  { echo "[generate_report] ERROR: $*" >&2; exit 1; }

command -v allure &>/dev/null || die "'allure' not found in PATH. Install it from https://allurereport.org/docs/install/"

# ---------------------------------------------------------------------------
# Sanity check — make sure there is something to report on
# ---------------------------------------------------------------------------
XML_COUNT=$(find "$RESULTS_DIR" -maxdepth 1 -name "*.xml" 2>/dev/null | wc -l)
if [[ "$XML_COUNT" -eq 0 ]]; then
    die "No XML result files found in $RESULTS_DIR/. Pull results from the device first: adb pull /data/local/tmp/test-results/ $RESULTS_DIR/"
fi
log "Found $XML_COUNT XML result file(s) in $RESULTS_DIR/."

# ---------------------------------------------------------------------------
# History status — informational only.
# Allure 3 reads and appends allure-history.jsonl automatically via
# allurerc.mjs.  Nothing needs to be copied manually.
# ---------------------------------------------------------------------------
if [[ -f "$HISTORY_FILE" ]]; then
    RUN_COUNT=$(wc -l < "$HISTORY_FILE")
    log "History file found: $HISTORY_FILE ($RUN_COUNT previous run(s) recorded)."
else
    log "No history file found — this will be the first run. Allure will create $HISTORY_FILE."
fi

# ---------------------------------------------------------------------------
# Step 4.5 — Read environment name from environment.properties
#
# The C runtime writes this file into UNITY_ALLURE_OUTPUT_DIR on the device
# (default: /data/local/tmp/test-results/).  After `adb pull` it lands in
# $RESULTS_DIR/environment.properties with the format:
#
#   Device=<device-name>          # e.g. iq-9075-evk
#   FastRPC.Version=<version>     # e.g. 1.0.0
#
# Allure 3 reads this file automatically and shows its key-value pairs in the
# report's Metadata section — no action needed for that.
#
# For the Environments dropdown, Allure 3's matchEnvironment() returns the
# CONFIG KEY of the first matching environments entry, not the label value.
# So the config key must equal the device name for the dropdown to show the
# real device name.  We generate a temporary allurerc.mjs with the actual
# ENV_NAME baked in as the key, pass it via --config, then delete it.
# ---------------------------------------------------------------------------
ENV_NAME=""
ENV_PROPS="$RESULTS_DIR/environment.properties"
if [[ -f "$ENV_PROPS" ]]; then
    # Compose "<Device>-<FastRPC.Version>" from the two separate keys,
    # matching the format written by unity_allure_output_set_environment().
    _DEV=$(grep -m1 '^Device=' "$ENV_PROPS" | cut -d'=' -f2- | tr -d '[:space:]')
    _VER=$(grep -m1 '^FastRPC\.Version=' "$ENV_PROPS" | cut -d'=' -f2- | tr -d '[:space:]')
    if [[ -n "$_DEV" && -n "$_VER" ]]; then
        ENV_NAME="${_DEV}-${_VER}"
        log "Environment: $ENV_NAME (Device=$_DEV, FastRPC.Version=$_VER)"
    elif [[ -n "$_DEV" ]]; then
        ENV_NAME="$_DEV"
        log "Environment: $ENV_NAME (Device=$_DEV, no version found)"
    else
        warn "$ENV_PROPS exists but contains no 'Device=' key — falling back to static config."
    fi
else
    warn "$ENV_PROPS not found — environment metadata will be absent from the report."
    warn "Ensure the test binary was built with utils/meson.build and run on the device before pulling results."
fi

# ---------------------------------------------------------------------------
# Step 4.6 — Write a temporary allurerc.mjs with the real environment key
#
# allurerc.mjs (committed to git) is a template.  We generate a temporary
# copy with ENV_NAME baked in as the environments key so the Environments
# dropdown shows the actual device name instead of a static placeholder.
# The temp file is deleted after allure generate completes.
#
# If ENV_NAME is empty (no environment.properties found), we fall back to
# the committed allurerc.mjs unchanged.
# ---------------------------------------------------------------------------
TMP_CONFIG=""
if [[ -n "$ENV_NAME" ]]; then
    TMP_CONFIG=$(mktemp "${TMPDIR:-/tmp}/allurerc_XXXXXX.mjs")
    # Escape ENV_NAME for safe embedding in a JS string literal:
    # backslash and double-quote are the only characters that need escaping.
    SAFE_ENV_NAME=$(printf '%s' "$ENV_NAME" | sed 's/\\/\\\\/g; s/"/\\"/g')
    cat > "$TMP_CONFIG" << ALLURERC_EOF
// AUTO-GENERATED by generate_report.sh — DO NOT COMMIT.
// Temporary config for this run; deleted after allure generate completes.
// Edit allurerc.mjs (the committed template) to change persistent settings.
export default {
  name: "FastRPC Tests",
  output: "./allure-report",
  historyPath: "./allure-history.jsonl",
  environments: {
    "${SAFE_ENV_NAME}": {
      name: "${SAFE_ENV_NAME}",
      matcher: ({ labels }) =>
        labels.some(({ name, value }) => name === "host" && value === "${SAFE_ENV_NAME}"),
    },
  },
  plugins: $(node -e "
    import('./allurerc.mjs').then(m => {
      process.stdout.write(JSON.stringify(m.default.plugins ?? {}, null, 2));
    });
  " 2>/dev/null || echo '{ "awesome": { "options": {} } }'),
};
ALLURERC_EOF
    log "Temporary config written: $TMP_CONFIG (env key: \"$ENV_NAME\")"
fi

# ---------------------------------------------------------------------------
# Step 5 — Generate the Allure report
#
# Use the temporary config when available (real device name as env key);
# fall back to the committed allurerc.mjs when no environment was detected.
# ---------------------------------------------------------------------------
log "=== Step 5: Generating Allure report ==="
if [[ -n "$TMP_CONFIG" ]]; then
    allure generate "$RESULTS_DIR" -o "$REPORT_DIR" --config="$TMP_CONFIG"
    rm -f "$TMP_CONFIG"
    log "Report generated for environment: $ENV_NAME"
else
    allure generate "$RESULTS_DIR" -o "$REPORT_DIR"
    log "Report generated (no environment assigned)."
fi
log "Report written to $REPORT_DIR/."

NEW_RUN_COUNT=$(wc -l < "$HISTORY_FILE" 2>/dev/null || echo "0")
log "History file updated: $HISTORY_FILE now has $NEW_RUN_COUNT run(s)."
log "Remember to commit $HISTORY_FILE to git so history is shared across machines."

# ---------------------------------------------------------------------------
# Step 6 — Open the report in the browser
# ---------------------------------------------------------------------------
if [[ $NO_OPEN -eq 0 ]]; then
    log "=== Step 6: Opening Allure report ==="
    allure open "$REPORT_DIR"
else
    log "=== Step 6: Skipped (--no-open) ==="
    log "To view the report later: allure open $REPORT_DIR"
fi

log "=== Done ==="
