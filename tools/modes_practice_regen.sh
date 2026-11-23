#!/bin/bash
# Practice row sets for ENG_MODES images (default on in every scenes image): HOST reference runs of the original code as Practice,
# one per course and class, then the per-course row headers and the bundle the image links.
#
#   tools/modes_practice_regen.sh [OUTDIR]      (from the repo root of a stage; WSL; needs the stage's generated/ and the owner's ROM data)
#
# Needs ref/build/rivals-gcc-O2/fzx-gp-ref (ref/rivals_build.sh gcc -O2 <SH2ZAM saturn dir>). Output: generated/courses/select/
# course_practice.{h,c,receipt} (ROM-derived, generated/ only, never committed). Run it after tools/course_bundle.py and again whenever
# the receipt is stale (tools/rivals_field_header.py, tools/course_practice_bundle.py or the reference build changed).
#
# A clean run: the script removes the previous reports, headers and bundle first, so a stale result cannot stand in for a failed
# one; every reference run is waited for on its own and must exit 0 and leave a report; tools/course_practice_bundle.py then checks
# the reports (the course length, class and seed asked for, 30 racers, the same machines in every class, six distinct courses); the
# input and generator hashes go to OUTDIR/practice-regen.log. Any failure stops the script before the bundle is written.
set -euo pipefail
OUT=${1:-/tmp/fzx-practice}
# Every course the image bundles (course_ids.c order); Death Race has no Practice and is skipped (its rows come from tools/modes_death_regen.sh).
COURSES=$(python3 -c "
import re,sys
sys.path.insert(0,'tools')
import course_bundle as b, course_catalog as k
by={v[0]:s for s,v in k.COURSES.items()}
print(' '.join(by[int(i)] for i in re.findall(r'\.id = (\d+)u', (b.OUT/'course_ids.c').read_text()) if by[int(i)] != 'death-race'))")
REF=ref/build/rivals-gcc-O2/fzx-gp-ref
[ -x "$REF" ] || { echo "build $REF first (ref/rivals_build.sh)" >&2; exit 1; }
[ -n "${OUT:?}" ] && [ "$OUT" != "/" ]
mkdir -p "$OUT"
SEL=$(python3 -c "import sys;sys.path.insert(0,'tools');import course_bundle as b;print(b.OUT)")
rm -f "$SEL"/course_practice.h "$SEL"/course_practice.c "$SEL"/course_practice.receipt "$OUT"/practice-regen.log
for c in $COURSES; do
  G=$(python3 -c "import sys;sys.path.insert(0,'tools');import course_catalog as k;print(k.gen_root('$c'))")
  IDX=$(python3 tools/course_catalog.py "$c" index)
  rm -rf "${OUT:?}/$c"; mkdir -p "$OUT/$c"
  rm -f "$G/rivals/rivals_field_practice.h"
  pids=()
  for d in 0 1 2 3; do   # the four classes, the shipped field seed 1
    "$REF" --course "$G/rivals/course.bin" --rom "$G/rivals/rom-boot.bin" --cpu-line "$G/rivals/cpu-line.bin" \
      --difficulty "$d" --seed 1 --ticks 14000 --report "$OUT/$c/practice-d$d-s1.json" --tuning-course "$IDX" --zones 1 --mode practice \
      > "$OUT/$c/practice-d$d-s1.out" 2>&1 &
    pids+=($!)
  done
  for p in "${pids[@]}"; do wait "$p" || { echo "reference run failed for $c (pid $p), see $OUT/$c/*.out" >&2; exit 1; }; done
  for d in 0 1 2 3; do [ -s "$OUT/$c/practice-d$d-s1.json" ] || { echo "no report for $c class $d" >&2; exit 1; }; done
  python3 tools/rivals_field_header.py --classes "$OUT/$c/practice-d0-s1.json" "$OUT/$c/practice-d1-s1.json" \
    "$OUT/$c/practice-d2-s1.json" "$OUT/$c/practice-d3-s1.json" "$OUT/$c/rivals_field_practice.h" --course "$c" --practice
  cp "$OUT/$c/rivals_field_practice.h" "$G/rivals/rivals_field_practice.h"
  { echo "course $c index $IDX"
    for f in "$G/rivals/course.bin" "$G/rivals/cpu-line.bin" "$OUT/$c"/practice-d?-s1.json "$OUT/$c/rivals_field_practice.h"; do
      echo "sha256 $(sha256sum "$f" | cut -d' ' -f1) ${f#"$PWD"/}"
    done; } >> "$OUT/practice-regen.log"
done
{ for f in "$REF" tools/modes_practice_regen.sh tools/rivals_field_header.py tools/course_practice_bundle.py tools/course_catalog.py tools/course_bundle.py; do
    echo "generator sha256 $(sha256sum "$f" | cut -d' ' -f1) $f"; done; } >> "$OUT/practice-regen.log"
# The bundle's rows follow course_ids.c (the courses the image bundles, Death Race last when it is on the disc).
ALL=$(python3 -c "
import re,sys
sys.path.insert(0,'tools')
import course_bundle as b, course_catalog as k
by={v[0]:s for s,v in k.COURSES.items()}
print(' '.join(by[int(i)] for i in re.findall(r'\.id = (\d+)u', (b.OUT/'course_ids.c').read_text())))")
python3 tools/course_practice_bundle.py $ALL --reports "$OUT"
echo "practice bundle ok ($OUT/practice-regen.log)"
