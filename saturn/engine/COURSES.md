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

**Road lookup tables aligned in the bake (lane `claude/vdp1`, 2026-11-20).**
- **Why:** VDP1 reads a lookup table from `(CMDCOLR & ~3) * 8`. `tools/road_bake.py` pools each 32-byte
  table at an 8-byte offset, so 106 to 186 tables per course are misaligned. `ENG_CLUT_ALIGN` copies
  them to aligned VDP1 VRAM at every track load (up to 6 KB after the road shades; fault 39 if they do
  not fit).
- **Card:** align the tables to 32 bytes in an engine-only bake path. Do not change the shared tool's
  default: the legacy recipe reads `generated/road_bake`. Regenerate the engine's six road bakes, preps
  and the select bundle. Then remove the load-time copy and its VRAM.
- **Gates:**
  - every table aligned at load (count 0 copies);
  - the Mednafen pictures unchanged against `ENG_CLUT_ALIGN=1`;
  - legacy recipe BIN `4b4cacb2` unchanged.

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

## 10. Jack Cup Grand Prix (lane `claude/jack-gp`, 2026-11-20)

HOST and EMULATOR (Ymir headless) tiers; timings MODELLED; hardware UNRUN. Evidence: `fzx-evidence/host/claude-jgp/`.
Pin: SH2ZAM main 6125bb8 (merge train 8, line-field-4; byte-identical images to ed55d38).

**Skyline refusal (status 30).** The texture-CD FNV check (status 8) refused SKYL2.PAK: all 8,320 bytes arrived,
the last sector as zeros. Cause: a sound CD command (the TOC read) left road_cd's stream open; `jack-wip` was based
before 8cae676 (every sound CD command now ends the stream). `jack-wip` rebuilt here (e3443d32) fails at field 986;
the same tree with 8cae676 alone races Sand Ocean. A second, rarer arrival fault showed on a GP NEXT load
(SKYL1.PAK's sectors 3 and 4 arrived as an ISO span 128 bytes off a sector boundary, stream open, no sound command
in flight): whole-file loads that fail the copy check or the FNV are now read again from a new stream, up to twice,
and counted (`fzx_texture_cd_retries`). Control: SKYL2.PAK with one byte flipped on the disc is still refused
(fault 30 after 2 retries). The root cause of that second fault is open.

**All six courses from one select image** (driver mode, sound on, rival surface on): race times equal HOST (104,040,
90,563, 92,553, 108,038, 126,928 ms; Port Town retires as HOST); every interval 2F, 0 over two; critical path p99/max
MC 24.88/27.29, Silence 24.62/25.51, Sand Ocean 26.63/28.03, Devil's Forest 24.13/25.44, Big Blue 23.24/24.81,
Port Town 23.47/25.32 ms; master stack unused 59,568 B; slave guard 0 trips.

**Rival bands (HOST, line-field-4 at the pin):** 14/14 and exact on all six courses (`all6.sh`). Control: Port Town
without zones fails 5 bands.

**Rivals round the tubes (3D).** `line_pose` / `line_pose_fast` place a pipe or cylinder rival on the tube's surface,
its up turned about forward by lateral / half width. Still pairs (`ENG_RIVAL_SURFACE` 1 against 0): rivals ride
Sand Ocean's pipe walls and sit on Big Blue's cylinder surface. **Open gap (tube wrap):** the tube-angle check
(`fzx-gp-ref --tube-pack`, `fzx-rivals-gp --tube-angles`, 6 seeds) gives the share of tube ticks beyond 60 degrees
from the drive line: Big Blue reference 0.56-0.65, native 0.09-0.12, wrap off 0.000; Sand Ocean reference
0.18-0.22, native 0.09-0.10, wrap off 0.000. The source's CPU racers go all round the cylinder (about 40% of their
tube time below the axis; the same with world up); ours stay within about 60 degrees of the top. The band is Codex's
call; the fix is a line-field-5 card with the rival drift, walls and falls.

**GP grid.** From round two the grid is the previous finish reversed (cpu.c: position = N - D_80115D50[i] + 1, set
at NEXT in menus.c), the player included; round one, its retries and other modes keep the course header's tier grid.
The grid is placed round the player's start body, as round one already does (moving the body put it past the line).
Scenes images only; `fzx_field_grid` / `fzx_field_grid_src` report it.

