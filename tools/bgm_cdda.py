#!/usr/bin/env python3
"""Render the original background music to CD audio tracks, locally, from the owner's ROM.

F-Zero X's cartridge music is not sequenced: each piece is one long mono VADPCM sample
(22,050 Hz) in the BGM sample bank, played as a single legato note at its own pitch by
channel 0 of the sound-effect sequence, with an intro and a loop region. This tool:

1. reads the soundfont, sequence and sample tables at the pinned decomp's ROM layout;
2. walks channel 0 of the sound-effect sequence with each music id on io port 0 (the
   way Audio_SEStart(0, id) starts music) to find the instrument and note level;
3. decodes each piece once (VADPCM, order 2), upsamples 2x to 44,100 Hz, makes it
   dual mono, and lays it out as a Red Book track: the intro once, then the loop body
   repeated to at least --course-seconds (or --menu-seconds), with the loop body
   starting on a CD sector and a loop-back sector whose phase matches the track end;
4. writes one WAV per track plus manifest.json (loop sectors, levels) to --output.

Nothing it writes may be committed or shared: the WAVs and the manifest are derived
from the ROM. --output must be outside the repository or git-ignored.
"""
from pathlib import Path
import argparse
import hashlib
import json
import math
import os
import re
import struct
import subprocess
import sys
import wave

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from import_course import extract_rom_course, normalize_rom  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
# A worktree without the submodule can point FZX_DECOMP at a checkout of the pinned decomp.
DECOMP = Path(os.environ.get("FZX_DECOMP", ROOT / "upstream/G-Diffuser/decomp"))
SECTOR = 588                 # stereo frames per CD-DA sector (1/75 s)
CD_RATE = 44100
SEQ_IO_NONE = -1


class BgmError(Exception):
    pass


# ---------------------------------------------------------------- pinned layout

def layout(decomp: Path) -> dict:
    """Segment starts from the US rev0 asset yaml and table rows from audio_tables.c."""
    yaml = (decomp / "yamls/us/rev0/assets.yaml").read_text()
    seg = {}
    for name in ("audio_bank", "audio_seq", "audio_table"):
        m = re.search(r"- name: %s\b.*?start: (0x[0-9A-Fa-f]+)" % name, yaml, re.S)
        if not m:
            raise BgmError("pinned asset yaml has no %s segment" % name)
        seg[name] = int(m.group(1), 16)
    tables = (decomp / "src/audio/rom/audio_tables.c").read_text()

    def rows(table):
        body = tables[tables.index(table):]
        body = body[:body.index("};")]
        return [tuple(int(x, 16) for x in r) for r in
                re.findall(r"\{\s*(0x[0-9A-Fa-f]+|0),\s*(0x[0-9A-Fa-f]+),\s*MEDIUM_CART", body.replace("{ 0,", "{ 0x0,"))]
    fonts = []
    body = tables[tables.index("gSoundFontTableData"):]
    body = body[:body.index("};")]
    for m in re.finditer(r"\{\s*(0x[0-9A-Fa-f]+|0),\s*(0x[0-9A-Fa-f]+),\s*MEDIUM_CART,\s*CACHEPOLICY_\d,\s*"
                         r"\((\w+) << 8\)\s*\|\s*(\w+),\s*\((\d+) << 8\)", body):
        fonts.append({"offset": int(m.group(1), 16), "size": int(m.group(2), 16),
                      "bank1": m.group(3), "bank2": m.group(4), "instruments": int(m.group(5))})
    seqs = rows("gSequenceTableData")
    banks = rows("gSampleBankTableData")
    if len(fonts) != 2 or len(seqs) != 2 or len(banks) != 3:
        raise BgmError("pinned audio tables changed shape")
    bank_ids = {"SAMPLE_SOUND_EFFECTS": 0, "SAMPLE_BGM": 1, "SAMPLE_GUITAR": 2}
    for f in fonts:
        f["bank1"] = bank_ids.get(f["bank1"])
        f["bank2"] = bank_ids.get(f["bank2"])
    return {"seg": seg, "fonts": fonts, "seqs": seqs, "banks": banks}


