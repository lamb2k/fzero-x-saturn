#!/usr/bin/env python3
"""HOST road-tier feasibility census. Requires NumPy; never changes runtime art.

Input is the private little-endian RefCamera (252 bytes) + RefRoadPlan
(796 bytes) stream recorded by the source replay, plus original road JSON.
Counts use a temporary HOST copy of the frozen geometry/clipping code, with
only repetition intervals changed. A separate Python bound checks them.
Requires a POSIX GCC/Clang compiler. This is a budget experiment, not an
image-quality or native-speed gate.
"""
from pathlib import Path
import argparse
import hashlib
import json
import math
import struct
import subprocess
import tempfile

import numpy as np
from import_source_art import ArtError, ROOT, output_location
from road_tiles import pair, strip

RECORD_BYTES = 1048


def reference_variant(source):
    """Counting adapter only, never a native renderer or proposed texture map.

    Keep the reference's integer geometry, winding, clipping and emission.
    Change only the repetition interval/mean selection. Texture addresses are
    dummy because grouped tier images do not exist yet; no pixels are compared.
    Exact sentinels make changes to the reference fail closed.
    """
    replacements = [
        ('#define U16 fzx_road_u16', 'static unsigned census_forced, census_tiers[4];\nstatic int census_thresholds[3];\n#define U16 fzx_road_u16'),
        ('if(front && (int64_t)screen_span*period*32 < (int64_t)span*3)',
         'if(!census_forced && front && (int64_t)screen_span*period*32 < (int64_t)span*census_thresholds[2])'),
        ('++d->stats->mean;return piece', '++census_tiers[3];++d->stats->mean;return piece'),
        ('const int32_t unit=(int32_t)period*32;',
         'unsigned census_tier=0;\n'
         '    if(!census_forced && front) {\n'
         '        int64_t pixels=(int64_t)screen_span*period*32;\n'
         '        if(pixels<(int64_t)span*census_thresholds[1])census_tier=2;\n'
         '        else if(pixels<(int64_t)span*census_thresholds[0])census_tier=1;\n'
         '    }\n'
         '    ++census_tiers[census_tier];\n'
         '    const int32_t unit=(int32_t)period*32*(1<<census_tier);'),
        ('if(!height || row+height>period)return fault(d,1);',
         'if(!height || row+height>period*(1u<<census_tier))return fault(d,1);\n'
         '        if(census_tier){row=0;height=1;} /* No tier texture yet. */')]
    for old, new in replacements:
        if source.count(old) != 1:
            raise ArtError('Frozen road reference changed; review counting adapter')
        source = source.replace(old, new)
    return source