**Cup runs** (diag catalog `FZX_CUP_DIAG_CATALOG=1`, driver mode, sound on; image gp-fix3 5da8bdf6):
- Six rounds at Novice: from round two every grid equals the previous finish reversed (5/5); points non-increasing by
  place and `last_place` consistent every round; race times equal HOST on rounds 1-5; 0 disc reads under music
  (3,951 reads); every race interval 2F, 0 over two; menus 0 over two. Critical path p99/max by round: 24.99/27.99,
  25.18/28.43, 27.41/31.12 (Sand Ocean from a GP grid), 24.27/25.72, 25.06/27.29, 24.08/25.97 ms.
- Round 6 (Port Town) ends in a retirement: the HOST-equal driver retires there, as the R01 reference does (also with
  E1_DRIVER_KH 1.5, 2.0, 3.5). A finished sixth round needs a driver that finishes Port Town.
- Three rounds at Standard: the same checks pass. Rival results equal Novice's: the engine's rival tuning does not
  depend on the class yet (headers baked at difficulty 1); the class sets only the spare machines.
- One whole-file re-read in the six rounds (status 8 on the round-2 load), none in the three.

**Complete cup with the R03 driver** (lane `claude/jack-gp2`; P3 front end merged; image gp-r03 11885ba5,
`ENG_SCENES=1 FZX_E1_DRIVE=1 E1_DRIVER=2 FZX_CUP_DIAG_CATALOG=1`, sound on from the shipping mixed-mode disc; Ymir,
timings MODELLED):
- **R03** (`saturn/engine/drive_ahead.h`): the original CPU steering law (cpu.c `Cpu_GenerateInputs`, road spans:
  1.2 x (0.3 x lateral + 360 x heading), clamp 70, stick 5/8, lean past the clamp) against the road direction
  0.1 x speed ticks ahead. R01 is the same law without the look-ahead and stays the plays-right reference. HOST
  (`fzx-phys --driver-ahead 0.1`) finishes all six: 111,280, 90,560, 92,738, 102,326, 125,976 and Port Town
  117,113 ms (look 0.08 and 0.12 finish too).
