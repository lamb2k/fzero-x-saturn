# Original machine and settings selection

`FZX_SOURCE_MACHINE_MENU=1` connects original Time Attack course selection to
machine selection, settings and the Mute City 1 / Blue Falcon race. It defaults
off and requires the original course menu, camera and machine paths. Native
selection and diagnostic race handoff are gated; the full [first-course goal](FIRST_COURSE_GOAL.md)
remains incomplete.

## Source behavior and assets

The pinned original machine-selection and settings update/draw code supplies
the six-machine fresh-save selection, repeat cadence, two-stage confirmation,
cancel/back flow, four color schemes and engine slider. All 30 original
miniatures are drawn, including the locked machines. Diagnostic unlock permits
inspection of all 30 settings screens; it is not the default save state.
Other course/machine race requests are held for inspection, with B returning
to settings. They are not silently redirected to the first-course slice.

Saturn A/C and Start confirm, B cancels, the directional pad navigates or
adjusts the engine, and L/R change the color scheme. Sound cues are captured
by source state but playback remains open. Choices survive returning to course
selection and reentering the machine screens. Save-file persistence is not implemented.

The UI converter retains 241 source images in 34 banks. Common UI occupies
277,776 bytes, with at most 96,560 additional bytes for one settings portrait
bank. The geometry converter retains all 30 original LOD1 models: 5,871
vertices and 2,963 triangles, with a maximum of 263 vertices / 132 triangles
per machine. Source shadow and reflection geometry, original colors and
textures, viewport placement, model rotation and draw priorities are retained.
Geometry, texture charts, effects and source camera matrices have independent
generation receipts and checked runtime identities. Retail-derived outputs
remain private.

Native presentation uses SH2ZAM model/view projection and bounded per-model
depth ordering. Source state and model data borrow guarded low work RAM;
select and settings reuse VRAM. The miniature bank is replaced by the selected
model bank on settings entry. Loading pauses menu time, drains drawing and
blanks both pages before replacing resident data.

The race handoff calls the original character/skin copy and engine conversion
routines before racer initialization. The earlier direct-race path assigned
the player slider but left the racer's engine field at zero. The enabled menu
path corrects that omission; its physics reference therefore uses the selected
engine and is distinct from the legacy disabled-path baseline. All four Blue
Falcon skins share source textures and use live body RGB in the existing race
material path.

The combined frontend/race initially overflowed high work RAM by 53,920 bytes.
In this native arm the complete 182,000-byte source graphics pool is placed
after the immutable road pack in the already-required 4 MiB cartridge.
The retained camera/countdown consumers use CPU accesses only; the pool is
not a DMA endpoint. Source structure offsets are unchanged. Native admission
checks capacity, alignment, zero-write readback and surrounding guards, then
checks those guards on every published race state. HOST keeps static storage
unless explicitly rebound for the relocation gate. Restart retains the binding.

The larger disc directory uses 256 file slots. Its CD workspace reserves 12 KiB
before the input log, instead of the disabled arm's 4 KiB; a compile-time size
check and generated-layout bounds prevent overlap. The enabled native build and
disc staging directory have a `-machine-menu` suffix, so toggling the option
cannot reuse its objects or leave the extra packs in the legacy disc directory.

## Saturn adaptations

- Original 320x240 layout is cropped by eight lines at the top and bottom.
- Models use original LOD1 geometry in both screens, offline texture charts,
  native RGB1555/CLUT4, Gouraud shading and bounded triangle ordering.
- The source cursor alpha pulse becomes invisible, half-transparent or opaque.
  Its source timing is retained. Equal adjacent background rows are coalesced.
- Source rotation is sampled offline over its repeating orbit. Integer
  projection permits a one-pixel edge displacement. Optional N64 C-button
  camera tilt is not mapped.
- Reflection coordinates use normalized camera axes before the source's
  signed-byte rounding; the measured difference is at most 0.070 texel.
- Reflections retain the source mesh and normal-based color lookup, rendered
  with mesh plus half transparency for the source's alpha-60 contribution.

These adaptations do not establish raster identity or owner look acceptance.

## Current evidence

HOST source state: 25,484 identical GCC/Clang UBSan snapshots; nine altered
behavior controls fail. UI conversion checks 1,721,288 original pixel samples,
and Windows/Linux output agrees. Camera export uses original initialization
with freshly decoded viewport assets. All 126 model-export receipt outputs
are current; corruption and bounded-allocation controls reject bad inputs.

The shared model projection covers 7,801,568 original vertex samples within
one pixel. Wrong-yaw and wrong-scale controls fail. Pure renderer and native
API-recorder checks cover 56,550 views with GCC/Clang UBSan: maximum 1,602
selection model commands and 250 settings model commands. The UI recorder
covers 7,440 cases; combined bounds are 1,646 / 285 commands, below the native
capacity of 2,019. Allocation shortages, all 16 model guards, wrong resident
identity, corrupt packs and rendering after a failed load are checked.

