# Engine gate baseline: `origin/engine` `78e6a92` (2026-11-20)

`tools/engine_gate.py --full --controls`. Tiers: HOST (`fzx-phys`) and EMULATOR (Ymir); every timing is MODELLED,
and HARDWARE was not run. The images were gated from `claude/engine-gate`, whose default BINs equal `78e6a92`'s
(identity below). This table is the reference for every later merge.

The receipts are `gate-receipts/5df18c14eb40-full-baseline-78e6a92.json` and `f7514fbaa593-quick-baseline-78e6a92-ctl2.json`
(ctl-course and ctl-disc). The base BINs are in `gate-receipts/78e6a92efd32….bins.json`.

## Verdict

Every check passes **except gate 9, the margin**. Critical-path p99 must be at most 24.5 ms (33.3 / 1.36). These
runs exceed it:

| Run | Critical-path p99 |
| --- | --- |
| Mute City field | 24.58 ms |
| roster | 24.70 ms |
| Mute City from the scenes image | 24.65 ms |
| Sand Ocean | 26.58 ms |

This is known. P4 sizes the margin work after the hardware run (RETRO_PLAN P0). Every control fails as it should.

## Images (BIN sha256, first 16 hex digits)

| Image | Make variables | BIN | Identity vs 78e6a92 | Agrees with the lanes' reports |
| --- | --- | --- | --- | --- |
| e0 | (none) | 9d4db29d94606840 | equal | yes |
| field | `FZX_E1_DRIVE=1 E1_DRIVER=1` | e77f313ee5a690d7 | equal | yes (c8int) |
| field-snd0 | + `ENG_SND=0` | 631e0590661bb5d2 | equal | yes |
| field-pad (shipped) | `FZX_E1_DRIVE=1` | 4231817a5bf889f6 | equal | yes |
| drive | + `E1_FIELD=0` | da2ab4d917fe7459 | equal | not reported |
| roster | + `E2_FASTSPRITE=1 E2_ROSTER=1` | d156f3c2a90db614 | equal | not reported |
| scenes (shipped) | `FZX_E1_DRIVE=1 ENG_SCENES=1` | 4915044fd9b00897 | equal | not reported |
| scenes-drv | + `E1_DRIVER=1` | beb0fe20e7015aeb | equal | yes (c8int m6) |
| scenes-drv-snd0 | + `ENG_SND=0` | d224293464555939 | equal | yes |
| legacy recipe | FUNCTIONAL_RECIPE.md | 4b4cacb26d221259 | | yes |
| legacy OFF | | bb5f0fc5baef1670 | | yes |

## Races (whole race from tick 420; ms are mean / p99 / max)

| Run | Over two | Master | Slave | Critical path | HOST | Memory (master / slave unused) |
| --- | --- | --- | --- | --- | --- | --- |
| field, sound on | 0 of 4,519 | 19.60 / 24.16 / 25.29 | 17.81 / 22.25 / 24.63 | 21.06 / **24.58** / 26.97 | 36/36 + final, 104,040 ms | 185,764 / 5,760 B |
| field, sound off | 0 of 4,519 | 19.51 / 24.06 / 25.19 | 17.79 / 22.23 / 24.60 | 20.97 / 24.49 / 26.86 | 36/36 + final | 196,112 / 5,760 B |
| drive | 0 of 4,519 | 18.68 / 21.26 / 22.37 | 15.05 / 21.84 / 24.64 | 19.15 / 24.02 / 27.12 | 36/36 + final | 515,656 / 5,760 B |
| roster | 0 of 4,519 | 19.60 / 24.35 / 25.82 | 17.88 / 22.24 / 24.39 | 21.08 / **24.70** / 26.70 | 36/36 + final | 181,060 / 5,752 B |
| E0 flythrough | 0 of 3,528 | 12.96 / 15.90 / 16.88 | 14.86 / 21.60 / 24.96 | 15.23 / 21.67 / 24.93 | n/a | 695,692 / 5,596 B |

Every race has 0 guard trips. Two field runs are identical: per-field ticks and pixel hashes, RAM and VRAM, with
the SMPC OREG buffer masked because Ymir fills it from the host clock.

## Courses (shipped scenes image, driver mode, selected in SELECT)

