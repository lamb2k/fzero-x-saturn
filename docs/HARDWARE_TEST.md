# Full-loop Saturn disc test

**P0 status: awaiting a physical run.** Use the P0 full-loop disc with six Jack courses, menus, music, effects and the cadence overlay. Use its matching CUE and all referenced tracks. Keep the E0 soak disc as a separate test.

**Run identity** — Date ______ Console model/region ______ Video mode 60 Hz ______ Loader/firmware ______ 4 MB expansion ______ Controller ______ Build commit ______ SHA256SUMS manifest ______ Display/capture ______

## Next console session

All five checks start **HARDWARE UNRUN**. For each, write the disc label and manifest hash beside the result. Use the build flags supplied with that disc. If a required build is missing, mark the check UNRUN and continue with the available discs.

| Check and disc | What to do | What to look for | What to record |
|---|---|---|---|
| 1. Records save, load and reboot. Use the memory4 quiet-section player build with sound compiled off (`ENG_SND=0`). | With the pad connected, cold boot, set a qualifying time and save distinctive initials. Once saving has finished, reopen RECORDS, reboot the console, reopen RECORDS and navigate with the pad. Repeat the boot/load to exercise a different timing window. | The fix holds off the vertical-blank INTBACK controller poll while the main thread issues backup-library SMPC commands. TITLE should appear, each save/load should finish, pad input should resume and the same course, initials and time should survive reboot. | Save medium, initials/course/time before and after reboot, successful boots/attempts and pad response. For a stall, record the last visible screen or indicator, elapsed wait and which operation was in progress. A freeze alone does not locate the SMPC command. |
| 2. Black to first TITLE. Use the scenes boot-sync build. | Start continuous video before cold boot. Repeat after a console reset. For each, mark the start of the game's black screen after the loader and the first field containing any TITLE artwork. | The submitted black frame should give way to TITLE cleanly. Watch for an extended blank, a flash or an old frame. Time the whole black interval, including the sync wait before the menu counter starts. | Cold boot and reset separately: capture rate, start/end timestamps or field numbers, elapsed time and clip reference. If the loader-to-game boundary is unclear, flag it. Take menu WORST/OVER2 separately. |
| 3. Playable guests. Use a guest-enabled player disc. | From SELECT drive Dr. Robotnik in the classic Eggmobile, then Metal Sonic, on the same course. For each, start from the grid, steer through a turn and reach RESULTS. | The selected portrait, racer on the grid and in the turn, and RESULTS identity should agree. Both should respond to the pad. | One row per guest: selected name, course, whether the race started, turn/input result, finish or retirement, and photos of SELECT, the race and RESULTS. Note the first screen where an identity differs. |
| 4. Uppercase title. Use the main title disc and the comic title disc (or the supplied variant selector). | For each title, boot with the pad connected, capture the title before the prompt, wait for PUSH START and press Start to enter SELECT. On a separate boot, start with the pad disconnected and capture the red warning. | Both titles should read **F ZERO S**. Main shows both guests. Comic shows the original panels without guest overlays; its red warning belongs in the original lower position. Check lettering and prompts on the console display. | Variant and disc hash, three labelled stills per variant (pre-prompt, PUSH START, disconnected), whether Start entered SELECT, and any cropping, overlap or unreadable text. |
| 5. Sand Ocean balanced rails. Pending the line-start decision. | When the coordinator identifies the road image to test, drive Sand Ocean through a bend and the pipe approach. If two line-start settings are supplied, use the same machine, camera and route on each. | Follow both road edges through the bend. Look for gaps, popping, uneven rail weight or a change in road texture or shading. Judge whether the rails make the drivable edge clear. | Road build and line-start setting from its manifest, lap/location, matching stills or clips, and race WORST/OVER2 before and after the section. Record the owner's look verdict for the tested setting. Leave this row pending until the configuration is named. |

## Full-loop route

1. Cold boot with sound on. Record time to title. Press Start, browse the six course names, choose Mute City and confirm a machine. Record time from confirmation to a playable grid. Check music during loading and the transition into the race.
2. Drive to results. Note steering, grip, boost response, picture and sound. Photograph the race counter during play and both counter blocks at results. Enter a name for a qualifying time. Retry once; verify the same course and machine, music restart and responsive input.
3. Return to title. Run Sand Ocean or Big Blue and record the same observations on a tube section. Pause and resume, then try pause/retry after music has played. Test the remaining courses in the table; distinguish a completed race, a retirement and a course only booted.
4. Cancel a load near the start, middle and end with A+B+C+Start. Release the chord after each return to title. Start another race and check disc reads, music and input. Exercise soft reset from title, select, race, results and records. Record the scene and what happened if the return or next load fails.
5. Reboot after the save indicator has settled. Open records and verify the saved time/name under its original course. If this disc exposes Jack GP, check the next-round load with music active and record the two courses. Mark GP unavailable if gated.
6. On a guest-enabled disc, select Dr. Robotnik and Metal Sonic in turn. Check each portrait on SELECT, the Eggmobile or Metal Sonic model on the grid and during turns, and the machine name on RESULTS. For a saved finish, check the same name and time in RECORDS after reboot.

| Course | Outcome / time | Race WORST / OVER2 | Menu WORST / OVER2 | Music, picture, pad or fault |
|---|---|---|---|---|
| Mute City | | | | |
| Silence | | | | |
| Sand Ocean (pipes) | | | | |
| Devil's Forest | | | | |
| Big Blue (cylinders) | | | | |
| Port Town | | | | |

**Reading the overlay.** RACE FPS covers the latest race second. MEAN and the race/menu GAPS counts accumulate across measured intervals since power-on. GAPS lists 1, 2, 3 and 4-or-more fields. Record both blocks before and after each route, including the race number and elapsed time. For the locked loop, new frames should arrive every two fields. Record any rise in OVER2 and its location even if MEAN still reads 30. If a counter is absent or does not advance, record the observation and leave cadence unverified.

**E0 soak.** Boot the separate E0 disc, let the flythrough loop for ten minutes, and photograph the counters at start and finish. Record visual corruption, lockups, blank frames, or a rising OVER2 count. Keep E0 evidence separate from the full-loop run.

**Owner verdict** — Pad/handling ______ Course and vehicle appearance ______ Cadence ______ Save/reboot ______ Sound and transitions ______ First reproducible issue (disc, course, action, timestamp) ______

The same disc run in Mednafen or Ymir gets a separate emulator row with its own hashes and settings. Hardware acceptance uses the physical console observations above.

---

## Archived prototype sheet

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
