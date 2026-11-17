"""Synthetic format/guard controls; no cartridge-derived fixtures."""
from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from course_menu_preview import ArtError,pack,validate,topology,parse_capture,fixed,fnv


class Preview(unittest.TestCase):
    def fixture(self):
        vertices=[[0,0,0],[10,0,0],[0,10,0]]*2
        courses=[dict(vertices=vertices,quads=topology(6)) for _ in range(24)]
        matrix=[float(i%5==0) for i in range(16)]+[1.0,1.0]
        return pack(courses,[matrix]*360)[0]

    def test_roundtrip_and_native_endian(self):
        data=self.fixture()
        self.assertTrue(validate(data))
        self.assertEqual(data[8:12],b'\0\0\0\1')
        self.assertEqual(topology(6),[(1,0,3,4),(0,2,5,3),(2,1,4,5)])

    def test_corruption_controls(self):
        original=self.fixture()
        for offset in (0,12,32,64,len(original)-1):
            data=bytearray(original);data[offset]^=1
            with self.assertRaises(ArtError): validate(data)
        with self.assertRaises(ArtError): validate(original[:-1])

    def test_valid_checksum_cannot_hide_invalid_faces(self):
        data=bytearray(self.fixture());qo=struct.unpack_from('>I',data,40)[0]
        data[qo:qo+2]=b'\xff\xff'
        struct.pack_into('>I',data,48,fnv(data[64:]))
        with self.assertRaises(ArtError): validate(data)

    def test_nonclosed_and_overflow_controls(self):
        with self.assertRaises(ArtError): topology(0xC00)
        with self.assertRaises(ArtError): topology(7)
        with self.assertRaises(ArtError): fixed(32768)
        with self.assertRaises(ArtError): parse_capture('FZXCPVRAW1\nC 1 6')


if __name__=='__main__': unittest.main()
