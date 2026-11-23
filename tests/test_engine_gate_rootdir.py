"""HOST: the engine gate's rootdir check (Sector Beta, "TRACK LOAD" 35).

The scenes image keeps the disc's root file list in RAM. cdfs_filelist_root_read lost the file whose directory record ended
exactly on a 2,048-byte sector boundary, so one course's own pack (RLOD14.PAK) was not found once the decal packs moved the
layout. The check compares the list's count in RAM with the files on the built disc. The control is the old reader's count
(one short): the check must fail it.
"""
from pathlib import Path
import json
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import engine_gate  # noqa: E402

LIM = json.loads((ROOT / 'tools' / 'engine_gate_matrix.json').read_text())['limits']


def record(name, lba=100, size=2048, flags=0):
    nm = name if isinstance(name, bytes) else name.encode()
    n = 33 + len(nm)
    n += n & 1
    r = bytearray(n)
    r[0] = n
    r[2:6] = struct.pack('<I', lba)
    r[10:14] = struct.pack('<I', size)
    r[25] = flags
    r[32] = len(nm)
    r[33:33 + len(nm)] = nm
    return bytes(r)


def iso(names, boundary):
    """A volume whose root is built from NAMES; with BOUNDARY every sector but the last is filled to byte 2048 by growing its
    last record (system-use padding), so that record ends exactly on the boundary and no zero padding follows it."""
    sectors, cur = [], []
    for r in [record(b'\0', flags=2), record(b'\1', flags=2)] + [record(n + ';1') for n in names]:
        if sum(map(len, cur)) + len(r) > 2048:
            if boundary:
                last = bytearray(cur[-1]) + bytes(2048 - sum(map(len, cur)))
                last[0] = len(last)
                cur[-1] = bytes(last)
            sectors.append(b''.join(cur).ljust(2048, b'\0'))
            cur = []
        cur.append(r)
    sectors.append(b''.join(cur).ljust(2048, b'\0'))
    root = b''.join(sectors)
    pvd = bytearray(2048)
    pvd[0] = 1
    pvd[1:6] = b'CD001'
    pvd[158:162] = struct.pack('<I', 18)
    pvd[166:170] = struct.pack('<I', len(root))
    return bytes(16 * 2048) + bytes(pvd) + bytes(2048) + root


class Stub:
    lim = LIM

    def __init__(self):
        self.rows = []

    def add(self, check, name, ok, value, detail='', info=False, sink=None):
        self.rows.append((check, ok, value, detail))


class Mem:
    def __init__(self, kept):
        self.kept = kept

    def has(self, s):
        return s == 'kept_count'

    def words(self, s, n):
        return (self.kept,)


class RootDir(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.names = ['F%06d.PAK' % i for i in range(60)] + ['RGAD3.PAK', 'RLOD14.PAK']
        (Path(self.tmp.name) / 'fzs-engine.iso').write_bytes(iso(self.names, boundary=True))

    def tearDown(self):
        self.tmp.cleanup()

    def test_listing(self):
        files, packs, edge = engine_gate.iso_root_files(Path(self.tmp.name) / 'fzs-engine.iso')
        self.assertEqual(files, self.names)
        self.assertEqual(packs, ['RGAD3.PAK'])
        self.assertGreaterEqual(edge, 1)   # the fixture has the hazard

    def check(self, kept):
        g = Stub()
        engine_gate.Gate.rootdir_check(g, 'course-x', Mem(kept), self.tmp.name)
        return g.rows[0]

    def test_complete_list_passes(self):
        self.assertTrue(self.check(len(self.names))[1])

    def test_control_one_short_fails(self):
        row = self.check(len(self.names) - 1)   # what the old reader produced
        self.assertFalse(row[1])
        self.assertIn('list %d of %d' % (len(self.names) - 1, len(self.names)), row[2])

    def test_over_the_slots_fails(self):
        (Path(self.tmp.name) / 'fzs-engine.iso').write_bytes(iso(['F%06d.PAK' % i for i in range(LIM['root_entries'] + 1)], boundary=False))
        self.assertFalse(self.check(LIM['root_entries'] + 1)[1])


if __name__ == '__main__':
    unittest.main()
