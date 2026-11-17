"""ROM-free controls for the bounded original countdown recipe converter."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from countdown_assets import geometry, words, ArtError, LISTS


def fixture():
    setup = [(0xd9000000, 0), (0xd9ffffff, 4), (0xd7000002, 0xffffffff),
             (0xe7000000, 0), (0xe3000a01, 0), (0xe200001c, 0),
             (0xfc000000, 0), (0xe3000800, 0), (0xe2001e01, 0),
             (0xe3000f00, 0), (0xe3001001, 0), (0xe3000d01, 0),
             (0xe3000c00, 0), (0xe3001201, 0), (0xe3001402, 0), (0xdf000000, 0)]
    def texture(address):
        return [(0xfd100000, address), (0xf5100000, 0x07054150),
                (0xe6000000, 0), (0xf3000000, 0x073ff100), (0xe7000000, 0),
                (0xf5101000, 0x00054150), (0xf2000000, 0x0007c07c)]
    address, rows = 0, {}
    def group(n):
        nonlocal address
        address += 64
        # Every face is synthetic, non-degenerate, with disjoint vertex rows.
        rows[address] = [[address+(j//4)*8+x, y, 0, x*32, y*32, 0, 0, 127, 255]
                         for j,(x,y) in enumerate([(0,0),(4,0),(4,4),(0,4)]*(n//4))]
        result = [(0x01000000 | (n << 12) | (n << 1), address)]
        for base in range(0, n, 4):
            a,b,c,d = [2*(base+i) for i in range(4)]
            result.append((0x06000000 | a<<16 | b<<8 | c, a<<16 | c<<8 | d))
        return result
    frame = texture(1)+group(12)+group(12)+[(0xe7000000,0)]
    frame += texture(2)+group(8)+group(8)+group(4)+group(20)+[(0xe7000000,0)]
    frame += texture(3)+group(4)+group(4)+group(8)+[(0xdf000000,0)]
    mouth = group(4)+group(4)+[(0xdf000000,0)]
    panel = group(4)[0][1]
    lists = dict(zip(LISTS, (setup, frame, mouth)))
    def read(at, n):
        if at not in rows or len(rows[at]) != n:
            raise ArtError('Invalid synthetic source span')
        return rows[at], 'fixture_%d'%at
    return lists, rows, read, panel


class CountdownTest(unittest.TestCase):
    def test_all_source_sections_and_dynamic_slots(self):
        lists, rows, read, panel = fixture()
        model = geometry(lists, read, lambda at: 'surface_%d'%at, panel)
        self.assertEqual((len(model['quads']),len(model['triangles'])), (23,46))
        self.assertEqual(sum(r['count'] for r in model['source_vertices']),92)
        self.assertEqual(model['sections'], dict(frame=[0,20],mouth=[20,2],digit=[22,1]))
        slots = [model['materials'][q[4]]['texture']['name'] for q in model['quads']]
        self.assertEqual(slots,['surface_1']*6+['surface_2']*10+['surface_3']*4+
                         ['countdown.mouth']*2+['countdown.digit'])
        for source in model['source_vertices']:
            self.assertEqual(source['count'], len(rows[source['address']]))

    def test_unsupported_call_and_early_end(self):
        for mutation in ((0xde000000,0), (0xdf000000,0)):
            lists, _, read, panel = fixture()
            lists[LISTS[1]][8] = mutation
            with self.assertRaises(ArtError):
                geometry(lists, read, str, panel)

    def test_bad_span_triangle_and_topology(self):
        for mutation in ((0x0100c018,999999), (0x01004008,64)):
            lists, _, read, panel = fixture()
            lists[LISTS[1]][7] = mutation
            with self.assertRaises(ArtError):
                geometry(lists, read, str, panel)
        for mutation in ((0x06000240,0x00000406), (0x06000204,0x00000206)):
            lists, _, read, panel = fixture()
            lists[LISTS[1]][8] = mutation
            with self.assertRaises(ArtError):
                geometry(lists, read, str, panel)

    def test_words_bounds_and_termination(self):
        rows = {'list':dict(type='GFX',offset=0), 'next':dict(type='VTX',offset=8)}
        self.assertEqual(words(bytes.fromhex('df00000000000000'),rows,'list'),[(0xdf000000,0)])
        for raw in (b'',bytes(8),bytes.fromhex('df00000000000001')):
            with self.assertRaises(ArtError): words(raw,rows,'list')


if __name__ == '__main__': unittest.main()
