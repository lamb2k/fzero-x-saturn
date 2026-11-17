# Compact source road bake: C2

The `FZXRLD01` bake preserves all 2,613 original vertex records and 2,483
quads in **151,128 bytes**, below the 160,000-byte limit. Its source-derived
texture bank occupies **147,328 bytes**, below 150,000 bytes, including three
fog variants. This is HOST asset preparation. The new pack is not yet used by
the Saturn renderer and does not change the measured frame rate.

## Content and conversion

- Source positions and UVs remain signed 16-bit values, in their original order.
  Their common black, opaque shade is encoded once. Every compact 10-byte vertex
  equals the first 10 bytes of the admitted source vertex record; no vertex
  welding or pre-split positions are stored.
- All original quad records match the admitted native tile pack byte for byte.
  A separate 4-byte record stores first UV phase, repetition count, final phase
  and whether the two longitudinal edges have unequal endpoints. Reconstructing
  the span is checked against the retained source UVs, including negative phases.
- The 300 original chunks and all conditional outer-wall flags remain intact.
- Far candidates span two or four chunks and reuse original endpoint indices.
  There are 2,288 two-chunk and 1,952 four-chunk candidates. Each retains its
  original starting quad and a conservative chord-error bound. The decoder
  reconstructs the chain and rejects changed endpoints or error bounds.
  These candidates do not activate merges; C1 passed without merging.

Textures are converted offline from the existing original-road strips. Each
strip has images containing one, two, four and eight repetitions at its base
height, plus the existing mean row. Higher tiers use periodic box filtering.
The original three fog variants are filtered independently. For lookup images
whose source strip width is not a multiple of 16, each pixel is duplicated
horizontally (8 to 16 or 24 to 48 pixels in this course). This keeps every partial
row start aligned to VDP1's eight-byte texture-address unit. All decoded colors,
normalized horizontal sampling, source UVs and geometry remain unchanged; 55
images need this expansion. The original strip width remains in the strip record,
while the image record contains the stored width.

Of the 140 tier images, 103 use 4-bit lookup indices with three RGB1555 palettes;
37 retain 16-bit pixels in all three fog variants. Lookup conversion is admitted
only when every stored pixel stays within one RGB5 step per channel of the
source-derived filtered image, in every fog variant. More colourful images use
the RGB fallback. This fixed error bound was not increased to meet memory.

Exact texture and palette byte sequences share storage. This reduces the
208,640-byte unshared bank to 147,328 bytes, including its header. The mixed bank
fits the 150 KB ceiling; it does not claim to meet the 96 KB all-lookup target.

## Binary contract

All integers are big-endian. Sections and texture addresses are aligned to
8 bytes. Reserved bytes must be zero. Checksums are FNV-1a with their own
four-byte field zeroed. The pack also binds the complete texture file by SHA-256.

`ROADLOD.PAK` has a 160-byte header:

| Offset | Field |
| ---: | --- |
| 0 | `FZXRLD01` |
| 8 / 12 / 16 / 20 | Version 1, total bytes, constant-shade flag 1, checksum |
| 24 / 56 | Source JSON SHA-256 / complete texture-file SHA-256, 32 bytes each |
| 88 | Eight u32 counts: vertices, quads, chunks, strips, images, far2, far4, texture bytes |
| 120 | Eight u32 section offsets, in the order below |
| 152 | Course base fog RGB and alpha 255; four reserved bytes follow |

| Section | Record bytes | Contents |
| --- | ---: | --- |
| Vertices | 10 | Five s16: X, Y, Z, S, T; implicit shade `(0,0,0,255)` |
| Quads | 16 | Four u16 vertex IDs, strip ID, flags, original first triangle ID, chunk ID |
| Repetitions | 4 | One u32 per quad; primitive quads use zero |
| Chunks | 8 | First quad and count, both u32 |
| Strips | 16 | Material, width, period, reserved; signed S endpoints; first image and image count |
| Images | 32 | Four u16: mode, width, height, repetition factor; three u32 pixel addresses and three palette addresses |
| Far2 / far4 | 16 | Four vertex IDs, strip ID, original first quad ID, chord error, outer-wall flag; all u16 |

Repetition bits 0-9 hold the first phase, 10-15 the piece count, 16-25 the
final phase, and bit 26 the unequal-edge flag. Higher bits are reserved.
The UV unit is `period * 32`. A zero final phase means the end of a complete
period. The span is `(count-1)*unit + (final_phase or unit) - first_phase`.
Source periods 8, 16 and 32 fit this contract; unsupported values fail.

Image order per strip is 1, 2, 4, 8 repetitions, then factor 0 for the mean row.
Mode 0 stores RGB1555 pixels; its palette addresses are zero. Mode 1 stores
4-bit indices, first pixel in the high nibble, with 16 RGB1555 entries per palette.
Its three pixel addresses are equal; palette selection supplies the fog variant.
Lookup rows must have an eight-byte stride. Their stored width is twice the
source width when that width is not divisible by 16; otherwise it is unchanged.
RGB image widths always match the source strip. Widths exceeding 504 are rejected.
Addresses are absolute offsets into `ROADLTX.PAK`, including its header.

`ROADLTX.PAK` has a 64-byte header: `FZXRTX01`, then u32 version/total/checksum/
opaque flag at offsets 8/12/16/20, source SHA-256 at 24, and u32 image count/data
offset at 56/60. Data begins at 64. Both index 0 and index 15 may contain ordinary
opaque road pixels; the native command mode must preserve them.

The chord error is the upward-rounded maximum L1 distance, in source units,
between each intermediate endpoint row and its evenly spaced endpoint chord.
It is a conservative admission aid, not an image-space certificate. Source
geometry makes the maximum 837 units for two chunks and 1,183 for four.

## Gates and remaining runtime work

Synthetic tests cover byte-exact source rows, curved/straight far chains,
negative and unequal UV phases, periodic filtering, palette fallback and
valid-checksum corruptions. Wrong repetition counts, missing coverage, bad
image addresses, nonopaque palettes and understated far error bounds fail.
The original asset conversion checks every compact vertex/quad and all 140
images against the admitted native assets: before horizontal duplication, 194,599 of 210,864 compared source pixels
are exact; 16,265 differ by at most one RGB5 step per channel. The aligned-bank
check decodes all 140 images and proves exact pixel duplication from that bank,
with every partial-row address aligned. Windows and Linux payloads agree.
The receipt rejects a deliberately altered output.

The stored fog variants use the course base colour, matching the admitted
texture bank. Source fog colour changes in 415 of the 9,907 replay inputs.
C3 must retain those dynamic states in its fog tables/shading; it must not
assume that the base colour remains constant. C3 also owns lookup-mode setup,
image-space and wrong-repetition controls, native residency, stack/VRAM accounting,
and the measured road CPU gate. Full scene/HARDWARE acceptance remains open.

## Reproduce locally

```sh
python3 tools/road_bake.py
bash tools/check_receipt.sh generated/road_bake/road_bake.receipt
python3 -m unittest discover -s tests -p test_road_bake.py -v
```

The public tests synthesize their own road. Original generated packs, colour
reports and source comparisons remain private. Evidence is under
`host/road-bake/source-qualification.json` and `host/road-row-align/comparison.json`; C1's command result is in
[ROAD_LOD_CENSUS.md](ROAD_LOD_CENSUS.md).

Metadata SHA-256: `cbf9e55186fcb2103e6b6d8d57fc92da4c5ff6bafff82c2bed07bd125d62308e`.
Texture SHA-256: `fd5b47f24d4abed280c1d4889e96c3e14fe40702efc76fab93f9d55a4cde1e6d`.
