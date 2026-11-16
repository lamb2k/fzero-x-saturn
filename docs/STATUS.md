# Status
Limit: 80 lines. History lives in git. Plan: [POC_PLAN.md](POC_PLAN.md).
Active goal: [first-course boot, selection and race completion](FIRST_COURSE_GOAL.md).

## Owner direction

Preserve F-Zero X on Saturn using original assets, prebaked into compatible formats.
Similar-quality rendering and behavior adaptations are authorized where exactness
is impractical. Preserve identity and playability; record meaningful differences.
Source assets stay private. Placeholders and missing content do not satisfy completion.

## Playable extent

The original Time Attack race code runs Mute City 1 on the master SH-2 with binary32
soft float. Full input replays finish three laps in 121,814 ms, matching HOST.
The image has original native road content locally; the craft and debug overlay
remain temporary, with incomplete presentation.
Source camera, partial original HUD and native sky are available behind explicit flags.
Boot/menu/selection/results/audio and the remaining source scene are not complete.
The old placeholder baseline missed 38 fields in 9,455 (MODELLED); it does not establish
performance for the content-bearing port. HARDWARE remains UNRUN.

## Gates and evidence

| Area | Current evidence / remaining work |
| --- | --- |
| Gate 0 | Repository hygiene, receipt rejection, evidence boundaries and visible simulation selector established |
| Gate 1 | Closure census, deterministic HOST reference and SH-2 emulator lane established; complete scene timing and capacity remain open |
| Simulation | Original race closure and 9,907-input source-camera replay finish at 121,814 ms; fixed-point conversion is not required to move presentation forward |
| Source models | Five Blue Falcon LODs and 22 textures exported; 72 pose/light checks pass. Native palette appearance and cost remain open |
| Source HUD | Timer, speed, lap, energy and lap flashes integrated under FZX_SOURCE_HUD=1; countdown/results/effects remain open |
| Source camera | 19 held SH-2 packets match; view/look-back replay finishes correctly |
| Source road | Native shared tiles retain all 300 chunks / 4,966 triangles and finish the full replay; 19 held views cost 72.99-244.16 ms MODELLED, so performance remains blocked |
| Source scene | Backdrop/skyline state pass 256 SH-2 probes. Native sky/skyline and original floor/cloud draw; floor/cloud full replay passes but its 28.90-103.62 ms MODELLED cost is blocked |
| Gates 2-5 | Native scene, entry/results/audio, pad-only acceptance, full-route cost/memory and final soak remain incomplete |
| Gate 6 | Full-game content/modes/opponents/progression/persistence remain planned |

## Current card

Source road integration and two reuse arms are gated. The default-off
`FZX_SOURCE_ROAD_TILES` path retains all 300 chunks / 4,966 source triangles,
pairs them into native quads and loads 108,384 texture bytes plus 43,088 metadata
bytes from guarded CD packs. The source road replaces the temporary ribbon.

HOST GCC/Clang/repeat, rejection controls and all 9,907 UBSan views pass, including
both reuse arms; at most 599/768 commands. Clipped-fan fog now follows actual clipped
depths and fan indices. Independent broken-depth/fan/reuse controls fail.

EMULATOR: corrected control and single reuse arms each pass 13 held views, with
unchanged scene packets and HOST geometry hashes. Projection-based culling changes
mean road draw cost from 153.89 to 129.56 ms; endpoint reuse alone to 150.23 ms,
MODELLED. These are 13 held views, not full-route performance acceptance.
The combined reuse image finishes all 9,907 inputs at 121,814 ms with the same pose,
19 matching hashes and unchanged scene packets. Low-RAM guards pass; stack retains
26,700 bytes. Held-view mean 123.25 ms, maximum 244.16 ms MODELLED. Earlier control
timeout at 9,883 remains incomplete. The control repeat matches; corrupt metadata
is rejected before race initialization. Road cost remains blocked after both arms.

## Local experiments retained

Hitch diagnostics, clip-divider and software-road WIP remain local/unpromoted.
Use fixed-input replay for comparisons; live autopilot depends on render cadence.
No release, ROM-derived publication, SD write or physical acceptance occurred.

## Next

Integrate the original Blue Falcon with bounded resident source texture charts,
actual race pose/camera and live lighting/body state. Then entry/selection and
race presentation/audio. Road budget remains blocked after two reuse experiments.
Return to plane cost with projected-vertex reuse or Saturn rotation backgrounds;
measure the complete content-bearing route before claiming 60 fps.
