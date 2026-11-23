#!/bin/bash
# Engine gate: WSL side (run as root, outside the chroot). Driven by tools/engine_gate.py; usable by hand.
#   stage.sh init STAGE SEED_STAGE           create $HOME/yaul-image/STAGE (root: the chroot image): generated (copied) and upstream (linked)
#                                            from SEED_STAGE, the work tree w/ (empty)
#   stage.sh tree STAGE TAR SUBPIN           replace w/ with the tree in TAR (git archive from Windows); link
#                                            generated, upstream and third_party/SH2ZAM (lib-SUBPIN, the legacy line's)
#   stage.sh lib STAGE PIN TAR               unpack an SH2ZAM archive as lib-PIN (once per pin)
#   stage.sh receipts STAGE                  print STALE lines for generated receipts that do not match w/
#   stage.sh build STAGE OUT LABEL VARS...   build saturn/engine with VARS; copy BIN/ISO/CUE/SYM/MAP to OUT/LABEL
#   stage.sh legacy STAGE OUT LABEL KIND     KIND recipe (4b4cacb2) or off (bb5f0fc5): build and copy the BIN
#   stage.sh hostatk STAGE OUT KEY SLUG      the HOST reference of an image with ENG_ATTACKS (fzx-atk, the R01 driver), built once per KEY and course
#   stage.sh host STAGE OUT KEY SLUG [DRIVER [AHEAD [TICKS]]] HOST checkpoints (fzx-phys --checkpoints 256, tubes build, built once
#                                            per KEY = tree id) for SLUG's R01 race
# OUT is a /mnt/c path. Generated assets are ROM-derived: they stay in the stage and never leave this machine.
set -euo pipefail
set -u
cmd=${1:?}; stage=${2:?}
case "$stage" in claude-gate-*) ;; *) echo "stage must be claude-gate-*" >&2; exit 9;; esac
R=${FZX_YAUL_IMAGE:-$HOME/yaul-image}; S=$R/$stage
[ -n "$S" ] && [ "$S" != "/" ] && [ "$S" != "$R/" ] || exit 9

chroot_run() { chroot $R /bin/bash "$@"; }

case $cmd in
init)
  seed=$R/${3:?}
  test -d "$seed/generated" && test -d "$seed/upstream"
  mkdir -p "$S"
  [ -d "$S/generated" ] || [ -d "$S/w/generated" ] || cp -a "$seed/generated" "$S/generated"   # a real copy: regenerating must not write through hard links
  [ -d "$S/upstream" ] || [ -d "$S/w/upstream" ] || cp -al "$seed/upstream" "$S/upstream"
  # The legacy recipe's closure reads the reference build's stub headers (ref/build/*/stub, from ref/build.sh on the
  # pinned decomp). The legacy line is frozen, so the stage keeps one copy across trees.
  if [ ! -d "$S/ref-build" ]; then mkdir -p "$S/ref-build"; for d in gcc-O0-camera-retail-machine-menu gcc-O0-road; do
    [ -d "$seed/ref/build/$d" ] && cp -a "$seed/ref/build/$d" "$S/ref-build/"; done; fi
  touch "$S/.claude-gate-stage"
  echo "stage $S ready"
  ;;
tree)
  tar=${3:?}; test -f "$S/.claude-gate-stage"
  # generated/ and upstream/ move into each new tree (the asset tools refuse output outside ROOT/generated).
  for d in generated upstream; do
    if [ -d "$S/w/$d" ] && [ ! -L "$S/w/$d" ] && [ ! -e "$S/$d" ]; then mv "$S/w/$d" "$S/$d"; fi
  done
  test -d "$S/generated" && test -d "$S/upstream"
  rm -rf "$S/w"; mkdir -p "$S/w"; tar -xf "$tar" -C "$S/w"
  for d in generated upstream; do rm -rf "$S/w/$d"; mv "$S/$d" "$S/w/$d"; done
  rm -rf "$S/w/ref/build"; ln -s ../../ref-build "$S/w/ref/build"
  sub=${4:?}
  [ -d "$S/lib-$sub" ] || { echo "missing lib-$sub (third_party/SH2ZAM)" >&2; exit 4; }
  rm -rf "$S/w/third_party/SH2ZAM"; ln -s ../../lib-$sub "$S/w/third_party/SH2ZAM"
  pin=$(cat "$S/w/saturn/engine/sh2zam.pin")
  echo "tree ok: pin $pin submodule $sub"
  ;;
