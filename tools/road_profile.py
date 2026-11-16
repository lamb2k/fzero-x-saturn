"""Validate exclusive road CPU samples against an independent video-field clock."""
import struct

PHASES = ('base', 'transform', 'project', 'cull', 'clip', 'split', 'material', 'emit', 'hash')


def decode_sample(row):
    words = struct.unpack('>26I', bytes.fromhex(row['profile_hex']))
    magic, total, transitions, maximum, phase, depth, faults, active = words[:8]
    ticks, calls = words[8:17], words[17:26]
    if magic != 0x52504631 or faults or active or depth or phase:
        raise ValueError('Incomplete or faulty road profile')
    if total != sum(ticks) or not transitions or maximum > 65535 or not calls[0]:
        raise ValueError('Invalid exclusive partition')
    field = row['field_ticks']
    fields = row['profile_outer_fields']
    overhead = (row['profile_outer_ticks'] - total) & 65535
    if overhead >= field:
        raise ValueError('Road profile disagrees with paired outer FRT interval')
    outer_total = total + overhead
    if not 0 < field < 32768:
        raise ValueError('Missing independent field clock')
    # Paired FRT endpoints lie inside their recorded video fields. This interval
    # is narrower than one FRT wrap, so losing 65536 ticks cannot pass.
    if not max(0, fields - 1) * field <= outer_total <= (fields + 1) * field:
        raise ValueError('Road profile disagrees with video fields (possible lost wrap)')
    scale = 1000 / (60 * field)
    return dict(tick=row['tick'], total_ticks=total, modelled_ms=total*scale,
                transitions=transitions, max_interval=maximum, outer_ticks=overhead,
                phases={name: dict(ticks=t, calls=c, modelled_ms=t*scale)
                        for name, t, c in zip(PHASES, ticks, calls)})
