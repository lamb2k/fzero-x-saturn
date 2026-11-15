#!/bin/sh
# Refuse stale or hand-edited generated files.
# Usage: tools/check_receipt.sh generated/course.receipt
# The receipt is written by tools/import_course.py. Every output it lists must
# still match its hash, and the generator must be the importer in this tree.
set -eu
receipt=${1:?usage: check_receipt.sh RECEIPT}
tools_dir=$(cd "$(dirname "$0")" && pwd)
out_dir=$(cd "$(dirname "$receipt")" && pwd)

fail() {
    printf 'check_receipt: %s\n' "$1" >&2
    printf 'check_receipt: regenerate with tools/import_course.py; never edit generated files.\n' >&2
    exit 1
}
hash_of() { sha256sum "$1" | cut -d ' ' -f 1; }

[ -f "$receipt" ] || fail "missing receipt $receipt"
head -n 1 "$receipt" | grep -q '^# fzx generation receipt v1' || fail "unknown receipt format in $receipt"

outputs=0
generator_seen=0
while read -r kind hash name; do
    case $kind in
        '#'*|'') ;;
        generator)
            generator_seen=1
            [ -f "$tools_dir/$name" ] || fail "generator $name not found"
            [ "$(hash_of "$tools_dir/$name")" = "$hash" ] ||
                fail "stale: $name changed since these files were generated"
            ;;
        input) ;;
        output)
            outputs=$((outputs + 1))
            [ -f "$out_dir/$name" ] || fail "missing generated file $name"
            [ "$(hash_of "$out_dir/$name")" = "$hash" ] ||
                fail "stale: $name does not match its receipt (edited or partly regenerated)"
            ;;
        *) fail "unknown receipt line: $kind" ;;
    esac
done < "$receipt"

[ "$generator_seen" = 1 ] || fail "receipt names no generator"
[ "$outputs" -gt 0 ] || fail "receipt lists no outputs"
printf 'check_receipt: %s outputs current\n' "$outputs"
