# Handoff

Limit: 50 lines. Replace this file each session; do not append.

## Last session (2026-11-15), branch `gate1`

- `FZX_SIM=orig` image runs the decomp race code on the master SH-2 (soft float,
  bit-exact). With the owner's ROM (`tools/saturn_orig.sh --rom`) it races Mute City 1.
- Whole 3-lap race on the emulator equals the host reference: finish time identical to
  the ms over 10,066 ticks (`tools/emu/race_log.ps1` + `tools/race_replay.py`).
- 60 fps on Mute City (EMULATOR, MODELLED): renderer windowed/classified/divider,
  pipelined VDP1 loop. Full race: 9,455 frames, 38 missed fields, worst 134% of a field.
- HUD timing line (CPU/MAX/SIM/MISS, X clear, X+Z overload control); console protocol
  in [HARDWARE_TEST.md](HARDWARE_TEST.md). Race HUD; Start retries (`ref_race_restart`,
  proven in `ref/check.sh`). Bridge fixes: `gTotalLapCount = 3`, CPU line storage.

## Next cards (in order)

1. **The 38 hitches.** Log the tick of each missed field with racer flags (wall hit,
   pit, lap line, fall-off; a fall-off tick costs about 4 fields) and cut those costs.
2. **Course setup 18.5 s** (1,108 fields). Host callgrind: chunk build 87%, spline
   arc length 27%, Taylor sine table 15%. Faster `src/sh2float` (SH-2 asm fadd/fmul),
   keeping `tests/test_fzx_float.c` and the 10 STRICT probes green.
3. **CPU racing line** for the run-out lap: the hooks skip its ROM read (0x400 bytes
   per course, `D_i3_8013DB80`). Extract locally like the boot area; never commit.
4. **Console run** on the owner's flash cart per HARDWARE_TEST.md.
5. Re-run the 10 SH-2 probes after closure changes (not re-run since 1a2daa2; host
   hashes unchanged and the whole-race replay matches).

## Boundaries

- Parity means behaviour: physics, rules, timing, controls. Visuals stay original
  placeholders. Do not build in the game's art or look (machine models, track
  textures or scenery, logos, HUD graphics, music), even extracted from the ROM.
- ROM-derived data (course record, boot area, logs of ROM-course runs, `--rom`
  images) stays in git-ignored folders and local evidence. Never commit or upload it.
- Soft-float vs POC_PLAN 4.2 is still the owner's decision; CI default stays `proto`.

## Notes for the next agent

- Emulator: `FZX_YABA_EXE` (path in agent memory). Scripts edit and restore the shared
  `yabause.ini`; stop the emulator before rebuilding. Another project may run its own
  Yaba on a different port: leave it alone.
- Symbols under LTO may carry `.lto_priv.N`; the scripts accept that.
- Debugger hooks in the image: `fzx_debug_n64`, `fzx_debug_autopilot` (look-ahead |
  coast << 8; 0x2803 finishes Mute City), `fzx_debug_restart`, `fzx_debug_overload`,
  `fzx_timing`, `fzx_input_log`, `fzx_init_fields`.
- From Git Bash: `MSYS_NO_PATHCONV=1 wsl -d Ubuntu-24.04 -- bash script.sh`. Heredocs
  eat backslashes: write C escapes with the Write tool or `chr(92)`.
- MCP PC sampling lands mostly in the vblank wait; time phases with the FRT instead.
