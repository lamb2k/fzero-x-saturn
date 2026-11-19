"""HOST checks of the CD audio tools (tools/bgm_cdda.py, tools/disc_cdda.py). Synthetic data only: no ROM.

Each check has a failing control: a deliberately wrong input must be caught.
"""
import json
import struct
import sys
import tempfile
import unittest
import wave
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import bgm_cdda as B  # noqa: E402
import disc_cdda as D  # noqa: E402
import sfx_pack as P  # noqa: E402


def reference_decode(data, book, count):
    """The N64 SDK's vadpcm decoder in its table form (order 2), written independently of bgm_cdda."""
    out, prev = [], [0, 0]
    for f in range(0, len(data) - 8, 9):
        hdr = data[f]
        scale = 1 << (hdr >> 4)
        p = hdr & 15
        c = [list(book[p * 16: p * 16 + 8]), list(book[p * 16 + 8: p * 16 + 16])]
        ix = []
        for b in data[f + 1: f + 9]:
            for v in (b >> 4, b & 15):
                ix.append(((v - 16) if v > 7 else v) * scale)
        for j in range(2):
            vec = prev + ix[j * 8: j * 8 + 8]
            res = []
            for i in range(8):
                # row i: c0[i] * vec[0] + c1[i] * vec[1] + 2048 * in[i] + sum_{k<i} c1[i-1-k] * in[k]
                acc = c[0][i] * vec[0] + c[1][i] * vec[1] + 2048 * vec[2 + i]
                for k in range(i):
                    acc += c[1][i - 1 - k] * vec[2 + k]
                r = acc >> 11
                res.append(max(-32768, min(32767, r)))
            out += res
            prev = res[6:8]
        if len(out) >= count:
            break
    return out[:count]


class DecodeTests(unittest.TestCase):
    def setUp(self):
        rng = np.random.default_rng(7)
        self.book = tuple(int(x) for x in rng.integers(-2048, 2048, size=8 * 2 * 4))
        frames = bytearray()
        for _ in range(64):
            frames.append(int(rng.integers(0, 4)) << 4 | int(rng.integers(0, 4)))
            frames += bytes(int(x) for x in rng.integers(0, 256, size=8))
        self.data = bytes(frames)

    def test_matches_reference(self):
        got = B.vadpcm_decode(self.data, self.book, 2, 64 * 16)
        self.assertEqual([int(x) for x in got], reference_decode(self.data, self.book, 64 * 16))

    def test_loop_state_check_and_control(self):
        pcm = B.vadpcm_decode(self.data, self.book, 2, 64 * 16)
        smp = {"loop_forever": True, "loop_start": 100, "loop_state": tuple(int(x) for x in pcm[96:112])}
        B.check_loop_state(pcm, smp, 0)
        bad = dict(smp, loop_state=tuple(v + (i == 3) for i, v in enumerate(smp["loop_state"])))
        with self.assertRaises(B.BgmError):            # control: one wrong sample is caught
            B.check_loop_state(pcm, bad, 0)


class LayoutTests(unittest.TestCase):
    def test_track_layout(self):
        rng = np.random.default_rng(3)
        pcm = rng.normal(0, 3000, 22050 * 12)
        r = B.render_track(pcm, 22050 * 2 + 37, 22050 * 11 + 5, True, 60.0)
        n = len(r["pcm"])
        self.assertEqual(n % B.SECTOR, 0)                          # whole sectors
        self.assertEqual(r["intro_frames"] % B.SECTOR, 0)           # loop body starts on a sector
        self.assertGreaterEqual(n, 60 * B.CD_RATE)
        L = r["loop_frames"]
        jump = r["loop_sector"] * B.SECTOR
        # the drive resumes at the loop-back sector: the track's last frames must lead into it continuously
        loop, pj = r["loop"], r["phase_jump"]
        self.assertEqual((jump - r["intro_frames"]) % L, pj)
        self.assertAlmostEqual(r["pcm"][-1], loop[(pj - 1) % L], places=6)
        self.assertAlmostEqual(r["pcm"][jump], loop[pj], places=6)
        self.assertGreaterEqual(n - jump, L)                       # the repeated range holds a whole loop
        # the cycle join is continuous: no step at loop -> loop larger than the music's own steps
        steps = np.abs(np.diff(np.concatenate([loop, loop[:8]])))
        self.assertLessEqual(steps[L - 1], 4 * np.percentile(steps, 99.9))
        # control: without the fade the join at the jump is a jump in phase
        raw = np.concatenate([loop] * 3)
        self.assertNotAlmostEqual(raw[(n - r["intro_frames"]) % L - 1 + L], loop[(pj - 1) % L], places=3)

    def test_one_shot(self):
        r = B.render_track(np.ones(1000), 0, 0, False, 60.0)
        self.assertEqual(len(r["pcm"]) % B.SECTOR, 0)
        self.assertIsNone(r["loop_sector"])


