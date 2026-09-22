#!/usr/bin/env python3
"""Conservatively weld coincident LOD0 asphalt edges across OBJ segments.

This tool changes only ``v`` records.  It never changes face order, OBJ vertex
indices, UVs, materials, or the surface-family map that feeds GEO/MAT/SDR.
"""

from __future__ import annotations

import argparse
import json
import math
import re
import shutil
import sys
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


SEGMENT_RE = re.compile(r"^seg_(\d+)\.obj$", re.IGNORECASE)
ASPHALT_SURFACE_TYPE = 1


@dataclass(frozen=True, order=True)
class VertexRef:
    segment_id: int
    vertex_index: int


@dataclass
class Edge:
    segment_id: int
    face_index: int
    a: VertexRef
    b: VertexRef
    pa: tuple[float, float, float]
    pb: tuple[float, float, float]


@dataclass
class ObjSegment:
    segment_id: int
    path: Path
    lines: list[str]
    vertices: list[tuple[float, float, float]]
    vertex_line_indices: list[int]
    faces: list[list[int]]


class UnionFind:
    def __init__(self) -> None:
        self.parent: dict[VertexRef, VertexRef] = {}

    def find(self, value: VertexRef) -> VertexRef:
        parent = self.parent.setdefault(value, value)
        if parent != value:
            parent = self.find(parent)
            self.parent[value] = parent
        return parent

    def union(self, left: VertexRef, right: VertexRef) -> None:
        left_root = self.find(left)
        right_root = self.find(right)
        if left_root == right_root:
            return
        if right_root < left_root:
            left_root, right_root = right_root, left_root
        self.parent[right_root] = left_root


def parse_vertex_index(token: str, vertex_count: int) -> int:
    index_text = token.split("/", 1)[0]
    if not index_text:
        raise ValueError(f"face vertex index missing in token '{token}'")
    raw = int(index_text)
    index = raw - 1 if raw > 0 else vertex_count + raw
    if index < 0 or index >= vertex_count:
        raise ValueError(f"face vertex index {raw} outside OBJ vertex range")
    return index


def load_obj(path: Path, segment_id: int) -> ObjSegment:
    lines = path.read_text(encoding="utf-8", errors="strict").splitlines(keepends=True)
    vertices: list[tuple[float, float, float]] = []
    vertex_line_indices: list[int] = []
    raw_faces: list[list[str]] = []

    for line_index, line in enumerate(lines):
        stripped = line.strip()
        if stripped.startswith("v "):
            parts = stripped.split()
            if len(parts) < 4:
                raise ValueError(f"{path.name}: malformed vertex line {line_index + 1}")
            vertices.append((float(parts[1]), float(parts[2]), float(parts[3])))
            vertex_line_indices.append(line_index)
        elif stripped.startswith("f "):
            parts = stripped.split()
            if len(parts) < 4:
                raise ValueError(f"{path.name}: malformed face line {line_index + 1}")
            raw_faces.append(parts[1:])

    # Keep face numbering identical to generate_segment_component.ps1: OBJ
    # n-gons are emitted as a fan before GEO/MAT receives the face-family map.
    faces: list[list[int]] = []
    for raw_face in raw_faces:
        face = [parse_vertex_index(token, len(vertices)) for token in raw_face]
        if len(face) > 4:
            for corner in range(1, len(face) - 1):
                faces.append([face[0], face[corner], face[corner + 1]])
        else:
            faces.append(face)
    if not vertices or not faces:
        raise ValueError(f"{path.name}: OBJ has no usable vertices/faces")
    return ObjSegment(segment_id, path, lines, vertices, vertex_line_indices, faces)


def surface_types_by_segment(map_path: Path) -> dict[int, list[int]]:
    # PowerShell Set-Content may emit a UTF-8 BOM for the temporary map.
    # utf-8-sig accepts both that form and ordinary UTF-8 deterministically.
    data = json.loads(map_path.read_text(encoding="utf-8-sig"))
    result: dict[int, list[int]] = {}
    for segment in data.get("segments", []):
        segment_id = int(segment["id"])
        values = segment.get("faceSurfaceType")
        if isinstance(values, list) and values:
            result[segment_id] = [int(value) for value in values]
            continue
        faces = segment.get("faces", [])
        if faces:
            ordered = sorted(faces, key=lambda face: int(face.get("index", 0)))
            result[segment_id] = [int(face.get("surfaceTypeId", 0)) for face in ordered]
    return result


