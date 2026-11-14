"""Generation receipts for files in generated/ (card G0.3).

A receipt sits beside its primary output as `<output>.receipt`. It lists
`sha256  path` for the generator script and every output, in the format
`sha256sum --check` reads, with notes on lines starting with '#'. Paths are
relative to the repository root. tools/check_receipt.sh verifies one; builds
call it and refuse stale or hand-edited output.
"""
from __future__ import annotations

import hashlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def _rel(path: Path) -> str:
    resolved = path.resolve()
    try:
        return resolved.relative_to(ROOT).as_posix()
    except ValueError:
        return resolved.as_posix()


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_receipt(primary: Path, generator: Path, outputs: list[Path],
                  notes: dict[str, str]) -> Path:
    """Write `<primary>.receipt` after every output has been written."""
    lines = ["# fzx generation receipt v1"]
    lines += [f"# {key}: {value}" for key, value in sorted(notes.items())]
    for path in (generator, *outputs):
        lines.append(f"{_sha256(path)}  {_rel(path)}")
    receipt = primary.with_name(primary.name + ".receipt")
    receipt.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return receipt
