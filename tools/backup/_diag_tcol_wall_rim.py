#!/usr/bin/env python3
"""Baseline: TCOL wall distance to asphalt centroid vs lateral rim (Fase 0 / 112009)."""
from __future__ import annotations

import math
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TCOL = ROOT / "cd" / "data" / "TCOL.BIN"

HEADER = struct.Struct("<4sHHHHHHHHHIII")
DIRECTORY = struct.Struct("<HHHHHHiiiiIIIIIII")
VERTEX = struct.Struct("<iii")
GROUND = struct.Struct("<HHHHHBBHH")
WALL = struct.Struct("<iiiiiiiiHH")


def closest(ax, az, bx, bz, px, pz):
    abx, abz = bx - ax, bz - az
    apx, apz = px - ax, pz - az
    ab2 = abx * abx + abz * abz
    if ab2 <= 0:
        return math.hypot(apx, apz)
    t = max(0.0, min(1.0, (apx * abx + apz * abz) / ab2))
    return math.hypot(ax + t * abx - px, az + t * abz - pz)


def main() -> int:
    blob = TCOL.read_bytes()
    magic, ver, *_rest = HEADER.unpack_from(blob)
    assert magic == b"TCL1", magic
    (
        _m,
        _v,
        _hs,
        _ds,
        _vs,
        _gs,
        _ws,
        _cs,
        grid,
        nseg,
        tg,
        tw,
        doff,
    ) = HEADER.unpack_from(blob)
    print(f"TCL1 walls={tw} ground={tg} segs={nseg} grid={grid}")
    print(
        f"{'SEG':>5} {'nw':>3} {'near_c':>8} {'near_rim':>8} "
        f"{'rim<40':>6} {'mid<48':>6} {'class'}"
    )

    want = set(range(1, 11)) | {26, 27, 28, 29, 30}
    for i in range(nseg):
        (
            sid,
            nverts,
            nground,
            nwalls,
            _gdim,
            _pad,
            min_x,
            max_x,
            min_z,
            max_z,
            voff,
            goff,
            woff,
            *_r,
        ) = DIRECTORY.unpack_from(blob, doff + i * DIRECTORY.size)
        if sid not in want:
            continue
        verts = [VERTEX.unpack_from(blob, voff + j * VERTEX.size) for j in range(nverts)]
        grounds = [GROUND.unpack_from(blob, goff + j * GROUND.size) for j in range(nground)]
        walls = [WALL.unpack_from(blob, woff + j * WALL.size) for j in range(nwalls)]
        asphalt = []
        for i0, i1, i2, i3, _fam, stype, nidx, _fl, _src in grounds:
            if stype != 1:
                continue
            for ix in (i0, i1, i2, i3)[:nidx]:
                asphalt.append(verts[ix])
        if not asphalt:
            print(f"{sid:5d} {nwalls:3d}  (no asphalt)")
            continue
        cx = sum(v[0] for v in asphalt) / len(asphalt)
        cz = sum(v[2] for v in asphalt) / len(asphalt)
        xs = [v[0] for v in asphalt]
        zs = [v[2] for v in asphalt]
        xspan = max(xs) - min(xs)
        zspan = max(zs) - min(zs)
        travel_z = zspan <= xspan
        # Lateral rim = outer 30% of across AABB of type1 verts.
        if travel_z:
            across_min, across_max = min(xs), max(xs)
        else:
            across_min, across_max = min(zs), max(zs)
        span = max(1, across_max - across_min)
        rim_band = 0.30 * span

        near_c = 1e99
        near_rim = 1e99
        rim_close = 0
        mid = 0
        for ax, az, bx, bz, *_restw in walls:
            d_c = closest(float(ax), float(az), float(bx), float(bz), cx, cz)
            near_c = min(near_c, d_c)
            mx = (ax + bx) * 0.5
            mz = (az + bz) * 0.5
            across = mx if travel_z else mz
            dist_rim = min(across - across_min, across_max - across)
            # distance of midpoint to nearest lateral AABB edge
            near_rim = min(near_rim, dist_rim)
            if dist_rim <= rim_band:
                if d_c / 65536.0 < 40.0:
                    rim_close += 1
            if d_c < (48 << 16):
                mid += 1
        cls = "VOID_FAR"
        if near_c / 65536.0 < 48:
            cls = "MID_LANE"
        elif near_rim / 65536.0 < 40:
            cls = "ON_RIM"
        elif near_c / 65536.0 < 180:
            cls = "NEAR_STRIP"
        print(
            f"{sid:5d} {nwalls:3d} {near_c/65536:8.1f} {near_rim/65536:8.1f} "
            f"{rim_close:6d} {mid:6d} {cls}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
