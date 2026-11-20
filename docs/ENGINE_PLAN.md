# Engine plan: hybrid sprite and native Saturn engine

Owner direction, 2026-11-19:
- "We need to rearchitect into a hybrid Saturn 2d 3d game using the original assets."
- "30 is the floor." "You can't have a racer at 20."
- "Correct play right and Saturn exclusive."
- "everything should be sh2zam."
- "No reason to run the original code it won't work."

This page is the plan for the engine line. It replaces the gate order of
[POC_PLAN.md](POC_PLAN.md) for new work. POC_PLAN stays as the record of the
legacy line and for its memory, evidence and source-art rules, which still apply.

Current state, retrospective and the plan from here (2026-11-20):
[RETRO_PLAN_20261120.md](RETRO_PLAN_20261120.md). Its phases P0 to P5 order the work after E0 to E4.

## 1. Why the old path could not get there

Measured on the legacy recipe, emulator, timing modelled:

- One racer costs 65.6 ms a frame. The floor is 33.3 ms with 30 racers.
- 22 ms of that is the original race code in soft float. 17 ms converts its
  state into draw data. Neither shrinks by tuning.
- The original code and its buffers fill about 513 KB of the 1 MB of high
  work RAM. New code had 112 bytes of stack margin left.
- Every change had to keep the legacy image byte-identical with its switch
  off and replay 7,059 ticks exactly. That is the right rule for tuning the
  old engine and the wrong rule for replacing it.
- Each change went through its own review round. About 40 hand-overs in two
  days left the reviewer no time for its own lanes.
- Nothing has run on a Saturn. The emulator does not charge for two CPUs
  sharing the bus or for DSP transfers.

## 2. The decision: a separate engine image

The engine is its own Saturn image with its own main loop. It links no
original game code. It is not a set of switches inside the legacy image.

| | Legacy line | Engine line |
| --- | --- | --- |
| Branch | `gate1` | `engine` |
| Purpose | frozen fallback; HOST reference; front-end work until E3 | the game we ship |
| Runtime code | original race code plus native presentation | SH2ZAM components plus game glue |
| Physics gate | exact replay, finish at tick 7059 | plays-right checks against the HOST reference |
| Change gate | switch-off binary identity | the engine gates in section 5 |
| New optimisation work | none | all of it |

The legacy recipe BIN stays frozen. One check guards it: it must still build
byte-identical. No further speed work lands on it.

## 3. What the engine is

| Layer | Saturn form | Source |
| --- | --- | --- |
| Sky, skyline | VDP2 back screen and scroll | original art |
| Floor, clouds | VDP2 RBG0, parameters A and B | original textures |
| Track | baked per-segment meshes with LODs on the cartridge; picked, transformed and emitted on the slave SH-2 | original course data |
| Racers, player included | baked rotation sprites; VDP1 distorted sprite for roll and perspective | original machine models |
| Track-side objects, effects | VDP1 sprites | original art |
| Player physics | native fixed point | original machine stats and course data |
| Rivals | native line followers with AI and distance LOD | original CPU-line data |
| Front end, HUD | the C8 screens, moved onto the engine at E3 | original UI art |
| Audio | SCSP from original samples and sequences | original audio |

CPU split: the master runs input, physics, rivals, VDP2 updates, sprite
selection and submission. The slave builds the track. The SCU DSP takes
batch transforms only where a hardware run shows a gain. VDP1 uses two
command banks so building overlaps drawing.

All engine code is an SH2ZAM component with a generic name. The game
repository holds glue, game data, and the bakers that build assets from the
owner's ROM at build time. Nothing derived from the ROM is committed.

## 4. Milestones

Each milestone is a vertical slice that runs, with stills, a short clip and
frame numbers. A milestone is done when its gates pass and the owner accepts
its look and feel.

| | Slice | Proves | Main lanes |
| --- | --- | --- | --- |
| E0 | Track flythrough: engine image boots to the first course; VDP2 backgrounds, baked track on the slave, double banks; the camera rides the baked racing line | the render budget holds the floor before any physics exists | track, frame, build |
| E1 | Drive: native player physics, sprite player, basic HUD, laps and finish | handling plays right; one racer at the floor | physics, racers |
| E2 | Race: 29 rivals with AI, contact, rank, a full Grand Prix race | 30 racers at the floor | rivals, racers |
| E3 | Game: front end, records and retry on the engine; whole roster; more courses through the bakers | the complete loop | Codex C8 and cars; track |
| E4 | Sound and finish: audio, effects, track-side objects, soak | release candidate | audio, effects |

Hardware: the owner runs each milestone's disc on a Saturn with the sheet in
[HARDWARE_TEST.md](HARDWARE_TEST.md). E0 is the first run. Until then every
timing is modelled, and the floor is not claimed.

E0 status (2026-11-19, Ymir, MODELLED; hardware pending): default image on the 3-lap route at 40.8 fps with 0 intervals over two fields. Owner look decisions: road LOD shift 2 ("go with shift 2"); stable chunk cap 26 ("Yes stable cap 26 should be the default", replacing "cap 32"). With the stable cap the slave p99 is 21.8 ms (33 rows over 22 ms), inside the 22 ms margin target; chunk pops over 3 laps 896 against 2,932 at the old cap 32. Open: the hardware run and the full-scene HOST = SH-2 equality (C-061 item 3).

