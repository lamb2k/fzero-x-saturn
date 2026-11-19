#!/usr/bin/env python3
"""Per-family vehicle rebake for the engine's 3D machines (tools/mach3d_packs.py).

machine_rebake.py stays the legacy importer path (its hash is bound by the legacy
machine menu, records and MACHRLD/MACHRTX receipts). This module adds the per-family
build: build(..., quantize=True) gives a single triangle sampling more than 16 colours
16 representative ones, merges palettes of one material over the native capacity
(first fit, then lossy joins) and lays texels out in face order. With quantize off,
family 0 bakes byte for byte as machine_rebake.build.
"""
from machine_rebake import *  # noqa: F401,F403 (helpers, constants and the shared imports)
from machine_rebake import ArtError, decode_texture, sampler, chart_geometry, uv_at, fnv  # noqa: F401

PALETTE_CAP=32  # include/fzx_vehicle.h FZX_VEHICLE_PALETTES (native arm)


def reduce16(colors):
    """16 representative RGBA colours for a chart that samples more (deterministic k-means
    over the sampled texels, seeded by frequency). Used only when quantize is requested."""
    counts={}
    for c in colors:counts[c]=counts.get(c,0)+1
    keys=sorted(counts,key=lambda k:(-counts[k],k));centres=[list(k) for k in keys[:16]]
    for _ in range(8):
        sums=[[0,0,0,0,0] for _ in centres]
        for k,n in counts.items():
            best=min(range(len(centres)),key=lambda i:(sum((k[j]-centres[i][j])**2 for j in range(4)),i))
            for j in range(4):sums[best][j]+=k[j]*n
            sums[best][4]+=n
        centres=[[(s[j]+s[4]//2)//s[4] for j in range(4)] if s[4] else centres[i] for i,s in enumerate(sums)]
    centres=[tuple(c) for c in centres]
    return [min(centres,key=lambda c:(sum((k[j]-c[j])**2 for j in range(4)),c)) for k in colors]


def chart(model,face,texture,sampling,pixels,quantize=False):
    vertices=[model['vertices'][i] for i in face['vertices']]
    v=[[*p['position'],*((u*65535)//65536 for u in p['uv'])] for p in vertices]
    if sampling['s_shift'] or sampling['t_shift']:
        raise ArtError('Vehicle rebake requires the source unshifted sampler')
    tw,th=texture['width'],texture['height']
    if not tw or not th or len(pixels)!=tw*th*4:raise ArtError('Invalid vehicle source image')
    for axis,n in (('s',tw),('t',th)):
        if sampling[axis+'_mode'] not in range(4) or sampling[axis+'_mask'] not in range(9):raise ArtError('Invalid vehicle sampler')
        if sampling[axis+'_mask'] and 1<<sampling[axis+'_mask']!=n:raise ArtError('Vehicle sampler mask mismatch')
    def distance(a,b):return sum((v[a][i]-v[b][i])**2 for i in (3,4))
    if len(v)==4:
        points=chart_geometry(v)
        w=extent(max(distance(0,1),distance(3,2)),8,32)
        h=extent(max(distance(0,3),distance(1,2)),2,32)
    else:
        w=extent(distance(0,1),8,16)
        h=extent(max(distance(0,2),distance(1,2)),2,16)
    colors=[]
    for y in range(h):
        for x in range(w):
            if len(v)==4:uv,den=uv_at(v,points,x,y,w,h)
            else:
                weights=[(w-1-x)*(h-1-y),x*(h-1-y),y*(w-1)];den=(w-1)*(h-1)
                uv=[sum(weights[k]*v[k][i] for k in range(3)) for i in (3,4)]
            # Point-sampled original UV, after the original texture scale.
            indices=[address((n+16*den)//(32*den),size,sampling[axis+'_mode'],sampling[axis+'_mask'])
                     for n,size,axis in zip(uv,(tw,th),('s','t'))]
            at=(indices[1]*tw+indices[0])*4;colors.append(tuple(pixels[at:at+4]))
    palette=sorted(set(colors))
    if len(palette)>16 and quantize and len(face['triangles'])==1:
        colors=reduce16(colors);palette=sorted(set(colors));chart.quantized+=1
    if len(palette)>16:raise ArtError('Vehicle chart exceeds 16 source colors')
    mapping={c:i for i,c in enumerate(palette)};indices=[mapping[c] for c in colors]
    packed=bytes((indices[i]<<4)|indices[i+1] for i in range(0,len(indices),2))
    return w,h,palette,packed


chart.quantized=0


def build(models,bindings,textures,raw,lut,body,fog,fixture=False,orders=None,quantize=False):
    """quantize (default off, the Blue Falcon never needs it): a single triangle whose
    chart samples more than 16 source colours gets 16 representative ones (counted in
    the report's quantized_charts) instead of failing."""
    chart.quantized=0
    if len(models)<5 or len(lut)!=256 or any(not 0<=x<=255 for x in [*lut,*body,*fog]) or len(body)!=3 or len(fog)!=3:
        raise ArtError('Invalid vehicle source setup')
    by_name={t['symbol']:t for t in textures};by_tile={b['tile']:b for b in bindings}
    orders_supplied=orders;supplied=0
    image_cache={};palette_ids={};palettes=[];bank=bytearray();prepared=[]
    for lod in LODS:
        model=models[lod]
        if not 0<len(model['vertices'])<=512 or not 0<len(model['triangles'])<=256:raise ArtError('Vehicle model capacity exceeded')
        unique_positions={};unique_normals={};maps=[]
        for v in model['vertices']:
            if len(v['position'])!=3 or len(v['uv'])!=2 or len(v['normal'])!=3 or any(abs(x)>1024 for x in v['position']):
                raise ArtError('Invalid vehicle vertex')
            if any(not -32768<=x<=32767 for x in v['uv']) or any(not -128<=x<=127 for x in v['normal']):raise ArtError('Vehicle vertex range exceeded')
            maps.append((unique_positions.setdefault(tuple(v['position']),len(unique_positions)),unique_normals.setdefault(tuple(v['normal']),len(unique_normals))))
        faces=[];rejected_pairs=0
        queue=pairing(model)
        while queue:
            face=queue.pop(0);mat=model['materials'][face['material']];binding=by_tile[mat['tile']];tex=by_name[binding['symbol']]
            if tex['symbol'] not in image_cache:image_cache[tex['symbol']]=decode_texture(raw[tex['symbol']],tex['format'],tex['width'],tex['height'])
            try:w,h,colors,pixels=chart(model,face,tex,sampler(binding,tex),image_cache[tex['symbol']],quantize)
            except ArtError as exc:
                if len(face['triangles'])!=2 or str(exc)!='Vehicle chart exceeds 16 source colors':raise
                rejected_pairs+=1
                for i in reversed(face['triangles']):
                    tri=model['triangles'][i];queue.insert(0,dict(vertices=list(tri['vertices']),triangles=[i],material=tri['material']))
                continue
            key=material_key(mat);identity=(key,tuple(colors))
            if identity not in palette_ids:
                palette_ids[identity]=len(palettes);palettes.append((key,colors))
            normal,plane=face_plane([model['vertices'][i] for i in face['vertices']])
            center=[sum(model['vertices'][j]['position'][i] for j in face['vertices']) for i in range(3)]
            faces.append(dict(**face,palette=palette_ids[identity],width=w,height=h,offset=len(bank),normal=normal,plane=plane,center=center))
            bank+=pixels
        faces.sort(key=lambda f:min(f['triangles']))
        covered=sorted(i for f in faces for i in f['triangles'])
        if covered!=list(range(len(model['triangles']))):raise ArtError('Vehicle face coverage changed')
        orders=[]
        for octant in range(8):
            direction=[1 if octant&(1<<i) else -1 for i in range(3)]
            orders.append(sorted(range(len(faces)),key=lambda i:(Fraction(dot(faces[i]['center'],direction),len(faces[i]['vertices'])),i)))
        if orders_supplied:
            # Supplied back-to-front orders index the unsplit pairing() faces.
            index={tuple(f['triangles']):i for i,f in enumerate(faces)};source=[tuple(f['triangles']) for f in pairing(model)]
            for octant in range(8):
                order=orders_supplied.get('%d:%d'%(lod+1,octant))
                if order is None:continue
                if sorted(order)!=list(range(len(source))) or any(t not in index for t in source):raise ArtError('Supplied vehicle order does not match the faces')
                orders[octant]=[index[source[i]] for i in order];supplied+=1
        prepared.append(dict(lod=lod+1,model=model,maps=maps,positions=list(unique_positions),normals=list(unique_normals),faces=faces,orders=orders,rejected_pairs=rejected_pairs))
    # Texels in face order (the pack's contract): a pair split for its colours puts its two
    # triangles' charts next to each other in the bank, ahead of faces sorted between them.
    relaid=bytearray()
    for p in prepared:
        for f in p['faces']:
            n=f['width']*f['height']//2;chunk=bank[f['offset']:f['offset']+n];f['offset']=len(relaid);relaid+=chunk
    bank=relaid
    merged=0
    if quantize and len(palettes)>PALETTE_CAP:
        # Over the native draw's palette capacity: merge palettes of one material key whose
        # colour union fits 16 (largest first, first fit), then re-index the chart texels.
        groups=[]
        for i in sorted(range(len(palettes)),key=lambda i:(-len(palettes[i][1]),i)):
            key,colors=palettes[i]
            for g in groups:
                if g['key']==key and len(g['colors']|set(colors))<=16:g['colors']|=set(colors);g['members'].append(i);break
            else:groups.append(dict(key=key,colors=set(colors),members=[i]))
        # Still over: join the two groups of one key with the smallest colour union (lossy:
        # a group over 16 colours is reduced to 16 below), until the capacity holds.
        while len(groups)>PALETTE_CAP:
            best=None
            for a in range(len(groups)):
                for b in range(a+1,len(groups)):
                    if groups[a]['key']!=groups[b]['key']:continue
                    u=len(groups[a]['colors']|groups[b]['colors'])
                    if best is None or u<best[0]:best=(u,a,b)
            if best is None:raise ArtError('Vehicle palettes exceed the native capacity (material keys)')
            _,a,b=best;groups[a]['colors']|=groups[b]['colors'];groups[a]['members']+=groups[b]['members'];del groups[b]
        new_of={};merged_palettes=[];nearest=[]
        for g in sorted(groups,key=lambda g:min(g['members'])):
            colors=sorted(g['colors']);near={c:c for c in colors}
            if len(colors)>16:
                reduced=reduce16(colors);near=dict(zip(colors,reduced));colors=sorted(set(reduced));chart.quantized+=1
            merged_palettes.append((g['key'],colors));nearest.append(near)
            for m in g['members']:new_of[m]=len(merged_palettes)-1
        for p in prepared:
            for f in p['faces']:
                g=new_of[f['palette']];old=palettes[f['palette']][1];new=merged_palettes[g][1]
                index={c:new.index(nearest[g][c]) for c in old}
                n=f['width']*f['height']//2;chunk=bank[f['offset']:f['offset']+n];out=bytearray(n)
                for i,b in enumerate(chunk):out[i]=(index[old[b>>4]]<<4)|index[old[b&15]]
                bank[f['offset']:f['offset']+n]=out;f['palette']=new_of[f['palette']]
        merged=len(palettes)-len(merged_palettes);palettes=merged_palettes
    data=bytearray(HEADER+len(prepared)*DESCRIPTOR);data[:8]=b'FZXMCH02'
    def align():data.extend(bytes((-len(data))%8))
    for index,p in enumerate(prepared):
        align();va=len(data)
        for v,ids in zip(p['model']['vertices'],p['maps']):
            data+=struct.pack('>5h3bB2H2x',*v['position'],*v['uv'],*v['normal'],v.get('alpha',255),*ids)
        align();pa=len(data)
        for pos in p['positions']:data+=struct.pack('>3hH',*pos,0)
        align();na=len(data)
        for n in p['normals']:data+=struct.pack('>3bB',*n,0)
        align();fa=len(data)
        for f in p['faces']:
            indices=f['vertices'] if len(f['vertices'])==4 else f['vertices']+[f['vertices'][-1]]
            triangle_ids=f['triangles'] if len(f['triangles'])==2 else f['triangles']+[65535]
            data+=struct.pack('>8HI3hHi4H3i4x',*indices,f['palette'],f['width'],f['height'],len(f['vertices']),f['offset'],
                              *f['normal'],0,f['plane'],*triangle_ids,f['material'],0,*f['center'])
        align();oa=len(data)
        for order in p['orders']:data+=struct.pack('>'+str(len(order))+'H',*order)
        struct.pack_into('>12I',data,HEADER+index*DESCRIPTOR,p['lod'],len(p['model']['vertices']),len(p['positions']),len(p['normals']),
                         len(p['faces']),va,pa,na,fa,oa,len(p['model']['triangles']),0)
    align();palette_at=len(data)
    for key,colors in palettes:
        primitive,environment,is_body,alpha=key
        data+=struct.pack('>9B7x',*primitive,*environment,is_body,alpha,len(colors))
        data+=b''.join(bytes(c) for c in colors)+bytes((16-len(colors))*4)
        native=[base_color(key,c,body) for c in colors];native+=native[:1]*(16-len(native))
        data+=struct.pack('>16H',*native)
    align();lut_at=len(data);data+=bytes(lut)
    if len(data)>PACK_LIMIT or len(bank)>TEXTURE_LIMIT:raise ArtError('Prepared vehicle exceeds pack budget')
    struct.pack_into('>10I',data,8,len(data),len(bank),fnv(bank),0,int(fixture),3,HEADER,len(palettes),palette_at,lut_at)
    data[48:54]=bytes([*body,*fog]);struct.pack_into('>I',data,20,fnv(data))
    report=dict(**({'quantized_charts':chart.quantized,'merged_palettes':merged} if quantize else {}),format='FZXMCH02',metadata_bytes=len(data),texture_bytes=len(bank),palette_groups=len(palettes),palette_vram_bytes=len(palettes)*32,
                gouraud_bytes=max(len(p['faces']) for p in prepared)*8,
                lods=[dict(lod=p['lod'],source_vertices=len(p['model']['vertices']),source_triangles=len(p['model']['triangles']),positions=len(p['positions']),
                           normals=len(p['normals']),faces=len(p['faces']),pairs=sum(len(f['triangles'])==2 for f in p['faces']),palette_rejected_pairs=p['rejected_pairs']) for p in prepared],
                runtime='UNRUN',adaptations=['original coplanar convex pairs only; UV/normal/material seams retained',
                'nearest original texels; triangle chart cap16 and quad chart cap32',
                'eight offline centroid orders; appearance qualification pending' if not supplied else '%d supplied offline topological orders, centroid orders elsewhere; appearance qualification pending'%supplied,
                'unlit default-body palette prebaked; source colors retained for shared live body changes; lighting is pending'])
    return bytes(data),bytes(bank),report
