#!/usr/bin/env python3
"""Repair INTERLAGOS_mundo_1 topology and build a 128x128 texture set.

The builder preserves the source geometry vertex count, face count, per-object
face count, material assignment, and 290 segment objects. Triangles become
real quads by consuming source-unused geometry vertices. Vertices with more
than four edge neighbors are split using indices freed from safe merges of
coincident source vertices, so the final ``v`` count remains unchanged.
"""

from __future__ import annotations

import argparse
import hashlib
import itertools
import json
import math
import re
import shutil
import subprocess
import sys
import tempfile
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


Vec2 = tuple[float, float]
Vec3 = tuple[float, float, float]


@dataclass
class Corner:
    vertex: int
    texcoord: int | None
    normal: int | None


@dataclass
class Face:
    corners: list[Corner]
    object_name: str
    material: str


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def parse_index(raw: str, count: int) -> int:
    value = int(raw)
    return value - 1 if value > 0 else count + value


def parse_obj(path: Path) -> tuple[list[Vec3], list[Vec2], list[Vec3], list[Face], list[str]]:
    vertices: list[Vec3] = []
    texcoords: list[Vec2] = []
    normals: list[Vec3] = []
    faces: list[Face] = []
    object_order: list[str] = []
    current_object = ""
    current_material = ""
    with path.open("r", encoding="utf-8", errors="replace") as handle:
        for raw in handle:
            parts = raw.strip().split()
            if not parts:
                continue
            tag = parts[0]
            if tag == "v" and len(parts) >= 4:
                vertices.append((float(parts[1]), float(parts[2]), float(parts[3])))
            elif tag == "vt" and len(parts) >= 3:
                texcoords.append((float(parts[1]), float(parts[2])))
            elif tag == "vn" and len(parts) >= 4:
                normals.append((float(parts[1]), float(parts[2]), float(parts[3])))
            elif tag == "o":
                current_object = " ".join(parts[1:])
                if current_object not in object_order:
                    object_order.append(current_object)
            elif tag == "usemtl":
                current_material = " ".join(parts[1:])
            elif tag == "f":
                corners: list[Corner] = []
                for token in parts[1:]:
                    fields = token.split("/")
                    vertex = parse_index(fields[0], len(vertices))
                    texcoord = parse_index(fields[1], len(texcoords)) if len(fields) > 1 and fields[1] else None
                    normal = parse_index(fields[2], len(normals)) if len(fields) > 2 and fields[2] else None
                    corners.append(Corner(vertex, texcoord, normal))
                faces.append(Face(corners, current_object, current_material))
    return vertices, texcoords, normals, faces, object_order


def build_graph(faces: list[Face], vertex_count: int) -> tuple[list[set[int]], list[set[int]]]:
    neighbors = [set() for _ in range(vertex_count)]
    incident = [set() for _ in range(vertex_count)]
    for face_index, face in enumerate(faces):
        ids = [corner.vertex for corner in face.corners]
        for a, b in zip(ids, ids[1:] + ids[:1]):
            if a == b:
                continue
            neighbors[a].add(b)
            neighbors[b].add(a)
        for vertex in ids:
            incident[vertex].add(face_index)
    return neighbors, incident


def polygon_area(vertices: list[Vec3], face: Face) -> float:
    points = [vertices[corner.vertex] for corner in face.corners]
    nx = ny = nz = 0.0
    for a, b in zip(points, points[1:] + points[:1]):
        nx += (a[1] - b[1]) * (a[2] + b[2])
        ny += (a[2] - b[2]) * (a[0] + b[0])
        nz += (a[0] - b[0]) * (a[1] + b[1])
    return 0.5 * math.sqrt(nx * nx + ny * ny + nz * nz)


def edge_length(vertices: list[Vec3], a: int, b: int) -> float:
    pa, pb = vertices[a], vertices[b]
    return math.sqrt(sum((pa[axis] - pb[axis]) ** 2 for axis in range(3)))


