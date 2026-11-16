#!/usr/bin/env python3
"""Compare held SH-2 source-road packets to the original planner's HOST oracle."""
from pathlib import Path
import argparse,json,struct

def reference(text):
    result={}
    for line in text.splitlines():
        p=line.split()
        if len(p)<13:raise ValueError('truncated source road trace')
        tick,count,visible,outer,fog=map(int,p[:5])
        if tick<0 or tick+1 in result or count<0 or count>64 or len(p)!=13+count:
            raise ValueError('invalid source road trace')
        data=bytearray(struct.pack('>4Ii8B',0,count,visible,outer,fog,*map(int,p[5:13])))
        for group in p[13:]:
            a,b,d,z=group.split(',');data+=struct.pack('>HHiI',int(a),int(b),int(d),int(z,16))
        data+=bytes(12*(64-count))
        result[tick+1]=bytes(data)
    if not result:raise ValueError('empty source road trace')
    return result

def check(rows,expected):
    if isinstance(rows,dict):rows=[rows]
    if not rows:raise ValueError('no road samples')
    seen=set()
    for row in rows:
        tick=row['tick']
        if tick in seen or tick not in expected or row['status']!=1 or row['sequence']<1 or row['frame']!=tick-1:
            raise ValueError('invalid or duplicate road sample')
        seen.add(tick)
        if bytes.fromhex(row['plan_hex'])!=expected[tick]:raise ValueError('source road differs at tick '+str(tick))
    return len(rows)

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('samples',type=Path);ap.add_argument('reference',type=Path)
    a=ap.parse_args();rows=json.loads(a.samples.read_text(encoding='utf-8-sig'));expected=reference(a.reference.read_text())
    n=check(rows,expected)
    for offset in (15,20,24,35):
        mutant=json.loads(json.dumps(rows));mutant=[mutant] if isinstance(mutant,dict) else mutant
        data=bytearray.fromhex(mutant[0]['plan_hex']);data[offset]^=1;mutant[0]['plan_hex']=data.hex()
        try:check(mutant,expected)
        except ValueError:pass
        else:raise ValueError('wrong road packet accepted')
    print('Native source road readback PASS:',n,'exact packets; outer/fog/rail/group controls rejected')

if __name__=='__main__':main()
