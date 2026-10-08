#!/usr/bin/env python3
"""Diagnose TCOL walls + asphalt centroid clearance on SEG27/28."""
from __future__ import annotations

import json
import math
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TCOL = ROOT / "cd" / "data" / "TCOL.BIN"
REPORT = ROOT / "cd" / "data" / "track_collision_report.json"

HEADER = struct.Struct("<4sHHHHHHHHHIII")
DIRECTORY = struct.Struct("<HHHHHHiiiiIIIIIII")
VERTEX = struct.Struct("<iii")
GROUND = struct.Struct("<HHHHHBBHH")
WALL = struct.Struct("<iiiiiiiiHH")


def closest_dist(ax, az, bx, bz, px, pz):
    abx, abz = bx - ax, bz - az
    apx, apz = px - ax, pz - az
    ab2 = abx * abx + abz * abz
    if ab2 <= 0:
        return math.hypot(apx, apz)
    t = max(0.0, min(1.0, (apx * abx + apz * abz) / ab2))
    return math.hypot(ax + t * abx - px, az + t * abz - pz)


def main() -> int:
    blob = TCOL.read_bytes()
    magic, ver, hdr_sz, dir_sz, vtx_sz, gnd_sz, wall_sz, cell_sz, grid, nseg, tg, tw, doff = HEADER.unpack_from(
        blob
    )
    assert magic == b"TCL1", magic
    print(f"TCL1 ver={ver} segs={nseg} walls={tw} ground={tg} grid={grid}")

    r = json.loads(REPORT.read_text(encoding="utf-8"))
    print(
        f"report walls={r['wallFaceCount']} seamRm={r['seamCapWallsRemoved']} "
        f"midRm={r['midLaneWallsRemoved']}"
    )

    want = {26, 27, 28, 29}
    for i in range(nseg):
        (
            sid,
            nverts,
            nground,
            nwalls,
            gdim,
            _pad,
            min_x,
            max_x,
            min_z,
            max_z,
            voff,
            goff,
            woff,
            *_rest,
        ) = DIRECTORY.unpack_from(blob, doff + i * DIRECTORY.size)
        if sid not in want:
            continue

        verts = [VERTEX.unpack_from(blob, voff + j * VERTEX.size) for j in range(nverts)]
        grounds = [GROUND.unpack_from(blob, goff + j * GROUND.size) for j in range(nground)]
        walls = [WALL.unpack_from(blob, woff + j * WALL.size) for j in range(nwalls)]

        # Asphalt centroid from ground faces with surface_type==1 (byte after family).
        asphalt_pts = []
        for i0, i1, i2, i3, fam, stype, nidx, flags, src in grounds:
            if stype != 1:
                continue
            idxs = (i0, i1, i2) if nidx == 3 else (i0, i1, i2, i3)
            for ix in idxs[:nidx]:
                asphalt_pts.append(verts[ix])
        if asphalt_pts:
            cx = sum(v[0] for v in asphalt_pts) / len(asphalt_pts)
            cy = sum(v[1] for v in asphalt_pts) / len(asphalt_pts)
            cz = sum(v[2] for v in asphalt_pts) / len(asphalt_pts)
        else:
            cx = (min_x + max_x) * 0.5
            cy = 0.0
            cz = (min_z + max_z) * 0.5

        xspan = max(1, max_x - min_x)
        zspan = max(1, max_z - min_z)
        travel_z = zspan <= xspan
        print(
            f"\n=== SEG{sid:03d} verts={nverts} ground={nground} walls={nwalls} "
            f"AABB x=[{min_x},{max_x}] z=[{min_z},{max_z}] "
            f"span=({xspan},{zspan}) travel_z={travel_z}"
        )
        print(f"  asphalt_centroid=({cx:.0f},{cy:.0f},{cz:.0f}) units_raw")

        # Classify each wall vs centroid / ends.
        near_mid = []
        transverse = []
        for wi, (ax, az, bx, bz, ymin, ymax, nx, nz, fam, src) in enumerate(walls):
            dist = closest_dist(float(ax), float(az), float(bx), float(bz), cx, cz)
            abx, abz = bx - ax, bz - az
            nlen = math.hypot(float(nx), float(nz)) or 1.0
            nnx, nnz = nx / nlen, nz / nlen
            # travel approx: shorter AABB axis
            tx, tz = (0.0, 1.0) if travel_z else (1.0, 0.0)
            align = abs(nnx * tx + nnz * tz)
            mx = (ax + bx) * 0.5
            mz = (az + bz) * 0.5
            if travel_z:
                dist_end = min(abs(mz - min_z), abs(max_z - mz))
                dist_lat = min(abs(mx - min_x), abs(max_x - mx))
                cross_lane = abs(abx) > abs(abz) * 1.2
            else:
                dist_end = min(abs(mx - min_x), abs(max_x - mx))
                dist_lat = min(abs(mz - min_z), abs(max_z - mz))
                cross_lane = abs(abz) > abs(abx) * 1.2
            row = {
                "i": wi,
                "dist_u": dist / 65536.0,
                "align": align,
                "cross": cross_lane,
                "dist_end_u": dist_end / 65536.0,
                "dist_lat_u": dist_lat / 65536.0,
                "nx": nnx,
                "nz": nnz,
                "len_u": math.hypot(abx, abz) / 65536.0,
                "ymin_u": ymin / 65536.0,
                "ymax_u": ymax / 65536.0,
            }
            if dist / 65536.0 < 80.0:
                near_mid.append(row)
            if align >= 0.7 and (cross_lane or dist_end <= dist_lat):
                transverse.append(row)

        print(f"  walls within 80u of asphalt centroid: {len(near_mid)}")
        for row in sorted(near_mid, key=lambda r: r["dist_u"])[:12]:
            print(
                f"    w{row['i']:02d} dist={row['dist_u']:.1f}u align={row['align']:.2f} "
                f"cross={row['cross']} end={row['dist_end_u']:.1f} lat={row['dist_lat_u']:.1f} "
                f"n=({row['nx']:.2f},{row['nz']:.2f}) len={row['len_u']:.1f} "
                f"Y=[{row['ymin_u']:.0f},{row['ymax_u']:.0f}]"
            )
        print(f"  candidate transverse/seam-ish: {len(transverse)}")
        for row in sorted(transverse, key=lambda r: r["dist_u"])[:12]:
            print(
                f"    w{row['i']:02d} dist={row['dist_u']:.1f}u align={row['align']:.2f} "
                f"cross={row['cross']} end={row['dist_end_u']:.1f} lat={row['dist_lat_u']:.1f} "
                f"n=({row['nx']:.2f},{row['nz']:.2f}) len={row['len_u']:.1f}"
            )

    # Neighbor centroid deltas 27->28
    cents = {}
    for i in range(nseg):
        sid, nverts, nground, nwalls, gdim, _pad, min_x, max_x, min_z, max_z, voff, goff, woff, *_ = (
            DIRECTORY.unpack_from(blob, doff + i * DIRECTORY.size)
        )
        if sid not in want:
            continue
        verts = [VERTEX.unpack_from(blob, voff + j * VERTEX.size) for j in range(nverts)]
        grounds = [GROUND.unpack_from(blob, goff + j * GROUND.size) for j in range(nground)]
        pts = []
        for i0, i1, i2, i3, fam, stype, nidx, flags, src in grounds:
            if stype != 1:
                continue
            for ix in (i0, i1, i2, i3)[:nidx]:
                pts.append(verts[ix])
        if pts:
            cents[sid] = (
                sum(p[0] for p in pts) / len(pts),
                sum(p[1] for p in pts) / len(pts),
                sum(p[2] for p in pts) / len(pts),
            )
    print("\ncentroid deltas (raw):")
    for a, b in ((26, 27), (27, 28), (28, 29)):
        if a in cents and b in cents:
            dx = (cents[b][0] - cents[a][0]) / 65536.0
            dy = (cents[b][1] - cents[a][1]) / 65536.0
            dz = (cents[b][2] - cents[a][2]) / 65536.0
            print(f"  SEG{a}->SEG{b}: dx={dx:.1f} dy={dy:.1f} dz={dz:.1f} lenXZ={math.hypot(dx,dz):.1f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
