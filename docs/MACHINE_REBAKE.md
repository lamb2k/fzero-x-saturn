# Prepared vehicle conversion

C4 is blocked on ordering and cost. The offline `FZXMCH02` pack and default-off
native experiment have been exercised. Thirteen held views reduce machine CPU
from 52.112 to 9.273 ms mean MODELLED, but miss the 1 ms target and expose an
overlap defect. The default renderer remains `FZXMCH01`. This is an experimental
checkpoint, not an admitted replacement or a completed C4 card.

Run `python3 tools/machine_rebake.py` from Linux with the existing private
`generated/source_art` receipt current. Check the resulting receipt with
`bash tools/check_receipt.sh generated/machine_rebake/machine_rebake.receipt`.
The tool writes generated packs and their identity header locally. The optional
`FZX_MACHINE_REBAKE=1` build selects the new consumer and checks its receipt. It
requires `FZX_SOURCE_MACHINE=1`; the normal recipe leaves it off. On disc the
new packs reuse `MACHINE.PAK` and `MACHTX.PAK` to stay within the CD root limit.

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
The experimental runtime updates shared palettes on body-color changes and
computes per-normal lighting/reflection with Gouraud adjustment. Fog remains
source-driven. The half-intensity palette representation is an approximation;
its visual gate has not passed.

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
The frozen source renderer remains the default, and C4 is not complete.

## Native experiment and failed gates

The pose adapter maps original model axes into the source racer basis, composes
one model-to-camera transform and calls SH2ZAM's batch projection. Over 9,907
HOST states, 599,449 on-screen vertices stay within one pixel of the reference.
GCC and Clang UBSan records agree. Culling is disabled when the inverse-basis
error bound cannot be certified; 36 views therefore retain extra faces.

HOST rendering emits p99 67 commands, maximum 112, with no clipped faces on this
route. This fails the strict maximum of 70. Synthetic tests exercise corruption,
capacity, dynamic palettes and repeated draws; altered hash/capacity/palette
controls fail. The code still has a callback per emitted face and a slow clipping
fallback. Clipped solids use representative material color and averaged current
lighting/reflection/fog; they remain a texture approximation. A synthetic clipping
test verifies lighting and fog independently change the output and rejects the
old half-intensity static-color control. Neither path is presented as the final
bounded architecture.

The native 13-view comparison uses identical 685 inputs and admitted source packs.
Camera, machine, road and backdrop packets and other draw hashes remain unchanged.
Machine mean/max CPU changes from 52.112/60.049 to 9.273/10.887 ms MODELLED. Separate
phase profiling attributes mean 0.694 ms to pose, 0.553 to projection, 0.103 to
palettes, 1.298 to lighting and 5.863 to command construction. These are subsystem
measurements, not frame rate. The knob-off executable is byte-identical to the
admitted view-edges baseline. Physical hardware is UNRUN.

Local stills expose a missing wing marking at held input 624. A HOST perspective
depth comparison over 15 views finds 572 wrong overlap pixels with the eight
centroid orders. Nearest-vertex, farthest-vertex and per-view centroid alternatives
also fail (395, 906 and 581 pixels respectively). Those alternatives were tested
on the host only; the candidate pack was not silently changed. A qualified offline
occlusion order or face partition is needed before reopening the look gate.

All checked allocation guards survive the held run, but only 2,612 painted stack
bytes remain untouched, below C7's 16 KB requirement. The low-RAM metadata/work
allocation occupies 28,492 bytes, excluding surrounding guards.

The uninterrupted experimental race completes all 9,907 inputs, all three laps,
source time 121,814 ms and the same final pose. Camera, machine, road and backdrop
packets match the baseline. At the actual 1,981 presented inputs, road and machine
command totals and ranges match independent HOST records. Whole-scene delivery is
3.837 fps MODELLED: mean 260.606 ms, p95 383.333 ms, maximum 416.667 ms and 28,980
missed fields. Machine CPU mean/max is 9.315/13.283 ms. All allocation guards pass;
only 2,316 painted stack bytes survive, failing the 16 KB target. The 16-bit wrapped
clock control is rejected. This is far below the required 60 fps.

That full replay used the retained pre-fix binary below. The clipped-solid fix is
not exercised by this route: all 9,907 HOST command records are byte-identical
before/after, with zero clipped faces. The final binary passes all 13 held-view checks with identical source packets and
render statistics, allocation guards intact and 2,388 painted stack bytes left.
Its ISO SHA256 is `1b897fcbc1f008b381b56b4f9486f7fde6cfd3f35c7c593cd986ccfdfc9f7029`.
This does not relabel the earlier full race. The final knob-off executable matches
the admitted baseline byte for byte.

Private evidence: `host/machine-rebake/native-comparison-v1.json`, `pose-*`,
`draw-*`, `order-comparison-*`, and `emu/machine-rebake/1-v2` / `1-profile` / `race`.
Candidate ISO SHA256: `06b3bc335281e28025d893537825e6df7272b4bfc1608218a1d94e2938744f00`.

Unblock conditions: a source-preserving ordering/partition passes overlap and
native still checks; actual emitted commands stay within 70; vehicle CPU reaches
1 ms with bounded work; the combined memory and frame gates pass. Do not retry
small ordering-key changes without a new mechanism. Move to dependency-safe C5
or C8 work while these requirements remain open.
