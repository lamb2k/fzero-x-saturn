# SH2ZAM view-guard-1 adoption

The port pins `view-guard-1`, commit
`623c3c804a6ba1e48a990020912de28a985d53a9`, after HOST qualification and an
isolated native comparison against `view-edges-1`. The pointer change is its
own commit. No SH2ZAM source was edited and no new road renderer is admitted.

GCC/Clang UBSan pass the upstream guard, view and edge suites plus the existing
port caller checks. Clang also passes the guard suite under ASan/UBSan. Each
compiler exercises 300,000 guard polygons, over one million output vertices,
0..4 interpolated attributes, reverse winding, in-place operation, overflow
and unchanged output on rejection. The upstream randomized counts differ
because C does not specify RNG-call argument evaluation order; each matches
its independent oracle. This is not a claim of identical randomized fixtures.

The existing 4,096-vertex port fixture remains byte-identical across both tags
and compilers, FNV `1cbb2030`. Its wrong endpoint-rounding and undersized-output
controls fail. The new operation reserves 16 outputs, accepts 3..8 inputs and
clips six planes before projection. Callers must choose a guard within the
VDP1 coordinate range and still qualify their own image-space mapping.

The final rounding clamp can move adversarial coarse-resolution vertices by
up to 26 pixels. The upstream bound is at most one pixel where a view-space
unit projects to at most 1/16 pixel. Passing the library suite does not waive
the port's one-pixel geometry or one-texel texture gates.

The native comparison uses the same 685-input schedule and 13 held views as
the prior adoption, with unchanged port C/header sources, scene assets,
settings and frozen road implementation. Both use Yaul `-O2`, Yaba Sanshiro
1.20.37, dynamic recompiler core 3, OpenGL core 1 and 4 MB cartridge.

| Frozen road CPU | view-edges-1 | view-guard-1 |
| --- | ---: | ---: |
| Mean, MODELLED | 112.033 ms | 112.037 ms |
| p99 / maximum of 13, MODELLED | 213.551 ms | 213.397 ms |

There is no meaningful measured speedup. All source-state and scene packets,
command hashes, texture/road/cache/machine guards and runtime-core checks pass.
The comparison does not finish a race or establish physical performance.
Private evidence: `host/view-guard-adoption/report.json`, `RESULTS.md`,
`native-comparison.json` and `emu/view-guard-adoption/after`.

The guard operation is available for new opt-in callers. C3 remains blocked
by the separate mapping experiments documented in [ROAD_STREAM.md](ROAD_STREAM.md).
