#!/usr/bin/env python3
"""Build the race sound-effect set for the SCSP, locally, from the owner's ROM.

Reads the same soundfont as tools/bgm_cdda.py and walks channels 1..15 of the sound-effect
sequence to find which instruments the game starts:
- racer channels (2..9, 14) -> engine loops: kept at their own rate (pitch follows speed);
- one-shot channels (1, 10..13, 15) -> one-shots: resampled down to --one-shot-hz (16 kHz);
- long non-looping clips in the BGM bank (announcer) -> left for the cartridge stream, not packed.

Format (SOUND-PLAN.md section 6): signed 8-bit PCM (the SCSP has no ADPCM), each clip
normalised to its own peak with the lost level returned as an SCSP TL attenuation, loop
points kept. Writes, to --output (git-ignored or outside the repository):
- SNDRACE.PAK: the sound-RAM image of the clip region, ready to copy to its base;
- snd_race.h: the clip table (offset, length, loop, rate, OCT/FNS, TL) and the sound-RAM map;
- snd_race.json: the same for tools.
Fails (overflow control) if the map exceeds --ram-bytes.
"""
from pathlib import Path
import argparse
import json
import math
import os
import struct
import sys

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bgm_cdda as B  # noqa: E402
from import_course import extract_rom_course, normalize_rom  # noqa: E402

SCSP_RATE = 44100
RAM_BYTES = 512 * 1024
IDLE_BYTES = 32            # 68000 vectors and its one-instruction idle loop
STREAM_RING = 8 * 1024     # one announcer stream ring
STREAMS = 2
# The game's race events, as (sequence channel, io value) the way external.c starts them:
# player SEs on channel 10 with the SE id (Audio_PlayerSEStart), system and voice SEs on channel 1
# (Audio_SystemSEStart), the player's engine on channel 2 (Audio_PlayerEngineStart, NA_LEVEL_SE_2)
# and the nearest rival's engine on channel 14 (Audio_UpdateEnemyEngine, NA_LEVEL_SE_1).
EVENTS = (("boost", 10, 7), ("wall", 10, 3), ("energy_slow", 10, 19), ("energy_medium", 10, 20),
          ("energy_fast", 10, 21), ("lap_two", 1, 17), ("lap_final", 1, 18),
          ("engine_player", 2, 2), ("engine_rival", 14, 1))
EVENT_PARTS = 3
RACER_CHANNELS = (2, 3, 4, 5, 6, 7, 8, 9, 14)
ONE_SHOT_CHANNELS = (1, 10, 11, 12, 13, 15)


class PackError(Exception):
    pass


def resample(x: np.ndarray, src: int, dst: int) -> np.ndarray:
    """Band-limited resampling (Kaiser-windowed sinc, cut-off at the lower Nyquist)."""
    if src == dst:
        return x.astype(np.float64)
    ratio = dst / src
    cut = min(1.0, ratio)
    half = 24
    n_out = int(math.floor(len(x) * ratio))
    t = np.arange(n_out) / ratio
    base = np.floor(t).astype(np.int64)
    out = np.zeros(n_out)
    xp = np.concatenate([np.zeros(half + 1), x.astype(np.float64), np.zeros(half + 1)])
    for k in range(-half, half + 1):
        idx = base + k
        d = t - idx
        w = cut * np.sinc(cut * d) * np.kaiser(2 * half + 1, 8.0)[k + half]
        out += xp[idx + half + 1] * w
    return out


def oct_fns(rate: float):
    """SCSP pitch for a clip recorded at `rate`: 44100 * 2^OCT * (1 + FNS/1024)."""
    r = rate / SCSP_RATE
    octv = math.floor(math.log2(r))
    fns = int(round((r / 2 ** octv - 1) * 1024))
    if fns == 1024:
        octv, fns = octv + 1, 0
    if not -8 <= octv <= 7:
        raise PackError("rate %d Hz is outside the SCSP pitch range" % rate)
    return octv, fns


