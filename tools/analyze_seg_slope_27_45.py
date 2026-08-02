#!/usr/bin/env python3
"""Analyze ground Y / grade continuity for SEG_027..SEG_045 NYA files."""
from __future__ import annotations

import math
import sys
from pathlib import Path


def be32(b: bytes, o: int) -> int:
    return int.from_bytes(b[o : o + 4], "big", signed=False)


def be16(b: bytes, o: int) -> int:
    return int.from_bytes(b[o : o + 2], "big", signed=False)


def sbe32(b: bytes, o: int) -> int:
    return int.from_bytes(b[o : o + 4], "big", signed=True)


def analyze(path: Path) -> dict | None:
    data = path.read_bytes()
    if len(data) < 12:
        return None
    typ = be32(data, 0)
    mesh_count = be32(data, 4)
    off = 12
    all_y: list[float] = []
    ground_y: list[float] = []
    face_grades: list[float] = []
    verts_all: list[tuple[float, float, float]] = []
    ground_centers: list[tuple[float, float, float]] = []

    for _mi in range(mesh_count):
        if off + 8 > len(data):
            break
        pts = be32(data, off)
        pol = be32(data, off + 4)
        off += 8
        verts: list[tuple[float, float, float]] = []
        for i in range(pts):
            x = sbe32(data, off + i * 12) / 65536.0
            y = sbe32(data, off + i * 12 + 4) / 65536.0
            z = sbe32(data, off + i * 12 + 8) / 65536.0
            verts.append((x, y, z))
            all_y.append(y)
            verts_all.append((x, y, z))
        verts_off = off
        off += pts * 12
        polys_off = off
        faces = []
        for fi in range(pol):
            p = polys_off + fi * 20
            i0 = be16(data, p + 12)
            i1 = be16(data, p + 14)
            i2 = be16(data, p + 16)
            i3 = be16(data, p + 18)
            faces.append((i0, i1, i2, i3))
        off = polys_off + pol * 20 + pol * 8
        if typ == 1:
            off += pts * 12

        for i0, i1, i2, i3 in faces:
            if max(i0, i1, i2, i3) >= pts:
                continue
            vs = [verts[i0], verts[i1], verts[i2]]
            if i2 != i3:
                vs.append(verts[i3])
            ax, ay, az = vs[1][0] - vs[0][0], vs[1][1] - vs[0][1], vs[1][2] - vs[0][2]
            bx, by, bz = vs[2][0] - vs[0][0], vs[2][1] - vs[0][1], vs[2][2] - vs[0][2]
            nx = ay * bz - az * by
            ny = az * bx - ax * bz
            nz = ax * by - ay * bx
            nlen = math.sqrt(nx * nx + ny * ny + nz * nz) + 1e-12
            ny /= nlen
            # Ground-ish: mostly vertical normal (Y-down world).
            if abs(ny) < 0.75:
                continue
            ys = [v[1] for v in vs]
            cy = sum(ys) / len(ys)
            cx = sum(v[0] for v in vs) / len(vs)
            cz = sum(v[2] for v in vs) / len(vs)
            best = 0.0
            for a, b in zip(vs, vs[1:] + vs[:1]):
                dx = b[0] - a[0]
                dz = b[2] - a[2]
                dy = b[1] - a[1]
                ds = math.hypot(dx, dz)
                if ds > 1.0:
                    g = dy / ds
                    if abs(g) > abs(best):
                        best = g
            # Keep per-face record; filter to top road band later.
            ground_centers.append((cx, cy, cz, best, min(ys), max(ys)))

    if not verts_all:
        return None

    def st(ys: list[float]):
        return min(ys), sum(ys) / len(ys), max(ys), max(ys) - min(ys)

    # Y-down: top driving surface = faces with smallest Y (highest altitude).
    # Keep faces within 12 units of the topmost ground face to drop walls/ledges.
    road = ground_centers
    if road:
        top_y = min(f[1] for f in road)
        band = 12.0
        road = [f for f in road if f[1] <= top_y + band]
        if len(road) < 2:
            road = sorted(ground_centers, key=lambda f: f[1])[: max(2, len(ground_centers) // 3)]

    ground_y = []
    face_grades = []
    for f in road:
        ground_y.extend([f[4], f[5]])
        face_grades.append(f[3])

    gcy = sum(f[1] for f in road) / len(road) if road else None
    gcx = sum(f[0] for f in road) / len(road) if road else None
    gcz = sum(f[2] for f in road) / len(road) if road else None

    return {
        "verts": len(verts_all),
        "y_all": st(all_y),
        "y_ground": st(ground_y) if ground_y else None,
        "n_ground_faces": len(ground_centers),
        "n_road_faces": len(road),
        "grade_mean": (sum(face_grades) / len(face_grades)) if face_grades else 0.0,
        "grade_max": max(face_grades, key=abs) if face_grades else 0.0,
        "cx": gcx if gcx is not None else sum(v[0] for v in verts_all) / len(verts_all),
        "cy": gcy if gcy is not None else sum(v[1] for v in verts_all) / len(verts_all),
        "cz": gcz if gcz is not None else sum(v[2] for v in verts_all) / len(verts_all),
        "gcy": gcy,
        "road_span": (max(f[1] for f in road) - min(f[1] for f in road)) if road else 0.0,
    }


def main() -> int:
    candidates = [
        Path(r"C:\saturn\SaturnRingLib-main\Projects\Interlagos_racing_old\cd\data\SETORES"),
        Path(r"C:\saturn\SaturnRingLib-main\Projects\pacote_rancing"),
        Path("cd/data"),
        Path("cd/CD/DATA"),
    ]
    use = None
    for c in candidates:
        if (c / "SEG_027.NYA").exists():
            use = c
            break
    if use is None:
        print("No SEG_027.NYA found in known paths")
        return 1

    print("SOURCE", use)
    print(
        f"{'seg':>4} {'roadF':>5} {'yRoad':>9} {'rSpan':>7} "
        f"{'gMean':>8} {'gMax':>8} {'dY':>8} {'tanPath':>8}"
    )
    prev = None
    rows: list[tuple[int, dict]] = []
    for i in range(27, 46):
        path = use / f"SEG_{i:03d}.NYA"
        if not path.exists():
            print(i, "MISSING")
            continue
        r = analyze(path)
        if not r:
            print(i, "fail")
            continue
        cy = r["cy"]
        dyc = 0.0
        tan_path = 0.0
        if prev is not None:
            dyc = cy - prev["cy"]
            dxz = math.hypot(r["cx"] - prev["cx"], r["cz"] - prev["cz"])
            tan_path = (dyc / dxz) if dxz > 1e-3 else 0.0
        rows.append((i, r))
        prev = r
        print(
            f"{i:4d} {r['n_road_faces']:5d} {cy:9.3f} {r['road_span']:7.3f} "
            f"{r['grade_mean']:8.4f} {r['grade_max']:8.4f} {dyc:8.3f} {tan_path:8.4f}"
        )

    print("\nPath grade between segment road centers (Y-down: +tan = downhill along path):")
    jumps = 0
    tans = []
    for a, b in zip(rows, rows[1:]):
        dxz = math.hypot(b[1]["cx"] - a[1]["cx"], b[1]["cz"] - a[1]["cz"])
        dy = b[1]["cy"] - a[1]["cy"]
        tan = dy / dxz if dxz > 1e-3 else 0.0
        tans.append(tan)
        # Residual vs linear trend later; flag discontinuous tan spikes
        flag = ""
        if abs(tan) > 0.45:
            flag = " *** STEEP"
            jumps += 1
        if a[1]["road_span"] > 8.0 or b[1]["road_span"] > 8.0:
            flag += " * multi-band"
        print(
            f"  {a[0]}->{b[0]}: dY={dy:+.3f} dXZ={dxz:.1f} tan={tan:+.4f} "
            f"roadSpan={a[1]['road_span']:.2f}/{b[1]['road_span']:.2f}{flag}"
        )

    if rows:
        c0, c1 = rows[0][1]["cy"], rows[-1][1]["cy"]
        print(f"\nOverall 27->45 road Y: {c0:.3f} -> {c1:.3f}  delta={c1-c0:+.3f}")
        if tans:
            mean_t = sum(tans) / len(tans)
            print(f"Mean path tan: {mean_t:+.4f}  max|tan|: {max(abs(t) for t in tans):.4f}")
            # Second difference of tan = jerk along climb (seam roughness)
            if len(tans) >= 3:
                jerks = [tans[i + 1] - tans[i] for i in range(len(tans) - 1)]
                print(
                    f"Tan deltas (smoothness): mean|d|= {sum(abs(j) for j in jerks)/len(jerks):.4f} "
                    f"max|d|={max(abs(j) for j in jerks):.4f}"
                )
                rough = [
                    (rows[i][0], rows[i + 1][0], jerks[i])
                    for i in range(len(jerks))
                    if abs(jerks[i]) > 0.12
                ]
                if rough:
                    print("Rough tan changes (consider more asphalt subdivision or weld seams):")
                    for a, b, j in rough:
                        print(f"  {a}->{b}: d(tan)={j:+.4f}")
                else:
                    print("Tan changes mostly smooth — bobbing likely camera/physics filters, not slab steps.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
