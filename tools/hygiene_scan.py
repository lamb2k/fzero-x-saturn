#!/usr/bin/env python3
"""Publication hygiene scan. This repository is public: every push publishes.

Checks every tracked file (or the paths given) for:
  - ROM, disc-image and archive file types;
  - N64 ROM headers in any byte order, and the exact retail ROM fingerprint;
  - binary content (NUL bytes) and files over the size limit;
  - absolute paths into a personal home directory, other local drive-letter or WSL share paths,
    and BIOS or ROM dump file names (environment variables and placeholders are the allowed forms).

Run before every push:  python3 tools/hygiene_scan.py
Exit status 0 means clean; 1 lists each problem as path: reason.
"""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
MAX_BYTES = 256 * 1024
FORBIDDEN_SUFFIXES = {
    ".z64", ".v64", ".n64", ".ndd", ".o2r", ".otr", ".rom",
    ".iso", ".cue", ".bin", ".chd", ".elf",
    ".zip", ".7z", ".rar", ".gz", ".xz",
}
# Big-endian, byte-swapped and word-swapped N64 cartridge headers.
N64_MAGICS = (b"\x80\x37\x12\x40", b"\x37\x80\x40\x12", b"\x40\x12\x37\x80")
# The importer's verified US rev0 fingerprint; see tools/import_course.py.
ROM_MD5 = "753437d0d8ada1d12f3f9cf0f0a5171f"
ROM_SHA1 = "5f658e88ffa9de23cba6986a8fd3d3a90d7b4340"
# Built from parts so this file does not match its own patterns.
_SEP = r"[\\/]+"
PRIVATE_PATH = re.compile(
    r"\b[A-Za-z]:" + _SEP + "(?i:Users|Documents and Settings)" + _SEP + r"[^\\/\s\"'`<>]+"
    + "|" + r"/(?:home|Users)/[A-Za-z0-9._-]+/"
    + "|" + r"(?<![\w.])/" + "root/"
)
# Any other absolute drive-letter path with a folder in it (a local project or tools tree), and WSL's UNC
# shares. Allowed forms: environment variables ($VAR, %VAR%), <placeholders> and C:\path\to\....
LOCAL_PATH = re.compile(
    r"(?<![\w\]])[A-Za-z]:" + _SEP + r"(?!path" + _SEP + r"to\b)[A-Za-z0-9_][^\s\"'`<>|*?\\/]*" + _SEP + r"[^\s\"'`<>|*?]+"
    + "|" + r"\\\\" + r"wsl(?:\$|\.localhost)\\"
)
# BIOS and ROM dump file names (dump tags, the usual BIOS names); the generic placeholders (rom.z64) stay allowed.
DUMP_NAME = re.compile(
    r"(?i)\((?:U|USA|E|Europe|J|Japan)\)\s*(?:\[[!a-z0-9]+\]\s*)?[^\s\"'`<>]*\.(?:z64|n64|v64|bin|cue|iso|rom)\b"
    + "|" + r"\[!\]\.(?:z64|n64|v64|bin|rom)\b"
    + "|" + r"\b(?:sega" + r"_101|mpr" + r"-1[0-9]{4}|bios[ _]saturn[^\n]*?\.bin|saturn[ _]bios[^\n]*?\.bin)\b"
)


def tracked_files() -> list[Path]:
    out = subprocess.run(["git", "-C", str(ROOT), "ls-files", "-z", "--cached"],
                         check=True, capture_output=True).stdout
    paths = [ROOT / name for name in out.decode("utf-8").split("\0") if name]
    return [path for path in paths if path.is_file()]  # Submodule gitlinks are directories.


def scan_file(path: Path) -> list[str]:
    problems = []
    if path.suffix.lower() in FORBIDDEN_SUFFIXES:
        problems.append(f"forbidden file type {path.suffix.lower()}")
    data = path.read_bytes()
    if data[:4] in N64_MAGICS:
        problems.append("starts with an N64 ROM header")
    if len(data) == 16 * 1024 * 1024 and (hashlib.md5(data).hexdigest() == ROM_MD5
                                          or hashlib.sha1(data).hexdigest() == ROM_SHA1):
        problems.append("is the retail ROM")
    if len(data) > MAX_BYTES:
        problems.append(f"larger than {MAX_BYTES // 1024} KiB ({len(data)} bytes)")
    if b"\0" in data[:65536]:
        problems.append("binary content (NUL bytes)")
        return problems
    text = data.decode("utf-8", errors="replace")
    for number, line in enumerate(text.splitlines(), 1):
        match = PRIVATE_PATH.search(line)
        if match:
            problems.append(f"line {number}: private absolute path {match.group(0)!r}")
            continue
        match = LOCAL_PATH.search(line)
        if match:
            problems.append(f"line {number}: local machine path {match.group(0)!r}")
            continue
        match = DUMP_NAME.search(line)
        if match:
            problems.append(f"line {number}: BIOS or ROM dump file name {match.group(0)!r}")
    return problems


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("paths", nargs="*", type=Path, help="Files to scan; default: every tracked file")
    args = parser.parse_args(argv)
    files = args.paths or tracked_files()
    failures = 0
    for path in files:
        for problem in scan_file(path):
            failures += 1
            try:
                shown = path.resolve().relative_to(ROOT)
            except ValueError:
                shown = path.name
            print(f"{shown}: {problem}")
    if failures:
        print(f"hygiene scan: {failures} problem(s) in {len(files)} file(s)", file=sys.stderr)
        return 1
    print(f"hygiene scan: {len(files)} file(s) clean")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
