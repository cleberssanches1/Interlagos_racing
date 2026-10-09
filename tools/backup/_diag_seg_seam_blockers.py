#!/usr/bin/env python3
"""Find TCOL walls that can block mid-lane travel near a segment join."""
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


def closest_dist(ax, az, bx, bz, px, pz):
    abx, abz = bx - ax, bz - az
    apx, apz = px - ax, pz - az
    ab2 = abx * abx + abz * abz
    if ab2 <= 0:
        return math.hypot(apx, apz), 0.0
    t = max(0.0, min(1.0, (apx * abx + apz * abz) / ab2))
    return math.hypot(ax + t * abx - px, az + t * abz - pz), t


def load_all(blob):
    magic, ver, hdr_sz, dir_sz, vtx_sz, gnd_sz, wall_sz, cell_sz, grid, nseg, tg, tw, doff = HEADER.unpack_from(
        blob
    )
    segs = {}
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
        verts = [VERTEX.unpack_from(blob, voff + j * VERTEX.size) for j in range(nverts)]
        grounds = [
            GROUND.unpack_from(blob, goff + j * GROUND.size) for j in range(nground)
        ]
        walls = [WALL.unpack_from(blob, woff + j * WALL.size) for j in range(nwalls)]
        asphalt = []
        for i0, i1, i2, i3, fam, stype, nidx, flags, src in grounds:
            if stype != 1:
                continue
            for ix in (i0, i1, i2, i3)[:nidx]:
                asphalt.append(verts[ix])
        if asphalt:
            cx = sum(p[0] for p in asphalt) / len(asphalt)
            cy = sum(p[1] for p in asphalt) / len(asphalt)
            cz = sum(p[2] for p in asphalt) / len(asphalt)
        else:
            cx = (min_x + max_x) * 0.5
            cy = 0.0
            cz = (min_z + max_z) * 0.5
        segs[sid] = {
            "bounds": (min_x, max_x, min_z, max_z),
            "centroid": (cx, cy, cz),
            "walls": walls,
            "verts": verts,
            "grounds": grounds,
        }
    return segs


