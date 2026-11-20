# Engine courses: one asset set per course (lane `claude/courses`, 2026-11-19)

The engine raced one course, Mute City. This page covers how every other original course gets its
asset set, what each course needs, and what the engine does not handle yet. All timings are EMULATOR
(Ymir), timing MODELLED; hardware UNRUN.

## 1. The courses

Course index = cup x 6 + round (`course_select/course_model.c`). The table is `tools/course_catalog.py`.

| Index | Course | Venue | Sky | Music | Shapes (control points) | Engine status |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | Mute City | 0 | 0 purple, clouds | 0 | road, walled, 1 tunnel, 1 air | the first course |
| 1 | Silence | 9 | 4 night, stars | 1 | road, walled, tunnel | runs (section 5) |
| 2 | Sand Ocean | 3 | 2 desert, clouds | 2 | 13 road, **15 pipe** | needs pipes (section 6) |
| 3 | Devil's Forest | 4 | 5 orange, clouds | 5 | road, tunnel; 7 dirt zones | runs (section 5) |
| 4 | Big Blue | 2 | 7 sky blue, clouds | 4 | 10 road, **10 cylinder** | needs cylinders (section 6) |
| 5 | Port Town | 1 | 6 sunset, clouds | 3 | road, walled, tunnel; 3 ice zones | runs (section 5) |

## 2. Baking a course

`generated/` holds the first course and `generated/courses/<slug>/` the others. Every tool keeps
the first course's outputs byte for byte (checked: all 29 engine-read outputs identical after
the changes).

| Asset | Tool | Course input |
| --- | --- | --- |
| Course record | `import_course.render_record_c` | ROM slot from `course_catalog.py` |
| Road mesh | `road_assets.py --course` | the HOST reference's course writer on the record, plus the venue's texture segment |
| Road pack, bake, prep | `road_pack.py`, `road_bake.py --pack --texture-limit`, `road_prep.py --bake --texture-limit` | the course's road |
| Backdrop | `backdrop_assets.py --course` | the venue floor, skybox and skyline list of the record and course index |
| Sky, skyline, clouds, floor | `sky_assets.py`, `skyline_assets.py`, `plane_assets.py`, `floor_rbg_assets.py` | the course's backdrop |
| Physics | `fzx-ref --bake` (handling build), then `engine_drive_assets.py` | the record |
| Camera path, constants | `fzx-sim --engine-course` on the R01 race's inputs (7,058 ticks), then `engine_path.py`, `engine_path_ranges.py --gen` | the record |
| Rivals | `rivals_assets.py --course`, `fzx-rivals`, `fzx-gp-ref --tuning-course`, `rivals_field_header.py --course` | the course's CPU line and its rows of the CPU tuning tables |

Build: `make -C saturn/engine ENG_COURSE=<slug> ...` takes that course's set. A later course also gets
its own skyline layout (`skyline_ring.h`), the larger track texture budget and the sky colour pick
by distance (below). The music defaults to the course's own. `ENG_COURSE=mute-city-1` (the default)
builds the images it always did. The E0 BIN is unchanged (9d4db29d). The field, scenes and drive
BINs change only through the SH2ZAM pin's `line-field-2` and the one glue line that sets it; the
Mute City field race keeps its pixel hash.

**Course catalog (`ENG_COURSE_SELECT=1`, scenes images):** `fzx_course_catalog` (`engine_courses.h`)
has one row per baked course: original id, cup and round, music id, Time Attack availability and a
display key. The select placeholder and the loading card show the row's key. LOAD refuses a course
the image carries no packs for (scenes report 61). The music is the selected row's. Today an image
carries one course's packs, so only that row is open. Run-time pack sets per row are the next step.

### Course differences the bake handles

- **Skyline:** one to six placements of up to four images (Silence 2, Port Town 3). Unused slots
  stay transparent and hidden, so the VRAM span does not change.
- **Night (Silence):** no cloud layer (the cloud bank is baked uncovered), and the skyline is drawn
  untinted, as the source does at night.
- **Sky gradients that are not monotonic** (the Port Town sunset): the sky band colour is the
  nearest of the 64 samples by channel distance (`FZX_SKY_LINEAR_PICK`). Mute City keeps its
  brightness bisection.
- **Track textures:** Silence's are 236,384 B and Port Town's 163,072 B, against the 150,000 B
  that was enough for Mute City. Later courses bake with a 240,000 B budget
  (`FZX_ROAD_STREAM_TEXTURE_LIMIT`). The image still refuses a pack that reaches the 3D player or
  the sprite slots (fail 35 / 51).
