# Source scene state on SH-2

This synthetic probe checks the backdrop and skyline state helpers on the master
SH-2. It does not draw the source scene, load retail content or qualify the game
recipe. The ordinary Saturn image is unchanged.

Build both HOST oracles from the repository root (WSL with gcc-multilib/Clang):

```sh
python3 tools/camera_assets.py --art fixture
FZX_SOURCE_CAMERA=1 FZX_BACKDROP_STATE=1 bash ref/build.sh gcc -O2
FZX_SOURCE_CAMERA=1 FZX_BACKDROP_STATE=1 bash ref/build.sh clang -O2
python3 tools/scene_probe/prepare.py
python3 tools/backdrop_state.py
```

The preparation step checks the host build manifests, compares GCC/Clang and a
repeat, and receipts all 256 expected state pairs. Input cameras are authored in
`ref/scene_probe.c`; they exercise yaw, tilt, viewport offsets, cloud heights,
pause and repeated vertex pools. Hashes use numeric fields in canonical byte
order, including signed vertices, source-rounded matrices, UVs, colors and floats.

With the standard Yaul image already mounted at `/work`, build the native probe:

```sh
chroot "$HOME/yaul-image" /bin/bash /work/tools/scene_probe/inner.sh
```

On Windows, configure `FZX_YABA_EXE` for the local MCP-enabled emulator and run:

```powershell
tools/emu/scene_probe.ps1 -Iso tools/scene_probe/fzx-scene-probe.iso `
  -Sym tools/scene_probe/build/fzx-scene-probe.sym -OutDir $evidence -Visible
```

`$evidence` must be outside this repository. The wrapper rejects incomplete runs,
wrong case counts, any mismatch and incorrect final state hashes. It records the
image, symbols, emulator and oracle hashes. Timing is not measured here.

The negative arm is built with `FZX_SCENE_CONTROL=1` in the chroot environment,
then checked with `-ExpectedBad 256`. It flips one backdrop hash bit for every
case, so the same gate must reject all 256. Rebuild the normal arm afterward.
Generated headers, objects, discs and results are ignored. HARDWARE remains unrun.
