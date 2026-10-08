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
        cy = sum(p[1] for p in pts) / len(pts)
        cz = sum(p[2] for p in pts) / len(pts)
        return {
            "sid": sid,
            "b": (minx, maxx, minz, maxz),
            "c": (cx, cy, cz),
            "walls": walls,
            "verts": verts,
            "grounds": grounds,
        }
    raise KeyError(sid_want)


def dist(ax, az, bx, bz, px, pz):
    abx, abz = bx - ax, bz - az
    apx, apz = px - ax, pz - az
    ab2 = abx * abx + abz * abz
    t = 0 if ab2 <= 0 else max(0, min(1, (apx * abx + apz * abz) / ab2))
    return math.hypot(ax + t * abx - px, az + t * abz - pz)


def main() -> int:
    s27, s28, s29 = load(27), load(28), load(29)
    jx = (s28["c"][0] + s29["c"][0]) / 2
    jz = (s28["c"][2] + s29["c"][2]) / 2
    tdx = s29["c"][0] - s28["c"][0]
    tdz = s29["c"][2] - s28["c"][2]
    tlen = math.hypot(tdx, tdz)
    tx, tz = tdx / tlen, tdz / tlen
    lx, lz = -tz, tx
    print(f"join mid=({jx/65536:.1f},{jz/65536:.1f})")

    for s in (s28, s29):
        print(f"\nSEG{s['sid']} walls near join plane:")
        for wi, (ax, az, bx, bz, ymin, ymax, nx, nz, fam, src) in enumerate(s["walls"]):
            mx = (ax + bx) / 2
            mz = (az + bz) / 2
            along = ((mx - jx) * tx + (mz - jz) * tz) / 65536
            across = ((mx - jx) * lx + (mz - jz) * lz) / 65536
            dpath = dist(ax, az, bx, bz, jx, jz) / 65536
            if abs(along) < 80 or dpath < 80:
                nlen = math.hypot(nx, nz) or 1
                print(
                    f"  w{wi:02d} along={along:+.1f} across={across:+.1f} dpath={dpath:.1f} "
                    f"n=({nx/nlen:.2f},{nz/nlen:.2f}) "
                    f"A=({ax/65536:.0f},{az/65536:.0f}) B=({bx/65536:.0f},{bz/65536:.0f})"
                )

    for s in (s28, s29):
        across_vals = []
        for v in s["verts"]:
            along = ((v[0] - jx) * tx + (v[2] - jz) * tz) / 65536
            if abs(along) < 60:
                across_vals.append(((v[0] - jx) * lx + (v[2] - jz) * lz) / 65536)
        if across_vals:
            print(
                f"SEG{s['sid']} asphalt verts near join: "
                f"across=[{min(across_vals):.1f},{max(across_vals):.1f}] "
                f"width={max(across_vals)-min(across_vals):.1f}"
            )

    # Simulate car probes: center + +/- half width along lateral at join
    half_w = 22.4
    rad = 2.0
    print(f"\nProbe clearance at join (half_w={half_w}, rad={rad}):")
    for lat in (-half_w, 0, half_w):
        px = jx + lat * 65536 * lx
        pz = jz + lat * 65536 * lz
        best = None
        for s in (s28, s29):
            for wi, (ax, az, bx, bz, *rest) in enumerate(s["walls"]):
                d = dist(ax, az, bx, bz, px, pz) / 65536
                if best is None or d < best[0]:
                    best = (d, s["sid"], wi)
        hit = best[0] < rad
        print(f"  lat={lat:+.1f} nearest SEG{best[1]}w{best[2]:02d} dist={best[0]:.1f}u hit={hit}")

    # Also check wider offsets (car cutting)
    print("\nWider lateral offsets:")
    for lat in range(-80, 81, 10):
        px = jx + lat * 65536 * lx
        pz = jz + lat * 65536 * lz
        best = None
        for s in (s28, s29):
            for wi, (ax, az, bx, bz, *rest) in enumerate(s["walls"]):
                d = dist(ax, az, bx, bz, px, pz) / 65536
                if best is None or d < best[0]:
                    best = (d, s["sid"], wi)
        mark = " HIT" if best[0] < rad else (" NEAR" if best[0] < 8 else "")
        if best[0] < 40:
            print(f"  lat={lat:+d} dist={best[0]:.1f}u SEG{best[1]}w{best[2]:02d}{mark}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
