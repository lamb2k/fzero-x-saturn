#!/usr/bin/env python3
"""The original courses the engine bakes: ROM slots, venue textures, CPU lines and music.

Course index = cup x 6 + round (course_select/course_model.c). Every value here is an
address or an id read from the pinned decomp (assets/yaml/us/rev0, ovl_i2/race.c,
ovl_i3/cpu.c); no game data. Per-course outputs live under generated/courses/<slug>/
(FZX_GEN_ROOT); the first course keeps generated/ itself, so its outputs and receipts
are unchanged.

    python3 tools/course_catalog.py            # list the courses
    python3 tools/course_catalog.py SLUG FIELD # print one field (for shell scripts)
"""
from __future__ import annotations

import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RECORD_BASE, RECORD_SIZE = 0x2AD1E0, 0x7E0

# slug: (index, record symbol, venue texture yaml, venue segment [start, end), CPU line
# offset in segment 0x0F (cpu.c D_i3_8013DB80), music id (race.c D_i2_80106F14), name)
COURSES = {
    "mute-city-1": (0, "aCourseMuteCity1", "mute_city_textures.yaml", (0x235130, 0x239A80), 0x264110, 0, "MUTE CITY"),
    "silence-1": (1, "aCourseSilence1", "silence_textures.yaml", (0x266C20, 0x26D780), 0x264340, 1, "SILENCE"),
    "sand-ocean-1": (2, "aCourseSandOcean1", "sand_ocean_textures.yaml", (0x243D90, 0x24A270), 0x264450, 2, "SAND OCEAN"),
    "devils-forest-1": (3, "aCourseDevilsForest1", "devils_forest_textures.yaml", (0x24A270, 0x2507F0), 0x2641D8, 5,
                        "DEVIL'S FOREST"),
    "big-blue-1": (4, "aCourseBigBlue1", "big_blue_textures.yaml", (0x23EC50, 0x243D90), 0x264690, 4, "BIG BLUE"),
    "port-town-1": (5, "aCoursePortTown1", "port_town_textures.yaml", (0x239A80, 0x23EC50), 0x264540, 3, "PORT TOWN"),
}
DEFAULT = "mute-city-1"


def course(slug: str) -> dict:
    if slug not in COURSES:
        raise SystemExit(f"unknown course {slug}; known: {', '.join(COURSES)}")
    index, symbol, yaml, venue, cpu_line, music, name = COURSES[slug]
    return dict(slug=slug, index=index, symbol=symbol, rom_offset=RECORD_BASE + index * RECORD_SIZE,
                venue_yaml=yaml, venue_segment=venue, cpu_line=cpu_line, music=music, name=name)


def gen_root(slug: str = DEFAULT) -> Path:
    """generated/ for the first course, generated/courses/<slug> for the others."""
    return ROOT / "generated" if slug == DEFAULT else ROOT / "generated/courses" / slug


def env_gen_root() -> Path:
    """The generated root the asset tools read and write (FZX_GEN_ROOT, default generated/)."""
    value = os.environ.get("FZX_GEN_ROOT")
    return Path(value).resolve() if value else ROOT / "generated"


def register(slug: str) -> None:
    """Make import_course.extract_rom_course accept this slot (its table names only the first)."""
    import import_course
    c = course(slug)
    import_course.COURSES.setdefault(slug, (c["rom_offset"], c["symbol"]))
    if import_course.COURSES[slug] != (c["rom_offset"], c["symbol"]):
        raise SystemExit(f"{slug}: ROM slot disagrees with import_course")


if __name__ == "__main__":
    if len(sys.argv) == 3:
        value = course(sys.argv[1])[sys.argv[2]]
        print(value if not isinstance(value, tuple) else " ".join("0x%X" % v for v in value))
    else:
        for slug in COURSES:
            c = course(slug)
            print(f"{c['index']} {slug:16} music {c['music']:2}  {c['name']}  -> {gen_root(slug).relative_to(ROOT)}")
