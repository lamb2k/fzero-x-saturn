# Handoff

Keep under 50 lines. Overwrite, do not append.

## Next card

G1.3 control: convert `Math_OrthonormalizeAroundForward` to SH2ZAM, build a Saturn image that
times it (FRT) in high work RAM and from the cartridge, compare with `docs/cost/` (MODELLED).
Then the G1.4 polygon/fill sweep (the 30 fps cause is found: prototype fzx_render costs a whole
field on the CPU; VDP1 is not yet the limit), G1.7 fit table and
cartridge detect, G1.8 `docs/GATE1.md` and the owner's GO / NO-GO question.

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
