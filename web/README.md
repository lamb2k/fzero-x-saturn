# Web workbench

The browser displays polygons produced by the same C core used by the Saturn
target. It does not emulate Saturn hardware or report Saturn performance.
The bundled test circuit and prototype handling are not original F-Zero X
gameplay.

Build the WebAssembly module using the repository's web build target, then open
`index.html`. The generated `fzero-module.js` embeds the module so a downloaded
build also works from `file://`. A web server can serve `fzero.wasm` as a fallback.

## Controls

| Action | Keyboard | Touch |
|---|---|---|
| Accelerate | Up / W | Go |
| Brake | Down / S | Brake |
| Steer | Left / Right or A / D | Arrow buttons |
| Pause | Escape or Space while the canvas is focused | Pause |
| Reset | R or Reset | Reset |

The simulation advances in fixed 60 Hz steps. Browser rendering speed is not a
Saturn frame-rate measurement. Switching away from the page pauses the run.

## Course data

Use the source importer in `tools/import_course.py` to produce a version 1 JSON
file. **Load course** reads that file locally and packs its fixed-point records
into the shared core's upload buffer. Files are never uploaded. Invalid data is
rejected before activation. **Use test circuit** restores the included fixture.

## Validation

After generating the module, install Python Playwright and Chromium, then run:

```sh
python3 web/test-browser.py
```

This runs the actual WebAssembly, exercises driving/reset and local course
import, compares general and prepared transforms at a frozen pose, and checks
mobile controls and layout. Screenshots are saved to `build/web-test/`.
