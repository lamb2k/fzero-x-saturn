# SH2ZAM view-edges-1 adoption and calibration

2026-11-16. The port now pins `view-edges-1`,
`a4863f056a8196c037075f53c6e6634aec2a20f4`, in the pointer-only commit
`36626ea`. No library files were edited. Synthetic contract checks and the
calibration image pass. Replacing the active road clipper is **blocked** by the
missing guard-band operation described below. No faster road renderer is admitted.

## What was qualified

- The existing camera-adapter and compact-road-pack synthetic suites pass before
  and after adoption under GCC and Clang.
- A new 4,096-vertex synthetic fixture produces the same complete-record hash
  (`1cbb2030`, little-endian HOST) on both library tags and both compilers.
- On the new tag, batch and in-place projection `_divu` results equal portable
  bytes; 32,768 stepped samples agree too. All 16 near-plane inside/outside
  masks pass, including the six-output cases and adjacent storage guards.
- Edge stepping is not identical to independent division at each sample. Its
  documented extra 16 fraction bits permit a one-unit difference. For example,
  delta 1, first 0, step 1, denominator 3 gives 0 at sample 3, versus 1 with a
  fresh division. This is a coordinate rounding rule, not a library defect.
  Shared edges must use one canonical direction.
- Controls that assume the old endpoint rounding or only five clip outputs fail.
  The new caller fixture runs with strict warnings and UBSan on both compilers.
- The upstream synthetic view and edge suites also pass on both compilers:
  12,595,435 view checks and 20,024,891 edge checks per compiler, with their
  negative controls detected. No retail fixtures were used for equivalence.

Public caller checks: `tests/view_edges_adoption.c` and
`tests/test_view_edges_adoption.py`. Private receipts and logs are under
`$FZX_EVIDENCE/host/view-edges-adoption`.

## Road measurement

Same method as the earlier approximately 112 ms result: 13 fixed held views,
including the worst view, over the same 685-input diagnostic schedule; same
complete scene and native road cache flags. The timer is unwrapped against
observed video fields. The after build uses clean port source at `36626ea`,
excluding title and older worktree experiments. Both use the same Yaul SH-2
toolchain, `-O2`, and Yaba Sanshiro dynamic recompiler, core 3.

| Road CPU per presented view | Before, view-contract-1 | After, view-edges-1 |
| --- | ---: | ---: |
| Mean, MODELLED | 112.045 ms | 112.033 ms |
| p99 / maximum of 13, MODELLED | 213.544 ms | 213.551 ms |

The 0.013 ms mean change establishes no useful gain. This is a **tag-only**
comparison of the frozen renderer. It does not measure a new `_divu` road stream.
The active renderer still uses its original Q20.12 transform, split and guard
clip path. C3 currently provides camera conversion, pack admission and gathering;
there was no native batch/edge stream already wired in to switch over.

Both builds preserve the diagnostic final state, all held scene packets and
command hashes. Allocation guards pass. This diagnostic neither finishes a
race nor establishes whole-frame FPS. Exact image/source bindings and detailed
measurements remain private in `native-comparison.json`; physical timing is UNRUN.

## Calibration image

Downloaded `sh2-compile-evidence` from SH2ZAM CI run `43998059830`, source
`ccce04f7251900eb847de05cef3f5ad7304efa7f`. This calibration source is newer than
the adopted library tag; the port pointer remains exactly `view-edges-1`.

Verified ISO SHA-256 before boot:
`e3297503ac1f7b30f6c173379355b2c84971f6a1ea7b891208db01b54de8ad28`.

Emulator: **Yaba Sanshiro 1.20.37**, local MCP build `d7767591`, dynamic
recompiler (core 3), OpenGL (core 1), emulated 4 MB DRAM cartridge. This image
contains no game data. The saved result screen is
`$FZX_EVIDENCE/emu/sh2zam-calibration/boot-20261116-212616.png`.

