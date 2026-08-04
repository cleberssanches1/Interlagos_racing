#!/usr/bin/env python3
"""Audit driveable track continuity without adding Saturn runtime cost.

Reads the final PAK1 GEO/MAT packs used by the game, checks shared XZ edges,
and samples PATH.NYA at center/left/right wheel lines with the same ABC/ACD
quad split used by TrackSystem.
"""
from __future__ import annotations

import argparse
import csv
import json
import math
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


PAK_MAGIC = 0x314B4150
GEO_MAGIC = 0x314F4547
MAT_MAGIC = 0x3154414D
DRIVEABLE_TYPES = frozenset((1, 2, 3))
GEO_FACE_SIZE = 28


@dataclass(frozen=True)
class Face:
    segment_id: int
    face_index: int
    vertices: tuple[tuple[int, int, int], ...]
    surface_type: int
    normal: tuple[int, int, int]

    @property
    def aabb_xz(self) -> tuple[int, int, int, int]:
        xs = [item[0] for item in self.vertices]
        zs = [item[2] for item in self.vertices]
        return min(xs), max(xs), min(zs), max(zs)


def read_pak(path: Path) -> dict[str, bytes]:
    data = path.read_bytes()
    if len(data) < 12:
        raise ValueError(f"PAK truncado: {path}")
    magic, version, count = struct.unpack_from("<III", data, 0)
    if magic != PAK_MAGIC or version != 1:
        raise ValueError(f"PAK invalido: {path}")
    table_end = 12 + count * 72
    if table_end > len(data):
        raise ValueError(f"Tabela PAK truncada: {path}")
    result: dict[str, bytes] = {}
    for index in range(count):
        base = 12 + index * 72
        raw_name = data[base:base + 64].split(b"\0", 1)[0]
        name = raw_name.decode("ascii").upper()
        offset, size = struct.unpack_from("<II", data, base + 64)
        if offset + size > len(data):
            raise ValueError(f"Entrada PAK truncada: {path}:{name}")
        result[name] = data[offset:offset + size]
    return result


def read_geo(blob: bytes, expected_segment: int) -> list[tuple[tuple[int, int, int], ...]]:
    if len(blob) < 24:
        raise ValueError(f"GEO truncado no segmento {expected_segment}")
    magic, version, _, segment_id, payload = struct.unpack_from("<IHHII", blob, 0)
    if magic != GEO_MAGIC or version != 1 or segment_id != expected_segment:
        raise ValueError(f"GEO invalido no segmento {expected_segment}")
    if payload + 16 > len(blob):
        raise ValueError(f"Payload GEO truncado no segmento {expected_segment}")
    vertex_count, face_count = struct.unpack_from("<II", blob, 16)
    vertex_offset = 24
    face_offset = vertex_offset + vertex_count * 12
    if face_offset + face_count * GEO_FACE_SIZE > len(blob):
        raise ValueError(f"Faces GEO truncadas no segmento {expected_segment}")
    vertices = [
        struct.unpack_from("<iii", blob, vertex_offset + index * 12)
        for index in range(vertex_count)
    ]
    faces: list[tuple[tuple[int, int, int], ...]] = []
    for face_index in range(face_count):
        base = face_offset + face_index * GEO_FACE_SIZE
        indices = struct.unpack_from("<HHHH", blob, base)
        kind = blob[base + 24]
        count = 3 if kind == 3 or indices[2] == indices[3] else 4
        if max(indices[:count], default=0) >= vertex_count:
            raise ValueError(
                f"Indice de vertice invalido: segmento={expected_segment} face={face_index}"
            )
        faces.append(tuple(vertices[item] for item in indices[:count]))
    return faces


def read_mat(blob: bytes, expected_segment: int) -> list[int]:
    if len(blob) < 20:
        raise ValueError(f"MAT truncado no segmento {expected_segment}")
    magic, version, _, segment_id, payload = struct.unpack_from("<IHHII", blob, 0)
    if magic != MAT_MAGIC or version != 1 or segment_id != expected_segment:
        raise ValueError(f"MAT invalido no segmento {expected_segment}")
    if payload + 16 > len(blob):
        raise ValueError(f"Payload MAT truncado no segmento {expected_segment}")
    face_count = struct.unpack_from("<I", blob, 16)[0]
    if 20 + face_count * 4 > len(blob):
        raise ValueError(f"Bindings MAT truncados no segmento {expected_segment}")
    return [struct.unpack_from("<I", blob, 20 + index * 4)[0] for index in range(face_count)]


