from pathlib import Path
import sys,unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from check_road_replay import reference,check

class RoadReplayTests(unittest.TestCase):
    def test_tick_numbering_and_rejection(self):
        line='0 1 2 1 990 10 20 30 255 255 0 0 255 2,4,-1,3f800000'
        second='1'+line[1:]
        expected=reference(line+'\n'+second)
        self.assertEqual(set(expected),{1,2})
        self.assertEqual(len(expected[1]),796)
        # Literal C ABI anchors: big-endian count and signed group direction.
        self.assertEqual(expected[1][4:8],b'\0\0\0\1')
        self.assertEqual(expected[1][28:40],bytes.fromhex('00020004ffffffff3f800000'))
        rows=[dict(tick=i,frame=i-1,status=1,sequence=i,plan_hex=expected[i].hex()) for i in (1,2)]
        self.assertEqual(check(rows,expected),2)
        with self.assertRaises(ValueError):reference(line+'\n'+line)
        with self.assertRaises(ValueError):reference('')
        with self.assertRaises(ValueError):check([],expected)
        with self.assertRaises(ValueError):check([rows[0],rows[0]],expected)
        rows[0]['frame']=1
        with self.assertRaises(ValueError):check(rows,expected)

if __name__=='__main__':unittest.main()
