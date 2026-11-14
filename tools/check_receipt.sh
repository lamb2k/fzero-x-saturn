#!/bin/sh
# Check a generation receipt. Usage: tools/check_receipt.sh generated/<file>.receipt
# A receipt lists "sha256  repo-relative-path" for the generator and every output;
# lines starting with '#' are notes. Paths are relative to the repository root.
# Fails when the generator changed since generation or an output was edited.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
receipt=${1:?usage: check_receipt.sh <receipt>}
case $receipt in /*) ;; *) receipt=$PWD/$receipt ;; esac
if [ ! -f "$receipt" ]; then
    echo "receipt: $receipt is missing; regenerate the output with its generator" >&2
    exit 1
fi
if ! (cd "$root" && grep -v '^#' "$receipt" | sha256sum --check --strict --quiet -); then
    echo "receipt: STALE generated output for $(basename "$receipt" .receipt)." >&2
    echo "receipt: the generator or an output changed after generation." >&2
    echo "receipt: regenerate it (never edit generated files by hand)." >&2
    exit 1
fi
echo "receipt: OK $(basename "$receipt")"