def to_s8(x: np.ndarray):
    peak = float(np.max(np.abs(x))) if len(x) else 0.0
    if peak < 1:
        return np.zeros(len(x), dtype=np.int8), 0
    scale = 127.0 / peak
    y = np.clip(np.round(x * scale), -128, 127).astype(np.int8)
    tl = int(round(20 * math.log10(32767.0 / peak) / 0.375))   # level returned through TL (0.375 dB steps)
    return y, max(0, min(255, tl))


def classify(font: dict, walker: B.SeqWalker, chans: dict, music: set) -> dict:
    started = {}
    for ch in sorted(chans):
        if ch == 0:
            continue
        for v in range(0, 128):
            try:
                c, layers, s = walker.start(chans[ch], v)
            except (IndexError, KeyError, TypeError):
                continue
            for _, pc in sorted(layers.items()):
                try:
                    notes = walker.notes(s, pc, c["instr"])
                except (IndexError, KeyError, TypeError):
                    continue
                for inst, pitch, vel, _ in notes:
                    if inst is not None and inst in font:
                        started.setdefault(inst, set()).add(ch)
    groups = {"engine": [], "one_shot": [], "stream": []}
    for inst in sorted(started):
        if inst in music:
            continue
        smp = font[inst]
        rate = round(32000 * smp["tuning"])
        n = smp["loop_end"] if smp["loop_forever"] else smp["size"] // 9 * 16
        if started[inst] & set(RACER_CHANNELS):
            groups["engine"].append(inst)
        elif smp["bank"] == 1 and not smp["loop_forever"] and n / rate >= 1.5:
            groups["stream"].append(inst)
        else:
            groups["one_shot"].append(inst)
    return groups


def event_parts(walker: B.SeqWalker, chans: dict, channel: int, io: int, font: dict) -> list:
    """(instrument, note, velocity) of each layer the channel starts for io, first note per layer."""
    c, layers, s = walker.start(chans[channel], io)
    parts = []
    for _, pc in sorted(layers.items()):
        notes = [n for n in walker.notes(s, pc, c["instr"]) if n[0] is not None and n[0] in font]
        if notes:
            parts.append((notes[0][0], notes[0][1], notes[0][2]))
    return parts[:EVENT_PARTS]


def build(clips: list, ram_bytes: int, streams: int = STREAMS, reverb_bytes: int = 0) -> dict:
    """Lay clips out in sound RAM. clips: dicts with 'name', 'data' (int8 array), loop fields, rate, tl."""
    offset = IDLE_BYTES
    table = []
    image = bytearray()
    for c in clips:
        offset = (offset + 1) & ~1                                 # SCSP sample addresses: even for 16-bit, any for 8-bit
        n = len(c["data"])
        if n > 0xFFFF:
            raise PackError("%s: %d samples, over the SCSP's 65,535-sample loop end" % (c["name"], n))
        table.append(dict(c, offset=offset, samples=n))
        image += bytes(offset - IDLE_BYTES - len(image))
        image += c["data"].astype(np.int8).tobytes()
        offset += n
    clip_end = (offset + 31) & ~31
    regions = [("idle", 0, IDLE_BYTES), ("clips", IDLE_BYTES, clip_end - IDLE_BYTES)]
    at = clip_end
    for i in range(streams):
        regions.append(("stream%d" % i, at, STREAM_RING))
        at += STREAM_RING
    if reverb_bytes:
        regions.append(("reverb", at, reverb_bytes))
        at += reverb_bytes
    if at > ram_bytes:
        raise PackError("sound RAM overflow: %d bytes mapped, %d available" % (at, ram_bytes))
    regions.append(("free", at, ram_bytes - at))
    return {"clips": table, "regions": regions, "image": bytes(image), "used": at}


