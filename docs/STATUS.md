# Status
Limit: 80 lines. History lives in git. Plan: [POC_PLAN.md](POC_PLAN.md).
## Required target (owner correction, 2026-11-15)

The real game on Saturn, preserving original models, materials/textures, scenery,
effects, HUD/fonts, menus and audio. ROM-derived content stays local. Placeholder
art is only a test fixture; the current race is not the completed port. See plan
section 4.7 and Gate 6 for source-art acceptance and full-game coverage.

## Playable extent

Locally, with the owner's ROM: a Time Attack race on Mute City 1, original race code
(`FZX_SIM=orig`, soft float) on the master SH-2, temporary road/craft, debug race HUD,
Start retries. A whole 3-lap race on the emulator matches the host reference to the ms
(EMULATOR). 60 fps with rare hitches (38 missed fields in 9,455), course set-up about
18.5 s (MODELLED). Never run on a console. CI images use the synthetic fixture and the
prototype driver (`FZX_SIM=proto`) by default.

## Gates

| Gate | State |
| --- | --- |
| 0 Ground rules | Done in CI (see card table) |
| 1 GO / NO-GO | In progress. G1.1, G1.2, G1.5 done; G1.3 and G1.4 mostly; next cards in [HANDOFF.md](HANDOFF.md) |
| 2 to 5 | Source-art preservation now required; asset census/decoder probes next. Partial original-race integration exists, but source art/audio, pad-only acceptance and final gates remain open |
| 6 Full game | Planned, not implemented: remaining content/modes, rivals, menus/progression/persistence and console acceptance |

### Gate 0 cards

| Card | State | Evidence |
| --- | --- | --- |
| G0.1 agent file, STATUS, HANDOFF, plan, README pointer | Done | Files in tree |
| G0.2 hygiene scan and CI job | Done | `tools/hygiene_scan.py`; CI job `hygiene`. Controls in `tests/test_hygiene.py`: a planted `.z64` stub, ROM headers in three byte orders and four home-path forms each make the scan fail. HOST |
| G0.3 evidence folder, generation receipts | Done | `$FZX_EVIDENCE` convention in AGENTS.md. Importer writes `course.receipt`; `tools/check_receipt.sh` checks it; `saturn/Makefile` refuses a stale header. Control: a hand-edited header fails in `tests/test_import_course.py` (HOST) and stops `make -C saturn FZX_IMPORTED=1` in CI (Yaul container) |
| G0.4 `FZX_SIM` selector shown | Done | `fzx_sim_id()` in the core; SIM line on the Saturn HUD (built, unbooted, so not yet seen); web shows it, checked by `web/test-browser.py`. Only `proto` exists; any other value is a build error |

### Gate 1 cards

