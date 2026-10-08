#!/usr/bin/env python3
"""F0/F1 audit: LOD0 vertical edges vs mid-lane radial vs mid-corridor across cull.

Confirms video 115458 root cause: fences at ~42u (f01364/f03664) die under
WALL_MID_LANE_CLEAR_RAW=48 but survive WALL_MID_CORRIDOR_RAW=30.
"""
from __future__ import annotations

import json
import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from build_track_collision import (  # noqa: E402
    DRIVEABLE_TYPES,
    WALL_MID_CORRIDOR_RAW,
    WALL_MID_LANE_CLEAR_RAW,
    Face,
    asphalt_aabb_xz,
    asphalt_centroid_xz,
    infer_travel_is_z,
    is_floor,
    normalize_stem,
    read_geo,
    read_mat,
    wall_crosses_mid_lane,
    wall_in_mid_corridor,
    walls_from_face,
)

PKG = Path(r"C:\saturn\SaturnRingLib-main\Projects\pacote_rancing")


def main() -> int:
    root = json.loads((PKG / "segments_map.json").read_text(encoding="utf-8-sig"))
    surface_by_family = {
        int(item["id"]): int(item.get("surfaceTypeId", 0)) for item in root["textureFamilies"]
    }
    family_by_stem = {
        normalize_stem(str(item.get("sourceStem", ""))): int(item["id"])
        for item in root["textureFamilies"]
    }
    stem_by_family = {v: k for k, v in family_by_stem.items()}

    print(
        f"midLaneClear={WALL_MID_LANE_CLEAR_RAW >> 16}u "
        f"midCorridor={WALL_MID_CORRIDOR_RAW >> 16}u"
    )
    for sid in range(1, 11):
        geo = read_geo(PKG / f"S{sid:03d}.GEO", sid)
        mat_path = PKG / f"S{sid:03d}M64.MAT"
        if not mat_path.exists():
            mat_path = PKG / f"S{sid:03d}M32.MAT"
        mat = read_mat(mat_path, sid)
        ground = []
        for idx, (verts, fid) in enumerate(zip(geo, mat)):
            st = surface_by_family.get(fid, 0)
            if st in DRIVEABLE_TYPES and is_floor(verts):
                ground.append(Face(idx, verts, fid, st))
        center = asphalt_centroid_xz(ground)
        bounds = asphalt_aabb_xz(ground)
        if center is None or bounds is None:
            print(f"SEG{sid:03d}: no asphalt")
            continue
        travel_is_z = infer_travel_is_z(*bounds, None)
        rows = []
        for idx, (verts, fid) in enumerate(zip(geo, mat)):
            st = surface_by_family.get(fid, 0)
            if is_floor(verts) or st in DRIVEABLE_TYPES:
                continue
            for wall in walls_from_face(Face(idx, verts, fid, st)):
                mx = (wall.ax + wall.bx) * 0.5
                mz = (wall.az + wall.bz) * 0.5
                dist = math.hypot(mx - center[0], mz - center[1]) / 65536.0
                mid = wall_crosses_mid_lane(wall, center, WALL_MID_LANE_CLEAR_RAW)
                cor = wall_in_mid_corridor(wall, center, travel_is_z, WALL_MID_CORRIDOR_RAW)
                rows.append((dist, mid, cor, stem_by_family.get(fid, "?"), fid))
        rows.sort(key=lambda r: r[0])
        kept_cor = [r for r in rows if not r[2]]
        culled_mid_kept_cor = [r for r in rows if r[1] and not r[2]]
        nearest = rows[0][0] if rows else None
        nearest_kept = kept_cor[0][0] if kept_cor else None
        near_s = f"{nearest:.1f}" if nearest is not None else "-"
        kept_s = f"{nearest_kept:.1f}" if nearest_kept is not None else "-"
        print(
            f"SEG{sid:03d} travel_z={travel_is_z} verts={len(rows)} "
            f"near={near_s} near_after_corridor={kept_s} "
            f"mid_kills_corridor_keeps={len(culled_mid_kept_cor)}"
        )
        for r in culled_mid_kept_cor[:4]:
            print(f"  rescued d={r[0]:5.1f} stem={r[3]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
