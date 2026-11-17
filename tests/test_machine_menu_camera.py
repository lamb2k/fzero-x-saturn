"""Synthetic camera packet/quantization guards; no cartridge-derived fixtures."""
from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from machine_menu_camera import ArtError,parse_capture,matrix_gate,fixed


class MachineCamera(unittest.TestCase):
    def capture(self,change=None):
        lines=['FZXMCRAW1']
        for run,scene in enumerate((0,1,1,0)):
            for frame in range(8):
                values=[0.0]*58
                values[15:18]=[60.0,16.0,8192.0]
                values[26:42]=[float(i%5==0)for i in range(16)]
                values[40]=-500.0-scene*100.0
                values[42]=values[47]=2.0
                state=[scene,0,frame,0,*values,0]
                if change:change(run,scene,frame,state)
                raw=struct.pack('<4i58fI',*state)
                lines.append('R %d %d %d '%(run,scene,frame)+' '.join('%08x'%v for v in struct.unpack('<63I',raw)))
        return '\n'.join(lines)

    def test_fixed_across_cold_warm_transitions(self):
        matrices,captures=parse_capture(self.capture())
        self.assertEqual(len(captures),32)
        self.assertEqual(matrices[0][14],-500.0)
        self.assertEqual(matrices[1][14],-600.0)
        data=b''.join(struct.pack('>18i',*(fixed(v)for v in m))for m in matrices)
        self.assertEqual(len(data),144)
        self.assertEqual(matrix_gate(matrices,data),0)

    def test_order_and_extent_controls(self):
        text=self.capture()
        for bad in (text.replace('R 0 0 0','R 0 1 0'),text+'\nR',text.split('\n',1)[1]):
            with self.assertRaises(ArtError):parse_capture(bad)

    def test_source_fault_nonfinite_and_wrong_lens_controls(self):
        for offset,value in ((-1,1),(4,float('nan')),(19,59.0)):
            def change(run,scene,frame,state):
                if run==0 and frame==0:state[offset]=value
            with self.assertRaises(ArtError):parse_capture(self.capture(change))

    def test_moving_or_duplicate_camera_controls(self):
        def moving(run,scene,frame,state):
            if run==3 and frame==7:state[44]+=1
        def duplicate(run,scene,frame,state):state[44]=-500.0
        for change in (moving,duplicate):
            with self.assertRaises(ArtError):parse_capture(self.capture(change))

    def test_projection_and_endian_controls(self):
        matrices,_=parse_capture(self.capture())
        data=bytearray(b''.join(struct.pack('>18i',*(fixed(v)for v in m))for m in matrices))
        struct.pack_into('>i',data,0,fixed(1.25))
        with self.assertRaises(ArtError):matrix_gate(matrices,data)
        with self.assertRaises(ArtError):matrix_gate(matrices,data[:-1])
        with self.assertRaises(ArtError):matrix_gate(matrices,b''.join(struct.pack('<18i',*(fixed(v)for v in m))for m in matrices))


if __name__=='__main__':unittest.main()