def analyze_join(segs, a: int, b: int) -> None:
    sa, sb = segs[a], segs[b]
    ca, cb = sa["centroid"], sb["centroid"]
    # Travel from a->b
    tdx = cb[0] - ca[0]
    tdz = cb[2] - ca[2]
    tlen = math.hypot(tdx, tdz) or 1.0
    tx, tz = tdx / tlen, tdz / tlen
    print(
        f"\n======== JOIN SEG{a}->SEG{b} travel=({tx:.3f},{tz:.3f}) "
        f"len={tlen/65536:.1f}u ========"
    )
    print(
        f"  A centroid=({ca[0]/65536:.1f},{ca[2]/65536:.1f}) "
        f"B=({cb[0]/65536:.1f},{cb[2]/65536:.1f})"
    )

    # Sample points along join (mid-lane path)
    samples = []
    for t in [i / 10 for i in range(11)]:
        px = ca[0] + tdx * t
        pz = ca[2] + tdz * t
        samples.append((t, px, pz))

    for sid in (a, b):
        s = segs[sid]
        min_x, max_x, min_z, max_z = s["bounds"]
        xspan = max(1, max_x - min_x)
        zspan = max(1, max_z - min_z)
        print(
            f"\n--- SEG{sid} walls={len(s['walls'])} "
            f"AABB xspan={xspan/65536:.0f} zspan={zspan/65536:.0f} ---"
        )
        blockers = []
        for wi, (ax, az, bx, bz, ymin, ymax, nx, nz, fam, src) in enumerate(s["walls"]):
            nlen = math.hypot(float(nx), float(nz)) or 1.0
            nnx, nnz = nx / nlen, nz / nlen
            align = abs(nnx * tx + nnz * tz)  # 1 = faces along travel (blocks path)
            # min distance to any mid-lane sample
            min_d = 1e18
            best_t = 0.0
            for t, px, pz in samples:
                d, _ = closest_dist(float(ax), float(az), float(bx), float(bz), px, pz)
                if d < min_d:
                    min_d = d
                    best_t = t
            # also dist to own centroid
            d_cent, _ = closest_dist(
                float(ax), float(az), float(bx), float(bz), s["centroid"][0], s["centroid"][2]
            )
            abx, abz = bx - ax, bz - az
            # near segment end toward neighbor?
            mx = (ax + bx) * 0.5
            mz = (az + bz) * 0.5
            # end toward b: for travel mostly -Z, the min_z end of a / max_z end of b
            if abs(tz) >= abs(tx):
                # Z travel
                if sid == a:
                    # toward lower Z if tz<0
                    end_ref = min_z if tz < 0 else max_z
                    dist_end = abs(mz - end_ref)
                else:
                    end_ref = max_z if tz < 0 else min_z
                    dist_end = abs(mz - end_ref)
                dist_lat = min(abs(mx - min_x), abs(max_x - mx))
            else:
                if sid == a:
                    end_ref = min_x if tx < 0 else max_x
                    dist_end = abs(mx - end_ref)
                else:
                    end_ref = max_x if tx < 0 else min_x
                    dist_end = abs(mx - end_ref)
                dist_lat = min(abs(mz - min_z), abs(max_z - mz))

            row = {
                "i": wi,
                "d_path_u": min_d / 65536.0,
                "d_cent_u": d_cent / 65536.0,
                "align": align,
                "path_t": best_t,
                "n": (nnx, nnz),
                "len_u": math.hypot(abx, abz) / 65536.0,
                "dist_end_u": dist_end / 65536.0,
                "dist_lat_u": dist_lat / 65536.0,
                "y": (ymin / 65536.0, ymax / 65536.0),
            }
            # Flag potential mid-path blockers: close to path OR (near end + high align)
            if (
                row["d_path_u"] < 60.0
                or (row["align"] >= 0.55 and row["dist_end_u"] < 80.0 and row["d_cent_u"] < 120.0)
                or (row["d_path_u"] < 100.0 and row["align"] >= 0.7)
            ):
                blockers.append(row)

        blockers.sort(key=lambda r: (r["d_path_u"], -r["align"]))
        print(f"  potential blockers: {len(blockers)}")
        for row in blockers[:20]:
            print(
                f"    w{row['i']:02d} d_path={row['d_path_u']:.1f}u@t={row['path_t']:.1f} "
                f"d_cent={row['d_cent_u']:.1f}u align={row['align']:.2f} "
                f"end={row['dist_end_u']:.1f} lat={row['dist_lat_u']:.1f} "
                f"n=({row['n'][0]:.2f},{row['n'][1]:.2f}) len={row['len_u']:.1f} "
                f"Y=[{row['y'][0]:.0f},{row['y'][1]:.0f}]"
            )

        # Also: for each path sample, nearest wall
        print("  nearest wall per path sample:")
        for t, px, pz in samples:
            best = None
            for wi, (ax, az, bx, bz, ymin, ymax, nx, nz, fam, src) in enumerate(s["walls"]):
                d, _ = closest_dist(float(ax), float(az), float(bx), float(bz), px, pz)
                nlen = math.hypot(float(nx), float(nz)) or 1.0
                align = abs((nx / nlen) * tx + (nz / nlen) * tz)
                if best is None or d < best[0]:
                    best = (d, wi, align, nx / nlen, nz / nlen)
            if best:
                print(
                    f"    t={t:.1f} nearest w{best[1]:02d} dist={best[0]/65536:.1f}u "
                    f"align={best[2]:.2f} n=({best[3]:.2f},{best[4]:.2f})"
                )


def main() -> int:
    segs = load_all(TCOL.read_bytes())
    for a, b in ((27, 28), (28, 29), (29, 30), (26, 27)):
        analyze_join(segs, a, b)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
