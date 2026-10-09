#!/usr/bin/env python3
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


def load(sid_want):
    for i in range(nseg):
        sid, nverts, nground, nwalls, gdim, pad, minx, maxx, minz, maxz, voff, goff, woff, *r = (
            DIRECTORY.unpack_from(blob, doff + i * DIRECTORY.size)
        )
        if sid != sid_want:
            continue
        verts = [VERTEX.unpack_from(blob, voff + j * VERTEX.size) for j in range(nverts)]
        grounds = [GROUND.unpack_from(blob, goff + j * GROUND.size) for j in range(nground)]
        walls = [WALL.unpack_from(blob, woff + j * WALL.size) for j in range(nwalls)]
        pts = []
        for i0, i1, i2, i3, fam, st, nidx, fl, src in grounds:
            if st != 1:
                continue
            for ix in (i0, i1, i2, i3)[:nidx]:
                pts.append(verts[ix])
        cx = sum(p[0] for p in pts) / len(pts)
        cz = sum(p[2] for p in pts) / len(pts)
        return {
            "sid": sid,
            "bounds": (minx, maxx, minz, maxz),
            "c": (cx, cz),
            "walls": walls,
        }
    raise KeyError(sid_want)


def dist_wall_pt(ax, az, bx, bz, px, pz):
    abx, abz = bx - ax, bz - az
    apx, apz = px - ax, pz - az
    ab2 = abx * abx + abz * abz
    t = 0.0 if ab2 <= 0 else max(0.0, min(1.0, (apx * abx + apz * abz) / ab2))
    return math.hypot(ax + t * abx - px, az + t * abz - pz)


def main() -> int:
    s27, s28, s29 = load(27), load(28), load(29)
    cx, cz = s28["c"]
    tdx = s29["c"][0] - cx
    tdz = s29["c"][1] - cz
    tlen = math.hypot(tdx, tdz)
    tx, tz = tdx / tlen, tdz / tlen
    tdx2 = cx - s27["c"][0]
    tdz2 = cz - s27["c"][1]
    tlen2 = math.hypot(tdx2, tdz2)
    tx2, tz2 = tdx2 / tlen2, tdz2 / tlen2

    path = []
    for t in [i / 8 for i in range(9)]:
        path.append(
            (
                "27-28",
                t,
                s27["c"][0] + (cx - s27["c"][0]) * t,
                s27["c"][1] + (cz - s27["c"][1]) * t,
            )
        )
    for t in [i / 8 for i in range(9)]:
        path.append(("28-29", t, cx + tdx * t, cz + tdz * t))

    # Also offset path ±30u and ±50u laterally (car half-width ~22)
    lx, lz = -tz, tx
    for lat in (-50, -30, 30, 50):
        for t in [i / 8 for i in range(9)]:
            path.append(
                (
                    f"28-29L{lat}",
                    t,
                    cx + tdx * t + lat * 65536 * lx,
                    cz + tdz * t + lat * 65536 * lz,
                )
            )

    print(f"SEG28 centroid=({cx/65536:.1f},{cz/65536:.1f}) travel28to29=({tx:.3f},{tz:.3f})")
    print("All SEG28 walls:")
    for wi, (ax, az, bx, bz, ymin, ymax, nx, nz, fam, src) in enumerate(s28["walls"]):
        nlen = math.hypot(nx, nz) or 1.0
        nnx, nnz = nx / nlen, nz / nlen
        dcent = dist_wall_pt(ax, az, bx, bz, cx, cz) / 65536
        align29 = abs(nnx * tx + nnz * tz)
        align27 = abs(nnx * tx2 + nnz * tz2)
        mind = 1e18
        best = ("?", 0.0)
        for tag, t, px, pz in path:
            d = dist_wall_pt(ax, az, bx, bz, px, pz)
            if d < mind:
                mind = d
                best = (tag, t)
        flag = " <<<" if mind / 65536 < 40 else ""
        print(
            f"  w{wi:02d} A=({ax/65536:.0f},{az/65536:.0f}) B=({bx/65536:.0f},{bz/65536:.0f}) "
            f"n=({nnx:.2f},{nnz:.2f}) dcent={dcent:.1f} dpath={mind/65536:.1f}@{best[0]}t={best[1]:.2f} "
            f"a29={align29:.2f} a27={align27:.2f} len={math.hypot(bx-ax,bz-az)/65536:.1f} "
            f"Y=[{ymin/65536:.0f},{ymax/65536:.0f}]{flag}"
        )

    print("\nSEG29 walls with dpath<80 (incl lateral offsets):")
    for wi, (ax, az, bx, bz, ymin, ymax, nx, nz, fam, src) in enumerate(s29["walls"]):
        nlen = math.hypot(nx, nz) or 1.0
        nnx, nnz = nx / nlen, nz / nlen
        mind = min(dist_wall_pt(ax, az, bx, bz, px, pz) for tag, t, px, pz in path)
        if mind / 65536 < 80:
            print(
                f"  w{wi:02d} A=({ax/65536:.0f},{az/65536:.0f}) B=({bx/65536:.0f},{bz/65536:.0f}) "
                f"n=({nnx:.2f},{nnz:.2f}) dpath={mind/65536:.1f} "
                f"align={abs(nnx*tx+nnz*tz):.2f} len={math.hypot(bx-ax,bz-az)/65536:.1f}"
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
