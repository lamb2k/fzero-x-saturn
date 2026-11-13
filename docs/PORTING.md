# Native port milestones

## Target

A native Sega Saturn adaptation of F-Zero X, using the chosen G-Diffuser source
tree and SH2ZAM. The first authentic gameplay milestone is one imported Mute
City course, Blue Falcon, original driving behavior and collision. Frame-rate
and content budgets will be set from hardware measurements.

## Foundation in this repository

G-Diffuser is pinned at `4f9320675758fd524bb3bc9ea98b913a09d48552`; its
decompilation submodule is `Zorkats/fzerox` at
`b944a4135409d24979f5c74b001befab8345761a`. The top-level source selection is
preserved instead of silently following a different decompilation branch.

The common C core has no heap or floating-point arithmetic. It evaluates the
source's adaptive cubic spline in Q16.16, samples eight rings per control-point
segment, interpolates widths/up vectors by approximate sampled arc length,
builds a ribbon and renders clipped, sorted polygons. It caches immutable mesh
bounds and uses SH2ZAM's prepared transforms. The alternate exact transform path
exists for output comparison.

The prototype driver follows the sampled track with lateral steering. It has
no original vehicle tuning, air physics, surface collision, boosts, energy,
opponent behavior or race rules. Its circuit, craft, colors and sky are fixtures.

The native backend submits flat RGB1555 VDP1 polygons and overlays a VDP2 HUD.
Only the master SH-2 is used. Input is digital. Browser rendering shares the C
geometry but uses Canvas to display the resulting polygons, so its rasterization
is not a VDP1 accuracy test.

### Numeric and memory contract

- Imported positions/radii use 1/16 original game scale and signed Q16.16.
- There are 4–64 control points, at most 512 sampled rings and 512 output polygons.
- Runtime input bounds: position components within ±4096 scaled units, radii
  from 0.25 to 256, tension from 0 to 2. Degenerate sampled geometry is rejected.
- Banks preserve the original 4096-step angle quantization before conversion to
  SH2ZAM turns. Spline arithmetic is quantized and is not bit-identical to float.
- Uploaded courses use separate staging storage. Validation completes before
  replacing the active course, and the active course is copied into owned memory.
- Geometry/render buffers are static. A 515-command native VDP1 buffer uses
  16,480 bytes. Actual linked memory usage is emitted by each native build.
- Drawing clips near, far and all four screen planes before integer projection.
  Painter sorting by average depth can still fail for intersecting geometry;
  the original track segmentation/visibility strategy is a later milestone.

## Next: authentic asset validation

Supply a US rev0 ROM, run the importer, and verify Mute City 1 against the chosen
upstream. Confirm checksum and adaptive tensions, spline sample positions,
bank sign, control-point scale and spawn orientation. The initial implementation
has been tested with synthetic records only.

Import `Machine_DrawBlueFalconLod5` as the first real machine. The selected source
uses 30 vertices and 11 triangles across `D_70077B8`, `D_7007878` and `D_7007778`
in uncompressed `machine_global_gfx` (US rev0 ROM base `0x1E23F0`). Extract locally;
do not commit extracted retail model/texture data. Preserve source indices and
check all triangles against an upstream reference render.

## Next: original driving and collisions

Audit `Racer_UpdateFromControls` and the tuning path through
`Racer_InitMachineStats`. They depend on globals and ROM-backed tuning data;
selecting one machine still requires extracting those values. Isolate a
deterministic one-racer update behind a platform-independent interface.

Capture input/state traces from the selected upstream. Convert the measured
hot math to fixed point with stated range and error contracts. Compare position,
speed, orientation, boundary behavior and lap transitions on identical traces.
The prototype driver must remain labeled until those traces pass.

## Next: console validation and performance

First boot the generated CUE in a full Saturn emulator and then on hardware.
Check video setup, pad input, depth order, near-plane crossings, worst turns,
framebuffer completion and memory headroom. Separate CPU simulation, transforms,
command generation and VDP1 completion time. A correct native compile is not a
boot test; isolated SH-2 instruction models do not establish full-system FPS.

Only after the representative scene is measured should we choose mesh density,
draw distance, audio memory, texture formats or slave-CPU work. Compare a competent
baseline against SH2ZAM on identical game workloads, including setup and output
checks. Do not extrapolate a kernel speedup into an overall game speedup.

## Later

Track features and other surface shapes; machine materials and scenery; audio;
opponents and collision; menus, race rules and saving; course coverage; 3D-pad
input; sustained hardware testing. Each phase should retain a small bootable
artifact and an explicit statement of what has actually been verified.