lib)
  pin=${3:?}; tar=${4:?}
  if [ ! -f "$S/lib-$pin/.gate-pin" ]; then rm -rf "$S/lib-$pin"; mkdir -p "$S/lib-$pin"; tar -xf "$tar" -C "$S/lib-$pin"; echo "$pin" > "$S/lib-$pin/.gate-pin"; fi
  echo "lib-$pin ok"
  ;;
receipts)
  cd "$S/w"
  find -L generated -name '*.receipt' | sort | while read -r f; do
    sh tools/check_receipt.sh "$f" >/dev/null 2>&1 || echo "STALE $f"
  done
  echo "receipts checked"
  ;;
cdda)
  # cdda STAGE: print the image's compiled CD track table (generated/cdda/cdda_tracks.h) for the disc check.
  cat "$S/w/generated/cdda/cdda_tracks.h"
  ;;
regen)
  # regen STAGE RECEIPT CMD...: when RECEIPT is stale, rerun the ROM-free generator CMD and say whether its outputs moved.
  receipt=${3:?}; shift 3
  cd "$S/w"
  if sh tools/check_receipt.sh "$receipt" >/dev/null 2>&1; then echo "REGEN $receipt current"; exit 0; fi
  d=$(dirname "$receipt"); rm -rf /tmp/gate-regen-$stage; cp -a "$d" /tmp/gate-regen-$stage
  # The art tools write only into git-ignored folders (git check-ignore). w/ is a git archive, so the generator gets
  # an empty repository for the tree's .gitignore rules, removed again before anything is built.
  made_git=0; [ -e .git ] || { git init -q . && made_git=1; }
  rc=0; "$@" > /tmp/gate-regen-$stage.log 2>&1 || rc=$?
  [ "$made_git" = 0 ] || rm -rf .git
  [ "$rc" = 0 ] || { tail -5 /tmp/gate-regen-$stage.log; echo "REGEN $receipt failed"; exit 1; }
  sh tools/check_receipt.sh "$receipt" >/dev/null 2>&1 || { echo "REGEN $receipt still stale"; exit 1; }
  moved=$(cd "$d" && for f in $(sed -n 's/^output [0-9a-f]* //p' "$(basename "$receipt")"); do cmp -s "$f" "/tmp/gate-regen-$stage/$f" || echo "$f"; done | tr '\n' ' ')
  echo "REGEN $receipt regenerated; outputs ${moved:+changed: }${moved:-unchanged}"
  ;;
build)
  out=${3:?}; label=${4:?}; shift 4
  pin=$(cat "$S/w/saturn/engine/sh2zam.pin")
  test -d "$S/lib-$pin" || { echo "missing lib-$pin" >&2; exit 4; }
  cat > "$S/gate-build.sh" <<IN
set -euo pipefail
source /yaul-env.sh
export SILENT=1
export PATH="\$YAUL_INSTALL_ROOT/bin:\$PATH"
cd /$stage/w
make -C saturn/engine SH2ZAM_ROOT=/$stage/lib-$pin "\$@" -B > /tmp/gate-build-$stage.log 2>&1 || { grep -E "error|\\*\\*\\*" /tmp/gate-build-$stage.log | head -20; exit 1; }
make -C saturn/engine SH2ZAM_ROOT=/$stage/lib-$pin "\$@" -pn 2>/dev/null | sed -n 's/^SH_OUTPUT_DIR := //p' | tail -1 > /tmp/gate-outdir-$stage
IN
  chroot_run /$stage/gate-build.sh "$@"
  od=$S/w/saturn/engine/$(cat $R/tmp/gate-outdir-$stage)
  d=$out/$label; rm -rf "$d"; mkdir -p "$d"
  cp "$od/fzs-engine.cue" "$od/fzs-engine.iso" "$od/yaul/fzs-engine.sym" "$od/yaul/fzs-engine.map" "$d/"
  cp "$od/cd/A.BIN" "$d/fzs-engine.bin"
  echo "$*" > "$d/vars.txt"; basename "$od" > "$d/outdir.txt"
  rm -rf "$od"
  echo "BIN $label $(sha256sum "$d/fzs-engine.bin" | cut -d' ' -f1)"
  ;;