def seq_args_table(decomp: Path) -> list:
    txt = (decomp / "src/audio/rom/lib/seqplayer.c").read_text()
    body = txt[txt.index("u8 sSeqInstructionArgsTable[] = {"):]
    body = body[:body.index("};")]
    table = []
    for m in re.finditer(r"CMD_ARGS_(\d)\(([^)]*)\)", body):
        types = [t.strip() for t in m.group(2).split(",") if t.strip()]
        table.append([2 if t == "s16" else 1 for t in types][:int(m.group(1))])
    if len(table) != 0x50:
        raise BgmError("pinned sequence argument table changed")
    return table


# ---------------------------------------------------------------- soundfont

def read_font(rom: bytes, lay: dict, font_id: int) -> dict:
    f = lay["fonts"][font_id]
    base = lay["seg"]["audio_bank"] + f["offset"]
    u32 = lambda o: struct.unpack_from(">I", rom, o)[0]
    out = {}
    for i in range(1, f["instruments"] + 1):
        off = u32(base + 4 * i)
        if not off:
            continue
        ib = base + off
        soff, tuning = struct.unpack_from(">If", rom, ib + 16)        # normal-pitch tuned sample
        sb = base + soff
        w0 = u32(sb)
        codec, medium, size = w0 >> 28, (w0 >> 26) & 3, w0 & 0xFFFFFF
        addr, loop_off, book_off = struct.unpack_from(">III", rom, sb + 4)
        lstart, lend, lcount = struct.unpack_from(">III", rom, base + loop_off)
        loop_state = struct.unpack_from(">16h", rom, base + loop_off + 16) if lcount else None
        order, npred = struct.unpack_from(">ii", rom, base + book_off)
        book = struct.unpack_from(">%dh" % (8 * order * npred), rom, base + book_off + 8)
        bank = f["bank1"] if medium == 0 else f["bank2"]
        out[i - 1] = {"codec": codec, "bank": bank, "size": size, "addr": addr, "tuning": tuning,
                      "loop_start": lstart, "loop_end": lend, "loop_forever": lcount == 0xFFFFFFFF, "loop_state": loop_state,
                      "order": order, "npred": npred, "book": book}
    return out


# ---------------------------------------------------------------- sequence walk

class _St:
    def __init__(self, pc):
        self.pc, self.stack, self.loops, self.value = pc, [], [], 0


def _s8(v):
    return v - 256 if v >= 128 else v


