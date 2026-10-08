#!/usr/bin/env python3
"""Sample ground continuity along SEG27->28 asphalt centroids (TCOL faces)."""
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


def point_in_tri_xz(px, pz, a, b, c):
    def sign(p1, p2, p3):
        return (p1[0] - p3[0]) * (p2[1] - p3[1]) - (p2[0] - p3[0]) * (p1[1] - p3[1])

    b1 = sign((px, pz), (a[0], a[2]), (b[0], b[2])) < 0.0
    b2 = sign((px, pz), (b[0], b[2]), (c[0], c[2])) < 0.0
    b3 = sign((px, pz), (c[0], c[2]), (a[0], a[2])) < 0.0
    return b1 == b2 == b3


def bary_y(px, pz, a, b, c):
    # Planar interp of Y over triangle in XZ
    x1, y1, z1 = a
    x2, y2, z2 = b
    x3, y3, z3 = c
    det = (z2 - z3) * (x1 - x3) + (x3 - x2) * (z1 - z3)
    if abs(det) < 1e-9:
        return None
    l1 = ((z2 - z3) * (px - x3) + (x3 - x2) * (pz - z3)) / det
    l2 = ((z3 - z1) * (px - x3) + (x1 - x3) * (pz - z3)) / det
    l3 = 1.0 - l1 - l2
    if l1 < -1e-4 or l2 < -1e-4 or l3 < -1e-4:
        return None
    return l1 * y1 + l2 * y2 + l3 * y3


def load_seg(blob, doff, i):
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
    verts = [VERTEX.unpack_from(blob, voff + j * VERTEX.size) for j in range(nverts)]
    grounds = []
    for j in range(nground):
        i0, i1, i2, i3, fam, stype, nidx, flags, src = GROUND.unpack_from(
            blob, goff + j * GROUND.size
        )
        idxs = [i0, i1, i2] if nidx == 3 else [i0, i1, i2, i3][:nidx]
        grounds.append((idxs, stype))
    return sid, verts, grounds, (min_x, max_x, min_z, max_z)


def asphalt_centroid(verts, grounds):
    pts = []
    for idxs, stype in grounds:
        if stype != 1:
            continue
        for ix in idxs:
            pts.append(verts[ix])
    if not pts:
        return None
    return (
        sum(p[0] for p in pts) / len(pts),
        sum(p[1] for p in pts) / len(pts),
        sum(p[2] for p in pts) / len(pts),
    )


def sample_y(px, pz, segs):
    best = None
    best_seg = None
    for sid, verts, grounds, _bounds in segs:
        for idxs, stype in grounds:
            if stype not in (1, 2, 3):
                continue
            # fan tris
            tris = []
            if len(idxs) == 3:
                tris = [idxs]
            elif len(idxs) >= 4:
                tris = [idxs[:3], [idxs[0], idxs[2], idxs[3]]]
            for tri in tris:
                a, b, c = verts[tri[0]], verts[tri[1]], verts[tri[2]]
                y = bary_y(px, pz, a, b, c)
                if y is None:
                    continue
                if best is None or y > best:
                    best = y
                    best_seg = sid
    return best, best_seg


def main() -> int:
    blob = TCOL.read_bytes()
    magic, ver, hdr_sz, dir_sz, vtx_sz, gnd_sz, wall_sz, cell_sz, grid, nseg, tg, tw, doff = HEADER.unpack_from(
        blob
    )
    loaded = []
    cents = {}
    for i in range(nseg):
        sid, verts, grounds, bounds = load_seg(blob, doff, i)
        if sid in (26, 27, 28, 29):
            loaded.append((sid, verts, grounds, bounds))
            c = asphalt_centroid(verts, grounds)
            cents[sid] = c
            print(f"SEG{sid} asphalt_centroid Y={c[1]/65536.0:.2f}u" if c else f"SEG{sid} no asphalt")

    c27, c28 = cents[27], cents[28]
    print("\nSamples along SEG27 centroid -> SEG28 centroid:")
    misses = 0
    prev_y = None
    for t in [i / 20.0 for i in range(21)]:
        px = c27[0] + (c28[0] - c27[0]) * t
        pz = c27[2] + (c28[2] - c27[2]) * t
        y, sid = sample_y(px, pz, loaded)
        if y is None:
            misses += 1
            print(f"  t={t:.2f} MISS")
            prev_y = None
            continue
        yu = y / 65536.0
        dy = "" if prev_y is None else f" dY={yu - prev_y:+.2f}"
        print(f"  t={t:.2f} Y={yu:.2f}u seg={sid}{dy}")
        prev_y = yu
    print(f"misses={misses}/21")

    # Also sample a wider corridor (±40u lateral to travel)
    dx = c28[0] - c27[0]
    dz = c28[2] - c27[2]
    length = math.hypot(dx, dz) or 1.0
    lx, lz = -dz / length, dx / length  # perpendicular
    print("\nLateral corridor ±40u / ±80u miss counts along join:")
    for lat_u in (0, 40, 80, 120):
        for sign in (-1, 1) if lat_u else (0,):
            miss = 0
            total = 0
            for t in [i / 20.0 for i in range(21)]:
                px = c27[0] + dx * t + sign * lat_u * 65536.0 * lx
                pz = c27[2] + dz * t + sign * lat_u * 65536.0 * lz
                y, _ = sample_y(px, pz, loaded)
                total += 1
                if y is None:
                    miss += 1
            tag = f"lat={sign * lat_u:+d}u"
            print(f"  {tag}: misses={miss}/{total}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
