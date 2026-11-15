#!/usr/bin/env python3
"""Import a local F-Zero X course into the Saturn showcase's fixed-point ABI.

No game data is downloaded or bundled. ROM inputs must be the unmodified US
revision 0 image; .z64/.v64/.n64 byte orders are normalized before fingerprinting.
Raw inputs are exactly one 0x7e0-byte CourseDataDisk record and require an explicit
byte order. Both routes verify the game's stored course checksum.

Format/algorithms: G-Diffuser (MIT, Vicente Mediano, 2026) and its pinned
Zorkats/fzerox decompilation (CC0, inspectredc and contributors). See ../NOTICE.md.
The layout, checksum and tension calculations below are adaptations of those
projects; this tool is MIT under the repository LICENSE.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import sys

UPSTREAM_COMMIT = "4f9320675758fd524bb3bc9ea98b913a09d48552"
DECOMP_COMMIT = "b944a4135409d24979f5c74b001befab8345761a"
UPSTREAM_URL = "https://github.com/Zorkats/G-Diffuser"
DECOMP_URL = "https://github.com/Zorkats/fzerox"
EXPECTED_ROM_MD5 = "753437d0d8ada1d12f3f9cf0f0a5171f"
EXPECTED_ROM_SHA1 = "5f658e88ffa9de23cba6986a8fd3d3a90d7b4340"
EXPECTED_ROM_VERSION = "F-ZERO X US REV 0 (v1.0)"
ROM_SIZE = 16 * 1024 * 1024
RECORD_SIZE = 0x7E0
COORDINATE_SCALE = 16
FX_ONE = 65536
# Match fzx_validate_course in src/fzx.c. These constants are in original
# game units; the existing retail record profile is narrower for coordinates.
RENDERER_POSITION_LIMIT = 4096 * COORDINATE_SCALE
RENDERER_RADIUS_MIN = COORDINATE_SCALE // 4
RENDERER_RADIUS_MAX = 256 * COORDINATE_SCALE
COURSES = {"mute-city-1": (0x2AD1E0, "aCourseMuteCity1")}
FEATURES = {
    "pit": (0x5A0, 3), "dash": (0x5E0, 2), "dirt": (0x620, 3),
    "ice": (0x660, 3), "jump": (0x6A0, 2), "landmine": (0x6E0, 2),
    "gate": (0x720, 2), "building": (0x760, 14), "sign": (0x7A0, 4),
}
FEATURE_WEIGHTS = (0, 0x10, 0x80, 0x100, 0x800, 0x1000, 0x8000, 0x10000, 0x80000)
DERIVED_TRACK_FLAGS = 0x600 | 0x38000 | 0x40000000


class CourseImportError(ValueError):
    """An input cannot safely or faithfully be represented by this importer."""


def f32(value: float) -> float:
    """Round an operation to the original game's IEEE-754 binary32 precision."""
    try:
        return struct.unpack("!f", struct.pack("!f", value))[0]
    except (OverflowError, struct.error) as exc:
        raise CourseImportError("A course calculation overflowed binary32") from exc


def _add(a: float, b: float) -> float:
    return f32(f32(a) + f32(b))


def _mul(a: float, b: float) -> float:
    return f32(f32(a) * f32(b))


def _round_away(value: float) -> int:
    return math.floor(value + 0.5) if value >= 0 else math.ceil(value - 0.5)


def to_fixed(value: float, *, coordinate: bool = True) -> int:
    """Round to Q16.16 without saturation; geometry uses 1/16 game scale."""
    if not math.isfinite(value):
        raise CourseImportError("Coordinates and tensions must be finite")
    result = _round_away(value * FX_ONE / (COORDINATE_SCALE if coordinate else 1))
    if not -(1 << 31) <= result < (1 << 31):
        raise CourseImportError("Value exceeds the signed Q16.16 range")
    return result


def bank_to_turns(degrees: int) -> int:
    """Preserve DEG_TO_FZXANG's truncation to the game's 4096-angle table."""
    # C integer division truncates toward zero; Python // does not for negatives.
    value = degrees * 4096
    angle = abs(value) // 360
    if value < 0:
        angle = -angle
    return (angle & 4095) << 4


