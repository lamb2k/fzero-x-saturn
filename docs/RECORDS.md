# Original Time Attack records

The admitted first-course flow stops after the original finish/results panel
requests name entry or the records table. The continuation now has source state,
draw capture and a native command writer under qualification. Native execution,
retry, audio playback and durable saving remain unaccepted.

`tools/records_assets.py` extends the existing eight results sheets with the BEST
label and complete source Font1, Font3 and Font4 mappings. Their 48, 234 and 235
mapping slots resolve to a combined 229-image bank of 46,144 bytes. Original font
advances and Font4 crops are retained. The private generated header contains the
three mappings; the existing results-only renderer cannot consume it unchanged.

Original RGB5 color and binary alpha survive conversion. I4 intensity becomes
RGB5 grayscale, with zero transparent. CLUT4 is used only when lossless; other
images retain RGB1555. Rows receive transparent padding to eight-pixel alignment.
The existing sixteen-level red pulse bank is unchanged (151,552 bytes stored,
9,472 bytes for the active level). Most Font1 images require RGB1555; the native
consumer must validate tint behavior rather than assume every glyph is CLUT4.

HOST qualification:

- All 39,104 stored pixels match independently decoded source colors, alpha and
  crop/padding. A one-pixel shifted control differs at 14,880 pixels.
- Windows and Linux produce identical packs, header, manifest and receipt.
- The original eight results sheets and pulse bank remain byte-identical.
- All four output corruption controls fail receipt verification.
- Three focused tests cover compressed/raw extents, malformed declarations,
  complete font mappings, color/alpha and crop behavior.

Private evidence is under `host/records-assets`. The UI pack SHA-256 is
`61aacfed0471f5ea7401cbb8e5ac4bffa834b0903a0fcfaa4f24d835fb02473d`.
No retail-derived artwork, source tables or evidence is published.

The current HOST continuation preserves source input repeat, keyboard transitions, RNG history, record
name writes and draw-side state through the explicit finish-menu-ready boundary.
The native scene needs guarded residency, Font1/3/4 packet selection, spinning
keyboard glyphs and up to six original record-machine previews. Session records
and save-request events do not establish persistence. Ordinary retry and source
audio playback remain separate required continuations.

The full source replay matches GCC and Clang over 7,924 ticks, with name entry
and the records terminal at frame 7,860. Its first 7,759 frames retain the admitted
prefix. All 29 held draw views produce matching native-writer commands, palettes
and model previews on both HOST compilers. This does not establish SH-2 execution.

Two transition failures found during that sweep are fixed: Font4's empty-time
minus uses its original 8-pixel descriptor width rather than 16-pixel storage;
the spinning END selector accepts a losslessly palette-packed Font1 glyph.
The spinner allocates its original untinted palette after the UI palettes and
rejects exhausted storage. Regression cases cover empty times, palette separation
and bounds; restoring the wrong minus width fails the draw test. HOST drawing
tests cover 1,289 captures; the spinner projection fixture covers 1,024 vertices
with zero maximum pixel error. Native API tests use mocked projection.

The integrated Linux HOST suite passes all 19 tests with no skips, including
GCC/Clang state, draw, command-writer and lifecycle checks and their negative
controls. `host/records-native/integrated-host-suite.json` records the run.

The corrected Saturn image contains the two new packs and preserves all 181
previous non-program payloads. The records UI and model pack corruption controls
both fail during loading before race entry. Records reuses the existing results
color-pulse bank, whose bytes match the separately generated records tint bank.
The valid emulator replay completes all 7,924 inputs and three diagnostic resets.
All 29 checkpoints match the source state, draw packets, pose, camera and RNG;
minimum sampled stack reserve is 23,064 bytes. The 20 active records captures
match both HOST compilers' native command streams and resident UI, model, tint
and palette data. The entered name `AAA` survives the session-record resets.
Private evidence is under `host/records-native` and `emu/records/full1`.

The rebuilt pre-records/current-library control is byte-identical to records-OFF
(SHA-256 `2d3887227823237fabeef0cf9f535eeaef9d320c1888ed0dc15df4c57190d8ba`).
The historical results binary used the older library; the dependency update adds
832 linked bytes. This isolates feature-OFF identity with dependencies held fixed,
not a speed claim. `host/records-native/control-currentlib-2` is the valid rebuild;
the initial relative-target control did not rebuild and is explicitly invalid.
Local framebuffer review shows the name-entry and records screens; diagnostic
text and the existing race HUD obscure parts of the presentation, so final
appearance remains open. Ordinary pad retry remains a separate gate.