legacy)
  out=${3:?}; label=${4:?}; kind=${5:?}
  if [ "$kind" = recipe ]; then
    env="FZX_BACKDROP_CATCHUP=1 FZX_ROAD_PLAN_FIXED=1 FZX_DEFER_PROJECTION=1 FZX_FASTFLOAT=1 FZX_SOURCE_CAMERA=1 FZX_SOURCE_COUNTDOWN=1 FZX_NATIVE_CORE_EMPTY=1 FZX_SOURCE_MACHINE=1 FZX_SOURCE_MACHINE_MENU=1 FZX_SOURCE_RESULTS=1 FZX_SOURCE_RECORDS=1 FZX_SOURCE_FINISH_MENU=1 FZX_SOURCE_ROAD_STREAM=1 FZX_ROAD_PLAN=1 FZX_SOURCE_SKY=1 FZX_SOURCE_SKYLINE=1 REF_CAMERA_ART=retail"
    stubs=machine-stubs-menu-tables; bd=build-orig-skyline-machine-menu-results-records-finishmenu-stream
    vars="FZX_SIM=orig FZX_IMPORTED=1 FZX_SOURCE_MACHINE=1 FZX_SOURCE_CAMERA=1 FZX_ROM_BOOT=1 FZX_SOURCE_HUD=1 FZX_SOURCE_SKY=1 FZX_SKY_SIMPLIFY=1 FZX_SOURCE_SKYLINE=1 FZX_SKYLINE_CD=1 FZX_HUD_CD=1 FZX_SOURCE_PLANES=1 FZX_SOURCE_ROAD=1 FZX_SOURCE_ROAD_TILES=1 FZX_ROAD_TILE_PROJECT_CULL=1 FZX_ROAD_TILE_EDGE_REUSE=1 FZX_ROAD_TILE_PROFILE=0 FZX_ROAD_TILE_VERTEX_CACHE=0 FZX_SOURCE_FLOOR_RBG=1 FZX_FRAME_PROFILE=1 FZX_SOURCE_TITLE=1 FZX_SOURCE_MENU=1 FZX_SOURCE_COURSE_MENU=1 FZX_SOURCE_MACHINE_MENU=1 FZX_SOURCE_RESULTS=1 FZX_SOURCE_RECORDS=1 FZX_SOURCE_FINISH_MENU=1 FZX_SOURCE_ROAD_STREAM=1 FZX_PLANE_CLOUD_ONLY=1 FZX_SOURCE_COUNTDOWN=1 FZX_NATIVE_CORE_EMPTY=1 FZX_DEFER_PROJECTION=1 FZX_ROAD_STREAM_NATIVE_LAYOUT=1 FZX_ROAD_STREAM_EMIT=1 FZX_MACHINE_REBAKE=1 FZX_VEHICLE_NATIVE=1 FZX_ROAD_STREAM_BUDGET=1 FZX_MENU_TABLES_LOW=1 FZX_PLANES_DEGENERATE_UV=1 FZX_VEHICLE_POSE1=1 FZX_CLOUDS_VDP2=1 FZX_BACKDROP_CATCHUP=1 FZX_ROAD_PLAN_FIXED=1 FZX_RBG_FAST=1 FZX_SLAVE_ROAD=1 FZX_ROAD_LOD=1"
  elif [ "$kind" = off ]; then
    env="FZX_BACKDROP_CATCHUP=0 FZX_ROAD_PLAN_FIXED=0 FZX_DEFER_PROJECTION=0 FZX_FASTFLOAT=1 FZX_SOURCE_CAMERA=1 FZX_SOURCE_COUNTDOWN=1 FZX_NATIVE_CORE_EMPTY=1 FZX_SOURCE_MACHINE=1 FZX_SOURCE_MACHINE_MENU=1 FZX_SOURCE_RESULTS=1 FZX_SOURCE_RECORDS=1 FZX_SOURCE_FINISH_MENU=0 FZX_SOURCE_ROAD_STREAM=0 FZX_ROAD_PLAN=1 FZX_SOURCE_SKY=1 FZX_SOURCE_SKYLINE=1 REF_CAMERA_ART=retail"
    stubs=machine-stubs; bd=build-orig-skyline-machine-menu-results-records
    vars="FZX_SIM=orig FZX_IMPORTED=1 FZX_SOURCE_MACHINE=1 FZX_SOURCE_CAMERA=1 FZX_ROM_BOOT=1 FZX_SOURCE_HUD=1 FZX_SOURCE_SKY=1 FZX_SKY_SIMPLIFY=1 FZX_SOURCE_SKYLINE=1 FZX_SKYLINE_CD=1 FZX_HUD_CD=1 FZX_SOURCE_PLANES=1 FZX_SOURCE_ROAD=1 FZX_SOURCE_ROAD_TILES=1 FZX_ROAD_TILE_PROJECT_CULL=1 FZX_ROAD_TILE_EDGE_REUSE=1 FZX_ROAD_TILE_PROFILE=0 FZX_ROAD_TILE_VERTEX_CACHE=1 FZX_SOURCE_FLOOR_RBG=1 FZX_FRAME_PROFILE=1 FZX_SOURCE_TITLE=1 FZX_SOURCE_MENU=1 FZX_SOURCE_COURSE_MENU=1 FZX_SOURCE_MACHINE_MENU=1 FZX_SOURCE_RESULTS=1 FZX_SOURCE_RECORDS=1 FZX_SOURCE_FINISH_MENU=0 FZX_SOURCE_ROAD_STREAM=0 FZX_PLANE_CLOUD_ONLY=1 FZX_SOURCE_COUNTDOWN=1 FZX_NATIVE_CORE_EMPTY=1 FZX_DEFER_PROJECTION=0"
  else echo "legacy kind recipe|off" >&2; exit 2; fi
  cat > "$S/gate-legacy.sh" <<IN
