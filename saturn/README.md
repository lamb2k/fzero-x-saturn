# Native Saturn backend

This target compiles the shared fixed-point driving and course renderer into a
Saturn application. The SH-2 transforms and clips geometry with SH2ZAM, then
VDP1 draws the resulting flat-colored polygons. VDP2 displays the HUD. The
default scene is the showcase's procedural demo course.

This is an engineering slice for the F-Zero X port effort. The vehicle handling
is prototype code; the original game's vehicle physics, AI, audio and race
systems are not implemented here. See the [showcase overview](../README.md)
and [source attribution](../NOTICE.md).

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

## Controls

| Saturn controller, port 1 | Action |
|---|---|
| D-pad left / right | Steer |
| A or C | Accelerate |
| B | Brake |
| Start | Reset on the button-down edge |

The display is 320 × 224. The core advances in 1/60-second ticks using a VBlank
counter, with nominal 60 Hz NTSC / 50 Hz PAL conversion. Catch-up is bounded to
five video fields after a long stall. This preserves normal PAL/NTSC driving
speed; sustained overload or long pauses can slow simulated time.

## Rendering contract

- The shared core provides screen coordinates in absolute pixels and polygons
  sorted from far to near. The native local coordinate is `(0, 0)`.
- Colors are RGB1555: bit 15 set, red in bits 0–4, green in 5–9 and blue in
  10–14. A clipped triangle uses the core's repeated-vertex convention.
- A static, aligned command array holds the system clip, local coordinate,
  up to 512 polygons and the draw-end command: 16,480 bytes in work RAM.
- The backend preserves the core's order, transfers only the active commands,
  and waits for VDP1 completion before reusing the buffer. Variable-interval
  presentation avoids swapping an unfinished polygon frame.
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
