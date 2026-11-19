# Functional release recipe

O-040 uses the owner-approved coarse far-road tier on the slave SH-2, with fast floor and cloud preparation, the fixed road planner and backdrop catch-up on the VDP2 clouds and NATIVE + pose-1 recipe with low vehicle tables and high vehicle work for local source-asset builds. Use this image for whole-frame comparisons and new C8 qualifications. Use `3600e763` for the matched slave-road comparison with LOD off, and `fcb42977` for serial road with LOD off. Use `a63fd93d` for serial road with fast preparation off. Use `b7d56eda` for the float-planner comparison with catch-up enabled. Use `e320045e` for the publication comparison with both publication switches off. Use BIN `901ac3cb` with VDP2 clouds off for paired comparisons. C3 legacy vehicle and FAST remain comparison arms. Keep the existing planes-off and knob-off identity controls.

## Build settings

The measured build uses source `576375d` (same image sources at integration commit `188223d`) and SH2ZAM `65b81d5`. Start from the qualified generated assets. Copy `sat_stubs.c` and `sat_stubs_data.s` from `generated/machine-stubs-menu-tables/1/` into the closure build directory. Export these settings for both closure generation and the Saturn build.

```text
FZX_BACKDROP_CATCHUP=1 FZX_ROAD_PLAN_FIXED=1 FZX_DEFER_PROJECTION=1 FZX_FASTFLOAT=1 FZX_SOURCE_CAMERA=1
FZX_SOURCE_COUNTDOWN=1 FZX_NATIVE_CORE_EMPTY=1 FZX_SOURCE_MACHINE=1
FZX_SOURCE_MACHINE_MENU=1 FZX_SOURCE_RESULTS=1 FZX_SOURCE_RECORDS=1
FZX_SOURCE_FINISH_MENU=1 FZX_SOURCE_ROAD_STREAM=1 FZX_ROAD_PLAN=1
FZX_SOURCE_SKY=1 FZX_SOURCE_SKYLINE=1 REF_CAMERA_ART=retail
```

Run `ref/sat/closure.sh` on that directory and pass its resulting `ref_closure.o` to make as `FZX_CLOSURE`. Use the following Saturn make assignments. Vehicle placement uses the measured defaults `FZX_VEHICLE_NATIVE_LOW_TABLES=1` and `FZX_VEHICLE_NATIVE_LOW_WORK=0`.

```text
FZX_SIM=orig FZX_IMPORTED=1 FZX_SOURCE_MACHINE=1
FZX_SOURCE_CAMERA=1 FZX_ROM_BOOT=1 FZX_SOURCE_HUD=1
FZX_SOURCE_SKY=1 FZX_SKY_SIMPLIFY=1 FZX_SOURCE_SKYLINE=1
FZX_SKYLINE_CD=1 FZX_HUD_CD=1 FZX_SOURCE_PLANES=1
FZX_SOURCE_ROAD=1 FZX_SOURCE_ROAD_TILES=1 FZX_ROAD_TILE_PROJECT_CULL=1
FZX_ROAD_TILE_EDGE_REUSE=1 FZX_ROAD_TILE_PROFILE=0 FZX_ROAD_TILE_VERTEX_CACHE=0
FZX_SOURCE_FLOOR_RBG=1 FZX_FRAME_PROFILE=1 FZX_SOURCE_TITLE=1
FZX_SOURCE_MENU=1 FZX_SOURCE_COURSE_MENU=1 FZX_SOURCE_MACHINE_MENU=1
FZX_SOURCE_RESULTS=1 FZX_SOURCE_RECORDS=1 FZX_SOURCE_FINISH_MENU=1
FZX_SOURCE_ROAD_STREAM=1 FZX_PLANE_CLOUD_ONLY=1 FZX_SOURCE_COUNTDOWN=1
FZX_NATIVE_CORE_EMPTY=1 FZX_DEFER_PROJECTION=1 FZX_ROAD_STREAM_NATIVE_LAYOUT=1
FZX_ROAD_STREAM_EMIT=1 FZX_MACHINE_REBAKE=1 FZX_VEHICLE_NATIVE=1
FZX_ROAD_STREAM_BUDGET=1 FZX_MENU_TABLES_LOW=1 FZX_PLANES_DEGENERATE_UV=1
FZX_VEHICLE_POSE1=1 FZX_CLOUDS_VDP2=1 FZX_BACKDROP_CATCHUP=1 FZX_ROAD_PLAN_FIXED=1 FZX_RBG_FAST=1 FZX_SLAVE_ROAD=1 FZX_ROAD_LOD=1
```

## Measurement and qualification

The sealed BIN SHA-256 is `4b4cacb26d221259b7e621f3f41e55488fd6fd5d50e28536c6c1c86945deb3d7`. The Ymir replay finished at input 7,059 across 1,800 presented frames. Modelled whole-frame time was 65.55 ms mean, 100.00 ms p99 and 116.67 ms maximum. Road and vehicle commands peaked at 290 and 66. The road maximum includes the clip command and 289 draws. All 1,802 slave jobs used coarse tiering. Zero road commands were dropped. The master had 18,008 unused stack bytes and the slave had 5,624 untouched painted bytes, with zero guard trips or late waits. Physical Saturn bus contention and frame timing remain unmeasured.

Strict selected13 passed in LOD mode against `3600e763`. The effective VDP1 prelude and post-road command sequences matched at all 13 holds. The approved visual change is reduced texture-period detail on the far road. Independent regeneration reproduced the road plan, sky, skyline and floor packages. The inherited countdown receipt failure remains open.

The fixed planner approximates render depth and visibility near clipping boundaries. Unsupported numeric ranges fall back to the float planner before chunk writes. Selected13 verified all 13 command streams, exact group membership and order, and a maximum group average depth difference of 0.39551 units within the per-hold bounds. Menu-table guards passed on the full route and selected13.

The exact-BIN finish-menu and records qualification from O-030 applies to comparison image `e320045e`. Finish-menu and records on the new recipe, ordinary controller retry and physical Saturn mode 3 remain unverified.