set -euo pipefail
source /yaul-env.sh
export SILENT=1
export PATH="\$YAUL_INSTALL_ROOT/bin:\$PATH"
cd /$stage/w
export $env
mkdir -p saturn/build-orig-camera
cp generated/$stubs/1/sat_stubs.c generated/$stubs/1/sat_stubs_data.s saturn/build-orig-camera/
bash ref/sat/closure.sh /$stage/w/saturn/build-orig-camera
make -B -C saturn $vars FZX_CLOSURE=/$stage/w/saturn/build-orig-camera/ref_closure.o
IN
  rm -rf "$S/w/saturn/$bd"
  chroot_run /$stage/gate-legacy.sh > /tmp/gate-legacy-$stage-$kind.log 2>&1 || { tail -20 /tmp/gate-legacy-$stage-$kind.log; exit 1; }
  d=$out/$label; rm -rf "$d"; mkdir -p "$d"
  cp "$S/w/saturn/$bd/yaul/fzero-x-saturn.bin" "$d/fzero-x-saturn.bin"
  rm -rf "$S/w/saturn/$bd" "$S/w/saturn/build-orig-camera"
  echo "LEGACY $kind $(sha256sum "$d/fzero-x-saturn.bin" | cut -d' ' -f1)"
  ;;
host)
  out=${3:?}; key=${4:?}; slug=${5:?}; driver=${6:-0.004,2.5}; ahead=${7:-}; ticks=${8:-}
  cd "$S/w"; pin=$(cat saturn/engine/sh2zam.pin)
  hb=$S/host-$key; mkdir -p "$hb"
  if [ ! -x "$hb/fzx-phys" ]; then
    SH2ZAM_ROOT=$S/lib-$pin TUBES=1 bash tools/physics_host/build.sh gcc -O2 > "$hb/build.log" 2>&1 || { tail -20 "$hb/build.log"; exit 1; }
    cp tools/physics_host/build/gcc-O2-tubes/fzx-phys "$hb/fzx-phys"
  fi
  g=$(python3 -c "import sys;sys.path.insert(0,'tools');import course_catalog as c;print(c.gen_root('$slug'))")
  printf "460 - 0 0\n${ticks:-9000} A 0 0\n" > /tmp/gate-r01-$stage.txt
  mkdir -p "$out"
  f=$out/$slug-$driver${ahead:+-a$ahead}${ticks:+-t$ticks}.txt
  da=; [ -n "$ahead" ] && da="--driver-ahead $ahead"
  "$hb/fzx-phys" --pack "$g/physics/PHYS.PAK" --inputs /tmp/gate-r01-$stage.txt --driver "$driver" $da --checkpoints 256 > "$f" 2>&1
  "$hb/fzx-phys" --pack "$g/physics/PHYS.PAK" --inputs /tmp/gate-r01-$stage.txt --driver "$driver" $da --out /tmp/gate-r01-$stage.tsv > /dev/null 2>&1
  # The finish (flags bit 25: race time, laps) or the retirement tick (bits 14, 19, 27), as tools' race_summary.
  python3 - /tmp/gate-r01-$stage.tsv >> "$f" <<'PY'
