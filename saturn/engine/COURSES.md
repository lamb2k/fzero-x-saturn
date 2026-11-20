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
| 2 | Sand Ocean | 3 | 2 desert, clouds | 2 | 13 road, **15 pipe** | races; rivals on tubes open (section 8) |
| 3 | Devil's Forest | 4 | 5 orange, clouds | 5 | road, tunnel; 7 dirt zones | runs (section 5) |
| 4 | Big Blue | 2 | 7 sky blue, clouds | 4 | 10 road, **10 cylinder** | races; rivals on tubes open (section 8) |
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
| Pipes (driving round the inside of a tube) | Sand Ocean (15 points), later cups | player physics and road done (section 8); rivals stay in line space |
| Cylinders (driving round the outside) | Big Blue (10 points), later cups | player physics and road done (section 8); rivals stay in line space |
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

## 8. Pipes and cylinders: results (lane `claude/tubes`, 2026-11-20)

HOST and EMULATOR (Ymir) tiers; timings MODELLED; hardware UNRUN. Evidence: `fzx-evidence/host/claude-tub/`.

**Physics.** The player runs SH2ZAM `vehicle-dynamics-2` (`sh2z_vd_step2`, PR #54; pin `claude/tubes-train`
b68b859 = courses-train aa0a587 + the PR). It ports `racer.c` `func_8008EC98` (pipe) and `func_8008F550`
(cylinder) with their joins.
- The reference bake (`ref/handling_probe.c`) writes each tube span's join t values and scale.
- Tube courses get physics pack v3 (a join table); every other course keeps v2 byte for byte.
- `ENG_TUBES` is on by default for sand-ocean-1 and big-blue-1 only. `ENG_TUBES=0` links
  vehicle-dynamics-1, which refuses the v3 pack (the control).
- The vehicle-dynamics-1 object is byte-identical, and so are all 44 library objects.
- Half-pipes are not modelled.

| Gate (HOST) | Sand Ocean | Big Blue |
| --- | --- | --- |
| R01 against the reference | -0.003% (92,553 against 92,556 ms; was +1.268% with pipes as road) | +0.000% (126,928 ms) |
| C-076 windows over every tube span and join (7 driver arms) | 368/368, endpoints 368/368 (worst 9.2 units) | 330/330 (worst 2.7 units) |
| Tube classes (passing/exercising) | pipe 368/368, join 143/143, tube_in 63/63, tube_out 81/81 | cylinder 330/330, join 69/69 |
| Mutation controls (windows still passing) | tubes-off 17/368, join-scale+10% 275, join-lever-half 233, pipe-rim-30 292 | tubes-off 0/330, join-scale+10% 283 |
| Whole-race guardrail | 4 finishing arms within 0.05%; lateral-target arms retire in both | 6 arms within 0.001%; the boost arm retires at the same tick in both |

- Mutations that change nothing: pipe-rim+5, and cyl-reach-50/-150, because no arm leaves the
  cylinder. Leaving the cylinder is covered by the SH2ZAM host test and its control.
- **Tools:** `tools/physics_host/build.sh` (`TUBES=1`) and the tube mutations in `fzx-phys`. The judge is
  `claude-tub/harness/tubjudge.py`.

**Road.** Big Blue now bakes.
- Its 8 cylinder-join pairs without constant-S edges get their own corner copies with snapped S
  (`tools/road_tiles.py`): approximate texturing on those 8 quads of 2,262.
- The road_bake and road_prep outputs of the other five courses are unchanged.
- The pick takes the road plan's outer bit from the player's span and t (`fzx_drive_outer`).
  It is 0 inside a pipe away from its joins (course.c `D_800F89D8`), so the tube's outer shell is
  not drawn there.
- The winding cull needs no change: the pipe's inner quads face the axis.
- **Not done:**
  - the original's darker fog inside a pipe (fog colour / 4);
  - closing the VDP2 floor and clouds inside a pipe;
  - tunnels' outer bit (still 1).
- Big Blue's camera path stops at tick 1,045, where a dash-plate boost changes the projection. The
  field image takes its lens before that.

**Ymir, field image, whole race** (29 rivals; HOST checkpoints from `fzx-phys --checkpoints 256`, tubes build):

| Course | Race | Checkpoints | Intervals | Master p99 / max | Slave p99 | VDP1 p99 | Road cmds max | Pixel hash |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Sand Ocean | 92,553 ms, as HOST | 36 + final equal | 4,519 x 2F, 0 over | 25.7 / 26.4 ms | 24.0 ms | 11.3 ms | 199 | 74c04953cdbf9ac8 |
| Big Blue | 126,928 ms, as HOST | 36 + final equal | 4,519 x 2F, 0 over | 27.7 / 29.5 ms | 16.9 ms | 8.9 ms | 130 | ea59e2727ee8661f |

**Default images unchanged** (against 17e1f84 with pin aa0a587, same stage):
- E0 9d4db29d;
- Mute City field 0e510d9b, drive 079e73e7, scenes 7d8513eb;
- Silence field 513abdfb, Devil's Forest 5dbe5bae, Port Town 7c48a88d.

**Corrupt-pack controls:** one byte flipped on the disc in the v3 PHYS.PAK's join table (Sand Ocean) and in Big
Blue's PHYS.PAK; both images refuse it (status 50).

**Rivals (E2 bands, 6 seeds, line-field-2):**
- Sand Ocean passes 14/14.
- Big Blue fails `finished` and `retired`: the native field retires 1 to 3 rivals and the
  reference none. The line field charges wall energy at the line's edge (`fld_wall`). A tube span
  has no edge, so the rival should wrap round it instead.
