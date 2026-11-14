# Handoff

Keep under 50 lines. Overwrite, do not append.

## Next card

G1.3 tick cost model: instrument the reference (count float ops by kind per function per tick
over T01-T10 and M01-M05), then an SH-2 microbenchmark image timing SH2ZAM fixed-point ops in
high work RAM and from the cartridge.

## Open threads

- ROM lane needs the owner's ROM each session: `FZX_TEST_ROM=... FZX_EVIDENCE=<outside git>
  ref/run_rom_lane.sh`. Last run: M03 finish at tick 6727, hash 3778b5c92f7669da.
- Camera state is stepped in the reference but not in the trace hash.
- G1.4 render probe: why the Saturn main loop gives PIC/FLD = 0.5 (30 fps), then the polygon and
  fill sweep.
- `BLOCKED_EMULATOR_PAD` (docs/EMULATOR.md). Not needed until G4.3.
- Tools in this container: `sudo apt-get install gcc-multilib clang gdb yabause-qt xvfb xdotool
  imagemagick`; `dockerd &` for Saturn builds; `pip install libclang` for the census tool.
- Local clang lacks ASan runtimes; `make sanitize CC=gcc` works here, CI runs clang.

## Fields still fed from the reference

- Not applicable until Gate 3.
