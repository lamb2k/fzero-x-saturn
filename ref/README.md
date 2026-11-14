# Float reference (card G1.2)

The judge for the fixed-point core. It compiles the decomp's own one-racer closure natively in
binary32 float and writes a per-tick trace. It is never linked into the Saturn or web builds.

## What is compiled

`racer.c`, `course.c` (with `course_chunk_build.inc`), `course_gadgets.c`, `camera.c`,
`sys/math.c`, `EA90.c`, `EA20.c` and `common.c` from the pinned decomp, unmodified except for one
mechanical rewrite of `racer.c` (hardware reads, below). `PORT` and `EXPANSION_KIT` are
undefined. Everything else the closure references becomes a generated stub
(`gen_stubs.py`): functions are counting no-ops listed after every run, data is zeroed storage.
The stub list is the dynamic check on the census: today it is audio, CPU AI, effects, HUD, save,
drawing and N64 platform calls only.

Harness side (`ref_game.c`, `ref_platform.c`, `ref_main.c`):

- boot as the game does: `Math_SinTableInit`, `func_8008D33C` (default machines);
- menu state for a one-player time attack on course slot 0, then `Race_Init` minus OUT work
  (`Course_Init`, `Racer_Init`, `Camera_Init`);
- per tick: the pad latch as `Controller_UpdateInputs` leaves it, then `Race_Update`'s order minus
  OUT steps: `Racer_Update`, `Camera_Update`, `Course_Update`, `func_80074844`;
- ROM DMA is served at `osEPiStartDma`: reads inside the course-data segment come from the course
  record (byte-swapped to host order); other reads are drawing assets, counted and ignored;
- the starting grid (`func_i3_80115E74`, cpu.c) is its one-racer reduction;
- `ROM_READ`/`IO_READ` in `racer.c` go to `ref_rom_read32`/`ref_io_read`. The only ROM reads are
  the 38 machine-tuning words in `Racer_InitMachineStats`: `--rom` takes them from a US rev0 ROM
  (ROM lane), otherwise an authored constant (`REF_FIXTURE_ROM_WORD`, not retail).

## Float discipline

`-m32 -msse2 -mfpmath=sse -ffp-contract=off -fno-fast-math`; GCC also uses
`-fexcess-precision=standard`, Clang `-ffp-eval-method=source`. `sqrtf` is pinned to
`ref_sqrtf` (SSE `sqrtss`, correctly rounded binary32 like VR4300 `sqrt.s`): glibc's i386
`sqrtf` returns an unrounded x87 result, which Clang -O2 kept in double and GCC rounded. That
made Clang -O2 one ulp off in `func_8009DB28`'s spline tensions; the importer's independent
implementation agrees with the rounded value.

## Trace format v1

Header lines start with `#` and carry the course segment count and a hash of the OFFLINE course
values (tension and length per segment). Then one row per tick: inputs, state flags, segment
index, spline t, position, velocity, speed, energy, lap, laps completed, race distance (floats as
hex bit patterns) and a state hash: FNV-1a 64 over the whole `Racer` struct with its four pointer
members nulled, plus the segment index. The run hash chains the course hash and every tick hash.

## Gate

```sh
sudo apt-get install -y gcc-multilib clang
git submodule update --init upstream/G-Diffuser && git -C upstream/G-Diffuser submodule update --init decomp
ref/run_corpus.sh            # build four ways, run T01..T10, compare, run both controls
ref/run_corpus.sh --update   # after a reviewed change only
```

Inputs are `traces/T01.txt` .. `T10.txt` (authored; T10 is a seeded fuzz). The fixture course is
`make_fixture.py`: a 12-point walled-road ellipse with one pit and one dash plate. Expected
hashes: `traces/expected.tsv`.

## Authoring inputs: the bot

A script line with stick_x `99` means "auto": a centre-line bot steers (offset from the spline
and heading, measured along the track's side axis), with an optional fifth column giving a
lateral target. `ref --record OUT.txt --bot 1,0.5,60` writes the inputs actually used as a plain
script. Sources live in `traces/src/` and `traces/rom/`; `record_traces.sh` regenerates
`traces/T*.txt`. The gate replays the recorded inputs, never the bot.

`--stop-at-finish 1` ends a run on the tick the racer finishes. After the finish the game drives
the racer through `Cpu_GenerateInputs`, a stub here, so any run in which that stub ran exits 3
as INVALID and fails the gate.

## Coverage (`trace_events.py`)

Synthetic fixture v2 (12 000 x 8 000 walled-road ellipse, half-width 150, authored tuning):
T05 boost chain (energy 121 to 68 on B taps), T07 dash plate, T09 three laps to the finish.
The fixture's pit never registers (its effect vectors are axis-aligned and the strips sit on a
diagonal); T06 stays as a pit-lane pass.

ROM lane (`run_rom_lane.sh`, Mute City 1, retail tuning; outputs stay in `$FZX_EVIDENCE/ref`):
M01 idle, M02 throttle into the first wall, M03 three laps to the finish, M04 boost chain and pit
recharge, M05 seeded fuzz. Dash plate, jump (airborne), wall hit, boost and pit all occur.

## Known gaps

- Fixture tuning is not Blue Falcon's; retail tuning and Mute City 1 are ROM-lane only.
- Camera state is stepped but not in the trace or hash yet.
- Data stubs are 64 KB each; a missing storage file can still hide behind one (game_context.c
  did). Sizing stubs from the decomp's symbol_addrs.txt would catch it.