Race handoff HOST tests cover eight settings, 600 ticks per run, restart,
ten invalid inputs and six failing controls. They verify source tuning/body
color, GCC/Clang parity and preservation of the disabled legacy path.
The native workspace wrapper's HOST tests check exact capacity boundaries,
32-byte alignment, readback, all guard words and failure readiness. External
pool tests preserve state across 600 ticks and restart. Native-loop HOST tests
also retain machine, color and tuning across scratch reuse and course reentry;
both a discarded-history control and a reset-color control fail.

The disabled native program is byte-identical to the admitted course-menu
program, SHA256 `89b7df2158782300685a0cdddb87b97f1b7e1d28c8588fca33af9adbe3724c6b`.
Four enabled EMULATOR routes pass 383 machine/settings captures against
independent full source states, native commands and active drawing inputs.
They cover fresh-save navigation, rotation, all four colors, both engine limits,
cancel/back, pad disconnect/reconnect and settings entry for all 30 machines.
The maximum is 1,627 native commands against a capacity of 2,019.
Both returns to course selection also
match fresh source snapshots, restored assets, commands and guards. The sampled
menu stack minimum is 109,512 bytes; this is not a race-stack measurement.
Three corrupted-disc controls reject the UI, geometry and miniature texture
packs before entering a race, with the expected loader failure statuses.

The ordinary pad-driven handoff reaches source tick 1 with the selected skin 1
and engine 0.5078125. All 39 checked racer fields, globals, published pose and
camera match fresh GCC/Clang source references; skin, tuning and physics mutation
controls fail. A separate diagnostic restart then replays 7,059 qualified inputs
through three laps, finishing at source time 110,639 ms. Holds at 600, 2,660,
4,860 and 7,059 match the source and retain all recorded guards. The minimum
sampled race stack guard is 107,568 bytes. This injected replay is not ordinary
digital-pad race-completion acceptance.

The final program is SHA256
`fad15cb6310cb35e39e8ec12bd88b4d34fd6196f709ced19af887b417f6306d4`.
The 30-machine and navigation sweeps preceded the CD-workspace correction;
only seven literal bytes changed, and the input-log address is the only changed
linked symbol. Frontend code/data remain identical. The final program passes
the menu-to-race route and full selected replay; isolated disc staging reproduces
that program exactly. All 157 added packs match their generated inputs, and
the 20 prior non-program payloads remain unchanged.

Results presentation, audio, ordinary pad-only race completion, appearance
acceptance, performance and HARDWARE remain open. This card establishes no
30 or 60 fps result. The race still uses the existing experimental presentation.

## Local generation

With `FZX_TEST_ROM` supplied privately:

```sh
python3 tools/machine_menu_assets.py
python3 tools/machine_menu_source.py
python3 tools/machine_menu_models.py
python3 tools/machine_menu_camera.py
python3 -m unittest discover -s tests -p 'test_machine_menu*.py' -v
```

Evidence lives under `$FZX_EVIDENCE/host/machine-menu`,
`$FZX_EVIDENCE/host/machine-menu-native-wrapper` and
`$FZX_EVIDENCE/emu/machine-menu`. Generated assets, source closures, captures
and discs are not published.

## Engine scenes machine grid

On the original front-end branch, confirming an available cup/course opens the
six-column, five-row stock grid. The D-pad wraps on each axis. A, C or Start
opens the selected machine's settings. B returns to the grid at the same slot.
Y opens the guest picker when guests are enabled. The settings confirm writes
the engine session and starts LOAD. Browsing and cancelling leave the session
intact.

Rebuild the private grid assets from the qualified C8 all-unlocked capture:

```sh
python tools/engine_machine_grid.py "$FZX_EVIDENCE/emu/machine-menu/all30-2" generated/machine_grid
sh tools/check_receipt.sh generated/machine_grid/machine_grid.receipt
```

The baker reconstructs the stationary grid from thirty C8 selections at
320 x 224. It stores a shared background, RGB1555 palettes and transparent
4-bit patches for each selected machine. The engine owns navigation and draws
the resulting VDP1 sprites. The machine name, weight, selected pilot and
original grades come from the same C8 view as the selection highlight.
Stock settings load one checked CD bank for the selected machine. It holds the
original entry picture, the three other source paint schemes as RGB555 patches,
and the four balance arrows. L/R cycle paint, left/right adjust balance, X/Z
select its limits, B cancels and A commits. Selecting a scheme requires no CD
read. The race close-up uses the confirmed machine's original RGB body colour.
Each scheme currently shows one pose. The guest picker uses its portrait panel.

Bake with the qualified all30-2, stock-skins and route1 reference captures.
The two route1 endpoints uncover the track behind the arrow. The stock-skins
route presses and releases R through all four schemes for every machine.

```sh
python tools/engine_machine_settings.py "$FZX_EVIDENCE/emu/machine-menu" generated/machine_settings
sh tools/check_receipt.sh generated/machine_settings/machine_settings.receipt
```