def convert_triangles_to_quads(
    vertices: list[Vec3], texcoords: list[Vec2], faces: list[Face], free_vertices: set[int]
) -> list[dict[str, object]]:
    repairs: list[dict[str, object]] = []
    for face_index, face in enumerate(faces):
        if len(face.corners) != 3:
            continue
        if not free_vertices:
            raise RuntimeError("No unused geometry vertex is available for triangle repair")
        edge_candidates = []
        for edge_index in range(3):
            a = face.corners[edge_index]
            b = face.corners[(edge_index + 1) % 3]
            edge_candidates.append((edge_length(vertices, a.vertex, b.vertex), edge_index))
        _, edge_index = max(edge_candidates)
        a = face.corners[edge_index]
        b = face.corners[(edge_index + 1) % 3]
        new_vertex = min(free_vertices)
        free_vertices.remove(new_vertex)
        pa, pb = vertices[a.vertex], vertices[b.vertex]
        vertices[new_vertex] = tuple((pa[axis] + pb[axis]) * 0.5 for axis in range(3))  # type: ignore[assignment]
        if a.texcoord is not None and b.texcoord is not None:
            ta, tb = texcoords[a.texcoord], texcoords[b.texcoord]
            texcoords.append(((ta[0] + tb[0]) * 0.5, (ta[1] + tb[1]) * 0.5))
            new_texcoord: int | None = len(texcoords) - 1
        else:
            new_texcoord = a.texcoord if a.texcoord is not None else b.texcoord
        new_corner = Corner(new_vertex, new_texcoord, a.normal if a.normal is not None else b.normal)
        face.corners.insert(edge_index + 1, new_corner)
        repairs.append(
            {
                "face": face_index + 1,
                "object": face.object_name,
                "material": face.material,
                "newVertex": new_vertex + 1,
                "splitEdge": [a.vertex + 1, b.vertex + 1],
            }
        )
    return repairs


def replace_vertex(faces: list[Face], old: int, new: int) -> None:
    for face in faces:
        for corner in face.corners:
            if corner.vertex == old:
                corner.vertex = new


def free_coincident_vertices(
    vertices: list[Vec3], faces: list[Face], free_vertices: set[int], target_free: int
) -> list[dict[str, int]]:
    merges: list[dict[str, int]] = []
    coordinate_groups: dict[Vec3, list[int]] = defaultdict(list)
    for index, position in enumerate(vertices):
        coordinate_groups[position].append(index)

    while len(free_vertices) < target_free:
        neighbors, incident = build_graph(faces, len(vertices))
        merged = False
        groups = sorted(coordinate_groups.values(), key=len, reverse=True)
        for group in groups:
            active = [index for index in group if index not in free_vertices and incident[index]]
            if len(active) < 2:
                continue
            active.sort(key=lambda index: (len(neighbors[index]), len(incident[index]), index))
            for keeper, donor in itertools.permutations(active, 2):
                if keeper == donor:
                    continue
                combined_neighbors = (neighbors[keeper] | neighbors[donor]) - {keeper, donor}
                if len(combined_neighbors) > 4:
                    continue
                if any(any(corner.vertex == keeper for corner in faces[fi].corners) for fi in incident[donor]):
                    continue
                keeper_objects = {faces[fi].object_name for fi in incident[keeper]}
                donor_objects = {faces[fi].object_name for fi in incident[donor]}
                if keeper_objects != donor_objects:
                    continue
                replace_vertex(faces, donor, keeper)
                free_vertices.add(donor)
                merges.append({"keeper": keeper + 1, "freed": donor + 1})
                merged = True
                break
            if merged:
                break
        if not merged:
            # Relax the same-object preference only if the conservative pass is exhausted.
            for group in groups:
                active = [index for index in group if index not in free_vertices and incident[index]]
                for keeper, donor in itertools.permutations(active, 2):
                    combined_neighbors = (neighbors[keeper] | neighbors[donor]) - {keeper, donor}
                    if len(combined_neighbors) > 4:
                        continue
                    if any(any(corner.vertex == keeper for corner in faces[fi].corners) for fi in incident[donor]):
                        continue
                    replace_vertex(faces, donor, keeper)
                    free_vertices.add(donor)
                    merges.append({"keeper": keeper + 1, "freed": donor + 1})
                    merged = True
                    break
                if merged:
                    break
        if not merged:
            break
    return merges


def local_neighbors_for_faces(faces: list[Face], vertex: int, face_indices: set[int]) -> set[int]:
    result: set[int] = set()
    for face_index in face_indices:
        ids = [corner.vertex for corner in faces[face_index].corners]
        for corner_index, value in enumerate(ids):
            if value != vertex:
                continue
            result.add(ids[(corner_index - 1) % len(ids)])
            result.add(ids[(corner_index + 1) % len(ids)])
    result.discard(vertex)
    return result


