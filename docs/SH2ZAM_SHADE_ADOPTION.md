# SH2ZAM shade-1 qualification

The pin is `shade-1`, commit
`624ae5ae9800daa928fb1364ba3e9a85147ccabc`, from `view-guard-src-1`
`e93dd4b9629fdf3bbe081ed1ed8bd1b663a56c46`. Qualification uses synthetic
fixtures only, in isolated before/after trees. Pointer-only port commit: `463f7c1`. Caller integration is separate.
All 32 existing library source/header files are byte-identical across the pin.

## HOST evidence

GCC and Clang strict-warning, UBSan builds each pass 57,720,452 shade checks
against the library's independent 64/128-bit oracle. This includes exhaustive
light-channel, Gouraud and fade arithmetic; random strided lighting/culling;
threshold fog boundary checks; rejected-argument no-write checks; and 600
synthetic vehicle pipelines. All ten deliberate mutation controls are detected.
These are local reruns of the upstream oracle suite, not a new independent oracle.

All seven selected view/divider/edge/guard/merge/round/source-guard regression
suites pass before and after under GCC and Clang, with strict warnings and
UBSan. The shade suite is run separately under both compilers as above.

Independent synthetic byte recorders cover ten original and ten truncating
view APIs over 2,048 views each. Each emits 28,868,608 bytes; all four
before/after GCC/Clang outputs agree exactly within each API family:

- Original: `934b8bc04fcd30f776e7153a2b92178009cc7254d03635f2646f8a6ede2fb242`.
- Truncating: `a1d06ae1ece43b72768761f4ad7ca1bfd5b35bc899cbf488134ab39411a840bf`.

Synthetic road command fixtures (regular and shifted) pass before and after,
including period, capacity and fog mutation controls. The camera adapter also
passes with the working-tree pointer selected at shade-1. No port caller code
was changed for these checks; unrelated local WIP was held constant.

Regression qualification is recorded in the private evidence directory
`host/shade-adoption/`: source-identity.json, shade-result.json, compiler logs,
API byte recorders and synthetic port fixtures. No retail inputs are used.

## Integration constraints

No port caller selects these kernels yet. Batch per strip or frame, rather than
per piece, to amortize argument checks. Validate planes and order indices at
load; culling requires absolute eye coordinates <=30000 and eye_error <=1024.
Preserve threshold fog: a plain z-shift table is not equivalent. Reuse light
results only while every light/reflection/basis dependency remains unchanged;
Gouraud reuse must also account for base colours and fog.

Prefer native 16-byte planes and normal indices over per-frame byte decoding.
Measure unique palette/normal pairs against drawn corners before baking them.
Any caller arm must remain default-off and pass source arithmetic, output,
rejection and disabled-arm identity gates before native frame comparisons.

Upstream synthetic SH-2 cycle estimates are MODELLED, not a measured port
speedup. No emulator or console qualification was run for this pin. C4's 1 ms
target and C3/C4 whole-frame benefit remain unestablished. Assembly work needs
a measured dominating loop first. The separate Q16-centre contract is still open.
