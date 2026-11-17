# Prepared vehicle conversion

C4 is in progress. The offline `FZXMCH02` pack passes HOST conversion checks;
the runtime still uses `FZXMCH01`. No native speed or appearance improvement is
claimed. The 70-command / 1 ms target, local still comparisons and knob-off
identity remain open.

Run `python3 tools/machine_rebake.py` from Linux with the existing private
`generated/source_art` receipt current. Check the resulting receipt with
`bash tools/check_receipt.sh generated/machine_rebake/machine_rebake.receipt`.
The tool writes generated packs and their identity header locally; no build
or runtime consumer is enabled by this change.

The converter pairs coplanar convex triangles only when shared position, UV,
normal and material identities agree. It preserves original vertex rows and
an explicit source-triangle mapping. Unpaired triangles remain. Quad charts
evaluate the original two triangles' UV field, then sample original pixels;
they do not interpolate UVs bilinearly across a new diagonal. Point sampling
and bounded chart sizes are Saturn adaptations requiring native look review.

The current private conversion uses 21,824 metadata bytes, 17,064 texture
bytes, 22 shared palette groups (704 VRAM bytes) and at most 896 Gouraud bytes.
The near model retains 132 source triangles in 112 faces, including 20 pairs.
These fit the converter's 64 KB metadata / 32 KB texture limits. They do not
prove total runtime RAM or VRAM residency.

Fixed-body unlit palettes are prebaked. Original material colors and texel
RGBA remain available for shared updates when source body colors change.
Lighting, reflection and fog still need a bounded native implementation;
freezing those source changes is not part of this conversion.

## Format

All integers are big endian. The header is 96 bytes: magic `FZXMCH02`, followed
by ten u32 fields at byte 8: metadata size, texture size, texture FNV, metadata
FNV, fixture flag, LOD count, descriptor offset, palette count, palette offset
and reflection-table offset. Metadata FNV treats its own word at byte 20 as
zero. Bytes 48-53 hold default body and fog colors; other header bytes are zero.

Three 48-byte descriptors describe source LODs 1, 4 and 5. Their twelve u32s
are LOD, vertex/position/normal/face counts, vertex/position/normal/face/order
offsets, source triangle count and zero. Tables are eight-byte aligned.
Vertices are 20 bytes, positions eight, normals four and faces 56. Face records
hold four vertex IDs, palette ID, chart width/height, vertex count, texture
offset, three signed normal components, zero, signed plane constant, two
source triangle IDs, material ID, zero, three centroid sums and four zero bytes.
Triangles duplicate their final corner and use `65535` for the second ID.

Eight order tables each contain one u16 face permutation. Palette groups are
112 bytes: primitive RGB, environment RGB, body-dependent flag, equation flag,
color count, seven zero bytes, sixteen original RGBA colors and sixteen RGB1555
default colors. Unused original colors are zero; native entries repeat the
first color. Textures are packed CLUT4 indices, high nibble first. A 256-byte
source reflection table ends the metadata.

## HOST evidence and controls

`tests/test_machine_rebake.py` independently decodes table ranges, hashes,
source coverage/winding, palette bindings and order permutations. It checks
non-affine UVs on a non-parallelogram, seams, material endpoints and culling
rounding. Corrupted hashes, missing coverage, invalid palette/order entries,
wrong material binding and wrong UV diagonal are rejected. The unsafe
unmargined culling control fails. Windows and Linux tests pass; independently
generated real packs match byte for byte. Private evidence is under
`host/machine-rebake` in the evidence root.

Packed normals truncate each exact scaled component by less than one. Their
dot sign alone is unsafe at silhouettes. For a rounded model-space eye E and
first vertex A, retain faces within `sum(abs(E-A))+3+sum(abs(normal))` of the
plane when eye error is bounded by one unit per component. This is a
conservative mathematical bound; the native eye transform must establish its
own error bound before using it. Across all 9,907 saved source states, 976,470
face/view checks gave 55 false front-face rejections without the margin and
zero with it. The provisional retained-face p99 is 66, maximum 71. These are
HOST plane counts, not emitted native commands or timing evidence.

Eight centroid orders are only candidate orders. Different camera directions
inside one octant can reverse overlap order. Native comparisons must include
interior directions and octant crossings, CLUT indices 0/15, dynamic body
colors, lighting/reflection/fog, clipping, motion and full-scene residency.
No source renderer was replaced, and C4 is not complete.