def normalize_rom(data: bytes) -> tuple[bytes, str]:
    if len(data) != ROM_SIZE:
        raise CourseImportError(f"US rev0 ROM must be exactly {ROM_SIZE} bytes")
    magic = data[:4]
    if magic == b"\x80\x37\x12\x40":
        return data, "z64-big-endian"
    out = bytearray(len(data))
    if magic == b"\x37\x80\x40\x12":
        out[0::2], out[1::2] = data[1::2], data[0::2]
        return bytes(out), "v64-byte-swapped"
    if magic == b"\x40\x12\x37\x80":
        for index in range(4):
            out[index::4] = data[3 - index::4]
        return bytes(out), "n64-word-swapped"
    raise CourseImportError("Unrecognized N64 ROM header byte order")


def extract_rom_course(data: bytes, course_name: str = "mute-city-1") -> tuple[bytes, dict]:
    if course_name not in COURSES:
        raise CourseImportError(f"Unsupported course selection: {course_name}")
    normalized, byte_order = normalize_rom(data)
    md5 = hashlib.md5(normalized).hexdigest()
    sha1 = hashlib.sha1(normalized).hexdigest()
    if md5 != EXPECTED_ROM_MD5 or sha1 != EXPECTED_ROM_SHA1:
        raise CourseImportError(
            "ROM fingerprint mismatch: expected an unmodified F-Zero X US rev0 image "
            f"(MD5 {EXPECTED_ROM_MD5}; SHA-1 {EXPECTED_ROM_SHA1}). "
            f"Received MD5 {md5}; SHA-1 {sha1}. No course was extracted."
        )
    offset, symbol = COURSES[course_name]
    record = normalized[offset:offset + RECORD_SIZE]
    return record, {
        "input_kind": "retail-rom", "input_byte_order": byte_order,
        "source_sha256": hashlib.sha256(data).hexdigest(),
        "normalized_rom_sha256": hashlib.sha256(normalized).hexdigest(),
        "normalized_rom_md5": md5, "normalized_rom_sha1": sha1,
        "verified_rom_version": EXPECTED_ROM_VERSION,
        "course_name": course_name, "course_symbol": symbol,
        "rom_offset": offset, "rom_fingerprint_verified": True,
    }


def course_checksum(points: list[dict], features: dict[str, list[int]]) -> int:
    """Course_CalculateChecksum, with explicit binary32 rounding and uint32 wrap."""
    checksum = len(points)
    for i, point in enumerate(points):
        x, y, z = point["position"]
        left, right = point["radius_left"], point["radius_right"]
        a = _add(x, _mul(_add(1.1, _mul(0.7, i)), y))
        b = _mul(_mul(_add(2.2, _mul(1.2, i)), z), _add(4.4, _mul(0.9, i)))
        c = _mul(_mul(_add(5.5, _mul(0.8, i)), right), 4.8)
        total = _add(_add(_add(a, b), left), c)
        if not -(1 << 31) <= total < (1 << 31):
            raise CourseImportError("Course checksum arithmetic exceeds the original s32 range")
        info = point["track_segment_info"] & ~DERIVED_TRACK_FLAGS
        checksum += math.trunc(total) + info * (0xFE - i)
        checksum += point["bank_degrees"] * (0x93DE - i * 2)
    for i in range(len(points)):
        for name, weight in zip(FEATURES, FEATURE_WEIGHTS):
            checksum += features[name][i] * (i + weight)
    return checksum & 0xFFFFFFFF


