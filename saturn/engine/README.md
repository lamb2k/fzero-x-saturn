# Engine image (E0)

The hybrid Saturn engine image: `make -C saturn/engine`. It links no original game code; see docs/ENGINE_PLAN.md.
Build switches and their defaults are in the Makefile (`E0_*`). Legacy builds never read them.

## Owner decisions

- **Road LOD shift 2 (2026-11-19, "go with shift 2").** `E0_LOD_SHIFT=2` (`FZX_ROAD_STREAM_LOD_SHIFT=2`) is the engine default: coarse road tiers start 4x nearer, so near road has fewer, longer pieces. Legacy builds keep 0 and stay byte-identical. All engine measurements from 2026-11-19 use shift 2.
- **Visible chunk cap 32 (2026-11-19, "cap 32").** `E0_CHUNK_CAP=32` is the engine default: past 32 visible chunks the pick drops its farthest runs. A distant curve drops out at the two densest views of each lap (ticks ~1895 and ~4100 on the route); the owner accepted the look. Legacy builds are unaffected.
- **Stable chunk cap 26 (2026-11-19, "Yes stable cap 26 should be the default").** `E0_CAP_STABLE=1`, `E0_CHUNK_CAP=26`: the pick's chunks are ranked by depth and the nearest 26 kept, with hysteresis (drop at rank 28 or farther; return after 4 frames inside rank 24). Replaces the run-dropping cap 32. Slave p99 21.8 ms, 33 of 4,809 rows over 22 ms; chunk pops over 3 laps 896 against 2,932 at the old cap.

## Track build switches (2026-11-19, Ymir modelled timing, 3-lap route)

| Switch | Default | Result |
| --- | --- | --- |
| `E0_WORK_HIGH` | 1 | Track tables in high work RAM. Slave mean 20.1 -> 19.9 ms (Ymir models little DRAM/SDRAM difference). |
| `E0_CLUSTERS` | 0 | Baked cluster bounds (tools/road_cluster_bake.py, ROADCL.PAK, SH2ZAM segmesh-2). Net loss: slave mean +1.7 ms, p99 +3 ms; the audit build counted 0.08 skipped quads per frame that would have drawn. Kept off. |
| `E0_CHUNK_CAP` | 26 (stable) | Drops the farthest pick runs beyond N chunks. Cap 32: slave p99 35.9 -> 29.1 ms, 0 frames over two fields; a distant curve drops out at ticks 1895 and 4100 (owner approved). |
| `E0_LEAN_CLIP` | 1 | Road pieces and quads clip at the near plane in 3D, then in 2D on the screen corners (SH-2 divider, only the sides crossed). Slave p99 29.1 -> 24.3 ms, rows over 22 ms 861 -> 178. Clipped near pieces' textures shift by a texel or so; no seams or holes (look sheet in the E0 evidence). |
| `ENG_DSP_BANDS` | 1 | DSP lane (src/fzx_band_corners.h). Floor and cloud fog-band rail points: the per-band corners only when `band_shared` fails (they were computed for every band), and `band_edge`'s points. 1: 32-bit deltas on the SH-2; 2: the products on the SCU DSP (SH2ZAM dsp-1 scale16). Bit-identical (whole-race CHECK 0 of 1,649,328; HOST 0 of 41,082,624; pixel hash, ticks and HOST checkpoints equal). Field master p99 27.3 -> 27.0 ms with either arm; the DSP adds nothing over arm 1, so 1 is the default (coordinator 2026-11-19) and 2 stays off. `ENG_DSP_BANDS_CHECK`/`_CORRUPT`: the diagnostic and its control. |
| `E0_FOG_EARLY` | 1 | Exact: pieces whose corners are all before the first fog threshold reuse one shading result per strip image per frame. Stills byte-identical at 9 route ticks; slave p99 24.3 -> 23.8 ms. |
