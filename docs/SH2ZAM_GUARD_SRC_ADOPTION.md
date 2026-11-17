# SH2ZAM view-guard-src-1 adoption

Pointer-only commit `24e4a2b` pins `view-guard-src-1` at
`e93dd4b9629fdf3bbe081ed1ed8bd1b663a56c46`. Its independent HOST
qualification completed before any port caller changed. No library source was
edited. The original renderer remains frozen.

The opt-in `sh2z_view_clip_guard_src_trunc[_divu]` contract preserves source
plane traversal, previous-to-current crossing arithmetic and inward lattice
snapping. Input corner order matters: matching order alone does not reproduce
the source crossing or snap. Whole-pixel centres are required by this API.
The port uses `max_out=10` and retains failure on any negative return.

GCC and Clang strict/UBSan builds pass six selected suites before the pin and
seven after it: view, divider, edges, guard, merge, rounding and the new
source-order guard suite. The latter reports 39,738,696 checks per compiler,
zero source-oracle mismatches, and detection of all ten deliberate controls,
including wrong order, crossing rule, omitted snap and ignored failures.

Independent recorders exercise ten original APIs and ten existing truncating
APIs over 2,048 synthetic views. Each recorder produces 28,868,608 bytes,
identical across both revisions and both compilers, including initialized output
buffers on failures. SHA-256:

- Original APIs: `934b8bc04fcd30f776e7153a2b92178009cc7254d03635f2646f8a6ede2fb242`.
- Truncating APIs: `a1d06ae1ece43b72768761f4ad7ca1bfd5b35bc899cbf488134ab39411a840bf`.

Five existing port view/adapter/edge/merge tests pass before and after. The
4,096-vertex caller retains hash `1cbb2030`. The same local core WIP was present
on both sides. No timing comparison is inferred from HOST test durations.
HOST `_divu` runs the portable fallback, not SH-2 divider assembly.

Private synthetic evidence and reproducible commands are under
`host/view-guard-src-adoption/`. This pin does not establish emulator, hardware
or native road performance acceptance.

## Separate caller qualification

The experimental road caller now selects the new API at both the textured-piece
guard and whole-quad area-cull guard. It preserves original quad order for
culling, and left-start/right-start/right-end/left-end for textured pieces.
The unchanged regular/+32 fixtures pass GCC and Clang with strict warnings
and UBSan. Period, capacity and fog mutation controls fail as required.
All 13 held images pass: texture displacement p99 0.650 px and geometry within
1 px. The integral census admits 9,684 of 9,907 views, identical across GCC
and Clang, with p99 413 commands. Forced-first-tier control gives p99 822,
failing the 420-command budget. Source state remains unchanged.

Private evidence: `host/road-stream-draw/source-guard-20261117/` and
`host/view-guard-src-adoption/caller-fixtures.log`. The 223 fractional-centre
views remain rejected pending the separate Q16-centre contract. No native path
selects this experimental command builder. Alignment, memory, native timing
and look qualification remain; no full-route or hardware pass follows.