import sys
fin = ret = None
for line in open(sys.argv[1]):
    if line.startswith('#'): continue
    f = line.rstrip('\n').split('\t'); fl = int(f[28], 16)
    if ret is None and fl & (1 << 14 | 1 << 27 | 1 << 19): ret = f[0]
    if fl & (1 << 25): fin = f[39]; break
print('finish_ms %s retire_tick %s' % (fin or '-', ret or '-'))
PY
  rm -f /tmp/gate-r01-$stage.tsv
  echo "HOST $slug $driver $(grep -E '^(ticks|finish_ms)' "$f" | tr '\n' ' ')"
  ;;
hostatk)
  # hostatk STAGE OUT KEY SLUG: the HOST reference of an image built with ENG_ATTACKS (tools/attacks_host, fzx-atk --gate 1, the R01 driver):
  # body checkpoints, attack words, the final body hash, the player's finish. One fzx-atk per course (its rivals pack is compiled in),
  # built with tubes for every course as the scenes images' physics is (vehicle-dynamics-2 gives road courses the same bits).
  out=${3:?}; key=${4:?}; slug=${5:?}
  cd "$S/w"; pin=$(cat saturn/engine/sh2zam.pin)
  hb=$S/host-$key; mkdir -p "$hb"
  g=$(python3 -c "import sys;sys.path.insert(0,'tools');import course_catalog as c;print(c.gen_root('$slug'))")
  if [ ! -x "$hb/fzx-atk-$slug" ]; then
    SH2ZAM_ROOT=$S/lib-$pin GEN=$g ATK_GEN=$S/w/generated TUBES=1 TAG=-gate-$key-$slug bash tools/attacks_host/build.sh gcc -O2 > "$hb/build-atk-$slug.log" 2>&1 || { tail -20 "$hb/build-atk-$slug.log"; exit 1; }
    cp tools/attacks_host/build/gcc-O2-gate-$key-$slug/fzx-atk "$hb/fzx-atk-$slug"
  fi
  mkdir -p "$out"
  "$hb/fzx-atk-$slug" --phys "$g/physics/PHYS.PAK" --rivals "$g/rivals/RIVALS.PAK" --driver 1 --gate 1 > "$out/$slug-atk.txt" 2> "$out/$slug-atk.err"
  echo "HOSTATK $slug $(grep -E '^(ticks|finish_ms)' "$out/$slug-atk.txt" | paste -sd' ' -)"
  ;;
*) echo "unknown command $cmd" >&2; exit 2;;
esac
