"""Prepared vehicle conversion gates using generated geometry and pixels only."""
from pathlib import Path
from fractions import Fraction
import copy
import random
import struct
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from model_probe.make_fixture import fixture_data
from machine_rebake import build, pairing, face_plane, base_color, chart, ArtError
from road_pack import fnv


def verify(meta,bank,source,expected_fixture=1):
    """Independent decoder; compare source coverage and all bound table ranges."""
    if meta[:8]!=b'FZXMCH02':raise ValueError('magic')
    size,tb,th,mh,fixture,nlod,da,np,pa,la=struct.unpack_from('>10I',meta,8)
    zeroed=bytearray(meta);zeroed[20:24]=bytes(4)
    if (size,tb,th,mh)!=(len(meta),len(bank),fnv(bank),fnv(zeroed)):raise ValueError('hash')
    if fixture!=expected_fixture or nlod!=3 or da!=96 or pa+112*np!=la or la+256!=size:raise ValueError('header')
    for k,lod in enumerate((0,3,4)):
        model=source[lod]
        level,nv,npos,nn,nf,va,posa,na,fa,oa,nt,res=struct.unpack_from('>12I',meta,da+k*48)
        if (level,nv,nt,res)!=(lod+1,len(model['vertices']),len(model['triangles']),0):raise ValueError('descriptor')
        for at,count,stride,end in ((va,nv,20,posa),(posa,npos,8,na),(na,nn,4,fa),(fa,nf,56,oa),(oa,nf,16,pa)):
            if at%8 or at+count*stride>end or end>len(meta):raise ValueError('range')
        for i,v in enumerate(model['vertices']):
            row=struct.unpack_from('>5h3bB2H2x',meta,va+i*20)
            if row[:9]!=tuple(v['position']+v['uv']+v['normal']+[v.get('alpha',255)]):raise ValueError('vertex')
            if row[9]>=npos or row[10]>=nn:raise ValueError('map')
            if struct.unpack_from('>3hH',meta,posa+row[9]*8)[:3]!=tuple(v['position']):raise ValueError('position')
            if struct.unpack_from('>3bB',meta,na+row[10]*4)[:3]!=tuple(v['normal']):raise ValueError('normal')
        covered=[]
        for i in range(nf):
            row=struct.unpack_from('>8HI3hHi4H3i4x',meta,fa+i*56)
            ids=list(row[:4]);pal,w,h,n=row[4:8];at=row[8];tri=row[14:16];material=row[16]
            if n not in (3,4) or max(ids)>=nv or (n==3 and ids[2]!=ids[3]):raise ValueError('face')
            if pal>=np or w%8 or h%2 or not 8<=w<=32 or not 2<=h<=32 or at%8 or at+w*h//2>len(bank):raise ValueError('texture')
            if n==3:
                if tri[1]!=65535:raise ValueError('triangle sentinel')
                triangles=[ids[:3]];tri=tri[:1]
            else:triangles=[ids[:3],[ids[0],ids[2],ids[3]]]
            for actual,t in zip(triangles,tri):
                if t>=nt:raise ValueError('triangle id')
                expected=model['triangles'][t]
                key=lambda j:tuple(model['vertices'][j]['position']+model['vertices'][j]['uv']+model['vertices'][j]['normal'])
                a=[key(j) for j in actual];b=[key(j) for j in expected['vertices']]
                if material!=expected['material'] or not any(a==b[s:]+b[:s] for s in range(3)):raise ValueError('source coverage')
                covered.append(t)
            ncolors=meta[pa+pal*112+8]
            if not 1<=ncolors<=16:raise ValueError('palette size')
            mat=model['materials'][material];body=mat['environment']=='racer.bodyRGB'
            expected=bytes((mat['primitive'] or [0,0,0])[:3]+([0,0,0] if body else mat['environment'][:3])+[int(body),int(mat['combine'][0]=='TEXEL0')])
            if meta[pa+pal*112:pa+pal*112+8]!=expected:raise ValueError('palette binding')
            for byte in bank[at:at+w*h//2]:
                if byte>>4>=ncolors or byte&15>=ncolors:raise ValueError('palette index')
        if sorted(covered)!=list(range(nt)):raise ValueError('missing face')
        for octant in range(8):
            order=struct.unpack_from('>'+str(nf)+'H',meta,oa+octant*nf*2)
            if sorted(order)!=list(range(nf)):raise ValueError('order permutation')


class MachineRebakeTests(unittest.TestCase):
    def test_pack_and_corruption_controls(self):
        args=fixture_data();meta,bank,report=build(*args,fixture=True)
        self.assertEqual((meta,bank,report),build(*args,fixture=True))
        verify(meta,bank,args[0])
        self.assertEqual([x['faces'] for x in report['lods']],[8]*3)
        def changed(at,value,rehash=True):
            bad=bytearray(meta);bad[at:at+len(value)]=value
            if rehash:
                bad[20:24]=bytes(4);struct.pack_into('>I',bad,20,fnv(bad))
            return bad
        fa=struct.unpack_from('>I',meta,96+32)[0]
        oa=struct.unpack_from('>I',meta,96+36)[0]
        controls=[changed(0,b'BADMAGIC'),changed(fa+8,b'\xff\xff'),
                  changed(fa+32,b'\x00\x01'),changed(oa,b'\xff\xff'),
                  changed(fa+20,b'\xff',False)]
        for bad in controls:
            with self.assertRaises(ValueError):verify(bad,bank,args[0])
        # Rehashed, valid-sized palette record with a different material.
        pa=struct.unpack_from('>I',meta,40)[0]
        bad=changed(pa,b'\x00')
        with self.assertRaisesRegex(ValueError,'palette binding'):verify(bad,bank,args[0])
        badbank=bytearray(bank);badbank[0]^=1
        with self.assertRaises(ValueError):verify(meta,badbank,args[0])

    def test_supplied_orders(self):
        """Supplied back-to-front orders replace only their octants; bad orders are refused."""
        args=fixture_data();base,_,_=build(*args,fixture=True)
        n=len(pairing(args[0][0]));order=list(reversed(range(n)))
        meta,bank,report=build(*args,fixture=True,orders={'1:5':order})
        verify(meta,bank,args[0])
        oa=struct.unpack_from('>I',meta,96+36)[0]
        for octant in range(8):
            got=list(struct.unpack_from('>%dH'%n,meta,oa+octant*n*2));was=list(struct.unpack_from('>%dH'%n,base,oa+octant*n*2))
            self.assertEqual(got,order if octant==5 else was)
        self.assertIn('1 supplied offline topological orders',' '.join(report['adaptations']))
        for bad in (order[:-1],order[:-1]+[order[0]],[n]+order[1:]):
            with self.assertRaises(ArtError):build(*args,fixture=True,orders={'1:0':bad})

    def test_pairing_preserves_seams_and_rejects_nonplanar(self):
        model=copy.deepcopy(fixture_data()[0][0])
        self.assertEqual(len(pairing(model)),8)
        model['vertices'][3]['position'][2]=1
        self.assertEqual(len(pairing(model)),9)
        model=copy.deepcopy(fixture_data()[0][0])
        model['triangles'][1]['material']=1
        self.assertEqual(len(pairing(model)),9)
        model=copy.deepcopy(fixture_data()[0][0])
        # A duplicated shared vertex with a normal seam cannot be merged.
        model['vertices'].append(copy.deepcopy(model['vertices'][0]))
        model['vertices'][-1]['normal'][0]=1
        model['triangles'][1]['vertices'][0]=len(model['vertices'])-1
        self.assertEqual(len(pairing(model)),9)

    def test_nonaffine_uv_independent_barycentric_and_wrong_diagonal(self):
        positions=[(0,0,0),(48,0,0),(32,32,0),(0,48,0)]
        uv=[(0,0),(480,0),(0,480),(96,96)]
        model={'vertices':[dict(position=list(p),uv=list(t),normal=[0,0,127]) for p,t in zip(positions,uv)]}
        tex=dict(width=16,height=16)
        # Sixteen colors encode column; non-affine U and non-parallelogram geometry.
        pixels=b''.join(bytes((x*17,0,0,255)) for y in range(16) for x in range(16))
        sampling=dict(s_mode=2,t_mode=2,s_mask=4,t_mask=4,s_shift=0,t_shift=0)
        w,h,palette,packed=chart(model,dict(vertices=[0,1,2,3]),tex,sampling,pixels)
        def orient(a,b,c):return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])
        def color(x,y,triangles):
            s=Fraction(x,w-1);t=Fraction(y,h-1)
            weights=[(1-s)*(1-t),s*(1-t),s*t,(1-s)*t]
            point=[sum(weights[i]*positions[i][j] for i in range(4)) for j in range(2)]
            for ids in triangles:
                a,b,c=[positions[i] for i in ids];area=orient(a,b,c)
                bary=[orient(b,c,point)/area,orient(c,a,point)/area,orient(a,b,point)/area]
                if min(bary)<0:continue
                value=sum(bary[k]*(uv[i][0]*65535//65536) for k,i in enumerate(ids))
                index=max(0,min(15,int((value+16)//32)))
                return (index*17,0,0,255)
            self.fail('uncovered chart point')
        wrong=0
        for y in range(h):
            for x in range(w):
                i=y*w+x;index=(packed[i//2]>>(0 if i&1 else 4))&15
                expected=color(x,y,[(0,1,2),(0,2,3)])
                self.assertEqual(palette[index],expected)
                wrong+=expected!=color(x,y,[(0,1,3),(1,2,3)])
        self.assertGreater(wrong,0,'wrong-diagonal control must fail')

    def test_conservative_plane_margin(self):
        rng=random.Random(7461);naive_errors=0
        for _ in range(100):
            points=[[rng.randrange(-100,101) for _ in range(3)] for _ in range(3)]
            a,b,c=points;u=[b[i]-a[i] for i in range(3)];v=[c[i]-a[i] for i in range(3)]
            exact=[u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]]
            if not any(exact):continue
            normal,plane=face_plane([dict(position=p) for p in points])
            # Eyes on the exact face plane are the important rounding control.
            for k in range(-20,21):
                eye=[a[i]+k*u[i]+(k+1)*v[i] for i in range(3)]
                packed=sum(normal[i]*eye[i] for i in range(3))-plane
                naive_errors+=packed<0
                margin=sum(abs(eye[i]-a[i]) for i in range(3))
                self.assertGreaterEqual(packed,-margin)
                # Add +/-1-unit eye error independently of normal rounding.
                rounded=[eye[i]+rng.randrange(-1,2) for i in range(3)]
                value=sum(normal[i]*rounded[i] for i in range(3))-plane
                bound=sum(abs(eye[i]-a[i]) for i in range(3))+sum(abs(n) for n in normal)
                self.assertGreaterEqual(value,-bound)
        self.assertGreater(naive_errors,0,'unmargined culling control must fail')

    def test_palette_body_endpoints_and_rejections(self):
        key=((255,0,0),(0,0,0),1,0)
        self.assertEqual(base_color(key,(255,255,255,255),(0,255,0)),0x801f)
        self.assertEqual(base_color(key,(0,0,0,255),(0,255,0)),0x83e0)
        for mutation in ('combine','normal','position'):
            args=list(copy.deepcopy(fixture_data()))
            if mutation=='combine':args[0][0]['materials'][0]['combine']=['UNKNOWN']
            else:args[0][0]['vertices'][0][mutation][0]=99999
            with self.assertRaises(ArtError):build(*args,fixture=True)


if __name__=='__main__':unittest.main()
