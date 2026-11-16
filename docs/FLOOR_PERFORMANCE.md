# Original floor on Saturn background hardware

`FZX_SOURCE_FLOOR_RBG=1` moves the original Mute City floor onto VDP2 RBG0.
It defaults off and requires the original planes and native road paths. Clouds
retain their existing VDP1 path. Simulation, camera, road and machine are unchanged.
This is a measured rendering step; the complete scene still misses the 60 fps target.

## Content and rendering

The original 64x32 RGB1555 floor is rearranged offline into 32 native characters.
Every pixel survives this rearrangement unchanged. A repeating character map
uses the original source UVs. There is no new artwork or runtime pixel conversion.
Boot reads receipt-bound banks directly from CD into VDP2 VRAM and verifies them.

The CPU derives a perspective homography from the source plane and camera.
VDP2's per-dot coefficient address selects a reciprocal from a resident table;
both axes use that coefficient, including rolled cameras. A line window bounds
the finite authored floor. Matrix/address fields use hardware ten-bit fractions.
The reciprocal table samples bin centers; this is an approximation.

Eight source-derived fog bands replace the previous tile/Gouraud treatment.
The first polygon clears the earlier sky from the floor area. Intermediate bands
use VDP1 palette pixels at priority 5 and VDP2 color calculation over RBG0 priority 3.
The final band is opaque original fog. Road, machine, clouds and HUD retain direct
RGB priority 6 and draw afterward. Some fog transitions and brightness differ from
the previous path. Final source-reference appearance acceptance remains open.

The completed VDP1 image and new rotation/window tables are presented together.
The variable-interval render callback uploads 992 bytes during VBlank. All 19 held
views match pending/presented input identity and exact table readback. Upload starts
and ends in blanking; captured upload cost is 0.094-0.095 ms MODELLED.
Actual word-register reads verify WCTLC=0003, SPCTL=1520, CCCTL=0040, PRISA=0506
and the eight blend ratios. This emulator returns zero for byte reads of these
registers; the private receipt uses word reads, with the original failed byte
observations retained separately.

## Evidence

- GCC and Clang UBSan process all 9,907 source views with identical output.
- Independent floating projection checks 20,430,862 visible samples on a four-pixel
  grid: 99th-percentile periodic UV error 0.550 texel, maximum 1.017 texels;
  maximum below half fog is 0.542 texel. No sampled coefficient is invalid.
- One sampled coverage difference is a fixed-view/clip edge moving 0.0043 pixel
  across a pixel center. This is reported, not hidden by an expanded tolerance.
- Authored packing/reciprocal/window tests pass. Removing the thin-left-sliver
  guard reproduces the signed-division rounding defect and fails the control.
- The final EMULATOR image replays all 9,907 inputs, finishes at source time
  121,814 ms and retains the prior final pose. All 19 other scene packets/hashes
  match; floor packets and cloud-only hashes match independent HOST executions.
- CD/low-RAM/cache guards pass; painted stack retains 8,012 bytes.
  A corrupted coefficient bank is refused before race initialization. Stale
  generated output and invalid flag/dependency settings are also rejected.
- Floor-off reproduces the entire previously admitted vertex-cache binary.

Image SHA256: `5f99f0982cd35fd79f0913cc175e0d93e52071857213e35f11c26be3cdc9fc27`.
Private evidence: `emu/floor-rbg`, `host/floor-rbg` under FZX_EVIDENCE.
The earlier `emu/floor-rbg-first` is a short integration diagnostic.
Retail-derived images, banks and traces remain private. HARDWARE is UNRUN.

## Cost and memory

Thirteen matched held views span inputs 1-685. B includes floor preparation,
remaining cloud work and the measured rotation/window upload. These are CPU
subsystem costs, not VDP1 completion or whole-frame FPS.

| CPU cost, MODELLED | Mean |
| --- | ---: |
| Prior floor + clouds A | 74.12 ms |
| RBG floor + clouds B | 56.62 ms |
| Prior floor + clouds A2 | 74.12 ms |

Mean reduction is 17.50 ms; maximum per-view A/A2 variation is
0.010 ms. Across 19 held views the new floor preparation alone
costs 12.77-15.55 ms and combined floor/cloud/upload reaches
84.53 ms. The scene budget remains blocked.

| VDP2 byte offsets | Contents | Bytes |
| --- | --- | ---: |
| 00000-00fff | Original floor characters | 4,096 |
| 20000-2005f | Rotation parameter A | 96 |
| 24000-27fff | Repeating map | 16,384 |
| 40000-5ffff | Reciprocal coefficients | 131,072 |
| 60000-6037f | Finite line window | 896 |

The new VDP2 data totals 152,544 bytes. Existing NBG3 font/map at 7c000/7e000
and backcolor at 7fffe are preserved. High-RAM end is 0x060fc4f4. The original
VDP1 floor atlas is retained for this bounded experiment; total VDP1 textures
remain 433,728/442,336 bytes. Removing that unused half-bank is separate work.
No retail pixel bank was added to program RAM.

Next candidates are reducing repeated fog clipping, a hardware or simpler native
cloud path, and larger source-derived road changes. Their savings remain unmeasured.
