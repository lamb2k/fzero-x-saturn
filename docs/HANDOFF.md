# Handoff

Keep under 50 lines. Overwrite, do not append.

## Next card

G1.4 render probe: why the main loop finishes one picture per two fields (PIC/FLD = 0.5), then
the polygon and fill sweep. Beside it: G1.1 census and G1.2 reference (need the decomp
submodule: `git submodule update --init upstream/G-Diffuser` and its `decomp`).

## Open threads

- `BLOCKED_EMULATOR_PAD` (docs/EMULATOR.md). Next attempt: Kronos from source, or Yaul INTBACK
  diagnosis under Yabause. Not needed until G4.3.
- Local clang in the agent container lacks ASan runtimes; `make sanitize CC=gcc` works here,
  CI runs clang.
- Local Saturn builds: start `dockerd`, then the README docker command.

## Fields still fed from the reference

- Not applicable until Gate 3.
