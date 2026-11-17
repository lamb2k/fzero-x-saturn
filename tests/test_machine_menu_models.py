"""Synthetic machine-menu conversion/format controls; no retail fixtures."""
from pathlib import Path
import copy
import struct
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from machine_menu_models import (ArtError,chart_colors,chart_pack,validate_charts,
                                 model_pack,validate_models,effect_pack,validate_effects,shadow_chart,fnv)


def vertex(x,y,z,u=0,v=0):
    return dict(position=[x,y,z],flag=0,uv=[u,v],normal=[0,127,0],alpha=255)


def fixture():
    v=[vertex(0,0,0),vertex(10,0,0,32),vertex(0,0,10,0,32)]
    g=dict(vertices=v,triangles=[dict(vertices=[0,1,2],material=0)],
           materials=[dict(tile=0,combine=1,environment='body',primitive=[255,255,255,255])])
    sampler=dict(s_mode=0,t_mode=0,s_mask=1,t_mask=1,s_shift=0,t_shift=0)
    machines=[dict(character=i,geometry=copy.deepcopy(g),reflection=dict(vertices=v,triangles=[[0,1,2]]),
                   shadow=dict(vertices=v+[vertex(10,0,10)],texture='aSynthetic'),
                   skins=[[32,64,128],[128,64,32],[0,255,0],[255,0,0]],
                   bindings=[dict(tile=0,symbol='aSynthetic',sampler=sampler)])for i in range(30)]
    textures={'aSynthetic':dict(family='machine_models',offset=0,format='I4',width=2,height=2,texels=b'\x01\x23')}
    identity=[float(i%5==0)for i in range(16)]
    return machines,textures,[identity]*629