- Six rounds at Novice to END: the player's time equals HOST every round; 5/5 grids are the previous finish reversed;
  points equal `gRacePositionPoints` exactly for every entrant; the cup reaches FINAL, then TITLE
  (`tools/gp_order.py ... 6 --player-ms ...`: PASS). 4,068 sector reads, 0 under music, 0 re-reads. Every race
  interval 2F, 0 over two; menus 0 over two. Critical path p99 by round 26.11, 25.38, 29.25, 25.52, 26.84, 25.46 ms
  (the look-ahead's soft-float work runs on the master in R03 images only).

**The GP NEXT CD fault, root cause.** A 256-sector stream (RIV1) filled the CD block's 200-sector buffer and paused.
The next file's stream start reset the selector, which freed partition 0; the drive resumed the old play and put its
next sector (FAD BEF) in partition 0 before the new Play (SKYL1, FAD 1371) seeked. The reader took it as SKYL1's
first sector, so the file arrived one sector late (CD block tracer: an insert of FAD BEF between the partition clear
and the 1371 Play; the VRAM copy is BEF, 1371..1374). "128 bytes off a sector boundary" was the shifted last sector's
128-byte tail over old VRAM. No CD-DA command was in the window. The fix is the head-FAD check at a discontinuous
start (`road_cd.c`, codex/p3-frontend 374ea5f): it drops such sectors (`fzx_cd_seek_stats[1]`). The whole-file
re-read stays as a counted safety net (0 in the cup). Control `FZX_CD_STALE_CONTROL=1` waits after each start's
selector reset for the stale sector: with the check, 49 met and 49 dropped, 0 re-reads; with `ENG_P3_UI=0` (no
check) the first load never completes.

## 11. The Queen Cup (lane `claude/courses-queen`, 2026-11-22)

Courses 6..11 (ROM record order: Sector Alpha, Red Canyon 1, Devil's Forest 2, Mute City 2, Big Blue 2, White Land 1)
go through the per-course pipeline of section 2 (`tools/course_catalog.py` rows, `SHIPPED` = the first six) and are
selectable on the same disc: the select image carries 12 courses. Tiers: HOST (`fzx-phys`), EMULATOR (Ymir, timing
MODELLED); HARDWARE not run.

**Bake measurements (HOST).** Chunks / triangles, ROADLTX (VDP1 texels), ROADLOD (cartridge), ROADPREP:
Sector Alpha 306 / 5,594, 173,992 / 165,968 / 71,740 B; Red Canyon 1 198 / 3,136, 67,000 / 94,984 / 39,504;
Devil's Forest 2 307 / 5,066, 164,312 / 149,304 / 66,668; Mute City 2 264 / 5,132, 189,992 / 149,688 / 67,696;
Big Blue 2 274 / 5,076, 164,480 / 150,544 / 67,976; White Land 1 281 / 5,062, 134,624 / 150,152 / 65,868.
Every course fits the Jack maxima for VDP1 (Silence 236,384 B, 316,768 B limit), the path pack region
(<= 566,644 of 573,440 B) and the cartridge (about 600 KB free); Sector Alpha has 2,797 quads (Jack max 2,661).

**Tool changes the Queen courses needed** (Jack outputs stay byte-identical):
- `road_tiles.py` accepts a reshaped source tile: same texel count and same line stride, width differing from the
  declared texture (Big Blue 2 material D_8002008, 64x32 drawn from a 128x16 texture).
- `road_bake.py` / `road_prep.py --pack-limit`, `FZX_ROAD_STREAM_PACK_LIMIT` in `src/fzx_road_stream.c`: Sector Alpha's
  ROADLOD is 165,968 B against the 160,000 B literal. The select and course-specific engine builds use 240,000.
- `ref/rivals_build.sh` links `-lm` (`fzx-rivals-gp`).
- CD root list 128 -> 192 entries (`FZX_TEXTURE_CD_ROOT_ENTRIES`, `road_native.c`): 12 courses are 141 files.

**Card: course identity rows on disc.** `fzx_course_ids[]` was 4,172 B a course in `.low` (25 KB for six, 100 KB for 24)
against 6,448 B of slack in the `.bsshost` buffer that carries `.low`'s load image: a 12-course image did not link.
`tools/course_bundle.py` now writes each row as a disc file `CIDnn.BIN` (big-endian, `fzx_course_id_t`'s layout built in
Python, a `_Static_assert` on every field offset in `course_ids.h`) and the image keeps only `fzx_course_index[]`
(id, size, FNV-1a: 8 B a course). `fzx_course_use()` reads and checks the selected row into `cid_row` (high work RAM) at
LOAD. The row carries an explicit `row` field; `engine_field.c` uses `fzx_cid->row` instead of
`fzx_cid - fzx_course_ids`, which was wrong with `ENG_LOW_COLD` (fzx_cid points at `cid_row`, not into the table).
`.low` is 0x102b0 B for 12 courses against 0x13640 B for six before; `.lowbss` +3.4 KB (root list).

**Gate.** `tools/engine_gate_matrix.json` lists the six courses. A course entry may name its own image and HOST driver:
White Land 1 retires under the R01 driver (reference and native model alike, 35 kl/kh pairs scanned) and finishes only
under R03, so it runs on `scenes-drv-r03` (`E1_DRIVER=2`) against `fzx-phys --driver 0.004,2.5 --driver-ahead 0.1`.
Run one course: `python tools/engine_gate.py --full --only course --courses mute-city-2 --base "" --stage <stage> --jobs 1`.

| Course | HOST finish (R01) | Ymir | Checkpoints | Intervals | Critical path mean / p99 / max | Master p99 | Slave p99 | VDP1 p99 | Cold LOAD |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Sector Alpha | 98,843 ms | equal | 25/25 | 3,102 x 2F, 0 over | 19.79 / 24.95 / 26.51 | 24.69 | 22.20 | 10.71 | 333 fields |
| Red Canyon 1 | 97,580 ms | equal | 25/25 | 3,064 x 2F, 0 over | 19.41 / 22.77 / 26.98 | 22.30 | 20.19 | 9.33 | 271 |
| Devil's Forest 2 | 109,503 ms | equal | 28/28 | 3,422 x 2F, 0 over | 21.30 / 24.70 / 25.93 | 23.98 | 22.33 | 11.04 | 325 |
| Mute City 2 | 97,479 ms | equal | 25/25 | 3,061 x 2F, 0 over | 21.71 / 29.54 / 31.38 | 24.83 | 27.44 | 11.78 | 326 |
| Big Blue 2 | 80,008 ms | equal | 21/21 | 2,537 x 2F, 0 over | 21.81 / 28.24 / 31.38 | 25.52 | 26.19 | 10.58 | 323 |
| White Land 1 (R03, `scenes-drv-r03`) | 111,775 ms (R03; R01 retires at tick 4,992) | equal | 28/28 | 3,490 x 2F, 0 over | 21.66 / 28.47 / 29.89 | 27.38 | 24.92 | 9.82 | 321 |

Master stack unused 110,732..110,748 B (109,436 B on the R03 image), slave 5,760 B, guard trips 0 on every course; menus 0 over two. Critical-path
p99 over the 24.5 ms limit on Sector Alpha, Devil's Forest 2, Mute City 2, Big Blue 2 and White Land 1 (R03's soft-float look-ahead runs on the master): reported, not tuned (the
Jack courses sit at 24.6 to 29.3 ms in the same gate). The native model's R01 finish is 0.03% (Red Canyon 1) and 0.06%
(Big Blue 2) from the original code's; Jack is within 0.005%: float noise of about 1e-4 growing into wall contact.

**Open (not baked or not drawn).** Stars on Mute City 2 (the Silence star item); jumps (Mute City 2 one, White Land 1
four) and air sections (Red Canyon 1, White Land 1) are physics-correct within the bands above but rivals ignore them;
borderless shapes (Big Blue 2, White Land 1); camera-path packs truncate on Red Canyon 1 (779 ticks), White Land 1
(1,062) and Big Blue 2 (996), as on Big Blue 1. Rival bands and drops for the Queen courses were not run.

**Re-bake steps after this merges (ROM needed).** `tools/course_catalog.py`, `road_bake.py`, `road_tiles.py`,
`road_prep.py` and `src/fzx_road_stream.c` are generator inputs, so every stage's road, backdrop and select receipts
are stale. Per course slug, one course at a time (parallel runs race for the `road` step): the `road` step is
`road_assets.py --course SLUG`, `road_pack.py`, `road_bake.py` and `road_prep.py` (Queen courses add
`--texture-limit 240000 --pack-limit 240000`; Jack keeps its defaults); the `back` step is `backdrop_assets.py --course SLUG`,
`sky_assets.py`, `skyline_assets.py`, `plane_assets.py --cloud-only` and `floor_rbg_assets.py`. The gate also lists every course's `rivals.receipt`
(`ref/rivals_build.sh` changed: `-lm`) and `course_class.receipt` as stale; the engine build does not read them. Then, for the 12 slugs in catalog order: `course_bundle.py`,
`course_drops_bundle.py`, `course_class_bundle.py`, `engine_load_cards.py`. Jack outputs come out byte-identical (only
`road_bake.json` differs: it records the pack limit).

## 12. The King and Joker Cups and Death Race (lane `claude/courses-kj`, 2026-11-22)

Courses 12..23 (ROM record order: Fire Field, Silence 2, Sector Beta, Red Canyon 2, White Land 2, Mute City 3, Rainbow Road,
Devil's Forest 3, Space Plant, Sand Ocean 2, Port Town 2, Big Hand) bake through section 2's pipeline
(`tools/course_catalog.py`; slugs `fire-field` ... `big-hand`) and are selectable on the 24-course select image. Death Race
(course index 54, ROM record 24 `aCourseBattle`, slug `death-race`) bakes (78 chunks) but is not on the disc yet (see Open).
Tiers: HOST (`fzx-phys`, `fzx-ref`), EMULATOR (Ymir, timing MODELLED); HARDWARE not run.

**Bake measurements (HOST).** Chunks / triangles / ROADLTX (VDP1) / ROADLOD (cartridge) / ROAD.PAK / ROADPREP, bytes:
Silence 2 378 / 6,048 / 201,976 / 178,896 / 175,552 / 77,852; Sector Beta 343 / 5,654; Red Canyon 2 305 / 5,158;
White Land 2 310 / 4,960; Mute City 3 274 / 4,384; Rainbow Road 419 / 6,704; Devil's Forest 3 269 / 4,698;
Space Plant 381 / 7,006 / 243,848 (the largest) / 205,512 / 195,240 / 89,336; Sand Ocean 2 394 / 6,826; Port Town 2 319 / 5,100;
Big Hand 422 / 6,752 (the largest chunk count; `FZX_EPATH_MAX_CHUNKS` 512). Every course fits VDP1 (headroom 72,920 B against
316,768 at Space Plant), the path region and the cartridge. Space Plant needs `--texture-limit 262144`.

**Engine changes the courses needed.**
- `engine_cup.c` catalog rows 12..23 (the select listed 12 courses); CD root list 192 -> 320 entries; `engine_courses.c` rows.
- The road work arena in high work RAM, `FZX_ROAD_STREAM_HIGH_BYTES` (default 98,304; the select image 131,072): Silence 2 filled
  the 98,304 B (`fzx_road_stream_high_used` 98,000 at the failure, "TRACK LOAD" 35 with status 2). The extra 32,768 B come out of
  the free gap between the end of `.bss` and the top of the master stack in high work RAM (the whole image is in high work RAM):
  that gap is 59,264 B on the select image at the engine pin `cc18a63` (63,456 B at `8d7773e`; the pin bump grew the image by 4,192 B,
  text and data 4,064, bss 128), and was 92,032 B with the 98,304 B arena. Measured at the end of 24 runs the master stack's
  untouched part is 53,444..53,464 B (52,184 at Mute City 3 and White Land 1), 33 KB above its 20,480 B floor. The arena peak,
  `fzx_road_stream_high_used`, is in the table (largest: Big Hand 109,072, Space Plant 108,584, Rainbow Road 108,408, Sand Ocean 2
  108,208, Silence 2 101,008): 22,000 B of headroom inside the 131,072 B arena at the worst course. The gadget lane's decals share
  the arena: Mute City needs +3.9 KB on 87,360 B and Devil's Forest more on 88,560 B; both fit, as the arena has 43,712 B and
  42,512 B free there. Growing the arena past about 33 KB more reaches the master stack floor.
- `E1_RACE_TICKS` (default 9,000 ticks after the start): Rainbow Road's finish is at 10,520 ticks; the image `scenes-drv-rr` runs
  12,000 and its HOST reference the same (`host_ticks`).
- `E1_DRIVER_KL` and `E1_DRIVER_TARGET` (HOST and test driver only): the R01-t driver `kl,kh,0,460,target` of the courses the
  nominal driver retires on (Fire Field 0.004/2.5/90, Red Canyon 2 0.008/3.5/-30, Space Plant 0.004/3.5/30, Port Town 2
  0.004/3.5/-90). Mute City 3 runs under R03 (look 0.1) like White Land 1. Big Hand: no R01 or R03 arm finishes it (it leaves
  the borderless road at segment 9); the gate checks that the engine retires at the HOST's tick, 1,460. The original's
  attract demo (`game.c` `sTitleDemoCourses`) never plays Big Hand; the demo is the CPU AI driving the player.
- Tunnel spans take the road plan's outer bit the pipe does (`fzx_drive_outer`): `TRACK_FLAG_INSIDE` is on the pipe and tunnel
  presets only, so the half-pipe stays 1. Look not checked (no still pair).
- SH2ZAM `vehicle-dynamics-5` (PR 78): `sh2z_vd_step2` models the half-pipe (White Land 2's trough). R01 native finish against the
  original code was +5.885% without it and is +0.023% with it.
- Gate: `host_ticks` per course, matrix images `scenes-drv-ff`, `-rc2`, `-sp`, `-pt2`, `-rr`, rows for courses 12..23.

**Death Race (card D).** The original has no field header for the mode: `Racer_Init` runs the same `Cpu_InitRacer` per racer
as the Grand Prix (30 racers; `racer.c` 2173-2236, `cpu.c` 474-540). Differences it makes: course 54 is past `COURSE_EDIT_1`, so
the CPU line is the default (speed 69, lateral 0 at every half segment) and there are no pace tables; every rival's energy and
maximum energy are halved (`racer.c` 2230); the rival speed ratio `unk_1A8` is random 0..0.2 (`cpu.c` 1097). `fzx-gp-ref --mode death`
(`ref/rivals_bake.c`: `GAMEMODE_DEATH_RACE`, `gCourseIndex` 54, the original's tables read at the course's own index) builds the
reference field from the original code: grid 1..30, no lap target, no chase, max energy halved (player 178, rivals 71 to 89),
30 vehicles. In the HOST run all 29 rivals retire between ticks 738 and 835 after the start and the race ends with the player
alive (`racer.c` 979: all CPUs retired). `rivals_finish.py` gives no second-lap target for course indexes of 30 and up. HOST
physics finishes at 36,275 ms equal to the original. Open: the energy drain and retirement of the rivals is a mode (a card),
not field data, and the native field does not have it; the select disc cannot hold Death Race either:
`FZX_COURSE_BUNDLE_MASK` is a 32-bit mask and the id is 54.

**Result (EMULATOR, Ymir; timing MODELLED), gate `g24new` at engine pin `cc18a63`, head `0a1e312`.** All 24 courses: HOST finish
equal to the millisecond and every checkpoint equal (24 of 24; Big Hand and Port Town 1 retire at the HOST's tick), 0 intervals
over two fields at 23 courses. Fire Field: cadence FAIL, 6 intervals of 3F+ (worst 4F, critical max 68.59 ms), card open in the
SH2ZAM locate-hint lane (the airborne locate search), not fixed here. The margin check (critical p99 x 1.36 against 33.3 ms)
fails at 17 of 24 courses and passes at 7 (reported, not tuned); the Jack and Queen six are in that count. White Land 2 on the
real pin: 82,335 ms equal to HOST (+5.885% before the half-pipe).

| Course | Host: finish vs HOST (checkpoints) | Intervals 1F/2F/3F+ | Critical mean / p99 / max ms | Master p99 | Slave p99 | VDP1 p99 | Cold LOAD fields | p99 x 1.36 ms (limit 33.3) | Stack unused B | Road arena peak B |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| fire-field | finish 100521 ms, HOST 100521 (checkpoints 26/26 equal) | 0/3142/6 FAIL | 20.11 / 25.19 / 68.59 | 24.5 | 21.52 | 14.61 | 314 | 34.2579 FAIL | 53,444 | 82,560 |
| silence-2 | finish 117586 ms, HOST 117586 (checkpoints 30/30 equal) | 0/3664/0 | 20.82 / 25.53 / 26.79 | 25.07 | 22.69 | 13.07 | 349 | 34.7256 FAIL | 53,464 | 101,008 |
| sector-beta | finish 125856 ms, HOST 125856 (checkpoints 32/32 equal) | 0/3912/0 | 20.77 / 26.17 / 28.53 | 24.56 | 24.06 | 8.91 | 352 | 35.5880 FAIL | 53,464 | 95,000 |
| red-canyon-2 | finish 117720 ms, HOST 117720 (checkpoints 30/30 equal) | 0/3668/0 | 20.6 / 25.11 / 26.94 | 24.7 | 22.67 | 10.29 | 341 | 34.1508 FAIL | 53,464 | 88,712 |
| white-land-2 | finish 82335 ms, HOST 82335 (checkpoints 22/22 equal) | 0/2607/0 | 21.14 / 25.11 / 27.24 | 24.49 | 22.69 | 9.62 | 321 | 34.1508 FAIL | 53,444 | 87,536 |
| mute-city-3 | finish 133428 ms, HOST 133428 (checkpoints 33/33 equal) | 0/4139/0 | 21.04 / 24.49 / 26.66 | 23.99 | 20.84 | 12.53 | 316 | 33.3038 FAIL | 52,184 | 81,456 |
| rainbow-road | finish 167651 ms, HOST 167651 (checkpoints 42/42 equal) | 0/5166/0 | 20.81 / 23.97 / 25.42 | 23.56 | 21.08 | 8.57 | 355 | 32.5969 PASS | 53,464 | 108,408 |
| devils-forest-3 | finish 97137 ms, HOST 97137 (checkpoints 25/25 equal) | 0/3051/0 | 20.1 / 25.31 / 28.36 | 24.17 | 22.67 | 10.21 | 324 | 34.4229 FAIL | 53,444 | 83,720 |
| space-plant | finish 144276 ms, HOST 144276 (checkpoints 36/36 equal) | 0/4465/0 | 20.47 / 24.7 / 27.2 | 24.02 | 22.2 | 9.1 | 379 | 33.5953 FAIL | 53,444 | 108,584 |
| sand-ocean-2 | finish 120334 ms, HOST 120334 (checkpoints 30/30 equal) | 0/3747/0 | 20.38 / 25.26 / 27.28 | 24.63 | 22.43 | 12.7 | 360 | 34.3497 FAIL | 53,464 | 108,208 |
| port-town-2 | finish 131043 ms, HOST 131043 (checkpoints 33/33 equal) | 0/4068/0 | 20.25 / 23.88 / 24.84 | 23.59 | 21.48 | 10.08 | 315 | 32.4752 PASS | 53,464 | 89,336 |
| big-hand | retires as HOST (tick 1460) (checkpoints 6/6 equal) | 0/657/0 | 20.01 / 24.08 / 24.7 | 23.19 | 21.37 | 8.92 | 359 | 32.7531 PASS | 53,464 | 109,072 |
| mute-city-1 | finish 104040 ms, HOST 104040 (checkpoints 27/27 equal) | 0/3258/0 | 21.33 / 25.03 / 27.26 | 24.58 | 22.49 | 12.21 | 322 | 34.0389 FAIL | 53,464 | 87,360 |
| silence-1 | finish 90563 ms, HOST 90563 (checkpoints 23/23 equal) | 0/2853/0 | 20.42 / 24.93 / 25.74 | 24.81 | 21.87 | 8.79 | 323 | 33.9060 FAIL | 53,464 | 73,472 |
| sand-ocean-1 | finish 92553 ms, HOST 92553 (checkpoints 24/24 equal) | 0/2913/0 | 21.46 / 26.57 / 27.97 | 25.57 | 24.56 | 10.95 | 330 | 36.1345 FAIL | 53,464 | 82,184 |
| devils-forest-1 | finish 108038 ms, HOST 108038 (checkpoints 28/28 equal) | 0/3378/0 | 20.77 / 24.35 / 25.41 | 23.9 | 21.72 | 15.13 | 317 | 33.1186 PASS | 53,464 | 88,560 |
| big-blue-1 | finish 126928 ms, HOST 126928 (checkpoints 32/32 equal) | 0/3944/0 | 20.13 / 23.48 / 25.08 | 23.09 | 20.91 | 8.66 | 325 | 31.9351 PASS | 53,464 | 82,712 |
| port-town-1 | retires as HOST (tick 6328) (checkpoints 25/25 equal) | 0/3091/0 | 20.09 / 23.52 / 25.28 | 23.08 | 21.2 | 13.53 | 334 | 31.9867 PASS | 53,464 | 91,600 |
| sector-alpha | finish 98843 ms, HOST 98843 (checkpoints 25/25 equal) | 0/3102/0 | 19.78 / 24.92 / 26.52 | 24.68 | 22.2 | 10.76 | 334 | 33.8980 FAIL | 53,444 | 92,848 |
| red-canyon-1 | finish 97580 ms, HOST 97580 (checkpoints 25/25 equal) | 0/3064/0 | 19.41 / 22.78 / 26.98 | 22.28 | 20.19 | 9.35 | 272 | 30.9777 PASS | 53,464 | 65,808 |
| devils-forest-2 | finish 109503 ms, HOST 109503 (checkpoints 28/28 equal) | 0/3422/0 | 21.29 / 24.69 / 25.92 | 23.97 | 22.32 | 11.04 | 327 | 33.5799 FAIL | 53,464 | 88,824 |
| mute-city-2 | finish 97479 ms, HOST 97479 (checkpoints 25/25 equal) | 0/3061/0 | 21.7 / 29.53 / 31.38 | 24.79 | 27.44 | 11.79 | 328 | 40.1600 FAIL | 53,464 | 86,720 |
| big-blue-2 | finish 80008 ms, HOST 80008 (checkpoints 21/21 equal) | 0/2537/0 | 21.8 / 28.24 / 31.36 | 25.52 | 26.19 | 10.57 | 324 | 38.4060 FAIL | 53,464 | 86,992 |
| white-land-1 | finish 111775 ms, HOST 111775 (checkpoints 28/28 equal) | 0/3490/0 | 21.65 / 28.44 / 29.87 | 27.37 | 24.9 | 9.82 | 322 | 38.6814 FAIL | 52,184 | 86,952 |
