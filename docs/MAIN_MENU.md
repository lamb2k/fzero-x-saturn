# Original cartridge main menu

`FZX_SOURCE_MENU=1` adds the original US cartridge main menu after the title.
It requires `FZX_SOURCE_TITLE=1` and defaults off. The six mode signs, submenu
choices, confirmation animation and back navigation use the original source
rules and artwork. This is a diagnostic frontend phase: course, machine,
settings, options and records destinations are not connected yet.

Source destination requests stay held for inspection; B returns to the menu.
A source title request returns to the title. No unsupported selection is
redirected into the existing Time Attack race. With this diagnostic flag on,
the frontend therefore does not enter a race. With it off, the qualified title
and race path remains byte-identical. The full first-course goal stays open.

## Source state

`tools/menu_source.py` extracts the pinned stock `MainMenu_Update`, six bounded
object drawing routines, OK movement helpers and color pulse routine. It
selects the US cartridge branches, excluding PORT and expansion-kit additions.
The adapter records typed image placements, colors and sound requests; it does
not interpret display lists. Snapshot reads do not advance source counters.

Fresh presses, 15-tick initial directional repeat followed by five-tick repeat,
submenu limits, confirmations, transition gates and distinct destinations are
retained. Audio requests, including multiple cues in a tick, are queued but
not played. The initial wallpaper and fresh, locked save state are explicit.
Cheat wallpaper transitions and persistence are not implemented. The native
slice reads port 1; multiplayer entry is not part of this frontend milestone.

## Native presentation

`tools/menu_assets.py` converts 19 original images to a 371,736-byte `MENU.PAK`.
Background, signs and flames use RGB1555. Grayscale labels use lossless CLUT4
indices with small per-draw tint palettes. Conversion is offline, receipt-bound
and private. The native scene borrows boot texture space and the command buffer,
with guards around the atlas and palette area.

The 320x240 layout is cropped by eight lines at each edge for the 224-line
display. The source wallpaper's black alpha-180 overlay is folded into RGB5
at 75/255 brightness. Header intensity uses binary transparency. Unselected
signs use 128/255 tint and ordered 11/16 coverage; selected sign alpha below
224 uses 13/16 coverage, otherwise full coverage. These bounded dither steps
approximate source blending. Label colors use integer RGB5 multiplication;
half-alpha labels use native half transparency. Owner appearance review is open.

Each picture explicitly clears the complete framebuffer before drawing.
Returning to title clears both buffers before the texture allocation is reused.

## Evidence

HOST: 196,656 converted pixel samples, palettes and sign variants match an
independent conversion check. Malformed format, dimensions, compression and
palette inputs are rejected. GCC and Clang under UBSan produce identical source
snapshots. Seven controls fail: changed grid navigation, lost input edges,
wrong repeat cadence, ignored transition gate, early slide completion, mutated
snapshot reads and discarded alpha. Asset and source receipt mutations fail.

EMULATOR: fresh Ymir, software rendering, SH-2 cache enabled and 4 MiB cartridge.
Emulated pad reports exercise Time Attack selection, records-row navigation,
confirmation, cancellation, slide completion and the course request. All 20
held views have exact HOST/native source snapshots, exact resident atlas and
palettes, and zero pixel differences against independent native composition in
both VDP1 framebuffers. Shifted-header controls fail. Memory guards pass; the
menu-only build retains 850,372 untouched stack bytes. This is not a race memory
or timing result. B also returns to the title in a separate run.

A one-byte corrupted `MENU.PAK` is rejected before menu input (menu status 4,
CD checksum status 8). All 17 existing non-program disc payloads are unchanged.
The menu-off binary is identical to the qualified title binary:
`fe5456afe2b77e32f901288138d970b8428d9fa334774e3bbcc0de19be321fcc`.

Private evidence: `$FZX_EVIDENCE/host/menu` and
`$FZX_EVIDENCE/emu/menu-current/route2`, `back`, `badasset`. The checks validate
the documented Saturn adaptation, not N64 raster identity. HARDWARE, owner look
review, audio, destination screens and pad-only end-to-end acceptance remain
UNRUN. There is no new race performance claim.

## Local generation

With `FZX_TEST_ROM` set privately:

```sh
python3 tools/menu_assets.py
python3 tools/menu_source.py
python3 -m unittest discover -s tests -p 'test_menu*.py' -v
```

Enable `FZX_SOURCE_MENU=1 FZX_SOURCE_TITLE=1` with the existing original-content
build. Generated source, atlases, disc images and captures stay outside git.
