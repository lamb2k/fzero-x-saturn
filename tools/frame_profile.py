#!/usr/bin/env python3
"""Decode an uninterrupted native full-frame capture. All timing is MODELLED.

VDP1 callback observes draw complete before framebuffer-change request: latency
includes scheduling/display wait. It does not isolate rasterization time.
"""
import argparse
import json
import math
from pathlib import Path
import struct

def delta(a,b):return (a-b)&0xffffffff
def stats(values):
    s=sorted(values)
    return dict(mean=sum(s)/len(s),p95=s[math.ceil(.95*len(s))-1],p99=s[math.ceil(.99*len(s))-1],maximum=s[-1])

def decode(data):
    h=struct.unpack('>16I',bytes.fromhex(data['header_hex']))
    raw=bytes.fromhex(data['rows_hex'])
    if (h[0]!=0x465a4650 or h[1] not in (1,2) or h[2]!=1) or h[6]!=64 or h[8] or h[12]!=h[4] or len(raw)!=h[4]*64 or not data['guards_intact'] or data['intentional_holds']:
        raise ValueError('invalid/overflowed/discontinuous capture')
    rows=list(struct.iter_unpack('>16I',raw))
    if len(rows)<2 or h[10] not in (50,60):raise ValueError('insufficient recording or video rate')
    # The native catch-up limit is five display fields, not five source ticks.
    # PAL advances six 60 Hz source ticks across those five 50 Hz fields.
    max_steps=math.ceil(5*60/h[10])
    for i,r in enumerate(rows):
        if r[15]!=3 or r[1]-r[0]!=r[14] or r[14]>max_steps:
            raise ValueError('incomplete row or source tick discontinuity')
        times=r[2:10]
        if any(delta(b,a)>0x7fffffff for a,b in zip(times,times[1:])) or delta(r[10],times[6])>0x7fffffff:
            raise ValueError('unordered timer samples')
        if i and (r[0]!=rows[i-1][1] or delta(r[12],rows[i-1][12])==0):
            raise ValueError('noncontiguous inputs or duplicate presentation')
    fields=delta(rows[-1][12],rows[0][12])
    ticks=delta(rows[-1][10],rows[0][10])
    if not fields or not ticks or not .98*h[9]<=ticks/fields<=1.02*h[9]:
        raise ValueError('extended clock disagrees with independent field count')
    # Calibrate against completed presentation field count over the same window.
    # This avoids treating the legacy 16-bit HUD timer as a whole-frame clock.
    tick_ms=fields*1000/h[10]/ticks
    intervals=[delta(b[10],a[10])*tick_ms for a,b in zip(rows,rows[1:])]
    gaps=[delta(b[12],a[12]) for a,b in zip(rows,rows[1:])]
    phase_names=['input','simulation','publication','wait_previous','scene_build','hud','submit_return']
    phases={name:stats([delta(r[3+i],r[2+i])*tick_ms for r in rows]) for i,name in enumerate(phase_names)}
    phases['submit_to_presentation']=stats([delta(r[10],r[8])*tick_ms for r in rows])
    phases['loop_to_presentation']=stats([delta(r[10],r[2])*tick_ms for r in rows])
    subsystems={}
    if h[1]==2:
        names=['sky','skyline','floor','clouds','road','machine','race_hud']
        aggregate=bytes.fromhex(data.get('subsystems_hex',''))
        if h[15]!=len(names) or len(aggregate)!=32*len(names):
            raise ValueError('missing subsystem counters')
        tick_total=0
        for name,v in zip(names,struct.iter_unpack('>8I',aggregate)):
            calls,lo,hi,clo,chi,minimum,maximum,peak_ticks=v
            total_ticks=lo+(hi<<32);commands=clo+(chi<<32)
            if calls!=len(rows) or minimum>maximum or not minimum*calls<=commands<=maximum*calls or total_ticks>peak_ticks*calls:
                raise ValueError('subsystem counters disagree with completed frames')
            tick_total+=total_ticks
            subsystems[name]=dict(presented_frames=calls,commands=commands,
                commands_per_presented_frame=commands/calls,min_commands=minimum,max_commands=maximum,
                frt_ticks=total_ticks,frt_ticks_per_command=total_ticks/commands if commands else None,
                mean_cpu_ms=total_ticks*tick_ms/calls,max_cpu_ms=peak_ticks*tick_ms)
        if tick_total>sum(delta(r[7],r[6]) for r in rows):
            raise ValueError('subsystem ticks exceed scene construction')
    road_samples={}
    if 'road_profile_header_hex' in data:
        rh=struct.unpack('>8I',bytes.fromhex(data['road_profile_header_hex']))
        rr=bytes.fromhex(data.get('road_profile_rows_hex',''))
        if (h[1]!=2 or rh[0]!=0x43335046 or rh[1]!=1 or rh[2]!=h[3]+h[5]*64 or
            rh[3]!=h[5] or rh[4]!=8 or rh[5]!=len(rows) or rh[5]>rh[3] or
            rh[6]!=h[7] or rh[6]!=rh[2]+rh[3]*8 or rh[7]!=0 or len(rr)!=len(rows)*8):
            raise ValueError('road profile layout disagrees with frame recording')
        samples=list(struct.iter_unpack('>2I',rr))
        road=subsystems['road']
        if (sum(t for t,c in samples)!=road['frt_ticks'] or sum(c for t,c in samples)!=road['commands'] or
            max(t for t,c in samples)*tick_ms!=road['max_cpu_ms'] or
            min(c for t,c in samples)!=road['min_commands'] or max(c for t,c in samples)!=road['max_commands'] or
            any(t>delta(r[7],r[6]) for (t,c),r in zip(samples,rows))):
            raise ValueError('road samples disagree with subsystem totals or scene timing')
        road_samples=dict(cpu_ms=stats([t*tick_ms for t,c in samples]),
            commands=stats([c for t,c in samples]),scope='Scene4 including native user clip and command conversion')
    return dict(tier='MODELLED',hardware='UNRUN',iso_sha256=data.get('iso_sha256'),rows=len(rows),
        input_begin=rows[0][0],input_end=rows[-1][1],presented_fields=fields,
        video_hz=h[10],ccr=h[11],clock_max_gap=h[9],frt_ticks_per_field=ticks/fields,
        delivered_fps=(len(rows)-1)*h[10]/fields,presentation_ms=stats(intervals),
        missed_fields=sum(max(0,g-1) for g in gaps),max_presentation_field_gap=max(gaps),
        simulation_steps=stats([r[14] for r in rows]),phases_ms=phases,subsystems=subsystems,road_samples=road_samples,
        scope='Uninterrupted replay after restart frame; callback before framebuffer change, not pure GPU time. Phase sums may overlap presentation latency.')

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('capture',type=Path);p.add_argument('--output',type=Path)
    a=p.parse_args();result=decode(json.loads(a.capture.read_text(encoding='utf-8-sig')))
    text=json.dumps(result,indent=2)
    if a.output:a.output.write_text(text+'\n',encoding='utf-8')
    print(text)
if __name__=='__main__':main()
