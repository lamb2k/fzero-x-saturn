#!/usr/bin/env python3
"""Repository hygiene scan. Run before every push; CI runs it on every commit.

This repository is public. The scan fails on anything that must not be published:
ROM images (by extension, N64 header magic or the retail fingerprint), binary
content, unknown file types, oversized files and absolute private paths.

It scans every file git would publish: tracked files plus untracked files that are
not ignored. Submodules are skipped; they are pinned upstream projects.

Usage: tools/hygiene_scan.py [--root DIR]
Exit status: 0 clean, 1 findings, 2 usage or git error.
"""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import re
import subprocess
import sys

TEXT_SUFFIXES = {
    ".c", ".h", ".py", ".js", ".html", ".css", ".md", ".json", ".yml", ".yaml",
    ".sh", ".txt", ".tsv", ".mk", ".ld",
}
TEXT_NAMES = {"Makefile", "LICENSE", ".gitignore", ".gitmodules", ".gitattributes"}
# ROM, archive, disc and save formats. Never publishable here, whatever their content.
FORBIDDEN_SUFFIXES = {
    ".z64", ".n64", ".v64", ".o2r", ".otr", ".rom", ".bin", ".iso", ".cue", ".chd",
    ".ndd", ".zip", ".7z", ".rar", ".gz", ".xz", ".tar", ".sav", ".eep", ".sra", ".fla",
}
MAX_TEXT_BYTES = 256 * 1024
# The three N64 header byte orders (z64, v64, n64).
N64_MAGIC = (b"\x80\x37\x12\x40", b"\x37\x80\x40\x12", b"\x40\x12\x37\x80")
# Retail F-Zero X US rev0, as verified by tools/import_course.py.
RETAIL_MD5 = {"753437d0d8ada1d12f3f9cf0f0a5171f"}
RETAIL_SHA1 = {"5f658e88ffa9de23cba6986a8fd3d3a90d7b4340"}
# Absolute paths that name a person's machine. Built from parts so this file is clean.
PRIVATE_PATH = re.compile(
    "|".join((
        r"/" + r"home/[A-Za-z0-9_.-]+/",
        r"/" + r"Users/[A-Za-z0-9_. -]+/",
        r"/" + r"root/",
        r"[A-Za-z]:\\\\?" + r"Users\\\\?",
        r"/" + r"mnt/[a-z]/Users/",
    ))
)


def publishable_files(root: Path) -> list[str]:
    """Tracked plus untracked-not-ignored paths, minus submodule gitlinks."""
    def git(*args: str) -> str:
        return subprocess.run(("git", "-C", str(root), *args), check=True,
                              capture_output=True, text=True).stdout

    gitlinks = {line.split("\t", 1)[1] for line in git("ls-files", "-s").splitlines()
                if line.startswith("160000 ")}
    listed = git("ls-files", "-z", "--cached", "--others", "--exclude-standard").split("\0")
    return sorted({p for p in listed if p and p not in gitlinks and (root / p).is_file()})


def scan_file(root: Path, rel: str) -> list[str]:
    path = root / rel
    name = path.name
    suffix = path.suffix.lower()
    data = path.read_bytes()
    findings = []
    if suffix in FORBIDDEN_SUFFIXES:
        findings.append(f"forbidden file type {suffix}")
    elif suffix not in TEXT_SUFFIXES and name not in TEXT_NAMES:
        findings.append("unknown file type; add it to the scan's allowlist only if it is text")
    if data[:4] in N64_MAGIC:
        findings.append("N64 ROM header magic")
    if hashlib.md5(data).hexdigest() in RETAIL_MD5 or hashlib.sha1(data).hexdigest() in RETAIL_SHA1:
        findings.append("retail ROM fingerprint")
    if len(data) > MAX_TEXT_BYTES:
        findings.append(f"{len(data)} bytes exceeds the {MAX_TEXT_BYTES}-byte limit")
    if b"\0" in data:
        findings.append("binary content (NUL byte)")
        return findings
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError:
        findings.append("not UTF-8 text")
        return findings
    for number, line in enumerate(text.splitlines(), 1):
        match = PRIVATE_PATH.search(line)
        if match:
            findings.append(f"line {number}: absolute private path {match.group(0)!r}")
    return findings


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args(argv)
    try:
        files = publishable_files(args.root)
    except (OSError, subprocess.CalledProcessError) as exc:
        print(f"hygiene: cannot list files: {exc}", file=sys.stderr)
        return 2
    failed = 0
    for rel in files:
        for finding in scan_file(args.root, rel):
            print(f"hygiene: {rel}: {finding}")
            failed += 1
    if failed:
        print(f"hygiene: FAIL, {failed} finding(s) in {len(files)} files")
        return 1
    print(f"hygiene: PASS, {len(files)} files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
