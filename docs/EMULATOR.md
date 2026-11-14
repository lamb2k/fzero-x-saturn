# Emulator lane (card G1.5)

Evidence tier: **EMULATOR**. Any frame-cost number read from it is **MODELLED**. A boot here is
not a hardware claim.

## Chosen emulator

Yabause 0.9.15, the Ubuntu 24.04 `yabause-qt` package, with its emulated (HLE) BIOS. No BIOS
file is needed, so it runs headless in the agent container and in CI. Software renderer under
Xvfb. Each run gets a fresh settings home (one instance per run, no shared state).

## Command

```sh
sudo apt-get install -y yabause-qt xvfb xdotool imagemagick
python3 tools/emu/yabause_run.py saturn/fzero-x-saturn.cue --seconds 10 --out still.png \
    [--cart 4mb|1mb|none] [--keys "4:+z 4.2:+Left 9:-z"]
```

It boots the image, waits, writes a 320x224 still of the viewport and prints
`emu: PASS ...` with exit 0 when the pass marker is seen: the image is running and its HUD is on
screen. CI's `emulator` job runs it with and without the 4 MB cartridge and uploads both stills.

## Image HUD readouts (debug console, row 5 and 6)

- `FLD`: video fields since boot. `TCK`: simulation ticks. `PIC`: finished pictures.
  At 60 fps `PIC` advances with `FLD`; `FLD - PIC` is the missed-field count.
- `PAD`: raw digital pad bits, `FFFF` when no pad is connected.
- `CYC/FLD`: SH-2 cycles per field from the free-running timer (clock/128). Yabause gives
  448,384 against the nominal 477,750: FLD counts real fields.
- `CPU/PIC`, `R`, `B`: cycles per picture in step + render + command build, in `fzx_render`, and
  in the VDP1 command build.
- Probe knobs (`make -C saturn ...`, defaults are the original behaviour): `FZX_HUD_EVERY`,
  `FZX_SYNC_WAIT` (2 or 1), `FZX_VDP1_INTERVAL` (-1 or 0). `--realtime` in the run script turns
  on Yabause's limiter, which does not hold headless (about 167 fields per wall second).

## Known limits

- **No RAM cartridge is actually present in this emulator.** With `Cartridge/Type` 7 (4 MB) or
  6 (1 MB) the cartridge ID byte reads `FF` and a write/read probe of cartridge RAM at
  0x22400000 fails (bench image, 2026-11-14). Earlier "boots with and without the 4 MB cart"
  meant only that the setting was passed. Cartridge timing and detection need another emulator
  or the console.

- **Pad input does not reach the image in this emulator.** The image reports `PAD FFFF` (no
  pad connected) with an explicit port-1 pad mapping, focused XTEST key events and a click into
  the view. Two hypotheses tested; the lane is `BLOCKED_EMULATOR_PAD`. Unblock: an emulator whose
  SMPC peripheral reports work with Yaul (candidates: Kronos built from source with HLE BIOS, or
  Mednafen with an owner-supplied BIOS run by the owner), or diagnose Yaul INTBACK under Yabause.
  Booting, rendering and HUD checks are unaffected.
- `ClockSync=false`: emulated time runs faster than wall time. Use `FLD`, not seconds.
- Yabause 0.9.15 does not model cartridge access cost or exact VDP1 timing. Frame-rate numbers
  from it are MODELLED and do not replace the hardware sheet.
