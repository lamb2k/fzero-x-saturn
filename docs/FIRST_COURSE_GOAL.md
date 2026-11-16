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
| Native backdrop and skyline | Original sky/skyline/floor/cloud draw through a full race; plane cost is blocked at 28.90-103.62 ms MODELLED and final scene review remains open |
| Native road within budget | Native shared tiles finish the original race; two reuse experiments still cost 72.99-244.16 ms in 19 held views, MODELLED. Budget remains blocked; pursue cheaper native projection/submission or reviewed source-derived asset adjustments |
| Native Blue Falcon | Five LODs and pose/light state extracted; fix palette appearance and measure the moving native model |
| Source entry and selection | Integrate the bounded original boot/menu/selection path into the slice; pad-driven transitions required |
| Race presentation and audio | Finish HUD/countdown/results/effects, source music and sound cues; integrate retry |
| Combined scene and performance | Measure actual scene, improve the largest cost, qualify memory/stack and deterministic gameplay on the combined image |
| First-course acceptance | Clean build, full end-to-end run, soak, local review package and hardware status |

Work one card through its gates at a time. A blocked approach gets two distinct
hypotheses, a concrete unblock condition and a dependency-safe next card. Completed
source-only changes continue to origin/gate1 under the existing publication rule.
This goal stays active across cards; isolated probes are progress, not completion.