def split_high_degree_vertices(
    vertices: list[Vec3], faces: list[Face], free_vertices: set[int], max_degree: int = 4
) -> list[dict[str, object]]:
    splits: list[dict[str, object]] = []
    for _ in range(2048):
        neighbors, incident = build_graph(faces, len(vertices))
        bad = [index for index, values in enumerate(neighbors) if len(values) > max_degree]
        if not bad:
            return splits
        vertex = max(bad, key=lambda index: (len(neighbors[index]), len(incident[index])))
        if not free_vertices:
            raise RuntimeError("No freed coincident vertex remains for valence repair")
        affected = sorted(incident[vertex])
        if len(affected) < 2:
            raise RuntimeError(f"Cannot split vertex {vertex + 1}")

        adjacency_faces: dict[int, set[int]] = defaultdict(set)
        for face_index in affected:
            ids = [corner.vertex for corner in faces[face_index].corners]
            for corner_index, value in enumerate(ids):
                if value == vertex:
                    adjacency_faces[ids[(corner_index - 1) % len(ids)]].add(face_index)
                    adjacency_faces[ids[(corner_index + 1) % len(ids)]].add(face_index)

        best: tuple[tuple[int, int, int, int], set[int]] | None = None
        affected_set = set(affected)
        for moved_count in range(1, len(affected)):
            for moved_tuple in itertools.combinations(affected, moved_count):
                moved = set(moved_tuple)
                remaining = affected_set - moved
                moved_neighbors = local_neighbors_for_faces(faces, vertex, moved)
                remaining_neighbors = local_neighbors_for_faces(faces, vertex, remaining)
                if len(moved_neighbors) > max_degree:
                    continue
                if len(remaining_neighbors) >= len(neighbors[vertex]):
                    continue
                if len(remaining_neighbors) > max_degree and len(neighbors[vertex]) <= max_degree + 1:
                    continue
                split_neighbors = [
                    neighbor
                    for neighbor, linked_faces in adjacency_faces.items()
                    if linked_faces & moved and linked_faces & remaining
                ]
                new_neighbor_violations = sum(len(neighbors[neighbor]) >= max_degree for neighbor in split_neighbors)
                score = (
                    new_neighbor_violations,
                    len(split_neighbors),
                    max(len(moved_neighbors), len(remaining_neighbors)),
                    abs(len(moved) - len(remaining)),
                )
                if best is None or score < best[0]:
                    best = (score, moved)
        if best is None:
            raise RuntimeError(f"No legal face partition found for vertex {vertex + 1}")

        clone = min(free_vertices)
        free_vertices.remove(clone)
        vertices[clone] = vertices[vertex]
        moved_faces = best[1]
        for face_index in moved_faces:
            for corner in faces[face_index].corners:
                if corner.vertex == vertex:
                    corner.vertex = clone
        splits.append(
            {
                "sourceVertex": vertex + 1,
                "cloneVertex": clone + 1,
                "movedFaces": [index + 1 for index in sorted(moved_faces)],
            }
        )
    raise RuntimeError("Valence repair exceeded iteration limit")


def edge_metrics(faces: list[Face], vertex_count: int) -> dict[str, object]:
    neighbors, _ = build_graph(faces, vertex_count)
    edges: Counter[tuple[int, int]] = Counter()
    for face in faces:
        ids = [corner.vertex for corner in face.corners]
        for a, b in zip(ids, ids[1:] + ids[:1]):
            edges[tuple(sorted((a, b)))] += 1
    degrees = [len(values) for values in neighbors]
    return {
        "edges": len(edges),
        "boundaryEdges": sum(value == 1 for value in edges.values()),
        "manifoldEdges": sum(value == 2 for value in edges.values()),
        "nonManifoldEdges": sum(value > 2 for value in edges.values()),
        "maxVertexEdgeDegree": max(degrees, default=0),
        "verticesOverDegree4": sum(value > 4 for value in degrees),
        "degreeDistribution": dict(sorted(Counter(degrees).items())),
    }


def corner_token(corner: Corner) -> str:
    vertex = corner.vertex + 1
    if corner.texcoord is None and corner.normal is None:
        return str(vertex)
    texcoord = "" if corner.texcoord is None else str(corner.texcoord + 1)
    if corner.normal is None:
        return f"{vertex}/{texcoord}"
    return f"{vertex}/{texcoord}/{corner.normal + 1}"


