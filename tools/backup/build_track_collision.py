#!/usr/bin/env python3
"""Build TCOL.BIN: texture-free LOD0 ground and wall collision geometry."""
from __future__ import annotations

import argparse
import json
import math
import re
import struct
from dataclasses import dataclass
from pathlib import Path


MAGIC = b"TCL1"
VERSION = 1
GRID_DIM = 4
HEADER = struct.Struct("<4sHHHHHHHHHIII")
DIRECTORY = struct.Struct("<HHHHHHiiiiIIIIIII")
VERTEX = struct.Struct("<iii")
GROUND = struct.Struct("<HHHHHBBHH")
WALL = struct.Struct("<iiiiiiiiHH")
CELL = struct.Struct("<IHH")
GEO_MAGIC = 0x314F4547
MAT_MAGIC = 0x3154414D
GEO_FACE_SIZE = 28
DRIVEABLE_TYPES = frozenset((1, 2, 3))
# Keep a wall if any sample is outside the asphalt AABB or within this band of
# a LATERAL border. End (seam) borders alone are not enough to keep a wall.
WALL_EDGE_KEEP_BAND_RAW = 96 << 16
# Legacy radial mid-lane clear (centroid dist). Kept for report/diag only —
# runtime cull uses WALL_MID_CORRIDOR_RAW (across-track) instead.
# Video 115458: radial 48u deleted LOD0 fences at ~42u (f01364/f03664).
WALL_MID_LANE_CLEAR_RAW = 48 << 16
# Drop walls whose closest point to the asphalt centroid has |across| below
# this threshold. Across = axis perpendicular to travel (infer_travel_is_z).
# ~30u keeps lateral fences at ~42u while clearing the racing mid-corridor.
WALL_MID_CORRIDOR_RAW = 30 << 16
# Body half-width. An edge this close to the asphalt centroid is in the car,
# whether it crosses the lane or runs along it. Real side fences on this
# circuit sit at about 26u and further (SEG287 ~25.6u is kept).
WALL_RACING_LINE_CLEAR_RAW = 22 << 16
# Fraction of the cross-track asphalt span treated as the "lateral rim"
# (used only when AABB interior cull is enabled).
WALL_LATERAL_RIM_FRAC = 0.30
# Fraction of the along-track span used to detect segment end seams.
# Slightly wider for S-curve joins (SEG27/28) where AABB ends are irregular.
WALL_SEAM_END_FRAC = 0.15
# Drop wall edges whose Y range does not overlap asphalt Y ± this margin.
# Prevents "Y_DEAD" barriers (often mirrored/underground LOD0 faces) from
# occupying wall cells without ever being hittable at plant height.
WALL_Y_OVERLAP_MARGIN_RAW = 32 << 16
# Minimum XZ length for an emitted wall edge (raw).
WALL_EDGE_MIN_LENGTH_RAW = 8 << 16
# When True, skip the AABB-interior cull. Asphalt AABBs include wide runoff,
# so "interior" false-dropped real guardrails near the racing strip (172530).
# Mid-corridor + seam-cap culls remain the primary free-lane protections.
WALL_DISABLE_AABB_INTERIOR_CULL = True
# Type=1 open-contour edges include asphalt→escape/grass boundaries. Those sit
# inside the visual driveable strip (video 101950: mid-lane SEG28 wh:1 wx:-3).
# Drop a wall when a step outward from the asphalt centroid still lands on
# driveable surface types 1/2/3 — BUT keep edges on the lateral rim band
# (video 112009: void-only walls at ~300u caused total pass-through).
# Inner-driveable cull applies ONLY to type=1 contour walls, not LOD0 verticals.
WALL_OUTWARD_PROBE_RAW = 12 << 16
# Legacy stem list is no longer the allow-list. Walls come from every non-floor
# face whose texture is outside freeTraverseStems.
WALL_ENABLE_STEM_EDGES = False
# Open contour of asphalt (type 1) is ground, not a wall texture. Each segment
# join is an open edge because the neighbor lives in another GEO, so contour
# walls become invisible caps that stop the car. Keep the extractor for diag;
# do not emit it.
WALL_ENABLE_DRIVEABLE_CONTOUR = False
# Non-floor faces block, except the free-traverse stems in the manifest.
# Floors never block (their XZ edges span the circuit and the joins).
WALL_EMIT_ALL_VERTICAL_FACES = True
# Lateral rim band (fraction of asphalt AABB across-span). Inner-driveable
# cull is skipped for walls whose midpoint lies in this outer band.
# ~0.35 so asphalt→escape edges near the visual strip (~69u / t≈0.73 on SEG2)
# survive inner-driveable cull while true mid-strip ghosts still drop.
WALL_INNER_CULL_RIM_KEEP_FRAC = 0.35
# Inner-driveable cull only applies inside this radius of the asphalt centroid.
# Farther edges (true outer / stem guardrails) are never mid-strip ghosts.
WALL_INNER_CULL_MAX_DIST_RAW = 200 << 16


@dataclass(frozen=True)
class Face:
    source_index: int
    vertices: tuple[tuple[int, int, int], ...]
    family_id: int
    surface_type: int


@dataclass(frozen=True)
class WallRecord:
    ax: int
    az: int
    bx: int
    bz: int
    min_y: int
    max_y: int
    nx: int
    nz: int
    family_id: int
    source_index: int


@dataclass
class Segment:
    segment_id: int
    source_face_count: int
    vertices: list[tuple[int, int, int]]
    ground: list[tuple[tuple[int, ...], int, int, int]]
    walls: list[WallRecord]
    bounds: tuple[int, int, int, int]
    ground_cells: list[list[int]]
    wall_cells: list[list[int]]
    duplicate_ground_removed: int
    merged_coplanar_pairs: int
    duplicate_walls_removed: int
    lane_walls_removed: int = 0
    mid_lane_walls_removed: int = 0
    mid_corridor_walls_removed: int = 0
    seam_cap_walls_removed: int = 0
    interior_walls_removed: int = 0
    y_dead_walls_removed: int = 0
    inner_driveable_walls_removed: int = 0
    crossing_walls_removed: int = 0
    driveable_walls_skipped: int = 0
    vertical_faces_emitted: int = 0
    free_faces_skipped: int = 0


def read_geo(path: Path, segment_id: int) -> list[tuple[tuple[int, int, int], ...]]:
    data = path.read_bytes()
    if len(data) < 24:
        raise ValueError(f"GEO muito pequeno: {path}")
    magic, version, _, actual_id, payload = struct.unpack_from("<IHHII", data, 0)
    if magic != GEO_MAGIC or version != 1 or actual_id != segment_id or payload + 16 > len(data):
        raise ValueError(f"cabecalho GEO invalido: {path}")
    vertex_count, face_count = struct.unpack_from("<II", data, 16)
    vertex_offset = 24
    face_offset = vertex_offset + vertex_count * VERTEX.size
    if face_offset + face_count * GEO_FACE_SIZE > len(data):
        raise ValueError(f"faces GEO truncadas: {path}")
    vertices = [VERTEX.unpack_from(data, vertex_offset + i * VERTEX.size) for i in range(vertex_count)]
    faces = []
    for face_index in range(face_count):
        indices = struct.unpack_from("<HHHH", data, face_offset + face_index * GEO_FACE_SIZE)
        if max(indices) >= vertex_count:
            raise ValueError(f"indice invalido: {path}, face={face_index}")
        if indices[2] == indices[3]:
            indices = indices[:3]
        faces.append(tuple(vertices[index] for index in indices))
    return faces


def read_mat(path: Path, segment_id: int) -> list[int]:
    data = path.read_bytes()
    if len(data) < 20:
        raise ValueError(f"MAT muito pequeno: {path}")
    magic, version, _, actual_id, payload = struct.unpack_from("<IHHII", data, 0)
    if magic != MAT_MAGIC or version != 1 or actual_id != segment_id or payload + 16 > len(data):
        raise ValueError(f"cabecalho MAT invalido: {path}")
    count = struct.unpack_from("<I", data, 16)[0]
    if 20 + count * 4 > len(data):
        raise ValueError(f"bindings MAT truncados: {path}")
    return [struct.unpack_from("<I", data, 20 + i * 4)[0] for i in range(count)]


