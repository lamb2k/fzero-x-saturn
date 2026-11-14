# Handoff

Keep under 50 lines. Overwrite, do not append.

## Next card

G1.2 reference harness `ref/`: build the TICK + OFFLINE closure from `docs/census/closure.tsv`
natively in float (-m32, binary32, -ffp-contract=off), trap-stub everything else, then run the
G1.1 control (PORT defined changes the hash). Census tool needs `pip install libclang` and the
decomp submodule (`git submodule update --init upstream/G-Diffuser`, then its `decomp`).
Also open: G1.4 render probe (why PIC/FLD = 0.5), then the polygon and fill sweep.

## Open threads

- `BLOCKED_EMULATOR_PAD` (docs/EMULATOR.md). Next attempt: Kronos from source, or Yaul INTBACK
  diagnosis under Yabause. Not needed until G4.3.
- Local clang in the agent container lacks ASan runtimes; `make sanitize CC=gcc` works here,
  CI runs clang.
- Local Saturn builds: start `dockerd`, then the README docker command.

## Fields still fed from the reference

- Not applicable until Gate 3.
