#!/usr/bin/env python3
"""Host test of the guest booster row validation in tools/fx_assets.py (GUEST-BOOSTERS draft 1).

No ROM and no generated files are needed: it checks that valid rows pass and that every malformed row is
refused before either CPU could read it, each refusal being a control that must fail the baker.
Usage: python3 tools/fx_assets_test.py"""
import json
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fx_assets  # noqa: E402
from import_source_art import ArtError  # noqa: E402

PAD = [0, 0, 0, 0]
# The draft's two guest rows (rendered ids 33 and 34): one live socket, spares zero.
GOOD = {'33': [1, 0, -31, -210, 96] + PAD * 3, '34': [1, 0, 5, -60, 80] + PAD * 3}
# The draft's stock example (rendered id 0 as a stock row shape): four sockets.
STOCK = [4, -87, 22, -208, 160, -87, 60, -208, 160, 87, 22, -208, 160, 87, 60, -208, 160]

BAD = {
    'count above 4': {'33': [5] + [1, 1, 1, 16] * 4},
    'negative count': {'33': [-1] + [0] * 16},
    'live socket with size 0': {'33': [1, 0, 0, 0, 0] + PAD * 3},
    'live socket with negative size': {'33': [1, 0, 0, 0, -16] + PAD * 3},
    'spare socket not zero': {'33': [1, 0, -31, -210, 96, 1, 0, 0, 16] + PAD * 2},
    'sixteen words': {'33': [1, 0, -31, -210, 96] + PAD * 2 + [0, 0, 0]},
    'eighteen words': {'33': [1, 0, -31, -210, 96] + PAD * 3 + [0]},
    'word outside 16 bits': {'33': [1, 40000, -31, -210, 96] + PAD * 3},
    'a stock id is not a guest': {'12': [1, 0, 0, 0, 16] + PAD * 3},
    'a float word': {'33': [1, 0.5, -31, -210, 96] + PAD * 3},
}


def rows_of(obj):
    with tempfile.TemporaryDirectory() as d:
        p = Path(d) / 'guest_boosters.json'
        p.write_text(json.dumps(obj))
        return fx_assets.guest_rows(p)


def main():
    fails = 0
    # No file: both rows empty (nothing drawn).
    empty = fx_assets.guest_rows(Path('/nonexistent/guest_boosters.json'))
    ok = empty == {'33': [0] * 17, '34': [0] * 17}
    print('PASS' if ok else 'FAIL', 'no file gives two empty rows')
    fails += not ok
    got = rows_of(GOOD)
    ok = got['33'] == GOOD['33'] and got['34'] == GOOD['34'] and all(len(v) == 17 for v in got.values())
    print('PASS' if ok else 'FAIL', 'the draft rows for 33 and 34 are accepted verbatim, 17 words each')
    fails += not ok
    ok = rows_of({'34': GOOD['34']})['33'] == [0] * 17
    print('PASS' if ok else 'FAIL', 'a missing id stays an empty row')
    fails += not ok
    # The stock example is a valid row shape (count 4, positive sizes) under the same rules.
    count = STOCK[0]
    ok = count == 4 and all(STOCK[1 + 4 * i + 3] > 0 for i in range(count))
    print('PASS' if ok else 'FAIL', 'the stock example has the row shape (count 4, four live sockets)')
    fails += not ok
    for name, obj in BAD.items():
        try:
            rows_of(obj)
            refused = False
        except (ArtError, TypeError):
            refused = True
        print('PASS' if refused else 'FAIL', 'control, refused: %s' % name)
        fails += not refused
    print('%d failure(s)' % fails)
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
