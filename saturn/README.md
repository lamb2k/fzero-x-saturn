# Native Saturn backend

This target compiles the shared fixed-point driving and course renderer into a
Saturn application. The SH-2 transforms and clips geometry with SH2ZAM, then
VDP1 draws the resulting flat-colored polygons. VDP2 displays the HUD. The
default scene is the showcase's procedural demo course.

The default build is a synthetic engineering fixture. `FZX_SIM=orig` below runs
the original one-racer simulation and race loop with temporary presentation.
The accepted port must preserve original models, textures, scenery, effects,
HUD/fonts, menus and audio through local native conversion. The current fixture
art is not the finished game; see the [port plan](../docs/POC_PLAN.md).

## Build

Use the Yaul SDK and its bare-metal `sh2eb-elf` compiler:

```sh
git submodule update --init --recursive
make -C saturn
```

The repository's cross build uses this pinned Yaul image. From the repository
root, an equivalent local build is:

```sh
docker run --rm -v "$PWD:/work" -w /work \
  ijacquez/yaul@sha256:b6d272204767cd0a734139dd3a2ab298ef5912fb7b7224b78e6d9fac1a7825a6 \
  /bin/bash -c 'export PATH="$YAUL_INSTALL_ROOT/bin:$PATH"; make -C saturn'
```

Outputs are `fzero-x-saturn.iso`, `fzero-x-saturn.cue`, and the executable and
symbol map under `build/`. Keep the ISO and CUE together when loading the CUE
in a Saturn emulator or a compatible disc setup.

`SH2ZAM_ROOT` defaults to `../third_party/SH2ZAM`, relative to the `saturn`
directory. Override it when using another checked-out SH2ZAM revision.

### Imported course

The ordinary build always selects the procedural demo, even when a generated
course header exists. After using the repository's importer to create
`generated/course.h` from your own supported game data, explicitly select it:

```sh
make -C saturn clean
make -C saturn FZX_IMPORTED=1
```

This defines `FZX_USE_IMPORTED_COURSE=1` for the shared core. The build stops
with a clear error if the generated header is absent. Clean when switching
between the demo and imported modes because compiler-flag changes alone do
not make Make rebuild every object. The HUD identifies the selected course
as `DEMO COURSE` or `IMPORTED COURSE`.

### Original race code (`FZX_SIM=orig`)

```sh
tools/saturn_orig.sh            # -> saturn/build-orig/fzero-x-saturn.iso
```

The decompilation's race code (the same closure `ref/sat/` proves STRICT
against the host reference) is compiled for the master SH-2 with the bit-exact
float library and linked into the game image. Each 1/60 s tick the frontend
maps the pad to the original N64 inputs and calls `ref_tick`; the renderer
then draws from the craft's own position and attitude (`fzx_set_pose`). The
HUD says `ORIGINAL RACE CODE (SOFT FLOAT)`. With no argument the course is the
synthetic CI fixture; no ROM is read. Objects and outputs live in
`saturn/build-orig/`, apart from the proto build. The default stays
`FZX_SIM=proto` until the owner decides on soft-float (docs/POC_PLAN.md 4.2).

| Saturn pad | Original input |
|---|---|
| A or C | A, accelerate |
| B | B, boost |
| Y | C-down, brake |
| L / R | Z / R, lean |
| D-pad | Stick, full deflection |
| Start | Retry: back to the grid (`ref_race_restart`) |

The race HUD shows speed, lap of 3, race time and energy (as a share of the
start), and says when the craft has retired or finished. Start restarts with
the racer steps of Race_Init on the course already set up (Course_Init takes
seconds on the SH-2); `ref/check.sh` proves a restarted race replays exactly as
a fresh one.

## Controls

| Saturn controller, port 1 | Action |
|---|---|
| D-pad left / right | Steer |
| A or C | Accelerate |
| B | Brake |
| Start | Reset on the button-down edge |
| X | Clear the timing line's worst-case figures |
| X + Z (held) | Overload control: burns 1.25 fields of CPU each frame |

Row 5 is the timing line for hardware runs (`docs/HARDWARE_TEST.md`):
`CPU` (this frame's CPU work), `MAX` (worst since the last clear) and `SIM`
(worst race ticks in one frame), all in hundredths of a video field, measured
with the master SH-2's free-running timer against the field itself; `MISS`
counts frames that took more than one field. Holding X + Z must make `MISS`
climb: a counter that cannot trip proves nothing. The same figures are in the
image's `fzx_timing` for a debugger (`tools/emu/orig_drive.ps1` prints them).

The display is 320 × 224. The core advances in 1/60-second ticks using a VBlank
counter, with nominal 60 Hz NTSC / 50 Hz PAL conversion. Catch-up is bounded to
five video fields after a long stall. This preserves normal PAL/NTSC driving
speed; sustained overload or long pauses can slow simulated time.

## Rendering contract

- The shared core provides screen coordinates in absolute pixels and polygons
  sorted from far to near. The native local coordinate is `(0, 0)`.
- Colors are RGB1555: bit 15 set, red in bits 0–4, green in 5–9 and blue in
  10–14. A clipped triangle uses the core's repeated-vertex convention.
- Two static, aligned command arrays each hold the system clip, local
  coordinate, up to 512 polygons and the draw-end command: 16,480 bytes each
  in work RAM.
- The backend preserves the core's order and transfers only the active
  commands. Two command buffers alternate: the CPU builds frame N + 1 while
  VDP1 draws frame N, and a buffer is reused only after the sync that sent it
  completes. Variable-interval presentation avoids swapping an unfinished
  polygon frame.
- VDP1 uses priority 6. Yaul's debug console uses NBG3 at priority 7. The
  framebuffer clears to transparent so the VDP2 back-screen color shows.
- The second SH-2 is not scheduled by this target. No speedup or frame-rate
  claim follows from successful compilation.

## Validation status

SDK calls and synchronization follow Yaul's public
[VDP1 drawing](https://github.com/yaul-org/libyaul-examples/tree/develop/vdp1-drawing)
and [VDP1 mesh](https://github.com/yaul-org/libyaul-examples/tree/develop/vdp1-mesh)
examples. Native cross compilation and disc-image creation are separate checks
from emulator or console execution. Until a boot capture is recorded, this
target must not be described as boot-tested or hardware-qualified.

When testing, check a complete circuit, near-camera clipping, controller
disconnect/reconnect, repeated resets, HUD visibility and a dense view. Record
the commit, generated-image hash, emulator or hardware setup, and observed
frame rate before making performance comparisons.