def normal(vertices: tuple[tuple[int, int, int], ...]) -> tuple[int, int, int]:
    ax, ay, az = vertices[0]
    bx, by, bz = vertices[1]
    cx, cy, cz = vertices[2]
    ux, uy, uz = bx - ax, by - ay, bz - az
    vx, vy, vz = cx - ax, cy - ay, cz - az
    return uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx


def is_floor(vertices: tuple[tuple[int, int, int], ...]) -> bool:
    nx, ny, nz = normal(vertices)
    length = math.sqrt(nx * nx + ny * ny + nz * nz)
    return length > 0.0 and abs(ny) / length >= 0.5


def canonical_polygon(vertices: tuple[tuple[int, int, int], ...]) -> tuple[tuple[int, int, int], ...]:
    variants = []
    for sequence in (vertices, tuple(reversed(vertices))):
        variants.extend(sequence[i:] + sequence[:i] for i in range(len(sequence)))
    return min(variants)


def polygon_area_xz(vertices: tuple[tuple[int, int, int], ...]) -> int:
    return abs(sum(
        vertices[i][0] * vertices[(i + 1) % len(vertices)][2]
        - vertices[(i + 1) % len(vertices)][0] * vertices[i][2]
        for i in range(len(vertices))
    ))


def try_merge_triangles(a: Face, b: Face) -> Face | None:
    if a.family_id != b.family_id or a.surface_type != b.surface_type:
        return None
    shared = set(a.vertices) & set(b.vertices)
    union = set(a.vertices) | set(b.vertices)
    if len(shared) != 2 or len(union) != 4:
        return None
    na = normal(a.vertices)
    nb = normal(b.vertices)
    la = math.sqrt(sum(value * value for value in na))
    lb = math.sqrt(sum(value * value for value in nb))
    if la == 0.0 or lb == 0.0 or abs(sum(x * y for x, y in zip(na, nb))) / (la * lb) < 0.999999:
        return None
    ox, oy, oz = a.vertices[0]
    if any(abs(na[0] * (x - ox) + na[1] * (y - oy) + na[2] * (z - oz)) > la * 64.0 for x, y, z in b.vertices):
        return None
    cx = sum(point[0] for point in union) / 4.0
    cz = sum(point[2] for point in union) / 4.0
    ordered = tuple(sorted(union, key=lambda point: math.atan2(point[2] - cz, point[0] - cx)))
    if polygon_area_xz(ordered) != polygon_area_xz(a.vertices) + polygon_area_xz(b.vertices):
        return None
    return Face(min(a.source_index, b.source_index), ordered, a.family_id, a.surface_type)


def simplify_ground(faces: list[Face]) -> tuple[list[Face], int, int]:
    unique: list[Face] = []
    seen = set()
    for face in faces:
        key = (face.family_id, face.surface_type, canonical_polygon(face.vertices))
        if key not in seen:
            seen.add(key)
            unique.append(face)
    duplicate_count = len(faces) - len(unique)
    edge_to_faces: dict[tuple[tuple[int, int, int], tuple[int, int, int]], list[int]] = {}
    for index, face in enumerate(unique):
        if len(face.vertices) != 3:
            continue
        for i in range(3):
            edge = tuple(sorted((face.vertices[i], face.vertices[(i + 1) % 3])))
            edge_to_faces.setdefault(edge, []).append(index)
    consumed: set[int] = set()
    replacements: dict[int, Face] = {}
    merged = 0
    for candidates in edge_to_faces.values():
        if len(candidates) != 2:
            continue
        left, right = candidates
        if left in consumed or right in consumed:
            continue
        combined = try_merge_triangles(unique[left], unique[right])
        if combined is not None:
            consumed.update((left, right))
            replacements[left] = combined
            merged += 1
    output = []
    for index, face in enumerate(unique):
        if index in replacements:
            output.append(replacements[index])
        elif index not in consumed:
            output.append(face)
    return output, duplicate_count, merged


def _wall_record_from_endpoints(
    a: tuple[int, int, int],
    b: tuple[int, int, int],
    family_id: int,
    source_index: int,
    min_y: int,
    max_y: int,
) -> WallRecord | None:
    dx, dz = b[0] - a[0], b[2] - a[2]
    length = math.hypot(dx, dz)
    if length < float(WALL_EDGE_MIN_LENGTH_RAW):
        return None
    nx = int(round((-dz / length) * 65536.0))
    nz = int(round((dx / length) * 65536.0))
    ax, az = a[0], a[2]
    bx, bz = b[0], b[2]
    if (ax, az) > (bx, bz):
        ax, az, bx, bz = bx, bz, ax, az
        nx, nz = -nx, -nz
    return WallRecord(ax, az, bx, bz, min_y, max_y, nx, nz, family_id, source_index)


def wall_from_face(face: Face) -> WallRecord | None:
    """Legacy: single longest XZ edge (kept for callers/tests)."""
    records = walls_from_face(face)
    if not records:
        return None
    return max(
        records,
        key=lambda w: (w.bx - w.ax) * (w.bx - w.ax) + (w.bz - w.az) * (w.bz - w.az),
    )


def walls_from_driveable_boundary(ground_faces: list[Face]) -> list[WallRecord]:
    """Emit wall segments on the open contour of asphalt (type 1) floor faces.

    Open type-1 edges include asphalt→escape (~69u, visual rim) and asphalt→void
    (~300u). Mid-lane ghosts from asphalt→escape are filtered later by mid-lane
    clear + inner-driveable with lateral-rim keep (101950 / 112009).
    """
    edge_uses: dict[tuple[tuple[int, int], tuple[int, int]], list[tuple]] = {}
    for face in ground_faces:
        if face.surface_type != 1:
            continue
        verts = face.vertices
        count = len(verts)
        if count < 3:
            continue
        fcx = sum(v[0] for v in verts) / count
        fcz = sum(v[2] for v in verts) / count
        for i in range(count):
            a = verts[i]
            b = verts[(i + 1) % count]
            ka = (a[0], a[2])
            kb = (b[0], b[2])
            key = (ka, kb) if ka <= kb else (kb, ka)
            edge_uses.setdefault(key, []).append((a, b, fcx, fcz))

    walls: list[WallRecord] = []
    # Body height pad so plant Y overlaps the edge (flat asphalt Y alone is thin).
    y_pad = 8 << 16
    for uses in edge_uses.values():
        if len(uses) != 1:
            continue
        a, b, fcx, fcz = uses[0]
        dx = b[0] - a[0]
        dz = b[2] - a[2]
        length = math.hypot(dx, dz)
        if length < float(WALL_EDGE_MIN_LENGTH_RAW):
            continue
        nx = -dz / length
        nz = dx / length
        mx = (a[0] + b[0]) * 0.5
        mz = (a[2] + b[2]) * 0.5
        # Outward = away from face interior.
        if nx * (fcx - mx) + nz * (fcz - mz) > 0.0:
            nx, nz = -nx, -nz
        ax, az = a[0], a[2]
        bx, bz = b[0], b[2]
        nnx = int(round(nx * 65536.0))
        nnz = int(round(nz * 65536.0))
        if (ax, az) > (bx, bz):
            ax, az, bx, bz = bx, bz, ax, az
            nnx, nnz = -nnx, -nnz
        min_y = min(a[1], b[1]) - (2 << 16)
        max_y = max(a[1], b[1]) + y_pad
        walls.append(
            WallRecord(ax, az, bx, bz, min_y, max_y, nnx, nnz, 0, 0)
        )
    return walls