def write_obj(
    path: Path,
    mtl_name: str,
    vertices: list[Vec3],
    texcoords: list[Vec2],
    normals: list[Vec3],
    faces: list[Face],
    object_order: list[str],
) -> None:
    by_object: dict[str, list[Face]] = defaultdict(list)
    for face in faces:
        by_object[face.object_name].append(face)
    with path.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write("# Interlagos 128 track - topology repaired without changing v/f counts\n")
        handle.write(f"mtllib {mtl_name}\n")
        for x, y, z in vertices:
            handle.write(f"v {x:.6f} {y:.6f} {z:.6f}\n")
        for u, v in texcoords:
            handle.write(f"vt {u:.6f} {v:.6f}\n")
        for x, y, z in normals:
            handle.write(f"vn {x:.6f} {y:.6f} {z:.6f}\n")
        for object_name in object_order:
            object_faces = by_object.get(object_name, [])
            if not object_faces:
                continue
            handle.write(f"o {object_name}\n")
            current_material = ""
            for face in object_faces:
                if face.material != current_material:
                    handle.write(f"usemtl {face.material}\n")
                    current_material = face.material
                handle.write("f " + " ".join(corner_token(corner) for corner in face.corners) + "\n")


def sanitize_texture_name(relative: str) -> str:
    stem = relative.replace("\\", "/")
    stem = re.sub(r"\.[Tt][Gg][Aa]$", "", stem)
    stem = re.sub(r"[^A-Za-z0-9]+", "_", stem).strip("_")
    return f"{stem}_128.TGA"


def extract_texture_paths(mtl_text: str) -> list[str]:
    result = []
    for raw in mtl_text.splitlines():
        match = re.match(r"\s*map_Kd\s+(.+?)\s*$", raw)
        if match and match.group(1) not in result:
            result.append(match.group(1))
    return result


def run_magick(magick: str, *arguments: str | Path) -> None:
    command = [magick, *(str(argument) for argument in arguments)]
    completed = subprocess.run(command, check=False, capture_output=True, text=True)
    if completed.returncode != 0:
        raise RuntimeError(f"ImageMagick failed: {' '.join(command)}\n{completed.stderr}")


def make_mirrored_tile(magick: str, source: Path, output: Path, work: Path) -> None:
    base = work / "base.png"
    horizontal = work / "horizontal.png"
    vertical = work / "vertical.png"
    both = work / "both.png"
    top = work / "top.png"
    bottom = work / "bottom.png"
    run_magick(magick, source, "-filter", "Lanczos", "-resize", "64x64!", base)
    run_magick(magick, base, "-flop", horizontal)
    run_magick(magick, base, "-flip", vertical)
    run_magick(magick, base, "-flip", "-flop", both)
    run_magick(magick, base, horizontal, "+append", top)
    run_magick(magick, vertical, both, "+append", bottom)
    run_magick(magick, top, bottom, "-append", "-alpha", "off", "-depth", "8", output)


def texture_override(relative: str) -> tuple[str | None, tuple[int, int, int]]:
    normalized = relative.replace("\\", "/").lower()
    basename = Path(normalized).name
    if normalized.endswith("arq_tga/f06364.tga"):
        return ("asphalt", (100, 100, 100))
    if basename == "f05464.tga":
        return ("kerb", (100, 100, 100))
    if basename in {"f00264.tga", "f00364.tga"}:
        return ("runoff", (100, 100, 100))
    grass_modulation = {
        "f02564.tga": (100, 100, 100),
        "f02364.tga": (92, 94, 100),
        "f02464.tga": (86, 88, 100),
        "f06464.tga": (82, 86, 100),
        "f06864.tga": (104, 88, 100),
    }
    if basename in grass_modulation:
        return ("grass", grass_modulation[basename])
    return (None, (100, 100, 100))


