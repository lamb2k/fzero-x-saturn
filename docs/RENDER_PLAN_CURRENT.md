# Render programme: current decisions

Updated 2026-11-17. The owner's private `generated/CODEX-RENDER-PLAN.md`
sets the C0-C8 dependency order. This page records its current execution state;
it does not narrow [the first-course milestone](FIRST_COURSE_GOAL.md).
Targets below remain targets until their stated evidence exists.

| Card | Established evidence | Next decision or work |
| --- | --- | --- |
| C0 | Frozen legacy-tuning full route: 3.287 fps MODELLED, 34,178 missed fields; frame clock controls pass | Retain this baseline. Measure the actual selected-tuning combined candidate separately; do not interchange replays |
| C1 | All 9,907 HOST views: p99 335 road commands; forced-first-tier control fails 420 | Census policy is qualified; native cost is not established |
| C2 | Source road bake: 151,128 B metadata, 147,328 B textures; receipts and corruption controls pass | Preserve source rows, fog inputs and frozen road reference |
| C3 | [Native stream](ROAD_STREAM_NATIVE.md): selected 7,059 inputs finish exactly; guards/stack pass; road p99 133.143 ms MODELLED | No-go above 16 ms: stop this implementation. Selected ideal-image gate fails; disabled executable identity passes |
| C4 | Rebake mean 9.273 ms MODELLED; HOST p99 67 / max 112 commands | The 1 ms / 70-command and overlap gates fail. Reassess measured costs after C3; this is not a qualified final vehicle path |
| C5 | Compact cloud atlas reclaims 128 KiB; dual rotation layers remain unproven | Dual-layer evidence remains open; [fixed grid](CLOUD_GRID.md) fits both routes but loses cloud pattern through clipped UV remapping |
| C6 | Not qualified; depends on C3 | Split only a qualified compact road job, preserve simulation, qualify chained submission |
| C7 | Combined 60 fps acceptance is absent | Full route with actual art, CPU budgets, VDP1 end-of-draw, missed fields and stack guards |
| C8 | Original selection reaches race; diagnostic replay reaches initial finish/results; post-log-cap clock, three state/record resets and four corrupted-pack controls pass | Records/name entry/retry flow, audio/effects, ordinary pad completion, soak and review |

## Changes to the original assumptions

The owner explicitly changed C3's texture gate from one source texel to one
screen pixel at p99. Source and drawn-tier texel error are still reported.
The geometry limit, 420-command p99 budget and shifted synthetic fixture stay
unchanged. The adopted `view-merge-1` pixel tolerance is Q8 256. The independent
held-view metric is a HOST mapping test, not a VDP1 appearance acceptance.
See [road stream](ROAD_STREAM.md) for tails and exclusions.

SH2ZAM requests go directly to the existing Opus integration session. The port
does not edit the library or duplicate general kernels. Tagged contracts are
independently qualified at card boundaries. The supplied `view-round-1` contract
adds final-coordinate truncation. [Dependency adoption](SH2ZAM_ROUND_ADOPTION.md)
passes HOST suites and old-API output checks. The subsequent source-order guard
and Q16-centre contracts resolve the anchor and fractional-centre blockers;
[centre adoption](SH2ZAM_CENTER_ADOPTION.md) qualifies all 9,907 HOST views and
the unchanged fixtures. No native road admission follows.
A small camera bias remains a diagnostic, not an accepted fix.

The selected full-route native experiment now measures road CPU p99 133.143 ms
MODELLED, above the explicit 16 ms stop threshold. Independent raw-clock and
scope audits confirm the failure. This implementation uses low-RAM working
arrays, unlike the proposed high-RAM projection scratch; stop this measured
route without generalizing its result to every possible architecture. Its
3.929 fps whole-scene result includes the original machine path, and establishes
neither 60 Hz simulation nor the 30 Hz drawing fallback. See the native report
for exact configuration, image and memory gates, and disabled-control checks.

The owner's [SCU DSP development notes](https://segaxtreme.net/threads/saturn-scu-dsp-notes.25289/)
are a follow-up reference for a bounded visibility or lighting experiment after
the SH-2 paths are measured. The author's reported gain is from another game;
it is not a port measurement. Any candidate must include transfer, synchronization
and contention costs in a whole-frame comparison. This does not change C0-C8
ordering or authorize duplicating SH2ZAM kernels in the port.

C8 may advance while a rendering dependency is pending. Missing results,
audio/effects or retry do not count as an approved Saturn adaptation. Original
assets remain the starting material; source-compatible offline conversions and
documented similar-quality adaptations are authorized.

The whole-scene target remains 60 fps. If only 60 Hz simulation with 30 Hz
drawing can be demonstrated, report precisely that. Current measurements
establish neither. HARDWARE remains UNRUN until physical evidence exists.
