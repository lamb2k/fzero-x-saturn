# Console test

An emulator run is not hardware timing. Yaba Sanshiro does not model the SH-2
cache or work-RAM wait states accurately, so every figure from it is MODELLED.
This sheet is the protocol for the first run on a real Saturn.

The protocol and figures below describe the earlier prototype renderer. They do
not certify the current original-art scene. Its legacy CPU/MAX timer wraps after
65,536 FRT ticks (roughly 78 ms), so a small displayed percentage can hide a long
frame. Use the [whole-scene recorder](FRAME_PERFORMANCE.md) for current cadence;
the old HUD is not evidence of 30 or 60 fps on this scene.

## What the image shows

Row 5 of the HUD is the timing line (see `saturn/README.md`, Controls):

```
 CPU 074 MAX 099 SIM 040 /100 MISS 0000
```

- `CPU`: this frame's CPU work (race ticks, render, VDP1 command build), in
  hundredths of a video field.
- `MAX`: the worst `CPU` since the last clear (press X).
- `SIM`: the worst race-tick time in one frame.
- `MISS`: frames that took more than one field since the last clear. Zero
  means a steady 60 frames per second (50 on PAL).

All figures come from the master SH-2's free-running timer, measured against
the video field itself, so they hold for NTSC, PAL and either dot clock.

## Setup record

- Console model, region and video standard; display and capture chain.
- Loader: optical drive emulator, flash or dev cart, or burned disc, with
  its firmware.
- Controller.
- Branch, commit, `sha256sum` of the image, and whether it was built with
  `--rom` (that image stays private: it carries the owner's course data).

## Run

1. Cold boot. Note the time from boot to the course on screen.
2. Press X on the grid to clear, wait 10 seconds: `MISS` must stay 0.
3. Drive a full lap with A, steering with the D-pad. Note `MAX`, `SIM` and
   `MISS` at the end (photograph the screen).
4. Boost with B until energy runs out; note `MISS`.
5. Negative control: hold X + Z for 3 seconds. `MISS` must climb by about 90.
   If it does not, the counters are not working and the run proves nothing.
6. Repeat from a second cold boot.

Pass: `MISS` 0 over the lap, `MAX` under 85 (15% headroom), negative control
trips. Anything else is a failure to record with its figures, not a pass.

## Historical prototype figures (MODELLED, not the current original-art scene)

Per-frame CPU 69-84% of a field in normal driving; zero missed fields through
start, throttle and steering; 4 missed fields when energy ran out (the original
code's tick at 40% of a field plus render, 137% in that one frame); the
negative control tripped (+40 misses in 3 s).
