"""The one menu-table layout in reserved low work RAM (FZX_MENU_TABLES_LOW=1).

Shared by tools/menu_tables_assets.py (pack, identity header, receipt) and
ref/sat/gen_sat.py (stand-in base). MENUTAB.PAK is the byte image of
BASE..BASE+pack_bytes: a 64-byte guard slot before each table and after the
last, each slot holding PATTERN x8 then 32 zero bytes, and each table
zero-padded to a 64-byte multiple. The spare bytes up to BASE+RESERVED are not
in the pack; the boot loader zeroes them. The stand-ins start at
STAND_IN_BASE.
"""

BASE = 0x00200000
RESERVED = 0xB000
STAND_IN_BASE = BASE + RESERVED
SLOT = 64
ALIGN = 64
PATTERN = 0x4D544731  # 'MTG1'
PATTERN_WORDS = 8
# Order is the layout order. (identity key, generated symbol)
TABLES = (
    ('MACHINE', 'fzx_machine_menu_images'),
    ('FINISH', 'fzx_finish_menu_images'),
    ('RECORDS', 'fzx_records_images'),
    ('COURSE', 'fzx_course_menu_images'),
)


class LayoutError(ValueError):
    pass


def padded(n):
    return (n + ALIGN - 1) // ALIGN * ALIGN


def guard_slot():
    return PATTERN.to_bytes(4, 'big') * PATTERN_WORDS + bytes(SLOT - 4 * PATTERN_WORDS)


def layout(sizes):
    """sizes: {symbol: payload bytes}. Returns offsets/addresses; raises LayoutError."""
    if set(sizes) != {s for _, s in TABLES}:
        raise LayoutError('menu-table set differs from the layout')
    at, guards, tables = 0, [], []
    for key, symbol in TABLES:
        n = sizes[symbol]
        if n <= 0 or n % 4:
            raise LayoutError('%s: size %d is not a positive multiple of 4' % (symbol, n))
        guards.append(at)
        at += SLOT
        if at % ALIGN:
            raise LayoutError('%s: start 0x%x is not %d-byte aligned' % (symbol, at, ALIGN))
        tables.append(dict(key=key, symbol=symbol, offset=at, address=BASE + at, bytes=n, padded=padded(n)))
        at += padded(n)
    guards.append(at)
    at += SLOT
    if BASE + at > STAND_IN_BASE:
        raise LayoutError('menu tables need 0x%x bytes, reserved 0x%x' % (at, RESERVED))
    if STAND_IN_BASE != BASE + RESERVED or STAND_IN_BASE % ALIGN:
        raise LayoutError('stand-in base is not the end of the reservation')
    return dict(base=BASE, reserved=RESERVED, stand_in_base=STAND_IN_BASE, guards=guards, tables=tables,
                pack_bytes=at, spare_offset=at, spare_bytes=RESERVED - at)


def build_pack(payloads, lay):
    """payloads: {symbol: bytes}. Returns the MENUTAB.PAK bytes for layout lay."""
    out = bytearray()
    for (key, symbol), table in zip(TABLES, lay['tables']):
        out += guard_slot()
        data = payloads[symbol]
        if len(data) != table['bytes'] or len(out) != table['offset']:
            raise LayoutError('%s payload does not match its slice' % symbol)
        out += data + bytes(table['padded'] - len(data))
    out += guard_slot()
    if len(out) != lay['pack_bytes']:
        raise LayoutError('pack length differs from the layout')
    return bytes(out)


def fnv(data):
    h = 2166136261
    for b in data:
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h
