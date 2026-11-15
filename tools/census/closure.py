#!/usr/bin/env python3
"""G1.1 closure census: every decomp function reachable from the race tick.

Preprocesses the pinned decomp as stock US rev0 (PORT and EXPANSION_KIT left
undefined, so the fork's hooks compile out), parses function definitions and
direct calls, and walks the call graph from the roots.

Static analysis only. The float column is a lexical signal (float literals,
f32 declarations, float math calls), not an operation count; G1.3 counts
operations from an instrumented reference run. Calls through function
pointers are flagged, not followed.

Usage (needs gcc; run in WSL or Linux):
  python3 tools/census/closure.py --out docs/census
  python3 tools/census/closure.py --define PORT --out /tmp/census-port   # the G1.1 control
"""
from __future__ import annotations

import argparse
from collections import defaultdict, deque
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
DECOMP = ROOT / "upstream/G-Diffuser/decomp"
SOURCES = ["src/game/*.c", "src/sys/math.c", "src/sys/controller.c", "src/overlays/ovl_i2/race.c"]
DEFINES = ["_LANGUAGE_C", "LANGUAGE_C", "F3DEX_GBI_2", "NDEBUG", "VERSION_US=1",
           "BUILD_VERSION=VERSION_I", "ASSET_VERSION=us", "ASSET_REVISION=rev0",
           "LEO_VERSION=LEO_VERSION_A", "MFS_VERSION=MFS_VERSION_A", "NON_MATCHING", "AVOID_UB",
           "_MIPS_SZLONG=32", "_MIPS_SZINT=32"]
INCLUDES = ["include", "include/libultra", "include/libc", ".", "src"]
FORK_MARK = re.compile(r"\b(PORT|EXPANSION_KIT|Gdx\w*|gdx_\w+|CVar\w*)\b")
KEYWORDS = {"if", "for", "while", "switch", "return", "sizeof", "do", "else", "case", "goto"}
FLOAT_CALLS = re.compile(r"\b(sqrtf|sinf|cosf|fabsf|atan2f|Math_\w+|SIN|COS)\s*\(")
FLOAT_LIT = re.compile(r"\b\d+\.\d*f?\b|\b\d+f\b")
GLOBAL = re.compile(r"\b((?:g|s)[A-Z]\w*|D_[0-9A-Fa-f]{6,}\w*)\b")
WRITE_AFTER = re.compile(r"\s*(?:\[[^\]]*\]|\.\w+|->\w+)*\s*(?:[-+*/|&^]?=(?!=)|\+\+|--)")
DEF_HEAD = re.compile(r"(?:^|[\s*])([A-Za-z_]\w*)\s*\(([^;{}]*)\)\s*$", re.S)
LINEMARK = re.compile(r'^# (\d+) "([^"]+)"')
RENDER = re.compile(r"Draw|Gfx|Mtx|Matrix_Set|Audio|Sfx|Sound|Hud|Effect|Font|Texture|Display|Print|Fault|Gfx")
CPU_ONLY = re.compile(r"Cpu|Ai\b|AI|Ghost|Replay|Demo|Record")


def preprocess(path: Path, stub: Path, extra: list[str]) -> str:
    args = ["gcc", "-E", "-undef", "-nostdinc"] + [f"-D{d}" for d in DEFINES + extra]
    args += [f"-I{DECOMP / i}" for i in INCLUDES] + [f"-I{stub}", str(path)]
    for _ in range(200):
        run = subprocess.run(args, capture_output=True, text=True)
        missing = re.search(r"fatal error: ([^:\s]+): No such file", run.stderr)
        if run.returncode == 0:
            return run.stdout
        if not missing:
            raise SystemExit(f"preprocess {path}: {run.stderr[-2000:]}")
        header = stub / missing.group(1)  # Generated asset headers: empty stand-ins are enough here.
        header.parent.mkdir(parents=True, exist_ok=True)
        header.touch()
    raise SystemExit(f"preprocess {path}: too many missing headers")


