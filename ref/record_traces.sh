#!/bin/sh
# Re-author the bot-driven trace inputs (card G1.2). ref/traces/src/Txx.txt may use stick_x 99
# ("bot steers", optional 5th column = lateral target); the reference resolves it once and
# writes plain inputs to ref/traces/Txx.txt, which the gate replays. Run after changing a
# source script or the fixture, then review and run ref/run_corpus.sh --update.
set -eu
cd "$(dirname "$0")/.."
python3 ref/make_fixture.py build/ref/fx/fixture.bin >/dev/null
make -s -C ref >/dev/null
for src in ref/traces/src/T*.txt; do
    t=$(basename "$src" .txt)
    build/ref/gcc-O2/ref --course build/ref/fx/fixture.bin --script "$src" \
        --trace "build/ref/fx/$t-record.tsv" --record "ref/traces/$t.txt" --bot 1,0.5,60 >/dev/null 2>&1
    head -1 "$src" | sed 's/^#/# Source:/' | cat - "ref/traces/$t.txt" > "ref/traces/$t.tmp" && mv "ref/traces/$t.tmp" "ref/traces/$t.txt"
    echo "$t: $(python3 ref/trace_events.py "build/ref/fx/$t-record.tsv" | tr '\n' ' ')"
done
