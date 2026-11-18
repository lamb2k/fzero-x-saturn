# Functional release recipe

O-027 adopts NATIVE + pose-1 with low vehicle tables and high vehicle work for local source-asset builds. Use this image for whole-frame comparisons and new C8 qualifications. C3 legacy vehicle and FAST remain comparison arms. Keep the existing planes-off and knob-off identity controls.

## Build settings

The measured build uses gate1 source `0f03247` and SH2ZAM `65b81d5`. Start from the qualified generated assets. Copy `sat_stubs.c` and `sat_stubs_data.s` from `generated/machine-stubs-menu-tables/1/` into the closure build directory. Export these settings for both closure generation and the Saturn build.

```text
FZX_DEFER_PROJECTION=1 FZX_FASTFLOAT=1 FZX_SOURCE_CAMERA=1
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
FZX_VEHICLE_POSE1=1
```

## Measurement and qualification

The sealed BIN SHA-256 is `901ac3cb8218311a9a12a819f6acd8a12e400b18d96f3c109089c272dc94d04d`. The emulator replay finished at input 7,059 with 1,417 presented frames. Modelled whole-frame time was 147.83 ms mean and 216.67 ms p99. Maximum command counts were 420 road and 67 vehicle, with 35,576 bytes of unused stack. Hardware is unrun.

Bind finish-menu v3, records and ordinary controller retry results to the exact recipe BIN. The earlier NATIVE finish-menu pass used the pre-pose image. The pose-1 finish-menu replay and ordinary retry are pending at adoption.