def face_normal(vertices: tuple[tuple[int, int, int], ...]) -> tuple[int, int, int]:
    a, b, c = vertices[:3]
    ux, uy, uz = b[0] - a[0], b[1] - a[1], b[2] - a[2]
    vx, vy, vz = c[0] - a[0], c[1] - a[1], c[2] - a[2]
    return uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx


def is_floor_like(normal: tuple[int, int, int]) -> bool:
    nx, ny, nz = normal
    length_sq = nx * nx + ny * ny + nz * nz
    return length_sq > 0 and (ny * ny * 4) >= length_sq


def load_driveable_faces(
    geo_pack: dict[str, bytes],
    mat_pack: dict[str, bytes],
    surface_by_family: dict[int, int],
    first_segment: int,
    last_segment: int,
) -> tuple[list[Face], list[dict[str, int]]]:
    faces: list[Face] = []
    segment_rows: list[dict[str, int]] = []
    for segment_id in range(first_segment, last_segment + 1):
        geo_name = f"S{segment_id:03d}.GEO"
        mat_name = f"S{segment_id:03d}M64.MAT"
        if geo_name not in geo_pack:
            raise ValueError(f"Ausente no GEO.BIN: {geo_name}")
        if mat_name not in mat_pack:
            fallback = f"S{segment_id:03d}M32.MAT"
            if fallback not in mat_pack:
                raise ValueError(f"Ausente no MAT pack: {mat_name}/{fallback}")
            mat_name = fallback
        geo_faces = read_geo(geo_pack[geo_name], segment_id)
        family_ids = read_mat(mat_pack[mat_name], segment_id)
        if len(geo_faces) != len(family_ids):
            raise ValueError(
                f"Faces divergentes SEG {segment_id:03d}: GEO={len(geo_faces)} MAT={len(family_ids)}"
            )
        degenerate = 0
        selected = 0
        for face_index, (vertices, family_id) in enumerate(zip(geo_faces, family_ids)):
            normal = face_normal(vertices)
            if normal == (0, 0, 0):
                degenerate += 1
                continue
            surface_type = surface_by_family.get(family_id, 0)
            if surface_type not in DRIVEABLE_TYPES or not is_floor_like(normal):
                continue
            faces.append(Face(segment_id, face_index, vertices, surface_type, normal))
            selected += 1
        segment_rows.append(
            {
                "segment": segment_id,
                "source_faces": len(geo_faces),
                "driveable_floor_faces": selected,
                "degenerate_faces": degenerate,
            }
        )
    return faces, segment_rows


def canonical_edge(
    a: tuple[int, int, int], b: tuple[int, int, int]
) -> tuple[tuple[tuple[int, int], tuple[int, int]], tuple[int, int]]:
    axz, bxz = (a[0], a[2]), (b[0], b[2])
    if axz <= bxz:
        return (axz, bxz), (a[1], b[1])
    return (bxz, axz), (b[1], a[1])


def audit_shared_edges(
    faces: Iterable[Face], threshold_raw: int
) -> tuple[list[dict[str, object]], list[dict[str, object]], int]:
    edges: dict[
        tuple[tuple[int, int], tuple[int, int]],
        list[tuple[Face, int, int]],
    ] = {}
    for face in faces:
        for index, a in enumerate(face.vertices):
            b = face.vertices[(index + 1) % len(face.vertices)]
            key, heights = canonical_edge(a, b)
            if key[0] == key[1]:
                continue
            edges.setdefault(key, []).append((face, heights[0], heights[1]))

    discontinuities: list[dict[str, object]] = []
    normal_jumps: list[dict[str, object]] = []
    shared_count = 0
    for key, owners in edges.items():
        if len(owners) < 2:
            continue
        shared_count += 1
        base_face, base_y0, base_y1 = owners[0]
        for other_face, y0, y1 in owners[1:]:
            delta0 = abs(y0 - base_y0)
            delta1 = abs(y1 - base_y1)
            max_delta = max(delta0, delta1)
            if max_delta > threshold_raw:
                discontinuities.append(
                    {
                        "segment_a": base_face.segment_id,
                        "face_a": base_face.face_index,
                        "segment_b": other_face.segment_id,
                        "face_b": other_face.face_index,
                        "edge_xz_raw": key,
                        "delta_y_raw": max_delta,
                        "delta_y_units": max_delta / 65536.0,
                    }
                )
                continue
            na, nb = base_face.normal, other_face.normal
            la = math.sqrt(sum(value * value for value in na))
            lb = math.sqrt(sum(value * value for value in nb))
            if la <= 0.0 or lb <= 0.0:
                continue
            cosine = max(-1.0, min(1.0, sum(a * b for a, b in zip(na, nb)) / (la * lb)))
            angle = math.degrees(math.acos(abs(cosine)))
            if angle >= 8.0:
                normal_jumps.append(
                    {
                        "segment_a": base_face.segment_id,
                        "face_a": base_face.face_index,
                        "segment_b": other_face.segment_id,
                        "face_b": other_face.face_index,
                        "normal_angle_deg": angle,
                    }
                )
    discontinuities.sort(key=lambda row: float(row["delta_y_units"]), reverse=True)
    normal_jumps.sort(key=lambda row: float(row["normal_angle_deg"]), reverse=True)
    return discontinuities, normal_jumps, shared_count