These are **EMULATOR calibration readings**. Their timing remains **MODELLED**
relative to a physical Saturn. No row displayed `OVF`.

| Screen row | Cycles/unit | Raw ticks | Unit |
| --- | ---: | ---: | --- |
| nop x16 loop | 1.09 | 559 | operation |
| dmuls.l | 4.28 | 2193 | operation |
| dmuls + sts mach | 5.24 | 2683 | pair |
| mac.l @+,@+ | 2.33 | 1192 | operation |
| sh2z_div DIV1 | 109.78 | 3513 | call |
| DIVU 64/32 | 76.50 | 2448 | division |
| int64 multiply, GCC | 50.53 | 1617 | operation |
| int64 divide, GCC | 300.00 | 9600 | operation |
| HWRAM cached | 3.25 | 6653 | word |
| HWRAM cache-through | 8.25 | 16903 | word |
| LWRAM cached | 15.51 | 31761 | word |
| LWRAM cache-through | 15.26 | 31250 | word |
| Cartridge cached | 33.51 | 68631 | word |
| Cartridge cache-through | 41.25 | 84483 | word |
| view_batch | 818.56 | 52388 | vertex |
| view_batch_divu | 171.95 | 11005 | vertex |

Screen checks:

- Memory sums: `85fc97af =`, `00000000 =` (high/low RAM cached versus through).
- View FNV: `75059307`.
- `divu==portable YES`.
- `DIVU vs C: 0/4096 bad`.
- Cartridge ID: `5c` (4 MB); sink: `26932039`.

The batch fixture is about 4.76 times faster with DIVU in this emulator. That
ratio does not apply to the whole road pass or frame. The 76.50-cycle divider
row includes call overhead and does not independently measure bare divider
latency. The owner still needs to run this exact ISO on hardware.

## Concrete request for Opus

`sh2z_view_clip_near[_divu]` explicitly leaves side clipping to VDP1. However,
VDP1 command vertex coordinates must be in **-1024 through 1023**, sign-extended
from bit 10. Its user/system clipping rectangle cannot make an unrepresentable
command vertex legal. See Sega's
[VDP1 command-coordinate specification](https://www.infochunk.com/saturn/segahtml_en/hard/vdp1/hon/p06_70.htm).

The public synthetic fixture demonstrates the gap without any game data:

1. Identity view, focal 160, centre (160,112), near 1; one face at depth 160
   includes the screen edge from (-2840,112) to (160,12). The face is visible.
   Near-only clipping retains the illegal -2840 coordinate; trivial rejection
   by common outcodes cannot remove the face.
2. Clamping that endpoint to -1024 changes the edge crossing at x=0 by more
   than eight pixels, failing the existing one-pixel requirement.
3. Raising near to 161 removes this visible face altogether. It is not a
   geometry-preserving alternative.

Please add a tagged, bounded **view-space guard-band polygon clip** that clips
near/far and side planes before projection, with a caller-supplied rectangle
inside VDP1's representable range (the reference uses -900 through 900).
Specify output capacity and overflow/failure behavior, including non-planar
quad cases; preserve canonical shared-edge crossings and the current rounding
contract. Portable and `_divu` variants need byte-equivalence fixtures and
controls. Include the interpolation information needed for caller-owned
attributes, or document a supported attribute-preservation approach.

No replacement general clipping kernel was written in the port. The working
reference clipper remains intact; deleting it now would remove the only qualified
guard-band path. This is the exact unblock condition for replacing its role in
the new stream. The pending C3 image-space gate must still pass before admission.

**P5:** yes, after the guard-band blocker: bounded face-normal dot products and
Gouraud/light-ramp generation would support C4; batched fog/shade work could
remove repeated per-piece operations. Offline model preparation, palette/data
formats and native command construction remain the port's responsibility.
Synthetic kernel wins will require fresh complete-road/frame measurements.