def distance(left: tuple[float, float, float], right: tuple[float, float, float]) -> float:
    return math.sqrt(sum((a - b) ** 2 for a, b in zip(left, right)))


def cell(point: tuple[float, float, float], tolerance: float) -> tuple[int, int]:
    # X/Z identify track continuity; Y is deliberately tested separately so a
    # genuine elevation transition is never welded merely because it overlaps in plan view.
    return (math.floor(point[0] / tolerance), math.floor(point[2] / tolerance))


def canonical_edge_cell(left: tuple[int, int], right: tuple[int, int]) -> tuple[tuple[int, int], tuple[int, int]]:
    return (left, right) if left <= right else (right, left)


def adjacent_edge_cells(edge: Edge, tolerance: float) -> Iterable[tuple[tuple[int, int], tuple[int, int]]]:
    a_cell = cell(edge.pa, tolerance)
    b_cell = cell(edge.pb, tolerance)
    emitted: set[tuple[tuple[int, int], tuple[int, int]]] = set()
    for ax in range(a_cell[0] - 1, a_cell[0] + 2):
        for az in range(a_cell[1] - 1, a_cell[1] + 2):
            for bx in range(b_cell[0] - 1, b_cell[0] + 2):
                for bz in range(b_cell[1] - 1, b_cell[1] + 2):
                    key = canonical_edge_cell((ax, az), (bx, bz))
                    if key not in emitted:
                        emitted.add(key)
                        yield key


def edge_pair_mapping(left: Edge, right: Edge, tolerance: float) -> list[tuple[VertexRef, VertexRef]] | None:
    direct = distance(left.pa, right.pa) <= tolerance and distance(left.pb, right.pb) <= tolerance
    if direct:
        return [(left.a, right.a), (left.b, right.b)]
    reverse = distance(left.pa, right.pb) <= tolerance and distance(left.pb, right.pa) <= tolerance
    if reverse:
        return [(left.a, right.b), (left.b, right.a)]
    return None


