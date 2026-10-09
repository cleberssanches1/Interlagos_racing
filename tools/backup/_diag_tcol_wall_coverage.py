#!/usr/bin/env python3
"""Diagnose TCOL wall coverage vs asphalt: Y overlap, lateral vs mid, seam caps."""
from __future__ import annotations

import math
import struct
from pathlib import Path

d = Path("cd/data/TCOL.BIN").read_bytes()
sc = struct.unpack_from("<H", d, 20)[0]
dir_off = struct.unpack_from("<I", d, 30)[0]
GROUND = struct.Struct("<HHHHHBBHH")


def read_seg(i: int) -> dict:
    o = dir_off + i * 56
    sid, vc, gc, wc, _gdim = struct.unpack_from("<HHHHH", d, o)
    minx, maxx, minz, maxz = struct.unpack_from("<iiii", d, o + 12)
    voff, goff, woff = struct.unpack_from("<III", d, o + 28)
    return {
        "sid": sid,
        "vc": vc,
        "gc": gc,
        "wc": wc,
        "minx": minx,
        "maxx": maxx,
        "minz": minz,
        "maxz": maxz,
        "woff": woff,
        "goff": goff,
        "voff": voff,
    }


def asphalt_stats(seg: dict) -> tuple[tuple[int, int, int, int] | None, tuple[float, float] | None, tuple[float, float] | None]:
    xs: list[int] = []
    zs: list[int] = []
    ys: list[int] = []
    for gi in range(seg["gc"]):
        i0, i1, i2, i3, _fam, st, _p, _src, _p2 = GROUND.unpack_from(
            d, seg["goff"] + gi * GROUND.size
        )
        if st != 1:
            continue
        for idx in (i0, i1, i2, i3):
            if idx >= seg["vc"]:
                continue
            x, y, z = struct.unpack_from("<iii", d, seg["voff"] + idx * 12)
            xs.append(x)
            ys.append(y)
            zs.append(z)
    if not xs:
        return None, None, None
    bounds = (min(xs), max(xs), min(zs), max(zs))
    center = (sum(xs) / len(xs), sum(zs) / len(zs))
    y_range = (min(ys) / 65536.0, max(ys) / 65536.0)
    return bounds, center, y_range


def walls_for(seg: dict):
    out = []
    for wi in range(seg["wc"]):
        o = seg["woff"] + wi * 36
        ax, az, bx, bz, miny, maxy, nx, nz, fam, src = struct.unpack_from(
            "<iiiiiiiiHH", d, o
        )
        out.append((ax, az, bx, bz, miny, maxy, nx, nz, fam, src))
    return out


def closest(ax, az, bx, bz, cx, cz):
    abx, abz = bx - ax, bz - az
    ab2 = abx * abx + abz * abz
    if ab2 <= 0:
        return math.hypot(cx - ax, cz - az), ax, az
    t = max(0.0, min(1.0, ((cx - ax) * abx + (cz - az) * abz) / ab2))
    qx, qz = ax + t * abx, az + t * abz
    return math.hypot(cx - qx, cz - qz), qx, qz


zero = 0
y_dead = 0
for i in range(min(sc, 40)):
    seg = read_seg(i)
    bounds, center, y_range = asphalt_stats(seg)
    if bounds is None or center is None or y_range is None:
        print(f"SEG {seg['sid']}: no asphalt")
        continue
    aminx, amaxx, aminz, amaxz = bounds
    xspan = max(1, amaxx - aminx)
    zspan = max(1, amaxz - aminz)
    travel_z = zspan <= xspan
    cross = xspan if travel_z else zspan
    lateral_band = max(96 << 16, int(0.30 * cross))
    print(
        f"SEG {seg['sid']} walls={seg['wc']} asphaltY=[{y_range[0]:.1f},{y_range[1]:.1f}] "
        f"travel={'Z' if travel_z else 'X'} lateralBandU={lateral_band/65536:.1f}"
    )
    if seg["wc"] == 0:
        zero += 1
        print("  (no walls)")
        continue
    cx, cz = center
    for wi, (ax, az, bx, bz, miny, maxy, nx, nz, fam, src) in enumerate(walls_for(seg)):
        dist, qx, qz = closest(ax, az, bx, bz, cx, cz)
        if travel_z:
            rim = min(qx - aminx, amaxx - qx)
        else:
            rim = min(qz - aminz, amaxz - qz)
        y0, y1 = miny / 65536.0, maxy / 65536.0
        y_ok = not (maxy < (y_range[0] - 12) * 65536 or miny > (y_range[1] + 12) * 65536)
        if not y_ok:
            y_dead += 1
        tags = []
        if dist < 48 * 65536:
            tags.append("MID")
        if rim > lateral_band and aminx <= qx <= amaxx and aminz <= qz <= amaxz:
            tags.append("INTERIOR")
        if rim <= lateral_band:
            tags.append("LATERAL")
        if not y_ok:
            tags.append("Y_DEAD")
        print(
            f"  w{wi} fam={fam} distU={dist/65536:.1f} rimU={rim/65536:.1f} "
            f"Y=[{y0:.1f},{y1:.1f}] {' '.join(tags) or 'OK'}"
        )

print(f"segs with 0 walls (first 40): {zero}")
print(f"Y_DEAD wall entries (first 40 segs): {y_dead}")
