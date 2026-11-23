#!/usr/bin/env python3
"""Build the menu sound-effect pack for the SCSP, locally, from the owner's ROM.

The front end plays the game's own system sound effects (Audio_TriggerSystemSE: channel 1 of the sound-effect
sequence, io value = the NA_SE number). This tool resolves each wanted NA_SE the way tools/sfx_pack.py resolves the
race events (first note of the layer: instrument, pitch, velocity), converts the instruments to signed 8-bit PCM
(one-shots capped at --one-shot-hz) and lays them out in sound RAM from the start of the race map's free region
(generated/snd/snd_race.json), so the race clip pack and its identity are untouched.

Writes, to --output (generated/snd_menu, git-ignored):
- SNDMENU.PAK: the sound-RAM image, loaded to SND_MENU_BASE by the CD loader;
- snd_menu.h: the effect table (NA_SE id, address, length, OCT/FNS, TL, level, one-shot frames) and the map;
- snd_menu.json: the same for tools; snd_menu.receipt: generators and output hashes (tools/check_receipt.sh).
Fails if the pack does not fit the free region. A looping effect (NA_SE_32, the refusal buzz) is keyed off by the
engine after --gate-fields fields.
"""
from pathlib import Path
import argparse
import json
import os
import sys

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bgm_cdda as B  # noqa: E402
import sfx_pack as S  # noqa: E402
from import_course import extract_rom_course, normalize_rom  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_IDS = (30, 16, 33, 62, 32, 22)   # cursor, back, accept, title Start / mode confirm, refused (gated loop), balance tick
GATE_FIELDS = 24   # a looping effect (the refusal buzz) is keyed off after this many fields: about 0.4 s