- **Rival pacing:** from lap 2 the source re-targets each paced CPU racer to the course's finish
  table (`cpu.c` `func_i3_80116A50`). SH2ZAM `line-field-2` adds this (`finish_ticks`), and
  `tools/rivals_finish.py` writes each racer's target into the field header. On Mute City the
  finish table equals three lap bases, so every target there is 0 and the field runs as before.

## 3. VDP1 VRAM map of a race (field image, end of race; texture partition 0x0a020..0x7c000)

Measured by `vdp1map.py` (claude-crs evidence) from the final RAM dump.

Below the track: the skyline (0x0ac20) and the sky. Above the slot pool: the CLUTs (0x7ee00..0x7f580) and the shadow
(0x7f580..0x7f758). The 3D player region is the scenes layout's bound (20,480 texel + 12 x 2,048 shade bytes),
larger than the field image's. Identical placements in the roster scenes image (`ENG_ROSTER=1`, race 1 at tick 2000),
whose rival sets stay on the cartridge and reach VDP1 only through the slot pool.

| Course | Track texels (bytes) | Track and shades end | 3D player region | Slot pool (90 slots) | Headroom |
| --- | --- | --- | --- | --- | --- |
| Mute City | 0x0cca0..0x30c20 (147,328) | 0x31c20 | 0x5b200..0x66200 | 0x66200..0x7c000 (89,600) | 169,440 B |
| Silence | 0x0cca0..0x46800 (236,384) | 0x47800 | 0x5b200..0x66200 | 0x66200..0x7c000 | 80,384 B |
| Devil's Forest | 0x0cca0..0x302b0 (144,912) | 0x312b0 | 0x5b200..0x66200 | 0x66200..0x7c000 | 171,856 B |
| Port Town | 0x0cca0..0x349a0 (163,072) | 0x359a0 | 0x5b200..0x66200 | 0x66200..0x7c000 | 153,696 B |

A pack that would reach the 3D player or the pool is refused at load (fail 35 past the partition, fail 51 on overlap).

## 4. Course features the engine does not handle yet

| Feature | Courses | State |
| --- | --- | --- |
| Pipes (driving round the inside of a tube) | Sand Ocean (15 points), later cups | physics treats them as road; the road bake already accepts the pipe geometry; section 6 |
| Cylinders (driving round the outside) | Big Blue (10 points), later cups | physics as road; the road bake refuses the cylinder strip pairing |
| Half-pipes | later cups | physics as road |
| Stars (night sky) | Silence | not drawn: the night sky is the gradient and the skyline only |
| Flashing skyline windows (night) | Silence | not animated |
| Surface zones for rivals (dirt, ice) | Devil's Forest, Port Town | rivals ignore them; the player's physics has them |

## 5. Per-course results

Field image (`FZX_E1_DRIVE=1 E1_DRIVER=1`, 29 rivals), whole 3-lap race, Ymir; HOST checkpoints are
`tools/physics_host` `fzx-phys --checkpoints 256` against the image's 36 checkpoints and final state.

| Course | R01 HOST reference | Native HOST | Ymir | Checkpoints | Intervals | Master p99 / max | Slave p99 | VDP1 p99 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Mute City (pin aa0a587) | 104,039 ms | 104,040 ms | 104,040 ms (+0.001%) | 36 + final equal | 4,519 x 2F, 0 over | 25.6 / 26.4 ms | 21.8 ms | 12.5 ms |
| Silence | 90,562 ms | 90,563 ms | 90,563 ms (+0.001%) | 36 + final equal | 4,519 x 2F, 0 over | 26.1 / 26.6 ms | 20.8 ms | 9.1 ms |
| Devil's Forest | 108,038 ms | 108,038 ms | 108,038 ms (0.000%) | 36 + final equal | 4,519 x 2F, 0 over | 25.1 / 25.9 ms | 20.8 ms | 15.2 ms |
| Port Town, R01 nominal | retires at tick 6,327 | retires at 6,328 | retires, as HOST | 36 + final equal | 4,519 x 2F, 0 over | 26.7 / 31.6 ms | 20.3 ms | 12.8 ms |
| Port Town, R01 with heading gain 3.0 | 118,080 ms | 118,080 ms | 118,080 ms (0.000%) | 36 + final equal | 4,519 x 2F, 0 over | 26.7 / 31.7 ms | 20.3 ms | 13.2 ms |

- **Mute City** keeps its pixel hash (84b959e1e52ce45f) with the new SH2ZAM pin; its E2 rival band
  hashes (6 seeds) equal the E2 lane's baseline.
- **Determinism:** a second run of each course gives the same pixel hash and field trace.
- **Corrupt-pack controls** (one byte flipped on the disc): all 11 course packs are refused on
  each new course (ENGPATH 17, SKYLINE 30, PLANES 31, FLOOR* 32, ROAD 34, ROADLOD/ROADLTX 35,
  PHYS/RIVALS 50). ROADLTX takes about 4,000 fields to refuse, as on Mute City.
