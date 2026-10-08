#!/usr/bin/env python3
"""Check what surface lies just outside SEG28 contour walls (type1 edges)."""
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


def bary_inside(px, pz, a, b, c):
    x1, z1 = a[0], a[2]
    x2, z2 = b[0], b[2]
    x3, z3 = c[0], c[2]
    det = (z2 - z3) * (x1 - x3) + (x3 - x2) * (z1 - z3)
    if abs(det) < 1e-9:
        return False
    l1 = ((z2 - z3) * (px - x3) + (x3 - x2) * (pz - z3)) / det
    l2 = ((z3 - z1) * (px - x3) + (x1 - x3) * (pz - z3)) / det
    l3 = 1.0 - l1 - l2
    return l1 >= -1e-4 and l2 >= -1e-4 and l3 >= -1e-4


def sample_types(verts, grounds, px, pz):
    found = set()
    for i0, i1, i2, i3, fam, st, nidx, fl, src in grounds:
        idxs = [i0, i1, i2] if nidx == 3 else [i0, i1, i2, i3][:nidx]
        tris = [idxs] if len(idxs) == 3 else [idxs[:3], [idxs[0], idxs[2], idxs[3]]]
        for tri in tris:
            if bary_inside(px, pz, verts[tri[0]], verts[tri[1]], verts[tri[2]]):
                found.add(st)
    return found


def load(sid_want):
    for i in range(nseg):
        sid, nv, ng, nw, _, pad, minx, maxx, minz, maxz, voff, goff, woff, *r = DIRECTORY.unpack_from(
            blob, doff + i * DIRECTORY.size
        )
        if sid != sid_want:
            continue
        verts = [VERTEX.unpack_from(blob, voff + j * VERTEX.size) for j in range(nv)]
        grounds = [GROUND.unpack_from(blob, goff + j * GROUND.size) for j in range(ng)]
        walls = [WALL.unpack_from(blob, woff + j * WALL.size) for j in range(nw)]
        pts = []
        for a, b, c, d, fam, st, nidx, fl, src in grounds:
            if st != 1:
                continue
            for ix in (a, b, c, d)[:nidx]:
                pts.append(verts[ix])
        cx = sum(p[0] for p in pts) / len(pts)
        cz = sum(p[2] for p in pts) / len(pts)
        return verts, grounds, walls, (cx, cz)
    raise KeyError(sid_want)


def main() -> int:
    # Merge faces from nearby segs for sampling
    all_verts_grounds = []
    for sid in range(26, 32):
        try:
            v, g, w, c = load(sid)
        except KeyError:
            continue
        all_verts_grounds.append((sid, v, g))
        if sid == 28:
            walls28 = w
            c28 = c

    print("SEG28 walls: surface types at mid ± outward step 8u / 20u")
    for wi, (ax, az, bx, bz, ymin, ymax, nx, nz, fam, src) in enumerate(walls28):
        nlen = math.hypot(nx, nz) or 1.0
        nnx, nnz = nx / nlen, nz / nlen
        mx, mz = (ax + bx) * 0.5, (az + bz) * 0.5
        # Orient using asphalt centroid: outward should be away from centroid
        to_out = nnx * (mx - c28[0]) + nnz * (mz - c28[1])
        if to_out < 0:
            nnx, nnz = -nnx, -nnz  # flip to outward from centroid
            flipped = True
        else:
            flipped = False
        for step_u in (8, 20):
            ox = mx + nnx * step_u * 65536
            oz = mz + nnz * step_u * 65536
            ix = mx - nnx * step_u * 65536
            iz = mz - nnz * step_u * 65536
            out_types = set()
            in_types = set()
            for sid, v, g in all_verts_grounds:
                out_types |= sample_types(v, g, ox, oz)
                in_types |= sample_types(v, g, ix, iz)
            dcent = math.hypot(mx - c28[0], mz - c28[1]) / 65536
            if step_u == 8:
                print(
                    f"  w{wi:02d} dcent={dcent:.1f} flipped={flipped} "
                    f"out{step_u}={sorted(out_types) or '-'} in{step_u}={sorted(in_types) or '-'} "
                    f"stored_n=({nx/nlen:.2f},{nz/nlen:.2f})"
                )
            else:
                print(
                    f"       out{step_u}={sorted(out_types) or '-'} in{step_u}={sorted(in_types) or '-'}"
                )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
