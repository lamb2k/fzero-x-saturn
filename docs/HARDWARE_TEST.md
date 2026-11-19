# E0 Saturn flythrough test

Use this sheet with the private E0 disc. The camera follows the racing line around the first course. The console run measures presented cadence while both SH-2s share the Saturn bus.

## Build and package the private disc

1. Take the full game commit and SH2ZAM pin from the E0 handoff. Create a clean detached worktree at that commit and verify it belongs to the published engine history. From the repository run the commands below with the actual commit and a fresh local destination.

   ```sh
   set -eu
   git fetch origin refs/heads/engine:refs/remotes/origin/engine
   git merge-base --is-ancestor "$E0_COMMIT" origin/engine
   git worktree add --detach "$E0_BUILD_DIR" "$E0_COMMIT"
   cd "$E0_BUILD_DIR"
   git submodule update --init --recursive
   git rev-parse HEAD
   git submodule status --recursive
   ```

2. In the pinned Yaul environment, run the asset bake and engine-disc command from this commitâ€™s E0 build record using the ownerâ€™s private ROM. Verify the SH2ZAM commit actually linked by the engine. The legacy submodule and the engine library may have different pins. Retain the compiler version, build options, pack receipts, link map, BIN and loader-ready disc files. Verify the map identifies the engine entry point and contains no original race closure.
3. From the private build output directory, produce the image manifest using the actual relative filenames. Include the game BIN, disc descriptor and every referenced track. If the image is a standalone ISO, include the BIN and ISO. Preserve the descriptorâ€™s relative track paths when copying the disc. In the loader staging directory verify the same file set against the manifest. Record the SHA-256 and any existing Ymir packaging result from the E0 handoff.

   ```sh
   # Assign these variables the relative names from the E0 build record.
   # For a cue with more tracks, append every track to the same command.
   set -- "$E0_BIN" "$E0_CUE" "$E0_TRACK"
   # For a standalone ISO instead, use: set -- "$E0_BIN" "$E0_ISO"
   sha256sum -- "$@" > SHA256SUMS
   # In the copy staged for the loader, with the same manifest and filenames.
   sha256sum --check SHA256SUMS
   ```
4. The engine branch point `908c9a6` contains the workflow documents. The executable build command, toolchain pin, boot filenames and image hashes are pending the first runnable E0 commit. Enter the tested values below when packaging it.

| Build record | Value |
| --- | --- |
| Engine commit / SH2ZAM pin / toolchain | |
| Private pack receipt / exact build invocation | |
| BIN and disc filenames / SHA-256 | |
| Loader payload checksum verification / date | |
| Ymir boot or equivalent packaging evidence | |

## Owner run sheet

Use a nominal 60 Hz mode for the two-field gate. Record a 50 Hz run separately. The counter must count newly displayed game frames and carry the worst field gap and count over two across the soak, or supply a field-resolved capture for those numbers.

| Setup | Record |
| --- | --- |
| Console model, region, video mode and date | |
| Loader and firmware, display or capture device | |
| 4 MB RAM cartridge model, cartridge-detect text or photo | |
| Disc hash or build record above | |

1. Cold boot. Photograph the cartridge-detect screen and copy its exact result. Record time to the first visible road. If boot or detection fails, record the last screen and elapsed time.
2. Start a ten-minute continuous soak at the first moving flythrough frame. Record the on-screen **presented frames per second** at each minute. Save a clip of a bend and any fault. For tearing, flicker, missing road or a freeze, note time, course landmark, duration and whether it repeats on the next pass.
3. At the end photograph the counters. If they are cumulative since boot, record the start and end field/frame counts and use measurements covering just the soak. Record duration, presented-frame count, mean fps, p99 and worst interval in fields, and intervals over two. Record any reboot or interruption. If a counter is absent, identify the missing measurement.

| Minute | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Presented fps | | | | | | | | | | |

| Result | Record |
| --- | --- |
| Boot time / completed soak duration | |
| Frames / mean fps / p99 fields / worst fields / count over two | |
| Tearing, flicker, missing road, hangs with time and landmark | |
| Photos or clip / owner's look decision | |

The E0 cadence gate passes with at most two fields between new frames throughout the measured flythrough. Sampled FPS alone leaves that gate open. E2 measures the full 30-racer field.

<details>
<summary>Archived prototype console procedure</summary>


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

</details>
