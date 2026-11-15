# F-Zero X Saturn: one-track POC plan

Plan for this repository, written against commit `33a94ec`.
Written 2026-11-13 for execution by an agent in Claude Code, with the owner as reviewer.

Sources of the lessons: two earlier private Saturn projects, an arcade port ("the arcade port":
agent rules, handoffs, learning notes, test plan, ADRs) and its porting toolkit ("the toolkit":
playbook and skills, the "recomp" side), and `lamb2k/re4dc`
(AGENTS, CLAUDE, hardware-translation notes, the "decomp" side).

**Owner decisions, 2026-11-13:** the target is **60 frames per second**, and the build **requires the 4 MB RAM cartridge**
(confirmed by the owner; the 1 MB cartridge is not a target).

---

## 1. Outcome

One track of the real game, driven with the real physics, on a Saturn.

| Tier | Name | Exit condition |
| --- | --- | --- |
| **T** | Technical POC | Mute City 1 from the owner's ROM, Blue Falcon, one racer. The original one-racer simulation runs in the shared fixed-point core and passes the differential gates against the float reference. The Saturn image boots on an emulator and completes a lap from pad input only. Frame cost, memory and parity numbers are recorded with their tiers. |
| **P** | Playable POC | T, plus the race loop from the source rules: countdown, three laps, timer, energy, boost, pit, dash plates, wall damage, finish, retry. HUD on VDP2. A new picture every field (60 fps NTSC) over the three-lap route, counted as missed fields. Ten-minute soak without fatals. The owner has played it and answered the review question. |

T is the commitment. P follows from T without a new architecture. If Gate 1 says NO-GO for P, T still stands as the deliverable.

**What 60 fps means here.** One simulation tick and one finished picture inside every 16.67 ms field: about 477,000 cycles per SH-2 per field, and VDP1 must finish drawing in the same field. A late picture shows the previous one and is counted as a missed field; the simulation still ticks. The architecture is built for this from the first card. An emulator can only give a MODELLED answer: "60 fps" is a HARDWARE-tier claim and stays unproven until the owner runs the sheet on a console. Any lower rate is the owner's decision, never a quiet fallback.