def build_textures(
    asset_root: Path,
    texture_paths: list[str],
    output_dir: Path,
    ai_sources: dict[str, Path],
    magick: str,
) -> tuple[dict[str, str], dict[str, object]]:
    output_dir.mkdir(parents=True, exist_ok=True)
    sources_dir = output_dir / "_sources"
    sources_dir.mkdir(exist_ok=True)
    copied_sources: dict[str, Path] = {}
    for semantic, source in ai_sources.items():
        target = sources_dir / f"{semantic}_imagegen_source.png"
        shutil.copy2(source, target)
        copied_sources[semantic] = target

    mapping: dict[str, str] = {}
    generated_count = 0
    enhanced_count = 0
    with tempfile.TemporaryDirectory(prefix="interlagos128_") as temp_name:
        temp = Path(temp_name)
        semantic_tiles: dict[str, Path] = {}
        for semantic, source in copied_sources.items():
            semantic_output = temp / f"{semantic}_tile.tga"
            semantic_work = temp / f"work_{semantic}"
            semantic_work.mkdir()
            make_mirrored_tile(magick, source, semantic_output, semantic_work)
            semantic_tiles[semantic] = semantic_output

        for relative in texture_paths:
            source = asset_root.joinpath(*relative.replace("\\", "/").split("/"))
            if not source.is_file():
                raise FileNotFoundError(f"Texture not found: {source}")
            filename = sanitize_texture_name(relative)
            target = output_dir / filename
            semantic, modulation = texture_override(relative)
            if semantic:
                base = semantic_tiles[semantic]
                run_magick(
                    magick,
                    base,
                    "-modulate",
                    f"{modulation[0]},{modulation[1]},{modulation[2]}",
                    "-alpha",
                    "off",
                    "-depth",
                    "8",
                    target,
                )
                generated_count += 1
            else:
                run_magick(
                    magick,
                    source,
                    "-filter",
                    "Lanczos",
                    "-resize",
                    "128x128!",
                    "-unsharp",
                    "0x0.55+0.65+0.015",
                    "-alpha",
                    "off",
                    "-depth",
                    "8",
                    target,
                )
                enhanced_count += 1
            mapping[relative] = f"{output_dir.name}/{filename}"

    preview = output_dir.parent / "INTERLAGOS_mundo_1_128_textures_preview.png"
    preview_inputs = [output_dir / sanitize_texture_name(relative) for relative in texture_paths]
    run_magick(
        magick,
        "montage",
        *preview_inputs,
        "-thumbnail",
        "128x128",
        "-tile",
        "8x",
        "-geometry",
        "128x128+3+3",
        "-background",
        "#202124",
        preview,
    )
    return mapping, {
        "textureCount": len(texture_paths),
        "imagegenDerivedTextures": generated_count,
        "enhancedSourceTextures": enhanced_count,
        "preview": str(preview),
        "imagegenSources": {key: str(value) for key, value in copied_sources.items()},
    }


def rewrite_mtl(source_text: str, texture_mapping: dict[str, str]) -> str:
    output = []
    for raw in source_text.splitlines():
        match = re.match(r"(\s*map_Kd\s+)(.+?)(\s*)$", raw)
        if match and match.group(2) in texture_mapping:
            output.append(match.group(1) + texture_mapping[match.group(2)])
        else:
            output.append(raw)
    return "\n".join(output) + "\n"


def tga_dimensions(path: Path) -> tuple[int, int]:
    header = path.read_bytes()[:18]
    if len(header) < 18:
        raise ValueError(f"Invalid TGA: {path}")
    return (int.from_bytes(header[12:14], "little"), int.from_bytes(header[14:16], "little"))


