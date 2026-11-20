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
BINs change through the SH2ZAM pin (`line-field-2`, `line-field-3`) and the glue that sets them.
With `line-field-2` alone the Mute City field race kept its pixel hash (84b959e1). `line-field-3`
changes Mute City's rivals on purpose (the first-lap chasers): pixel hash c8d8ff83, with the player's
race, the HOST checkpoints and the final state unchanged.

**One disc, every course (`ENG_COURSE_SELECT=1`, scenes images).**
- **Bundle:** `tools/course_bundle.py <slugs>` writes `generated/courses/select`:
  - `course_ids.c`, one `fzx_course_id_t` per course. It holds everything the single-course image
    compiles in: pack sizes and hashes, ROADPREP table offsets, the engine path ranges, sky
    colours and breaks, the skyline layout, plane fog, the rival field, the course record and
    the sky pick mode.
  - `files.txt`, giving each course's packs their own disc names (`EPATH3.PAK`, `RLTX5.PAK`, ...).
    The disc carries all of them (64 root entries).
- **Code:** `saturn/course_select.h` routes every identity use through `CSEL`, `CNAME` and `CMAX`
  to `fzx_cid`. In other builds these are the old macros, so the E0 BIN (9d4db29d) and the field
  BIN are unchanged by them.
- **Catalog:** `fzx_course_catalog` (`engine_courses.h`) has one row per course: original id, cup
  and round, music id, Time Attack availability and a display key.
- **Select:** LOAD calls `fzx_course_use(session.course)`, refusing a course that is not bundled
  (scenes report 61). The resident race set is keyed by course and machine.
- **Music** comes from the selected row.
- **Placeholder select:** left and right cycle the course; probe `0x2nn` sets course `nn` for
  scripts.

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
| Stars (night sky) | Silence | not drawn; the night sky is the gradient and the skyline only. Source: 100 IA8 8x8 sprites at random points 100,000..200,000 up (`Background_InitStars`); planned as VDP1 sprites in the prelude |
| Flashing skyline windows (night) | Silence | not animated; the source cycles the palette's white entry (`Background_UpdateBackgroundSprites`) |
| Surface zones for rivals (dirt, ice, dash plates) | Devil's Forest, Port Town and every course with plates | rivals ignore them; the player's physics has them |

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

- **Mute City:** with `line-field-2` the field image kept its pixel hash (84b959e1e52ce45f), and
  its E2 band hashes (6 seeds) equalled the E2 lane's baseline. `line-field-3` then moves its
  first-lap chasers (pixel hash c8d8ff83; race 104,040 ms; 36 checkpoints and the final state
  unchanged). With `line-field-3` the field races give master p99 25.5 (Mute City), 26.1 (Devil's
  Forest), 26.7 (Silence) and 26.5 ms (Port Town, kh 3.0), with 0 intervals over two fields.
- **Determinism:** a second run of each course gives the same pixel hash and field trace.
- **Corrupt-pack controls** (one byte flipped on the disc): all 11 course packs are refused on
  each new course (ENGPATH 17, SKYLINE 30, PLANES 31, FLOOR* 32, ROAD 34, ROADLOD/ROADLTX 35,
  PHYS/RIVALS 50). ROADLTX takes about 4,000 fields to refuse, as on Mute City.
- **Port Town and R01:** the nominal driver (kl 0.004, kh 2.5) hits the walls of the segment 9-11
  chicane and the segment 21-22 hairpin every lap, in the reference and the native model alike
  (the same contact ticks). Its energy runs out on lap 3. With kh 3.0 (`E1_DRIVER_KH=3.0`,
  `fzx-ref --driver 0.004,3.0`) both finish. kh 3.5 is chaotic: the reference finishes and the
  native retires. The panel is in the lane evidence.