Already built and reused by E0 to E2: VDP2 clouds and floor preparation,
the slave job channel, the segment-mesh picker, rotation sprites, the line
follower, the double-bank manager, the low-RAM helpers.

## 5. Engine gates

Checked per lane merge by the lane itself, and audited per milestone:

1. **Frame floor.** At most two 60 Hz fields between newly presented frames.
   Report mean, p99, worst interval and the count over budget. A passing
   p99 does not establish the floor.
2. **Plays right.** Fixtures, inputs, units, reference values and tolerances
   are agreed before the checkpoint: lap and race time, speed and turn
   response, boost and energy, wall and vehicle contact, rival pace. Discrete
   rules are exact: checkpoint order, laps, finish, retry reset, records.
3. **Determinism.** The same inputs and seeds give the same state hash on
   every run, and the HOST build of the fixed-point code matches the SH-2
   build exactly.
4. **Look.** Owner-reviewed still pairs or clips against the original.
5. **Memory.** Master stack at least 20,480 B unused; slave stack at least
   4 KB untouched with guards; work RAM, VRAM and cartridge maps with no
   overlaps.
6. **Budgets and guards.** VDP1 command budgets, pack guards, no crash or
   hang on the route.
7. **Controls.** Every check ships with an input that makes it fail.
8. **Legacy untouched.** The legacy recipe BIN still builds byte-identical.
9. **Margin.** Until hardware numbers exist, the modelled critical path
   (master busy plus its wait for the track) at p99, times 1.36, stays within
   33.3 ms: 24.5 ms modelled. A per-CPU cap is not the measure.
10. **Matrix.** Gates 1 to 9 hold on the default image as shipped, with sound
    off and with scenes off, and for race systems on two courses, one with
    tubes, using real packs. The list is in AGENTS.md under Workflow.

## 6. What stops

- Speed work on the legacy image, including the simulation pipeline and the
  road-plan rewrite.
- New switches inside the legacy image for engine features.
- Promotion reviews of legacy-only changes. Open ones finish only if the
  engine reuses the code.
- Review rounds on diagnostic tools before they are used. Tools carry their
  own failing controls and are audited at the milestone.

## 7. Lessons carried in from the MvC and AvP Saturn ports (2026-11-19)

Sources: the MvC repository's `docs/notes/CAPREF.md` (a study of Capcom's own Saturn MvC), `AVP-REFERENCE.md` and MvC `docs/STATUS.md`.

1. **Emulator margin.**
   - On the AvP project the console measured about 36% worse than the emulator model.
   - Until our own hardware run, an engine slice counts as holding the floor only if it fits about 22 ms per presented frame in the emulator (33.3 ms / 1.5) and keeps the two-field rule.
   - A hardware run replaces this margin with real numbers.
2. **Sprite art stays compressed on the cartridge and is decoded straight into VDP1 VRAM,** one picture before it is shown, with no staging copy.
   - That is how Capcom's MvC handles fighter art: about 0.56 of raw size, and at most about 5.5 KB of new tiles in one vblank.
   - It applies to the racer atlases, currently 1.7 MB raw on the cartridge.
3. **A fixed pool of VDP1 slots, least recently used first,** with live-frame protection.
   - Nothing on screen or in the list being drawn is evicted.
   - The slave decodes into free slots ahead of need.
4. **Flip with the VDP1 command, not a mirrored copy,** wherever the art is symmetric. Bake both sides only where a decal or detail breaks symmetry.
5. **Change flags on everything uploaded per frame.**
   - This covers palettes, VDP2 coefficient and band tables, and window tables.
   - Upload only what changed. MvC's palette dirty marks cut its hand-off vblank by more than half.
6. **Cartridge code runs slow in Yaba,** so judge cartridge-resident hot code on Ymir or hardware. Keep hot code in high work RAM.
7. **VDP1 and tile writes by DMA** wait for a console timing, as on MvC. CPU copy stays the default.

**Measured for the racers (2026-11-19, SH2ZAM PR #42 `sprite-cache-1`; HOST plus an SH-2 cycle model):**
- **Decode cost:** decoding a compressed cell during the race costs about 1.9 ms per near cell. That is too slow for misses.
- **So the racers:** keep the art compressed on disc (cell4z, about 0.4 of raw), decompress it into the cartridge at race load, and fill a shared 89.6 KB VDP1 slot pool by CPU copy on the slave. That replaces 254 to 310 KB of resident sets.
- **Synthetic 30-racer race:** a miss on 4.4% of draws, and 0.2% of draws show the previous cell for one frame.
- **The point:** MvC decodes during play because its art arrives in small tiles. Ours doesn't.

SH2ZAM already has the MvC kernels for this, ready to reuse: `vlc-1`/`vlc-2` (bit reader and decode) and `memo-1` (block memo and sync).
