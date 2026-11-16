#!/usr/bin/env python3
"""Check native adapter readback against source matrices in local replay evidence.

The single-player Time Attack samples use Camera_Draw's ordinary draw scissor.
This checks numeric conversion; it is not source-game visual acceptance.
"""
import argparse
import json
from pathlib import Path
import struct


def f32(x):
    return struct.unpack('>f', struct.pack('>f', x))[0]


def expected(camera_hex):
    data = bytes.fromhex(camera_hex)
    if len(data) != 252:
        raise ValueError('source camera length')
    values = struct.unpack('>63f', data)
    view = [values[30 + i*4:34 + i*4] for i in range(4)]
    projection = [values[46 + i*4:50 + i*4] for i in range(4)]
    sx, sy, tx, ty = values[22:26]
    rotation = [int(view[col][row] * (-1 if row == 2 else 1) * 2**30)
                for row in range(3) for col in range(3)]
    translation = [int(view[3][row] * (-1 if row == 2 else 1) * 4096)
                   for row in range(3)]
    focal = [int(f32(sx*projection[0][0])*65536), int(f32(sy*projection[1][1])*65536)]
    center = [int(f32(tx-f32(sx*projection[2][0]))*65536),
              int(f32(f32(ty-8)+f32(sy*projection[2][1]))*65536)]
    near_far = [int(values[20]*4096), int(values[21]*4096)]
    return struct.pack('>18i4h', *rotation, *translation, *focal, *center, *near_far, 12, 8, 308, 216)


def check(rows):
    if isinstance(rows, dict):
        rows = [rows]
    if not rows:
        raise ValueError('no camera samples')
    for row in rows:
        if row['status'] != 1 or row['overflows'] != 0:
            raise ValueError('camera renderer fault')
        if bytes.fromhex(row['view_hex']) != expected(row['camera_hex']):
            raise ValueError(f"native camera conversion differs at tick {row['tick']}")
    return len(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('samples', type=Path)
    args = parser.parse_args()
    rows = json.loads(args.samples.read_text(encoding='utf-8-sig'))
    count = check(rows)
    # A wrong native focal length must fail this same comparison.
    control = json.loads(json.dumps(rows))
    if isinstance(control, dict):
        control = [control]
    raw = bytearray.fromhex(control[0]['view_hex'])
    raw[51] ^= 1
    control[0]['view_hex'] = raw.hex()
    try:
        check(control)
    except ValueError:
        pass
    else:
        raise ValueError('wrong focal control accepted')
    print(f'Native camera readback PASS: {count} exact matrices/projections/scissors; control rejected')


if __name__ == '__main__':
    main()
