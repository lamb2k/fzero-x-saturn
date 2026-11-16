#!/usr/bin/env python3
"""Eight synthetic texture panels through the same native model pack path."""
from pathlib import Path
import hashlib
import sys

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
from model_assets import header


def fixture_data():
    vertices, triangles, materials, textures, bindings, raw = [], [], [], [], [], {}
    for tile in range(8):
        symbol = "synthetic_panel_" + str(tile)
        textures.append(dict(symbol=symbol, format="I4", width=16, height=16))
        raw[symbol] = bytes((((x+tile)%16)<<4)|((x+y+1)%16) for y in range(16) for x in range(8))
        if tile == 0:
            bindings.append(dict(tile=tile, symbol=symbol, sampling=dict(s_mode=3,t_mode=3,s_mask=4,t_mask=4,s_shift=0,t_shift=0)))
        else:
            bindings.append(dict(tile=tile,symbol=symbol,source_command="gDPLoadMultiBlock_4b",
                                 arguments=["0",str(tile),"G_IM_FMT_I","16","16","0","G_TX_CLAMP","G_TX_WRAP","4","4","G_TX_NOLOD","G_TX_NOLOD"]))
        x, y = (tile%4)*120-180, (tile//4)*120
        start = len(vertices)
        for dx,dy,u,v in [(-45,-45,0,0),(45,-45,480,0),(45,45,480,480),(-45,45,0,480)]:
            vertices.append(dict(position=[x+dx,y+dy,0],uv=[u,v],normal=[0,0,-127]))
        triangles.extend([dict(vertices=[start,start+1,start+2],material=tile),dict(vertices=[start,start+2,start+3],material=tile)])
        materials.append(dict(tile=tile,primitive=[255,150,50,255],environment=[0,30,160,255],combine=["PRIMITIVE"]))
    models=[dict(vertices=vertices,triangles=triangles,materials=materials) for _ in range(5)]
    return models,bindings,textures,raw,list(range(256)),[0,255,255],[255,240,180]


def fixture():
    return header(*fixture_data(),fixture=True)


if __name__ == "__main__":
    out=TOOLS.parent/"generated/model_fixture"
    out.mkdir(parents=True,exist_ok=True)
    data=fixture();(out/"model_probe.h").write_bytes(data)
    digest=lambda b:hashlib.sha256(b).hexdigest()
    receipt=["# fzx generation receipt v1"]
    for name in ("model_assets.py","model_probe/make_fixture.py"):
        receipt.append(f"generator {digest((TOOLS/name).read_bytes())} {name}")
    receipt += [f"output {digest(data)} model_probe.h", ""]
    (out/"source_art.receipt").write_bytes("\n".join(receipt).encode())
    print("Synthetic model panels generated; no retail model or pixels")
