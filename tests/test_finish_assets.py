"""Bounded source recipe failures use synthetic commands, never retail bytes."""
from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from finish_assets import recipe,geometry,ArtError,LISTS
from countdown_assets import SHAPES

class FinishAssetsTests(unittest.TestCase):
    def test_setup_shape_and_termination(self):
        rows={LISTS[0]:dict(type='GFX',offset=0),'next':dict(offset=128)}
        words=[(op<<24,0) for op in SHAPES[0]]
        data=b''.join(struct.pack('>II',*w) for w in words)
        self.assertEqual(recipe(data,rows,LISTS[0]),words)
        for offset,value in ((0,0xde),(120,0),(16,0xdf)):
            bad=bytearray(data);bad[offset]=value
            with self.assertRaises(ArtError):recipe(bad,rows,LISTS[0])
        with self.assertRaises(ArtError):recipe(data[:-8],rows,LISTS[0])
        with self.assertRaises(ArtError):recipe(data,rows,'arbitrary')

    def test_unloaded_triangle_and_unknown_texture(self):
        end=(0xdf000000,0)
        def vertices(*args):raise AssertionError('not expected')
        for command in ((0x05000204,0),(0xfd100000,0x04000100),(0xde000000,0)):
            lists={LISTS[0]:[end],LISTS[1]:[command,end]}
            with self.assertRaises(ArtError):geometry(lists,vertices,lambda a:'unknown')

if __name__=='__main__':unittest.main()