def walls_from_face(face: Face) -> list[WallRecord]:
    """Emit vertical wall edges from a non-floor wall-stem face.

    Uses every boundary edge with enough XZ length so continuous guardrails
    are not collapsed to a single longest edge (pass-through gaps).
    """
    if is_floor(face.vertices):
        return []
    verts = face.vertices
    if len(verts) < 2:
        return []
    min_y = min(v[1] for v in verts)
    max_y = max(v[1] for v in verts)
    records: list[WallRecord] = []
    count = len(verts)
    for i in range(count):
        a = verts[i]
        b = verts[(i + 1) % count]
        # Prefer mostly-horizontal edges in XZ (skip near-vertical risers).
        dx = b[0] - a[0]
        dy = b[1] - a[1]
        dz = b[2] - a[2]
        xz_len = math.hypot(dx, dz)
        if xz_len < float(WALL_EDGE_MIN_LENGTH_RAW):
            continue
        # Drop edges that are mostly vertical in 3D (riser), keep fence lines.
        if abs(dy) > xz_len * 2.0:
            continue
        rec = _wall_record_from_endpoints(a, b, face.family_id, face.source_index, min_y, max_y)
        if rec is not None:
            records.append(rec)
    # Fallback: longest chord if boundary walk yielded nothing.
    if not records:
        pairs = [
            (a, b)
            for i, a in enumerate(verts)
            for b in verts[i + 1 :]
        ]
        if pairs:
            a, b = max(
                pairs,
                key=lambda pair: (pair[1][0] - pair[0][0]) ** 2
                + (pair[1][2] - pair[0][2]) ** 2,
            )
            rec = _wall_record_from_endpoints(
                a, b, face.family_id, face.source_index, min_y, max_y
            )
            if rec is not None:
                records.append(rec)
    return records


def asphalt_y_range(faces: list[Face]) -> tuple[int, int] | None:
    ys = [v[1] for face in faces if face.surface_type == 1 for v in face.vertices]
    if not ys:
        ys = [v[1] for face in faces for v in face.vertices]
    if not ys:
        return None
    return min(ys), max(ys)


def wall_y_overlaps_asphalt(
    wall: WallRecord, asphalt_y: tuple[int, int], margin: int = WALL_Y_OVERLAP_MARGIN_RAW
) -> bool:
    a_min, a_max = asphalt_y
    return not (wall.max_y < (a_min - margin) or wall.min_y > (a_max + margin))


def wall_distance_to_point_xz(wall: WallRecord, px: float, pz: float) -> float:
    abx = float(wall.bx - wall.ax)
    abz = float(wall.bz - wall.az)
    ab2 = abx * abx + abz * abz
    if ab2 <= 0.0:
        return math.hypot(px - wall.ax, pz - wall.az)
    t = max(0.0, min(1.0, ((px - wall.ax) * abx + (pz - wall.az) * abz) / ab2))
    qx = wall.ax + t * abx
    qz = wall.az + t * abz
    return math.hypot(px - qx, pz - qz)


def ground_centroid_xz(faces: list[Face]) -> tuple[float, float] | None:
    if not faces:
        return None
    sx = 0.0
    sz = 0.0
    count = 0
    for face in faces:
        for x, _y, z in face.vertices:
            sx += float(x)
            sz += float(z)
            count += 1
    if count <= 0:
        return None
    return sx / count, sz / count


def asphalt_centroid_xz(faces: list[Face]) -> tuple[float, float] | None:
    asphalt = [face for face in faces if face.surface_type == 1]
    return ground_centroid_xz(asphalt) if asphalt else ground_centroid_xz(faces)


def asphalt_aabb_xz(faces: list[Face]) -> tuple[int, int, int, int] | None:
    asphalt = [face for face in faces if face.surface_type == 1] or faces
    xs: list[int] = []
    zs: list[int] = []
    for face in asphalt:
        for x, _y, z in face.vertices:
            xs.append(int(x))
            zs.append(int(z))
    if not xs:
        return None
    return min(xs), max(xs), min(zs), max(zs)


