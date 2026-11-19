#!/usr/bin/env bash
# HOST check for the engine line (docs/ENGINE_PLAN.md, E0): the engine's
# fixed-point track pick (SH2ZAM segmesh, saturn/engine/engine_scene.c)
# against the original road planner on the CI fixture course.
#   tools/engine_plan_check.sh SH2ZAM_ROOT
# ref/build.sh is a receipt-pinned generator, so it is not edited: a temporary
# copy gains the HOST-only course dump (ref/engine_dump.c) and is removed after.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd); Z=${1:?SH2ZAM root}
O=$ROOT/generated/engine_path/check; mkdir -p "$O"
T=$ROOT/ref/.engine-edump-build.sh; trap 'rm -f "$T"' EXIT
python3 - "$ROOT/ref/build.sh" "$T" <<'PY'
import sys
s = open(sys.argv[1]).read()
def rep(old, new):
    global s
    assert old in s, old
    s = s.replace(old, new, 1)
rep('MACHINE_MENU=${FZX_SOURCE_MACHINE_MENU:-0}\n', 'MACHINE_MENU=${FZX_SOURCE_MACHINE_MENU:-0}\nEDUMP=1\n')
rep('if [ "$MACHINE_MENU" = 1 ]; then OUT=$OUT-machine-menu; fi\n', 'if [ "$MACHINE_MENU" = 1 ]; then OUT=$OUT-machine-menu; fi\nOUT=$OUT-edump\n')
rep('for s in $BRIDGES; do  # Bridge side: decomp headers.', 'BRIDGES="$BRIDGES engine_dump"\nfor s in $BRIDGES; do  # Bridge side: decomp headers.')
rep('-DFZX_BACKDROP_STATE=$BACKDROP -c "$ROOT/ref/$DRIVER.c"', '-DFZX_BACKDROP_STATE=$BACKDROP -DFZX_ENGINE_DUMP=1 -c "$ROOT/ref/$DRIVER.c"')
open(sys.argv[2], 'w').write(s)
PY
cd "$ROOT"
FZX_SOURCE_CAMERA=1 FZX_ROAD_PLAN=1 bash "$T" gcc -O2 > "$O/ref-plan-build.log" 2>&1 || { tail -8 "$O/ref-plan-build.log"; exit 1; }
R=ref/build/gcc-O2-camera-fixture-plan-edump/fzx-ref
gcc -O2 -Wall -Wextra -Iinclude -Iref -Isaturn/engine -I"$Z/saturn/include" src/fzx_view.c saturn/engine/engine_scene.c \
    saturn/engine/engine_path.c "$Z/saturn/src/sh2zam_segmesh.c" tools/engine_bake.c -lm -o "$O/engine_bake"
python3 ref/fixture_course.py "$O/fixture.bin"
rc=0
for t in T02_full_throttle T05_slalom T06_boost T10_random; do
    REF_ROAD_PLAN_TRACE=$O/$t.plan REF_ENGINE_COURSE=$O/$t.course $R --course "$O/fixture.bin" --byte-order big \
        --inputs ref/inputs/$t.txt --camera-trace "$O/$t.pcamera" > /dev/null
    echo "$t: $("$O/engine_bake" plan "$O/$t.pcamera" "$O/$t.course" "$O/$t.plan" 2>&1 | tail -2 | tr '\n' ' ')"
    "$O/engine_bake" plan "$O/$t.pcamera" "$O/$t.course" "$O/$t.plan" > /dev/null 2>&1 || rc=1
done
for m in 1 2 3 4; do
    if "$O/engine_bake" plan "$O/T10_random.pcamera" "$O/T10_random.course" "$O/T10_random.plan" $m > /dev/null 2>&1; then
        echo "mutation $m: NOT CAUGHT"; rc=1
    else echo "mutation $m: rejected"; fi
done
exit $rc
