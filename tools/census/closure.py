#!/usr/bin/env python3
"""Closure census of the pinned decomp for one racer (card G1.1).

Builds a static call graph of the decomp with libclang, configured as the reference
builds it: PORT and EXPANSION_KIT undefined, NON_MATCHING and AVOID_UB defined, US rev0,
32-bit pointers. From the tick roots (the race tick's update sequence) and the init root
(Race_Init) it lists every reachable function with its lines, static float-operation
counts by kind, globals read and written by direct reference, NON_MATCHING flag, fork
hook sites in its source lines, and a class: TICK, OFFLINE or OUT, with the rule or
override that decided it.

Static counts only. Dynamic per-tick counts come from the instrumented reference (G1.3).
Writes through pointers (for example `racer->` into gRacers) are not attributed to globals.

Needs: the decomp submodule (upstream/G-Diffuser/decomp) and the `libclang` Python
package. Usage:
  python3 tools/census/closure.py [--out docs/census]
Outputs closure.tsv (one row per reachable function) and callgraph.tsv (edges).
"""
from __future__ import annotations

import argparse
import collections
import csv
from pathlib import Path
import re
import sys

import clang.cindex as ci

ROOT = Path(__file__).resolve().parents[2]
GDX = ROOT / "upstream/G-Diffuser"
DECOMP = GDX / "decomp"
SOURCES = ["src/game", "src/sys", "src/overlays"]
ARGS = [
    "-m32", "-std=gnu90", "-funsigned-char", "-fno-builtin", "-D_LANGUAGE_C",
    "-DNON_MATCHING", "-DAVOID_UB", "-DVERSION_US=1", "-DBUILD_VERSION=VERSION_I",
    "-DASSET_VERSION=us", "-DASSET_REVISION=rev0", "-D_MIPS_SZLONG=32", "-D_MIPS_SZINT=32",
    "-D_MIPS_SZPTR=32", "-Iinclude", "-I.", "-Iinclude/libultra", "-Iinclude/PR",
    f"-I{GDX / 'include'}", "-Wno-everything",
]
# Race_Update's sequence, in source order (src/overlays/ovl_i2/race.c).
TICK_ROOTS = ["Menus_Update", "Effects_Update", "Racer_Update", "Camera_Update",
              "Background_Update", "Course_Update", "func_80074844"]
INIT_ROOTS = ["Race_Init"]
# Indirect calls resolved by hand. D_800CF4B8 (racer.c) is the per-track-shape handler table,
# indexed by TRACK_SHAPE_INDEX; it is called from these functions (3 sites).
SHAPE_TABLE = ["func_8008EC38", "func_8008EC58", "func_8008EC98", "func_8008F550",
               "func_8008FC80", "func_8008EC78", "func_80090490", "func_80090568"]
