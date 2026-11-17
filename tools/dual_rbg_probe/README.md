# C5 standalone dual rotation probe

Synthetic art only. SH-2 build, HOST coordinate checks and all seven Ymir
capture/control checks pass. Yaba has not executed the probe successfully;
hardware execution is **UNRUN**. This tests whether both rotation
layers can consume the same per-dot reciprocal table. It does not integrate
clouds, establish game appearance, free existing atlases, or measure game cost.
Debug text and all NBG/VDP1 layers are off. The picture is static for repeatable
captures; there is no pad interaction or game simulation.

Build all seven isolated images from WSL Ubuntu-24.04:

```sh
bash tools/dual_rbg_probe/build-all.sh
```

Or build one with `SATURN_DIR=tools/dual_rbg_probe tools/saturn_build.sh
PROBE_MODE=0`. Outputs are `dual-rbg-N.iso`/`.cue`, `build-N/`, `cd-N/` inside
this directory. No emulator is started and no emulator settings are changed.
The pinned SDK emits its usual RWX-load-segment link warnings.

## Recorded emulator scope

The existing local Ymir core, software renderer and cache emulation ran each
image for 1,200 fields from a fresh virtual machine. Actual VRAM/CRAM/register
audits pass in all seven modes. RGB5-normalized captures match the coordinate
model at every pixel. Missing-layer, wrong-B-geometry and independent A/B
coefficient controls all visibly differ; their pixel counts match the HOST
predictions below. This is EMULATOR evidence for this static synthetic probe.
The core library and capture executable are hash-bound in the private receipt;
the old checkout's Git metadata was unavailable, so its folder label is not
treated as a verified source revision.

Yaba's hidden launch reported running but stayed at frame zero and the boot
entry PC, with the probe ready word still zero. A framebuffer burst also made
no progress. These are startup failures, not evidence of a VDP2 discrepancy.
Independent-emulator qualification remains incomplete. Private evidence:
`emu/dual-rbg-probe/ymir/receipt.json`, `image-comparison.json`, `yaba-debug/`
and `yaba-step/`. No game renderer or memory layout has changed.

## Memory and registers

| Bank / address | Contents | Bytes |
|---|---|---:|
| A0 `25e00000` | RBG0 512x256 CLUT4 cyan/blue checker bitmap | 65,536 |
| A1 `25e20000` | Shared 32-bit reciprocal table, `k[i]=floor(256*65536/(i+128))` | 131,072 |
| B0 `25e40000` | RBG1 four synthetic 8x8 CLUT4 cells | 128 |
| B1 `25e60000` | RBG1 64x64 cell map; all 16 rotation-B planes repeat it | 16,384 |
| B1 `25e64000`, `25e64080` | Parameter A, parameter B, each 96 bytes in a 128-byte slot | 256 reserved |
| B1 `25e7fffe` | Back-screen color | 2 |
| CRAM `25f00000` | Two 16-color palettes | 64 |

Mode 0 uses `RAMCTL=1307`: CRAM mode 1, both VRAM halves split, A0 designated
RBG0 bitmap, A1 designated coefficients, B0/B1 ownership bits both zero as
required for RBG1. It deliberately does not reuse the current game's floor
layout. Rotation VRAM reads use these bank designations, not NBG cycle slots.

`BGON=0030` enables the two real layers with transparent palette index zero.
`CHCTLB=0200` selects RBG0 bitmap; `CHCTLA=0000`, `PNCN0=0000` select RBG1
8x8 cells / two-word pattern names. RBG1 uses NBG0's format/priority registers,
but **rotation-B** map and parameter registers. `MPABRB..MPOPRB=1818`,
`MPOFR=0000`, `PLSZ=0000`. RBG0 priority is 3; RBG1 priority is 5.

`RPMD=0` leaves RBG0 on parameter A; RBG1 always uses parameter B.
`RPTA=00032000` addresses byte offset `64000`; B is `+80` bytes, not `+60`.
`RPRCTL=0` allows Xst/Yst/KAst to accumulate between lines. `KTCTL=0101`
enables two-word kx/ky coefficients for A and B. `KTAOF=0000`; the uploaded
KAst integer includes `8000` entries to reach A1, plus the initial sample index.
This bypasses Yaul's address-descriptor versus uploaded-KAst ambiguity and its
lack of an RBG1 cell-format enum. Two RBG0 parameter alternatives are not
treated as two composited layers.

