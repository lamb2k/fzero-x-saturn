#!/usr/bin/env bash
# Source-road storage integration. Rendering still uses the temporary course.
# Local ROM and all generated art remain private. No cartridge disc I/O in play.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
: "${FZX_TEST_ROM:?Set FZX_TEST_ROM locally}"
cd "$ROOT"
python3 tools/import_source_art.py
python3 tools/road_assets.py
python3 tools/road_pack.py
FZX_SOURCE_CAMERA=1 FZX_SOURCE_HUD=1 FZX_SOURCE_ROAD=1 bash tools/saturn_orig.sh --rom "$FZX_TEST_ROM"
