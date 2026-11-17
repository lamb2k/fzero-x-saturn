#!/usr/bin/env python3
"""Offline conversion of the thirty stock US machine-menu models.

Only the thirty pinned LOD1/load functions and thirty named reflection lists
are accepted. No display-list execution is included in the native product.
Source geometry, UVs, signed normals, materials and all four body skins are
retained; Saturn texture charts are an explicitly sampled derivative.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import struct
import subprocess
import sys
from import_source_art import (ROOT,DECOMP,ArtError,SourceArt,bounded,commands,
                               stock_function,normalize_rom,extract_rom_course,
                               verify_source,output_location,machine_setup)
from model_assets import sampler
from road_pack import fnv
from machine_rebake import address, base_color
from countdown_pack import chart_geometry,uv_at

RACER=DECOMP/'src/game/racer.c'
DRAW=DECOMP/'src/overlays/ovl_i9/machine_draw.c'
MENU=DECOMP/'src/overlays/ovl_i4/machine.c'
COMMON=DECOMP/'src/game/common.c'
GAME=DECOMP/'include/fzx_game.h'
COMBINERS=[
    'TEXEL0 ENVIRONMENT TEXEL0_ALPHA ENVIRONMENT 0 0 0 ENVIRONMENT COMBINED 0 SHADE 0 0 0 0 COMBINED'.split(),
    'PRIMITIVE ENVIRONMENT TEXEL0 ENVIRONMENT 0 0 0 ENVIRONMENT COMBINED 0 SHADE 0 0 0 0 COMBINED'.split(),
    '0 0 0 ENVIRONMENT 0 0 0 ENVIRONMENT COMBINED 0 SHADE 0 0 0 0 COMBINED'.split(),
]


def array_symbols(source,name):
    match=re.search(r'\b'+re.escape(name)+r'\[\] = \{(.*?)\};',source,re.S)
    if not match: raise ArtError('Missing machine source table '+name)
    return re.findall(r'\bD_[0-9A-Fa-f]+\b',match[1])


def source_inventory():
    source=RACER.read_text()
    begin=source.index('sMachineLoadTexturesFuncs[]')
    end=source.index('sMachineDrawFuncs[][5]')
    loads=re.findall(r'(Machine_DrawLoad\w+Textures),',source[begin:end])[:30]
    models=re.findall(r'\{ (Machine_Draw\w+Lod1),',source[end:])[:30]
    colors=re.findall(r'\{ CUSTOM_MACHINE_DEFAULT, (\d+), \d+, \{([^}]+)\}, \{([^}]+)\}, \{([^}]+)\}[^\n]*// (\w+)',source)[:30]
    if len(models)!=30 or len(loads)!=30 or len(colors)!=30 or len(set(models))!=30:
        raise ArtError('Stock machine inventory changed')
    result=[]
    for character,(model,load,color) in enumerate(zip(models,loads,colors)):
        channels=[[int(v.strip()) for v in row.split(',')] for row in color[1:4]]
        if any(len(c)!=4 or any(v<0 or v>255 for v in c) for c in channels):
            raise ArtError('Source body skin extent changed')
        result.append(dict(character=character,model=model,loader=load,shadow_type=int(color[0]),
                           character_name=color[4],skins=[list(c) for c in zip(*channels)]))
    return result


def source_slots():
    block=re.search(r'typedef enum Character \{(.*?)\} Character;',GAME.read_text(),re.S)
    if not block:raise ArtError('Missing original character enum')
    names=re.findall(r'/\* \d+ \*/\s*(\w+),',block[1])
    table=re.search(r'const s8 kMachineSelectCharacterList\[\] = \{(.*?)\};',COMMON.read_text(),re.S)
    if not table or len(names)!=30:raise ArtError('Original character enum/table extent')
    entries=[v.strip()for v in table[1].split(',')]
    if len(entries)!=31 or entries[-1]!='30' or set(entries[:-1])!=set(names):raise ArtError('Original machine slot permutation changed')
    inventory=source_inventory()
    if [r['character_name']for r in inventory]!=names:raise ArtError('Machine descriptor order differs from source enum')
    return [names.index(n)for n in entries[:-1]]


def source_vertices(art,symbol,first,count):
    row=art.rows.get(symbol,{})
    if row.get('type')!='VTX' or not 0<count<=64 or first<0 or first+count>row['count']:
        raise ArtError('Machine vertex span exceeds source array')
    vertices=[]
    for index in range(first,first+count):
        raw=bounded(art.segments[row['family']],row['offset']+index*16,16)
        x,y,z,flag,u,v,nx,ny,nz,alpha=struct.unpack('>3hH2h3bB',raw)
        vertices.append(dict(symbol=symbol,index=index,position=[x,y,z],flag=flag,uv=[u,v],normal=[nx,ny,nz],alpha=alpha))
    return vertices


def model(art,name):
    if name not in {r['model'] for r in source_inventory()}: raise ArtError('Model is outside stock menu scope')
    vertices=[];triangles=[];materials=[];cache={}
    state=dict(tile=None,primitive=[0,0,0,255],environment='body',combine=None,textured=True)
    for command,args in commands(stock_function(art.draw.decode(),name)):
        if not args or args[0]!='gfx++': raise ArtError('Unexpected source graphics destination')
        if command=='gDPPipeSync': continue
        if command=='gDPSetCombineLERP':
            if args[1:] not in COMBINERS: raise ArtError('Unknown stock menu material equation')
            state['combine']=COMBINERS.index(args[1:])
        elif command in ('gDPSetPrimColor','gDPSetEnvColor'):
            color=list(map(int,args[-4:]))
            if any(v<0 or v>255 for v in color): raise ArtError('Source material color range')
            state['primitive' if command=='gDPSetPrimColor' else 'environment']=color
        elif command=='gSPTexture':
            if args[1:4]!=['0xFFFF','0xFFFF','0'] or args[-1] not in ('G_ON','G_OFF') or int(args[4]) not in range(8):
                raise ArtError('Source menu texture scaling changed')
            state['tile']=int(args[4])
            state['textured']=args[-1]=='G_ON'
        elif command=='gSPVertex':
            match=re.fullmatch(r'&?(D_[0-9A-Fa-f]+)(?:\[(\d+)\])?',args[1])
            if not match or len(args)!=4 or int(args[3])!=0: raise ArtError('Source vertex load expression')
            loaded=source_vertices(art,match[1],int(match[2] or 0),int(args[2]))
            cache={i:len(vertices)+i for i in range(len(loaded))};vertices+=loaded
        elif command in ('gSP1Triangle','gSP2Triangles'):
            ids=list(map(int,args[1:]))
            if len(ids)!=(4 if command=='gSP1Triangle' else 8) or state['combine'] is None or state['tile'] is None:
                raise ArtError('Source triangle lacks bounded material state')
            if not state['textured'] and state['combine']!=2: raise ArtError('Textured material while source texture is off')
            if state not in materials: materials.append(dict(state))
            for i in range(0,len(ids),4):
                if ids[i+3] or any(v not in cache for v in ids[i:i+3]): raise ArtError('Source triangle cache reference')
                triangles.append(dict(vertices=[cache[v] for v in ids[i:i+3]],material=materials.index(state)))
        else: raise ArtError('Unexpected stock model command '+command)
    if not 0<len(vertices)<=512 or not 0<len(triangles)<=256: raise ArtError('Stock model exceeds bounded geometry')
    return dict(source_function=name,vertices=vertices,triangles=triangles,materials=materials)


def asset_span(art,name):
    row=art.rows[name];segment=art.segments[row['family']]
    end=min((r['offset'] for r in art.rows.values()if r['family']==row['family'] and r['offset']>row['offset']),default=len(segment))
    return bounded(segment,row['offset'],end-row['offset'])


def menu_setup(art):
    raw=asset_span(art,'D_303A5F8');state=machine_setup(raw)
    if state['geometry']!=0xA20405: raise ArtError('Machine menu geometry state changed')
    address=state['texture_image']['address']
    names=[n for n,r in art.rows.items()if r['family']=='machine_custom_gfx' and r['offset']==address&0xFFFFFF and r['type']=='TEXTURE']
    if len(names)!=1 or address>>24!=3: raise ArtError('Menu base texture mapping changed')
    state.pop('race_patch')
    state['symbol']='D_303A5F8';state['texture_symbol']=names[0]
    return state


def bindings(art,name,setup):
    if name not in {r['loader'] for r in source_inventory()}: raise ArtError('Texture function outside stock menu scope')
    rows=[dict(tile=0,symbol=setup['texture_symbol'],sampling=setup['tile_zero'])]
    for cmd,args in commands(stock_function(art.draw.decode(),name)):
        if cmd=='gDPPipeSync': continue
        if cmd not in ('gDPLoadMultiBlock','gDPLoadMultiBlock_4b') or args[0]!='gfx++':
            raise ArtError('Unknown stock machine texture load')
        rows.append(dict(tile=int(args[3]),symbol=args[1],source_command=cmd,arguments=args[2:]))
    if sorted(r['tile'] for r in rows)!=list(range(len(rows))) or len(rows)>8:
        raise ArtError('Stock menu tiles are not contiguous')
    for row in rows: row['sampler']=sampler(row,art.texture(row['symbol']))
    return rows


def reflection(art,name):
    """Decode one named static source reflection mesh; reject every other opcode."""
    allowed=array_symbols(RACER.read_text(),'D_800CDAD8')
    if name not in allowed or len(allowed)!=30: raise ArtError('Reflection mesh outside source menu table')
    vertices=[];triangles=[];cache={};ended=False
    raw=asset_span(art,name)
    for a,b in struct.iter_unpack('>II',raw):
        opcode=a>>24
        if ended:
            if a or b: raise ArtError('Nonzero data follows reflection end')
            continue
        if opcode==0xDF:
            if a!=0xDF000000 or b: raise ArtError('Reflection end command changed')
            ended=True;continue
        if opcode==1:
            count=(a>>12)&255;base=((a>>1)&127)-count
            if a&0xFF000FFF!=0x01000000|((base+count)<<1) or base<0 or base+count>64 or b>>24!=9:
                raise ArtError('Reflection vertex command outside bounded layout')
            offset=b&0xFFFFFF
            matches=[(n,r) for n,r in art.rows.items()if r['family']=='machine_models' and r['type']=='VTX' and r['offset']<=offset and offset+count*16<=r['offset']+r['count']*16]
            if len(matches)!=1 or (offset-matches[0][1]['offset'])%16: raise ArtError('Reflection vertex address unmapped')
            symbol,row=matches[0];loaded=source_vertices(art,symbol,(offset-row['offset'])//16,count)
            for i in range(count):cache[base+i]=len(vertices)+i
            vertices+=loaded
        elif opcode in (5,6):
            words=(a,) if opcode==5 else (a,b)
            if opcode==5 and b: raise ArtError('Reflection TRI1 reserved word')
            for word in words:
                packed=[(word>>shift)&255 for shift in (16,8,0)]
                if any(v&1 or v//2 not in cache for v in packed): raise ArtError('Reflection triangle vertex cache')
                triangles.append([cache[v//2] for v in packed])
        else: raise ArtError('Unexpected reflection mesh opcode')
    if not ended or not vertices or not triangles: raise ArtError('Incomplete reflection mesh')
    return dict(symbol=name,vertices=vertices,triangles=triangles,source_sha256=hashlib.sha256(raw).hexdigest())


def census(rom):
    _,provenance=extract_rom_course(rom);normalized,_=normalize_rom(rom)
    art=SourceArt(normalized);setup=menu_setup(art);inventory=source_inventory()
    source=RACER.read_text();refl=array_symbols(source,'D_800CDAD8')
    sv=array_symbols(source,'D_800CDBA4');st=array_symbols(source,'D_800CDC54')
    result=[];textures={}
    for row in inventory:
        geometry=model(art,row['model']);tiles=bindings(art,row['loader'],setup)
        if any(m['tile']>=len(tiles) for m in geometry['materials']): raise ArtError('Unbound source material tile')
        for tile in tiles:
            texture=art.texture(tile['symbol']);textures[tile['symbol']]=texture
        shadow_tex=art.texture(st[row['shadow_type']]);textures[shadow_tex['symbol']]=shadow_tex
        if (shadow_tex['format'],shadow_tex['width'],shadow_tex['height'])!=('I4',32,64):
            raise ArtError('Source shadow image format changed')
        shadow=dict(vertices=source_vertices(art,sv[row['shadow_type']],0,4),triangles=[[0,1,2],[0,3,1]],texture=shadow_tex['symbol'])
        result.append(dict(**row,geometry=geometry,bindings=tiles,reflection=reflection(art,refl[row['character']]),shadow=shadow))
    texture=art.texture('D_9000008');textures[texture['symbol']]=texture
    if (texture['format'],texture['width'],texture['height'])!=('RGBA16',16,16): raise ArtError('Source reflection texture changed')
    menu=MENU.read_text()
    if not re.search(r's16 D_i4_8011D4DC = 60;',menu)or not re.search(r's32 D_i4_8011D4E0 = 2600;',menu):raise ArtError('Source reflection alpha/scale changed')
    effect_states={}
    for name,expected in (('D_303A758',0x404),('D_303A7D8',0x405),('D_90186C8',0xA60405)):
        raw=asset_span(art,name);geometry=0;changes=0
        for a,b in struct.iter_unpack('>II',raw):
            if a>>24==0xD9:geometry=(geometry&(a&0xFFFFFF))|b;changes+=1
        if changes!=2 or geometry!=expected:raise ArtError('Source effect geometry flags changed')
        effect_states[name]=dict(geometry=geometry,source_sha256=hashlib.sha256(raw).hexdigest())
    return result,textures,dict(setup=setup,rom_sha256=provenance['normalized_rom_sha256'],reflection_alpha=60,reflection_scale=2600,effect_states=effect_states)


def chart_colors(geometry,triangle,texture,sampling,skin,width,height):
    """Source-order triangular chart: A/B top corners, C collapsed bottom edge."""
    m=geometry['materials'][triangle['material']]
    environment=skin if m['environment']=='body' else m['environment'][:3]
    key=(tuple(m['primitive'][:3]),tuple(environment),0,int(m['combine']==0))
    if width not in (8,16) or height not in (2,16): raise ArtError('Chart size outside menu scope')
    vertices=[geometry['vertices'][i] for i in triangle['vertices']]
    tw,th=texture['width'],texture['height'];pixels=texture['rgba'];colors=[]
    if len(pixels)!=tw*th*4: raise ArtError('Menu texture extent mismatch')
    def shifted(value,shift):
        if not 0<=shift<=15: raise ArtError('Source texture shift range')
        return value/(1<<shift) if shift<=10 else value*(1<<(16-shift))
    for y in range(height):
        for x in range(width):
            if m['combine']==2:
                colors.append(0x8000|sum((environment[i]>>3)<<(i*5) for i in range(3)));continue
            weights=((width-1-x)*(height-1-y),x*(height-1-y),y*(width-1))
            den=(width-1)*(height-1)
            # Preserve the source 0xffff texture multiplier, nearest point sample.
            uv=[sum(weights[k]*vertices[k]['uv'][axis]*65535 for k in range(3))/(den*65536)
                for axis in (0,1)]
            ij=[address(int((shifted(v,sampling[a+'_shift'])+16)//32),size,
                        sampling[a+'_mode'],sampling[a+'_mask']) for v,size,a in zip(uv,(tw,th),('s','t'))]
            if not 0<=ij[0]<tw or not 0<=ij[1]<th: raise ArtError('Source texture sampling outside dimensions')
            at=(ij[1]*tw+ij[0])*4
            colors.append(base_color(key,pixels[at:at+4],skin))
    return colors


def chart_pack(charts,width,height,character,skin):
    """Share immutable pixels; each triangle owns its mutable lighting palette."""
    blob=bytearray();cache={};records=[];modes={4:0,16:0}
    def insert(data):
        data=bytes(data)
        if data not in cache:
            blob.extend(b'\0'*(-len(blob)%8));cache[data]=len(blob);blob.extend(data)
        return cache[data]
    for colors in charts:
        if len(colors)!=width*height or any(not 0x8000<=c<=65535 for c in colors): raise ArtError('Chart color range')
        palette=sorted(set(colors))
        if len(palette)<=16:
            indices={c:i for i,c in enumerate(palette)}
            pixels=bytes((indices[colors[i]]<<4)|indices[colors[i+1]] for i in range(0,len(colors),2))
            blob.extend(b'\0'*(-len(blob)%8));palette_offset=len(blob)
            blob.extend(struct.pack('>16H',*(palette+[0]*(16-len(palette)))))
            pixel_offset=insert(pixels);mode=4
        else:
            # Native lighting updates RGB pixels directly, so RGB spans, like
            # CLUT palettes, must remain unique per source triangle.
            blob.extend(b'\0'*(-len(blob)%8));pixel_offset=len(blob)
            blob.extend(struct.pack('>'+str(len(colors))+'H',*colors));palette_offset=0xFFFFFFFF;mode=16
        records.append((pixel_offset,palette_offset,mode,0));modes[mode]+=1
    descriptors=b''.join(struct.pack('>4I',*r) for r in records)
    blob_offset=64+len(descriptors);payload=descriptors+blob
    header=struct.pack('>8s14I',b'FZXMC01\0',1,64+len(payload),len(records),64,blob_offset,len(blob),
                       fnv(payload),width,height,character,skin,0,0,0)
    data=header+payload;validate_charts(data)
    return data,dict(bytes=len(data),vram_bytes=len(blob),charts=len(records),clut4=modes[4],rgb16=modes[16],fnv=fnv(data))


def validate_charts(data):
    if len(data)<64: raise ArtError('Truncated model charts')
    magic,*h=struct.unpack_from('>8s14I',data)
    version,size,count,do,bo,bs,checksum,w,he,char,skin,*reserved=h
    if magic!=b'FZXMC01\0' or version!=1 or size!=len(data) or not 0<count<=4000 or do!=64 or bo!=64+count*16 or bo+bs!=size or reserved!=[0]*3:
        raise ArtError('Model chart header')
    if (w,he)not in ((8,2),(16,16)) or char>30 or skin>3 or (char==30 and (w,he,skin)!=(8,2,0)):
        raise ArtError('Model chart identity')
    if fnv(data[64:])!=checksum: raise ArtError('Model chart checksum')
    for i in range(count):
        po,pa,mode,reserved=struct.unpack_from('>4I',data,do+i*16)
        size=w*he//2 if mode==4 else w*he*2
        if mode not in (4,16) or reserved or po%8 or po+size>bs or (mode==4 and(pa%8 or pa+32>bs)) or (mode==16 and pa!=0xFFFFFFFF):
            raise ArtError('Model chart descriptor')
        if mode==4:
            palette=struct.unpack_from('>16H',data,bo+pa)
            for byte in data[bo+po:bo+po+size]:
                if not palette[byte>>4]&0x8000 or not palette[byte&15]&0x8000: raise ArtError('Model chart missing palette entry')
    return True


def motion_capture(output):
    """Compile original source basis/matrix functions offline, twice independently."""
    from menu_source import function
    from course_menu_preview import linux_path
    source=(DECOMP/'src/sys/math.c').read_text()
    def math_function(name):
        match=re.search(r'^(?:s32|void) '+re.escape(name)+r'\([^;{]*\) \{.*?^\}',source,re.M|re.S)
        if not match:raise ArtError('Original math function missing: '+name)
        return 'static '+match.group(0)+'\n'
    functions='\n'.join(math_function(n) for n in ('Math_OrthonormalizeAroundForward','Matrix_SetLockedLookAt'))
    original=function(MENU.read_text(),'MachineSelect_MachineUpdate')
    update=original[original.index('var_s0->trueBasis.x.x +='):original.index('        Matrix_SetLockedLookAt')]
    original_settings=function(MENU.read_text(),'MachineSettings_MachineUpdate')
    if 'var_fv0 = 0.015f;' not in original_settings or 'var_fv0 = 0.03f;' not in original:
        raise ArtError('Source menu rotation step changed')
    prelude=r'''#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
typedef int32_t s32; typedef float f32;
typedef struct {float x,y,z;} Vec3f;
typedef struct {Vec3f x,y,z;} Mtx3F;
typedef struct {float m[4][4];} MtxF;
typedef struct {int dummy;} Mtx;
typedef struct {Mtx3F trueBasis;} Racer;
#define SQ(x) ((x)*(x))
static Mtx sDefaultMtx;static MtxF sDefaultMtxF;
static void Matrix_ToMtx(MtxF *f,Mtx *m){(void)f;(void)m;}
static void Matrix_SetTransRot(Mtx *m,MtxF *f,float s,int x,int y,int z,float px,float py,float pz){
 (void)m;(void)f;(void)s;(void)x;(void)y;(void)z;(void)px;(void)py;(void)pz;abort();}
'''
    main=r'''
static void print_matrix(unsigned group,unsigned frame,Racer *r){
 MtxF f;Matrix_SetLockedLookAt(NULL,&f,1,1,1,r->trueBasis.x.x,r->trueBasis.x.y,r->trueBasis.x.z,
 r->trueBasis.y.x,r->trueBasis.y.y,r->trueBasis.y.z,0,0,0);
 printf("%u %u",group,frame);for(unsigned a=0;a<4;a++)for(unsigned b=0;b<4;b++)printf(" %a",(double)f.m[a][b]);puts("");
}
int main(void){
 for(unsigned group=0;group<3;group++){
  Racer racer={{{0,0,1},{0,1,0},{1,0,0}}};Racer *var_s0=&racer;
  unsigned count=group==0?0:group==1?209:419;
  float var_fv0=group==1?0.03f:0.015f;
  for(unsigned frame=0;frame<=count;frame++){
   print_matrix(group,frame,var_s0);
   UPDATE
  }
 }
 return 0;
}
'''.replace('UPDATE',update)
    path=output/'motion-source.c';path.write_text(prelude+functions+main)
    captures=[]
    for cc in ('gcc','clang'):
        exe=output/('motion-'+cc)
        command=[cc,'-std=c11','-O2','-ffp-contract=off','-fno-fast-math','-Wall','-Wextra','-Werror',linux_path(path),'-lm','-o',linux_path(exe)]
        if os.name=='nt': command=['wsl','--',*command]
        subprocess.run(command,check=True,capture_output=True)
        command=[linux_path(exe)]
        if os.name=='nt':command=['wsl','--',*command]
        captures.append(subprocess.check_output(command))
    if captures[0]!=captures[1]: raise ArtError('GCC/Clang source model matrices differ')
    (output/'motion-source.txt').write_bytes(captures[0])
    groups=[[],[],[]]
    for line in captures[0].decode().splitlines():
        g,i,*values=line.split();g=int(g);i=int(i)
        if g not in range(3) or i!=len(groups[g]) or len(values)!=16: raise ArtError('Source model matrix capture extent')
        groups[g].append([float.fromhex(v) for v in values])
    if list(map(len,groups))!=[1,210,420]: raise ArtError('Source model motion counts')
    seam=[max(abs(a-b) for a,b in zip(g[0],g[-1])) for g in groups[1:]]
    matrices=groups[0]+groups[1][:-1]+groups[2][:-1]
    report=dict(tier='HOST',compiler_equal=True,select_frames=209,settings_frames=419,
                seam_max_matrix_delta=seam,source_capture_sha256=hashlib.sha256(captures[0]).hexdigest(),
                adaptation='Periodic lookup resets after209/419 source updates; no cumulative runtime float',
                source_float_matrices=matrices)
    return matrices,report


def model_pack(machines,textures,matrices):
    if len(machines)!=30 or len(matrices)!=629: raise ArtError('Machine pack dimensions')
    names=sorted(textures);texids={n:i for i,n in enumerate(names)}
    data=bytearray(64+30*96);descriptors=[];mini=0
    def insert(raw):
        data.extend(b'\0'*(-len(data)%4));offset=len(data);data.extend(raw);return offset
    def verts(rows):
        return b''.join(struct.pack('>3hH2h3bB',*v['position'],v['flag'],*v['uv'],*v['normal'],v['alpha'])for v in rows)
    for expected,machine in enumerate(machines):
        if machine['character']!=expected: raise ArtError('Machine character order')
        m=machine['geometry'];r=machine['reflection'];s=machine['shadow'];n=len(m['triangles'])
        vo=insert(verts(m['vertices']))
        to=insert(b''.join(struct.pack('>4H',*t['vertices'],t['material']) for t in m['triangles']))
        material=bytearray()
        for row in m['materials']:
            body=row['environment']=='body';env=[0,0,0,0]if body else row['environment']
            material+=struct.pack('>HBB4B4BI',row['tile'],row['combine'],body,*row['primitive'],*env,0)
        mo=insert(material)
        bindings=b''.join(struct.pack('>HBB6H',texids[b['symbol']],b['tile'],0,
                                     *(b['sampler'][k]for k in ('s_mode','t_mode','s_mask','t_mask','s_shift','t_shift')))
                          for b in machine['bindings'])
        bo=insert(bindings);rvo=insert(verts(r['vertices']))
        rto=insert(b''.join(struct.pack('>4H',*t,0)for t in r['triangles']))
        svo=insert(verts(s['vertices']));so=insert(bytes(c for rgb in machine['skins']for c in rgb))
        descriptors.append([expected,vo,len(m['vertices']),to,n,mo,len(m['materials']),bo,len(machine['bindings']),
                            rvo,len(r['vertices']),rto,len(r['triangles']),svo,texids[s['texture']],so,mini,n,
                            0xA20405,0xA60405,0,0,0,0]);mini+=n
    motion=insert(b''.join(struct.pack('>16i',*(round(v*65536)for v in matrix)) for matrix in matrices))
    tmo=insert(bytes(len(names)*32));raw_offset=len(data)
    for i,name in enumerate(names):
        t=textures[name];raw=t['texels'];offset=insert(raw)
        address=({'machine_models':9,'machine_custom_gfx':3}[t['family']]<<24)|t['offset']
        struct.pack_into('>8I',data,tmo+i*32,address,{'RGBA16':0,'I4':1,'IA4':2}[t['format']],
                         t['width'],t['height'],offset,len(raw),fnv(raw),0)
    for i,row in enumerate(descriptors):struct.pack_into('>24I',data,64+i*96,*row)
    struct.pack_into('>8s14I',data,0,b'FZXMM01\0',1,len(data),30,64,96,motion,629,tmo,len(names),
                     raw_offset,len(data)-raw_offset,fnv(data[64:]),0,0)
    validate_models(data)
    return bytes(data),dict(bytes=len(data),models=30,vertices=sum(r[2]for r in descriptors),
                           triangles=mini,reflection_vertices=sum(r[10]for r in descriptors),
                           reflection_triangles=sum(r[12]for r in descriptors),source_textures=len(names),
                           max_vertices=max(r[2]for r in descriptors),max_triangles=max(r[4]for r in descriptors),
                           descriptors=descriptors,texture_symbols=names,fnv=fnv(data))


def validate_models(data):
    if len(data)<64: raise ArtError('Truncated model pack')
    magic,*h=struct.unpack_from('>8s14I',data)
    version,size,count,do,ds,mo,mc,to,tc,ro,rs,checksum,z0,z1=h
    if magic!=b'FZXMM01\0'or(version,size,count,do,ds,mc,z0,z1)!=(1,len(data),30,64,96,629,0,0):raise ArtError('Model pack header')
    if not 0<tc<256 or mo+mc*64!=to or to+tc*32!=ro or ro+rs!=size or fnv(data[64:])!=checksum:raise ArtError('Model pack sections/checksum')
    def span(o,n,stride,limit=0):
        if o%4 or o<64+30*96 or n<=0 or o+n*stride>(limit or mo):raise ArtError('Model pack record span')
    mini=0
    for i in range(count):
        r=struct.unpack_from('>24I',data,do+i*ds)
        if r[0]!=i or r[16]!=mini or r[17]!=r[4]or r[18:20]!=(0xA20405,0xA60405)or any(r[20:]):raise ArtError('Model descriptor identity')
        for o,n,stride in ((r[1],r[2],16),(r[3],r[4],8),(r[5],r[6],16),(r[7],r[8],16),
                            (r[9],r[10],16),(r[11],r[12],8),(r[13],4,16),(r[15],4,3)):span(o,n,stride)
        if r[2]>512 or r[4]>256 or r[6]>64 or r[8]>8 or r[14]>=tc:raise ArtError('Model geometry count')
        for offset,n,vc,mat in ((r[3],r[4],r[2],r[6]),(r[11],r[12],r[10],1)):
            for j in range(n):
                a,b,c,m=struct.unpack_from('>4H',data,offset+j*8)
                if max(a,b,c)>=vc or m>=mat:raise ArtError('Model triangle indices')
        for j in range(r[6]):
            tile,equation,body,*rest=struct.unpack_from('>HBB4B4BI',data,r[5]+j*16)
            if tile>=r[8]or equation>2 or body>1 or rest[-1]:raise ArtError('Model material state')
        for j in range(r[8]):
            tid,tile,z,sm,tm,sk,tk,ss,ts=struct.unpack_from('>HBB6H',data,r[7]+j*16)
            if tid>=tc or tile!=j or z or sm>3 or tm>3 or sk>15 or tk>15 or ss>15 or ts>15:raise ArtError('Model sampler state')
        mini+=r[4]
    for i in range(tc):
        symbol,fmt,w,he,offset,n,checksum,z=struct.unpack_from('>8I',data,to+i*32)
        expected=w*he*2 if fmt==0 else(w+1)//2*he
        if not symbol or fmt>2 or not 0<w<=1024 or not 0<he<=1024 or n!=expected or offset<ro or offset+n>size or z or fnv(data[offset:offset+n])!=checksum:raise ArtError('Model source texture')
    return True


def shadow_chart(vertices,texture):
    """Bake authored shadow UV into native boundary0,3,1,2, retaining its diagonal."""
    if len(vertices)!=4 or(texture['format'],texture['width'],texture['height'])!=('I4',32,64)or len(texture['texels'])!=1024:
        raise ArtError('Shadow source chart extent')
    ordered=[[*vertices[i]['position'],*vertices[i]['uv']]for i in (0,3,1,2)]
    points=chart_geometry(ordered);raw=texture['texels'];indices=[]
    for y in range(64):
        for x in range(32):
            uv,den=uv_at(ordered,points,x,y,32,64)
            # gSPTexture(0xffff,0xffff), mirror|clamp, source masks5/6.
            ij=[address((n*65535+16*den*65536)//(32*den*65536),size,3,mask)
                for n,size,mask in zip(uv,(32,64),(5,6))]
            index=ij[1]*32+ij[0]
            indices.append((raw[index//2]>>(4 if not index&1 else 0))&15)
    return bytes((indices[i]<<4)|indices[i+1]for i in range(0,len(indices),2))


def effect_pack(machines,textures,texture_symbols):
    """Bake original I4 shadow UV; native uses one black half-translucent CLUT."""
    if len(machines)!=30:raise ArtError('Effects character count')
    blob=bytearray(struct.pack('>16H',0,*([0x8000]*15)));descriptors=[];cache={}
    for machine in machines:
        name=machine['shadow']['texture'];texture=textures[name]
        if (texture['format'],texture['width'],texture['height'])!=('I4',32,64):raise ArtError('Effects shadow source format')
        raw=shadow_chart(machine['shadow']['vertices'],texture)
        if raw not in cache:cache[raw]=len(blob);blob.extend(raw)
        descriptors.append((cache[raw],texture_symbols.index(name),0,0))
    reflection=textures['D_9000008'];raw=reflection['texels'];reflection_offset=len(blob)
    if (reflection['format'],reflection['width'],reflection['height'])!=('RGBA16',16,16):raise ArtError('Effects reflection source format')
    alpha_values=set();colors=[]
    for (value,)in struct.iter_unpack('>H',raw):
        alpha_values.add(value&1)
        # Source reflection combines texel RGB with primitive alpha60; texel alpha
        # does not control transparency. Retain RGB5 exactly and mark VDP1 valid.
        colors.append(0x8000|((value>>11)&31)|(((value>>6)&31)<<5)|(((value>>1)&31)<<10))
    blob+=struct.pack('>256H',*colors)
    desc=b''.join(struct.pack('>4I',*row)for row in descriptors);payload=desc+blob
    data=struct.pack('>8s14I',b'FZXME01\0',1,64+len(payload),30,64,64+len(desc),len(blob),
                     fnv(payload),reflection_offset,16,16,0,32,64,0)+payload
    validate_effects(data)
    return data,dict(bytes=len(data),blob_offset=64+len(desc),blob_bytes=len(blob),shadow_palette_offset=0,
                     reflection_pixel_offset=reflection_offset,reflection_pixels_bytes=512,
                     selected_shadow_bytes=1024,selected_vram_bytes=1568,shadow_descriptors=descriptors,
                     reflection_source_alpha_values=sorted(alpha_values),fnv=fnv(data),
                     adaptation='Shadow authoredUV sampled offline into32x64I4 boundary0,3,1,2 withsource0xffff scale andmirror|clamp; originalraw staysinMMODELS. Index0transparent,1..15black withnativefixedhalftransparency. Reflection originalRGB5 retained; nativeblend approximates sourcealpha60.')


def validate_effects(data):
    if len(data)<64:raise ArtError('Truncated effects pack')
    magic,*h=struct.unpack_from('>8s14I',data)
    version,size,count,do,bo,bs,checksum,rpo,rw,rh,spo,sw,sh,z=h
    if magic!=b'FZXME01\0'or(version,size,count,do,bo,rw,rh,spo,sw,sh,z)!=(1,len(data),30,64,544,16,16,0,32,64,0):raise ArtError('Effects header')
    if bo+bs!=size or fnv(data[64:])!=checksum or rpo%8 or rpo<32 or rpo+512!=bs:raise ArtError('Effects extents/checksum')
    if struct.unpack_from('>16H',data,bo)!=(0,*([0x8000]*15)):raise ArtError('Effects shadow palette')
    for i in range(30):
        po,tid,z0,z1=struct.unpack_from('>4I',data,do+i*16)
        if po%8 or po<32 or po+1024>rpo or tid>=256 or z0 or z1:raise ArtError('Effects shadow descriptor')
    if any(not v&0x8000 for(v,)in struct.iter_unpack('>H',data[bo+rpo:])):raise ArtError('Effects reflection RGB mode')
    return True


def export(rom,output):
    output=output.resolve();output_location(output);verify_source(DECOMP);output.mkdir(parents=True,exist_ok=True)
    machines,textures,source=census(rom);matrices,motion=motion_capture(output)
    data,report=model_pack(machines,textures,matrices);outputs={'MMODELS.PAK':data};mini=[];settings=[]
    for machine in machines:
        m=machine['geometry'];tiles=machine['bindings'];cid=machine['character']
        for skin in range(4):
            charts=[]
            for triangle in m['triangles']:
                binding=tiles[m['materials'][triangle['material']]['tile']];texture=textures[binding['symbol']]
                charts.append(chart_colors(m,triangle,texture,binding['sampler'],machine['skins'][skin],16,16))
                if skin==0:mini.append(chart_colors(m,triangle,texture,binding['sampler'],machine['skins'][0],8,2))
            filename='MS%02d%d.PAK'%(cid,skin)
            bank,entry=chart_pack(charts,16,16,cid,skin);outputs[filename]=bank
            settings.append(dict(character=cid,skin=skin,file=filename,**entry))
    outputs['MMINI.PAK'],mini_report=chart_pack(mini,8,2,30,0)
    outputs['MEFFECT.PAK'],effects_report=effect_pack(machines,textures,report['texture_symbols'])
    slots=source_slots()
    report.update(schema=1,tier='HOST',source=source,motion=motion,mini=mini_report,settings=settings,effects=effects_report,slot_to_character=slots,
                  max_settings_vram_bytes=max(r['vram_bytes']for r in settings),
                  geometry_adaptation='None: original LOD1 vertices/UVs/normals/material order and reflection/shadow retained',
                  chart_adaptation='8x2mini /16x16settings point-sampled triangle charts; alloriginal4skins; exactRGB5 palette orRGBfallback',
                  native='UNRUN',appearance='UNRUN')
    outputs['machine_models.json']=(json.dumps(report,indent=2)+'\n').encode()
    # Keep a complete private source oracle, including original texture metadata.
    oracle=dict(machines=machines,textures={n:{k:v for k,v in t.items()if k not in ('rgba','texels')}for n,t in textures.items()})
    outputs['machine_models_source.json']=(json.dumps(oracle,indent=2)+'\n').encode()
    identity='/* Generated source menu models identity; regenerate, never edit. */\n'
    for name,value in [('BYTES',len(data)),('HASH',fnv(data)),('MINI_BYTES',len(outputs['MMINI.PAK'])),
                       ('MINI_HASH',fnv(outputs['MMINI.PAK'])),('MINI_VRAM_BYTES',mini_report['vram_bytes']),
                       ('SETTINGS_MAX_VRAM_BYTES',report['max_settings_vram_bytes']),
                       ('SETTINGS_MAX_BYTES',max(r['bytes']for r in settings)),
                       ('MAX_VERTICES',report['max_vertices']),('MAX_TRIANGLES',report['max_triangles']),
                       ('MAX_REFLECTION_VERTICES',max(r[10]for r in report['descriptors'])),
                       ('MAX_REFLECTION_TRIANGLES',max(r[12]for r in report['descriptors'])),
                       ('EFFECT_BYTES',len(outputs['MEFFECT.PAK'])),('EFFECT_HASH',fnv(outputs['MEFFECT.PAK'])),
                       ('EFFECT_SELECTED_VRAM_BYTES',effects_report['selected_vram_bytes'])]:
        identity+='#define FZX_MACHINE_MODELS_%s %du\n'%(name,value)
    identity+='/* [character][skin]: packbytes,wholepackFNV,VRAMblobbytes. */\n'
    identity+='static const unsigned fzx_machine_models_settings_identity[30][4][3]={\n'
    for character in range(30):
        identity+=' {'+','.join('{%du,%du,%du}'%(r['bytes'],r['fnv'],r['vram_bytes'])for r in settings[character*4:character*4+4])+'},\n'
    identity+='};\n'
    identity+='static const unsigned char fzx_machine_models_slot_to_character[30]={'+','.join(map(str,slots))+'};\n'
    outputs['machine_models_identity.h']=identity.encode()
    deps=[Path(__file__),ROOT/'tools/import_source_art.py',ROOT/'tools/import_course.py',ROOT/'tools/model_assets.py',
          ROOT/'tools/machine_rebake.py',ROOT/'tools/road_pack.py',ROOT/'tools/menu_source.py',ROOT/'tools/countdown_pack.py',
          ROOT/'tools/course_menu_preview.py',ROOT/'include/fzx_machine_menu_model.h',RACER,DRAW,MENU,COMMON,GAME,DECOMP/'src/sys/math.c']
    deps+=sorted((DECOMP/'assets/yaml/us/rev0').glob('machine*.yaml'))
    receipt=['# fzx generation receipt v1; regenerate, never edit']
    receipt+=['generator %s %s'%(hashlib.sha256(p.read_bytes()).hexdigest(),os.path.relpath(p,ROOT/'tools').replace('\\','/'))for p in deps]
    receipt+=['input %s owner-rom'%hashlib.sha256(rom).hexdigest()]
    for name,content in outputs.items():
        (output/name).write_bytes(content);receipt.append('output %s %s'%(hashlib.sha256(content).hexdigest(),name))
    (output/'machine_models.receipt').write_bytes(('\n'.join(receipt)+'\n').encode())
    return {k:v for k,v in report.items()if k in ('bytes','models','vertices','triangles','reflection_vertices','reflection_triangles','source_textures','max_vertices','max_triangles','max_settings_vram_bytes','mini','effects')}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'generated/machine_menu_models')
    parser.add_argument('--config',type=Path)
    args=parser.parse_args();path=json.loads(args.config.read_text())['rom']if args.config else os.environ.get('FZX_TEST_ROM')
    if not path:parser.error('Set FZX_TEST_ROM privately or pass --config')
    if os.name!='nt'and re.match(r'^[A-Za-z]:[\\/]',path):path='/mnt/'+path[0].lower()+'/'+path[3:].replace('\\','/')
    try:print(json.dumps(export(Path(path).read_bytes(),args.output),sort_keys=True))
    except Exception as exc:
        print('Machine model export failed: '+(str(exc)if isinstance(exc,ArtError)else type(exc).__name__),file=sys.stderr);return 1
    return 0


if __name__=='__main__':raise SystemExit(main())
