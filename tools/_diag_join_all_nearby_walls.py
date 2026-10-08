#!/usr/bin/env python3
"""All TCOL walls from seed±4 near SEG28/29 join mid-lane samples."""
from __future__ import annotations

import math
import struct
from pathlib import Path

HEADER = struct.Struct("<4sHHHHHHHHHIII")
DIRECTORY = struct.Struct("<HHHHHHiiiiIIIIIII")
VERTEX = struct.Struct("<iii")
GROUND = struct.Struct("<HHHHHBBHH")
WALL = struct.Struct("<iiiiiiiiHH")
blob = Path("cd/data/TCOL.BIN").read_bytes()
magic, ver, hdr_sz, dir_sz, vtx_sz, gnd_sz, wall_sz, cell_sz, grid, nseg, tg, tw, doff = HEADER.unpack_from(
    blob
)


def load_all():
    segs = {}
    for i in range(nseg):
        sid, nv, ng, nw, _, pad, minx, maxx, minz, maxz, voff, goff, woff, *r = DIRECTORY.unpack_from(
            blob, doff + i * DIRECTORY.size
        )
        verts = [VERTEX.unpack_from(blob, voff + j * VERTEX.size) for j in range(nv)]
        grounds = [GROUND.unpack_from(blob, goff + j * GROUND.size) for j in range(ng)]
        walls = [WALL.unpack_from(blob, woff + j * WALL.size) for j in range(nw)]
        pts = []
        for a, b, c, d, fam, st, nidx, fl, src in grounds:
            if st != 1:
                continue
            for ix in (a, b, c, d)[:nidx]:
                pts.append(verts[ix])
        if not pts:
            continue
        cx = sum(p[0] for p in pts) / len(pts)
        cz = sum(p[2] for p in pts) / len(pts)
        segs[sid] = {"c": (cx, cz), "walls": walls}
    return segs


def dist(ax, az, bx, bz, px, pz):
    abx, abz = bx - ax, bz - az
    apx, apz = px - ax, pz - az
    ab2 = abx * abx + abz * abz
    t = 0 if ab2 <= 0 else max(0, min(1, (apx * abx + apz * abz) / ab2))
    return math.hypot(ax + t * abx - px, az + t * abz - pz)


def main() -> int:
    segs = load_all()
    c28 = segs[28]["c"]
    c29 = segs[29]["c"]
    # samples along join, mid-lane
    samples = []
    for t in [i / 10 for i in range(11)]:
        samples.append(
            (
                t,
                c28[0] + (c29[0] - c28[0]) * t,
                c28[1] + (c29[1] - c28[1]) * t,
            )
        )

    seed = 28
    window = list(range(seed - 4, seed + 5)) + list(range(29 - 4, 29 + 5))
    window = sorted(set(window))
    print(f"scanning segs {window}")

    hits = []
    for sid in window:
        if sid not in segs:
            continue
        for wi, (ax, az, bx, bz, ymin, ymax, nx, nz, fam, src) in enumerate(segs[sid]["walls"]):
            mind = 1e18
            best_t = 0
            for t, px, pz in samples:
                d = dist(ax, az, bx, bz, px, pz)
                if d < mind:
                    mind = d
                    best_t = t
            if mind / 65536.0 < 50.0:
                nlen = math.hypot(nx, nz) or 1
                hits.append((mind / 65536.0, sid, wi, best_t, nx / nlen, nz / nlen, ax, az, bx, bz))

    hits.sort()
    print(f"walls within 50u of mid-lane 28-29 path: {len(hits)}")
    for d, sid, wi, t, nnx, nnz, ax, az, bx, bz in hits[:40]:
        print(
            f"  d={d:.1f}u t={t:.1f} SEG{sid}w{wi:02d} n=({nnx:.2f},{nnz:.2f}) "
            f"A=({ax/65536:.0f},{az/65536:.0f}) B=({bx/65536:.0f},{bz/65536:.0f})"
        )

    # Also: any wall within 30u of ANY sample?
    print("\nwalls within 30u:")
    for d, sid, wi, t, nnx, nnz, ax, az, bx, bz in hits:
        if d < 30:
            print(f"  d={d:.1f}u t={t:.1f} SEG{sid}w{wi:02d} n=({nnx:.2f},{nnz:.2f})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