| Course | Race (= HOST) | Checkpoints | Over two | Critical p99 | Slave p99 | Cold LOAD |
| --- | --- | --- | --- | --- | --- | --- |
| Mute City | 104,040 ms | 27/27 | 0 | **24.65** | 22.30 | 322 fields |
| Silence | 90,563 ms | 23/23 | 0 | 24.29 | 21.38 | 320 |
| Sand Ocean (tubes) | 92,553 ms | 24/24 | 0 | **26.58** | 24.57 | 325 |
| Devil's Forest | 108,038 ms | 28/28 | 0 | 23.97 | 21.35 | 316 |
| Big Blue (tubes) | 126,928 ms | 32/32 | 0 | 23.11 | 20.87 | 323 |
| Port Town | retires at HOST's tick 6,328 | 25/25 | 0 | 23.46 | 21.15 | 330 |

## Scenes loop, reset, cancel (sound on and sound off)

- **Loop:**
  - Cold race and retry are both 104,040 ms, with 27 equal checkpoints and equal tick-2000 stills.
  - Both races have 0 intervals over two.
  - The saved name survives a reboot.
  - Two runs are identical.
- **Soft reset:** fires from all seven places (title, select, records, a fade, load, race, results), and the fire
  count rises 1 to 7.
- **Cancel:** loads cancelled at 30, 100 and 180 fields return to TITLE with load state 3, and the next cold load
  races.
- **Menus:** 0 frames over two; the worst gap is 2 fields.
- **Disc:** every sound-on run used the shipping mixed-mode disc (data track and 21 CD audio tracks, 22 tracks in
  all, about 55 minutes).

## Controls (each must fail)

| Control | Shown failing |
| --- | --- |
| identity | E0_FOG_EARLY=0 field BIN 4b5ed340 is not e77f313e |
| legacy | OFF bb5f0fc5 is not recipe 4b4cacb2 |
| cadence | chunk cap off: 14 intervals over two, worst 3 fields |
| margin | balance off: critical p99 26.00 ms |
| host | HOST at heading gain 2.6: 2 of 36 checkpoints equal |
| determinism | the field race against the balance-off race: different |
| memory (stack) | a word painted 4 KB into the master stack: 4,096 B unused |
| memory (guard) | slave guard word cleared: the image stops with status 27 |
| loop | the retry race forced to retire: 104,040 against 27,866 ms |
| reset | chord without B: never returns to TITLE |
| cancel | chord without B: the load is not cancelled |
| menus | `ENG_SCN_STALL_CONTROL=1`: 1 menu frame over two (gap 5 fields) |
| course | Silence judged against Mute City's HOST: 0 of 23 checkpoints equal |
| disc | data-only CUE: flagged non-shipping; its audio hash and peak differ from the mixed-disc run (38b58987 / 15,511 against 8e5658aa / 16,300), and so does the sound driver's report word 0 (4 against 1) |

## Agreement with numbers reported earlier

- **"Critical-path p99 about 24.9 ms on Mute City's field" (RETRO_PLAN).** That is 24.87 ms from the balance lane's
  build at `c07f6cf`: BIN b68756c8, pin 5a94dbe, pixel hash 994dd847.
  - The jack merge then moved the field image to e77f313e: line-field-3's first-lap chasers and pin accd505, with
    pixel hash 9750aeb6, as COURSES.md section 9 says.
  - On that image the critical-path p99 is 24.58 ms.
  - The balance lane's own `bal_table.py` and `pace4.py` give 24.58 ms on the same run, so the scorers agree; the
    code changed.
- **Field image:** 0 over two holds.
- **Scenes loop:** cold and retry are equal, as reported.
- **Courses:**
  - Sand Ocean, Devil's Forest and Port Town match the jack table exactly.
  - Big Blue is 23.11 against 23.12 ms.
  - Mute City is 24.65 against 24.73 ms, and Silence 24.29 against 24.42 ms. Those runs predate the C8 screens and
    sound CD changes merged at e3559f5.

## Findings made while building the gate

- **Shared stages.** The claude-jack, claude-cup and one more stage share hard-linked `generated/` files. The select
  bundle was rewritten there by an uncommitted line-field-4 `course_bundle.py`, and a clean `78e6a92` build in those
  stages refuses it. The gate stage copies `generated/` and regenerates the bundle with the committed tool.
- **Legacy OFF (`bb5f0fc5`) did not build at `origin/engine`.** The tubes lane's `road_tiles.py` change made the
  road_tiles receipt stale. Its outputs regenerate byte-identical, after which both legacy BINs reproduce. The gate
  regenerates it.
- **Sound-on runs used data-only discs (C-093).** The scenes, cup, jack and balance lanes ran their sound-on images
  from single-track CUEs, so CD audio never played in those runs. The gate now uses the mixed-mode disc.