def edge_cross_xz(a: tuple[int, int, int], b: tuple[int, int, int], x: int, z: int) -> int:
    return (x - a[0]) * (b[2] - a[2]) - (z - a[2]) * (b[0] - a[0])


def point_in_triangle_xz(
    a: tuple[int, int, int],
    b: tuple[int, int, int],
    c: tuple[int, int, int],
    x: int,
    z: int,
) -> bool:
    values = (edge_cross_xz(a, b, x, z), edge_cross_xz(b, c, x, z), edge_cross_xz(c, a, x, z))
    return not (any(value < 0 for value in values) and any(value > 0 for value in values))


def solve_plane_y(
    a: tuple[int, int, int],
    b: tuple[int, int, int],
    c: tuple[int, int, int],
    x: int,
    z: int,
) -> int | None:
    ux, uy, uz = b[0] - a[0], b[1] - a[1], b[2] - a[2]
    vx, vy, vz = c[0] - a[0], c[1] - a[1], c[2] - a[2]
    nx = uy * vz - uz * vy
    ny = uz * vx - ux * vz
    nz = ux * vy - uy * vx
    if ny == 0:
        return None
    return a[1] - (nx * (x - a[0]) + nz * (z - a[2])) // ny


def face_y_at(face: Face, x: int, z: int) -> int | None:
    min_x, max_x, min_z, max_z = face.aabb_xz
    if x < min_x or x > max_x or z < min_z or z > max_z:
        return None
    a, b, c = face.vertices[:3]
    if point_in_triangle_xz(a, b, c, x, z):
        return solve_plane_y(a, b, c, x, z)
    if len(face.vertices) == 4:
        d = face.vertices[3]
        if point_in_triangle_xz(a, c, d, x, z):
            return solve_plane_y(a, c, d, x, z)
    return None


def read_path_points(path: Path) -> list[tuple[int, int, int]]:
    data = path.read_bytes()
    if len(data) < 8:
        raise ValueError(f"PATH.NYA truncado: {path}")
    version, line_count = struct.unpack_from(">II", data, 0)
    if version != 1 or line_count == 0 or line_count > 16:
        raise ValueError(f"PATH.NYA invalido: version={version} lines={line_count}")
    lines: list[list[tuple[int, int, int]]] = []
    for line_index in range(line_count):
        count, offset = struct.unpack_from(">II", data, 8 + line_index * 8)
        if offset + count * 12 > len(data):
            raise ValueError(f"Linha PATH truncada: {line_index}")
        lines.append([
            struct.unpack_from(">iii", data, offset + point_index * 12)
            for point_index in range(count)
        ])
    return max(lines, key=len)


def sample_wheel_paths(
    faces: list[Face], path_points: list[tuple[int, int, int]], half_track_raw: int
) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for point_index, point in enumerate(path_points):
        previous = path_points[point_index - 1]
        following = path_points[(point_index + 1) % len(path_points)]
        dx, dz = following[0] - previous[0], following[2] - previous[2]
        length = math.hypot(dx, dz)
        if length <= 0.0:
            continue
        right_x, right_z = dz / length, -dx / length
        for lane_name, lane_scale in (("left", -1), ("center", 0), ("right", 1)):
            x = point[0] + int(right_x * half_track_raw * lane_scale)
            z = point[2] + int(right_z * half_track_raw * lane_scale)
            candidates: list[tuple[int, Face]] = []
            for face in faces:
                y = face_y_at(face, x, z)
                if y is not None:
                    candidates.append((y, face))
            if not candidates:
                continue
            y, face = min(candidates, key=lambda item: abs(item[0] - point[1]))
            rows.append(
                {
                    "path_index": point_index,
                    "lane": lane_name,
                    "segment": face.segment_id,
                    "face": face.face_index,
                    "x_raw": x,
                    "z_raw": z,
                    "path_y_raw": point[1],
                    "surface_y_raw": y,
                    "surface_y_units": y / 65536.0,
                }
            )
    previous_by_lane: dict[str, dict[str, object]] = {}
    for row in rows:
        lane = str(row["lane"])
        previous = previous_by_lane.get(lane)
        delta = 0 if previous is None else int(row["surface_y_raw"]) - int(previous["surface_y_raw"])
        path_delta = 0 if previous is None else int(row["path_y_raw"]) - int(previous["path_y_raw"])
        dx = 0 if previous is None else int(row["x_raw"]) - int(previous["x_raw"])
        dz = 0 if previous is None else int(row["z_raw"]) - int(previous["z_raw"])
        distance_raw = int(math.hypot(dx, dz))
        row["delta_y_raw"] = delta
        row["delta_y_units"] = delta / 65536.0
        row["path_delta_y_raw"] = path_delta
        row["delta_error_raw"] = delta - path_delta
        row["delta_error_units"] = (delta - path_delta) / 65536.0
        row["distance_xz_units"] = distance_raw / 65536.0
        row["grade_tan"] = (delta / distance_raw) if distance_raw > 0 else 0.0
        row["surface_minus_path_units"] = (
            int(row["surface_y_raw"]) - int(row["path_y_raw"])
        ) / 65536.0
        previous_by_lane[lane] = row
    return rows