**Not in this POC:** rivals and AI, audio, menus, saving, other tracks, other machines, track shapes the census does not find on Mute City 1, 64DD content, textures before T, physical-hardware acceptance (the sheet ships; the run is the owner's).

---

## 2. Where the repo stands

Verified by reading the tree, not taken from the README:

- Shared C core (`src/fzx.c`, 417 lines): Q16.16 spline evaluation, sampled flat ribbon, clipping, global depth sort, flat RGB1555 polygons, 512-polygon cap. No heap, no float.
- `fzx_step` is a **prototype driver** that follows the sampled centreline. It is labelled as such. None of the original racer code is ported.
- Saturn backend on Yaul (`saturn/main.c`): VDP1 polygons, VDP2 debug-console HUD, digital pad, 60 Hz tick with bounded catch-up, master SH-2 only. CI builds the ISO. **It has never been booted**, on an emulator or a console.
- Importer (`tools/import_course.py`) reads Mute City 1 from US rev0. **It has only ever run on synthetic records**; no ROM was available.
- Web workbench: the same core in WebAssembly, with Playwright tests.
- Upstream pinned: G-Diffuser `4f93206`, decomp `Zorkats/fzerox` `b944a41`, SH2ZAM `3374d77`, Yaul image by digest.

Facts about the decomp that shape the plan (measured at the pinned commit):

| Fact | Consequence |
| --- | --- |
| `racer.c` is 8,510 lines; `Racer_UpdateFromControls` alone is about 1,750; `Racer_Update` about 620. `course.c` 4,824, `camera.c` 3,702, `sys/math.c` 1,208. | The physics port is the bulk of the work. Slice it; never port it in one pass. |
| The game is float throughout (`f32`), and the SH-2 has no FPU. | Float to fixed is the central technical risk. It gets its own gate before anything large is built. |
| The pinned decomp is **G-Diffuser's fork**: `racer.c` carries 62 `PORT` / `EXPANSION_KIT` / `Gdx` / `CVar` references, `course.c` 30. | The reference must build with `PORT` and `EXPANSION_KIT` undefined, and every hook left in the call graph must be shown inert. Otherwise the "original physics" are a PC port's physics. |
| `NON_MATCHING` markers: `racer.c` 6, `course.c` 4, `math.c` 4. | Flag any that land in the one-racer closure; they are equivalent by claim, not by bytes. |
| `GAMEMODE_TIME_ATTACK` exists in `fzx_game.h`. | Candidate source path for a one-racer race. Confirm in the census; do not invent a mode. |
| `sDefaultMachines` is a C table in `racer.c`. | Some tuning is in CC0 source and needs no ROM. Anything under `assets/` needs the ROM. Classify each datum. |
| The course draw is ordered traversal (`Course_DrawForward…` / `Course_DrawBackward…ChunkGroup`) over eight shapes (`TRACK_SHAPE_*`). | The original already draws the track in a camera-relative order. That order is a painter order for VDP1, which has no z-buffer. Road chunks are quads, which is VDP1's native primitive. |

---

## 3. Lessons carried over

Each row is a rule this plan applies, with where it was learned.

| # | Lesson | Learned from | Rule here |
| --- | --- | --- | --- |
| 1 | The arcade port ran on a mixed architecture with authored gameplay until a full reset. A prototype quietly became the game. | arcade port reset audit | The prototype driver stays behind `FZX_SIM=proto` and is named on screen. It can never be reported as the port. Default flips only when the trace gate passes. |
| 2 | Measure before building. Phase 1 is a written GO or NO-GO with numbers. | toolkit playbook, sh2-game-logic | Gate 1 below. Nothing in Gates 2 to 5 starts before it. |
| 3 | The arcade port's interactive play was blocked for months by a missing tick contract: the image picked updates from captured cadence tables. | arcade port handoff, learning notes | The tick contract is written first (section 4.4). The simulation consumes current state and current input only. Traces exist only inside tests and the `AUTOTEST` build. |
| 4 | Lift first, prove it against the reference, then make it readable and fast, one function at a time, each swap gated. | sh2-game-logic | The float decomp is the "lifted" tier here. Fixed-point functions replace it slice by slice, each behind the same gate (section 4.2, Gate 3). |
| 5 | Source owns simulation and state; the native layer owns presentation. Never alter collision, event order, state or AI for speed. | re4dc AGENTS | Renderer and platform changes must leave the simulation trace STRICT-identical. |
| 6 | Every optimisation lands behind a knob that defaults off, with knob-off identity, one cost arm and one STRICT gate. | re4dc contribution rule | Same rule, same words. Applies from Gate 4 on. |
| 7 | Evidence tiers never promote each other: HOST, reference, EMULATOR, HARDWARE, MODELLED. | toolkit emulator-testing; arcade port test plan | Section 5. Emulator timers are MODELLED for frame cost. Hardware is UNRUN until the owner runs it. |
| 8 | A passing gate needs a control that can fail. A hardcoded zero is not evidence. Mutation counts and row-match percentages are not playability. | arcade port agent rules | Every card names a negative control. Reports state playable extent first, numbers second. |
| 9 | Float results drift. re4dc split comparisons into STRICT (tick-identical) and DISCRETE (same decisions). | re4dc CLAUDE notes | Three parity classes (section 4.3). Raw differences are reported next to any normalised ones. |
| 10 | Sampling one frame-count residue gave re4dc a wrong "budget met". | re4dc, 2026-09-26 | Cost is measured over every tick of a fixed route: mean, p95, max. Never one sampled frame. |
| 11 | Emulators hide faults (Flycast hid SH-4 misaligned access; Yaba slowed from 60 to 20 vblanks/s within a minute). | re4dc, toolkit | Short bounded runs, fresh process per run, one emulator instance per lane. Build with alignment-safe access; do not treat an emulator boot as a hardware claim. |
| 12 | Pad-only proofs. A bot that pokes memory proves flow, not play. | toolkit lesson 4 | Proofs of play use pad input through the normal latch. Anything else is labelled DIAGNOSTIC. |
| 13 | Pair before fixing: an apparent port bug was the arcade original's own effect. | toolkit lesson 5 | Before calling a visual or handling difference a bug, compare with the reference (G-Diffuser still or reference trace). |
| 14 | Kept intermediates went stale and produced phantom tile bugs. | toolkit lesson 1 | Generated files carry a receipt (hashes of generator, inputs, output). Builds refuse stale output. Re-check any odd bug on a clean build first. |
| 15 | The arcade port's state file grew past 100 KB with single lines thousands of characters long; the agent rules had to forbid rereading it. | arcade port state file | One `docs/STATUS.md` under 80 lines. One handoff under 50. History lives in git. |
| 16 | The arcade port's agent file and its work cards gave conflicting permissions; the conflict sat open. | arcade port handoff | One agent file, one owner of writes per lane, no per-card permission flags. |
| 17 | Per-component human review loops stalled the arcade port; the fix was review only at slice boundaries. | arcade port agent rules | The owner is asked at exactly three points (section 7). |
| 18 | An exhausted gate stays blocked. A new commit or deadline does not renew its attempt budget. | arcade port agent rules | Blocked cards record the exact unblock condition; work moves to the next dependency-safe card. |
| 19 | Check history before solving: a screen fit already existed on an old branch. | toolkit lesson 6 | Before designing anything, search this repo, SH2ZAM's `saturn/` docs and the decomp for an existing answer. |
| 20 | No general engine. The arcade port rejected a general CPS2 runtime and an ECS by ADR. | arcade port ADRs | No libultra layer, no F3DEX2 or display-list interpreter, no general float-to-fixed translator, no new engine. Title-specific code only. |
| 21 | re4dc is a public repo: every push is a publication. | re4dc AGENTS | So is this one. Hygiene scan before every push; nothing ROM-derived in git, including traces and stills from the retail course. |
| 22 | Master runs the game and hands one snapshot per frame to the slave, which owns drawing. A busy slave skips a picture, never the game. | toolkit saturn-graphics | This is the planned CPU split for 60 fps (section 4.5), behind a knob, with the simulation trace STRICT-identical either way. |
| 23 | The arcade port kept per-region memory asserts, copied cartridge data to VDP1 with the CPU, and banned SCU DMA with an A-bus endpoint and any mid-play data read until physical evidence existed. | arcade port agent rules | Same rules for the cartridge here (section 4.6). |
| 24 | re4dc split its tick into a gameplay budget, a render budget and a margin, and chose every option by measured milliseconds. | re4dc AGENTS | A field budget split (section 4.4), set from Gate 1 measurements. |

---

## 4. Architecture

### 4.1 The reference: the decomp, built natively in float

The arcade port needed MAME as its oracle. This project has something better: C source that compiles on a PC.

`ref/` is a small host program that builds the one-racer closure of the pinned decomp (from the submodule, with `PORT` and `EXPANSION_KIT` undefined) against thin stubs for libultra, audio and graphics. It takes an input script and a course, steps the original code, and writes a per-tick trace.

- Float discipline: binary32, `-ffp-contract=off`, no fast-math. Determinism check: GCC and Clang, `-O0` and `-O2`, same trace hash. If they differ, find the libm call and pin it.
- The reference is never linked into the Saturn or web builds. It is the judge, not a component.
- G-Diffuser itself (the full PC port) is the visual reference for still pairs and for optional human-played traces. That runs on the owner's machine.

### 4.2 Where each piece of original code goes

For every function in the closure, pick the cheapest tier that is still correct:

1. **OFFLINE.** Run it in the reference at import time and bake the result into `generated/` (local, ignored). Course initialisation belongs here: tensions, segment lengths, joins, forms, bases, chunk reference positions, wall edges. Machine stats for Blue Falcon belong here too. Exact, free at run time, and nothing to convert.
2. **TICK, fixed-point.** Per-tick code is rewritten on SH2ZAM in `src/sim/` and gated against the reference. This is where the work is.
3. **OUT.** Not needed for one racer on this track. Listed in the census with the reason.

The repo's existing contract stands: no heap and no float in the shipping core. At 60 fps there is no room for soft-float in any per-tick path, so it is not an option. Gate 1 still counts float operations per function per tick, because that count ranks the conversion work and sizes the fixed-point tick.

The cartridge changes what OFFLINE can hold. With 4 MB of read-mostly space, bake more and compute less: track vertices for every chunk and LOD, wall edges, a spatial index for the track query, and any table that removes a divide or square root from the tick.

### 4.3 Parity classes

| Class | Meaning | Used for |
| --- | --- | --- |
| **STRICT** | State hash identical every tick. | Fixed core on host GCC, host Clang, WebAssembly and SH-2. Reference repeat runs. Knob-off identity. Any renderer or platform change. |
| **BOUNDED** | Open loop: state is reseeded from the reference each tick; each output field of the fixed function is within its stated error. | Function-level gate for each converted slice. Error bounds come from the field's Q format and are written in the range contract, not tuned until green. |
| **DISCRETE** | Closed loop, free running: the same ordered events (segment crossings, wall hits, dash and pit entry, boost on and off, lap line, finish) within a stated tick skew, lap time within a stated delta. | Whole-route gate. Position drift is reported raw alongside; it is never hidden by the normalisation. |

"Functional parity" is what this POC claims. Frame-exact parity with the N64 is not claimed anywhere.

### 4.4 Tick contract

- The simulation advances in fixed 1/60 s ticks (G-Diffuser documents the original at a true 60 Hz; confirm in the census from the game loop). The existing VBlank accumulator with bounded catch-up stays.
- The target is one picture per tick. The two stay decoupled in code: a late renderer skips a picture and counts a missed field, never a tick.
- Tick order is the source's: latch input, update racer, resolve track and walls, update race state, then camera. Written down from the source in Gate 1, with line references.
- Field budget, proposed until Gate 1 replaces it with measured numbers: master (input, simulation, track query, camera, snapshot) at most 8 ms; slave (transform, clip, command build) at most 12 ms; VDP1 draw complete inside the field; the rest is margin. Report against these three lines separately.
- If the simulation tick cannot fit on the master, that is an owner decision at Gate 1 with numbers. Halving the tick rate changes the physics and is not something the agent may choose.

### 4.5 Render contract

- Track: the original chunk traversal order from the camera's segment, far to near, replaces the global depth sort for track geometry. Quads go to VDP1 as quads.
- CPU split: the master runs the game and publishes one immutable snapshot per field (camera, racer pose, visible chunk window). The slave transforms, clips and builds the VDP1 list from it. The snapshot boundary goes into the shared core API now, so host, web and single-CPU builds use the same path. The split is a knob; the simulation trace is STRICT-identical with it on or off.
- VDP1 fill is a budget of its own. Gate 1 finds how many polygons and how much screen coverage finish in one field; draw distance and LOD are set from that number, not from what looks nice.
- Per-frame geometry work is transform and clip only. Spline evaluation and chunk vertex construction are baked into the cartridge pack.
- T tier is flat or Gouraud polygons. Textured distorted sprites are a P-tier knob, default off, added only if the 60 fps fill budget has room.
- Near plane: clip polygons as today. Subdivide near quads only if still pairs show it is needed.
- Sky and backdrop: VDP2 back colour, then one scrolling NBG layer at P. Camera roll on the backdrop is out of scope.
- VDP1 command list: clear polygon at the head, static aligned buffer, wait for draw-end before reuse (already so).

### 4.6 Memory contract

One table, kept current, with link-map numbers and asserts: work RAM high (1 MB), work RAM low (1 MB), cartridge RAM (4 MB), VDP1 VRAM (512 KB), VDP2 VRAM (512 KB), SCSP RAM (unused in this POC). Fixed pools only. Headroom in a spec is not headroom in the link map.

Cartridge rules:
- The image detects the cartridge at boot and shows a plain "RAM cartridge required" screen without it. Emulator configs and the hardware sheet name the cartridge.
- Placement: code, stacks and per-tick mutable state in high work RAM. The cartridge holds large read-mostly baked data. Treat cartridge access as slower than high work RAM until measured, and note that an emulator may not model that cost at all.
- Loaded once, before the race, with the display blanked. Nothing is read from disc during play.
- Cartridge data reaches VDP1 by CPU copy. No SCU DMA with a cartridge endpoint until physical evidence says it is safe.

---

## 5. Evidence and gates per change

Tiers: **HOST** (fixed core or reference on PC), **EMULATOR** (the ISO on a Saturn emulator), **HARDWARE** (owner only; UNRUN until then), **MODELLED** (any estimate, and any frame-cost number from an emulator timer).

Every change (lean):
- strict host build with the repo's warning set, GCC and Clang;
- the trace gates the change can touch (all of them for `src/sim/`);
- a deterministic repeat;
- the card's negative control;
- for a knob: knob off gives the identical trace hash and, where claimed, the identical image hash;
- one short emulator run if `saturn/` or the render path changed.

Full tier, only on these triggers:
- memory layout or image size change: whole-lap emulator run, link-map table updated;
- simulation change: every trace, plus the fuzz trace;
- performance decision: same build, same route, one variable, mean, p95 and max;
- T or P candidate: everything once, on the one final image, built clean.

Trace corpus (inputs are scripts; outputs come from the reference):

| ID | Route |
| --- | --- |
| T01 | Idle on the grid through the countdown |
| T02 | Full throttle on the first straight |
| T03 | Steering sweeps left and right at speed |
| T04 | Wall grind, each side, shallow and steep |
| T05 | Boost chain with energy drain |
| T06 | Pit lane recharge |
| T07 | Dash plate entry |
| T08 | Brake, drift and slide inputs |
| T09 | Three clean laps to the finish |
| T10 | Seeded fuzz inputs, long |
| H01+ | Human-played traces from G-Diffuser (owner, optional) |

Each also exists on a **synthetic course with a fixture machine**, and those versions are committed and run in CI. Retail-course traces stay in the evidence folder outside git; only their hashes and counts are committed.

---

## 6. The gates

Card format: **goal**, **done when**, **control** (the mutation that must make the gate fail), **needs ROM**. Effort is the suggested session effort.

### Gate 0: ground rules in the repo (ROM-free, effort medium, one session)

| Card | Goal | Done when | Control |
| --- | --- | --- | --- |
| G0.1 | `AGENTS.md`, a three-line `CLAUDE.md` pointing at it, `docs/STATUS.md`, `docs/HANDOFF.md`, this plan as `docs/POC_PLAN.md` | Files exist; STATUS under 80 lines; README "next" section points at the plan | none |
| G0.2 | Hygiene scan as a script and a CI job: file types, sizes, binary content, private paths, ROM fingerprints | CI fails on a planted `.z64` stub and on a planted absolute home path | the two planted files |
| G0.3 | Evidence folder convention: `$FZX_EVIDENCE` outside git, one subfolder per lane; generation receipts for everything in `generated/` | A stale `generated/course.h` makes the Saturn build stop with a clear message | edit the header by hand |
| G0.4 | `FZX_SIM` selector (`proto` now; `fixed` later) shown on the HUD and in the web UI | Both builds display the active simulation | none |

### Gate 1: GO or NO-GO, with numbers (effort high; this gate decides everything after it)

| Card | Goal | Done when | Control | ROM |
| --- | --- | --- | --- | --- |
| G1.1 | **Closure census.** From `Racer_Update` for one human racer on a walled-road course: every reachable function, its lines, float operations, globals read and written, `NON_MATCHING` flag, and every `PORT` / `EXPANSION_KIT` / `Gdx` / `CVar` site. Assign OFFLINE, TICK or OUT. Confirm the game mode and the tick order from source. | `docs/census/closure.tsv` and a one-page summary; every fork hook in the closure is shown compiled out or inert | Build the reference with `PORT` defined and show the trace hash changes (proves the hooks matter and are off) | no |
| G1.2 | **Reference harness** `ref/` with trace format v1 (inputs, racer state subset, event bits, state hash) | Runs T01 to T10 on the synthetic course; four-way determinism check passes | Change one tuning constant; hash must change | no |
| G1.3 | **Tick cost model.** Count float operations per tick by kind from an instrumented reference run (mean, p95, max per function). Measure the cost of the matching SH2ZAM fixed-point operations with a microbenchmark image, in high work RAM and from the cartridge. | A ranked table: function, ops per tick, MODELLED fixed-point cycles per tick, share of the 8 ms master budget | Cross-check the model on one function by converting it and running it for real in the emulator | no |
| G1.4 | **First boot and 60 fps render probe.** Boot the existing ISO on a Saturn emulator with the cartridge enabled. Record a still. Measure the split per field (simulation, transform, command build, VDP1 completion) over a full demo lap. Then sweep polygon count and screen coverage to find what VDP1 finishes inside one field, and time transform and command build per polygon on one CPU. | First boot evidence in STATUS with image hash; a per-field polygon and fill budget; a cost table labelled EMULATOR / MODELLED | A deliberately oversized command list must trip the capacity assert | no |
| G1.5 | **Emulator lane.** Pick the emulator the agent can drive: headless in the container or CI with a high-level BIOS if possible; otherwise scripts the owner runs. One bounded attempt at headless. | A documented command that boots an ISO and returns a screenshot and a pass marker; or `BLOCKED_EMULATOR` with the exact unblock condition | none | no |
| G1.6 | **Importer on the real ROM** and course census: shapes, types and features present on Mute City 1 | Checksum, tensions, sample positions, bank sign and spawn pose agree with the reference's own course code (BOUNDED); a counts-only census is committed | Flip the bank sign; the comparison must fail | **yes** |
| G1.7 | **Fit table** per section 4.6 from the link map plus projected closure size, and a first layout of the cartridge pack (what is baked, at what sampling, how many bytes). Cartridge detect and the "required" screen. | Table in the gate report; the image refuses to start a race without the cartridge | Boot with the cartridge disabled | no |
| G1.8 | **Gate report** `docs/GATE1.md`: GO or NO-GO for T and for P at 60 fps, the measured field budget split, the per-field polygon budget, the ranked conversion list, the scope the census removed | Owner has read it and answered the Gate 1 question (section 7) | none | no |

Decision rules the report applies (the owner can change the thresholds once, at this gate):
- If the modelled master work fits its 8 ms line, the slave work fits its 12 ms line at a usable polygon budget, and VDP1 finishes that budget in a field, P at 60 fps is GO.
- If the simulation fits but rendering does not, P is GO with a reduced draw distance or LOD; show the stills at the budget that fits, so the owner sees what 60 fps costs in picture.
- If the simulation tick does not fit the master even after the top-ranked conversions and cartridge tables are modelled, P at 60 fps is NO-GO as specified. The report then gives the measured rate that is reachable and what would close the gap. The owner chooses; the agent does not lower the target.
- T does not depend on the frame rate and proceeds in every case.

### Gate 2: track truth (effort medium; runs beside Gate 3)

| Card | Goal | Done when | Control | ROM |
| --- | --- | --- | --- | --- |
| G2.1 | **Course pack v2.** The importer calls the reference to bake everything the census marked OFFLINE into `generated/` (and the browser's local JSON): course tables, chunk vertices per LOD, wall edges and the track-query index, laid out for the cartridge. | Pack round-trips; receipt written; size fits the cartridge layout from G1.7; synthetic pack committed as a fixture | Corrupt one baked length; the load must reject it | partly |
| G2.2 | **Track query** in fixed point: world position to segment, parameter, lateral offset, surface and wall distance (the role of `func_8009EBEC` and its callers) | BOUNDED on a dense grid of points and on every trace position | Swap left and right radius | no |
| G2.3 | **Renderer v2**: chunk-ordered painter from baked vertices, walled-road shape with walls, distance LOD, the per-field polygon budget from G1.4, reading only the per-field snapshot. Knob, default off until still pairs pass. | STRICT simulation trace unchanged; still pairs against G-Diffuser at five fixed poses reviewed; worst-case polygons under the cap | Reverse the traversal; ordering test must fail | stills need ROM |
| G2.4 | **Blue Falcon** low-LOD model import, local only | Triangle and vertex counts match the source table; visible in both builds | none | **yes** |

### Gate 3: racer truth (effort high; the long pole)

Order follows the sh2-game-logic campaign: kernel, then the hottest and most depended-on code first.

| Card | Slice | Notes |
| --- | --- | --- |
| G3.1 | **Range contract.** Run the whole corpus through the reference and record min, max and smallest meaningful step for every state field and every intermediate the closure names. Choose the Q format per field from that table. | Generated, not guessed. Q16.16 at 1/16 scale is the default; anything that does not fit is listed with its own format. |
| G3.2 | Math kernel: the game's 4096-step sine table, square root, vector and matrix helpers used by the closure, mapped to SH2ZAM | Reuse SH2ZAM where the contract matches; add nothing general. |
| G3.3 | Machine stats for Blue Falcon | OFFLINE: baked by the reference for the settings values the POC exposes. |
| G3.4 | `Racer_UpdateFromControls`, in sections: input shaping; thrust and boost; turning, slide and drift; gravity and ground attachment; energy | One card per section. This is about 1,750 source lines; expect several sessions. |
| G3.5 | Wall and surface response (`Racer_HitWall`, damage, dash, pit, dirt if the census found it) | Uses G2.2. |
| G3.6 | `Racer_UpdateMovement`, `Racer_Update` orchestration, lap and checkpoint progress for one racer | Closes the loop. |
| G3.7 | **Closed-loop gate** on T01 to T10, then flip the default to `FZX_SIM=fixed` | DISCRETE passes on every trace; drift table published. |

Every G3 card is done when: its BOUNDED function gate passes on the whole corpus; the closed-loop traces that reach it still pass or improve; STRICT holds across host GCC, Clang and WebAssembly; the control (perturb one constant in the slice) fails the gate; and the handoff lists the fields still fed from the reference.

Input mapping is the one handling choice with no source answer: the N64 stick is analogue and the Saturn pad is digital. The traces use the mapped inputs, so parity is unaffected. The mapping itself (full deflection, or a ramp) is a feel decision for the owner at the P review. Start with a short linear ramp behind a knob.

### Gate 4: Saturn integration, T exit (effort medium)

| Card | Goal | Done when |
| --- | --- | --- |
| G4.1 | `AUTOTEST` image: runs an embedded synthetic trace at boot and prints the final state hash and PASS or FAIL | The hash on the emulator screenshot equals the host hash: STRICT on SH-2 |
| G4.2 | Normal image with the fixed simulation, renderer v2, imported course and machine | Boots; HUD names course and simulation |
| G4.3 | Pad-only lap: a scripted pad schedule, then a read-only closed-loop bot | One full lap from pad input; run line recorded (image hash, flags, ticks, fatals) |
| G4.4 | Dual-CPU split: master publishes the snapshot, slave transforms, clips and builds the list. Knob. | Simulation trace STRICT-identical with the knob on and off; pictures identical at fixed poses |
| G4.4b | Cost run over the lap, every field: master, slave and VDP1 lines as mean, p95, max, plus missed-field count | Table in STATUS, labelled EMULATOR / MODELLED |
| G4.5 | Hardware sheet `docs/HARDWARE_TEST.md`: console, 4 MB cartridge, steps, what to look for (missed-field counter on the HUD, cartridge screen), what to send back | Sheet exists; status line says UNRUN |
| G4.6 | **T candidate**: clean build, full tier once on that image, hashes recorded | T exit condition in section 1 met and written up |

### Gate 5: playable loop, P exit (effort medium, high for pacing work)

| Card | Goal |
| --- | --- |
| G5.1 | Race state from source rules: countdown, lap count, timer, finish, retire and retry |
| G5.2 | HUD on VDP2 in fixed cells: speed, lap, time, energy. Replace the debug console. |
| G5.3 | Race camera: census `camera.c` for the default chase mode only; port that mode or keep the labelled prototype, by owner preference at the review |
| G5.4 | Close the gap to 60 fps on the three-lap route: zero missed fields is the target, and the count is reported whatever it is. Each optimisation: knob default off, knob-off identity, one cost arm, STRICT trace. Order by measured cost: cartridge tables for the tick, hot-loop work with SH2ZAM's prepared batches, draw distance and LOD, then command-build tricks. A missed-field counter stays on the HUD in test builds. |
| G5.5 | Backdrop layer; optional road texture knob, only if the 60 fps fill budget has room |
| G5.6 | Ten-minute soak, repeated resets, pad disconnect and reconnect |
| G5.7 | **P candidate** and the review package: the exact image, controls, a continuous clip, the list of adaptations, known issues, hardware status |

---

## 7. Owner touchpoints

Only these. Everything else the agent decides and records.

1. **Before Gate 1.6:** where the US rev0 ROM lives for ROM-lane cards (a private path exposed as `FZX_TEST_ROM`, on the owner's machine or a private environment). It never enters the repo, a CI log, or a chat.
2. **Gate 1 report:** "GO or NO-GO as recommended?" The frame target (60 fps) and the cartridge requirement are already decided. If the report shows 60 fps is out of reach as specified, it will also ask which trade the owner prefers: less picture, a longer optimisation campaign, or a lower rate.
3. **P review:** "What should stay as it is, what looks or feels wrong, and which issue should be fixed first?" Silence is not approval.

Also needing the owner: any still or clip of the retail course in a public place, hosting a build, and running the hardware sheet.

---

## 8. Operating rules for the executing agent

Written for an agent in Claude Code. Reasons are given so the rules can be applied with judgement.

**Start of every session.** Read `AGENTS.md`, `docs/STATUS.md`, `docs/HANDOFF.md` and the one gate section of this plan you are working in. That is the whole boot set. Read source files when a card needs them, not before. The context window is large; the reason to stay small is that old checkpoint prose reads like instructions and pulls work off course (lesson 15).

**Effort.** Medium is right for most cards. Use high for Gate 1, for every G3 card, and for any divergence hunt. Use low for inventories and log scans handed to subagents. Raise effort before adding "think harder" prose; lower it before adding "be brief" prose.

**One card at a time, to done.** Done means committed, pushed and gated, with the control shown failing. "Coded, unrun" is a valid status and must be written as such. A first patch or a green host test is not a stopping point if the card's condition is not met.

**When a gate fails.** Fix ordinary defects without asking. If the same named gate fails after a bounded effort (two distinct hypotheses tested), record the blocker and the exact unblock condition in STATUS and move to the next dependency-safe card. Do not loosen a tolerance to pass; a tolerance changes only with a written reason tied to a Q format.

**Divergence hunts.** Find the first divergent tick and the first divergent field, then the function that wrote it. Fix there. Do not tune downstream constants to pull a trace back.

**Subagents.** Use them for independent, bounded work: the closure census (split by file), float-operation counting, log scans, an independent review of a converted slice. Give a reviewer the code and the gate, not your conclusion; an agent handed a hypothesis tends to confirm it. Cheaper models are fine for inventories. Do not delegate a check you can run inline.

**Parallel lanes** are allowed after Gate 1 and only with disjoint files: `src/sim/` (Gate 3), `src/track/` and `src/render/` (Gate 2), `saturn/` and `tools/emu/` (Gates 1.4, 1.5, 4). One writer per lane; the coordinator lands and reruns the cheapest gate on the merged tree. One emulator instance per lane.

**Looking at output.** Read stills and screenshots yourself before describing them. Identity is judged by hashes, cost by tables, look and feel by the owner.

**Reports.** Lead with the playable extent in plain words. Then gates passed, failed and unrun, each with its tier. Then image and commit identities. Then the next card. Never substitute a pass count for the question "can someone drive a lap".

**Repository safety.** This repo is public. Never commit ROMs, extracted course, model or texture data, retail-course traces, or stills of retail content. Run the hygiene scan before every push. Stage named paths only; no `git add -A`, no force-push, no history rewrites. Generated files are regenerated, never hand-edited.

**Stop and ask only for:** a missing ROM path for a ROM-lane card when no ROM-free card remains; the three touchpoints in section 7; anything outward-facing; an unresolved material architecture choice that Gate 1's rules do not already decide.

---

## 9. Risks and what decides them

| Risk | Signal | Decided by |
| --- | --- | --- |
| Fixed-point physics drift makes the car feel wrong although gates pass | DISCRETE passes but lap time delta or drift table grows with route length | G3.1 range contract first; widen the format of the offending field; owner feel check at P |
| The simulation tick does not fit the master's share of a field | G1.3 table | Gate 1 rules; cartridge tables first, then owner |
| 60 fps holds on the emulator and fails on a console | Only a console can show it; emulator timing is MODELLED, and cartridge access cost may not be modelled at all | Hardware sheet with the missed-field counter; keep margin in every budget line; never report 60 fps above the tier that measured it |
| Slave hand-off bugs appear as rare flicker or torn lists | Soak run, picture identity at fixed poses with the knob on and off | One hand-off per field, immutable snapshot, no shared mutable state |
| The fork's hooks leak into the reference | G1.1 control shows no hash change when it should, or a hook reads a default that differs from stock | Cross-check the function against `inspectredc/fzerox`; record the outcome |
| No emulator can be driven from the agent's environment | G1.5 blocked | Owner-run scripts with returned evidence; the lane stays EMULATOR-blocked, HOST work continues |
| No ROM reaches the agent | G1.6 blocked | All ROM-free cards proceed on the synthetic course; T cannot be claimed until G1.6 and G2.4 run |
| Painter order breaks where the track overlaps itself | G2.3 still pairs | Chunk order within the visible window plus per-chunk priority; do not add a z-buffer emulation |
| VDP1 fill time, not CPU, is the limit at 60 fps | G1.4 sweep and G4.4b show draw-end past the field | Fewer, larger quads; shorter draw distance; lower resolution of the far band; all as measured knobs |
| Affine texture warping looks bad near the camera | G5.5 stills | Keep the road flat-shaded near the camera; textures are optional for P |
| Plan and state sprawl | STATUS or HANDOFF over their line limits | Move history to git; trim in the same commit |

---

## 10. Kickoff prompt

Paste into a fresh Claude Code session opened on `lamb2k/fzero-x-saturn`:

```
Read docs/POC_PLAN.md sections 1, 4, 5 and 8, then Gate 0 in section 6.
Goal: a one-track technical POC of F-Zero X on Saturn (tier T), then playable (tier P).
Start with Gate 0 and continue into Gate 1 without waiting for me, card by card.
A card is done only when it is committed, pushed, gated, and its control has been
shown to fail. Skip ROM-lane cards until FZX_TEST_ROM is set; do every ROM-free card first.
Stop when docs/GATE1.md is written and ask me the Gate 1 question from section 7.
Report playable extent first, then gates with their evidence tier, then what is unrun.
```

Set the session effort to high for Gate 1.
