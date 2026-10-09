#!/usr/bin/env python3
"""Compare type=1 asphalt strip width vs driveable 1/2/3 near SEG28 join."""
from __future__ import annotations

import math
import struct
from pathlib import Path

HEADER = struct.Struct("<4sHHHHHHHHHIII")
DIRECTORY = struct.Struct("<HHHHHHiiiiIIIIIII")
VERTEX = struct.Struct("<iii")
GROUND = struct.Struct("<HHHHHBBHH")
blob = Path("cd/data/TCOL.BIN").read_bytes()
magic, ver, hdr_sz, dir_sz, vtx_sz, gnd_sz, wall_sz, cell_sz, grid, nseg, tg, tw, doff = HEADER.unpack_from(
    blob
)


def load(sid_want):
    for i in range(nseg):
        sid, nv, ng, nw, _, pad, minx, maxx, minz, maxz, voff, goff, woff, *r = DIRECTORY.unpack_from(
            blob, doff + i * DIRECTORY.size
        )
        if sid != sid_want:
            continue
        verts = [VERTEX.unpack_from(blob, voff + j * VERTEX.size) for j in range(nv)]
        grounds = [GROUND.unpack_from(blob, goff + j * GROUND.size) for j in range(ng)]
        return sid, verts, grounds
    raise KeyError(sid_want)


def face_pts(verts, grounds, types):
    pts = []
    for a, b, c, d, fam, st, nidx, fl, src in grounds:
        if st not in types:
            continue
        for ix in (a, b, c, d)[:nidx]:
            pts.append(verts[ix])
    return pts


def main() -> int:
    s28 = load(28)
    s29 = load(29)
    # centroids type1
    def cent(pts):
        return (
            sum(p[0] for p in pts) / len(pts),
            sum(p[2] for p in pts) / len(pts),
        )

    a28 = face_pts(s28[1], s28[2], {1})
    d28 = face_pts(s28[1], s28[2], {1, 2, 3})
    a29 = face_pts(s29[1], s29[2], {1})
    d29 = face_pts(s29[1], s29[2], {1, 2, 3})
    ca28, ca29 = cent(a28), cent(a29)
    cd28, cd29 = cent(d28), cent(d29)
    print(f"SEG28 type1 centroid=({ca28[0]/65536:.1f},{ca28[1]/65536:.1f})")
    print(f"SEG28 driveable centroid=({cd28[0]/65536:.1f},{cd28[1]/65536:.1f})")
    print(
        f"  delta type1-vs-drv=({(ca28[0]-cd28[0])/65536:.1f},{(ca28[1]-cd28[1])/65536:.1f})"
    )
    print(f"SEG29 type1 centroid=({ca29[0]/65536:.1f},{ca29[1]/65536:.1f})")
    print(f"SEG29 driveable centroid=({cd29[0]/65536:.1f},{cd29[1]/65536:.1f})")

    tdx = ca29[0] - ca28[0]
    tdz = ca29[1] - ca28[1]
    tlen = math.hypot(tdx, tdz)
    tx, tz = tdx / tlen, tdz / tlen
    lx, lz = -tz, tx
    jx = (ca28[0] + ca29[0]) / 2
    jz = (ca28[1] + ca29[1]) / 2

    def across_range(pts, label):
        vals = []
        for p in pts:
            along = ((p[0] - jx) * tx + (p[2] - jz) * tz) / 65536
            if abs(along) < 80:
                vals.append(((p[0] - jx) * lx + (p[2] - jz) * lz) / 65536)
        if not vals:
            print(f"{label}: no verts near join")
            return
        print(
            f"{label}: across=[{min(vals):.1f},{max(vals):.1f}] "
            f"width={max(vals)-min(vals):.1f} mid={ (min(vals)+max(vals))/2:.1f}"
        )

    across_range(a28, "SEG28 type1")
    across_range(d28, "SEG28 driveable 1/2/3")
    across_range(a29, "SEG29 type1")
    across_range(d29, "SEG29 driveable 1/2/3")

    # Wall positions from type1 contour relative to driveable mid
    # Driveable mid across
    dvals = []
    for p in d28 + d29:
        along = ((p[0] - jx) * tx + (p[2] - jz) * tz) / 65536
        if abs(along) < 80:
            dvals.append(((p[0] - jx) * lx + (p[2] - jz) * lz) / 65536)
    drv_mid = (min(dvals) + max(dvals)) / 2
    print(f"\nDriveable visual mid across={drv_mid:.1f}")
    print(f"Type1 centroid at across={((ca28[0]-jx)*lx+(ca28[1]-jz)*lz)/65536:.1f}")

    # Where are contour walls relative to driveable mid?
    WALL = struct.Struct("<iiiiiiiiHH")
    # reload walls for 28
    for i in range(nseg):
        sid, nv, ng, nw, _, pad, minx, maxx, minz, maxz, voff, goff, woff, *r = DIRECTORY.unpack_from(
            blob, doff + i * DIRECTORY.size
        )
        if sid not in (28, 29):
            continue
        print(f"\nSEG{sid} walls across vs driveable mid:")
        for wi in range(nw):
            ax, az, bx, bz, ymin, ymax, nx, nz, fam, src = WALL.unpack_from(
                blob, woff + wi * WALL.size
            )
            mx, mz = (ax + bx) / 2, (az + bz) / 2
            along = ((mx - jx) * tx + (mz - jz) * tz) / 65536
            across = ((mx - jx) * lx + (mz - jz) * lz) / 65536
            if abs(along) > 100:
                continue
            gap = across - drv_mid
            print(
                f"  w{wi:02d} across={across:+.1f} from_drv_mid={gap:+.1f} along={along:+.1f}"
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
