#!/usr/bin/env python3
"""Audit LOD0: driveable boundary edges vs wall-stem faces (SEG1-5 sample)."""
from __future__ import annotations

import json
import math
import re
import struct
from collections import defaultdict
from pathlib import Path

GEO_MAGIC = 0x314F4547
VERTEX = struct.Struct("<iii")
DRIVEABLE = frozenset((1, 2, 3))
PKG = Path(r"C:\saturn\SaturnRingLib-main\Projects\pacote_rancing")
MANIFEST = Path("tools/track_collision_manifest.json")
SEGMENTS_MAP = PKG / "segments_map.json"


def normalize_stem(value: str) -> str:
    return Path(value).stem.lower().rstrip("_")


def read_geo(path: Path, segment_id: int):
    data = path.read_bytes()
    magic, version, _, actual_id, payload = struct.unpack_from("<IHHII", data, 0)
    if magic != GEO_MAGIC or actual_id != segment_id:
        raise ValueError(path)
    vertex_count, face_count = struct.unpack_from("<II", data, 16)
    verts = [
        VERTEX.unpack_from(data, 24 + i * VERTEX.size) for i in range(vertex_count)
    ]
    faces = []
    base = 24 + vertex_count * VERTEX.size
    for i in range(face_count):
        # GEO face: matches build_track_collision read_geo
        off = base + i * 28
        i0, i1, i2, i3 = struct.unpack_from("<HHHH", data, off)
        idx = [i0, i1, i2] if i3 == 0xFFFF or i3 == i2 else [i0, i1, i2, i3]
        faces.append(tuple(verts[j] for j in idx))
    return faces


def read_mat(path: Path, segment_id: int) -> list[int]:
    data = path.read_bytes()
    # MAT1 header then family ids per face
    magic, version, _, actual_id, payload = struct.unpack_from("<IHHII", data, 0)
    count = struct.unpack_from("<I", data, 16)[0]
    return list(struct.unpack_from(f"<{count}H", data, 20))


def is_floor(vertices) -> bool:
    if len(vertices) < 3:
        return False
    ax, ay, az = vertices[0]
    bx, by, bz = vertices[1]
    cx, cy, cz = vertices[2]
    nx = (by - ay) * (cz - az) - (bz - az) * (cy - ay)
    ny = (bz - az) * (cx - ax) - (bx - ax) * (cz - az)
    nz = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax)
    length = math.sqrt(nx * nx + ny * ny + nz * nz)
    return length > 0 and abs(ny) / length >= 0.5


def edge_key(a, b):
    return tuple(sorted((a, b)))


def main() -> int:
    root = json.loads(SEGMENTS_MAP.read_text(encoding="utf-8-sig"))
    families = root.get("textureFamilies", [])
    surface_by_family = {int(f["id"]): int(f.get("surfaceTypeId", 0)) for f in families}
    stem_by_family = {
        int(f["id"]): normalize_stem(str(f.get("sourceStem", ""))) for f in families
    }
    man = json.loads(MANIFEST.read_text(encoding="utf-8-sig"))
    wall_stems = {normalize_stem(s) for s in man.get("wallSourceStems", [])}
    wall_fams = {fid for fid, stem in stem_by_family.items() if stem in wall_stems}

    for seg_id in range(1, 6):
        geo = PKG / f"S{seg_id:03d}.GEO"
        mat = PKG / f"S{seg_id:03d}M64.MAT"
        if not mat.exists():
            mat = PKG / f"S{seg_id:03d}M32.MAT"
        faces = read_geo(geo, seg_id)
        fams = read_mat(mat, seg_id)
        edge_driveable = defaultdict(int)
        edge_wallstem = defaultdict(int)
        edge_other_vert = defaultdict(int)
        for face_i, (verts, fam) in enumerate(zip(faces, fams)):
            st = surface_by_family.get(fam, 0)
            floor = is_floor(verts)
            for i in range(len(verts)):
                a = verts[i]
                b = verts[(i + 1) % len(verts)]
                key = edge_key((a[0], a[2]), (b[0], b[2]))
                if floor and st in DRIVEABLE:
                    edge_driveable[key] += 1
                elif (not floor) and fam in wall_fams:
                    edge_wallstem[key] += 1
                elif not floor:
                    edge_other_vert[key] += 1
        boundary = [e for e, c in edge_driveable.items() if c == 1]
        shared_wall = sum(1 for e in boundary if e in edge_wallstem)
        shared_other = sum(1 for e in boundary if e in edge_other_vert and e not in edge_wallstem)
        # stems on other vertical faces near boundary
        other_stems = defaultdict(int)
        for e in boundary:
            if e in edge_other_vert and e not in edge_wallstem:
                other_stems["boundary_other_vert"] += 1
        # count vertical face stems overall
        stem_counts = defaultdict(int)
        for face_i, (verts, fam) in enumerate(zip(faces, fams)):
            if is_floor(verts):
                continue
            stem_counts[stem_by_family.get(fam, f"fam{fam}")] += 1
        print(
            f"SEG{seg_id:03d}: driveable_boundary_edges={len(boundary)} "
            f"shared_wallstem={shared_wall} shared_other_vert={shared_other}"
        )
        top = sorted(stem_counts.items(), key=lambda kv: -kv[1])[:8]
        print("  vertical stems:", ", ".join(f"{s}:{n}" for s, n in top))
        missing = [s for s, n in top if s not in wall_stems and not s.startswith("fam")]
        if missing:
            print("  NOT in wallSourceStems:", ", ".join(missing))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
