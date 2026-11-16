# Credits and source provenance

This independent Saturn project uses [G-Diffuser](https://github.com/Zorkats/G-Diffuser)
as its selected upstream. G-Diffuser's original port work is MIT licensed,
copyright Vicente Mediano (2026); its component notices remain in the pinned
upstream submodule. Its decompilation dependency is
[Zorkats/fzerox](https://github.com/Zorkats/fzerox), derived from the
[inspectredc F-Zero X decompilation](https://github.com/inspectredc/fzerox).
The decompilation declares CC0. Exact source identities are in
[`upstream.lock.json`](upstream.lock.json).

The course importer and fixed-point spline implementation adapt the following
decompilation algorithms/data layouts at commit
`b944a4135409d24979f5c74b001befab8345761a`:

- `include/fzx_course.h`: `CourseData` and `ControlPoint` layouts.
- `assets/yaml/us/rev0/course_data.yaml`: US rev0 course offsets.
- `src/game/course.c`: `Course_SplineGetPosition`, `Course_SplineGetTangent`,
  `Course_SplineCalculateTensions` and `func_8009DB28`.
- `src/game/EA90.c`: `func_80074CE4` segment up-vector construction.
- `include/fzx_math.h`: the original 4096-step angle conversion.
- `src/overlays/ovl_i3/hud.c`: single-player timer, speed and lap layout/glyph
  selection, adapted in `src/fzx_hud.c` and tested against the pinned functions.
  Native atlases use local ROM extraction; no retail pixels are committed.

The current temporary renderer uses a sampled flat road ribbon and approximate
polyline arc lengths. It does not reproduce all original course surface types,
features, textures or scenery. The original racer-code route is separate from
this presentation layer. The port plan requires source models, materials, UI
and audio through local conversion; the temporary renderer is not that endpoint.

[SH2ZAM](https://github.com/lamb2k/SH2ZAM) supplies fixed-point scalar, vector,
trigonometric and prepared batch-transform code under MIT. SH2ZAM in turn credits
[SH4ZAM](https://github.com/gyrovorbis/sh4zam), Falco Girgis and the SH4ZAM
contributors; their notices remain with that dependency.

The native backend uses [Yaul](https://github.com/yaul-org/libyaul)'s public APIs,
with API usage checked against the `vdp1-drawing` and `vdp1-mesh` examples.

New platform, prototype driving, browser, test and build code in this repository
is MIT licensed. The bundled test circuit and placeholder craft are original
fixtures. No Nintendo ROM, extracted retail course/model data or proprietary
audio is distributed. F-Zero X and its associated characters and assets belong
to Nintendo; this project is not affiliated with or endorsed by Nintendo, Sega,
the G-Diffuser maintainers or the original decompilation team.