def parse(text: str) -> list[dict]:
    """Function definitions with their original file and line range."""
    funcs, depth, i, head_start = [], 0, 0, 0
    lines, cur_file, cur_line = [], "?", 0
    for raw in text.split("\n"):  # Map every output line to its source position.
        mark = LINEMARK.match(raw)
        if mark:
            cur_line, cur_file = int(mark.group(1)), mark.group(2)
            lines.append(("", cur_file, -1))
            continue
        lines.append((raw, cur_file, cur_line))
        cur_line += 1
    flat, pos = [], []
    for raw, f, ln in lines:
        flat.append(raw + "\n")
        pos.append((f, ln))
    starts, acc = [], 0
    for chunk in flat:
        starts.append(acc)
        acc += len(chunk)
    src = "".join(flat)

    def where(offset: int):
        lo, hi = 0, len(starts) - 1
        while lo < hi:
            mid = (lo + hi + 1) // 2
            if starts[mid] <= offset:
                lo = mid
            else:
                hi = mid - 1
        return pos[lo]

    n = len(src)
    while i < n:
        c = src[i]
        if c in "\"'":  # Skip string and character literals.
            q, i = c, i + 1
            while i < n and src[i] != q:
                i += 2 if src[i] == "\\" else 1
        elif c == "{":
            if depth == 0:
                head = src[head_start:i]
                m = DEF_HEAD.search(head)
                if m and m.group(1) not in KEYWORDS and "=" not in head.split(m.group(1))[0] \
                        and not re.search(r"\b(struct|union|enum)\s+\w*\s*$", head.split(m.group(1))[0]):
                    end, d = i, 0
                    while end < n:
                        if src[end] == "{":
                            d += 1
                        elif src[end] == "}":
                            d -= 1
                            if d == 0:
                                break
                        end += 1
                    f0, l0 = where(head_start + m.start(1))
                    _, l1 = where(end)
                    funcs.append({"name": m.group(1), "file": f0, "line": l0, "end": l1,
                                  "body": src[i:end + 1]})
                    i, head_start = end + 1, end + 1
                    continue
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                head_start = i + 1
        elif c == ";" and depth == 0:
            head_start = i + 1
        i += 1
    return funcs


TABLE = re.compile(r"\(\s*\*\s*(\w+)\s*\[[^\]]*\]\s*\)\s*\([^)]*\)\s*=\s*\{([^}]*)\}"
                   r"|\b(\w+)\s*\[[^\]]*\]\s*=\s*\{([^{}]*)\}")


def tables(text: str) -> dict[str, list[str]]:
    """Global initialised arrays, by name, with the identifiers they list (function tables)."""
    found = {}
    for m in TABLE.finditer(text):
        name, init = (m.group(1), m.group(2)) if m.group(1) else (m.group(3), m.group(4))
        found.setdefault(name, re.findall(r"\b([A-Za-z_]\w*)\b", init))
    return found


def analyse(funcs: list[dict], tabs: dict[str, list[str]]) -> dict:
    by_name = {}
    for f in funcs:
        by_name.setdefault(f["name"], f)  # Static inline copies from headers repeat; keep the first.
    fn_tables = {t: [e for e in entries if e in by_name] for t, entries in tabs.items()}
    fn_tables = {t: e for t, e in fn_tables.items() if e}
    for f in by_name.values():
        body = f["body"]
        called = set(re.findall(r"\b([A-Za-z_]\w*)\s*\(", body)) - KEYWORDS
        # A function table used in the body may dispatch to any of its entries: follow them all.
        via = {t for t in fn_tables if re.search(rf"\b{t}\b", body)}
        f["tables"] = sorted(via)
        called |= {e for t in via for e in fn_tables[t]}
        # Functions named without a call (stored as callbacks) are followed too.
        called |= {w for w in re.findall(r"\b([A-Za-z_]\w*)\b", body) if w in by_name and w != f["name"]}
        f["calls"] = sorted(c for c in called if c in by_name and c != f["name"])
        f["external"] = sorted(c for c in called if c not in by_name and not re.match(r"^(f32|s32|u32|s16|u16|s8|u8|f64)$", c))
        f["indirect"] = bool(re.search(r"(->\w+|\]\s*)\s*\(|\(\s*\*\s*\w+\s*\)\s*\(", body))
        reads, writes = set(), set()
        for m in GLOBAL.finditer(body):
            (writes if WRITE_AFTER.match(body, m.end()) or body[max(0, m.start() - 2):m.start()] in ("++", "--")
             else reads).add(m.group(1))
        f["reads"], f["writes"] = sorted(reads - writes), sorted(writes)
        f["float_signal"] = (len(FLOAT_LIT.findall(body)) + len(FLOAT_CALLS.findall(body))
                             + len(re.findall(r"\bf32\b", body)))
    return by_name


def closure(by_name: dict, roots: list[str]) -> dict[str, int]:
    seen, queue = {}, deque((r, 0) for r in roots if r in by_name)
    while queue:
        name, depth = queue.popleft()
        if name in seen:
            continue
        seen[name] = depth
        queue.extend((c, depth + 1) for c in by_name[name]["calls"])
    return seen


