# Original Time Attack records

The first-course flow currently stops after the original finish/results panel
requests name entry or the records table. This card prepares the original artwork
for that continuation. It does not establish native records presentation, retry,
audio playback or durable saving.

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

Next, preserve source input repeat, keyboard transitions, RNG history, record
name writes and draw-side state through the explicit finish-menu-ready boundary.
The native scene needs guarded residency, Font1/3/4 packet selection, spinning
keyboard glyphs and up to six original record-machine previews. Session records
and save-request events do not establish persistence. Ordinary retry and source
audio playback remain separate required continuations.
