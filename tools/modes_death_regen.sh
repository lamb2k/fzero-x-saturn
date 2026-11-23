#!/bin/bash
# Death Race field rows (course 54) for ENG_MODES images: the HOST reference of the original code (fzx-gp-ref --mode death, seed 1, difficulty 1,
# a passive player), its per-tick diagnostic, and tools/death_field_header.py's class header with the seed-1 pre-take-off pace table applied by
# id (C-169 item 2). Writes generated/courses/death-race/rivals/rivals_field_class.h (ROM-derived, generated/ only, never committed); then run
# tools/course_class_bundle.py and tools/course_practice_bundle.py over the bundled courses.
#
#   tools/modes_death_regen.sh [OUTDIR]      (from the repo root of a stage; WSL; needs generated/courses/death-race/rivals from the course bake)
set -euo pipefail
OUT=${1:-/tmp/fzx-death}
REF=ref/build/rivals-gcc-O2/fzx-gp-ref
[ -x "$REF" ] || { echo "build $REF first (ref/rivals_build.sh)" >&2; exit 1; }
[ -n "${OUT:?}" ] && [ "$OUT" != "/" ]
mkdir -p "$OUT"
G=$(python3 -c "import sys;sys.path.insert(0,'tools');import course_catalog as k;print(k.gen_root('death-race'))")
for f in course.bin rom-boot.bin cpu-line.bin; do [ -s "$G/rivals/$f" ] || { echo "missing $G/rivals/$f" >&2; exit 1; }; done
rm -f "$OUT"/death-s1.json "$OUT"/death-s1.diag "$OUT"/death-table.json "$G/rivals/rivals_field_class.h" "$OUT"/death-regen.log
"$REF" --course "$G/rivals/course.bin" --rom "$G/rivals/rom-boot.bin" --cpu-line "$G/rivals/cpu-line.bin" --difficulty 1 --seed 1 --ticks 3000 \
  --report "$OUT/death-s1.json" --diag "$OUT/death-s1.diag" --mode death --player-cpu 0 > "$OUT/death-s1.out"
[ -s "$OUT/death-s1.json" ] && [ -s "$OUT/death-s1.diag" ] || { echo "the reference run left no report" >&2; exit 1; }
python3 tools/death_field_header.py "$OUT/death-s1.json" "$OUT/death-s1.diag" "$G/rivals/rivals_field_class.h" "$OUT/death-table.json"
{ for f in "$REF" tools/modes_death_regen.sh tools/death_field_header.py "$G/rivals/course.bin" "$G/rivals/cpu-line.bin" "$OUT/death-s1.json" "$OUT/death-s1.diag" "$G/rivals/rivals_field_class.h"; do
    echo "sha256 $(sha256sum "$f" | cut -d' ' -f1) ${f#"$PWD"/}"; done; } > "$OUT/death-regen.log"
echo "death rows ok ($OUT/death-regen.log)"
