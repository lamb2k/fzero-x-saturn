#!/usr/bin/env python3
"""Build a multi-track Saturn disc: data track 1 from the engine ISO, CD audio tracks 2..N from WAVs.

Writes the redump-style layout every Saturn optical drive emulator (GDEMU/Rhea/Phoebe,
Satiator, MODE, Fenrir) and emulator reads: one BIN per track, track 1 as MODE1/2352
raw sectors (sync, header, EDC and ECC generated here), audio tracks as raw 16-bit
little-endian stereo with a 2-second pregap stored in the file (INDEX 00), and a CUE.

It also writes cdda_tracks.h for the engine: music id -> track number, the loop-back
sector (relative to the track's INDEX 01) and the track length, from the renderer's
manifest.json. The audio tracks and that header are derived from the owner's ROM:
keep them local (the engine build reads the header from git-ignored generated/).

    disc_cdda.py --iso build/cd.iso --music DIR --ids 0 --output DIR --name fzs-engine [--header H]
"""
from pathlib import Path
import argparse
import hashlib
import json
import wave

SECTOR_RAW = 2352
SECTOR_DATA = 2048
PREGAP = 150                     # 2 seconds of silence before each audio track (INDEX 00)


class DiscError(Exception):
    pass


# ---------------------------------------------------------------- EDC / ECC (ECMA-130)

def _tables():
    edc = []
    for i in range(256):
        e = i
        for _ in range(8):
            e = (e >> 1) ^ (0xD8018001 if e & 1 else 0)
        edc.append(e)
    f_lut = [0] * 256
    b_lut = [0] * 256
    for i in range(256):
        j = ((i << 1) ^ (0x11D if i & 0x80 else 0)) & 0xFF
        f_lut[i] = j
        b_lut[i ^ j] = i
    return edc, f_lut, b_lut


EDC_LUT, ECC_F, ECC_B = _tables()


def edc(data) -> int:
    e = 0
    for byte in data:
        e = (e >> 8) ^ EDC_LUT[(e ^ byte) & 0xFF]
    return e


def _ecc_block(src, major_count, minor_count, major_mult, minor_inc):
    size = major_count * minor_count
    out = bytearray(2 * major_count)
    for major in range(major_count):
        index = (major >> 1) * major_mult + (major & 1)
        a = b = 0
        for _ in range(minor_count):
            t = src[index]
            index += minor_inc
            if index >= size:
                index -= size
            a ^= t
            b ^= t
            a = ECC_F[a]
        a = ECC_B[ECC_F[a] ^ b]
        out[major] = a
        out[major + major_count] = a ^ b
    return out


