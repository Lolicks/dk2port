#!/usr/bin/env bash
# Re-run the full Ghidra static analysis and export for DKII.EXE.
# Requires Ghidra installed by REA at ~/.local/share/rea-tools/ghidra_12.2_PUBLIC.
set -euo pipefail
PORT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
GAME_DIR="$(dirname "$PORT_DIR")"
GHIDRA="${GHIDRA_HOME:-$HOME/.local/share/rea-tools/ghidra_12.2_PUBLIC}"
PROJECT="${GHIDRA_PROJECT:-$HOME/ghidra-dk2-project}"

mkdir -p "$PROJECT" "$PORT_DIR/re/ghidra"
GHIDRA_HEADLESS_MAXMEM="${GHIDRA_HEADLESS_MAXMEM:-3G}" \
"$GHIDRA/support/analyzeHeadless" "$PROJECT" dk2 \
  -import "$GAME_DIR/DKII.EXE" \
  -analysisTimeoutPerFile 2400 \
  -scriptPath "$PORT_DIR/re" \
  -postScript GhidraExportScript.java \
  -overwrite 2>&1 | tee "$PORT_DIR/re/ghidra_run.log"

echo "Export written to $PORT_DIR/re/ghidra"
