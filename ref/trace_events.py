#!/usr/bin/env python3
"""Summarise the events a reference trace reaches (card G1.2 coverage check).

Usage: trace_events.py TRACE.tsv   Prints laps, finish, boost, dash plate, pit recharge,
wall hits, retirement and the tick each first happened. Reads only the trace.
"""
import struct
import sys

FLAGS = {"wall_hit": 1 << 13, "spin_out": 1 << 14, "damage": 1 << 17, "retired": 1 << 18,
         "can_boost": 1 << 20, "dash_plate": 1 << 24, "finished": 1 << 25,
         "airborne": 1 << 26, "crashed": 1 << 27}


def f32(h: str) -> float:
    return struct.unpack(">f", bytes.fromhex(h))[0]


def events(path: str) -> dict:
    rows = [line.rstrip("\n").split("\t") for line in open(path) if not line.startswith("#")]
    header, rows = rows[0], [dict(zip(rows[0], r)) for r in rows[1:]]
    first: dict[str, int] = {}
    previous_energy = None
    for r in rows:
        tick, flags, energy = int(r["tick"]), int(r["state_flags"], 16), f32(r["energy"])
        for name, bit in FLAGS.items():
            if flags & bit:
                first.setdefault(name, tick)
        if previous_energy is not None and energy > previous_energy + 0.01:
            first.setdefault("energy_regain", tick)
        if previous_energy is not None and energy < previous_energy - 0.01:
            first.setdefault("energy_loss", tick)
        previous_energy = energy
        laps = int(r["laps_completed"])
        first.setdefault(f"laps_completed_{laps}", tick)
    first["ticks"] = len(rows)
    return first


if __name__ == "__main__":
    for name, tick in sorted(events(sys.argv[1]).items(), key=lambda kv: kv[1]):
        print(f"{name}: {tick}")
