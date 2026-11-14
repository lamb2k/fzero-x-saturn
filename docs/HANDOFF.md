# Handoff

Keep under 50 lines. Overwrite, do not append.

## Next card

G1.3 tick cost model: instrument the reference (count float ops by kind per function per tick
over the corpus), then an SH-2 microbenchmark image timing SH2ZAM fixed-point ops in high work RAM
and from the cartridge. Before that, close the G1.2 coverage gap below so the counts include
boost, pit, dash and lap code.

## Open threads

- G1.2 event coverage: open-loop scripts retire the fixture racer near tick 700, so T05-T09 miss
  boost, pit, dash and laps. Generate their inputs with a closed-loop bot run once in the
  reference, keep the inputs as scripts, then `ref/run_corpus.sh --update` after review.
- Camera state is stepped in the reference but not in the trace hash.
- G1.4 render probe: why the Saturn main loop gives PIC/FLD = 0.5 (30 fps), then the polygon and
  fill sweep.
- `BLOCKED_EMULATOR_PAD` (docs/EMULATOR.md). Not needed until G4.3.
- Tools in this container: `sudo apt-get install gcc-multilib clang gdb yabause-qt xvfb xdotool
  imagemagick`; `dockerd &` for Saturn builds; `pip install libclang` for the census tool.
- Local clang lacks ASan runtimes; `make sanitize CC=gcc` works here, CI runs clang.

## Fields still fed from the reference

- Not applicable until Gate 3.
