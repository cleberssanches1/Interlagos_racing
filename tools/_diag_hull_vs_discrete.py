#!/usr/bin/env python3
"""Offline: discrete ±halfWidth probes vs lateral hull segment vs TCOL walls.

Validates F2 (130219): at runoff L≈22..38u toward a ~41u wall, discrete misses
while left↔right hull (r=2) hits. Default segs 5/7/9 (video start straight).
"""
from __future__ import annotations

import math
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TCOL = ROOT / "cd" / "data" / "TCOL.BIN"
HEADER = struct.Struct("<4sHHHHHHHHHIII")
DIRECTORY = struct.Struct("<HHHHHHiiiiIIIIIII")
VERTEX = struct.Struct("<iii")
GROUND = struct.Struct("<HHHHHBBHH")
WALL = struct.Struct("<iiiiiiiiHH")

HALF_W = 22.4
RADIUS = 2.0


def closest_point_on_seg(ax, az, bx, bz, px, pz):
    abx, abz = bx - ax, bz - az
    apx, apz = px - ax, pz - az
    ab2 = abx * abx + abz * abz
    if ab2 <= 0:
        return px, pz, math.hypot(apx, apz)
    t = max(0.0, min(1.0, (apx * abx + apz * abz) / ab2))
    cx, cz = ax + t * abx, az + t * abz
    return cx, cz, math.hypot(px - cx, pz - cz)


def seg_seg_dist(ax, az, bx, bz, cx, cz, dx, dz):
    d1 = closest_point_on_seg(ax, az, bx, bz, cx, cz)[2]
    d2 = closest_point_on_seg(ax, az, bx, bz, dx, dz)[2]
    d3 = closest_point_on_seg(cx, cz, dx, dz, ax, az)[2]
    d4 = closest_point_on_seg(cx, cz, dx, dz, bx, bz)[2]

    def orient(px, pz, qx, qz, rx, rz):
        return (qx - px) * (rz - pz) - (qz - pz) * (rx - px)

    o1 = orient(ax, az, bx, bz, cx, cz)
    o2 = orient(ax, az, bx, bz, dx, dz)
    o3 = orient(cx, cz, dx, dz, ax, az)
    o4 = orient(cx, cz, dx, dz, bx, bz)
    crosses = ((o1 > 0 and o2 < 0) or (o1 < 0 and o2 > 0)) and (
        (o3 > 0 and o4 < 0) or (o3 < 0 and o4 > 0)
    )
    if crosses:
        return 0.0
    return min(d1, d2, d3, d4)


def main() -> int:
    seg_ids = tuple(int(x) for x in sys.argv[1:]) if len(sys.argv) > 1 else (5, 7, 9)
    blob = TCOL.read_bytes()
    (_m, _v, _hs, _ds, _vs, _gs, _ws, _cs, grid, nseg, tg, tw, doff) = HEADER.unpack_from(
        blob
    )
    print(f"TCL1 walls={tw} halfW={HALF_W} r={RADIUS} segs={seg_ids}")
    print("Hit when dist < r. Discrete=point probes; Hull=left-right segment.\n")

    for want in seg_ids:
        for i in range(nseg):
            (
                sid,
                nverts,
                nground,
                nwalls,
                _g,
                _p,
                minx,
                maxx,
                minz,
                maxz,
                voff,
                goff,
                woff,
                *_,
            ) = DIRECTORY.unpack_from(blob, doff + i * DIRECTORY.size)
            if sid != want:
                continue
            verts = [
                VERTEX.unpack_from(blob, voff + j * VERTEX.size) for j in range(nverts)
            ]
            grounds = [
                GROUND.unpack_from(blob, goff + j * GROUND.size) for j in range(nground)
            ]
            walls_raw = [
                WALL.unpack_from(blob, woff + j * WALL.size) for j in range(nwalls)
            ]
            asphalt = []
            for i0, i1, i2, i3, _fam, stype, nidx, _fl, _src in grounds:
                if stype != 1:
                    continue
                for ix in (i0, i1, i2, i3)[:nidx]:
                    asphalt.append(verts[ix])
            if not asphalt:
                print(f"SEG{sid}: no asphalt")
                continue
            cx = sum(v[0] for v in asphalt) / len(asphalt) / 65536.0
            cz = sum(v[2] for v in asphalt) / len(asphalt) / 65536.0
            xs = [v[0] / 65536.0 for v in asphalt]
            zs = [v[2] / 65536.0 for v in asphalt]
            travel_z = (max(zs) - min(zs)) <= (max(xs) - min(xs))
            walls = [
                (ax / 65536.0, az / 65536.0, bx / 65536.0, bz / 65536.0, fam)
                for ax, az, bx, bz, miny, maxy, nx, nz, fam, src in walls_raw
            ]
            best = None
            for ax, az, bx, bz, fam in walls:
                d = closest_point_on_seg(ax, az, bx, bz, cx, cz)[2]
                mx, mz = (ax + bx) * 0.5, (az + bz) * 0.5
                side = 1 if ((mx if travel_z else mz) >= (cx if travel_z else cz)) else -1
                if best is None or d < best[0]:
                    best = (d, side, fam)
            near_d, side, near_fam = best
            axis = "X" if travel_z else "Z"
            print(
                f"=== SEG{sid} asphalt=({cx:.1f},{cz:.1f}) across={axis} "
                f"near={near_d:.1f}u fam={near_fam} side={side:+d} nw={nwalls}"
            )
            print(
                f"{'L':>6} {'dC':>6} {'dAway':>6} {'dTwd':>6} {'dHull':>6} "
                f"{'disc':>5} {'hull':>5}  note"
            )
            for L in [
                0,
                10,
                15,
                18,
                20,
                22,
                25,
                30,
                35,
                38,
                40,
                41,
                42,
                45,
                50,
                55,
                60,
            ]:
                if travel_z:
                    px, pz = cx + side * L, cz
                    toward_x, toward_z = px + side * HALF_W, pz
                    away_x, away_z = px - side * HALF_W, pz
                else:
                    px, pz = cx, cz + side * L
                    toward_x, toward_z = px, pz + side * HALF_W
                    away_x, away_z = px, pz - side * HALF_W
                dC = min(
                    closest_point_on_seg(ax, az, bx, bz, px, pz)[2]
                    for ax, az, bx, bz, _ in walls
                )
                dT = min(
                    closest_point_on_seg(ax, az, bx, bz, toward_x, toward_z)[2]
                    for ax, az, bx, bz, _ in walls
                )
                dA = min(
                    closest_point_on_seg(ax, az, bx, bz, away_x, away_z)[2]
                    for ax, az, bx, bz, _ in walls
                )
                dH = min(
                    seg_seg_dist(ax, az, bx, bz, away_x, away_z, toward_x, toward_z)
                    for ax, az, bx, bz, _ in walls
                )
                disc = (dC < RADIUS) or (dT < RADIUS) or (dA < RADIUS)
                hull = dH < RADIUS
                note = ""
                if hull and not disc:
                    note = "<< HULL-ONLY (130219 miss)"
                elif disc and hull:
                    note = "both"
                print(
                    f"{L:6.1f} {dC:6.1f} {dA:6.1f} {dT:6.1f} {dH:6.1f} "
                    f"{str(disc):>5} {str(hull):>5}  {note}"
                )
            print()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
