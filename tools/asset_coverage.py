#!/usr/bin/env python3
"""Source declaration census, not proof of conversion or native coverage.

No ROM is read. Detailed inventories stay in private/ignored output directories.
Every row starts unqualified; declarations and consumers are separate evidence.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re

from import_source_art import DECOMP, YAML, metadata, output_location, verify_source


def census(root):
    assets, inputs = [], {}
    for path in sorted((root / YAML).glob('*.yaml')):
        relative = path.relative_to(root).as_posix()
        inputs[relative] = hashlib.sha256(path.read_bytes()).hexdigest()
        for symbol, row in metadata(path.read_text()).items():
            if row.get('type') not in ('VTX', 'GFX', 'TEXTURE', 'COMPRESSED_TEXTURE'):
                continue
            assets.append(dict(id=path.stem + ':' + symbol, source=relative,
                               symbol=symbol, kind=row['type'],
                               status='unqualified', evidence=[]))
    racer = root / 'src/game/racer.c'
    source = racer.read_text()
    inputs['src/game/racer.c'] = hashlib.sha256(racer.read_bytes()).hexdigest()
    table = re.search(r'sMachineDrawFuncs\[\]\[5\]\)\(Gfx\*\) = \{(.*?)\n\};', source, re.S)
    if not table:
        raise ValueError('Missing original machine draw table')
    machines = re.findall(r'\{\s*(Machine_Draw\w+Lod1)\s*,(.*?)\}\s*,?\s*//\s*([^\r\n]+)', table[1], re.S)
    if len(machines) != 33:
        raise ValueError('Expected 30 stock and three extra source machine families')
    models = []
    for first, rest, character in machines:
        functions = [first] + re.findall(r'Machine_Draw\w+Lod\d+', rest)
        if len(functions) != 5 or [int(f[-1]) for f in functions] != list(range(1, 6)):
            raise ValueError('Missing or reordered machine LOD')
        models.extend(dict(id=f, character=character, lod=i + 1,
                           status='unqualified', evidence=[]) for i, f in enumerate(functions))
    if not assets or len({a['id'] for a in assets}) != len(assets):
        raise ValueError('Empty or duplicate source asset inventory')
    title = root / 'src/overlays/ovl_i4/title.c'
    inputs[title.relative_to(root).as_posix()] = hashlib.sha256(title.read_bytes()).hexdigest()
    title_symbols = sorted(set(re.findall(r'\baTitle\w+Tex\b', title.read_text())))
    return dict(schema=1, product='F Zero S', tier='SOURCE_CENSUS', inputs=inputs,
                counts=dict(Counter(a['kind'] for a in assets)), assets=assets,
                machine_lods=models, title_symbols=title_symbols,
                guests=[dict(id=n, status='native_integration_pending') for n in
                        ('metal_sonic', 'dr_robotnik_eggmobile')],
                limitations=['Declarations are not unique meshes or proof of runtime reachability.',
                             'Procedural course geometry, runtime effects and conditional content require consumer review.',
                             'No declaration is automatically promoted to converted or native-qualified.'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output_location(args.output)
    verify_source(DECOMP)
    result = census(DECOMP)
    args.output.mkdir(parents=True, exist_ok=True)
    data = (json.dumps(result, indent=2) + '\n').encode()
    (args.output / 'coverage.json').write_bytes(data)
    receipt = dict(schema=1, generator_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                   inputs=result['inputs'], outputs={'coverage.json': hashlib.sha256(data).hexdigest()})
    (args.output / 'coverage.receipt').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps(dict(counts=result['counts'], machine_lods=len(result['machine_lods']),
                          title_symbols=result['title_symbols'], qualified=False)))


if __name__ == '__main__':
    main()