def segment_tension(points: list[dict], index: int) -> float:
    """Adapt func_8009DB28 and Course_SplineCalculateTensions from course.c.

    Original position differences round as float, the quadratic runs as double,
    sqrtf rounds its argument and result to float, and the two roots become float
    before their average. Offline calculation avoids SH-2 software floating point.
    """
    count = len(points)
    p0, p1, p2, p3 = (points[i % count]["position"]
                      for i in (index - 1, index, index + 1, index + 2))
    delta = [f32(a - b) for a, b in zip(p1, p2)]
    combined = [f32(a - b) + d for a, b, d in zip(p0, p3, delta)]
    dot = sum(a * b for a, b in zip(delta, combined)) * 0.75
    dot_squared = dot * dot
    chord_squared = sum(a * a for a in delta) * 9.0
    quarter_squared = sum(a * a for a in combined) * 0.0625
    roots = []
    for a, b in ((p2, p0), (p3, p1)):
        diff = [f32(x - y) for x, y in zip(a, b)]
        coefficient = quarter_squared - sum(x * x for x in diff)
        product = coefficient * chord_squared
        denominator = 2.0 * coefficient
        if product > dot_squared or abs(denominator) <= 0.1:
            raise CourseImportError(f"Control point {index}: invalid adaptive spline tension")
        discriminant = f32(dot_squared - product)
        root_term = f32(math.sqrt(discriminant))
        root = f32((dot + (root_term if denominator > 0 else -root_term)) / denominator)
        if not math.isfinite(root) or not 0.0 <= root <= 2.0:
            raise CourseImportError(f"Control point {index}: spline tension outside [0, 2]")
        roots.append(root)
    return _mul(_add(roots[0], roots[1]), 0.5)


def record_checksum(data: bytes, byte_order: str) -> int:
    """The checksum the original game computes for a record (ref/fixture_course.py stores it)."""
    points, features = parse_geometry(data, byte_order)
    return course_checksum(points, features)


def parse_record(data: bytes, byte_order: str) -> dict:
    points, features = parse_geometry(data, byte_order)
    endian = ">" if byte_order == "big" else "<"
    creator, count, venue, skybox = struct.unpack_from("Bbbb", data)
    return _finish_record(data, endian, creator, count, venue, skybox, points, features)


def parse_geometry(data: bytes, byte_order: str) -> tuple[list[dict], dict[str, list[int]]]:
    if len(data) != RECORD_SIZE:
        raise CourseImportError(f"Raw CourseDataDisk record must be exactly {RECORD_SIZE} bytes")
    if byte_order not in ("big", "little"):
        raise CourseImportError("Raw record byte order must be explicitly big or little")
    endian = ">" if byte_order == "big" else "<"
    creator, count, venue, skybox = struct.unpack_from("Bbbb", data)
    if not 4 <= count <= 64:
        raise CourseImportError(f"Control point count {count} is outside [4, 64]")
    if not 0 <= venue <= 10 or not 0 <= skybox <= 7:
        raise CourseImportError("Course venue or skybox is outside the original game range")
    points = []
    for i in range(count):
        x, y, z, left, right, info = struct.unpack_from(endian + "fffhhI", data, 0x20 + i * 0x14)
        if not all(math.isfinite(v) for v in (x, y, z)):
            raise CourseImportError(f"Control point {i}: non-finite coordinate")
        if any(abs(v) > RENDERER_POSITION_LIMIT for v in (x, y, z)):
            raise CourseImportError(
                f"Control point {i}: position exceeds the renderer limit of "
                "±65536 original units (±4096 scaled units)"
            )
        # The source's Expansion Kit checksum validation accepts these retail bounds.
        if not (-15000 <= x <= 15000 and -250 <= y <= 5000 and -15000 <= z <= 15000):
            raise CourseImportError(f"Control point {i}: outside supported retail course bounds")
        if not (RENDERER_RADIUS_MIN <= left <= RENDERER_RADIUS_MAX and
                RENDERER_RADIUS_MIN <= right <= RENDERER_RADIUS_MAX):
            raise CourseImportError(
                f"Control point {i}: road radii must be in [4, 4096] original "
                "units ([0.25, 256] scaled units), matching the renderer"
            )
        bank = struct.unpack_from(endian + "h", data, 0x520 + i * 2)[0]
        points.append({"position": (x, y, z), "radius_left": left,
                       "radius_right": right, "bank_degrees": bank,
                       "track_segment_info": info})
    for i, point in enumerate(points):
        if point["position"] == points[(i + 1) % count]["position"]:
            raise CourseImportError(f"Control point {i}: consecutive duplicate position")
    features = {}
    for name, (offset, maximum) in FEATURES.items():
        values = list(struct.unpack_from(f"{count}b", data, offset))
        if any(value < -1 or value > maximum for value in values):
            raise CourseImportError(f"Course feature {name} contains an invalid value")
        features[name] = values
    return points, features


