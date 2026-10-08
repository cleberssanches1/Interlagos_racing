#!/usr/bin/env python3
"""List TCOL walls that look like seam-caps crossing driveable asphalt."""
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
    sid, vc, gc, wc, gdim = struct.unpack_from("<HHHHH", d, o)
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


def walls_for(seg: dict):
    out = []
    for wi in range(seg["wc"]):
        o = seg["woff"] + wi * 36
        ax, az, bx, bz, miny, maxy, nx, nz, fam, src = struct.unpack_from(
            "<iiiiiiiiHH", d, o
        )
        out.append((ax, az, bx, bz, miny, maxy, nx, nz, fam, src))
    return out


def asphalt_bounds(seg: dict) -> tuple[int, int, int, int] | None:
    xs: list[int] = []
    zs: list[int] = []
    for gi in range(seg["gc"]):
        i0, i1, i2, i3, _fam, st, _p, _src, _p2 = GROUND.unpack_from(
            d, seg["goff"] + gi * GROUND.size
        )
        if st != 1:
            continue
        for idx in (i0, i1, i2, i3):
            if idx >= seg["vc"]:
                continue
            x, _y, z = struct.unpack_from("<iii", d, seg["voff"] + idx * 12)
            xs.append(x)
            zs.append(z)
    if not xs:
        for i in range(seg["vc"]):
            x, _y, z = struct.unpack_from("<iii", d, seg["voff"] + i * 12)
            xs.append(x)
            zs.append(z)
    if not xs:
        return None
    return min(xs), max(xs), min(zs), max(zs)


def point_in_expanded_aabb(
    x: float, z: float, bounds: tuple[int, int, int, int], pad: int = 0
) -> bool:
    minx, maxx, minz, maxz = bounds
    return (minx - pad) <= x <= (maxx + pad) and (minz - pad) <= z <= (maxz + pad)


suspect = 0
for i in range(min(12, sc)):
    seg = read_seg(i)
    bounds = asphalt_bounds(seg)
    if bounds is None:
        continue
    aminx, amaxx, aminz, amaxz = bounds
    cx = (aminx + amaxx) * 0.5
    cz = (aminz + amaxz) * 0.5
    zspan = max(1, amaxz - aminz)
    xspan = max(1, amaxx - aminx)
    print(
        f"SEG {seg['sid']} asphalt=({aminx},{amaxx},{aminz},{amaxz}) "
        f"walls={seg['wc']}"
    )
    for wi, (ax, az, bx, bz, miny, maxy, nx, nz, fam, src) in enumerate(walls_for(seg)):
        abx = bx - ax
        abz = bz - az
        length = math.hypot(abx, abz)
        ab2 = abx * abx + abz * abz
        t = 0.0 if ab2 == 0 else max(0.0, min(1.0, ((cx - ax) * abx + (cz - az) * abz) / ab2))
        qx = ax + t * abx
        qz = az + t * abz
        dist = math.hypot(cx - qx, cz - qz)
        near_zmin = min(az, bz) <= aminz + int(0.08 * zspan)
        near_zmax = max(az, bz) >= amaxz - int(0.08 * zspan)
        near_xmin = min(ax, bx) <= aminx + int(0.08 * xspan)
        near_xmax = max(ax, bx) >= amaxx - int(0.08 * xspan)
        # Travel early segs is mostly ±Z; transverse ≈ dominant X span.
        transverse_z = abs(abx) > abs(abz) * 1.2
        # Travel later may be ±X; transverse ≈ dominant Z span.
        transverse_x = abs(abz) > abs(abx) * 1.2
        samples = (
            (ax, az),
            (bx, bz),
            ((ax + bx) * 0.5, (az + bz) * 0.5),
        )
        samples_on_asphalt = sum(
            1 for x, z in samples if point_in_expanded_aabb(x, z, bounds)
        )
        # Wall crosses asphalt interior if closest point on wall to centroid
        # lies inside asphalt AABB (not just near outer rim).
        closest_inside = point_in_expanded_aabb(qx, qz, bounds)
        # Lateral rim: closest point near left/right border.
        rim_x = min(qx - aminx, amaxx - qx)
        rim_z = min(qz - aminz, amaxz - qz)
        on_lateral_rim = rim_x <= int(0.12 * xspan)
        on_end_rim = rim_z <= int(0.12 * zspan)

        flags: list[str] = []
        if (near_zmin or near_zmax) and transverse_z:
            flags.append("SEAM_TRANS_Z")
        if (near_xmin or near_xmax) and transverse_x:
            flags.append("SEAM_TRANS_X")
        if samples_on_asphalt >= 2:
            flags.append(f"SAMP_ASPHALT{samples_on_asphalt}")
        if closest_inside and not on_lateral_rim:
            flags.append("CROSS_INTERIOR")
        if dist < 80 * 65536:
            flags.append(f"NEAR{dist / 65536:.0f}")

        if not flags:
            continue
        suspect += 1
        print(
            f"  w{wi} fam={fam} distU={dist / 65536:.1f} lenU={length / 65536:.1f} "
            f"rimX={rim_x / 65536:.1f} rimZ={rim_z / 65536:.1f} "
            f"{' '.join(flags)}"
        )

print(f"suspect walls in first 12 segs: {suspect}")