INDIRECT = {"Racer_Update": SHAPE_TABLE, "Racer_UpdateFromControls": SHAPE_TABLE}
HOOK = re.compile(r"\bPORT\b|EXPANSION_KIT|\bgdx|\bGdx|GDX_|CVar")
FLOAT_KINDS = {ci.TypeKind.FLOAT, ci.TypeKind.DOUBLE, ci.TypeKind.LONGDOUBLE}
ASSIGN = {"=", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>="}


def is_float(t: ci.Type) -> bool:
    return t.get_canonical().kind in FLOAT_KINDS


def operator(cur: ci.Cursor) -> str:
    kids = list(cur.get_children())
    if not kids:
        return "?"
    end = kids[0].extent.end.offset
    for tok in cur.get_tokens():
        if tok.extent.start.offset >= end and tok.kind == ci.TokenKind.PUNCTUATION:
            return tok.spelling
    return "?"


def unary_operator(cur: ci.Cursor) -> str:
    toks = [t.spelling for t in cur.get_tokens()]
    if not toks:
        return "?"
    if toks[0] in ("-", "+", "!", "~", "&", "*", "++", "--"):
        return toks[0]
    if toks[-1] in ("++", "--"):
        return toks[-1]
    return "?"


def base_decl(cur: ci.Cursor) -> ci.Cursor | None:
    """Global or static variable a write target names directly (x, x.f, x[i], x[i].f)."""
    while cur is not None:
        if cur.kind == ci.CursorKind.DECL_REF_EXPR:
            return cur.referenced
        if cur.kind == ci.CursorKind.MEMBER_REF_EXPR:
            kids = list(cur.get_children())
            # Through a pointer (a->b) the base is not the written object.
            if not kids or any(t.spelling == "->" for t in cur.get_tokens()
                               if t.extent.start.offset >= kids[0].extent.end.offset):
                return None
            cur = kids[0]
        elif cur.kind in (ci.CursorKind.ARRAY_SUBSCRIPT_EXPR, ci.CursorKind.PAREN_EXPR,
                          ci.CursorKind.UNEXPOSED_EXPR):
            kids = list(cur.get_children())
            cur = kids[0] if kids else None
        else:
            return None
    return None


def is_global(decl: ci.Cursor | None) -> bool:
    if decl is None or decl.kind != ci.CursorKind.VAR_DECL:
        return False
    parent = decl.semantic_parent
    return (parent is not None and parent.kind == ci.CursorKind.TRANSLATION_UNIT) or \
        decl.storage_class == ci.StorageClass.STATIC


class Func:
    def __init__(self, name: str, path: Path, start: int, end: int):
        self.name, self.path, self.start, self.end = name, path, start, end
        self.calls: set[str] = set()
        self.indirect = 0
        self.ops = collections.Counter()
        self.reads: set[str] = set()
        self.writes: set[str] = set()
        self.returns_gfx = False
        self.line_ops: collections.Counter = collections.Counter()  # (line, kind) -> count

    def add(self, kind: str, node: ci.Cursor) -> None:
        self.ops[kind] += 1
        self.line_ops[(node.location.line, kind)] += 1


def scan_body(fn: Func, cur: ci.Cursor) -> None:
    for node in cur.walk_preorder():
        kind = node.kind
        if kind == ci.CursorKind.CALL_EXPR:
            ref = node.referenced
            if ref is not None and ref.kind == ci.CursorKind.FUNCTION_DECL:
                fn.calls.add(ref.spelling)
                if ref.spelling == "sqrtf":
                    fn.add("fsqrt", node)
            else:
                fn.indirect += 1
        elif kind == ci.CursorKind.BINARY_OPERATOR:
            kids = list(node.get_children())
            op = operator(node)
            if op in ASSIGN:
                target = base_decl(kids[0]) if kids else None
                if is_global(target):
                    fn.writes.add(target.spelling)
            if any(is_float(k.type) for k in kids):
                if op in ("+", "-"):
                    fn.add("fadd", node)
                elif op == "*":
                    fn.add("fmul", node)
                elif op == "/":
                    fn.add("fdiv", node)
                elif op in ("<", ">", "<=", ">=", "==", "!="):
                    fn.add("fcmp", node)
        elif kind == ci.CursorKind.COMPOUND_ASSIGNMENT_OPERATOR:
            kids = list(node.get_children())
            op = operator(node)
            target = base_decl(kids[0]) if kids else None
            if is_global(target):
                fn.writes.add(target.spelling)
            if kids and is_float(kids[0].type):
                fn.add("fdiv" if op == "/=" else "fmul" if op == "*=" else "fadd", node)
        elif kind == ci.CursorKind.UNARY_OPERATOR:
            op = unary_operator(node)
            kids = list(node.get_children())
            if op in ("++", "--") and kids:
                target = base_decl(kids[0])
                if is_global(target):
                    fn.writes.add(target.spelling)
            if op == "-" and is_float(node.type):
                fn.add("fneg", node)
        elif kind in (ci.CursorKind.CSTYLE_CAST_EXPR, ci.CursorKind.UNEXPOSED_EXPR):
            kids = list(node.get_children())
            if kids and is_float(node.type) != is_float(kids[0].type) and \
                    node.type.get_canonical().kind != ci.TypeKind.VOID and \
                    kids[0].type.get_canonical().kind not in (ci.TypeKind.INVALID,):
                if node.type.get_canonical().kind in FLOAT_KINDS | {ci.TypeKind.INT, ci.TypeKind.UINT,
                        ci.TypeKind.SHORT, ci.TypeKind.USHORT, ci.TypeKind.CHAR_S, ci.TypeKind.SCHAR,
                        ci.TypeKind.UCHAR, ci.TypeKind.CHAR_U, ci.TypeKind.LONG, ci.TypeKind.ULONG,
                        ci.TypeKind.LONGLONG, ci.TypeKind.ULONGLONG}:
                    fn.add("fcvt", node)
        elif kind == ci.CursorKind.DECL_REF_EXPR:
            ref = node.referenced
            if is_global(ref):
                fn.reads.add(ref.spelling)


def nonmatching_ranges(text: list[str]) -> list[tuple[int, int]]:
    """Line ranges of `#ifdef NON_MATCHING` (or `#if defined NON_MATCHING`) active branches."""
    ranges, stack = [], []
    for number, line in enumerate(text, 1):
        s = line.strip()
        if s.startswith("#if"):
            stack.append((number, "NON_MATCHING" in s and "!" not in s.split("NON_MATCHING")[0][-2:]))
        elif s.startswith("#else") or s.startswith("#elif"):
            if stack and stack[-1][1]:
                ranges.append((stack[-1][0], number))
                stack[-1] = (number, False)
        elif s.startswith("#endif") and stack:
            start, flag = stack.pop()
            if flag:
                ranges.append((start, number))
    return ranges


def collect() -> dict[str, Func]:
    index = ci.Index.create()
    funcs: dict[str, Func] = {}
    files = sorted(p for d in SOURCES for p in (DECOMP / d).rglob("*.c"))
    for path in files:
        tu = index.parse(str(path), args=ARGS)
        for cur in tu.cursor.walk_preorder():
            if cur.kind != ci.CursorKind.FUNCTION_DECL or not cur.is_definition():
                continue
            loc = cur.location
            if loc.file is None:
                continue
            src = Path(loc.file.name).resolve()
            fn = Func(cur.spelling, src, cur.extent.start.line, cur.extent.end.line)
            fn.returns_gfx = "Gfx" in cur.result_type.spelling
            scan_body(fn, cur)
            key = cur.spelling
            if key in funcs and funcs[key].path != src:
                key = f"{cur.spelling}@{src.relative_to(DECOMP)}"
            funcs.setdefault(key, fn)
    for caller, targets in INDIRECT.items():
        funcs[caller].calls.update(targets)
    return funcs


def reach(funcs: dict[str, Func], roots: list[str],
          blocked: set[str] = frozenset()) -> dict[str, list[str]]:
    """Function -> roots that reach it, not expanding through `blocked` functions."""
    seen: dict[str, set[str]] = collections.defaultdict(set)
    for root in roots:
        todo = [root]
        while todo:
            name = todo.pop()
            if root in seen[name]:
                continue
            seen[name].add(root)
            fn = funcs.get(name)
            if fn and name not in blocked:
                todo.extend(sorted(fn.calls))
    return {k: sorted(v) for k, v in seen.items()}


def classify(name: str, fn: Func | None, tick: list[str], init: list[str],
             overrides: list[tuple[re.Pattern, str, str]]) -> tuple[str, str]:
    for pattern, cls, why in overrides:
        if pattern.fullmatch(name.split("@")[0]):
            if cls == "TICK" and not tick:
                return "OFFLINE", "reached only from Race_Init: run in the reference at import time"
            return cls, why
    rel = str(fn.path.relative_to(DECOMP)) if fn else ""
    if fn is None:
        return "OUT", "no definition in decomp sources (libultra/libc/asm)"
    if fn.returns_gfx or re.search(r"Draw|Gfx|Dl_|DisplayList", name):
        return "OUT", "drawing: the renderer is native"
    if rel.startswith("src/audio") or name.startswith("Audio_") or name.startswith("func_800BA"):
        return "OUT", "audio: not in this POC"
    if not tick and init:
        return "OFFLINE", "reached only from Race_Init: run in the reference at import time"
    return "TICK", "reached from the race tick"


def load_overrides(path: Path) -> list[tuple[re.Pattern, str, str]]:
    result = []
    if path.is_file():
        with path.open(encoding="utf-8") as f:
            for row in csv.reader((line for line in f if not line.startswith("#")), delimiter="\t"):
                if len(row) >= 3:
                    result.append((re.compile(row[0]), row[1], row[2]))
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=Path, default=ROOT / "docs/census")
    args = parser.parse_args(argv)
    if not (DECOMP / "src/game/racer.c").is_file():
        print("census: decomp submodule missing; see AGENTS.md", file=sys.stderr)
        return 2
    import os
    os.chdir(DECOMP)
    funcs = collect()
    overrides = load_overrides(args.out / "overrides.tsv")
    full_tick = reach(funcs, TICK_ROOTS)
    full_init = reach(funcs, INIT_ROOTS)
    # A function's own rule, then reachability that does not pass through OUT functions:
    # whatever is reached only through OUT code is OUT itself.
    own = {name: classify(name, funcs.get(name), full_tick.get(name, []),
                          full_init.get(name, []), overrides)
           for name in set(full_tick) | set(full_init)}
    blocked = {name for name, (cls, _why) in own.items() if cls == "OUT"}
    tick = reach(funcs, TICK_ROOTS, blocked)
    init = reach(funcs, INIT_ROOTS, blocked)
    names = sorted(set(full_tick) | set(full_init))
    texts: dict[Path, list[str]] = {}
    args.out.mkdir(parents=True, exist_ok=True)
    rows = []
    for name in names:
        fn = funcs.get(name)
        if name in tick or name in init:
            cls, why = classify(name, fn, tick.get(name, []), init.get(name, []), overrides)
        else:
            cls, why = "OUT", "reached only through OUT functions"
        if fn:
            lines = texts.setdefault(fn.path, fn.path.read_text(encoding="utf-8", errors="replace").splitlines())
            body = lines[fn.start - 1:fn.end]
            hooks = sum(1 for line in body if HOOK.search(line))
            nm = any(a <= fn.start <= b for a, b in nonmatching_ranges(lines))
            active_hooks = sorted(c for c in fn.calls | fn.reads | fn.writes
                                  if re.search(r"(^gdx|^Gdx|^GDX|^CVar|^gGdx|^sGdx)", c))
            rows.append([name, str(fn.path.relative_to(DECOMP)), fn.start, fn.end, fn.end - fn.start + 1,
                         ",".join(full_tick.get(name, [])) or "-", ",".join(full_init.get(name, [])) or "-",
                         cls, why, fn.ops["fadd"], fn.ops["fmul"], fn.ops["fdiv"], fn.ops["fcmp"],
                         fn.ops["fneg"], fn.ops["fcvt"], fn.indirect, "yes" if nm else "no", hooks,
                         ",".join(active_hooks) or "-", len(fn.calls),
                         ",".join(sorted(fn.reads)) or "-", ",".join(sorted(fn.writes)) or "-"])
        else:
            rows.append([name, "-", 0, 0, 0, ",".join(full_tick.get(name, [])) or "-",
                         ",".join(full_init.get(name, [])) or "-", cls, why,
                         0, 0, 0, 0, 0, 0, 0, "no", 0, "-", 0, "-", "-"])
    header = ["function", "file", "line_start", "line_end", "lines", "tick_roots", "init_roots",
              "class", "reason", "fadd", "fmul", "fdiv", "fcmp", "fneg", "fcvt", "indirect_calls",
              "non_matching", "hook_lines", "active_hook_calls", "callees", "globals_read",
              "globals_written"]
    with (args.out / "closure.tsv").open("w", encoding="utf-8", newline="") as f:
        w = csv.writer(f, delimiter="\t", lineterminator="\n")
        w.writerow(header)
        w.writerows(rows)
    # Per-line float operations of TICK and OFFLINE functions, for the G1.3 cost model.
    kinds = ["fadd", "fmul", "fdiv", "fsqrt", "fcmp", "fneg", "fcvt"]
    with (args.out / "float_ops_by_line.tsv").open("w", encoding="utf-8", newline="") as f:
        w = csv.writer(f, delimiter="\t", lineterminator="\n")
        w.writerow(["file", "line", "function", "class", *kinds])
        for row in rows:
            name, cls = row[0], row[7]
            fn = funcs.get(name)
            if fn is None or cls == "OUT":
                continue
            per_line: dict[int, collections.Counter] = collections.defaultdict(collections.Counter)
            for (line, kind), n in fn.line_ops.items():
                per_line[line][kind] += n
            for line in sorted(per_line):
                w.writerow([str(fn.path.relative_to(DECOMP)), line, name, cls,
                            *(per_line[line][k] for k in kinds)])
    with (args.out / "callgraph.tsv").open("w", encoding="utf-8", newline="") as f:
        w = csv.writer(f, delimiter="\t", lineterminator="\n")
        w.writerow(["caller", "callee"])
        for name in names:
            for callee in sorted(funcs[name].calls) if name in funcs else []:
                w.writerow([name, callee])
    counts = collections.Counter(r[7] for r in rows)
    print(f"census: {len(rows)} reachable functions; " +
          ", ".join(f"{k} {v}" for k, v in sorted(counts.items())))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