def _finish_record(data: bytes, endian: str, creator: int, count: int, venue: int, skybox: int,
                   points: list[dict], features: dict[str, list[int]]) -> dict:
    stored_checksum = struct.unpack_from(endian + "I", data, 4)[0]
    calculated_checksum = course_checksum(points, features)
    if stored_checksum != calculated_checksum:
        raise CourseImportError(
            f"Course checksum mismatch: stored {stored_checksum:08x}, "
            f"calculated {calculated_checksum:08x}; check record byte order and integrity"
        )
    converted = []
    for i, point in enumerate(points):
        tension = segment_tension(points, i)
        converted.append({
            "position": dict(zip(("x", "y", "z"), map(to_fixed, point["position"]))),
            "radius_left": to_fixed(point["radius_left"]),
            "radius_right": to_fixed(point["radius_right"]),
            "bank": bank_to_turns(point["bank_degrees"]),
            "track_segment_info": point["track_segment_info"],
            "tension": to_fixed(tension, coordinate=False),
        })
    return {
        "schema_version": 1, "coordinate_scale": COORDINATE_SCALE,
        "count": count, "points": converted,
        "source_metadata": {
            "creator_id": creator, "venue": venue, "skybox": skybox,
            "flag": data[8], "bgm": struct.unpack_from("b", data, 0x1F)[0],
            "file_name_hex": data[9:0x1F].hex(), "course_checksum": f"{stored_checksum:08x}",
            "feature_values": features,
        },
        "provenance": {
            "upstream_url": UPSTREAM_URL, "upstream_commit": UPSTREAM_COMMIT,
            "decomp_url": DECOMP_URL, "decomp_commit": DECOMP_COMMIT,
            "record_sha256": hashlib.sha256(data).hexdigest(),
            "record_byte_order": "big" if endian == ">" else "little", "course_checksum_verified": True,
            "tension_algorithm": "Course_SplineCalculateTensions/func_8009DB28",
            "bank_conversion": "truncate(degrees*4096/360), wrap 4096, multiply 16",
            "coordinate_rounding": "nearest, ties away from zero",
            "expected_rom_version": EXPECTED_ROM_VERSION,
            "expected_rom_md5": EXPECTED_ROM_MD5, "expected_rom_sha1": EXPECTED_ROM_SHA1,
            "gameplay_scope": "course geometry; original handling and course features are not implemented",
        },
    }


def render_header(course: dict) -> str:
    provenance = course["provenance"]
    lines = [
        "/* Generated from a user-supplied F-Zero X course. Do not redistribute game data.",
        f" * Record SHA-256: {provenance['record_sha256']}",
        f" * G-Diffuser: {UPSTREAM_COMMIT}; decomp: {DECOMP_COMMIT}",
        f" * ROM profile: {EXPECTED_ROM_VERSION}; see the adjacent JSON for verification.",
        " * Positions/radii: Q16.16 at 1/16 game scale. Bank: original 12-bit quantization.",
        " */", "#ifndef FZX_IMPORTED_COURSE_H", "#define FZX_IMPORTED_COURSE_H",
        '#include "fzx.h"', "", "static const fzx_course_t fzx_course_data = {",
        f"    .count = {course['count']}u,", "    .points = {",
    ]
    for point in course["points"]:
        pos = point["position"]
        lines.extend([
            f"        {{ .position = {{ {pos['x']}, {pos['y']}, {pos['z']} }},",
            f"          .radius_left = {point['radius_left']}, .radius_right = {point['radius_right']},",
            f"          .bank = {point['bank']}u, .reserved = 0u,",
            f"          .track_segment_info = 0x{point['track_segment_info']:08x}u, .tension = {point['tension']} }},",
        ])
    lines.extend(["    }", "};", "", "#endif", ""])
    return "\n".join(lines)


def render_record_c(record: bytes, byte_order: str) -> str:
    """The checksum-verified CourseDataDisk record as C, for the original course code (FZX_SIM=orig).

    Derived from the owner's input: it lives in generated/ (git-ignored) like every output here.
    """
    rows = [", ".join(f"0x{b:02X}" for b in record[i:i + 16]) for i in range(0, len(record), 16)]
    return ("/* Generated by tools/import_course.py; regenerate, never edit. Local only. */\n"
            f"const unsigned char fzx_course_record[{len(record)}] = {{\n    " + ",\n    ".join(rows) + "\n};\n"
            f"const int fzx_course_record_big_endian = {1 if byte_order == 'big' else 0};\n")


