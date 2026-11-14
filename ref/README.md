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

## Known gaps

- **Event coverage.** With open-loop scripts the fixture racer grinds the walls and retires
  around tick 700 on every trace, so T05 to T09 do not yet reach boost, pit, dash plate or three
  laps. Next: generate those input scripts with a closed-loop bot run once in the reference.
- Fixture tuning is not Blue Falcon's. Retail tuning, Mute City 1 and its traces are ROM-lane.
- Camera state is stepped but not in the trace or hash yet.
