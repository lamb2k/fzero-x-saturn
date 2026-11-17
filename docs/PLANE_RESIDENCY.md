# Compact cloud residency

`FZX_PLANE_CLOUD_ONLY=1` retains only the original cloud half of `PLANES.PAK`.
It requires `FZX_SOURCE_PLANES=1` and `FZX_SOURCE_FLOOR_RBG=1`; the RBG0 floor
already supplies the original floor image. The flag defaults to zero.

The combined VDP1 atlas is 262,144 bytes. Its second 131,072-byte half contains
all eight cloud coverage variants and all four subdivision levels, totaling
680 charts. Compact generation retains those bytes exactly. The source floor,
cloud geometry, fades, command counts and shared command hashes are unchanged.
Only native texture addresses are rebased; subsequent road/machine allocations
move down by 131,072 bytes. Existing shade allocations keep their original size.
This is an allocation change, not a different cloud renderer or timing claim.

The native loader verifies the compact pack hash and size. Generated-mode and
resident-size compile assertions reject mismatched packs. Native emission
checks cloud identity, dimensions, alignment and the entire texture span before
rebasing an address. Invalid build combinations fail before SDK compilation.

HOST gates pass with GCC/Clang UBSan over all 680 charts. Regenerating the
flag-off payload, identity header and description reproduces the previous
outputs byte for byte. The compact payload equals the exact second half of the
combined atlas. Wrong-half, shifted-pack, wrong-mode/size and incorrect-address
controls fail. Private evidence: `host/plane-compact/verification.json` and
`build-controls.json`.

```sh
python3 tools/plane_assets.py --cloud-only
bash tools/check_receipt.sh generated/planes_cloud/planes.receipt
python3 tests/test_plane_compact.py
```

SH-2 builds pass. Flag-off whole-binary identity matches the admitted guard-tag
build. ISO comparison changes only `A.BIN` and `PLANES.PAK`; the other 13
payloads remain exact. See `host/plane-compact/native-build-verification.json`.

EMULATOR checks pass in the private Ymir software harness over 13 held views
through 685 original inputs. All 7,076 submitted command-list entries retain
their fields except the exact texture/palette/shade address rebase. Actual
referenced bytes, all scene packets, both VDP1 framebuffers and VDP2 floor
resources match. Composite differences are confined to two diagnostic CPU
number cells; original scene pixels match exactly. The cloud pack is verified
in actual VDP1 memory, and downstream road/machine allocations move down by
131,072 bytes. Guard/status checks pass. Evidence: `emu/plane-compact/comparison.json`.

Minimum untouched stack is 5,936 bytes, below C7's 16 KiB target (the matched
baseline is 6,076 bytes). This diagnostic is not a full race, pad-flow, physical
or final appearance acceptance, and supplies no FPS improvement claim.
HARDWARE remains unrun. Dual RBG1 cloud integration remains a separate unfinished
card; see [dual layers](DUAL_RBG.md).