def write_csv(path: Path, rows: list[dict[str, object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fields = list(rows[0].keys()) if rows else ["path_index", "lane"]
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--geo-pack", type=Path, default=Path("cd/data/GEO.BIN"))
    parser.add_argument("--mat-pack", type=Path, default=Path("cd/data/MAT64.BIN"))
    parser.add_argument("--segments-map", type=Path, default=Path("cd/data/SMAP.TXT"))
    parser.add_argument("--path", type=Path, default=Path("cd/data/PATH.NYA"))
    parser.add_argument("--first-segment", type=int, default=22)
    parser.add_argument("--last-segment", type=int, default=42)
    parser.add_argument("--edge-threshold", type=float, default=0.25)
    parser.add_argument("--half-track", type=float, default=0.55)
    parser.add_argument("--output-dir", type=Path, default=Path("BuildDrop/physics_surface_audit"))
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.first_segment <= 0 or args.last_segment < args.first_segment:
        raise SystemExit("Intervalo de segmentos invalido")
    root = json.loads(args.segments_map.read_text(encoding="utf-8-sig"))
    surface_by_family = {
        int(item["id"]): int(item.get("surfaceTypeId", 0))
        for item in root.get("textureFamilies", [])
        if int(item.get("id", 0)) > 0
    }
    faces, segments = load_driveable_faces(
        read_pak(args.geo_pack),
        read_pak(args.mat_pack),
        surface_by_family,
        args.first_segment,
        args.last_segment,
    )
    discontinuities, normal_jumps, shared_edges = audit_shared_edges(
        faces, int(args.edge_threshold * 65536.0)
    )
    wheel_rows = sample_wheel_paths(
        faces, read_path_points(args.path), int(args.half_track * 65536.0)
    )
    args.output_dir.mkdir(parents=True, exist_ok=True)
    write_csv(args.output_dir / "wheel_paths.csv", wheel_rows)
    biggest_path_steps = sorted(
        wheel_rows, key=lambda row: abs(float(row["delta_error_units"])), reverse=True
    )[:20]
    report = {
        "segments": {"first": args.first_segment, "last": args.last_segment},
        "driveableFloorFaceCount": len(faces),
        "sharedEdgeCount": shared_edges,
        "edgeDiscontinuityCount": len(discontinuities),
        "normalJumpCount": len(normal_jumps),
        "pathSampleCount": len(wheel_rows),
        "segmentAudit": segments,
        "largestEdgeDiscontinuities": discontinuities[:50],
        "largestNormalJumps": normal_jumps[:50],
        "largestPathSteps": biggest_path_steps,
    }
    report_path = args.output_dir / "continuity_report.json"
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(
        f"surface_audit: segments={args.first_segment}-{args.last_segment} "
        f"faces={len(faces)} shared_edges={shared_edges} "
        f"edge_discontinuities={len(discontinuities)} normal_jumps={len(normal_jumps)} "
        f"path_samples={len(wheel_rows)}"
    )
    for row in biggest_path_steps[:10]:
        print(
            "path_step: "
            f"idx={row['path_index']} lane={row['lane']} seg={row['segment']} "
            f"face={row['face']} dy={float(row['delta_y_units']):.4f} "
            f"grade={float(row['grade_tan']):.4f} "
            f"path_error={float(row['delta_error_units']):.4f}"
        )
    print(f"report={report_path}")
    print(f"wheel_csv={args.output_dir / 'wheel_paths.csv'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
