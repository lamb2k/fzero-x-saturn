#!/usr/bin/env bash
set -euo pipefail
FZX_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$FZX_ROOT"
SH2ZAM_DIR="${SH2ZAM_DIR:-third_party/SH2ZAM}"
test -f "$SH2ZAM_DIR/saturn/include/sh2zam/sh2zam.h" || {
  printf '%s\n' 'Initialize third_party/SH2ZAM before building.' >&2
  exit 1
}
"${CLANG:-clang}" --target=wasm32 -O2 -std=c11 -ffreestanding -fno-builtin \
  -fno-fast-math -ffp-contract=off -nostdlib -DSH2Z_FORCE_PORTABLE=1 \
  -Iweb/include -Iinclude -I"$SH2ZAM_DIR/saturn/include" \
  src/fzx.c src/wasm_support.c "$SH2ZAM_DIR"/saturn/src/*.c \
  -Wl,--no-entry -Wl,--export-memory -Wl,--initial-memory=2097152 \
  -Wl,--max-memory=2097152 -Wl,-z,stack-size=65536 \
  -Wl,--export=fzx_init -Wl,--export=fzx_step -Wl,--export=fzx_render \
  -Wl,--export=fzx_course_buffer -Wl,--export=fzx_activate_course \
  -Wl,--export=fzx_use_demo -Wl,--export=fzx_set_transform_mode \
  -Wl,--export=fzx_sim_id \
  -o web/fzero.wasm
python3 - <<'PY'
import base64,hashlib,json,pathlib
data=pathlib.Path('web/fzero.wasm').read_bytes()
pathlib.Path('web/fzero-module.js').write_text(
    '// Generated from the shared C core.\n'
    'window.FZX_WASM_BASE64='+json.dumps(base64.b64encode(data).decode())+';\n'
    'window.FZX_WASM_SHA256='+json.dumps(hashlib.sha256(data).hexdigest())+';\n')
print('Built web/fzero.wasm and self-contained loader ('+str(len(data))+' bytes).')
PY
