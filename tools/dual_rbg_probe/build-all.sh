#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
for probe_mode in 0 1 2 3 4 5 6; do
    SATURN_DIR=tools/dual_rbg_probe tools/saturn_build.sh PROBE_MODE="$probe_mode"
done
python3 tools/dual_rbg_probe/audit.py
