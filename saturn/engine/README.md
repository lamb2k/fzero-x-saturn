# Engine image (E0)

The hybrid Saturn engine image: `make -C saturn/engine`. It links no original game code; see docs/ENGINE_PLAN.md.
Build switches and their defaults are in the Makefile (`E0_*`). Legacy builds never read them.

## Owner decisions

- **Road LOD shift 2 (2026-11-19, "go with shift 2").** `E0_LOD_SHIFT=2` (`FZX_ROAD_STREAM_LOD_SHIFT=2`) is the engine default: coarse road tiers start 4x nearer, so near road has fewer, longer pieces. Legacy builds keep 0 and stay byte-identical. All engine measurements from 2026-11-19 use shift 2.
- **Visible chunk cap 32 (2026-11-19, "cap 32").** `E0_CHUNK_CAP=32` is the engine default: past 32 visible chunks the pick drops its farthest runs. A distant curve drops out at the two densest views of each lap (ticks ~1895 and ~4100 on the route); the owner accepted the look. Legacy builds are unaffected.

## Track build switches (2026-11-19, Ymir modelled timing, 3-lap route)

| Switch | Default | Result |
| --- | --- | --- |
| `E0_WORK_HIGH` | 1 | Track tables in high work RAM. Slave mean 20.1 -> 19.9 ms (Ymir models little DRAM/SDRAM difference). |
| `E0_CLUSTERS` | 0 | Baked cluster bounds (tools/road_cluster_bake.py, ROADCL.PAK, SH2ZAM segmesh-2). Net loss: slave mean +1.7 ms, p99 +3 ms; the audit build counted 0.08 skipped quads per frame that would have drawn. Kept off. |
| `E0_CHUNK_CAP` | 32 | Drops the farthest pick runs beyond N chunks. Cap 32: slave p99 35.9 -> 29.1 ms, 0 frames over two fields; a distant curve drops out at ticks 1895 and 4100 (owner approved). |