def one_shot_frames(samples: int, oct_: int, fns: int) -> int:
    """Length in 60 Hz fields at the pitch it plays, rounded up (engine_snd.c clip_frames)."""
    num = samples * 60 * 1024
    den = 44100 * (1024 + fns)
    if oct_ < 0:
        num <<= -oct_
    else:
        den <<= oct_
    return min(0xFFFF, (num + den - 1) // den)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--output", type=Path, required=True)
    ap.add_argument("--race-json", type=Path, default=ROOT / "generated/snd/snd_race.json")
    ap.add_argument("--ids", default=",".join(str(i) for i in DEFAULT_IDS))
    ap.add_argument("--one-shot-hz", type=int, default=16000)
    ap.add_argument("--gate-fields", type=int, default=GATE_FIELDS)
    args = ap.parse_args()
    path = os.environ.get("FZX_TEST_ROM")
    if not path:
        raise SystemExit("FZX_TEST_ROM is required locally; keep its path out of logs")
    B.output_ok(args.output)
    race = json.loads(args.race_json.read_text())
    free = next(r for r in race["regions"] if r[0] == "free")
    base, free_bytes = (free[1] + 31) & ~31, free[2]
    ids = [int(x) for x in args.ids.split(",")]
    raw = Path(path).read_bytes()
    extract_rom_course(raw)
    rom, _ = normalize_rom(raw)
    lay = B.layout(B.DECOMP)
    font = B.read_font(rom, lay, 0)
    args_table = B.seq_args_table(B.DECOMP)
    seq_seg = lay["seg"]["audio_seq"]
    seq = rom[seq_seg + lay["seqs"][0][0]: seq_seg + lay["seqs"][0][0] + lay["seqs"][0][1]]
    walker = B.SeqWalker(seq, args_table)
    chans = walker.channels()
    rows, clips, index = [], [], {}
    for na in ids:
        parts = S.event_parts(walker, chans, 1, na, font)
        if not parts:
            raise S.PackError("NA_SE_%d starts no note" % na)
        inst, note, vel = parts[0]
        smp = font[inst]
        if inst not in index:
            rate = round(32000 * smp["tuning"])
            start = lay["seg"]["audio_table"] + lay["banks"][smp["bank"]][0] + smp["addr"]
            n = smp["loop_end"] if smp["loop_forever"] else smp["size"] // 9 * 16
            pcm = B.vadpcm_decode(rom[start: start + smp["size"]], smp["book"], smp["order"], n)
            out_rate = min(rate, args.one_shot_hz)
            y = S.resample(pcm, rate, out_rate)
            data, tl = S.to_s8(y)
            ls = int(round(smp["loop_start"] * out_rate / rate)) if smp["loop_forever"] else 0
            index[inst] = len(clips)
            clips.append({"inst": inst, "data": data, "rate": out_rate, "tl": tl, "loop": bool(smp["loop_forever"]),
                          "loop_start": ls})
        c = clips[index[inst]]
        o, f = S.oct_fns(c["rate"] * 2 ** ((note - 39) / 12.0))
        rows.append({"na_se": na, "clip": index[inst], "oct": o, "fns": f,
                     "level": min(255, int(round(255 * (vel / 127.0) ** 2)))})
    offset, image = base, bytearray()
    for c in clips:
        c["offset"] = offset
        c["samples"] = len(c["data"])
        image += c["data"].astype(np.int8).tobytes()
        offset += len(c["data"])
    image += bytes((-len(image)) % 4)                      # the CD loader moves whole longwords
    if len(image) > free_bytes:
        raise S.PackError("menu pack %d bytes does not fit the free region (%d bytes)" % (len(image), free_bytes))
    fnv = 2166136261
    for b in image:
        fnv = ((fnv ^ b) * 16777619) & 0xFFFFFFFF
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "SNDMENU.PAK").write_bytes(bytes(image))
    table = ",\n".join(
        "    { %du, 0x%05Xu, %uu, %uu, %d, %uu, %uu, %uu, %uu, %uu }" % (
            r["na_se"], clips[r["clip"]]["offset"], clips[r["clip"]]["samples"], clips[r["clip"]]["loop_start"],
            r["oct"], r["fns"], clips[r["clip"]]["tl"], r["level"],
            args.gate_fields if clips[r["clip"]]["loop"] else one_shot_frames(clips[r["clip"]]["samples"], r["oct"], r["fns"]),
            1 if clips[r["clip"]]["loop"] else 0)
        for r in rows)
    (args.output / "snd_menu.h").write_text(
        "/* Generated by tools/sfx_menu_pack.py from the owner's ROM; regenerate, never edit. Local only (git-ignored). */\n"
        "#ifndef SND_MENU_H\n#define SND_MENU_H\n#include <stdint.h>\n"
        "typedef struct snd_menu_se { uint8_t na_se; uint32_t address; uint16_t samples; int8_t oct; uint16_t fns;"
        " uint8_t tl, level; uint16_t frames; } snd_menu_se_t;\n"
        "#define SND_MENU_COUNT %d\n#define SND_MENU_BASE 0x%05Xu\n#define SND_MENU_PAK_BYTES %uu\n"
        "#define SND_MENU_PAK_HASH 0x%08Xu\n#define SND_MENU_END 0x%05Xu\n"
        "static const snd_menu_se_t snd_menu_se[SND_MENU_COUNT] __attribute__((section(\".rodata.fzx_snd_menu\"))) = {\n%s\n};\n#endif\n"
        % (len(rows), base, len(image), fnv, base + len(image), table))
    (args.output / "snd_menu.json").write_text(json.dumps(
        {"note": "Local, derived from the owner's ROM. Do not commit or share.", "base": base, "bytes": len(image),
         "effects": rows, "clips": [{k: v for k, v in c.items() if k != "data"} for c in clips]}, indent=1) + "\n")
    S.write_receipt(args.output, "snd_menu.receipt", ("sfx_menu_pack.py", "sfx_pack.py", "bgm_cdda.py", "import_course.py"),
                    ("SNDMENU.PAK", "snd_menu.h", "snd_menu.json"))
    print("menu effects %s: %d clips, %.1f KB at 0x%05X (free region %d bytes from 0x%05X)" % (
        ",".join(str(i) for i in ids), len(clips), len(image) / 1024, base, free_bytes, base))


if __name__ == "__main__":
    try:
        main()
    except (S.PackError, B.BgmError) as e:
        raise SystemExit("sfx_menu_pack: %s" % e)