class EccTests(unittest.TestCase):
    def test_sector_structure_and_control(self):
        user = bytes(range(256)) * 8
        s = D.mode1_sector(16, user)
        self.assertEqual(len(s), 2352)
        self.assertEqual(s[:12], b"\x00" + b"\xff" * 10 + b"\x00")
        self.assertEqual(s[12:16], bytes((0x00, 0x02, 0x16, 0x01)))        # FAD 166 = 00:02:16, mode 1
        self.assertEqual(struct.unpack("<I", s[0x810:0x814])[0], D.edc(s[:0x810]))
        self.assertEqual(s[0x814:0x81C], bytes(8))
        # ECC P/Q are a function of header + data: recomputing must reproduce them; a changed byte must not
        again = D.mode1_sector(16, user)
        self.assertEqual(again, s)
        other = D.mode1_sector(16, user[:100] + bytes((user[100] ^ 1,)) + user[101:])
        self.assertNotEqual(other[0x81C:], s[0x81C:])                     # control

    def test_edc_known_value(self):
        # CRC-32/CD-ROM-EDC check value for "123456789"
        self.assertEqual(D.edc(b"123456789"), 0x6EC2EDC4)


class DiscTests(unittest.TestCase):
    def test_build(self):
        with tempfile.TemporaryDirectory() as tmp:
            t = Path(tmp)
            (t / "music").mkdir()
            frames = np.zeros(B.SECTOR * 10, dtype="<i2")
            with wave.open(str(t / "music/music-00.wav"), "wb") as w:
                w.setnchannels(2), w.setsampwidth(2), w.setframerate(44100)
                w.writeframes(np.repeat(frames, 2).tobytes())
            (t / "music/manifest.json").write_text(json.dumps({"tracks": [
                {"music_id": 0, "file": "music-00.wav", "sectors": 10, "loop_sector": 3}]}))
            (t / "a.iso").write_bytes(bytes(2048 * 20))
            r = D.build(t / "a.iso", t / "music", [0], t / "out", "x", t / "h/cdda_tracks.h")
            self.assertEqual(r["sectors"], 20 + 150 + 10)
            cue = (t / "out/x.cue").read_text()
            self.assertIn("TRACK 01 MODE1/2352", cue)
            self.assertIn("TRACK 02 AUDIO", cue)
            self.assertIn("INDEX 01 00:02:00", cue)
            self.assertEqual((t / "out/x (Track 01).bin").stat().st_size, 20 * 2352)
            self.assertEqual((t / "out/x (Track 02).bin").stat().st_size, 160 * 2352)
            self.assertIn("{ 0, 2, 10u, 3u }", (t / "h/cdda_tracks.h").read_text())
            (t / "a.iso").write_bytes(bytes(2048 * 20 + 1))
            with self.assertRaises(D.DiscError):                         # control: not whole sectors
                D.build(t / "a.iso", t / "music", [0], t / "out2", "x")


class SfxPackTests(unittest.TestCase):
    def clip(self, n, name="c"):
        return {"name": name, "data": np.ones(n, dtype=np.int8), "loop": False, "loop_start": 0, "loop_end": n,
                "rate": 16000, "oct": -2, "fns": 0, "tl": 0, "kind": "one_shot"}

    def test_layout_and_overflow_control(self):
        pack = P.build([self.clip(1000, "a"), self.clip(3, "b"), self.clip(500, "c")], 512 * 1024)
        offs = [c["offset"] for c in pack["clips"]]
        self.assertEqual(offs[0], P.IDLE_BYTES)
        self.assertTrue(all(o % 2 == 0 for o in offs))
        for c, d in zip(pack["clips"], pack["clips"][1:]):
            self.assertLessEqual(c["offset"] + c["samples"], d["offset"])      # no overlap
        ends = [b + n for _, b, n in pack["regions"]]
        self.assertEqual(ends[-1], 512 * 1024)
        for (_, b, n), (_, b2, _) in zip(pack["regions"], pack["regions"][1:]):
            self.assertEqual(b + n, b2)                                         # regions tile sound RAM
        with self.assertRaises(P.PackError):                                    # control: overflow is caught
            P.build([self.clip(60000), self.clip(60000)], 100000)
        with self.assertRaises(P.PackError):                                    # control: SCSP loop-end limit
            P.build([self.clip(70000)], 512 * 1024)

    def test_pitch_and_level(self):
        self.assertEqual(P.oct_fns(44100), (0, 0))
        self.assertEqual(P.oct_fns(22050), (-1, 0))
        o, f = P.oct_fns(16000)
        self.assertAlmostEqual(44100 * 2 ** o * (1 + f / 1024), 16000, delta=16000 * 0.001)
        y, tl = P.to_s8(np.array([0.0, 16383.5, -16383.5]))
        self.assertEqual(int(np.max(np.abs(y.astype(int)))), 127)
        self.assertEqual(tl, 16)                                                # -6.02 dB = 16 TL steps

    def test_resample_tone(self):
        t = np.arange(32000)
        x = 10000 * np.sin(2 * np.pi * 1000 * t / 32000)
        y = P.resample(x, 32000, 16000)
        self.assertEqual(len(y), 16000)
        ref = 10000 * np.sin(2 * np.pi * 1000 * np.arange(16000) / 16000)
        self.assertLess(np.max(np.abs(y[100:-100] - ref[100:-100])), 60)


if __name__ == "__main__":
    unittest.main()
