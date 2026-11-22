# Closest-point search stop (SH2ZAM vehicle-dynamics-6)

The player's physics (`sh2z_vd_step`, `sh2z_vd_step2`) finds the road under the machine with a closest-point search of up to
99 passes. Next to the line it converges in 1 to 3 passes. A machine in the air over a gap, hundreds of units off the line,
has no closest point: Fire Field's big jump (380 units) ran all 99 passes on the 4 to 5 ticks of each lap's jump, about
20 ms a tick on the master, and the field missed its 30 fps floor (6 intervals of 3F or more, critical maximum 68.59 ms,
BACKLOG 33; 5 intervals and 66.57 ms on engine `3910ea0` plus the course data).

## The stop

`ENG_LOCATE_FAR` (default 128) and `ENG_LOCATE_FAR_RUN` (default 8) in `saturn/engine/Makefile` pass
`SH2Z_VD_LOCATE_FAR` / `SH2Z_VD_LOCATE_FAR_RUN` to SH2ZAM: a search whose residual stays above 128 units for 8 passes in a
row stops with the state it has. On for every course (E1 drive images). A search that converges, or overshoots and comes
back, is never stopped.

- `ENG_LOCATE_FAR=0` builds the earlier search (the cost control); `ENG_LOCATE_FAR=3 ENG_LOCATE_FAR_RUN=1` is the stop that
  changes a normal course (the control that must fail).
- The HOST reference runs the same stop: `tools/physics_host/build.sh` builds `fzx-phys` with it by default (`LOCATE_FAR=0`
  for the earlier search, `LOCATE_COUNT=1` for a build that prints `locate_stops N` on stderr, the searches the stop ended).
- Approximation, ruled by the coordinator (BACKLOG 33): the stopped state is one point of the circle the search was running
  round, as the 99-pass state is, but not the same one. Outcome is what is checked (below).

## Evidence (2026-11-22; HOST and EMULATOR, Ymir timing MODELLED; no console run)

HOST, `fzx-phys` over the 24 courses, stop on against off (`locate_count`):

- 23 courses: 0 stops, every tick of the trace and every 256-tick checkpoint equal, finish equal to the millisecond (Port Town
  1 and Big Hand retire at the same tick).
- Fire Field: 25 stops, finish 100,490 ms against 100,521 (-0.03%); lap starts 0 to 3 ticks earlier.
- 63 driver settings on Fire Field, 22 of which finish with the earlier search: all 22 finish within 0.5% with the stop.
- Control (FAR 3, RUN 1): 993 to 11,999 stops on every other course and every trace changes.

EMULATOR, gate `--only course` (Fire Field, scenes image with the R01-t driver), engine `3910ea0` + `claude/courses-kj` data:

| Arm | Finish vs HOST | Checkpoints | Intervals 1F/2F/3F+ | Critical mean / p99 / max ms |
| --- | --- | --- | --- | --- |
| stop off (`ENG_LOCATE_FAR=0`) | 100,521 (HOST with the stop 100,490) | 4/26 (the stop is the difference) | 0/3143/5, worst 4F | 18.61 / 23.72 / 66.57 |
| stop on (default) | 100,490 = HOST | 26/26 equal | 0/3151/0, worst 2F | 18.52 / 23.46 / 31.17 |
| Mute City 1, stop on | 104,040 = HOST | 27/27 equal | 0/3258/0 | 19.82 / 23.42 / 24.89 |
| Mute City 1, FAR 3 RUN 1 (control) | 22,366 against HOST 104,040: FAIL as it must | 1/7 | | |

Critical p99 x 1.36 = 31.90 ms on Fire Field (limit 33.3). Master maximum 27.41 ms (was 65.83). The 31.17 ms maximum leaves
2.1 ms to the 33.3 ms line (which frame it is was not traced).

Reproduce: `claude/fire-field` with the course data of `claude/courses-kj`, `python tools/engine_gate.py --full --only course
--courses fire-field --base "" --stage claude-gate-<name>` (the arms are matrices with the image's make variables;
`ENG_LOCATE_FAR=0` for the earlier search).
