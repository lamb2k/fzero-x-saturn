#!/usr/bin/env bash
# Build the Saturn image locally inside the pinned Yaul image, unpacked by tools/fetch_yaul.py.
#   tools/saturn_build.sh [make args...]        e.g. tools/saturn_build.sh FZX_SIM=proto
#   SATURN_DIR=tools/bench_sh2 tools/saturn_build.sh   builds another Yaul project in the repo
# Needs root (chroot and bind mount; WSL's default user is root). YAUL_IMAGE defaults to
# ~/yaul-image. Same toolchain and flags as the CI job; the CI build stays the record.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
IMG=${YAUL_IMAGE:-$HOME/yaul-image}
[ -f "$IMG/yaul-env.sh" ] || { echo "no Yaul image at $IMG: run tools/fetch_yaul.py $IMG" >&2; exit 1; }
mkdir -p "$IMG/work" "$IMG/proc" "$IMG/dev"
mountpoint -q "$IMG/work" || mount --bind "$ROOT" "$IMG/work"
mountpoint -q "$IMG/proc" || mount -t proc proc "$IMG/proc"
mountpoint -q "$IMG/dev" || mount --bind /dev "$IMG/dev"
args=$(printf ' %q' "$@")
chroot "$IMG" /bin/bash -c "source /yaul-env.sh && export PATH=\"\$YAUL_INSTALL_ROOT/bin:\$PATH\" && cd /work && make -C ${SATURN_DIR:-saturn}$args"
