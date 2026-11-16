# First-course persistent goal

Owner instruction, 2026-11-16: continue until the first level is playable from
boot through selection and race completion, with Saturn-optimized faithful source
content and a 60 fps target. Blender may be used when assets need adjustment.

Owner clarification, 2026-11-16: this is a Sega Saturn preservation project.
Approximate behavior or similar quality is acceptable when exact source behavior
is impractical. Use source content and preserve recognizable handling, presentation
and race rules; document meaningful differences. Exact parity is a useful baseline,
not a blocker to an otherwise faithful playable Saturn implementation.
Assets remain original or are converted offline for Saturn compatibility; rendering
and behavior approximations do not authorize newly designed replacement assets.

The first course is Mute City 1. The current Time Attack / Blue Falcon integration
is the starting slice. The entry and selection flow must use the original game's
presentation and lead into that playable race; booting directly into a diagnostic
race does not satisfy selection. Other courses and modes remain in the full-game
plan, but are not prerequisites for this first-course milestone.

## Completion gates

- A clean owner-ROM build boots on the Saturn target, enters selection from pad
  input, starts Mute City 1, completes all three laps, shows results and retries.
- Original simulation, camera, machine, road/materials, backdrop/scenery, HUD,
  effects and race audio are integrated. No placeholder content satisfies a gate.
- Required content has source provenance, reproducible conversion and receipts.
  Missing or stale content fails clearly. Source assets stay private.
- Saturn-native conversion and measured asset adjustments preserve the source's
  identity. Record each adaptation with source comparisons; use Blender where
  useful. Do not silently drop content or lower the 60 fps target to pass.
- The actual content-bearing race is measured over the full route: mean, p95,
  maximum frame cost and missed fields, with 60 fps / zero missed fields the target.
  Count all memory pools and peak stack use. Check simulation parity where retained
  and explicitly validate any adapted behavior against its intended race rules.
- Run a clean final candidate through boot/select/race/results/retry, the relevant
  negative controls, a ten-minute soak and local source-reference visual/audio
  review. Record image, code, asset and configuration identities together.
- Report HOST, EMULATOR, MODELLED and HARDWARE separately. Emulator evidence cannot
  establish physical 60 fps. Hardware and owner look acceptance remain explicit
  pending gates until actually exercised.

## Ordered work cards

| Card | Current state / next exit |
| --- | --- |
| Native backdrop and skyline | Original floor now uses RBG0 through a full race; 13-view floor/cloud mean 74.12 -> 56.62 ms MODELLED with A/B/A. Clouds, scene budget and final look remain open |
| Native road within budget | Native shared tiles finish the original race. Shared vertex cache preserves 9,907 HOST views and 13 native scene checks; road mean 125.92 -> 112.04 ms MODELLED, A/B/A confirmed. Budget remains blocked; pursue larger rendering and source-derived asset adjustments |
| Native Blue Falcon | Original model now completes the actual race with live pose/body/lighting and guarded resident packs; 0.67-59.93 ms held-view cost MODELLED remains over budget; final appearance acceptance open |
| Source entry and selection | Integrate the bounded original boot/menu/selection path into the slice; pad-driven transitions required |
| Race presentation and audio | Finish HUD/countdown/results/effects, source music and sound cues; integrate retry |
| Combined scene and performance | Measure actual scene, improve the largest cost, qualify memory/stack and deterministic gameplay on the combined image |
| First-course acceptance | Clean build, full end-to-end run, soak, local review package and hardware status |

Current priority follows the owner's `generated/CODEX-RENDER-PLAN.md` C0-C8:
C0 now establishes a 3.287 fps MODELLED whole-route baseline with 34,178 missed
fields. C1's full HOST road census passes at p99 335 commands with the forced
full-repetition control at 818. Next are road bake/stream, vehicle rebake,
cloud architecture, CPU split and combined measurements. Entry and race-presentation
work can interleave where the plan allows. The render plan does not replace or
narrow the completion gates above. A 60 Hz simulation with 30 Hz drawing must be
reported explicitly if that fallback becomes necessary; it is not 60 fps.

Work one card through its gates at a time. A blocked approach gets two distinct
hypotheses, a concrete unblock condition and a dependency-safe next card. Completed
source-only changes continue to origin/gate1 under the existing publication rule.
This goal stays active across cards; isolated probes are progress, not completion.