def write_outputs(out: Path, pack: dict):
    out.mkdir(parents=True, exist_ok=True)
    image = pack["image"] + bytes((-len(pack["image"])) % 4)        # the CD loader moves whole longwords
    (out / "SNDRACE.PAK").write_bytes(image)
    fnv = 2166136261
    for b in image:
        fnv = ((fnv ^ b) * 16777619) & 0xFFFFFFFF
    def event_table(events):
        if not events:
            return ""
        defs = "".join("#define SND_RACE_EV_%s %d\n" % (n.upper(), k) for k, (n, _) in enumerate(events))
        body = ",\n".join("    { %s }" % ", ".join(
            ["{ %d, %d, %du, %du }" % (p["clip"], p["oct"], p["fns"], p["level"]) for p in row] +
            ["{ 0xFF, 0, 0u, 0u }"] * (EVENT_PARTS - len(row))) for _, row in events)
        return ("/* Race events: up to %d parts (clip index, pitch for the note, level 0..255); clip 0xFF = none. */\n"
                "typedef struct snd_race_part { uint8_t clip; int8_t oct; uint16_t fns; uint8_t level; } snd_race_part_t;\n"
                "%s#define SND_RACE_EVENT_COUNT %d\n#define SND_RACE_EVENT_PARTS %d\n"
                "static const snd_race_part_t snd_race_events[SND_RACE_EVENT_COUNT][SND_RACE_EVENT_PARTS] = {\n%s\n};\n"
                % (EVENT_PARTS, defs, len(events), EVENT_PARTS, body))

    rows = []
    for c in pack["clips"]:
        rows.append("    { 0x%05Xu, %5uu, %5uu, %5uu, %2d, %4uu, %3uu, %uu } /* %s */" % (
            c["offset"], c["samples"], c["loop_start"], c["loop_end"], c["oct"], c["fns"], c["tl"],
            1 if c["loop"] else 0, c["name"]))
    regions = "\n".join("#define SND_RACE_%s_BASE 0x%05Xu\n#define SND_RACE_%s_BYTES %uu" % (
        n.upper(), a, n.upper(), b) for n, a, b in pack["regions"])
    (out / "snd_race.h").write_text(
        "/* Generated by tools/sfx_pack.py from the owner's ROM; regenerate, never edit. Local only (git-ignored). */\n"
        "#ifndef SND_RACE_H\n#define SND_RACE_H\n#include <stdint.h>\n"
        "typedef struct snd_race_clip { uint32_t offset; uint16_t samples, loop_start, loop_end; int8_t oct;"
        " uint16_t fns; uint8_t tl, loop; } snd_race_clip_t;\n"
        "#define SND_RACE_CLIP_COUNT %d\n#define SND_RACE_USED_BYTES %uu\n%s\n"
        "/* SNDRACE.PAK: the clip region's image, loaded to SND_RACE_CLIPS_BASE (FNV-1a 32 of the file). */\n"
        "#define SND_RACE_PAK_BYTES %uu\n#define SND_RACE_PAK_HASH 0x%08Xu\n"
        "_Static_assert(SND_RACE_USED_BYTES <= 0x80000u, \"sound RAM map exceeds 512 KB\");\n"
        "_Static_assert(SND_RACE_CLIPS_BASE + SND_RACE_PAK_BYTES <= SND_RACE_STREAM0_BASE, \"clip image overlaps the streams\");\n"
        "static const snd_race_clip_t snd_race_clips[SND_RACE_CLIP_COUNT] = {\n%s\n};\n%s#endif\n"
        % (len(pack["clips"]), pack["used"], regions, len(image), fnv, ",\n".join(rows),
           event_table(pack.get("events", []))))
    meta = {"note": "Local, derived from the owner's ROM. Do not commit or share.", "used": pack["used"],
            "regions": pack["regions"], "events": pack.get("events", []),
            "clips": [{k: v for k, v in c.items() if k != "data"} for c in pack["clips"]]}
    (out / "snd_race.json").write_text(json.dumps(meta, indent=1) + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--output", type=Path, required=True)
    ap.add_argument("--one-shot-hz", type=int, default=16000)
    ap.add_argument("--ram-bytes", type=int, default=RAM_BYTES)
    ap.add_argument("--reverb-bytes", type=int, default=0)
    args = ap.parse_args()
    path = os.environ.get("FZX_TEST_ROM")
    if not path:
        raise SystemExit("FZX_TEST_ROM is required locally; keep its path out of logs")
    B.output_ok(args.output)
    raw = Path(path).read_bytes()
    extract_rom_course(raw)
    rom, _ = normalize_rom(raw)
    lay = B.layout(B.DECOMP)
    font = B.read_font(rom, lay, 0)
    args_table = B.seq_args_table(B.DECOMP)
    music = {i for t in B.music_table(rom, lay, args_table, font).values() for i, _ in t["parts"]}
    seq_seg = lay["seg"]["audio_seq"]
    seq = rom[seq_seg + lay["seqs"][0][0]: seq_seg + lay["seqs"][0][0] + lay["seqs"][0][1]]
    walker = B.SeqWalker(seq, args_table)
    chans = walker.channels()
    groups = classify(font, walker, chans, music)
    events = {name: event_parts(walker, chans, ch, io, font) for name, ch, io in EVENTS}
    for name, parts in events.items():
        if not parts:
            raise PackError("event %s starts no note" % name)
        for inst, _, _ in parts:
            if inst in groups["stream"]:                           # an event's voice clip is packed for the race
                groups["stream"].remove(inst)
                groups["one_shot"].append(inst)
    clips = []
    for kind in ("engine", "one_shot"):
        for inst in groups[kind]:
            smp = font[inst]
            rate = round(32000 * smp["tuning"])
            start = lay["seg"]["audio_table"] + lay["banks"][smp["bank"]][0] + smp["addr"]
            n = smp["loop_end"] if smp["loop_forever"] else smp["size"] // 9 * 16
            pcm = B.vadpcm_decode(rom[start: start + smp["size"]], smp["book"], smp["order"], n)
            B.check_loop_state(pcm, smp, inst)
            out_rate = rate if kind == "engine" else min(rate, args.one_shot_hz)
            y = resample(pcm, rate, out_rate)
            ls = int(round(smp["loop_start"] * out_rate / rate)) if smp["loop_forever"] else 0
            le = len(y) if smp["loop_forever"] else len(y)
            data, tl = to_s8(y)
            octv, fns = oct_fns(out_rate)
            clips.append({"name": "%s inst %d" % (kind, inst), "inst": inst, "kind": kind, "data": data,
                          "rate": out_rate, "loop": smp["loop_forever"], "loop_start": ls, "loop_end": le,
                          "oct": octv, "fns": fns, "tl": tl})
    pack = build(clips, args.ram_bytes, reverb_bytes=args.reverb_bytes)
    index = {c["inst"]: k for k, c in enumerate(pack["clips"])}
    pack["events"] = []
    for name, parts in events.items():
        row = []
        for inst, note, vel in parts:
            c = pack["clips"][index[inst]]
            rate = c["rate"] * 2 ** ((note - 39) / 12.0)               # the note transposes from middle C
            o, f = oct_fns(rate)
            row.append({"clip": index[inst], "oct": o, "fns": f, "level": min(255, int(round(255 * (vel / 127.0) ** 2)))})
        pack["events"].append((name, row))
    write_outputs(args.output, pack)
    kb = lambda b: b / 1024
    by = {k: sum(c["samples"] for c in pack["clips"] if c["kind"] == k) for k in ("engine", "one_shot")}
    print("engine loops: %d clips, %.1f KB; one-shots: %d clips, %.1f KB; streamed (not packed): %d clips"
          % (len(groups["engine"]), kb(by["engine"]), len(groups["one_shot"]), kb(by["one_shot"]), len(groups["stream"])))
    for name, base, size in pack["regions"]:
        print("  %-8s 0x%05X  %7.1f KB" % (name, base, kb(size)))
    print("sound RAM used %.1f of %.0f KB" % (kb(pack["used"]), kb(args.ram_bytes)))


if __name__ == "__main__":
    try:
        main()
    except (PackError, B.BgmError) as e:
        raise SystemExit("sfx_pack: %s" % e)