class SeqWalker:
    """Just enough of the sequence player to see which instrument a channel starts.

    Follows seqplayer.c's channel and layer interpreters for the ops the music channel
    uses; anything it does not model ends the walk instead of guessing."""

    SEQ_ARGS = {0xDF: [1], 0xDE: [1], 0xDD: [1], 0xDC: [1], 0xDA: [1, 2], 0xDB: [1], 0xD9: [1], 0xD7: [2],
                0xD6: [2], 0xD5: [1], 0xD3: [1], 0xD0: [1], 0xCC: [1], 0xC9: [1], 0xC8: [1], 0xC7: [1, 2]}

    def __init__(self, seq: bytes, args: list):
        self.seq0, self.args = bytes(seq), args

    def _rd8(self, s, st):
        v = s[st.pc]
        st.pc += 1
        return v

    def _rd16(self, s, st):
        v = (s[st.pc] << 8) | s[st.pc + 1]
        st.pc += 2
        return v

    def _rdc16(self, s, st):
        v = self._rd8(s, st)
        return ((v & 0x7F) << 8) | self._rd8(s, st) if v & 0x80 else v

    def _argv(self, s, st, cmd):
        return [self._rd16(s, st) if w == 2 else self._rd8(s, st) for w in self.args[cmd - 0xB0]]

    def _flow(self, s, st, cmd, arg):
        if cmd == 0xFF:
            if not st.stack:
                return "end"
            st.pc = st.stack.pop()
        elif cmd in (0xFD, 0xFE):
            return "delay"
        elif cmd == 0xFC:
            st.stack.append(st.pc)
            st.pc = arg & 0xFFFF
        elif cmd == 0xF8:
            st.loops.append(arg)
            st.stack.append(st.pc)
        elif cmd == 0xF7:
            st.loops[-1] -= 1
            if st.loops[-1]:
                st.pc = st.stack[-1]
            else:
                st.loops.pop()
                st.stack.pop()
        elif cmd == 0xF6:
            st.stack.pop()
            st.loops.pop()
        elif cmd in (0xF5, 0xF9, 0xFA, 0xFB):
            take = {0xFA: st.value == 0, 0xF9: st.value < 0, 0xF5: st.value >= 0, 0xFB: True}[cmd]
            if take:
                st.pc = arg & 0xFFFF
        elif cmd in (0xF2, 0xF3, 0xF4):
            take = {0xF3: st.value == 0, 0xF2: st.value < 0, 0xF4: True}[cmd]
            if take:
                st.pc += _s8(arg & 0xFF)
        return None

    def channels(self) -> dict:
        s, st, chans = bytearray(self.seq0), _St(0), {}
        for _ in range(400):
            cmd = self._rd8(s, st)
            if cmd >= 0xF2:
                a = self._argv(s, st, cmd)
                if self._flow(s, st, cmd, a[0] if a else 0) in ("end", "delay"):
                    break
            elif cmd >= 0xC0:
                for w in self.SEQ_ARGS.get(cmd, []):
                    (self._rd16 if w == 2 else self._rd8)(s, st)
            elif cmd & 0xF0 == 0x90:
                chans[cmd & 0xF] = self._rd16(s, st)
        return chans

    def start(self, chan_pc: int, io0: int):
        """Run a channel with io port 0 = io0 until it starts layers; return (channel, layers, seq copy)."""
        s, st = bytearray(self.seq0), _St(chan_pc)
        io = [SEQ_IO_NONE] * 8
        io[0] = io0
        ch = {"instr": None, "vol": 127}
        dyn, layers, delays = None, {}, 0
        entry = lambda v: (s[dyn + 2 * v] << 8) | s[dyn + 2 * v + 1]
        for _ in range(4000):
            cmd = self._rd8(s, st)
            if cmd >= 0xB0:
                a = self._argv(s, st, cmd)
                a0 = a[0] if a else 0
                if cmd >= 0xF2:
                    r = self._flow(s, st, cmd, a0)
                    if r == "end":
                        break
                    if r == "delay":
                        delays += 1
                        if layers or delays > 50:
                            break
                elif cmd == 0xC1:
                    ch["instr"] = a0
                elif cmd == 0xC2:
                    dyn = a0
                elif cmd == 0xC5 and st.value != SEQ_IO_NONE:
                    dyn = entry(st.value)
                elif cmd == 0xC7:
                    s[a[1]] = (st.value + a0) & 0xFF
                elif cmd == 0xC8:
                    st.value -= _s8(a0)
                elif cmd == 0xCC:
                    st.value = _s8(a0)
                elif cmd == 0xC9:
                    st.value &= _s8(a0)
                elif cmd == 0xCB:
                    st.value = s[a0 + st.value]
                elif cmd == 0xDF:
                    ch["vol"] = a0
                elif cmd in (0xE4, 0xEB) and st.value != SEQ_IO_NONE:
                    st.stack.append(st.pc)
                    st.pc = entry(st.value)
                elif cmd == 0xEA:
                    break
                continue
            if cmd >= 0x80:
                lb, op = cmd & 7, cmd & 0xF8
                if op == 0x80:
                    st.value = 0 if lb in layers else SEQ_IO_NONE
                elif op == 0x88:
                    layers[lb] = self._rd16(s, st)
                elif op == 0x98 and st.value != SEQ_IO_NONE:
                    layers[lb] = entry(st.value)
                continue
            lb, op = cmd & 0xF, cmd & 0xF0
            if op == 0x00:
                delays += 1
                if layers or delays > 50:
                    break
            elif op == 0x60:
                st.value = io[lb] if lb < 8 else SEQ_IO_NONE
                if lb < 2:
                    io[lb] = SEQ_IO_NONE
            elif op == 0x70:
                io[lb & 7] = st.value
            elif op == 0x50:
                st.value -= io[lb & 7]
            elif op == 0x20:
                self._rd16(s, st)
            elif op in (0x30, 0x40):
                self._rd8(s, st)
        return ch, layers, s

    def notes(self, s: bytearray, pc: int, instr):
        """A layer's notes up to its first jump back: (instrument, pitch, velocity, duration ticks)."""
        st, out, seen = _St(pc), [], set()
        for _ in range(400):
            if st.pc in seen:
                break
            seen.add(st.pc)
            cmd = self._rd8(s, st)
            if cmd >= 0xF2:
                a = self._argv(s, st, cmd)
                if cmd == 0xFB and (a[0] & 0xFFFF) < st.pc:   # loop back: the steady state repeats
                    break
                if self._flow(s, st, cmd, a[0] if a else 0) == "end":
                    break
                continue
            if cmd >= 0xC0:
                if cmd in (0xC0, 0xC3):
                    self._rdc16(s, st)
                elif cmd == 0xC6:
                    instr = self._rd8(s, st)
                elif cmd in (0xC1, 0xC2, 0xC9, 0xCA, 0xCD, 0xCE, 0xCF):
                    self._rd8(s, st)
                elif cmd == 0xCB:
                    self._rd16(s, st)
                    self._rd8(s, st)
                elif cmd == 0xC7:
                    mode = self._rd8(s, st)
                    self._rd8(s, st)
                    (self._rd8 if mode & 0x80 else self._rdc16)(s, st)
                continue
            kind, pitch = cmd & 0xC0, cmd & 0x3F
            if kind == 0x00:
                dur = self._rdc16(s, st)
                vel = self._rd8(s, st)
                self._rd8(s, st)
            elif kind == 0x40:
                dur = self._rdc16(s, st)
                vel = self._rd8(s, st)
            else:
                dur = None
                vel = self._rd8(s, st)
                self._rd8(s, st)
            out.append((instr, pitch, vel, dur))
        return out