def wall_is_deep_inside_asphalt(
    wall: WallRecord, min_x: int, max_x: int, min_z: int, max_z: int, edge_band: int
) -> bool:
    """True when every sample is inside the AABB and farther than edge_band from the border."""
    samples = (
        (wall.ax, wall.az),
        (wall.bx, wall.bz),
        ((wall.ax + wall.bx) // 2, (wall.az + wall.bz) // 2),
    )
    for x, z in samples:
        if x < min_x or x > max_x or z < min_z or z > max_z:
            return False
        border = min(x - min_x, max_x - x, z - min_z, max_z - z)
        if border <= edge_band:
            return False
    return True


def wall_closest_dist_sq_to_point(wall: WallRecord, cx: float, cz: float) -> float:
    """Squared distance from (cx,cz) to the closest point on the finite wall segment."""
    abx = float(wall.bx - wall.ax)
    abz = float(wall.bz - wall.az)
    ab2 = abx * abx + abz * abz
    if ab2 <= 0.0:
        dx = cx - float(wall.ax)
        dz = cz - float(wall.az)
        return dx * dx + dz * dz
    t = ((cx - float(wall.ax)) * abx + (cz - float(wall.az)) * abz) / ab2
    if t < 0.0:
        t = 0.0
    elif t > 1.0:
        t = 1.0
    qx = float(wall.ax) + t * abx
    qz = float(wall.az) + t * abz
    dx = cx - qx
    dz = cz - qz
    return dx * dx + dz * dz


def wall_crosses_mid_lane(
    wall: WallRecord, centroid_xz: tuple[float, float], clear_radius_raw: int
) -> bool:
    """True when the wall segment passes inside clear_radius of the asphalt centroid.

    Legacy radial cull — prefer wall_in_mid_corridor for free-lane filtering.
    """
    cx, cz = centroid_xz
    clear_sq = float(clear_radius_raw) * float(clear_radius_raw)
    return wall_closest_dist_sq_to_point(wall, cx, cz) < clear_sq


def wall_closest_point_xz(
    wall: WallRecord, cx: float, cz: float
) -> tuple[float, float]:
    """Closest XZ point on the finite wall segment to (cx, cz)."""
    abx = float(wall.bx - wall.ax)
    abz = float(wall.bz - wall.az)
    ab2 = abx * abx + abz * abz
    if ab2 <= 0.0:
        return float(wall.ax), float(wall.az)
    t = ((cx - float(wall.ax)) * abx + (cz - float(wall.az)) * abz) / ab2
    if t < 0.0:
        t = 0.0
    elif t > 1.0:
        t = 1.0
    return float(wall.ax) + t * abx, float(wall.az) + t * abz


def wall_in_mid_corridor(
    wall: WallRecord,
    centroid_xz: tuple[float, float],
    travel_is_z: bool,
    corridor_raw: int = WALL_MID_CORRIDOR_RAW,
) -> bool:
    """True when the wall's closest point to the centroid sits in the mid-corridor.

    Across-track axis is X when travel is Z, else Z. Unlike radial mid-lane
    clear, a lateral fence at across≈42u survives corridor=30u (video 115458).
    """
    cx, cz = centroid_xz
    qx, qz = wall_closest_point_xz(wall, cx, cz)
    if travel_is_z:
        across = abs(qx - cx)
    else:
        across = abs(qz - cz)
    return across < float(corridor_raw)


def _point_in_face_xz(px: float, pz: float, face: Face) -> bool:
    verts = face.vertices
    count = len(verts)
    if count < 3:
        return False

    def tri(a: tuple[int, int, int], b: tuple[int, int, int], c: tuple[int, int, int]) -> bool:
        x1, z1 = float(a[0]), float(a[2])
        x2, z2 = float(b[0]), float(b[2])
        x3, z3 = float(c[0]), float(c[2])
        det = (z2 - z3) * (x1 - x3) + (x3 - x2) * (z1 - z3)
        if abs(det) < 1e-9:
            return False
        l1 = ((z2 - z3) * (px - x3) + (x3 - x2) * (pz - z3)) / det
        l2 = ((z3 - z1) * (px - x3) + (x1 - x3) * (pz - z3)) / det
        l3 = 1.0 - l1 - l2
        return l1 >= -1e-4 and l2 >= -1e-4 and l3 >= -1e-4

    if count == 3:
        return tri(verts[0], verts[1], verts[2])
    # Fan for quads / n-gons.
    for i in range(1, count - 1):
        if tri(verts[0], verts[i], verts[i + 1]):
            return True
    return False


def wall_borders_inner_driveable(
    wall: WallRecord,
    asphalt_center: tuple[float, float],
    ground_faces: list[Face],
    step_raw: int = WALL_OUTWARD_PROBE_RAW,
) -> bool:
    """True when the wall is an asphalt→escape/grass (inner) contour, not outer void.

    Step from the wall midpoint away from the asphalt centroid. If that probe
    still hits a driveable face (types 1/2/3), the edge is inside the visual
    runoff strip and must not block mid-lane travel.
    """
    cx, cz = asphalt_center
    mx = (wall.ax + wall.bx) * 0.5
    mz = (wall.az + wall.bz) * 0.5
    ox = mx - cx
    oz = mz - cz
    olen = math.hypot(ox, oz)
    if olen <= 1.0:
        # Degenerate: fall back to stored normal direction.
        nlen = math.hypot(float(wall.nx), float(wall.nz))
        if nlen <= 0.0:
            return False
        ox, oz = float(wall.nx) / nlen, float(wall.nz) / nlen
        # Prefer the direction that increases distance from centroid.
        if ox * (mx - cx) + oz * (mz - cz) < 0.0:
            ox, oz = -ox, -oz
    else:
        ox, oz = ox / olen, oz / olen
    px = mx + ox * float(step_raw)
    pz = mz + oz * float(step_raw)
    for face in ground_faces:
        if face.surface_type not in DRIVEABLE_TYPES:
            continue
        if _point_in_face_xz(px, pz, face):
            return True
    return False


def wall_on_lateral_rim(
    wall: WallRecord,
    min_x: int,
    max_x: int,
    min_z: int,
    max_z: int,
    travel_is_z: bool,
    rim_frac: float = WALL_INNER_CULL_RIM_KEEP_FRAC,
) -> bool:
    """True when wall midpoint sits in the outer lateral band of the asphalt AABB.

    Used to keep guardrail-adjacent contour/stem edges that the inner-driveable
    probe would otherwise drop (112009 void-only pass-through).
    """
    mx = (wall.ax + wall.bx) * 0.5
    mz = (wall.az + wall.bz) * 0.5
    if travel_is_z:
        span = max_x - min_x
        if span <= 0:
            return False
        t = (mx - min_x) / float(span)
    else:
        span = max_z - min_z
        if span <= 0:
            return False
        t = (mz - min_z) / float(span)
    return t <= rim_frac or t >= (1.0 - rim_frac)


def reorient_wall_outward(
    wall: WallRecord, asphalt_center: tuple[float, float]
) -> WallRecord:
    """Ensure wall normal points away from the asphalt centroid."""
    cx, cz = asphalt_center
    mx = (wall.ax + wall.bx) * 0.5
    mz = (wall.az + wall.bz) * 0.5
    # Dot(normal, mid - centroid) should be > 0 for outward.
    dot = float(wall.nx) * (mx - cx) + float(wall.nz) * (mz - cz)
    if dot >= 0.0:
        return wall
    return WallRecord(
        wall.ax,
        wall.az,
        wall.bx,
        wall.bz,
        wall.min_y,
        wall.max_y,
        -wall.nx,
        -wall.nz,
        wall.family_id,
        wall.source_index,
    )


def wall_closest_point_xz(
    wall: WallRecord, cx: float, cz: float
) -> tuple[float, float]:
    abx = float(wall.bx - wall.ax)
    abz = float(wall.bz - wall.az)
    ab2 = abx * abx + abz * abz
    if ab2 <= 0.0:
        return float(wall.ax), float(wall.az)
    t = ((cx - float(wall.ax)) * abx + (cz - float(wall.az)) * abz) / ab2
    if t < 0.0:
        t = 0.0
    elif t > 1.0:
        t = 1.0
    return float(wall.ax) + t * abx, float(wall.az) + t * abz


def infer_travel_is_z(
    min_x: int,
    max_x: int,
    min_z: int,
    max_z: int,
    neighbor_delta: tuple[float, float] | None = None,
) -> bool:
    """Infer along-track axis.

    Prefer the vector between neighboring asphalt centroids when available.
    Fallback: the *shorter* AABB span is usually segment advance; runoff often
    makes the cross-track span larger (Interlagos LOD0 slices).
    """
    if neighbor_delta is not None:
        dx, dz = neighbor_delta
        if abs(dx) + abs(dz) > 0.0:
            return abs(dz) >= abs(dx)
    xspan = max(1, max_x - min_x)
    zspan = max(1, max_z - min_z)
    return zspan <= xspan


def wall_crosses_racing_line(
    wall: WallRecord,
    centroid_xz: tuple[float, float],
    travel: tuple[float, float],
    corridor_raw: int = WALL_MID_CORRIDOR_RAW,
    across_span_raw: int = 40 << 16,
) -> bool:
    """Drop stem chords that cut the racing line.

    A guard-rail face sometimes has one vertex near the lane and the others
    hundreds of units off the circuit. The resulting edge crosses the asphalt
    even though the visible fence stays on the shoulder. A real lateral fence
    keeps both endpoints on the same side, a few dozen units out, with almost
    no across-track span.
    """
    cx, cz = centroid_xz
    tx, tz = travel
    length = math.hypot(tx, tz)
    if length <= 0.0:
        return False
    tx /= length
    tz /= length

    def across(x: float, z: float) -> float:
        return (x - cx) * (-tz) + (z - cz) * tx

    a = across(wall.ax, wall.az)
    b = across(wall.bx, wall.bz)
    dist = wall_distance_to_point_xz(wall, cx, cz)
    if dist < float(WALL_RACING_LINE_CLEAR_RAW):
        return True
    if a * b < 0.0:
        return True
    if abs(a - b) <= float(across_span_raw):
        return False
    return dist < float(corridor_raw)


def wall_is_seam_transverse_cap(
    wall: WallRecord,
    min_x: int,
    max_x: int,
    min_z: int,
    max_z: int,
    travel_is_z: bool,
    neighbor_delta: tuple[float, float] | None = None,
    seam_frac: float = WALL_SEAM_END_FRAC,
) -> bool:
    """True for walls that sit on a segment end and run across the lane.

    Asphalt contour edges at S00n↔S00n+1 joins become invisible transverse caps
    (e.g. SEG27/28 S-curve). Detect by:
      1) edge geometry near AABB end + spans cross-track, or
      2) wall normal mostly aligned with along-track travel (blocks the path).
    """
    xspan = max(1, max_x - min_x)
    zspan = max(1, max_z - min_z)
    abx = wall.bx - wall.ax
    abz = wall.bz - wall.az
    seam_z = int(seam_frac * zspan)
    seam_x = int(seam_frac * xspan)
    near_z_end = (
        min(wall.az, wall.bz) <= min_z + seam_z
        or max(wall.az, wall.bz) >= max_z - seam_z
    )
    near_x_end = (
        min(wall.ax, wall.bx) <= min_x + seam_x
        or max(wall.ax, wall.bx) >= max_x - seam_x
    )
    if travel_is_z:
        if near_z_end and abs(abx) > (abs(abz) * 6) // 5:
            return True
    else:
        if near_x_end and abs(abz) > (abs(abx) * 6) // 5:
            return True

    # Normal-vs-travel: caps face along the path (blocks forward), laterals face sideways.
    tx = 0.0
    tz = 1.0 if travel_is_z else 0.0
    if not travel_is_z:
        tx, tz = 1.0, 0.0
    if neighbor_delta is not None:
        ndx, ndz = neighbor_delta
        nlen = math.hypot(ndx, ndz)
        if nlen > 0.0:
            tx, tz = ndx / nlen, ndz / nlen
    nlen = math.hypot(float(wall.nx), float(wall.nz))
    if nlen <= 0.0:
        return False
    nx = float(wall.nx) / nlen
    nz = float(wall.nz) / nlen
    align = abs(nx * tx + nz * tz)
    # Along-track normal whose midpoint sits in the travel-end band. Endpoint
    # contact is not enough: a side rail runs the whole segment and touches both
    # ends, but its midpoint stays in the middle. Threshold 0.70 keeps a sideways
    # normal even when that midpoint is near the join.
    mid_x = (wall.ax + wall.bx) * 0.5
    mid_z = (wall.az + wall.bz) * 0.5
    mid_near_z = mid_z <= (min_z + seam_z) or mid_z >= (max_z - seam_z)
    mid_near_x = mid_x <= (min_x + seam_x) or mid_x >= (max_x - seam_x)
    near_travel_end = mid_near_z if (abs(tz) >= abs(tx)) else mid_near_x
    if align >= 0.70 and near_travel_end:
        return True
    return False


def wall_crosses_asphalt_interior(
    wall: WallRecord,
    min_x: int,
    max_x: int,
    min_z: int,
    max_z: int,
    centroid_xz: tuple[float, float],
    travel_is_z: bool,
    edge_band: int = WALL_EDGE_KEEP_BAND_RAW,
    lateral_frac: float = WALL_LATERAL_RIM_FRAC,
) -> bool:
    """True when the wall cuts across driveable asphalt away from lateral rims.

    Principle: movement over LOD0 ground must be free. A wall is only kept when
    it lies outside the asphalt AABB or on a lateral rim. End-seam contact alone
    does not count as a lateral barrier.
    """
    xspan = max(1, max_x - min_x)
    zspan = max(1, max_z - min_z)
    cross_span = xspan if travel_is_z else zspan
    lateral_band = max(edge_band, int(lateral_frac * cross_span))

    # Fast path: closest approach to asphalt centroid is in the interior.
    cx, cz = centroid_xz
    qx, qz = wall_closest_point_xz(wall, cx, cz)
    if min_x <= qx <= max_x and min_z <= qz <= max_z:
        if travel_is_z:
            dist_lateral = min(qx - min_x, max_x - qx)
        else:
            dist_lateral = min(qz - min_z, max_z - qz)
        if dist_lateral > lateral_band:
            return True

    # Sample path: any interior sample far from both lateral borders → drop.
    samples = (
        (wall.ax, wall.az),
        (wall.bx, wall.bz),
        ((wall.ax + wall.bx) // 2, (wall.az + wall.bz) // 2),
    )
    interior_hits = 0
    for x, z in samples:
        if x < min_x or x > max_x or z < min_z or z > max_z:
            continue
        if travel_is_z:
            dist_lateral = min(x - min_x, max_x - x)
        else:
            dist_lateral = min(z - min_z, max_z - z)
        if dist_lateral > lateral_band:
            interior_hits += 1
    return interior_hits >= 2


def cell_members(bounds: tuple[int, int, int, int], boxes: list[tuple[int, int, int, int]]) -> list[list[int]]:
    min_x, max_x, min_z, max_z = bounds
    span_x = max(1, max_x - min_x + 1)
    span_z = max(1, max_z - min_z + 1)
    cells = [[] for _ in range(GRID_DIM * GRID_DIM)]
    for index, (box_min_x, box_max_x, box_min_z, box_max_z) in enumerate(boxes):
        x0 = max(0, min(GRID_DIM - 1, ((box_min_x - min_x) * GRID_DIM) // span_x))
        x1 = max(0, min(GRID_DIM - 1, ((box_max_x - min_x) * GRID_DIM) // span_x))
        z0 = max(0, min(GRID_DIM - 1, ((box_min_z - min_z) * GRID_DIM) // span_z))
        z1 = max(0, min(GRID_DIM - 1, ((box_max_z - min_z) * GRID_DIM) // span_z))
        for z in range(z0, z1 + 1):
            for x in range(x0, x1 + 1):
                cells[z * GRID_DIM + x].append(index)
    return cells


def build_segment(
    segment_id: int,
    geometry_dir: Path,
    surface_by_family: dict[int, int],
    free_families: set[int],
    neighbor_delta: tuple[float, float] | None = None,
) -> Segment:
    geo_path = geometry_dir / f"S{segment_id:03d}.GEO"
    mat_path = geometry_dir / f"S{segment_id:03d}M64.MAT"
    if not mat_path.exists():
        mat_path = geometry_dir / f"S{segment_id:03d}M32.MAT"
    source_faces = read_geo(geo_path, segment_id)
    family_ids = read_mat(mat_path, segment_id)
    if len(source_faces) != len(family_ids):
        raise ValueError(f"SEG {segment_id:03d}: GEO={len(source_faces)} MAT={len(family_ids)}")
    ground_source = []
    wall_source = []
    driveable_walls_skipped = 0
    vertical_faces_emitted = 0
    free_faces_skipped = 0
    for index, (vertices, family_id) in enumerate(zip(source_faces, family_ids)):
        surface_type = surface_by_family.get(family_id, 0)
        face = Face(index, vertices, family_id, surface_type)
        floor = is_floor(vertices)
        # Named free textures (asphalt, grass, escape, zebra, pit, f07364, …)
        # never block, including vertical faces. Driveable floors stay ground.
        if family_id in free_families:
            if floor and surface_type in DRIVEABLE_TYPES:
                ground_source.append(face)
            elif floor:
                # f07364 is surface type 0 and entirely floor. Wheels only plant
                # on types 1/2/3, so keep it as grass or the car loses support.
                ground_source.append(Face(index, vertices, family_id, 3))
            free_faces_skipped += 1
            continue
        if surface_type in DRIVEABLE_TYPES and floor:
            ground_source.append(face)
            continue
        # Horizontal faces are not side walls. Emitting their XZ edges recreates
        # lane-crossing and join caps.
        if floor:
            continue
        recs = walls_from_face(face)
        if recs:
            vertical_faces_emitted += 1
            wall_source.extend(recs)
    ground_faces, removed_ground, merged = simplify_ground(ground_source)
    # Blockers are non-floor faces outside the free-texture list. Asphalt
    # contour edges are ground and include the open join between segments.
    boundary_walls = (
        walls_from_driveable_boundary(ground_faces)
        if WALL_ENABLE_DRIVEABLE_CONTOUR
        else []
    )
    candidate_walls = list(boundary_walls)
    if WALL_EMIT_ALL_VERTICAL_FACES or WALL_ENABLE_STEM_EDGES:
        candidate_walls.extend(wall_source)
    unique_walls = []
    wall_keys = set()
    for wall in candidate_walls:
        key = (wall.ax, wall.az, wall.bx, wall.bz, wall.min_y, wall.max_y, wall.family_id)
        if key not in wall_keys:
            wall_keys.add(key)
            unique_walls.append(wall)
    duplicate_walls_removed = max(0, len(candidate_walls) - len(unique_walls))

    # Driveable asphalt must stay free at the mid-corridor and segment seams.
    # Cull by across-track corridor (not radial centroid dist): radial 48u
    # deleted LOD0 fences at ~42u (115458). Do NOT use wide AABB interior culls.
    lane_walls_removed = 0
    mid_lane_walls_removed = 0
    mid_corridor_walls_removed = 0
    seam_cap_walls_removed = 0
    interior_walls_removed = 0
    y_dead_walls_removed = 0
    inner_driveable_walls_removed = 0
    crossing_walls_removed = 0
    asphalt_bounds = asphalt_aabb_xz(ground_faces)
    asphalt_center = asphalt_centroid_xz(ground_faces)
    asphalt_y = asphalt_y_range(ground_faces)
    if asphalt_bounds is not None or asphalt_center is not None or asphalt_y is not None:
        min_x = max_x = min_z = max_z = 0
        if asphalt_bounds is not None:
            min_x, max_x, min_z, max_z = asphalt_bounds
        travel_is_z = True
        if asphalt_bounds is not None:
            travel_is_z = infer_travel_is_z(
                min_x, max_x, min_z, max_z, neighbor_delta
            )
        cleared_walls = []
        for wall in unique_walls:
            if asphalt_y is not None and not wall_y_overlaps_asphalt(wall, asphalt_y):
                y_dead_walls_removed += 1
                continue
            if asphalt_center is not None:
                if neighbor_delta is not None and (abs(neighbor_delta[0]) + abs(neighbor_delta[1])) > 0.0:
                    travel = neighbor_delta
                elif travel_is_z:
                    travel = (0.0, 1.0)
                else:
                    travel = (1.0, 0.0)
                if wall_crosses_racing_line(wall, asphalt_center, travel):
                    crossing_walls_removed += 1
                    continue
            # Across-track mid-corridor ONLY for type=1 contour walls (family_id==0).
            # Video 130219: corridor=30u deleted real LOD0 fences at ~25.6u on
            # SEG287 (f03664). Vertical faces are visual barriers — never corridor-cull.
            is_contour = wall.family_id == 0
            if (
                is_contour
                and asphalt_center is not None
                and wall_in_mid_corridor(
                    wall, asphalt_center, travel_is_z, WALL_MID_CORRIDOR_RAW
                )
            ):
                mid_corridor_walls_removed += 1
                # Keep legacy counter for report continuity when radial would
                # also have fired (diag scripts still key midLaneWallsRemoved).
                if wall_crosses_mid_lane(wall, asphalt_center, WALL_MID_LANE_CLEAR_RAW):
                    mid_lane_walls_removed += 1
                continue
            if asphalt_bounds is not None and wall_is_seam_transverse_cap(
                wall, min_x, max_x, min_z, max_z, travel_is_z, neighbor_delta
            ):
                seam_cap_walls_removed += 1
                continue
            # Inner-driveable cull ONLY on type=1 contour walls.
            # Real LOD0 verticals/stems must not be dropped as "asphalt→escape".
            if (
                is_contour
                and asphalt_center is not None
                and wall_borders_inner_driveable(wall, asphalt_center, ground_faces)
            ):
                mx = (wall.ax + wall.bx) * 0.5
                mz = (wall.az + wall.bz) * 0.5
                dist_c = math.hypot(mx - asphalt_center[0], mz - asphalt_center[1])
                on_rim = False
                if asphalt_bounds is not None:
                    on_rim = wall_on_lateral_rim(
                        wall, min_x, max_x, min_z, max_z, travel_is_z
                    )
                if (not on_rim) and dist_c < float(WALL_INNER_CULL_MAX_DIST_RAW):
                    inner_driveable_walls_removed += 1
                    continue
            if not WALL_DISABLE_AABB_INTERIOR_CULL:
                if (
                    asphalt_bounds is not None
                    and asphalt_center is not None
                    and wall_crosses_asphalt_interior(
                        wall, min_x, max_x, min_z, max_z, asphalt_center, travel_is_z
                    )
                ):
                    interior_walls_removed += 1
                    continue
                if asphalt_bounds is not None and wall_is_deep_inside_asphalt(
                    wall, min_x, max_x, min_z, max_z, WALL_EDGE_KEEP_BAND_RAW
                ):
                    lane_walls_removed += 1
                    continue
            if asphalt_center is not None:
                wall = reorient_wall_outward(wall, asphalt_center)
            cleared_walls.append(wall)
        unique_walls = cleared_walls

    vertex_list: list[tuple[int, int, int]] = []
    vertex_index: dict[tuple[int, int, int], int] = {}
    ground = []
    for face in ground_faces:
        indices = []
        for vertex in face.vertices:
            if vertex not in vertex_index:
                if len(vertex_list) >= 65535:
                    raise ValueError(f"SEG {segment_id:03d}: vertices de colisao excedem uint16")
                vertex_index[vertex] = len(vertex_list)
                vertex_list.append(vertex)
            indices.append(vertex_index[vertex])
        ground.append((tuple(indices), face.family_id, face.surface_type, face.source_index))
    points_x = [v[0] for v in vertex_list] + [p for w in unique_walls for p in (w.ax, w.bx)]
    points_z = [v[2] for v in vertex_list] + [p for w in unique_walls for p in (w.az, w.bz)]
    bounds = (min(points_x), max(points_x), min(points_z), max(points_z)) if points_x else (0, 0, 0, 0)
    ground_boxes = [
        (min(vertex_list[i][0] for i in indices), max(vertex_list[i][0] for i in indices),
         min(vertex_list[i][2] for i in indices), max(vertex_list[i][2] for i in indices))
        for indices, _, _, _ in ground
    ]
    wall_boxes = [(min(w.ax, w.bx), max(w.ax, w.bx), min(w.az, w.bz), max(w.az, w.bz)) for w in unique_walls]
    return Segment(segment_id, len(source_faces), vertex_list, ground, unique_walls, bounds,
                   cell_members(bounds, ground_boxes), cell_members(bounds, wall_boxes),
                   removed_ground, merged, duplicate_walls_removed,
                   lane_walls_removed, mid_lane_walls_removed,
                   mid_corridor_walls_removed,
                   seam_cap_walls_removed, interior_walls_removed,
                   y_dead_walls_removed, inner_driveable_walls_removed,
                   crossing_walls_removed,
                   driveable_walls_skipped, vertical_faces_emitted,
                   free_faces_skipped)


def append_cells(blob: bytearray, cells: list[list[int]]) -> tuple[int, int]:
    table_offset = len(blob)
    blob.extend(b"\0" * (len(cells) * CELL.size))
    indices_offset = len(blob)
    for cell_index, members in enumerate(cells):
        member_offset = len(blob)
        if len(members) > 65535:
            raise ValueError("celula TCOL excede uint16")
        for member in members:
            blob.extend(struct.pack("<H", member))
        CELL.pack_into(blob, table_offset + cell_index * CELL.size, member_offset, len(members), 0)
    return table_offset, indices_offset


def build_binary(segments: list[Segment]) -> bytes:
    directory_offset = HEADER.size
    blob = bytearray(HEADER.size + len(segments) * DIRECTORY.size)
    total_ground = sum(len(segment.ground) for segment in segments)
    total_walls = sum(len(segment.walls) for segment in segments)
    HEADER.pack_into(blob, 0, MAGIC, VERSION, HEADER.size, DIRECTORY.size, VERTEX.size,
                     GROUND.size, WALL.size, CELL.size, GRID_DIM, len(segments),
                     total_ground, total_walls, directory_offset)
    for directory_index, segment in enumerate(segments):
        vertex_offset = len(blob)
        for vertex in segment.vertices:
            blob.extend(VERTEX.pack(*vertex))
        ground_offset = len(blob)
        for indices, family_id, surface_type, source_index in segment.ground:
            padded = indices + (indices[-1],) * (4 - len(indices))
            flags = 0x01 | (0x02 if surface_type == 1 else 0x04)
            blob.extend(GROUND.pack(*padded, family_id, surface_type, len(indices), flags, source_index))
        wall_offset = len(blob)
        for wall in segment.walls:
            blob.extend(WALL.pack(wall.ax, wall.az, wall.bx, wall.bz, wall.min_y, wall.max_y,
                                  wall.nx, wall.nz, wall.family_id, wall.source_index))
        ground_cell_offset, ground_index_offset = append_cells(blob, segment.ground_cells)
        wall_cell_offset, wall_index_offset = append_cells(blob, segment.wall_cells)
        DIRECTORY.pack_into(
            blob, directory_offset + directory_index * DIRECTORY.size,
            segment.segment_id, len(segment.vertices), len(segment.ground), len(segment.walls), GRID_DIM, 0,
            *segment.bounds, vertex_offset, ground_offset, wall_offset,
            ground_cell_offset, ground_index_offset, wall_cell_offset, wall_index_offset,
        )
    return bytes(blob)


def validate_binary(blob: bytes, segments: list[Segment]) -> None:
    if len(blob) < HEADER.size:
        raise ValueError("TCOL truncado")
    values = HEADER.unpack_from(blob)
    if values[:10] != (MAGIC, VERSION, HEADER.size, DIRECTORY.size, VERTEX.size,
                       GROUND.size, WALL.size, CELL.size, GRID_DIM, len(segments)):
        raise ValueError("cabecalho TCOL invalido")
    if values[10] != sum(len(s.ground) for s in segments) or values[11] != sum(len(s.walls) for s in segments):
        raise ValueError("contagens TCOL invalidas")
    previous = 0
    for index, expected in enumerate(segments):
        entry = DIRECTORY.unpack_from(blob, HEADER.size + index * DIRECTORY.size)
        if entry[0] <= previous or entry[0] != expected.segment_id:
            raise ValueError("diretorio TCOL fora de ordem")
        previous = entry[0]
        if entry[1:5] != (len(expected.vertices), len(expected.ground), len(expected.walls), GRID_DIM):
            raise ValueError(f"contagens TCOL divergentes no segmento {expected.segment_id}")
        for offset, count, size in ((entry[10], entry[1], VERTEX.size), (entry[11], entry[2], GROUND.size),
                                    (entry[12], entry[3], WALL.size), (entry[13], GRID_DIM * GRID_DIM, CELL.size),
                                    (entry[15], GRID_DIM * GRID_DIM, CELL.size)):
            if offset + count * size > len(blob):
                raise ValueError(f"offset TCOL invalido no segmento {expected.segment_id}")
        for cell_table in (entry[13], entry[15]):
            for cell in range(GRID_DIM * GRID_DIM):
                member_offset, member_count, _ = CELL.unpack_from(blob, cell_table + cell * CELL.size)
                if member_offset + member_count * 2 > len(blob):
                    raise ValueError(f"celula TCOL truncada no segmento {expected.segment_id}")


def audit_join_corridor(
    segments: list[Segment],
    centroids: dict[int, tuple[float, float]],
    segment_ids: list[int],
) -> dict[str, object]:
    """Centerline samples across each join must stay clear of wall edges.

    Clearance is the hull half-width band (~24u). Samples run ±20u along the
    centroid-to-centroid line through the join.
    """
    unit = 1 << 16
    clearance = 24 * unit
    contour = 0
    hits: list[dict[str, object]] = []
    walls_by_id = {segment.segment_id: segment.walls for segment in segments}
    for segment in segments:
        contour += sum(1 for wall in segment.walls if wall.family_id == 0)
    for index, segment_id in enumerate(segment_ids[:-1]):
        start = centroids.get(segment_id)
        end = centroids.get(segment_ids[index + 1])
        if start is None or end is None:
            continue
        dx = end[0] - start[0]
        dz = end[1] - start[1]
        length = math.hypot(dx, dz)
        if length < 1.0:
            continue
        ux = dx / length
        uz = dz / length
        mid_x = (start[0] + end[0]) * 0.5
        mid_z = (start[1] + end[1]) * 0.5
        walls = walls_by_id.get(segment_id, []) + walls_by_id.get(segment_ids[index + 1], [])
        for step in range(-20, 21, 4):
            px = mid_x + ux * (step * unit)
            pz = mid_z + uz * (step * unit)
            for wall in walls:
                distance = wall_distance_to_point_xz(wall, px, pz)
                if distance >= clearance:
                    continue
                normal_len = math.hypot(wall.nx, wall.nz)
                align = 0.0
                if normal_len > 0.0:
                    align = abs((wall.nx / normal_len) * ux + (wall.nz / normal_len) * uz)
                # A side rail can pass near the asphalt centroid on a narrow
                # bend. Only an along-track normal stops the car at the join.
                if align < 0.70:
                    continue
                hits.append({
                    "from": segment_id,
                    "to": segment_ids[index + 1],
                    "alongUnits": step,
                    "distanceUnits": round(distance / unit, 2),
                    "familyId": wall.family_id,
                    "align": round(align, 3),
                })
                break
            else:
                continue
            break
    seg2_min_units = None
    seg2_walls = walls_by_id.get(2, [])
    seg2_center = centroids.get(2)
    if seg2_center is not None and seg2_walls:
        nearest = min(
            wall_distance_to_point_xz(wall, seg2_center[0], seg2_center[1])
            for wall in seg2_walls
        )
        seg2_min_units = round(nearest / unit, 2)
    ground_by_id = {segment.segment_id: segment for segment in segments}
    ground_misses = 0
    for index, segment_id in enumerate(segment_ids[:-1]):
        start = centroids.get(segment_id)
        end = centroids.get(segment_ids[index + 1])
        if start is None or end is None:
            continue
        mid_x = (start[0] + end[0]) * 0.5
        mid_z = (start[1] + end[1]) * 0.5
        covered = False
        for owner_id in (segment_id, segment_ids[index + 1]):
            owner = ground_by_id.get(owner_id)
            if owner is None:
                continue
            if _point_on_segment_ground(owner, mid_x, mid_z):
                covered = True
                break
        if not covered:
            ground_misses += 1
    return {
        "contourWalls": contour,
        "seamCorridorHits": len(hits),
        "seamCorridorHitSamples": hits[:16],
        "seg2NearestWallUnits": seg2_min_units,
        "joinGroundMisses": ground_misses,
    }


def _point_on_segment_ground(segment: Segment, px: float, pz: float) -> bool:
    for indices, _family, surface_type, _source in segment.ground:
        if surface_type not in DRIVEABLE_TYPES:
            continue
        verts = [segment.vertices[index] for index in indices]
        if _point_in_face_xz(px, pz, verts):
            return True
    return False


def _point_in_face_xz(px: float, pz: float, verts: list[tuple[int, int, int]]) -> bool:
    if len(verts) < 3:
        return False
    for index in range(1, len(verts) - 1):
        ax, az = float(verts[0][0]), float(verts[0][2])
        bx, bz = float(verts[index][0]), float(verts[index][2])
        cx, cz = float(verts[index + 1][0]), float(verts[index + 1][2])
        c1 = (bx - ax) * (pz - az) - (bz - az) * (px - ax)
        c2 = (cx - bx) * (pz - bz) - (cz - bz) * (px - bx)
        c3 = (ax - cx) * (pz - cz) - (az - cz) * (px - cx)
        if (c1 >= 0.0 and c2 >= 0.0 and c3 >= 0.0) or (c1 <= 0.0 and c2 <= 0.0 and c3 <= 0.0):
            return True
    return False


def normalize_stem(value: str) -> str:
    return Path(value).stem.lower().rstrip("_")


def texture_stem(value: str) -> str:
    """Family key. Keeps a trailing underscore so f07564_ is not f07564."""
    return Path(str(value)).stem.lower()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--geometry-dir", type=Path, required=True)
    parser.add_argument("--segments-map", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    root = json.loads(args.segments_map.read_text(encoding="utf-8-sig"))
    manifest = json.loads(args.manifest.read_text(encoding="utf-8-sig"))
    families = root.get("textureFamilies", [])
    surface_by_family = {int(item["id"]): int(item.get("surfaceTypeId", 0)) for item in families}
    families_by_stem: dict[str, list[int]] = {}
    for item in families:
        stem = texture_stem(str(item.get("sourceStem", "")))
        if not stem:
            continue
        families_by_stem.setdefault(stem, []).append(int(item["id"]))
    requested_free = [texture_stem(item) for item in manifest.get("freeTraverseStems", [])]
    if len(requested_free) != len(set(requested_free)):
        raise ValueError("freeTraverseStems tem entradas repetidas")
    missing = [stem for stem in requested_free if stem not in families_by_stem]
    if missing:
        raise ValueError("texturas livres ausentes no catalogo de familias: " + ", ".join(missing))
    free_families = {family_id for stem in requested_free for family_id in families_by_stem[stem]}
    segment_ids = sorted({int(match.group(1)) for path in args.geometry_dir.glob("S???.GEO")
                          if (match := re.fullmatch(r"S(\d{3})\.GEO", path.name, re.IGNORECASE))})
    if not segment_ids:
        raise ValueError(f"nenhum S###.GEO encontrado em {args.geometry_dir}")

    # Neighbor travel deltas from asphalt centroids (seam cull on S-curves).
    centroids: dict[int, tuple[float, float]] = {}
    for segment_id in segment_ids:
        geo_path = args.geometry_dir / f"S{segment_id:03d}.GEO"
        mat_path = args.geometry_dir / f"S{segment_id:03d}M64.MAT"
        if not mat_path.exists():
            mat_path = args.geometry_dir / f"S{segment_id:03d}M32.MAT"
        try:
            source_faces = read_geo(geo_path, segment_id)
            family_ids = read_mat(mat_path, segment_id)
        except Exception:
            continue
        asphalt_faces: list[Face] = []
        for index, vertices in enumerate(source_faces):
            if index >= len(family_ids):
                break
            family_id = int(family_ids[index])
            surface_type = int(surface_by_family.get(family_id, 0))
            if surface_type == 1 and is_floor(vertices):
                asphalt_faces.append(Face(index, vertices, family_id, surface_type))
        center = asphalt_centroid_xz(asphalt_faces)
        if center is not None:
            centroids[segment_id] = center
    neighbor_deltas: dict[int, tuple[float, float] | None] = {}
    for index, segment_id in enumerate(segment_ids):
        center = centroids.get(segment_id)
        nxt = centroids.get(segment_ids[index + 1]) if index + 1 < len(segment_ids) else None
        prv = centroids.get(segment_ids[index - 1]) if index > 0 else None
        delta: tuple[float, float] | None = None
        if center is not None and nxt is not None:
            delta = (nxt[0] - center[0], nxt[1] - center[1])
        elif center is not None and prv is not None:
            delta = (center[0] - prv[0], center[1] - prv[1])
        neighbor_deltas[segment_id] = delta

    segments = [
        build_segment(
            segment_id,
            args.geometry_dir,
            surface_by_family,
            free_families,
            neighbor_deltas.get(segment_id),
        )
        for segment_id in segment_ids
    ]
    join_audit = audit_join_corridor(segments, centroids, segment_ids)
    blob = build_binary(segments)
    validate_binary(blob, segments)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(blob)
    report = {
        "format": "TCL1", "version": VERSION, "grid": [GRID_DIM, GRID_DIM],
        "segmentCount": len(segments), "binaryBytes": len(blob),
        "sourceFaceCount": sum(s.source_face_count for s in segments),
        "selectedGroundFaceCountBeforeSimplification": sum(
            len(s.ground) + s.duplicate_ground_removed + s.merged_coplanar_pairs for s in segments
        ),
        "groundFaceCount": sum(len(s.ground) for s in segments),
        "wallFaceCount": sum(len(s.walls) for s in segments),
        "collisionVertexCount": sum(len(s.vertices) for s in segments),
        "duplicateGroundFacesRemoved": sum(s.duplicate_ground_removed for s in segments),
        "coplanarTrianglePairsMerged": sum(s.merged_coplanar_pairs for s in segments),
        "groundFacesSavedByConservativeSimplification": sum(
            s.duplicate_ground_removed + s.merged_coplanar_pairs for s in segments
        ),
        "duplicateWallsRemoved": sum(s.duplicate_walls_removed for s in segments),
        "laneWallsRemoved": sum(s.lane_walls_removed for s in segments),
        "midLaneWallsRemoved": sum(s.mid_lane_walls_removed for s in segments),
        "midCorridorWallsRemoved": sum(s.mid_corridor_walls_removed for s in segments),
        "seamCapWallsRemoved": sum(s.seam_cap_walls_removed for s in segments),
        "interiorWallsRemoved": sum(s.interior_walls_removed for s in segments),
        "yDeadWallsRemoved": sum(s.y_dead_walls_removed for s in segments),
        "innerDriveableWallsRemoved": sum(s.inner_driveable_walls_removed for s in segments),
        "crossingWallsRemoved": sum(s.crossing_walls_removed for s in segments),
        "driveableWallsSkipped": sum(s.driveable_walls_skipped for s in segments),
        "verticalFacesEmitted": sum(s.vertical_faces_emitted for s in segments),
        "freeFacesSkipped": sum(s.free_faces_skipped for s in segments),
        "wallEdgeKeepBandRaw": WALL_EDGE_KEEP_BAND_RAW,
        "wallMidLaneClearRaw": WALL_MID_LANE_CLEAR_RAW,
        "wallMidCorridorRaw": WALL_MID_CORRIDOR_RAW,
        "wallLateralRimFrac": WALL_LATERAL_RIM_FRAC,
        "wallSeamEndFrac": WALL_SEAM_END_FRAC,
        "wallDisableAabbInteriorCull": WALL_DISABLE_AABB_INTERIOR_CULL,
        "wallYOverlapMarginRaw": WALL_Y_OVERLAP_MARGIN_RAW,
        "wallOutwardProbeRaw": WALL_OUTWARD_PROBE_RAW,
        "wallEnableStemEdges": WALL_ENABLE_STEM_EDGES,
        "wallEnableDriveableContour": WALL_ENABLE_DRIVEABLE_CONTOUR,
        "wallEmitAllVerticalFaces": WALL_EMIT_ALL_VERTICAL_FACES,
        "wallInnerCullRimKeepFrac": WALL_INNER_CULL_RIM_KEEP_FRAC,
        "wallInnerCullMaxDistRaw": WALL_INNER_CULL_MAX_DIST_RAW,
        "freeTraverseStems": requested_free,
        "freeFamilyIds": sorted(free_families),
        "groundSelection": "LOD0 floor-like faces whose family surfaceTypeId is 1, 2 or 3",
        "wallSelection": (
            "every non-floor LOD0 face except freeTraverseStems; floors never block; "
            "driveable contour stays off; drop edges that cross the racing line, "
            "Y-dead walls, and transverse seam caps"
        ),
        "joinAudit": join_audit,
        "simplification": "exact vertex weld + exact duplicate removal + conservative coplanar triangle pairing",
        "runtimePayload": "XYZ vertices, ground topology/surface ids, planar walls and 4x4 spatial cells; no UV, texture or lighting",
        "segments": [{"id": s.segment_id, "sourceFaces": s.source_face_count,
                      "vertices": len(s.vertices), "groundFaces": len(s.ground), "walls": len(s.walls),
                      "duplicatesRemoved": s.duplicate_ground_removed,
                      "coplanarPairsMerged": s.merged_coplanar_pairs,
                      "laneWallsRemoved": s.lane_walls_removed,
                      "midLaneWallsRemoved": s.mid_lane_walls_removed,
                      "midCorridorWallsRemoved": s.mid_corridor_walls_removed,
                      "seamCapWallsRemoved": s.seam_cap_walls_removed,
                      "interiorWallsRemoved": s.interior_walls_removed,
                      "yDeadWallsRemoved": s.y_dead_walls_removed,
                      "innerDriveableWallsRemoved": s.inner_driveable_walls_removed,
                      "crossingWallsRemoved": s.crossing_walls_removed,
                      "driveableWallsSkipped": s.driveable_walls_skipped,
                      "verticalFacesEmitted": s.vertical_faces_emitted} for s in segments],
    }
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"TCOL: segments={len(segments)} ground={report['groundFaceCount']} walls={report['wallFaceCount']} "
          f"vertices={report['collisionVertexCount']} bytes={len(blob)} "
          f"contour={join_audit['contourWalls']} seamHits={join_audit['seamCorridorHits']} "
          f"seg2Nearest={join_audit['seg2NearestWallUnits']} "
          f"groundMiss={join_audit['joinGroundMisses']}")
    if join_audit["contourWalls"] or join_audit["seamCorridorHits"]:
        raise SystemExit(
            "TCOL join audit failed: contour walls or centerline seam hits remain"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