| Card | State | Evidence |
| --- | --- | --- |
| G1.1 closure census | Done | [census/SUMMARY.md](census/SUMMARY.md): 40 TICK functions (4,450 lines), 95 OFFLINE, 8 OUT. Mode `GAMEMODE_TIME_ATTACK`, tick order from `Race_Update`. Control: `--define PORT` finds 91 fork-hook calls in the closure; the stock build finds 0. HOST |
| G1.2 reference harness | Done | [ref/README.md](../ref/README.md): the original stock race code built natively in binary32 SSE float, driven through the original `Controller_UpdateInputs` and `Racer_Update`. T01 to T10 on the synthetic oval give the same trace hash with gcc and clang at -O0 and -O2, and match `ref/expected/fixture.hashes`. Control: one bit of one machine constant moves the T02 hash. Found on the way: clang needs `-fno-unsafe-math-optimizations` beyond `-fno-fast-math` (1 ulp start-position drift). CI job `reference`. HOST |
| G1.3 tick cost model | In progress | [cost/SUMMARY.md](cost/SUMMARY.md): `ref/cost.sh` counts every binary32 op per tick by kind and function (callgrind, gcc -O0 attribution, -O2 cross-check). Fixture: 539 float ops / 3,570 x86 instrs per live tick; MODELLED with SH2ZAM per-call costs, mean 9.6%, p95 13.5%, max 15.6% of the 8 ms master line (worst tick incl. no-energy ticks 63k float cycles, 28%). `Racer_UpdateFromControls` is 59% of it. HOST counts, MODELLED cycles. Not done: HWRAM vs cartridge microbenchmark and the convert-one-function cross-check, both need the G1.5 emulator lane. Fixture machine has energy on only 775 of 9,987 race ticks; rerun on Mute City 1 at G1.6 |
| G1.4 first boot and 60 fps render probe | In progress | First boot: CI image `05534805bfc7aa87` (run 43742967214) boots with the 4 MB DRAM cart; HUD shows `SIM PROTO DRIVER, NOT THE PORT` (G0.4 now seen); main loop alive (`_video_fields` advances about 60 per second). Still image kept in `$FZX_EVIDENCE/emu`. Not done: per-field split, VDP1 polygon/fill sweep, the oversized-list assert control. EMULATOR |
| G1.5 emulator lane | Done | `tools/emu/yaba_boot.ps1`: Yaba Sanshiro 1.20.37 with the MCP debug server (`FZX_YABA_EXE`, port 9640) boots an ISO, reads a symbol's counter twice over MCP, saves a screenshot and prints `FZX-EMU PASS|FAIL`. Runs on the owner's Windows machine, not in CI (needs a GPU window). Vulkan video core fails to start on this host, so the script uses OpenGL. Gives memory reads, breakpoints and screenshots; its timings are MODELLED, not cycle-exact. EMULATOR |

## Evidence tiers held

- HOST: core tests (GCC, Clang, sanitizers), importer and hygiene tests, reference harness determinism (ref/check.sh), per-tick op counts (ref/cost.sh).
- MODELLED: G1.3 fixed-point cycles per tick (SH2ZAM model costs; no cache or bus).
- EMULATOR: boot (G1.4, G1.5); SH-2 probes T01 to T10 STRICT; a whole Mute City race equal
  to the host reference; frame timing from on-screen counters (overload control trips).
  HARDWARE: UNRUN.

## Owner inputs outstanding

- Soft-float route vs POC_PLAN 4.2 (the CI default stays `proto` until decided).
- A console run on the owner's flash cart (docs/HARDWARE_TEST.md).

## Current local experiments (not promoted)

Hitch/clip-divider WIP remains local and unpromoted: HOST image identity and
65,776 SH-2 ratio cases pass; timing-sensitive autopilot runs disagree. Use the new
fixed-input replay for render comparisons. Emulator timing remains MODELLED.

## Blockers
ROM found and verified; FZX_TEST_ROM saved locally. Native models/materials, source
road/scenery/camera, full HUD/UI/audio and full-game coverage remain incomplete.
## Source-art extraction and camera probes
Five Blue Falcon LODs and 22 textures exported; isolated HOST suite: 58 pass/1 skip.
FZX_SOURCE_HUD=1 draws timer/speed/lap/energy art and source-timed lap flashes.
Stale assets fail the build. EMULATOR: 9,907 fixed inputs finish in 121,814 ms,
equal to host on isolated 86ea5d3. Source-reference visuals and HARDWARE unrun.
Blue Falcon: 72 pose/light checks pass; palette appearance and CPU cost remain open.
Source-camera view integration: 19 held SH-2 packets match; view/look-back replay
finishes in 121,814 ms. Road pack: 300 chunks/4,966 triangles; 146,120 cartridge bytes
match in EMULATOR. Native fog/clock and 19 draw-plan packets match HOST; material and masked-chart HOST gates pass.
[Ledger/memory](SOURCE_ART.md). Software road draw: 132–165 fields/view (MODELLED).
Full-detail patch baking exceeds VRAM; shared tiles remain open. Backdrop/skyline pass 256 SH-2 probes; native scene drawing next.
