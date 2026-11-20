#!/usr/bin/env bash
# Build the duel reference (attacks lane): the original race code with two pad-driven racers.
#   ref/duel_build.sh [CC] [OPT]
# Output: ref/build/<cc><opt>-duel/fzx-duel. As ref/handling_build.sh, from a private edited copy of
# ref/build.sh, which is never changed.
#
# racer.c is compiled from a private copy with logging hooks (C-092: the reference's own predicate
# decisions, not overlap guesses). Each hook only records values the original already computed, and
# the closing hook returns the original's own comparison unless a branch replay forces one tick
# (ref_duel_force_*). The hooks are placed by exact whole-line matches; a decomp change fails the build.
set -euo pipefail
CC=${1:-gcc}
OPT=${2:--O2}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
D=$ROOT/upstream/G-Diffuser/decomp
OUT=$ROOT/ref/build/${CC}${OPT}-duel
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$OUT"

R=$TMP/racer.c   # the decomp's racer.c without its byte-order mark and carriage returns
sed -e '1s/^\xEF\xBB\xBF//' -e 's/\r$//' "$D/src/game/racer.c" > "$R"
P=$OUT/racer_duel.c
want_once() { # the anchor line occurs exactly once in racer.c (fixed string, whole line)
    local n
    n=$(grep -cxF -- "$1" "$R" || true)
    [ "$n" = 1 ] || { echo "duel_build: racer.c anchor found $n times: $1" >&2; exit 1; }
}
A_KO='    gRacersKOd++;'
A_DMG='void Racer_ReceiveDamage(Racer* racer, f32 damageStrength) {'
A_FIRST='                        var_fs0 *= 1.1f;'
A_TOUCH='                    racerPair->areColliding = true;'
A_CLOSE='                    if (((sp110 * xVelocityDiff) + (sp10C * yVelocityDiff) + (sp108 * zVelocityDiff)) <= 0.0f) {'
A_PUSH='                        racer2->stateFlags |= RACER_STATE_COLLISION_RECOIL;'
for a in "$A_KO" "$A_DMG" "$A_FIRST" "$A_TOUCH" "$A_CLOSE" "$A_PUSH"; do want_once "$a"; done
{
    echo "#include \"$ROOT/ref/duel_hooks.h\""
    echo '#line 1 "src/game/racer.c"'
    awk -v ko="$A_KO" -v dmg="$A_DMG" -v first="$A_FIRST" -v touch="$A_TOUCH" -v closing="$A_CLOSE" -v push="$A_PUSH" '
        $0 == ko    { print; print "    ref_duel_ko(arg0, temp_v0, arg1);"; next }
        $0 == dmg   { print; print "    ref_duel_damage(racer, damageStrength);"; next }
        $0 == first { print; print "                        ref_duel_ctx = 1;"; next }
        $0 == touch { print "                    ref_duel_ctx = 0;";
                      print "                    ref_duel_touch(racer, racer2, racerPair->areColliding, sp128, racerPair->areColliding ? 0.0f : var_fs0, var_fs2, var_fs1);";
                      print; next }
        $0 == closing { print "                    if (ref_duel_closing(racer, racer2, sp110, sp10C, sp108, xVelocityDiff, yVelocityDiff, zVelocityDiff,";
                      print "                                         (sp110 * xVelocityDiff) + (sp10C * yVelocityDiff) + (sp108 * zVelocityDiff))) {"; next }
        $0 == push  { print; print "                        ref_duel_push(racer, racer2, var_fs1, var_fs2, var_fa0, var_fa1, var_ft4);"; next }
        { print }' "$R"
} > "$P.tmp"
[ "$(grep -c 'ref_duel_' "$P.tmp")" = 7 ] || { echo "duel_build: hook placement count" >&2; exit 1; }
mv "$P.tmp" "$P"

sed -e "s|^ROOT=.*|ROOT=$ROOT|" \
    -e 's|^OUT=$ROOT/ref/build/${CC}${OPT}$|OUT=$ROOT/ref/build/${CC}${OPT}-duel|' \
    -e 's|^BRIDGES="bridge hooks"$|BRIDGES="bridge hooks handling_probe course_line duel_probe rivals_bake"|' \
    -e 's|^DRIVER=main$|DRIVER=duel_main|' \
    -e 's|^        compile "$D/$s" "$o"$|        if [ "$s" = src/game/racer.c ]; then compile "$OUT/racer_duel.c" "$o"; else compile "$D/$s" "$o"; fi|' \
    "$ROOT/ref/build.sh" > "$TMP/build.sh"
for want in "ROOT=$ROOT" 'OUT=$ROOT/ref/build/${CC}${OPT}-duel' 'BRIDGES="bridge hooks handling_probe course_line duel_probe rivals_bake"' 'DRIVER=duel_main' \
            '        if [ "$s" = src/game/racer.c ]; then compile "$OUT/racer_duel.c" "$o"; else compile "$D/$s" "$o"; fi'; do
    grep -qxF "$want" "$TMP/build.sh" || { echo "duel_build: ref/build.sh changed; cannot place: $want" >&2; exit 1; }
done
bash "$TMP/build.sh" "$CC" "$OPT"
