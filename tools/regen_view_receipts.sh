#!/bin/sh
# Regenerate the engine_path and road_prep outputs (and receipts) of one course, or all six, after a change to
# src/fzx_view.c (an input of both generators). Run from the tree root, under Linux/WSL, with the course's ROM-derived
# generated/ inputs in place (engine_path/raw, road_bake); nothing here reads or prints the ROM.
#   tools/regen_view_receipts.sh [COURSE|.|all]       COURSE is a slug (silence-1, ...), . the default course
# Each course's fog and texture budget are read back from its own engine_path.json and road_bake.json, so the outputs are
# the ones the receipts were made from. SH2ZAM: $SH2ZAM, else ../lib-<pin> (the gate stage), else third_party/SH2ZAM.
# After a change that must not move the outputs (an exact cut), the regenerated outputs equal the old ones byte for
# byte and only the receipts' generator stamps differ: compare with `cmp` against a copy taken before.
set -eu
which=${1:-all}
pin=$(cat saturn/engine/sh2zam.pin)
if [ -n "${SH2ZAM:-}" ]; then lib=$SH2ZAM; elif [ -d "../lib-$pin" ]; then lib=../lib-$pin; else lib=third_party/SH2ZAM; fi
one() {
    if [ "$1" = . ]; then g=generated; else g=generated/courses/$1; fi
    fog=$(python3 -c "import json,sys;print(json.load(open(sys.argv[1]))['fog'])" "$g/engine_path/engine_path.json")
    tex=$(python3 -c "import json,sys;print(json.load(open(sys.argv[1]))['texture_limit'])" "$g/road_bake/road_bake.json")
    python3 tools/engine_path.py --raw "$g/engine_path/raw" --output "$g/engine_path" --sh2zam "$lib" --fog "$fog"
    python3 tools/road_prep.py --sh2zam "$lib" --bake "$g/road_bake" --output "$g/road_prep" --texture-limit "$tex"
}
if [ "$which" = all ]; then
    for c in . silence-1 sand-ocean-1 devils-forest-1 big-blue-1 port-town-1; do one "$c"; done
else one "$which"; fi