def source_marks(f: dict) -> tuple[int, bool]:
    """Fork-hook sites and NON_MATCHING marker inside the function's original line range."""
    path = DECOMP / f["file"] if not Path(f["file"]).is_absolute() else Path(f["file"])
    try:
        lines = path.read_text(errors="replace").splitlines()
    except OSError:
        return 0, False
    seg = lines[max(0, f["line"] - 1):f["end"]]
    before = lines[max(0, f["line"] - 4):f["line"] - 1]
    return (sum(1 for ln in seg if FORK_MARK.search(ln)),
            any("NON_MATCHING" in ln for ln in before + seg))


def classify(name: str, f: dict, tick: dict, init: dict) -> tuple[str, str]:
    if name in tick:
        if RENDER.search(name):
            return "OUT", "presentation (drawing, audio, HUD or effects); the native layer owns it"
        if CPU_ONLY.search(name):
            return "OUT", "not reached by one human racer in a plain race (CPU, ghost, replay or demo)"
        return "TICK", "per-tick simulation for the racer, track query or race state"
    if name in init:
        if RENDER.search(name):
            return "OUT", "presentation set-up"
        return "OFFLINE", "race initialisation; run in the reference at import time and bake the result"
    return "OUT", "not reachable from the roots"


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--define", action="append", default=[], help="Extra macro, e.g. PORT (the control)")
    ap.add_argument("--tick-root", action="append", default=["Racer_Update"])
    ap.add_argument("--init-root", action="append", default=["Race_Init"])
    args = ap.parse_args(argv)
    if not (DECOMP / "src").is_dir():
        raise SystemExit("decomp submodule missing; see docs/HANDOFF.md")

    funcs, tabs = [], {}
    with tempfile.TemporaryDirectory() as stub:
        for pattern in SOURCES:
            for path in sorted(DECOMP.glob(pattern)):
                text = preprocess(path, Path(stub), args.define)
                funcs += parse(text)
                for name, entries in tables(text).items():
                    tabs.setdefault(name, entries)
    by_name = analyse(funcs, tabs)
    tick = closure(by_name, args.tick_root)
    init = closure(by_name, args.init_root)
    rows = []
    for name in sorted(set(tick) | set(init), key=lambda n: (by_name[n]["file"], by_name[n]["line"])):
        f = by_name[name]
        fork, nonmatch = source_marks(f)
        try:  # Repository-relative paths only: absolute ones leak the local home folder.
            f["file"] = Path(f["file"]).resolve().relative_to(DECOMP).as_posix()
        except ValueError:
            f["file"] = Path(f["file"]).name
        tier, reason = classify(name, f, tick, init)
        rows.append({"function": name, "file": f["file"], "line": f["line"], "lines": f["end"] - f["line"] + 1,
                     "root": "tick" if name in tick else "init", "depth": tick.get(name, init.get(name)),
                     "tier": tier, "float_signal": f["float_signal"], "indirect": int(f["indirect"]),
                     "non_matching": int(nonmatch), "fork_sites": fork,
                     "fork_calls": ",".join(c for c in f["external"] if FORK_MARK.search(c)),
                     "tables": ",".join(f["tables"]),
                     "calls": ",".join(f["calls"]), "external": ",".join(c for c in f["external"] if not FORK_MARK.search(c)),
                     "reads": ",".join(f["reads"]), "writes": ",".join(f["writes"]), "reason": reason})
    args.out.mkdir(parents=True, exist_ok=True)
    cols = list(rows[0])
    with open(args.out / "closure.tsv", "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\t".join(cols) + "\n")
        for r in rows:
            fh.write("\t".join(str(r[c]).replace("\t", " ") for c in cols) + "\n")
    tiers = defaultdict(lambda: [0, 0])
    for r in rows:
        tiers[r["tier"]][0] += 1
        tiers[r["tier"]][1] += r["lines"]
    fork_calls = sorted({c for r in rows for c in r["fork_calls"].split(",") if c})
    print(f"functions parsed: {len(by_name)}; tick closure: {len(tick)}; init closure: {len(init)}")
    for t, (count, lines) in sorted(tiers.items()):
        print(f"  {t}: {count} functions, {lines} lines")
    print(f"fork hook calls left in the closure: {len(fork_calls)} {' '.join(fork_calls[:12])}")
    print(f"NON_MATCHING in closure: {sum(r['non_matching'] for r in rows)}; "
          f"indirect calls: {sum(r['indirect'] for r in rows)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