def bcd(v: int) -> int:
    return (v // 10) << 4 | (v % 10)


def msf(fad: int) -> bytes:
    return bytes((bcd(fad // 4500), bcd(fad // 75 % 60), bcd(fad % 75)))


def mode1_sector(lba: int, user: bytes) -> bytes:
    """One raw MODE1 sector at logical block lba (FAD = lba + 150)."""
    if len(user) != SECTOR_DATA:
        raise DiscError("user data must be 2048 bytes")
    s = bytearray(SECTOR_RAW)
    s[0:12] = b"\x00" + b"\xff" * 10 + b"\x00"
    s[12:15] = msf(lba + 150)
    s[15] = 1
    s[16:16 + SECTOR_DATA] = user
    s[0x810:0x814] = edc(s[0:0x810]).to_bytes(4, "little")
    p = _ecc_block(memoryview(s)[0xC:], 86, 24, 2, 86)
    s[0x81C:0x81C + 172] = p
    q = _ecc_block(memoryview(s)[0xC:], 52, 43, 86, 88)
    s[0x8C8:0x8C8 + 104] = q
    return bytes(s)


# ---------------------------------------------------------------- disc

def cue_time(sectors: int) -> str:
    return "%02d:%02d:%02d" % (sectors // 4500, sectors // 75 % 60, sectors % 75)


def read_wav(path: Path) -> bytes:
    with wave.open(str(path), "rb") as w:
        if (w.getnchannels(), w.getsampwidth(), w.getframerate()) != (2, 2, 44100):
            raise DiscError("%s: CD audio must be 44.1 kHz 16-bit stereo" % path.name)
        frames = w.readframes(w.getnframes())
    if len(frames) % SECTOR_RAW:
        raise DiscError("%s: not a whole number of CD sectors" % path.name)
    return frames


def build(iso: Path, music: Path, ids: list, out: Path, name: str, header: Path = None) -> dict:
    data = iso.read_bytes()
    if len(data) % SECTOR_DATA:
        raise DiscError("the ISO is not a whole number of 2048-byte sectors")
    manifest = json.loads((music / "manifest.json").read_text())
    by_id = {t["music_id"]: t for t in manifest["tracks"]}
    out.mkdir(parents=True, exist_ok=True)
    files = []
    track1 = out / ("%s (Track 01).bin" % name)
    n1 = len(data) // SECTOR_DATA
    with open(track1, "wb") as f:
        for lba in range(n1):
            f.write(mode1_sector(lba, data[lba * SECTOR_DATA:(lba + 1) * SECTOR_DATA]))
    files.append((track1, "MODE1/2352", n1, None))
    table = []
    for k, mid in enumerate(ids):
        if mid not in by_id:
            raise DiscError("music id %d is not in the manifest" % mid)
        t = by_id[mid]
        pcm = read_wav(music / t["file"])
        if len(pcm) // SECTOR_RAW != t["sectors"]:
            raise DiscError("%s does not match its manifest length" % t["file"])
        path = out / ("%s (Track %02d).bin" % (name, k + 2))
        with open(path, "wb") as f:
            f.write(bytes(PREGAP * SECTOR_RAW))
            f.write(pcm)
        files.append((path, "AUDIO", PREGAP + t["sectors"], PREGAP))
        for alias in [mid] + t.get("aliases", []):
            table.append({"music_id": alias, "track": k + 2, "sectors": t["sectors"],
                          "loop_sector": t["loop_sector"]})
    lines = []
    for n, (path, mode, _, gap) in enumerate(files, 1):
        lines.append('FILE "%s" BINARY' % path.name)
        lines.append("  TRACK %02d %s" % (n, mode))
        if gap:
            lines.append("    INDEX 00 00:00:00")
            lines.append("    INDEX 01 %s" % cue_time(gap))
        else:
            lines.append("    INDEX 01 00:00:00")
    cue = out / ("%s.cue" % name)
    cue.write_text("\r\n".join(lines) + "\r\n")
    total = sum(f[2] for f in files)
    if total > 333000:
        raise DiscError("disc is %d sectors, over 74 minutes" % total)
    sums = []
    for path in [cue] + [f[0] for f in files]:
        h = hashlib.sha256()
        with open(path, "rb") as fh:
            for chunk in iter(lambda: fh.read(1 << 20), b""):
                h.update(chunk)
        sums.append("%s  %s" % (h.hexdigest(), path.name))
    (out / "SHA256SUMS").write_text("\n".join(sums) + "\n")
    if header:
        write_header(header, table)
    return {"cue": cue.name, "sectors": total, "minutes": round(total / 4500, 2), "tracks": table}


def write_header(path: Path, table: list):
    _write_header(path, table)
    # tools/check_receipt.sh format, so the engine build refuses a stale table
    import hashlib as _h
    tools = Path(__file__).resolve().parent
    sha = lambda f: _h.sha256(Path(f).read_bytes()).hexdigest()
    (path.parent / "cdda.receipt").write_text(
        "# fzx generation receipt v1; regenerate, never edit\n"
        "generator %s disc_cdda.py\noutput %s %s\n" % (sha(tools / "disc_cdda.py"), sha(path), path.name))


def _write_header(path: Path, table: list):
    rows = ",\n".join("    { %d, %d, %du, %du }" % (t["music_id"], t["track"], t["sectors"],
                                                     0xFFFFFFFF if t["loop_sector"] is None else t["loop_sector"])
                      for t in table)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("/* Generated by tools/disc_cdda.py from the local music manifest; regenerate, never edit.\n"
                    "   Derived from the owner's ROM: lives in git-ignored generated/ only. */\n"
                    "#ifndef FZX_CDDA_TRACKS_H\n#define FZX_CDDA_TRACKS_H\n"
                    "#define FZX_CDDA_NO_LOOP 0xFFFFFFFFu\n"
                    "typedef struct fzx_cdda_track { unsigned char music_id, track; unsigned long sectors, loop_sector; }"
                    " fzx_cdda_track_t;\n"
                    "static const fzx_cdda_track_t fzx_cdda_tracks[] = {\n%s\n};\n"
                    "#define FZX_CDDA_TRACK_COUNT %d\n#endif\n" % (rows, len(table)))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--iso", type=Path, help="data track (the engine build's ISO); not needed with --header-only")
    ap.add_argument("--music", type=Path, required=True, help="bgm_cdda.py output directory")
    ap.add_argument("--ids", required=True, help="comma-separated music ids in track order from track 2, or 'all' (manifest order)")
    ap.add_argument("--output", type=Path)
    ap.add_argument("--header-only", action="store_true", help="write only --header (the engine needs it before its build)")
    ap.add_argument("--name", default="fzs-engine")
    ap.add_argument("--header", type=Path)
    args = ap.parse_args()
    tracks = json.loads((args.music / "manifest.json").read_text())["tracks"]
    by_id = {t["music_id"]: t for t in tracks}
    ids = [t["music_id"] for t in tracks] if args.ids == "all" else [int(x) for x in args.ids.split(",")]
    if args.header_only:
        if not args.header:
            raise DiscError("--header-only needs --header")
        write_header(args.header, [{"music_id": a, "track": k + 2, "sectors": by_id[m]["sectors"],
                                    "loop_sector": by_id[m]["loop_sector"]}
                                   for k, m in enumerate(ids) for a in [m] + by_id[m].get("aliases", [])])
        return
    if not args.iso or not args.output:
        raise DiscError("--iso and --output are required")
    r = build(args.iso, args.music, ids, args.output, args.name, args.header)
    print(json.dumps(r, indent=1))


if __name__ == "__main__":
    try:
        main()
    except DiscError as e:
        raise SystemExit("disc_cdda: %s" % e)
