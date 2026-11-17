# Original course selection

`FZX_SOURCE_COURSE_MENU=1` connects the original main menu's Time Attack
request to the stock course-selection screen. It requires the original title
and main-menu flags and defaults off. Cup/course navigation, names, original
rotating preview meshes, ghost choices, confirmation and back navigation are
present. The machine-selection request remains held for inspection; B cancels
that hold. Other modes are not redirected to Time Attack or Mute City.

This is one C8 frontend step. Machine/settings selection, race entry through
this frontend, results, audio, retry and soak remain open. The full
[first-course goal](FIRST_COURSE_GOAL.md) is unchanged.

## Source and conversion

`tools/course_menu_source.py` extracts the pinned US cartridge update/draw
closure and bounded object helpers. Source state owns repeat cadence,
confirm/cancel transitions, cup unlock bounds, course limits, ghost choices,
camera targets and object ordering. Snapshots do not advance state. Native
port-1 input maps Saturn A/C to confirm, B to back, Start to confirm and the
directional pad to source navigation. Sound cues are queued, not played.
The current entry uses fresh, locked save settings; persistence is absent.

`tools/course_menu_assets.py` converts original wallpaper, cup signs, arrows,
labels and font 3/4 glyphs into 199 images occupying 417,216 bytes. RGB1555
retains source RGB5 channels. Grayscale art uses lossless CLUT4 indices and
per-draw tint palettes. Font-width overrides crop at the original row stride.
The UI layout is cropped by eight lines at the top and bottom for 320x224.

`tools/course_menu_preview.py` runs a private host reference to generate all
24 stock preview tubes and 360 source camera views. Original course loading,
segment lengths and preview geometry routines supply 8,367 vertices and
8,295 triangle pairs. The 142,932-byte, big-endian `FZXCPV01` pack retains
every original s16 position, closed ring, triangle index and source order.
It contains at most 525 vertices / 522 pairs per course. Its source camera
matrices are rounded to Q16.16 offline. GCC and Clang captures are identical.

Native admission checks both expected-file and embedded-payload hashes,
section extents, consecutive descriptors, closed rings, exact topology and
all camera records. The runtime uses the adopted SH2ZAM view/guard contracts
for batched projection and clipping. Each source pair remains two double-sided
triangles with its original diagonal; concave projections are not merged.
The meshes are inserted at the source preview object's priority, between cup
art and subsequent UI. No general graphics interpreter or new math kernel is
introduced. Meshes and scratch borrow guarded low work RAM during the menu.

## Documented Saturn adaptations

- The wallpaper uses its opaque RGB75 steady-state appearance. Source
  alpha-180 persistence against the preceding framebuffer is omitted.
- Cup pulses choose among offline tint levels 100, 180 and 255. CLUT labels
  retain RGB5 tint multiplication and native half transparency where selected.
- All six previews in a cup are resident on entry, replacing the original
  staggered loading. Artwork and geometry are not substituted or reduced.
- The camera retains its source orbit at pitch 20. Optional N64 C-button tilt
  is not mapped. Integer projection can move an edge by one pixel.
- Preview triangles use native flat RGB1555 polygon rasterization. This is
  not a claim of N64 antialiasing or raster identity.

## Evidence

HOST: 234,624 independently decoded UI pixel samples pass. Source state has
13,167 identical GCC/Clang UBSan snapshots; ten deliberate behavior changes
fail. The preview export's format/corruption controls pass. Native pack and
projection tests run with both compilers and UBSan, rejecting malformed
extents, faces, cameras and bounds. Five modified generated outputs are
rejected by their receipts.

The actual shared projection path covers 8,640 course/camera combinations and
3,012,120 original vertices. Maximum source-to-native edge displacement is
one pixel; there are no near/far crossings. Wrong-yaw and doubled-scale controls
fail by 94 and 54 pixels respectively. This is HOST geometry evidence.

EMULATOR: Ymir with software rendering, SH-2 cache and 4 MiB cartridge captures
41 held pad-driven views. These cover cups, all six Jack courses, Queen preview,
confirm/cancel, machine request and disconnect/reconnect. Every source snapshot,
resident asset byte, tint palette and submitted VDP1 command matches HOST.
The busiest captured view has 770 commands. UI/preview/CD guards pass, with
781,528 untouched stack bytes in this menu-only path; this is not race stack
or frame-time acceptance. Native stills were inspected locally; owner look
review and HARDWARE remain open.

Six additional views match while backing out. The returned main menu has exact
source state, restored original assets and intact guards. Separate one-byte
corruptions of each new pack are rejected before course input: course statuses
4/8 and CD checksum status 8. These controls use the same qualified binary.

The default-off binary is identical to the admitted main-menu program:
`60f69c181bd0ea9a324c83d130e82632a1cd3823561893530424452fc887e436`.
Both new disc payloads match their generated packs; all 18 previous non-program
payloads are unchanged. Private evidence is under
`$FZX_EVIDENCE/host/course-menu` and `$FZX_EVIDENCE/emu/course-menu`.

## Local generation

With `FZX_TEST_ROM` set privately:

```sh
python3 tools/course_menu_assets.py
python3 tools/course_menu_source.py
python3 tools/course_menu_preview.py
python3 -m unittest discover -s tests -p 'test_course*.py' -v
```

Enable `FZX_SOURCE_COURSE_MENU=1 FZX_SOURCE_MENU=1 FZX_SOURCE_TITLE=1` with
the existing original-content build. Generated source, images, meshes, ROM
records, private captures and discs remain outside git.