def render_receipt(source: bytes, input_kind: str, outputs: list[Path]) -> str:
    """Generation receipt checked by tools/check_receipt.sh before a build uses the outputs.

    Paths are basenames beside the receipt, so the receipt holds no local paths.
    """
    lines = ["# fzx generation receipt v1; regenerate, never edit",
             f"generator {hashlib.sha256(Path(__file__).read_bytes()).hexdigest()} import_course.py",
             f"input {hashlib.sha256(source).hexdigest()} {input_kind}"]
    lines += [f"output {hashlib.sha256(path.read_bytes()).hexdigest()} {path.name}" for path in outputs]
    return "\n".join(lines) + "\n"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    inputs = parser.add_mutually_exclusive_group(required=True)
    inputs.add_argument("--rom", type=Path, help="Local, unmodified US rev0 ROM (.z64/.v64/.n64)")
    inputs.add_argument("--record", type=Path, help="Local raw 0x7e0-byte CourseDataDisk record")
    parser.add_argument("--byte-order", choices=("big", "little"), help="Required with --record")
    parser.add_argument("--course", choices=tuple(COURSES), help="ROM slot; defaults to mute-city-1")
    parser.add_argument("--output", type=Path, required=True, help="Generated C header path")
    parser.add_argument("--json-output", type=Path, help="Browser/provenance JSON; defaults beside the header")
    parser.add_argument("--record-output", type=Path,
                        help="Also write the verified record itself as C (for FZX_SIM=orig, local only)")
    args = parser.parse_args(argv)
    if args.record and not args.byte_order:
        parser.error("--record requires --byte-order big or little")
    if args.rom and args.byte_order:
        parser.error("ROM byte order is detected from the header; omit --byte-order")
    if args.record and args.course:
        parser.error("--course only identifies a verified ROM slot, not a raw record")
    source = args.rom or args.record
    json_output = args.json_output or args.output.with_suffix(".json")
    try:
        paths = [source.resolve(), args.output.resolve(), json_output.resolve()]
        if len(set(paths)) != 3:
            raise CourseImportError("Input, generated header and JSON must have different paths")
        expected_size = ROM_SIZE if args.rom else RECORD_SIZE
        if source.stat().st_size != expected_size:
            raise CourseImportError(f"Input must be exactly {expected_size} bytes")
        data = source.read_bytes()
        if args.rom:
            record, provenance = extract_rom_course(data, args.course or "mute-city-1")
            record_order = "big"
            course = parse_record(record, "big")
        else:
            record, record_order = data, args.byte_order
            course = parse_record(data, args.byte_order)
            provenance = {"input_kind": "raw-course-record", "input_byte_order": args.byte_order,
                          "source_sha256": hashlib.sha256(data).hexdigest(),
                          "rom_fingerprint_verified": False, "verified_rom_version": None}
        course["provenance"].update(provenance)
        # Validate everything before either output is opened. No source bytes are changed.
        header = render_header(course)
        json_text = json.dumps(course, indent=2, sort_keys=True, allow_nan=False) + "\n"
        args.output.parent.mkdir(parents=True, exist_ok=True)
        json_output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(header, encoding="utf-8")
        json_output.write_text(json_text, encoding="utf-8")
        outputs = [args.output, json_output]
        if args.record_output:
            args.record_output.parent.mkdir(parents=True, exist_ok=True)
            args.record_output.write_bytes(render_record_c(record, record_order).encode())
            outputs.append(args.record_output)
        receipt = args.output.with_suffix(".receipt")
        # Bytes, so the receipt has LF line ends for the shell check on every host.
        receipt.write_bytes(render_receipt(data, provenance["input_kind"], outputs).encode())
    except (OSError, CourseImportError) as exc:
        print(f"Course import failed: {exc}", file=sys.stderr)
        return 2
    print(f"Imported {course['count']} control points: {args.output} and {json_output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
