#!/usr/bin/env python3
"""Rival line inputs from the owner's ROM (diagnostic, claude/rivals-native).

Writes, under the output directory (default generated/rivals, git-ignored):
  course.bin    the course record (0x7E0 bytes, big-endian), via import_course
  rom-boot.bin  the first 4 KB of the normalized (z64) ROM (machine constants)
  cpu-line.bin  the course's CPU line: 0x200 big-endian s16 (speed, lateral per half segment)
  pace.txt      base and per-rank lap targets (ms) from the decomp's CPU tables
Nothing derived from the ROM is printed or committed.
"""
import argparse, hashlib, os, re, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from import_course import extract_rom_course, normalize_rom  # noqa: E402
import course_catalog  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT / "upstream/G-Diffuser/decomp"
# common_assets_compressed (segment 0x0F) starts at ROM 0x2B9EA0 (yamls/us/rev0/assets.yaml);
# the Mute City CPU line is D_F264110 (segment offset 0x264110, cpu.c D_i3_8013DB80[0]).
SEGMENT_F_ROM = 0x2B9EA0
CPU_LINE_OFFSETS = {slug: course_catalog.course(slug)["cpu_line"] for slug in course_catalog.COURSES}
DIFFICULTY = {"novice": 0, "standard": 1, "expert": 2, "master": 3}


def table(name: str, source: str) -> list[int]:
    m = re.search(r"u32 %s\[\] = \{(.*?)(?:#ifdef|\};)" % re.escape(name), source, re.S)
    if not m:
        raise SystemExit(f"cannot find {name} in cpu.c")
    return [int(v) for v in re.findall(r"\d+", m.group(1))]


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--output", type=Path, default=ROOT / "generated/rivals")
    ap.add_argument("--course", default="mute-city-1", choices=tuple(course_catalog.COURSES))
    ap.add_argument("--difficulty", choices=DIFFICULTY, default="standard")
    args = ap.parse_args()
    path = os.environ.get("FZX_TEST_ROM")
    if not path:
        raise SystemExit("FZX_TEST_ROM is required locally")
    raw = Path(path).read_bytes()
    course_catalog.register(args.course)
    record, _ = extract_rom_course(raw, args.course)
    rom, _ = normalize_rom(raw)
    if CPU_LINE_OFFSETS[args.course] is None:
        # Death Race (course 54): cpu.c has no line for indexes from 30 up and fills the table with its default,
        # speed 69 and lateral 0 at every half segment (ovl_i3/cpu.c, the else branch after D_i3_8013DB80).
        line = b"".join((69).to_bytes(2, "big") + (0).to_bytes(2, "big") for _ in range(0x100))
    else:
        start = SEGMENT_F_ROM + CPU_LINE_OFFSETS[args.course]
        line = rom[start:start + 0x400]
    src = (DECOMP / "src/overlays/ovl_i3/cpu.c").read_text()
    course_index, d = course_catalog.course(args.course)["index"], DIFFICULTY[args.difficulty]
    if course_index < 30:
        base = table("D_i3_8013E4A8", src)[course_index * 4 + d]
        step = table("D_i3_8013E628", src)[course_index * 4 + d]
    else:
        base, step = 33000, 300   # no pace table past course 29 (the CPU pace is not tuned); fzx-rivals' own default
    out = args.output
    out.mkdir(parents=True, exist_ok=True)
    (out / "course.bin").write_bytes(record)
    (out / "rom-boot.bin").write_bytes(rom[:0x1000])
    (out / "cpu-line.bin").write_bytes(line)
    (out / "pace.txt").write_text(f"{base},{step}\n")
    # Plausibility only (values are not printed): speeds positive, laterals within a track width.
    words = [int.from_bytes(line[i:i + 2], "big", signed=True) for i in range(0, 0x400, 2)]
    speeds, lats = words[0::2], words[1::2]
    ok = all(-1 <= s <= 400 for s in speeds) and all(abs(v) <= 2000 for v in lats)
    receipt = ["# fzx generation receipt v1; regenerate, never edit"]
    for name in ("course.bin", "rom-boot.bin", "cpu-line.bin", "pace.txt"):
        receipt.append(f"output {hashlib.sha256((out / name).read_bytes()).hexdigest()} {name}")
    (out / "rivals.receipt").write_text("\n".join(receipt) + "\n")
    print(f"rival inputs written ({args.course}, {args.difficulty}); cpu line plausible: {ok}")


if __name__ == "__main__":
    main()