Both tables use the same KAst/DeltaKAst/DeltaKAx. The quantized rolled basis is
`s=0.25`, `c=0.96875` (about 14.5 degrees), with a nonzero per-dot DeltaKAx.
For centered screen coordinates `X=x-160`, `Y=y-112`, the denominator samples
`i=floor(256+s*X+c*Y)`; the visible range is 107..403. Layer B has different
scale, constant-depth numerator and translation, making accidental reuse of A
detectable. The screen has diagonal cyan checker lines and orange/yellow/pink
cross/hatch patterns with transparent holes. This is a synthetic projective
plane diagnostic, not a reproduction of the source game's camera or sky.

## Controls and acceptance

| Mode | Change | Expected distinguishing result |
|---:|---|---|
| 0 | Both per-dot layers | Blue/cyan checker and orange/yellow/pink overlay |
| 1 | RBG1 disabled | All overlay colors disappear; RBG0 remains |
| 2 | B geometry replaced by A | Overlay placement/scale changes |
| 3 | Both coefficient enables off | Affine constant-scale picture |
| 4 | Roll basis zeroed consistently | Axis-aligned geometry and per-line coefficients |
| 5 | Only B coefficients off | Overlay changes; underlying A coordinates stay fixed |
| 6 | Only A coefficients off | Floor changes; B coordinates stay fixed |

HOST oracle predicts 59,697 visible floor pixels / 11,983 overlay pixels in
mode 0. Controls 1..6 differ at 11,983 / 22,584 / 68,078 / 38,184 / 19,983 /
56,286 pixels respectively. This establishes diagnostic separation in the
coordinate model; it does not establish that the VDP2 rendered those pixels.
Do not accept a mode-0 image alone. Capture modes 0,1,2,3,5,6 in Yaba and an
independent emulator or console, with matching memory/register audits. Mode 4
provides a per-line versus rolled per-dot comparison. Inspect both colors and
geometry; a constant or missing second layer must not pass.

`python tools/dual_rbg_probe/audit.py` regenerates `out/audit.json`, its SHA256
receipt and seven source-only PPM references. JSON has source/ISO hashes,
per-range byte counts, SHA256/FNV1a values and expected register words. The PPMs
are HOST mathematical references; exact hardware rounding has not been
qualified and no pixel-error tolerance is asserted. Regenerate after building
or modifying a source; do not edit generated outputs.

Runtime exports (look up addresses in each ELF/map, since modes can relocate):

- `dual_rbg_ready`: `44524247` after two completed synchronization fields.
- `dual_rbg_fields`, `dual_rbg_mode`: field count and compile-time mode.
- `dual_rbg_hashes[6]`: FNV1a of actual uploaded bitmap, coefficient, chars,
  map, 256-byte RPT reservation and 64-byte CRAM, in that order.
- `dual_rbg_registers[144]`: intended register shadow, **not** hardware readback.
- `dual_rbg_params[2]`: CPU-side parameter structs, contiguous 96-byte records.

Capture raw VDP2 VRAM, CRAM and registers as big-endian bytes; audit using:

```sh
python tools/dual_rbg_probe/audit.py --mode 0 --out /path/to/evidence/audit \
  --vram /path/to/vram.bin --cram /path/to/cram.bin --registers /path/to/regs.bin
```

The VRAM dump must be exactly 524,288 bytes; register dump at least 288 bytes.
Only explicitly configured stable registers are compared. A capture mismatch
fails the command; byte order and CPU alias conventions must be established
before interpreting one as a renderer defect. Archive exact images and captures
externally with the emulator version/backend and source/ISO hashes. All seven
ISOs and generated references are ignored locally by this directory's rules.

## Specification basis

- Sega VDP2 manual [6.1](https://www.infochunk.com/saturn/segahtml_en/hard/vdp2/hon/p06_10.htm):
  RBG1 uses parameter B; either parameter can use per-dot coefficients.
- Sega VDP2 manual [6.2](https://www.infochunk.com/saturn/segahtml_en/hard/vdp2/hon/p06_20.htm):
  both layers required, no NBG, RBG1 CPD/PND fixed to B0/B1, and dedicated
  per-dot coefficient bank constraints.
- Sega VDP2 manual [6.4](https://www.infochunk.com/saturn/segahtml_en/hard/vdp2/hon/p06_40.htm):
  two-word coefficient units, entry addressing and kx/ky selection.
- Sega VDP2 [register diagram](https://www.infochunk.com/saturn/segahtml_en/hard/vdp2/hon/p15_30.htm):
  RBG1's NBG0-format registers and rotation-B map/parameter selection.
- Installed pinned Yaul `vdp2/scrn_rotation.h`, `scrn_shared.h`, `scrn_macros.h`
  and `vdp2-rbg0-bitmap` example: ABI and helper semantics cross-check.

No third-party sources, shared application files, SH2ZAM, emulator settings or
repository history are changed by this probe's build.
