#!/usr/bin/env bash
# Size the reference's data stand-ins from use and prove the sizes on the host.
#   ref/sat/host_stubs.sh      -> ref/sat/build/host-small/{stubs.c,sizes.txt}
# 1. The full-size gcc -O0 host reference reports the highest non-zero byte of every data
#    stand-in over T01 to T10; each gets twice that (64 bytes minimum).
# 2. The host reference relinked with exactly those sizes must keep every hash in
#    ref/expected/fixture.hashes.
# Used by ref/sat/build.sh (the probe) and tools/saturn_orig.sh (the game image).
# REF_STUB_EXTRA_COURSE (a big-endian record) and REF_STUB_ROM: also measure every script on
# that course with that ROM's machine constants (tools/saturn_orig.sh --rom). Local only.
set -euo pipefail
REF=$(cd "$(dirname "$0")/.." && pwd)
HOST=$REF/build/gcc-O0
SMALL=$REF/sat/build/host-small
CAMERA=${FZX_SOURCE_CAMERA:-0}
MACHINE_MENU=${FZX_SOURCE_MACHINE_MENU:-0}
[ "$MACHINE_MENU" = 0 ] || [ "$MACHINE_MENU" = 1 ] || { echo "invalid FZX_SOURCE_MACHINE_MENU" >&2; exit 1; }
CAMERA_ARGS=()
if [ "$CAMERA" = 1 ]; then
    ART=${REF_CAMERA_ART:-fixture}
    HOST=$HOST-camera-$ART
    SMALL=$SMALL-camera-$ART
    CAMERA_ARGS=(--camera-trace "$SMALL/reference-camera.txt")
fi
if [ "$MACHINE_MENU" = 1 ]; then
    HOST=$HOST-machine-menu
    SMALL=$SMALL-machine-menu
    if [ "$CAMERA" = 1 ]; then CAMERA_ARGS=(--camera-trace "$SMALL/reference-camera.txt"); fi
fi
if [ "$CAMERA" = 1 ] || [ "$MACHINE_MENU" = 1 ]; then
    env -u REF_STUB_SIZES REF_STUB_SIZE=0x40000 bash "$REF/build.sh" gcc -O0 >/dev/null
fi
[ -x "$HOST/fzx-ref" ] || env -u REF_STUB_SIZES REF_STUB_SIZE=0x40000 bash "$REF/build.sh" gcc -O0 >/dev/null
mkdir -p "$SMALL"
EXPECTED=$REF/expected/fixture.hashes
# The menu arm supplies the original racer tuning omitted by the legacy path.
# Size reductions must match that arm's full-storage reference exactly.
if [ "$MACHINE_MENU" = 1 ]; then EXPECTED=$SMALL/reference-fixture.hashes; : > "$EXPECTED"; fi
python3 "$REF/fixture_course.py" "$SMALL/course.bin"
rm -f "$SMALL/extents.txt"
while read -r name _; do
    full=$(REF_STUB_REPORT="$SMALL/extents.txt" "$HOST/fzx-ref" --course "$SMALL/course.bin" --byte-order big \
        --inputs "$REF/inputs/$name.txt" | awk '{print $4}')
    if [ "$MACHINE_MENU" = 1 ]; then printf '%s %s\n' "$name" "$full" >> "$EXPECTED"; fi
    if [ -n "${REF_STUB_EXTRA_COURSE:-}" ]; then
        REF_STUB_REPORT="$SMALL/extents.txt" "$HOST/fzx-ref" --course "$REF_STUB_EXTRA_COURSE" \
            --byte-order big --rom "$REF_STUB_ROM" --inputs "$REF/inputs/$name.txt" >/dev/null
    fi
done < "$REF/expected/fixture.hashes"
if [ -n "${REF_STUB_EXTRA_INPUTS:-}" ]; then
    [ -n "${REF_STUB_EXTRA_COURSE:-}" ] && [ -n "${REF_STUB_ROM:-}" ] || {
        echo "extra replay requires local course and ROM" >&2; exit 1;
    }
    REF_STUB_REPORT="$SMALL/extents.txt" "$HOST/fzx-ref" --course "$REF_STUB_EXTRA_COURSE" \
        --byte-order big --rom "$REF_STUB_ROM" --inputs "$REF_STUB_EXTRA_INPUTS" \
        --trace "$SMALL/reference-race.txt" "${CAMERA_ARGS[@]}" >/dev/null
fi
awk '{ if ($2 > m[$1]) m[$1] = $2; seen[$1] = 1 }
     END { for (k in seen) { s = 2 * m[k]; if (s < 64) s = 64; printf "%s 0x%X\n", k, int((s + 63) / 64) * 64 } }' \
    "$SMALL/extents.txt" | sort > "$SMALL/sizes.txt"
objs=$(ls "$HOST"/*.o | grep -v '/stubs.o$')
REF_STUB_CALLS=0 REF_STUB_SIZES="$SMALL/sizes.txt" python3 "$REF/gen_stubs.py" "$SMALL/stubs.c" "$HOST"/*.i -- $objs >/dev/null
gcc -m32 -fno-pic -O0 -w -c "$SMALL/stubs.c" -o "$SMALL/stubs.o"
gcc -m32 -no-pie $objs "$SMALL/stubs.o" -lm -o "$SMALL/fzx-ref"
while read -r name want; do
    got=$("$SMALL/fzx-ref" --course "$SMALL/course.bin" --byte-order big --inputs "$REF/inputs/$name.txt" | awk '{print $4}')
    [ "$got" = "$want" ] || { echo "sized stubs change $name: $got != $want" >&2; exit 1; }
done < "$EXPECTED"
if [ -n "${REF_STUB_EXTRA_INPUTS:-}" ]; then
    CAMERA_ARGS=()
    if [ "$CAMERA" = 1 ]; then CAMERA_ARGS=(--camera-trace "$SMALL/small-camera.txt"); fi
    "$SMALL/fzx-ref" --course "$REF_STUB_EXTRA_COURSE" --byte-order big --rom "$REF_STUB_ROM" \
        --inputs "$REF_STUB_EXTRA_INPUTS" --trace "$SMALL/small-race.txt" "${CAMERA_ARGS[@]}" >/dev/null
    cmp "$SMALL/reference-race.txt" "$SMALL/small-race.txt"
    if [ "$CAMERA" = 1 ]; then cmp "$SMALL/reference-camera.txt" "$SMALL/small-camera.txt"; fi
fi
echo "host: measured stand-in sizes keep all fixture hashes ($(awk '{s += strtonum($2)} END {print s}' "$SMALL/sizes.txt") bytes)"