- **E2 rival bands** (6 seeds, original code against the native field; SH2ZAM `line-field-2`
  re-target plus `line-field-3` first-lap chase cap):

  | Course | Result |
  | --- | --- |
  | Mute City | 13 + exact pass |
  | Silence | 14/14 |
  | Devil's Forest | 14/14 |
  | Port Town | 13/14: `rank_change_ticks` native 192..272 against an accepted floor of 227.5 (reference 455..575) |

  - **First-lap chasers:** native chasers ran their first lap about 11% fast on every course
    (Mute City 2,156 against 2,424 ticks mean). The cause is in `cpu.c`: on lap 1 a chaser's
    `unk_1E8` (the boost share of its engine force, `racer.c` around line 5025) is
    `D_i3_8013DFA0`, which is 0 on these courses (1.2 later). It therefore runs at its machine's
    own top speed.
  - That top speed was measured on the HOST reference (eight stock machines at the default engine
    0.5, zones off) at 36.53..37.05, mean 36.7. `tools/rivals_finish.py` caps each chaser's
    first-lap target at 36.7 x the bake's speed calibration (`line-field-3` `first_lap_cap`).
    The caps from `D_i3_8013DC18` (42.6..55.6) never bind, and Blue Falcon's slider-0 top of 33.4
    was too low (Port Town failed four bands with it).
  - **Port Town's remaining band:** the source's field changes order about twice as often there.
    The likely cause is that rivals do not ride its six dash plates or slide on its ice
    (section 4).

**Run-time course choice** (`ENG_COURSE_SELECT=1`, scenes image in driver mode, `E1_DRIVER=1`). One
disc. The course is chosen in the select scene, then raced to the results:

| Course | Race | HOST checkpoints | Cold LOAD (fields) |
| --- | --- | --- | --- |
| Mute City | 104,040 ms | 27/27 equal to the E1 HOST run | 317 |
| Silence | 90,563 ms | 23/23 | 316 |
| Devil's Forest | 108,038 ms | 28/28 | 312 |
| Port Town (R01 nominal) | retires, as HOST | 25/25 | 326 |

- The default scenes image's cold LOAD is 302 fields.
- **Work RAM in the select image:** bss ends 17.7 KB later than the scenes image's (the course
  table is 13.6 KB). Master stack unused is 118 KB.
- **Low work RAM:** the track workspace ends at 0x2a3980 on Port Town, the largest course.
- **VDP1 VRAM:** placement is per course, as in section 3.


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

**Legacy recipe:** built from this branch, the legacy recipe BIN is 4b4cacb2 (sha256 4b4cacb26d22...),
byte-identical to the recipe of record (docs/FUNCTIONAL_RECIPE.md).

## 7. Next cards

**Rivals on surface zones (`line-field-4`; Port Town's last band).**
- **What the source does:** CPU racers ride dash plates (a free boost) and lose grip on ice like
  the player. The native field ignores both. Port Town has 6 plates and 3 ice zones. Its reference
  field changes order 455..575 ticks against native 192..272.
- **Data:**
  - The physics pack already holds each segment's zones: kind, t range and side bounds (`ref/handling_probe.c` `zone`,
    `tools/physics_pack.py`).
  - The course-line stations (`fzx-ref --line`) give each RIVALS.PAK node its segment and t.
  - A small offline table (node range, kind, lateral range) per course can be baked from these two.
    It needs no change to RIVALS.PAK. It goes into the field header, and into `course_ids` for
    the one-disc image.
- **Library (generic):** a zone list in `sh2z_field_params_t`.
  - An agent whose node and lateral fall inside a plate zone starts a free boost.
  - On ice its steer gain drops; on rough ground its target speed is damped unless it is boosting.
  - With no zones, the field is `line-field-3` step for step.
- **Gates:**
  - Port Town 14/14.
  - Mute City (4 plates), Silence (8) and Devil's Forest (dirt) keep passing.
  - Pinned `line-field-3` hashes when zones are absent.

**Silence's night sky.**
- **Stars:** 100 IA8 8x8 sprites at random sky points, from the source's `Background_InitStars`.
  Bake the positions offline with a fixed seed. Draw them as VDP1 sprites in the prelude. The
  source projects them with the camera and draws those inside the scissor. The star texture
  `D_F2207C8` goes into the skyline pack's spare slots.
- **Windows:** the skyline palette's white entry flashes at night (`Background_UpdateBackgroundSprites`,
  `whiteIndex`). The plan is one CLUT word rewritten per frame, behind the change flag.
