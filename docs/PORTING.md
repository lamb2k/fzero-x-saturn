# Native port milestones

## Target

The real F-Zero X on Saturn, preserving original gameplay and source art.
The owner reaffirmed this on 2026-11-15. The authoritative work plan is
[POC_PLAN.md](POC_PLAN.md); current evidence and blockers are in [STATUS.md](STATUS.md).
The target remains 60 fps with the 4 MB RAM cartridge.

Original machine models/materials, course surfaces/textures, scenery, effects,
HUD/fonts, menus and audio are required content. Synthetic art is useful for CI
and diagnosis but is not a replacement for the game. A faithful one-track slice
is the first milestone; completion also requires the remaining source content,
modes, opponents, progression and persistence, followed by console acceptance.

## Source and current implementation

G-Diffuser is pinned at `4f9320675758fd524bb3bc9ea98b913a09d48552`; its decompilation
is `Zorkats/fzerox` at `b944a4135409d24979f5c74b001befab8345761a`.
The public default uses a synthetic circuit and prototype driver. The local
`FZX_SIM=orig` route compiles the original race closure for the master SH-2 with
binary32 software float. With the owner's ROM it has completed a three-lap
Mute City 1 race and matched a host replay's finish time. Retry and the timing HUD
exist; source-art presentation and original audio remain unimplemented.

The temporary renderer samples a flat ribbon, follows the source racer pose,
clips/sorts polygons and submits native VDP1 commands. This establishes an
integration path; it does not establish visual fidelity or the final scene budget.
Emulator timings are MODELLED. Physical-console acceptance remains UNRUN.

## First source-art slice

1. Inventory source asset symbols, draw routines, formats and dependencies for
   Mute City 1 and Blue Falcon, including the source camera, UI and sound cues.
2. Extract locally from the verified ROM. Preserve source vertices/indices,
   authored LODs, materials, UVs, palettes and placement. Decode display lists
   offline into native packs; do not introduce a runtime N64 interpreter.
3. Validate a small authored Blue Falcon LOD first, then the near model and LOD
   transitions. A permanently low-detail craft is not the acceptance target.
4. Replace the temporary road with source surfaces, materials and features;
   integrate scenery/backdrop/effects and the original HUD/fonts and race audio.
5. Compare fixed poses and a moving full-race capture against the selected source
   reference, then measure CPU, VDP1 fill, memory and audio I/O on that real scene.
6. Run behavioral parity, retry/soak, negative controls and the owner console sheet.

Preserve appearance through lossless layout/native-format conversion first.
Any mesh reduction, downsampling, omitted object or changed audio needs explicit
adaptation evidence and owner review. Performance work does not waive fidelity.

## Reproducible local content

ROM-derived outputs live only in ignored local storage. Each pack carries a
receipt and source-to-output manifest with ROM identity, symbols/offsets,
converter version, counts, material bindings and hashes. A port build rejects
missing/stale required content instead of substituting a fixture. The repository
holds conversion/runtime code and synthetic tests, not extracted game assets.

## After the slice

Gate 6 tracks every original course, machine, mode, opponent/CPU-line path,
menu, progression/persistence feature, UI asset and sound. Keep a coverage ledger:
implemented, tested, blocked, or explicitly accepted platform adaptation.
Do not call the port complete while those rows or hardware acceptance remain open.
