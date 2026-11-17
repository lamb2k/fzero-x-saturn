# Original first title screen

`FZX_SOURCE_TITLE=1` adds the original cartridge's first title screen before
race setup. It uses a receipt-bound copy of the source `Title_Update` routine,
original images, and the Saturn pad. The flag defaults off. This is a frontend
foundation: course/machine/settings selection and audio remain unfinished.
Without the separate [main-menu diagnostic](MAIN_MENU.md), this arm continues directly into the race.
It does not satisfy the complete boot/selection/race/results/retry gate.

## Content and state

`tools/title_assets.py` converts five original images into `TITLE.PAK`:
background, logo, push-start, copyright and disconnected-controller warning.
The atlas occupies 186,944 bytes of temporary VDP1 texture space. Four RGBA16
images retain source RGB5 channels and binary alpha. The original I4 warning
mask becomes fixed red with binary transparency. No replacement art is used.

The native display crops eight lines from each edge of the source's 320x240
layout. The first title variant is retained. The push-start color cycle and
warning pulse/backplate are not yet implemented; these omissions remain visible
presentation gaps. Original title/confirmation sound is also absent.

`tools/title_source.py` extracts the unchanged stock `Title_Update` body into
a generated closure. The bridge maps A/C and Start, preserves fresh-press
semantics, stalls the source timer when disconnected, accepts confirmation
starting at tick 80, and shows push-start starting at tick 95. The unused random
call is counted without consuming the race RNG. Source audio requests and
stop-music requests are recorded, not played. No 64DD state is emulated.

The wrapper loads the pack through the existing checksum-checked CD path,
borrows the command buffer and temporary texture space, and guards both ends
of the atlas. It waits for confirmation release before returning. Two explicit
transparent full-screen draws clear both framebuffers before race assets reuse
VRAM. Ordinary timed erase left stale lower title lines in Ymir; merely submitting
two empty lists did not clear them.

## Qualification

HOST: independent channel/mask conversion checks all 93,472 pixels. Synthetic
asset tests reject wrong declarations, spans, compression and layout anchors.
GCC and Clang UBSan run the actual generated source timeline and agree; changed
unlock/prompt thresholds, repeated-press behavior and disconnected behavior fail.
Both generation receipts are current. SH-2 builds pass, and the title-off binary
is identical to the admitted countdown image.

EMULATOR checks use a fresh Ymir instance, cache enabled, a 4 MiB cartridge and
the software renderer. Emulated SMPC pad reports exercise disconnection,
reconnection, early-held confirmation, prompt appearance, fresh Start/A and
release before entry. Debugger timeline holds support these checks; this is
not full pad-only or physical acceptance.

The native title's disconnected, pre-prompt and prompt views match independent
composition of the converted original images in both VDP1 framebuffers, with
exact resident atlas bytes. A deliberately shifted logo fails the image check.
This validates the native adaptation, not N64 raster identity or owner approval.

The 685-input race replay after title entry matches the title-off build in all
13 held views: source packets, native commands, referenced textures and both
VDP1 framebuffers. Screen differences are confined to diagnostic timing digits.
All allocation guards pass; the minimum untouched stack is 47,632 bytes.
The comparison restarts both arms before their first race tick. An earlier
harness ran three extra ticks in one arm, changing the source sign's previous
camera at input 1; aligning that setup removed the packet difference.

A one-byte mutation of `TITLE.PAK` is rejected before race startup by the native
loader (title status 4, CD checksum status 8). All 16 existing non-program disc
payloads are unchanged; the single added payload exactly matches its receipt.
Private evidence is under `$FZX_EVIDENCE/host/title` and
`$FZX_EVIDENCE/emu/title-current`. HARDWARE and owner appearance review remain
UNRUN. There is no new speed claim; the combined race remains below target.

## Local build

With `FZX_TEST_ROM` supplied privately:

```sh
python3 tools/title_assets.py
python3 tools/title_source.py
python3 tests/test_title_assets.py
python3 tests/test_title_state.py
```

Enable `FZX_SOURCE_TITLE=1` with the existing original-content native build.
Required source content and receipts must be current. Converted assets, native
captures and extracted source closures stay outside git.