def validate(
    source_counts: dict[str, int],
    vertices: list[Vec3],
    texcoords: list[Vec2],
    faces: list[Face],
    object_order: list[str],
    texture_dir: Path,
    texture_mapping: dict[str, str],
) -> dict[str, object]:
    metrics = edge_metrics(faces, len(vertices))
    object_face_counts = Counter(face.object_name for face in faces)
    errors = []
    if len(vertices) != source_counts["vertices"]:
        errors.append("geometry vertex count changed")
    if len(faces) != source_counts["faces"]:
        errors.append("face count changed")
    if len(faces) > 5000:
        errors.append("face count exceeds 5000")
    if any(len(face.corners) != 4 for face in faces):
        errors.append("non-quad face remains")
    if metrics["maxVertexEdgeDegree"] > 4:
        errors.append("vertex edge degree exceeds four")
    if len(object_face_counts) != 290:
        errors.append("segment object count is not 290")
    if any(len({corner.vertex for corner in face.corners}) != 4 for face in faces):
        errors.append("quad contains repeated geometry indices")
    areas = [polygon_area(vertices, face) for face in faces]
    if min(areas, default=0.0) <= 1.0e-7:
        errors.append("degenerate geometry quad found")
    invalid_textures = []
    for output_relative in texture_mapping.values():
        texture = texture_dir.parent / Path(output_relative)
        if not texture.is_file() or tga_dimensions(texture) != (128, 128):
            invalid_textures.append(str(texture))
    if invalid_textures:
        errors.append("one or more output textures are missing or not 128x128")
    if errors:
        raise RuntimeError("; ".join(errors))
    return {
        "vertices": len(vertices),
        "texcoords": len(texcoords),
        "faces": len(faces),
        "quads": sum(len(face.corners) == 4 for face in faces),
        "triangles": sum(len(face.corners) == 3 for face in faces),
        "segmentObjects": len(object_face_counts),
        "minFacesPerSegment": min(object_face_counts.values()),
        "maxFacesPerSegment": max(object_face_counts.values()),
        "minQuadArea": min(areas),
        **metrics,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source_obj", type=Path)
    parser.add_argument("--source-mtl", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--ai-asphalt", type=Path, required=True)
    parser.add_argument("--ai-grass", type=Path, required=True)
    parser.add_argument("--ai-kerb", type=Path, required=True)
    parser.add_argument("--ai-runoff", type=Path, required=True)
    parser.add_argument("--magick", default=shutil.which("magick"))
    args = parser.parse_args()

    source_obj = args.source_obj.resolve()
    source_mtl = (args.source_mtl or source_obj.with_suffix(".mtl")).resolve()
    output_obj = (args.output or source_obj.with_name("INTERLAGOS_mundo_1_128.obj")).resolve()
    output_mtl = output_obj.with_suffix(".mtl")
    report_path = output_obj.with_name(output_obj.stem + "_report.json")
    texture_dir = output_obj.parent / "ARQ_TGA_128"
    if not args.magick:
        parser.error("ImageMagick 'magick' executable was not found")
    required_paths = [source_obj, source_mtl, args.ai_asphalt, args.ai_grass, args.ai_kerb, args.ai_runoff]
    if any(not path.resolve().is_file() for path in required_paths):
        parser.error("one or more required source files do not exist")
    if output_obj == source_obj or output_mtl == source_mtl:
        parser.error("output must not overwrite the source OBJ/MTL")

    vertices, texcoords, normals, faces, object_order = parse_obj(source_obj)
    source_counts = {"vertices": len(vertices), "texcoords": len(texcoords), "faces": len(faces)}
    source_metrics = {
        **source_counts,
        "quads": sum(len(face.corners) == 4 for face in faces),
        "triangles": sum(len(face.corners) == 3 for face in faces),
        "segmentObjects": len({face.object_name for face in faces}),
        **edge_metrics(faces, len(vertices)),
    }
    _, incident = build_graph(faces, len(vertices))
    free_vertices = {index for index, linked_faces in enumerate(incident) if not linked_faces}
    triangle_repairs = convert_triangles_to_quads(vertices, texcoords, faces, free_vertices)
    coincident_merges = free_coincident_vertices(vertices, faces, free_vertices, target_free=256)
    valence_splits = split_high_degree_vertices(vertices, faces, free_vertices)

    source_mtl_text = source_mtl.read_text(encoding="utf-8", errors="replace")
    texture_paths = extract_texture_paths(source_mtl_text)
    ai_sources = {
        "asphalt": args.ai_asphalt.resolve(),
        "grass": args.ai_grass.resolve(),
        "kerb": args.ai_kerb.resolve(),
        "runoff": args.ai_runoff.resolve(),
    }
    texture_mapping, texture_report = build_textures(
        source_obj.parent, texture_paths, texture_dir, ai_sources, args.magick
    )
    output_mtl.write_text(rewrite_mtl(source_mtl_text, texture_mapping), encoding="utf-8", newline="\n")
    write_obj(output_obj, output_mtl.name, vertices, texcoords, normals, faces, object_order)
    output_metrics = validate(
        source_counts, vertices, texcoords, faces, object_order, texture_dir, texture_mapping
    )
    report = {
        "generator": "tools/build_interlagos_128_track.py",
        "sourceObj": str(source_obj),
        "sourceMtl": str(source_mtl),
        "sourceObjSha256": sha256(source_obj),
        "sourceMtlSha256": sha256(source_mtl),
        "outputObj": str(output_obj),
        "outputMtl": str(output_mtl),
        "outputObjSha256": sha256(output_obj),
        "outputMtlSha256": sha256(output_mtl),
        "source": source_metrics,
        "output": output_metrics,
        "repairs": {
            "triangleToQuad": triangle_repairs,
            "coincidentVertexMerges": coincident_merges,
            "valenceSplits": valence_splits,
            "unusedGeometryVerticesAfterRepair": len(free_vertices),
        },
        "textures": {
            **texture_report,
            "outputDirectory": str(texture_dir),
            "mapping": texture_mapping,
        },
    }
    report_path.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