def run_reference(replay, output, thresholds, cc):
    if any(int(t) != t for t in thresholds):
        raise ArtError('HOST reference requires integral pixel thresholds')
    code = ROOT/'src/fzx_road_tiles.c'
    inputs = [code, ROOT/'src/fzx_view.c', ROOT/'src/fzx_road_pack.c',
              ROOT/'include/fzx_road_tiles.h', ROOT/'include/fzx_road_pack.h', ROOT/'include/fzx_view.h',
              ROOT/'include/fzx_road_profile.h', ROOT/'ref/ref.h', ROOT/'ref/road_plan.h',
              ROOT/'tools/road_lod_host.c', ROOT/'generated/road_pack/ROAD.PAK',
              ROOT/'generated/road_tiles/ROADTL.PAK']
    with tempfile.TemporaryDirectory(prefix='fzx-road-census-') as td:
        temp = Path(td)
        variant = reference_variant(code.read_text())
        (temp/'road_lod_variant.c').write_text(variant)
        command = [cc, '-std=c11', '-O2', '-fsanitize=undefined', '-fno-sanitize-recover=all',
                   '-DFZX_SOURCE_CAMERA=1', '-I'+str(ROOT/'include'), '-I'+str(ROOT/'ref'), '-I'+str(temp),
                   str(ROOT/'tools/road_lod_host.c'), str(ROOT/'src/fzx_view.c'),
                   str(ROOT/'src/fzx_road_pack.c'), '-lm', '-o', str(temp/'census')]
        subprocess.run(command, check=True)
        subprocess.run([str(temp/'census'), str(inputs[-2]), str(inputs[-1]), str(replay),
                        str(output/'reference.bin'), *[str(int(t)) for t in thresholds]], check=True)
    receipt = dict(compiler=subprocess.check_output([cc, '--version'], text=True).splitlines()[0],
                   flags=command[1:6], variant_sha256=hashlib.sha256(variant.encode()).hexdigest(),
                   inputs={str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs},
                   replay_sha256=hashlib.sha256(replay.read_bytes()).hexdigest(),
                   output_sha256=hashlib.sha256((output/'reference.bin').read_bytes()).hexdigest())
    (output/'reference-receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
    return (output/'reference.bin').read_bytes()


def pieces(lo, hi, unit):
    if unit <= 0 or hi <= lo:
        raise ArtError('Invalid repetition span')
    return (hi - 1) // unit - lo // unit + 1


def tier(pixels, thresholds=(16, 8, 4)):
    if len(thresholds) != 3 or not thresholds[0] > thresholds[1] > thresholds[2] > 0:
        raise ArtError('Thresholds must be descending and positive')
    return 1 if pixels >= thresholds[0] else 2 if pixels >= thresholds[1] else 4 if pixels >= thresholds[2] else 0


def chunks(plan, count):
    faults, groups, visible, outer = struct.unpack_from('<4I', plan)
    if faults or groups > 64 or outer > 1:
        raise ArtError('Invalid source road plan')
    result = []
    for g in range(groups):
        start, end, direction = struct.unpack_from('<HHi', plan, 28 + 12*g)
        if start >= count or end >= count or direction not in (-1, 1):
            raise ArtError('Invalid source chunk group')
        i = (end - 1) % count if direction == 1 else start
        for _ in range(count):
            result.append(i)
            if direction == 1:
                if i == start:
                    break
                i = (i - 1) % count
            else:
                i = (i + 1) % count
                if i == end:
                    break
        else:
            # start==end legitimately visits the complete circular course.
            if start != end:
                raise ArtError('Unterminated source chunk group')
    if len(result) != visible or len(set(result)) != len(result):
        raise ArtError('Visible chunk count/uniqueness mismatch')
    return result, bool(outer)


def trunc_div(n, d):
    return np.sign(n) * (np.abs(n) // d)


def project(camera, positions):
    """Same fixed-point conversion/transform as the frozen source view path."""
    if len(camera) != 252 or struct.unpack_from('<I', camera, 248)[0]:
        raise ArtError('Invalid source camera')
    f = np.frombuffer(camera, dtype='<f4')
    if not np.all(np.isfinite(f[4:62])):
        raise ArtError('Nonfinite source camera')
    view = f[30:46].reshape(4, 4)
    projection = f[46:62].reshape(4, 4)
    vp = f[22:26]
    rotation = (view[:3, :3].T * np.array([1, 1, -1], dtype=np.float32)[:, None] * np.float32(2**30)).astype(np.int64)
    translation = (view[3, :3] * np.array([1, 1, -1], dtype=np.float32) * np.float32(4096)).astype(np.int64)
    p = trunc_div((positions*4096) @ rotation.T, 2**30) + translation
    focal = (vp[:2] * np.array([projection[0, 0], projection[1, 1]]) * np.float32(65536)).astype(np.int64)
    center = (np.array([vp[2] - vp[0]*projection[2, 0], vp[3] - np.float32(8) + vp[1]*projection[2, 1]]) * np.float32(65536)).astype(np.int64)
    near, far = (f[20:22] * np.float32(4096)).astype(np.int64)
    if near <= 0 or far <= near or np.any(np.abs(p) > 2147483647):
        raise ArtError('Invalid fixed-point view range')
    x, y, z = p.T
    # Camera_Draw single-player crop, as used by the admitted native path.
    distances = (z-near, far-z, focal[0]*x+(center[0]-12*65536)*z,
                 (308*65536-center[0])*z-focal[0]*x,
                 (center[1]-8*65536)*z-focal[1]*y,
                 focal[1]*y+(216*65536-center[1])*z)
    codes = sum((d < 0).astype(np.uint8)*(1 << k) for k, d in enumerate(distances))
    denominator = np.maximum(z, 1)
    xy16 = np.column_stack((center[0]+trunc_div(focal[0]*x, denominator),
                            center[1]-trunc_div(focal[1]*y, denominator)))
    xy = trunc_div(xy16, 65536)
    front = (z >= near) & (np.abs(xy16).max(axis=1) <= 2147483647)
    safe = front & (np.abs(xy).max(axis=1) <= 900)
    native = struct.pack('<18i4h', *rotation.ravel(), *translation, *focal, *center, near, far, 12, 8, 308, 216)
    return p, xy, codes, safe, front, native


def prepare(mesh):
    quads = pair(mesh)
    vertices = mesh['vertices']
    strips, lookup = [], {}
    for q in quads:
        p = [vertices[i] for i in q['vertices']]
        q['strip'] = -1
        if q['primitive']:
            q.update(lo=0, hi=1, unit=1, edges=(0, 1, 3, 2))
            continue
        u0, u1 = sorted((p[0][3], p[2][3]))
        key = (q['material'], u0, u1)
        if key not in lookup:
            data, info = strip(mesh, *key, [0, 0, 0])
            words = struct.unpack('>%dH' % (len(data)//2), data)
            colors = len(set(words[:info['width']*info['period']]))
            info.update(material=key[0], u0=u0, u1=u1, unique_rgb1555=colors)
            lookup[key] = len(strips)
            strips.append(info)
        q['strip'] = lookup[key]
        left, right = (0, 3) if p[0][3] < p[2][3] else (3, 0)
        le, re = (1 if left == 0 else 2), (1 if right == 0 else 2)
        if p[left][4] > p[le][4]:
            left, le = le, left
        if p[right][4] > p[re][4]:
            right, re = re, right
        if p[le][4] <= p[left][4] or p[re][4] <= p[right][4]:
            raise ArtError('Unsupported source longitudinal UV')
        q.update(lo=min(p[left][4], p[right][4]), hi=max(p[le][4], p[re][4]),
                 unit=strips[q['strip']]['period']*32, edges=(left, le, right, re))
    return quads, strips


def texture_sizes(strips):
    """Sizing only: four base-height periodic images (1/2/4/8), plus mean row.

    Fog variants are explicitly separate. CLUT is lossless only when every
    stored tier fits 16 colors; the base count cannot certify filtered tiers.
    """
    tiers = {}
    for repeat in (1, 2, 4, 8):
        pixels = sum(s['width']*s['period'] for s in strips)
        tiers[str(repeat)] = dict(rgb1555=pixels*2, clut4_lower_bound=pixels//2+32*len(strips))
    mean = sum(s['width'] for s in strips)
    rgb = sum(s['rgb1555'] for s in tiers.values()) + mean*2
    clut = sum(s['clut4_lower_bound'] for s in tiers.values()) + mean//2+32*len(strips)
    return dict(tiers=tiers, mean_rgb1555=mean*2, rgb1555_total=rgb,
                rgb1555_three_fog_variants=rgb*3, clut4_total_lower_bound=clut,
                base_strips_lossless_clut16=sum(s['unique_rgb1555'] <= 16 for s in strips),
                strips=len(strips), filtered_palette_status='UNPROVEN; quantization/image gate required')


def far_chains(mesh, quads, length):
    """Topology candidates only. No curved-road removal credited in counts."""
    vertices = mesh['vertices']
    def edge(q, a, b):
        return tuple(sorted((tuple(vertices[q['vertices'][a]][:3]), tuple(vertices[q['vertices'][b]][:3]))))
    starts = {}
    for i, q in enumerate(quads):
        a, b, c, d = q['edges']
        key = (q['chunk'], q['material'], q['strip'], q['outer'], edge(q, a, c))
        starts.setdefault(key, []).append(i)
    chains = []
    for i, q in enumerate(quads):
        if q['primitive']:
            continue
        chain = [i]
        while len(chain) < length:
            last = quads[chain[-1]]
            a, b, c, d = last['edges']
            key = ((last['chunk']+1) % (len(mesh['chunks'])-1), last['material'], last['strip'], last['outer'], edge(last, b, d))
            found = starts.get(key, [])
            if len(found) != 1:
                break
            chain.append(found[0])
        if len(chain) == length:
            chains.append(chain)
    return chains


def disjoint_chains(chains, far):
    selected = set()
    groups = 0
    for chain in chains:
        if all(i in far and i not in selected for i in chain):
            selected.update(chain)
            groups += 1
    return groups


def percentile(values, fraction):
    return sorted(values)[max(0, math.ceil(len(values)*fraction)-1)]


def census(mesh, replay, reference, thresholds=(16, 8, 4), expected_views=9907):
    if len(replay) != RECORD_BYTES*expected_views or expected_views <= 0:
        raise ArtError('Replay size does not match the required view count')
    if len(reference) != 104*expected_views:
        raise ArtError('HOST reference size does not match replay')
    tier(0, thresholds)
    quads, strips = prepare(mesh)
    positions = np.array([p[:3] for p in mesh['vertices']], dtype=np.int64)
    ids = np.array([q['vertices'] for q in quads])
    by_chunk = [[i for i, q in enumerate(quads) if q['chunk'] == c] for c in range(len(mesh['chunks'])-1)]
    chains = {n: far_chains(mesh, quads, n) for n in (2, 4)}
    rows = []
    for frame in range(expected_views):
        record = replay[frame*RECORD_BYTES:(frame+1)*RECORD_BYTES]
        selected, outer = chunks(record[252:], len(by_chunk))
        p, xy, codes, safe, front, native = project(record[:252], positions)
        exact = reference[104*frame:104*(frame+1)]
        if native != exact[:80]:
            raise ArtError('Fixed-point view differs from frozen C reference at input %d' % (frame+1))
        native_counts = struct.unpack_from('<6I', exact, 80)
        active = np.array([i for c in selected for i in by_chunk[c] if outer or not quads[i]['outer']], dtype=int)
        corners = ids[active]
        on_screen = np.bitwise_and.reduce(codes[corners], axis=1) == 0
        projected = xy[corners]
        area = (projected[:, :, 0]*np.roll(projected[:, :, 1], -1, axis=1)-projected[:, :, 1]*np.roll(projected[:, :, 0], -1, axis=1)).sum(axis=1)
        in_front = front[corners].all(axis=1)
        active = active[on_screen & (~in_front | (area < 0))]
        counts = {1: 0, 2: 0, 4: 0, 0: 0}
        total = control = risky = 0
        far = set()
        for i in active:
            q = quads[i]
            v = ids[i]
            clip_bound = 1 if safe[v].all() else 4
            risky += clip_bound != 1
            if q['primitive']:
                total += clip_bound
                control += clip_bound
                continue
            a, b, c, d = q['edges']
            # Max L1 length over all parent corners is conservative relative to
            # either longitudinal edge. Near crossings force full repetition.
            screen_span = max(int(np.abs(xy[v[x]]-xy[v[y]]).sum()) for x in range(4) for y in range(x))
            pixels = screen_span*q['unit']/(q['hi']-q['lo'])
            t = tier(pixels, thresholds) if front[v].all() else 1
            count = 1 if t == 0 else pieces(q['lo'], q['hi'], q['unit']*t)
            total += count*clip_bound
            control += pieces(q['lo'], q['hi'], q['unit'])*clip_bound
            counts[t] += 1
            if t == 0:
                far.add(int(i))
        merge = {str(n): sum(all(i in far for i in chain) for chain in values) for n, values in chains.items()}
        disjoint = {str(n): disjoint_chains(values, far) for n, values in chains.items()}
        if native_counts[0] > total+1 or native_counts[1] > control+1:
            raise ArtError('Reference exceeds independent conservative bound at input %d' % (frame+1))
        rows.append(dict(input=frame+1, commands=native_counts[0], forced_first=native_counts[1],
                         native_tiers=list(native_counts[2:]), command_bound=total+1, forced_first_bound=control+1,
                         tiers={str(k): v for k, v in counts.items()}, clipped_parents=risky,
                         far_chain_candidates=merge, far_disjoint_groups=disjoint))
    def stats(key):
        values = [r[key] for r in rows]
        return dict(mean=sum(values)/len(values), p99=percentile(values, .99), maximum=max(values),
                    worst_input=values.index(max(values))+1)
    normal, control = stats('commands'), stats('forced_first')
    report = dict(tier='HOST', scope='counted geometry emission; no image, runtime or speed acceptance',
                  views=len(rows), thresholds=list(thresholds), source_vertices=len(positions), source_quads=len(quads),
                  commands=normal, forced_first=control, budget=420,
                  budget_pass=normal['p99'] <= 420, control_fails_budget=control['p99'] > 420,
                  conservative_bound=stats('command_bound'), fixed_views_match=True,
                  strips=strips, textures=texture_sizes(strips),
                  far_topology_candidates={str(n): len(v) for n, v in chains.items()},
                  far_merges={str(n): dict(max_groups=max(r['far_disjoint_groups'][str(n)] for r in rows),
                                          mean_groups=sum(r['far_disjoint_groups'][str(n)] for r in rows)/len(rows),
                                          max_quads_replaced=n*max(r['far_disjoint_groups'][str(n)] for r in rows)) for n in chains},
                  far_merge_scope='topology candidates only; no command reduction credited; intermediate curvature/image gate still required')
    return report, rows


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--source', type=Path, default=ROOT/'generated/road/road.json')
    ap.add_argument('--replay', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--thresholds', type=float, nargs=3, default=(16, 8, 4))
    ap.add_argument('--cc', default='gcc')
    args = ap.parse_args()
    output_location(args.output)
    for receipt in (args.source.parent/'road.receipt', ROOT/'generated/road_pack/road_pack.receipt',
                    ROOT/'generated/road_tiles/road_tiles.receipt'):
        subprocess.run(['bash', str(ROOT/'tools/check_receipt.sh'), str(receipt)], check=True)
    manifest = json.loads((ROOT/'generated/road_pack/manifest.json').read_text())
    if manifest['source_sha256'] != hashlib.sha256(args.source.read_bytes()).hexdigest():
        raise ArtError('Census JSON and admitted native road pack are not the same source')
    args.output.mkdir(parents=True, exist_ok=True)
    reference = run_reference(args.replay, args.output, args.thresholds, args.cc)
    report, rows = census(json.loads(args.source.read_text()), args.replay.read_bytes(), reference, args.thresholds)
    report['bindings'] = {k: hashlib.sha256(p.read_bytes()).hexdigest() for k, p in
                          [('source', args.source), ('replay', args.replay), ('census_tool', Path(__file__)), ('strip_tool', ROOT/'tools/road_tiles.py')]}
    (args.output/'summary.json').write_text(json.dumps(report, indent=2)+'\n')
    (args.output/'views.json').write_text(json.dumps(rows)+'\n')
    print(json.dumps({k: v for k, v in report.items() if k not in ('strips', 'bindings')}, indent=2))


if __name__ == '__main__':
    main()
