#!/usr/bin/env python3
"""Audit steep NYA road segments for face and inter-segment continuity.

The tool is intentionally offline.  It reads SEG_###.NYA plus the matching
segments_map.json, isolates asphalt meshes, and reports whether adjacent
segments share the same XZ seam at the same Y altitude.
"""
from __future__ import annotations

import argparse
import json
import math
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


ROAD_TOKENS = ("asfalto", "asphalt", "roadpit", "roadgrid", "pista")
MAX_DRIVEABLE_TAN = math.tan(math.radians(70.0))


def u16(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset : offset + 2], "big")


def u32(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset : offset + 4], "big")


def s32(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset : offset + 4], "big", signed=True)


@dataclass(frozen=True)
class Vertex:
    x: float
    y: float
    z: float


@dataclass
class RoadMesh:
    name: str
    vertices: list[Vertex]
    faces: list[tuple[int, ...]]


def is_road_name(name: str) -> bool:
    folded = name.casefold()
    return any(token in folded for token in ROAD_TOKENS)


def load_segment_names(path: Path) -> dict[int, list[str]]:
    root = json.loads(path.read_text(encoding="utf-8-sig"))
    result: dict[int, list[str]] = {}
    for row in root.get("segments", []):
        label = str(row.get("segment", ""))
        try:
            segment_id = int(label.rsplit("_", 1)[1])
        except (IndexError, ValueError):
            continue
        result[segment_id] = [str(item) for item in row.get("entries", [])]
    return result


def read_nya(path: Path, names: list[str]) -> tuple[int, list[RoadMesh]]:
    data = path.read_bytes()
    if len(data) < 12:
        raise ValueError(f"NYA muito pequeno: {path}")
    nya_type = u32(data, 0)
    mesh_count = u32(data, 4)
    offset = 12
    selected: list[RoadMesh] = []
    for mesh_index in range(mesh_count):
        if offset + 8 > len(data):
            raise ValueError(f"cabecalho de mesh truncado em {path}, mesh {mesh_index}")
        point_count = u32(data, offset)
        polygon_count = u32(data, offset + 4)
        offset += 8
        vertices = [
            Vertex(
                s32(data, offset + i * 12) / 65536.0,
                s32(data, offset + i * 12 + 4) / 65536.0,
                s32(data, offset + i * 12 + 8) / 65536.0,
            )
            for i in range(point_count)
        ]
        offset += point_count * 12
        faces: list[tuple[int, ...]] = []
        for face_index in range(polygon_count):
            base = offset + face_index * 20
            indices = tuple(u16(data, base + 12 + j * 2) for j in range(4))
            if max(indices) >= point_count:
                continue
            faces.append(indices[:3] if indices[2] == indices[3] else indices)
        attributes_offset = offset + polygon_count * 20
        road_faces: list[tuple[int, ...]] = []
        road_names: set[str] = set()
        for face_index, face in enumerate(faces):
            attr = attributes_offset + face_index * 8
            texture_id = u32(data, attr + 4)
            name = names[texture_id] if texture_id < len(names) else f"texture_{texture_id}"
            if is_road_name(name):
                road_faces.append(face)
                road_names.add(name)
        offset = attributes_offset + polygon_count * 8
        if nya_type == 1:
            offset += point_count * 12
        if road_faces:
            selected.append(RoadMesh(", ".join(sorted(road_names)), vertices, road_faces))
    return mesh_count, selected


def vertex_key(vertex: Vertex, scale: float = 1000.0) -> tuple[int, int, int]:
    return round(vertex.x * scale), round(vertex.y * scale), round(vertex.z * scale)


def xz_key(vertex: Vertex, scale: float = 1000.0) -> tuple[int, int]:
    return round(vertex.x * scale), round(vertex.z * scale)


def road_vertices(meshes: Iterable[RoadMesh]) -> list[Vertex]:
    result: list[Vertex] = []
    for mesh in meshes:
        used = {index for face in mesh.faces for index in face}
        result.extend(mesh.vertices[index] for index in used)
    return result