class MachineModels(unittest.TestCase):
    def test_geometry_order_skin_and_signed_fields_roundtrip(self):
        machines,textures,matrices=fixture();machines[0]['geometry']['vertices'][0]=vertex(-500,-3,127,-64,128)
        data,report=model_pack(machines,textures,matrices)
        self.assertTrue(validate_models(data));self.assertEqual(report['triangles'],30)
        descriptor=struct.unpack_from('>24I',data,64)
        self.assertEqual(struct.unpack_from('>3hH2h3bB',data,descriptor[1]),(-500,-3,127,0,-64,128,0,127,0,255))
        self.assertEqual(struct.unpack_from('>4H',data,descriptor[3]),(0,1,2,0))
        self.assertEqual(data[descriptor[15]:descriptor[15]+12],bytes(c for skin in machines[0]['skins']for c in skin))

    def test_checksums_and_truncation_fail(self):
        data,_=model_pack(*fixture())
        for at in (0,12,64,len(data)-1):
            bad=bytearray(data);bad[at]^=1
            with self.assertRaises(ArtError):validate_models(bad)
        with self.assertRaises(ArtError):validate_models(data[:-1])

    def test_rehashed_bad_geometry_and_sampler_fail(self):
        data,_=model_pack(*fixture());descriptor=struct.unpack_from('>24I',data,64)
        for at,value in ((descriptor[3],65535),(descriptor[7]+4,4)):
            bad=bytearray(data);struct.pack_into('>H',bad,at,value)
            struct.pack_into('>I',bad,52,fnv(bad[64:]))
            with self.assertRaises(ArtError):validate_models(bad)

    def test_chart_palette_is_exact_and_unique_per_triangle(self):
        colors=[0x8000+i for i in range(16)]
        data,report=chart_pack([colors,colors],8,2,30,0)
        self.assertTrue(validate_charts(data));self.assertEqual(report['clut4'],2)
        first=struct.unpack_from('>4I',data,64);second=struct.unpack_from('>4I',data,80)
        self.assertEqual(first[0],second[0]);self.assertNotEqual(first[1],second[1])
        bo=struct.unpack_from('>I',data,24)[0]
        palette=struct.unpack_from('>16H',data,bo+first[1]);decoded=[]
        for b in data[bo+first[0]:bo+first[0]+8]:decoded.extend((palette[b>>4],palette[b&15]))
        self.assertEqual(decoded,colors)

    def test_rgb_fallback_retains_all_colors(self):
        colors=[0x8000+i for i in range(256)]
        data,report=chart_pack([colors,colors],16,16,4,3)
        self.assertEqual(report['rgb16'],2);po,pa,mode,_=struct.unpack_from('>4I',data,64)
        self.assertNotEqual(po,struct.unpack_from('>I',data,80)[0])
        self.assertEqual((pa,mode),(0xFFFFFFFF,16))
        bo=struct.unpack_from('>I',data,24)[0]
        self.assertEqual(list(struct.unpack_from('>256H',data,bo+po)),colors)

    def test_rehashed_chart_bounds_and_palette_controls_fail(self):
        data,_=chart_pack([[0x8123]*16],8,2,30,0)
        bad=bytearray(data);struct.pack_into('>I',bad,64,0xFFFFFFF8)
        struct.pack_into('>I',bad,32,fnv(bad[64:]))
        with self.assertRaises(ArtError):validate_charts(bad)
        bad=bytearray(data);bo=struct.unpack_from('>I',bad,24)[0];pa=struct.unpack_from('>I',bad,68)[0]
        bad[bo+pa]=0;struct.pack_into('>I',bad,32,fnv(bad[64:]))
        with self.assertRaises(ArtError):validate_charts(bad)

    def test_material_equations_and_uv_scale(self):
        machines,_,_=fixture();g=machines[0]['geometry'];t=g['triangles'][0]
        tex=dict(width=2,height=2,rgba=bytes([255,0,0,255,0,255,0,255,0,0,255,255,255,255,255,255]))
        sampling=machines[0]['bindings'][0]['sampler'];skin=[0,0,255]
        # Equation0 alpha1 retains texelRGB, including sourceUV point sample.
        g['materials'][0]['combine']=0
        colors=chart_colors(g,t,tex,sampling,skin,8,2)
        self.assertEqual(colors[0],0x801F);self.assertEqual(colors[7],0x83E0)
        self.assertEqual(colors[-1],0xFC00)
        # Equation2 is environment only, independent of all source texels.
        g['materials'][0]['combine']=2
        self.assertEqual(set(chart_colors(g,t,tex,sampling,skin,8,2)),{0xFC00})

    def test_original_effect_texels_and_rgb_conversion(self):
        machines,_,_=fixture()
        shadow=[vertex(0,0,0,0,0),vertex(10,0,10,31*32,63*32),vertex(0,0,10,0,63*32),vertex(10,0,0,31*32,0)]
        for machine in machines:machine['shadow']['vertices']=shadow
        tex={'aSynthetic':dict(format='I4',width=32,height=64,texels=bytes(range(256))*4),
             'D_9000008':dict(format='RGBA16',width=16,height=16,texels=struct.pack('>H',0xF801)*256)}
        data,report=effect_pack(machines,tex,['aSynthetic','D_9000008'])
        self.assertTrue(validate_effects(data));self.assertEqual(report['selected_vram_bytes'],1568)
        bo=544;po=struct.unpack_from('>I',data,64)[0]
        self.assertEqual(data[bo+po:bo+po+1024],tex['aSynthetic']['texels'])
        self.assertEqual(struct.unpack_from('>H',data,bo+report['reflection_pixel_offset'])[0],0x801F)
        bad=bytearray(data);bad[bo+2]^=1;struct.pack_into('>I',bad,32,fnv(bad[64:]))
        with self.assertRaises(ArtError):validate_effects(bad)

    def test_shadow_reversed_cropped_and_clamped_uv(self):
        values=[((x//2)+(y//4))%16 for y in range(64)for x in range(32)]
        raw=bytes((values[i]<<4)|values[i+1]for i in range(0,len(values),2))
        texture=dict(format='I4',width=32,height=64,texels=raw)
        v=[vertex(0,0,0,20*32,6*32),vertex(10,0,10,4*32,50*32),
           vertex(0,0,10,20*32,50*32),vertex(10,0,0,4*32,6*32)]
        chart=shadow_chart(v,texture)
        corners=[chart[0]>>4,chart[15]&15,chart[-1]&15,chart[-16]>>4]
        self.assertEqual(corners,[11,3,14,6])
        self.assertNotEqual(chart,raw) # fullsource-image orientation control
        for i,uv in enumerate(((-3200,-3200),(3200,3200),(-3200,3200),(3200,-3200))):v[i]['uv']=list(uv)
        chart=shadow_chart(v,texture)
        self.assertEqual([chart[0]>>4,chart[15]&15,chart[-1]&15,chart[-16]>>4],[0,15,14,15])


if __name__=='__main__':unittest.main()
