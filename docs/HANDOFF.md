# Handoff

Keep under 50 lines. Overwrite, do not append.

## Next card

G1.4 polygon/fill sweep (the 30 fps cause is found: prototype `fzx_render` costs ~447k cycles a
picture on the CPU; VDP1 is not yet the limit). Then G1.7 fit table and cartridge detect, G1.8
`docs/GATE1.md` and the owner's GO / NO-GO question.

## Performance roadmap (owner: 60 fps on hardware, as fast as the Saturn allows)

Both repositories are maintained together (AGENTS.md "Improve SH2ZAM as you go").

SH2ZAM, in order of modelled payoff for this tick (`docs/cost/`):
1. `sh2z_div` with DIV1 steps: lamb2k/SH2ZAM#1 (div/recip 266 to 104 cycles, rsqrt 689 to
   534; exact). On merge, move the pin here and rerun host, web, Saturn and bench checks.
2. Exact fast `sh2zi_isqrt`: normalise, table seed, reciprocal-sqrt Newton with DMULU, then an
   exact +-1 correction against the 64-bit square. Feeds sqrt (445), rsqrt, length and
   normalize (1,115). Oracle: the existing root_property tests; model run must stay exact.
3. Measure the result in `saturn/bench` (straight translation 2,485, rsqrt shape 1,934 today).

This repo:
1. Renderer v2 (G2.3): baked chunk vertices, original chunk order instead of the global sort,
   transform only the visible window; then the master/slave split (G4.4). This is where the
   current 30 fps goes.
2. Convert the tick functions in the cost ranking (`docs/cost/cycle_model.tsv`), each against
   the reference traces.
3. Hardware: the bench and main images run unchanged on a console; the owner's hardware sheet
   (G4.5) is the only source of a 60 fps claim.

## Open threads

- Emulator gaps: no pad, no RAM cartridge (ID FF, RAM probe fails), timing ~6% off; see
  docs/EMULATOR.md. Owner asked about a console or another emulator (Kronos, Mednafen).
- ROM lane needs the owner's ROM each session: `FZX_TEST_ROM=... FZX_EVIDENCE=<outside git>
  ref/run_rom_lane.sh`. Last run: M03 finish at tick 6727, hash 3778b5c92f7669da.
- Camera state is stepped in the reference but not in the trace hash.
- Tools in this container: `sudo apt-get install gcc-multilib clang gdb yabause-qt xvfb xdotool
  imagemagick`; `dockerd &` for Saturn builds; `pip install libclang` for the census tool.
- Local clang lacks ASan runtimes; `make sanitize CC=gcc` works here, CI runs clang.

## Fields still fed from the reference

- Not applicable until Gate 3.