def boundary_vertices(meshes: Iterable[RoadMesh]) -> list[Vertex]:
    # Mesh-local indices cannot be combined directly, so count geometric edges.
    edges: Counter[tuple[tuple[int, int, int], tuple[int, int, int]]] = Counter()
    positions: dict[tuple[int, int, int], Vertex] = {}
    for mesh in meshes:
        for face in mesh.faces:
            for a, b in zip(face, face[1:] + face[:1]):
                ka = vertex_key(mesh.vertices[a])
                kb = vertex_key(mesh.vertices[b])
                positions[ka] = mesh.vertices[a]
                positions[kb] = mesh.vertices[b]
                edges[tuple(sorted((ka, kb)))] += 1
    keys = {key for edge, count in edges.items() if count == 1 for key in edge}
    return [positions[key] for key in keys]


def face_grade(mesh: RoadMesh, face: tuple[int, ...]) -> float | None:
    a, b, c = (mesh.vertices[face[i]] for i in range(3))
    ux, uy, uz = b.x - a.x, b.y - a.y, b.z - a.z
    vx, vy, vz = c.x - a.x, c.y - a.y, c.z - a.z
    nx = uy * vz - uz * vy
    ny = uz * vx - ux * vz
    nz = ux * vy - uy * vx
    if abs(ny) < 1e-9:
        return None
    return math.hypot(nx, nz) / abs(ny)


def internal_xz_spread(vertices: Iterable[Vertex]) -> tuple[int, float]:
    columns: dict[tuple[int, int], list[float]] = defaultdict(list)
    for vertex in vertices:
        columns[xz_key(vertex)].append(vertex.y)
    spreads = [max(values) - min(values) for values in columns.values() if len(values) > 1]
    return len(spreads), max(spreads, default=0.0)


def seam_metrics(left: list[Vertex], right: list[Vertex], tolerance: float) -> dict[str, float | int]:
    matches: list[float] = []
    nearest_distance = math.inf
    nearest_dy = 0.0
    for a in left:
        candidates: list[tuple[float, Vertex]] = []
        for b in right:
            distance = math.hypot(a.x - b.x, a.z - b.z)
            if distance < nearest_distance:
                nearest_distance = distance
                nearest_dy = b.y - a.y
            if distance <= tolerance:
                candidates.append((abs(b.y - a.y), b))
        # Stacked road/pit surfaces may share XZ. Pair each boundary point with
        # the altitude-coherent counterpart instead of taking a Cartesian mix.
        if candidates:
            best = min(candidates, key=lambda item: item[0])[1]
            matches.append(best.y - a.y)
    return {
        "matches": len(matches),
        "max_abs_dy": max((abs(value) for value in matches), default=math.nan),
        "mean_abs_dy": (sum(abs(value) for value in matches) / len(matches)) if matches else math.nan,
        "nearest_xz": nearest_distance,
        "nearest_dy": nearest_dy,
    }