def music_table(rom: bytes, lay: dict, args: list, font: dict) -> dict:
    """Music id -> list of (instrument, velocity) as channel 0 plays them (an intro jingle may precede the loop)."""
    seq_seg = lay["seg"]["audio_seq"]
    seq = rom[seq_seg + lay["seqs"][0][0]: seq_seg + lay["seqs"][0][0] + lay["seqs"][0][1]]
    walker = SeqWalker(seq, args)
    chans = walker.channels()
    if 0 not in chans:
        raise BgmError("sound-effect sequence does not start channel 0")
    table = {}
    for music_id in range(0, 32):
        try:
            ch, layers, s = walker.start(chans[0], music_id)
        except IndexError:                                       # id past the end of the music table
            continue
        if not layers:
            continue
        notes = []
        for _, pc in sorted(layers.items()):
            notes += [n for n in walker.notes(s, pc, ch["instr"]) if n[0] is not None]
        notes = [n for n in notes if n[0] in font]
        if notes and all(n[1] == 39 for n in notes):            # middle C: the sample's own rate
            table[music_id] = {"parts": [(n[0], n[2]) for n in notes], "channel_volume": ch["vol"]}
    return table


# ---------------------------------------------------------------- decode and render

def vadpcm_decode(data: bytes, book: tuple, order: int, count: int) -> np.ndarray:
    if order != 2:
        raise BgmError("only order-2 codebooks are expected")
    pred = [(book[p * 16: p * 16 + 8], book[p * 16 + 8: p * 16 + 16]) for p in range(len(book) // 16)]
    out = np.zeros(count + 16, dtype=np.int32)
    l6 = l7 = 0
    n = 0
    for f in range(0, len(data) - 8, 9):
        if n >= count:
            break
        hdr = data[f]
        shift = hdr >> 4
        c1, c2 = pred[hdr & 15]
        nib = []
        for b in data[f + 1: f + 9]:
            for v in (b >> 4, b & 15):
                nib.append(((v - 16) if v > 7 else v) << shift)
        for g in (0, 8):
            ins = nib[g: g + 8]
            res = []
            for i in range(8):
                total = c1[i] * l6 + c2[i] * l7
                for j in range(i):
                    total += ins[j] * c2[i - 1 - j]
                r = ((ins[i] << 11) + total) >> 11
                res.append(32767 if r > 32767 else (-32768 if r < -32768 else r))
            out[n: n + 8] = res
            n += 8
            l6, l7 = res[6], res[7]
    return out[:count].astype(np.float64)


def check_loop_state(pcm: np.ndarray, smp: dict, inst: int):
    """Decoder self-check: a looping sample stores the decoded 16-sample frame holding its loop start
    (the state the RSP restarts from). The decode must reproduce it exactly."""
    if not smp["loop_forever"] or smp["loop_state"] is None:
        return
    frame = (smp["loop_start"] // 16) * 16
    got = tuple(int(v) for v in pcm[frame: frame + 16])
    if got != tuple(smp["loop_state"]):
        raise BgmError("instrument %d: decode does not reproduce the stored loop state" % inst)


def halfband_upsample2(x: np.ndarray) -> np.ndarray:
    """2x upsample with a Kaiser-windowed sinc low-pass at the source Nyquist (linear phase, delay compensated)."""
    taps = 255
    n = np.arange(taps) - (taps - 1) / 2
    h = np.sinc(n / 2) * np.kaiser(taps, 10.0)
    h *= 2 / h.sum()
    up = np.zeros(len(x) * 2)
    up[0::2] = x
    y = np.convolve(up, h)
    d = (taps - 1) // 2
    return y[d: d + len(up)]


def render_track(pcm22: np.ndarray, loop_start: int, loop_end: int, loops_forever: bool, target_s: float) -> dict:
    """Lay one piece out as a CD track (44.1 kHz frames). Returns the mono buffer and its sector map."""
    up = halfband_upsample2(pcm22)
    if not loops_forever:
        lead = (-len(up)) % SECTOR
        body = np.concatenate([up, np.zeros(lead)])
        return {"pcm": body, "loop_sector": None, "loop_phase_error": 0, "intro_frames": len(up), "loop_frames": 0}
    ls, le = 2 * loop_start, 2 * loop_end
    L = le - ls
    pad = (-ls) % SECTOR                                         # loop body starts on a sector boundary
    intro = np.concatenate([np.zeros(pad), up[:ls]])
    loop = up[ls:le]
    k = max(1, math.ceil((target_s * CD_RATE - len(intro)) / L))
    body = np.concatenate([intro] + [loop] * (k + 1))
    end = len(intro) + k * L
    end_sec = -(-end // SECTOR) * SECTOR                         # round the end up to a whole sector
    phase_end = (end_sec - len(intro)) % L
    best_m, best_err = 0, L
    limit = (end_sec - len(intro) - L) // SECTOR                  # the replayed range spans at least one loop
    for m in range(0, max(1, limit)):
        e = (m * SECTOR - phase_end) % L
        e = min(e, L - e)
        if e < best_err:
            best_m, best_err = m, e
            if e == 0:
                break
    return {"pcm": body[:end_sec], "loop_sector": len(intro) // SECTOR + best_m, "loop_phase_error": int(best_err),
            "intro_frames": len(intro), "loop_frames": L, "pad_frames": pad}


def write_wav(path: Path, mono: np.ndarray):
    s = np.clip(np.round(mono), -32768, 32767).astype("<i2")
    stereo = np.repeat(s, 2)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(CD_RATE)
        w.writeframes(stereo.tobytes())


def output_ok(out: Path):
    out = out.resolve()
    if out.is_relative_to(ROOT.resolve()):
        r = subprocess.run(["git", "-C", str(ROOT), "check-ignore", "--quiet", str(out / "x.wav")], capture_output=True)
        if r.returncode != 0:
            raise BgmError("in-repository output must be git-ignored")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--output", type=Path, required=True)
    ap.add_argument("--ids", default="all", help="comma-separated music ids, or 'all'")
    ap.add_argument("--course-seconds", type=float, default=240.0, help="minimum track length for course music")
    ap.add_argument("--menu-seconds", type=float, default=120.0, help="minimum track length for other looping music")
    ap.add_argument("--peak-dbfs", type=float, default=-1.0, help="peak of the loudest track after the shared gain")
    ap.add_argument("--list", action="store_true", help="print the music table and stop")
    ap.add_argument("--seam-test", action="store_true",
                    help="also write 10 s around each loop-back (track end, then the loop-back sector)")
    args = ap.parse_args()
    path = os.environ.get("FZX_TEST_ROM")
    if not path:
        raise SystemExit("FZX_TEST_ROM is required locally; keep its path out of logs")
    output_ok(args.output)
    raw = Path(path).read_bytes()
    extract_rom_course(raw)                                      # fingerprint: unmodified US rev0
    rom, _ = normalize_rom(raw)
    pin = json.loads((ROOT / "upstream.lock.json").read_text())["decompilation"]["commit"]
    head = subprocess.run(["git", "-C", str(DECOMP), "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
    if head != pin:
        raise BgmError("the decomp checkout is not at the pinned commit (upstream.lock.json)")
    lay = layout(DECOMP)
    font = read_font(rom, lay, 0)
    table = music_table(rom, lay, seq_args_table(DECOMP), font)
    if args.list:
        for mid, t in sorted(table.items()):
            parts = ", ".join("inst %d vel %d %.1f s%s" % (i, v, font[i]["loop_end"] / (32000 * font[i]["tuning"]),
                                                           " loop" if font[i]["loop_forever"] else "")
                              for i, v in t["parts"])
            print("music %2d: %s" % (mid, parts))
        return
    ids = sorted(table) if args.ids == "all" else [int(x) for x in args.ids.split(",")]
    course_ids = set(range(0, 14))                               # BGM_MUTE_CITY .. BGM_NEW_04 (include/sfx.h)
    bank_base = lay["seg"]["audio_table"]
    cache, tracks = {}, []

    def decoded(inst):
        if inst not in cache:
            smp = font[inst]
            rate = round(32000 * smp["tuning"])
            if rate != 22050:
                raise BgmError("music sample at %d Hz; this renderer expects 22,050" % rate)
            start = bank_base + lay["banks"][smp["bank"]][0] + smp["addr"]
            count = smp["loop_end"] if smp["loop_forever"] else smp["size"] // 9 * 16
            cache[inst] = vadpcm_decode(rom[start: start + smp["size"]], smp["book"], smp["order"], count)
            check_loop_state(cache[inst], smp, inst)
        return cache[inst]

    def gain(mid, vel):
        return (vel / 127.0) ** 2 * (table[mid]["channel_volume"] / 127.0)

    for mid in ids:
        if mid not in table:
            raise BgmError("music id %d does not start a note" % mid)
        tracks.append((mid, [(inst, gain(mid, vel)) for inst, vel in table[mid]["parts"]]))
    # one shared gain over ALL music keeps the game's relative levels whatever --ids selects;
    # the loudest piece peaks at --peak-dbfs
    peak = max(np.max(np.abs(decoded(i))) * gain(mid, v) for mid, t in table.items() for i, v in t["parts"])
    # one shared gain keeps the game's relative levels; the loudest piece peaks at --peak-dbfs
    shared = 32767 * 10 ** (args.peak_dbfs / 20) / peak
    args.output.mkdir(parents=True, exist_ok=True)
    manifest = {"note": "Local rendering from the owner's ROM. Do not commit, upload or share.",
                "rate": CD_RATE, "sector_frames": SECTOR, "shared_gain": shared, "tracks": []}
    for mid, pieces in tracks:
        target = args.course_seconds if mid in course_ids else args.menu_seconds
        segs = []
        loop_info = None
        for n, (inst, g) in enumerate(pieces):
            smp = font[inst]
            last = n == len(pieces) - 1
            r = render_track(decoded(inst) * g * shared, smp["loop_start"], smp["loop_end"],
                             smp["loop_forever"] and last, target)
            if last:
                lead = sum(len(x) for x in segs)
                if lead % SECTOR:
                    raise BgmError("intro jingle is not sector aligned")
                loop_info = r
                loop_info["lead_sectors"] = lead // SECTOR
            segs.append(r["pcm"])
        pcm = np.concatenate(segs)
        name = "music-%02d.wav" % mid
        write_wav(args.output / name, pcm)
        sectors = len(pcm) // SECTOR
        entry = {"music_id": mid, "file": name, "sectors": sectors, "seconds": round(len(pcm) / CD_RATE, 3),
                 "instruments": [i for i, _ in pieces],
                 "loop_sector": None if loop_info["loop_sector"] is None else loop_info["lead_sectors"] + loop_info["loop_sector"],
                 "loop_phase_error_frames": loop_info["loop_phase_error"],
                 "loop_seconds": round(loop_info["loop_frames"] / CD_RATE, 3),
                 "sha256": hashlib.sha256((args.output / name).read_bytes()).hexdigest()}
        manifest["tracks"].append(entry)
        if args.seam_test and entry["loop_sector"] is not None:
            seam = 5 * CD_RATE
            jump = entry["loop_sector"] * SECTOR
            write_wav(args.output / ("music-%02d-loopback-test.wav" % mid),
                      np.concatenate([pcm[len(pcm) - seam:], pcm[jump: jump + seam]]))
        print("music %2d -> %s: %d sectors (%.1f s), loop-back sector %s, phase error %d frames"
              % (mid, name, sectors, entry["seconds"], entry["loop_sector"], entry["loop_phase_error_frames"]))
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=1) + "\n")


if __name__ == "__main__":
    try:
        main()
    except BgmError as e:
        raise SystemExit("bgm_cdda: %s" % e)