def write_obj_vertices(segment: ObjSegment, replacements: dict[int, tuple[float, float, float]]) -> None:
    for vertex_index, position in replacements.items():
        line_index = segment.vertex_line_indices[vertex_index]
        original = segment.lines[line_index]
        newline = "\r\n" if original.endswith("\r\n") else "\n" if original.endswith("\n") else ""
        prefix_length = len(original) - len(original.lstrip())
        prefix = original[:prefix_length]
        fields = original.strip().split()
        fields[1:4] = [f"{value:.9f}" for value in position]
        segment.lines[line_index] = prefix + " ".join(fields) + newline
    segment.path.write_text("".join(segment.lines), encoding="utf-8", newline="")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--obj-dir", required=True, type=Path)
    parser.add_argument("--segments-map", required=True, type=Path)
    parser.add_argument("--report", required=True, type=Path)
    parser.add_argument("--tolerance", type=float, default=0.25)
    parser.add_argument("--apply", action="store_true")
    parser.add_argument("--backup-dir", type=Path)
    args = parser.parse_args()

    if args.tolerance <= 0.0:
        raise ValueError("--tolerance must be greater than zero")
    if args.apply and args.backup_dir is None:
        raise ValueError("--backup-dir is required with --apply")

    surface_types = surface_types_by_segment(args.segments_map)
    segments: dict[int, ObjSegment] = {}
    asphalt_faces = 0
    edges: list[Edge] = []
    for path in sorted(args.obj_dir.glob("seg_*.obj")):
        match = SEGMENT_RE.match(path.name)
        if not match:
            continue
        segment_id = int(match.group(1))
        if segment_id not in surface_types:
            raise ValueError(f"{path.name}: segment missing from surface map")
        segment = load_obj(path, segment_id)
        types = surface_types[segment_id]
        if len(types) != len(segment.faces):
            raise ValueError(
                f"{path.name}: OBJ faces={len(segment.faces)} but faceSurfaceType={len(types)}; refusing to weld"
            )
        segments[segment_id] = segment
        for face_index, (face, surface_type) in enumerate(zip(segment.faces, types)):
            if surface_type != ASPHALT_SURFACE_TYPE:
                continue
            asphalt_faces += 1
            for corner in range(len(face)):
                left_index = face[corner]
                right_index = face[(corner + 1) % len(face)]
                edges.append(
                    Edge(segment_id, face_index,
                         VertexRef(segment_id, left_index), VertexRef(segment_id, right_index),
                         segment.vertices[left_index], segment.vertices[right_index])
                )

    buckets: dict[tuple[tuple[int, int], tuple[int, int]], list[int]] = defaultdict(list)
    for edge_index, edge in enumerate(edges):
        buckets[canonical_edge_cell(cell(edge.pa, args.tolerance), cell(edge.pb, args.tolerance))].append(edge_index)

    union_find = UnionFind()
    matching_edge_pairs = 0
    seen_pairs: set[tuple[int, int]] = set()
    for edge_index, edge in enumerate(edges):
        for key in adjacent_edge_cells(edge, args.tolerance):
            for other_index in buckets.get(key, []):
                if other_index == edge_index:
                    continue
                pair = (edge_index, other_index) if edge_index < other_index else (other_index, edge_index)
                if pair in seen_pairs:
                    continue
                seen_pairs.add(pair)
                other = edges[other_index]
                if edge.segment_id == other.segment_id and edge.face_index == other.face_index:
                    continue
                mapping = edge_pair_mapping(edge, other, args.tolerance)
                if mapping is None:
                    continue
                matching_edge_pairs += 1
                for left, right in mapping:
                    union_find.union(left, right)

    positions: dict[VertexRef, tuple[float, float, float]] = {}
    for segment in segments.values():
        for index, position in enumerate(segment.vertices):
            positions[VertexRef(segment.segment_id, index)] = position
    groups: dict[VertexRef, list[VertexRef]] = defaultdict(list)
    for reference in union_find.parent:
        groups[union_find.find(reference)].append(reference)

    replacements: dict[int, dict[int, tuple[float, float, float]]] = defaultdict(dict)
    rejected_groups = 0
    change_examples: list[dict[str, object]] = []
    max_displacement = 0.0
    for members in groups.values():
        if len(members) < 2:
            continue
        canonical_ref = min(members)
        canonical_position = positions[canonical_ref]
        if any(distance(canonical_position, positions[member]) > args.tolerance for member in members):
            rejected_groups += 1
            continue
        for member in members:
            delta = distance(canonical_position, positions[member])
            if delta == 0.0:
                continue
            replacements[member.segment_id][member.vertex_index] = canonical_position
            max_displacement = max(max_displacement, delta)
            if len(change_examples) < 32:
                change_examples.append({
                    "segmentId": member.segment_id,
                    "vertexIndex": member.vertex_index,
                    "canonicalSegmentId": canonical_ref.segment_id,
                    "canonicalVertexIndex": canonical_ref.vertex_index,
                    "displacement": delta,
                })

    changed_files = sorted(segment_id for segment_id, items in replacements.items() if items)
    if args.apply:
        assert args.backup_dir is not None
        args.backup_dir.mkdir(parents=True, exist_ok=True)
        for segment_id in changed_files:
            segment = segments[segment_id]
            shutil.copy2(segment.path, args.backup_dir / segment.path.name)
            write_obj_vertices(segment, replacements[segment_id])

    report = {
        "version": 1,
        "mode": "apply" if args.apply else "audit",
        "objDir": str(args.obj_dir),
        "segmentsMap": str(args.segments_map),
        "tolerance": args.tolerance,
        "segments": len(segments),
        "asphaltFaces": asphalt_faces,
        "asphaltEdges": len(edges),
        "matchingEdgePairs": matching_edge_pairs,
        "candidateVertexGroups": len(groups),
        "rejectedGroups": rejected_groups,
        "changedVertices": sum(len(items) for items in replacements.values()),
        "changedFiles": changed_files,
        "maxDisplacement": max_displacement,
        "backupDir": str(args.backup_dir) if args.apply else None,
        "changes": change_examples,
    }
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(
        "LOD0 asphalt weld: "
        f"segments={report['segments']} asphaltFaces={asphalt_faces} "
        f"edgePairs={matching_edge_pairs} changedVertices={report['changedVertices']} "
        f"maxDelta={max_displacement:.6f} mode={report['mode']}"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:  # keep PowerShell build error concise and actionable
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