- **Port Town and R01:** the nominal driver (kl 0.004, kh 2.5) hits the walls of the segment 9-11
  chicane and the segment 21-22 hairpin every lap, in the reference and the native model alike
  (the same contact ticks). Its energy runs out on lap 3. With kh 3.0 (`E1_DRIVER_KH=3.0`,
  `fzx-ref --driver 0.004,3.0`) both finish. kh 3.5 is chaotic: the reference finishes and the
  native retires. The panel is in the lane evidence.
- **E2 rival bands** (6 seeds, original code against the native field, `line-field-2`):

  | Course | Result |
  | --- | --- |
  | Mute City | 13 + exact pass; band hashes unchanged |
  | Silence | 14/14 (it failed `winner_tick` before the re-target) |
  | Port Town | 13/14; the exact `through_ticks` check counts 1 tick |
  | Devil's Forest | 13/14; `lap1_median` fails: native 1,947..2,018 ticks against an accepted floor of 1,987 |

  The open item is first-lap chasers. The native chasers run their first lap about 11% faster
  than the source's on every course, Mute City included (Mute City 2,156 against 2,424 ticks
  mean, inside its band). A first-lap cap from the source's `D_i3_8013DC18` changed nothing and
  was dropped.


## 6. Plan: pipes and cylinders (Sand Ocean, Big Blue)

Estimate before building. Section 4 says what exists.

**Physics (SH2ZAM `vehicle-dynamics-2`).**
- **Surface frame:** in a pipe the ground is the inside of a circle of the segment's radius around
  the spline; in a cylinder it is the outside. The source (`racer.c` `func_8008EC98` pipe,
  `func_8008F550` cylinder, `func_8008FC80` half-pipe, about 200 lines each) derives
  `upFromGround` from the radial displacement to the spline: inward for a pipe, outward for a
  cylinder.
- **Gravity:** along `-upFromGround` rather than world up. Height above ground is radius minus
  radial distance (pipe) or radial distance minus radius (cylinder).
- **Joins:** the join tapers at both segment ends (`joinScale`, previous/next join T values).
- **Walls:** none inside a closed pipe. Leaving a cylinder's surface is falling.
- **Native form:** the radial vector is already computed (the closest-point search gives the
  displacement). One normalise per tick (`fixed-unit-1`), plus the taper.
- **Shared data:** the shape codes and radii are already in the physics pack (`SH2Z_VD_SHAPE_PIPE`,
  `_CYLINDER`, `_HALF_PIPE` exist and fall back to road today).
- **Gates:** HOST checks against the reference on its own fixtures first (a straight pipe, a
  cylinder, a join), each with a failing control. Then R01 on Sand Ocean, where today, with pipes
  treated as road, native is +1.27% against the reference's 92,556 ms. R01 on Big Blue needs its
  road bake.
- **Estimate:** 2 to 3 days.

**Road pick (`engine_pick_body.h` `out->outer = 1`).**
- The pick assumes the camera is outside the track surface. Inside a pipe it is inside: back-face
  and cull rules flip.
- The fix is a per-segment `outer` from the shape flags baked into ROADPREP (the bake has the
  chunk flags: `TRACK_SHAPE_MASK`, `TRACK_FLAG_INSIDE`). The pick takes the camera's segment.
- **Estimate:** 1 day, with the pick's HOST plan check on Sand Ocean against the reference planner
  (`engine_bake plan`).

**Rendering inside tubes.**
- **Culling:** quads facing away from the axis are culled by winding, as today. Inside a pipe the
  winding test inverts with `outer`.
- **The cap:** a pipe seen from inside covers the screen, and the far end needs no backdrop. Where
  every visible chunk is a closed pipe, the VDP2 floor and clouds are switched off for the frame
  (a cheaper frame).
- **Floor and clouds hidden:** the floor's line window narrows to nothing while the camera's chunk
  is a closed pipe. RBG0 parameter B and the cloud window close the same way. Restore on exit.
- **Cylinder strips:** the road bake's strip pairing refuses Big Blue's cylinder topology (an "S"
  pair). `road_lod_census.pair` needs the cylinder's ring order. About 1 day.
- **Budget risk:** a pipe's inside shows more track at once (the walls are road). The 26-chunk
  stable cap and the 22 ms slave margin decide whether LOD shift 2 is enough there.
- **Estimate:** 3 to 4 days, including stills.

**Total:** about 7 to 9 days for both courses, in this order: Sand Ocean physics and pick (its road
already bakes), then the tube render, then the Big Blue strip pairing and cylinder physics.