- Next card: a line-field change that gives tube spans no edge and a wrapped lateral, and a per-point
  shape in RIVALS.PAK. It also needs the 3D placement of rivals round the tube; today they sit on
  the road plane.

## 9. The Jack Cup on one disc (lane `claude/jack`, 2026-11-20)

EMULATOR tier (Ymir headless); timings MODELLED; hardware UNRUN. Evidence: `fzx-evidence/host/claude-jack/`.

**Integration.** `claude/jack` = engine c07f6cf (cup, balance) + `claude/courses` c4d9d0a + `claude/tubes` f1381f4.
- **SH2ZAM pin:** main accd505 (train 7: line-field-3 and vehicle-dynamics-2 both landed). It replaced
  `claude/jack-train` 8449e35, whose library sources and headers are identical: the E0, Mute City field, select
  and default scenes BINs built against accd505 are byte-identical to those built against 8449e35 (9d4db29d,
  5d4cdc2c, 1fad0b2f, d426166b).
- **One catalog.** The cup's catalog (`engine_cup.c`, SCENES.md 11.1) decides what a build can race. The courses
  lane's table became `fzx_course_info` (cup, round, music, name). LOAD selects the packs of the course it loads
  (`fzx_course_use(load_course)`), so a GP round would load its own; probe `0x2nn` moves the select cursor.
- **Six courses on the disc.** `tools/course_bundle.py` bundles all six (60 course files, 81 root entries) and
  writes `FZX_COURSE_BUNDLE_MASK`. The CD file list grows to 96 entries; the scenes image keeps it in `kept[]`
  (bss), so the 4 KB CD scratch no longer holds a list there. `ENG_TUBES` is on in this image: vehicle-dynamics-2
  gives road shapes vehicle-dynamics-1's bits (HOST checkpoints on the four road courses identical with either).
- **Default.** `ENG_COURSE_SELECT=1` is now the scenes default (default `ENG_COURSE`); `=0` builds the single-course
  scenes image.

**Each course from the same image** (scenes image in driver mode, `ENG_SCENES=1 FZX_E1_DRIVE=1 E1_DRIVER=1`, BIN
1fad0b2f; the default scenes image without `E1_DRIVER` is d426166b). Chosen in select (probe), raced to RESULTS.
Frames from tick 420.

| Course | Race | HOST checkpoints | Intervals | Master p99 | Critical path p99 | Slave p99 | VDP1 p99 | Cold LOAD |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Mute City | 104,040 ms | 27/27 | 3,258 x 2F | 24.3 ms | 24.73 ms | 21.6 ms | 12.5 ms | 322 fields |
| Silence | 90,563 ms | 23/23 | 2,853 x 2F | 24.3 ms | 24.42 ms | 20.8 ms | 9.1 ms | 320 |
| Sand Ocean | 92,553 ms | 24/24 | 2,913 x 2F | 24.9 ms | 26.58 ms | 24.6 ms | 11.2 ms | 325 |
| Devil's Forest | 108,038 ms | 28/28 | 3,378 x 2F | 23.6 ms | 23.97 ms | 20.7 ms | 15.5 ms | 316 |
| Big Blue | 126,928 ms | 32/32 | 3,944 x 2F | 22.0 ms | 23.12 ms | 16.9 ms | 9.0 ms | 323 |
| Port Town (R01 nominal) | retires, as HOST | 25/25 | 3,091 x 2F | 22.9 ms | 23.46 ms | 20.5 ms | 14.0 ms | 330 |

- Every race time equals the HOST reference's (`fzx-phys --checkpoints 256`, tubes build, these packs).
- Sand Ocean has the highest critical path (26.58 ms p99) and slave (24.6 ms): the pipes show the most track.

**Memory** (RESULTS dumps).
- **VDP1:** section 3's map holds; the two new courses: Sand Ocean track texels 0x0cca0..0x3e3a0 (202,496 B),
  headroom 114,272 B; Big Blue 0x0cca0..0x37db0 (176,400 B), headroom 140,368 B. Silence stays the largest (80,384 B
  headroom).
- **Low work RAM:** the path pack (largest Port Town, 566,708 B of the 573,440 B region), CD scratch 0x28c000, input
  log 0x28d000, track workspace end 0x2a3da0 at most (Sand Ocean), then the frame rows (6,289 rows of room, 4,155
  used on Big Blue).
- **High work RAM:** bss 0x0604a140..0x060ebf40; master stack unused 76,324 B at least.

**Controls.**
- **Corrupt packs** (one byte flipped on the disc, Sand Ocean): EPATH2 17 (a header byte: the drive reads only the
  header and the lens record, so a middle byte is never read), ROAD2 34, RLOD2 35, RLTX2 35, RPREP2 35, SKYL2 30,
  PLANE2 31, FCHR2 32, FLOORMAP 32, FLOORKOF 32, PHYS2 50, RIV2 50; Big Blue PHYS4 50.
- **Determinism:** a second run of Sand Ocean and Big Blue gives the same RESULTS picture, the same scene report and
  the same low work RAM; high work RAM differs only in the SMPC OREG buffer.

**Identity.**
- E0 BIN 9d4db29d, unchanged. Legacy recipe BIN 4b4cacb2 (sha256 4b4cacb26d22...), byte-identical.
- **Mute City field image** (`FZX_E1_DRIVE=1 E1_DRIVER=1`): 104,040 ms, HOST checkpoints 36/36 and the final state
  identical, 4,519 x 2F. Pixel hash 9750aeb61b9128fb with the balance defaults; with `ENG_BAL_CLOUDS=0
  ENG_BAL_EARLY=0 ENG_BAL_PREWARM=0` it is c8d8ff834422fd3, line-field-3's hash (as line-field-2's 84b959e1 became
  994dd847 with the balance on).