def fmt(value: float) -> str:
    return "n/a" if not math.isfinite(value) else f"{value:.4f}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True, help="Diretorio contendo SEG_###.NYA")
    parser.add_argument("--map", dest="map_path", type=Path, default=None, help="segments_map.json")
    parser.add_argument("--start", type=int, default=22)
    parser.add_argument("--end", type=int, default=42)
    parser.add_argument("--seam-xz-tolerance", type=float, default=0.05)
    parser.add_argument("--output", type=Path, default=Path("tools/reports/steep_track_mesh_022_042.md"))
    args = parser.parse_args()
    map_path = args.map_path or args.source / "segments_map.json"
    names_by_segment = load_segment_names(map_path)

    rows: list[dict[str, object]] = []
    boundaries: dict[int, list[Vertex]] = {}
    for segment_id in range(args.start, args.end + 1):
        path = args.source / f"SEG_{segment_id:03d}.NYA"
        if not path.exists():
            raise FileNotFoundError(path)
        mesh_count, meshes = read_nya(path, names_by_segment.get(segment_id, []))
        for mesh in meshes:
            mesh.faces = [
                face for face in mesh.faces
                if (grade := face_grade(mesh, face)) is not None and grade <= MAX_DRIVEABLE_TAN
            ]
        meshes = [mesh for mesh in meshes if mesh.faces]
        vertices = road_vertices(meshes)
        boundaries[segment_id] = boundary_vertices(meshes)
        grades = [grade for mesh in meshes for face in mesh.faces if (grade := face_grade(mesh, face)) is not None]
        duplicate_columns, max_xz_spread = internal_xz_spread(vertices)
        rows.append({
            "id": segment_id,
            "mesh_count": mesh_count,
            "road_meshes": len(meshes),
            "road_faces": sum(len(mesh.faces) for mesh in meshes),
            "road_vertices": len(vertices),
            "boundary_vertices": len(boundaries[segment_id]),
            "grade_max": max(grades, default=0.0),
            "duplicate_columns": duplicate_columns,
            "max_xz_spread": max_xz_spread,
            "names": sorted({mesh.name for mesh in meshes}),
        })

    seams = [
        (segment_id, segment_id + 1,
         seam_metrics(boundaries[segment_id], boundaries[segment_id + 1], args.seam_xz_tolerance))
        for segment_id in range(args.start, args.end)
    ]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        f"# Auditoria da malha — segmentos {args.start}–{args.end}",
        "",
        f"Fonte: `{args.source}`",
        "",
        "## Malha dirigível por segmento",
        "",
        "| Seg | Meshes NYA | Meshes pista | Faces | Vértices de borda | max |tan| | Sobreposição XZ max ΔY |",
        "|---:|---:|---:|---:|---:|---:|---:|",
    ]
    for row in rows:
        lines.append(
            f"| {row['id']} | {row['mesh_count']} | {row['road_meshes']} | {row['road_faces']} | "
            f"{row['boundary_vertices']} | {row['grade_max']:.4f} | {row['max_xz_spread']:.4f} |"
        )
    lines += [
        "",
        "## Continuidade entre segmentos",
        "",
        f"Correspondência usa distância XZ <= {args.seam_xz_tolerance:.3f} unidade.",
        "",
        "| Junção | Pares coincidentes | max |ΔY| | média |ΔY| | XZ mais próximo | ΔY desse par | Diagnóstico |",
        "|:--|--:|--:|--:|--:|--:|:--|",
    ]
    discontinuities = 0
    for left, right, seam in seams:
        max_dy = float(seam["max_abs_dy"])
        matches = int(seam["matches"])
        bad = matches == 0 or (math.isfinite(max_dy) and max_dy > 0.05)
        discontinuities += int(bad)
        diagnosis = "REVISAR" if bad else "soldada"
        lines.append(
            f"| {left}→{right} | {matches} | {fmt(max_dy)} | "
            f"{fmt(float(seam['mean_abs_dy']))} | {fmt(float(seam['nearest_xz']))} | "
            f"{fmt(float(seam['nearest_dy']))} | {diagnosis} |"
        )
    lines += [
        "",
        "## Conclusão automática",
        "",
        f"- Junções classificadas para revisão: **{discontinuities}/{len(seams)}**.",
        "- Uma junção `soldada` possui vértices de borda coincidentes em XZ e diferença de altitude <= 0,05.",
        "- Se as junções estiverem soldadas e o carro ainda descer em degraus, a causa está no filtro/snap do runtime, não na malha.",
        "",
    ]
    args.output.write_text("\n".join(lines), encoding="utf-8")
    print(f"Relatorio: {args.output}")
    print(f"Segmentos: {len(rows)}; juncoes para revisar: {discontinuities}/{len(seams)}")
    return 0 if discontinuities == 0 else 2


if __name__ == "__main__":
    raise SystemExit(main())
