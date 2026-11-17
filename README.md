# F-Zero X Saturn

[![Build and validation](https://github.com/lamb2k/fzero-x-saturn/actions/workflows/build.yml/badge.svg)](https://github.com/lamb2k/fzero-x-saturn/actions/workflows/build.yml)

An experimental native Sega Saturn port foundation using
[G-Diffuser](https://github.com/Zorkats/G-Diffuser) as its source reference and
[SH2ZAM](https://github.com/lamb2k/SH2ZAM) for fixed-point mathematics.

**Target: the real F-Zero X on Saturn, preserving its original art and gameplay.**
The local `FZX_SIM=orig` build runs the original one-racer code on Mute City 1,
including a three-lap diagnostic replay and restart. Local experimental options draw the original
Blue Falcon, road, backdrop, entry/selection, race HUD and initial finish/results.
Rendering is far over budget; records/retry flow, audio and effects remain incomplete,
so this is not the finished game. Original models, textures,
scenery, effects, HUD/fonts, menus and audio are required port content.
The public default circuit and craft remain synthetic test fixtures.

[Open the web workbench](https://lamb2k.github.io/fzero-x-saturn/) ·
[Native build artifacts](https://github.com/lamb2k/fzero-x-saturn/actions/workflows/build.yml) ·
[Porting milestones](docs/PORTING.md) · [Credits](NOTICE.md)

## What is implemented

| Component | Current behavior |
|---|---|
| Source integration | G-Diffuser and SH2ZAM are pinned Git submodules. The exact decompilation revision used by G-Diffuser is recorded in `upstream.lock.json`. |
| Course import | Reads Mute City 1 from a locally supplied, verified US rev0 ROM, or a checksum-valid raw course record. Produces a fixed-point C header and local browser JSON. |
| Original course math | Adaptive spline tension is computed offline from the source algorithm. The C core evaluates its position/tangent polynomial with SH2ZAM and constructs banked road frames. |
| Native geometry | Sampled flat road ribbons, a chase camera, six-plane polygon clipping, depth ordering and VDP1 polygon commands. |
| Prototype driving | Fixed-step acceleration, braking, lateral steering, boundary slowdown, laps and reset. These are new test controls, not the original game's handling. |
| Original race route | `FZX_SIM=orig` links the original racer code with binary32 software float; local owner-ROM builds supply Mute City 1 and machine constants. Emulator whole-race finish times have matched host replays. Source-art integration remains open. |
| Source-art pipeline | Local verified export of five Blue Falcon LODs and 22 textures; native machine, road, backdrop and partial HUD behind explicit flags. [Coverage and build details](docs/SOURCE_ART.md). |
| Original selection | Original title, main menu, course selection and [machine/settings screens](docs/MACHINE_MENU.md) behind explicit flags. Source models, portraits, colors and engine settings are converted locally. |
| Finish/results | Original FINISH animation and initial lap/time/max-speed panel behind `FZX_SOURCE_RESULTS=1`; [source and native checks](docs/RESULTS.md). Records, name entry, ordinary retry and audio remain open. |
| Saturn platform | Yaul build, VDP1 drawing, VDP2 backgrounds, digital pad input and PAL/NTSC tick scheduling. |
| Web preview | The same C core compiled to WebAssembly, with keyboard/touch controls and local imported-course loading. |
| Validation | Independent spline oracle, driving/reset/ordering checks, exact prepared/general transform comparisons, importer checks, sanitizers and browser interaction tests. |

**Hardware performance is unmeasured.** The [whole-scene baseline](docs/FRAME_PERFORMANCE.md)
completes 9,907 replay inputs at **3.287 fps MODELLED**: 304.2 ms mean presentation
interval and 34,178 missed display fields. The original final race state is retained,
but the rendering pipeline needs substantial changes to reach the 60 fps target.
These emulator measurements do not establish physical Saturn performance.

The [road tier census](docs/ROAD_LOD_CENSUS.md) passes its HOST budget over all
9,907 views: p99 335 commands against a 420 limit, with the forced-full-detail
control at 818. The [source road bake](docs/ROAD_BAKE.md) fits its geometry and
texture budgets with all original rows/quads retained. Native stream integration
and speed/image checks come next.

The latest [original-floor experiment](docs/FLOOR_PERFORMANCE.md) moves it onto
VDP2 RBG0. A/B/A held-view checks reduce floor/cloud CPU work from 74.12 to
56.62 ms MODELLED, while the complete 9,907-input replay retains its finish state.
The rendering budget and final appearance review remain open.

## Next

First finish a faithful Mute City 1 slice with the original Blue Falcon,
track materials, scenery, race HUD, camera and audio at the 60 fps target,
requiring the 4 MB RAM cartridge. Then expand to the original game's content
and modes. Preserve source art through local native conversion; placeholders
and texture-off builds cannot satisfy the port milestones.
See the [port plan](docs/POC_PLAN.md) and current [status](docs/STATUS.md).

## Get the project

```sh
git clone https://github.com/lamb2k/fzero-x-saturn.git
cd fzero-x-saturn
git submodule update --init third_party/SH2ZAM
```

The native/workbench build needs only SH2ZAM. To inspect the selected game source:

```sh
git submodule update --init upstream/G-Diffuser
git -C upstream/G-Diffuser submodule update --init decomp
```

G-Diffuser's desktop runtime, libultraship, OpenGL and SDL are not linked into the
Saturn executable. The port builds a native SH-2/VDP1 backend around reusable
course algorithms and, in later milestones, game logic and locally extracted data.

## Run the workbench

The [hosted workbench](https://lamb2k.github.io/fzero-x-saturn/) starts paused.
Use the arrow keys or **W/A/S/D** to accelerate, steer and brake; **R** resets and
**Escape** pauses. Touch controls are available on small screens. Speed is shown
in prototype units.

To build locally, install Clang with WebAssembly support and LLD:

```sh
make web
python3 -m http.server 8000 --directory web
```

Open `http://localhost:8000`. The generated loader also allows opening
`web/index.html` directly. Imported JSON stays in the browser; no game data is
uploaded to a server.

The transform selector switches between SH2ZAM's general and prepared batch
implementations at the same pose. Both are checked for exact output equality.
It does not compare against the original game's floating-point code and makes
no speedup claim.

## Build for Sega Saturn

Use the same pinned Yaul toolchain image as SH2ZAM:

```sh
docker run --rm -v "$PWD:/work" -w /work \
  ijacquez/yaul@sha256:b6d272204767cd0a734139dd3a2ab298ef5912fb7b7224b78e6d9fac1a7825a6 \
  /bin/bash -c 'export PATH="$YAUL_INSTALL_ROOT/bin:$PATH"; make -C saturn'
```

The build produces `saturn/fzero-x-saturn.iso`, its CUE and an ELF with symbols.
The `fzero-x-saturn-native` Actions artifact contains these outputs plus the
compiler, architecture, size and image-hash evidence. Use the CUE with a Saturn
emulator or an appropriate hardware loading setup.

**Saturn controls:** D-pad steers, A or C accelerates, B brakes, Start resets.
See [native build details](saturn/README.md).

## Import a real course

The source repository does not supply the original game data. Supply your own
unmodified **F-Zero X US rev0** dump; `.z64`, `.v64` and `.n64` byte orders are
recognized and normalized before verifying the complete ROM fingerprint.

```sh
python3 tools/import_course.py --rom /path/to/F-Zero-X.z64 \
  --output generated/course.h
```

This creates `generated/course.h`, `generated/course.json` and a
`generated/course.receipt` of their hashes; the imported Saturn build refuses
a header that no longer matches its receipt. The importer
validates the ROM version, original course checksum, geometry and numeric
ranges before writing. The supported retail extraction is Mute City 1.

- **Browser:** choose **Load course**, then select the generated JSON.
- **Saturn:** run `make -C saturn clean` and rebuild with `FZX_IMPORTED=1`.
- **Raw course record:** use `--record /path/course.bin --byte-order big` instead
  of `--rom`; this validates the record but cannot establish its ROM provenance.

The default build always uses the test circuit, even when an imported header is
present. Generated data and ROM/archive formats are ignored by Git.
Course features are preserved as import metadata, but pits, boosts, hazards,
tubes, cylinders and scenery are not implemented by this first road renderer.

Retail extraction has been tested locally on a verified owner-supplied ROM.
Public CI uses synthetic records; retail tests skip unless `FZX_TEST_ROM` is
configured locally. Extraction checks do not establish full visual fidelity.

## Test

```sh
python3 tools/hygiene_scan.py   # before every push; this repository is public
make test
make sanitize
make web
python3 web/test-browser.py   # requires Playwright and its Chromium install
```

The C tests remain active with `NDEBUG`. The browser test drives and resets the
actual WebAssembly core, compares the two transform paths, checks local import
and rejection, exercises touch controls and saves desktop/mobile screenshots.
GitHub Actions also compiles and links the actual big-endian SH-2 executable.

## Project relationship

This is a separate game project, linked from
[SH2ZAM's Showcase](https://github.com/lamb2k/SH2ZAM#showcase).
SH2ZAM remains the reusable math dependency. Game-specific importer, rendering,
input, simulation and asset work belongs here.

Original project credit goes to **G-Diffuser / Vicente Mediano**, **inspectredc
and the F-Zero X decompilation contributors**, **SH2ZAM**, **Falco Girgis and the
SH4ZAM contributors**, and **Yaul's contributors**. See [NOTICE.md](NOTICE.md)
for source attribution and component licenses. New code is [MIT licensed](LICENSE).
