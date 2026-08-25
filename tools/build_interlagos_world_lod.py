#!/usr/bin/env python3
"""Build an always-resident, quad-only world LOD from the Interlagos OBJ.

The source is expected to contain ordered objects named ``pista_seg.NNN``.
The generated mesh is a closed four-vertex ring strip:

    outer-left -- road-left -- road-right -- outer-right

Each pair of consecutive rings creates three quads. With 200 rings the output
therefore has exactly 600 faces. Shared position vertices keep the maximum
edge degree at four while OBJ UV indices remain per-face.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import struct
import sys
import zlib
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


Vec3 = tuple[float, float, float]


@dataclass
class Face:
    vertices: tuple[int, ...]
    material: str
    object_name: str


@dataclass
class Station:
    center: Vec3
    road_half_left: float
    road_half_right: float
    outer_left: float
    outer_right: float
    road_y_left: float
    road_y_right: float
    outer_y_left: float
    outer_y_right: float
    road_texture: str
    left_texture: str
    right_texture: str


def add(a: Vec3, b: Vec3) -> Vec3:
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def sub(a: Vec3, b: Vec3) -> Vec3:
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def mul(a: Vec3, value: float) -> Vec3:
    return (a[0] * value, a[1] * value, a[2] * value)


def distance_xz(a: Vec3, b: Vec3) -> float:
    return math.hypot(b[0] - a[0], b[2] - a[2])


def normalize_xz(dx: float, dz: float) -> tuple[float, float]:
    length = math.hypot(dx, dz)
    if length <= 1.0e-9:
        return (0.0, 1.0)
    return (dx / length, dz / length)


def polygon_normal_area(vertices: list[Vec3], indices: tuple[int, ...]) -> tuple[Vec3, float]:
    if len(indices) < 3:
        return ((0.0, 0.0, 0.0), 0.0)
    nx = ny = nz = 0.0
    for ia, ib in zip(indices, indices[1:] + indices[:1]):
        a, b = vertices[ia], vertices[ib]
        nx += (a[1] - b[1]) * (a[2] + b[2])
        ny += (a[2] - b[2]) * (a[0] + b[0])
        nz += (a[0] - b[0]) * (a[1] + b[1])
    length = math.sqrt(nx * nx + ny * ny + nz * nz)
    if length <= 1.0e-12:
        return ((0.0, 0.0, 0.0), 0.0)
    return ((nx / length, ny / length, nz / length), 0.5 * length)


def face_centroid(vertices: list[Vec3], face: Face) -> Vec3:
    count = len(face.vertices)
    return tuple(sum(vertices[i][axis] for i in face.vertices) / count for axis in range(3))  # type: ignore[return-value]


def material_base(name: str) -> str:
    return re.sub(r"(?:\.\d+)+$", "", name).lower()


def is_main_road(material: str) -> bool:
    return material_base(material) == "asfalto_64"


def is_ground(material: str) -> bool:
    base = material_base(material)
    if is_main_road(material):
        return False
    tokens = (
        "grama",
        "grass",
        "escape",
        "scape",
        "cinza",
        "gramalow",
        "zebra",
        "faixa",
        "branco",
        "btanco",
        "amarelo",
    )
    return any(token in base for token in tokens)


def parse_obj(path: Path) -> tuple[list[Vec3], list[Face], list[str]]:
    vertices: list[Vec3] = []
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
            elif tag == "o":
                current_object = " ".join(parts[1:])
                if current_object not in object_order:
                    object_order.append(current_object)
            elif tag == "usemtl":
                current_material = " ".join(parts[1:])
            elif tag == "f":
                indices: list[int] = []
                for token in parts[1:]:
                    raw_index = int(token.split("/", 1)[0])
                    index = raw_index - 1 if raw_index > 0 else len(vertices) + raw_index
                    indices.append(index)
                faces.append(Face(tuple(indices), current_material, current_object))
    return vertices, faces, object_order


def parse_mtl(path: Path) -> dict[str, str]:
    result: dict[str, str] = {}
    current = ""
    with path.open("r", encoding="utf-8", errors="replace") as handle:
        for raw in handle:
            parts = raw.strip().split(maxsplit=1)
            if not parts:
                continue
            if parts[0] == "newmtl" and len(parts) == 2:
                current = parts[1]
            elif parts[0] == "map_Kd" and len(parts) == 2 and current:
                result[current] = parts[1]
    return result


def tga_dimensions(path: Path) -> tuple[int, int] | None:
    try:
        header = path.read_bytes()[:18]
    except OSError:
        return None
    if len(header) < 18:
        return None
    return (int.from_bytes(header[12:14], "little"), int.from_bytes(header[14:16], "little"))


def prefer_existing_8x8_textures(material_to_texture: dict[str, str], asset_root: Path) -> dict[str, str]:
    """Resolve high-resolution aliases to an existing <=8x8 sibling when available."""
    result: dict[str, str] = {}
    for material, texture in material_to_texture.items():
        normalized = texture.replace("\\", "/")
        basename = Path(normalized).name
        candidates = [f"ARQ_TGA/{basename}", f"ARQ_TGA/8_renomeados/{basename}", normalized]
        chosen = normalized
        for candidate in candidates:
            candidate_path = asset_root.joinpath(*candidate.split("/"))
            dimensions = tga_dimensions(candidate_path)
            if dimensions and dimensions[0] <= 8 and dimensions[1] <= 8:
                chosen = candidate
                break
        result[material] = chosen
    return result


def ordered_track_objects(object_order: Iterable[str]) -> list[str]:
    numbered: list[tuple[int, str]] = []
    for name in object_order:
        match = re.fullmatch(r"pista_seg\.(\d+)", name, flags=re.IGNORECASE)
        if match:
            numbered.append((int(match.group(1)), name))
    numbered.sort()
    if not numbered:
        raise ValueError("No ordered pista_seg.NNN objects were found")
    expected = list(range(numbered[0][0], numbered[-1][0] + 1))
    actual = [number for number, _ in numbered]
    if actual != expected:
        raise ValueError("pista_seg object numbering is not contiguous")
    return [name for _, name in numbered]


def weighted_face_center(vertices: list[Vec3], faces: list[Face]) -> Vec3 | None:
    total = 0.0
    accum = (0.0, 0.0, 0.0)
    for face in faces:
        _, area = polygon_normal_area(vertices, face.vertices)
        if area <= 1.0e-9:
            continue
        accum = add(accum, mul(face_centroid(vertices, face), area))
        total += area
    return mul(accum, 1.0 / total) if total > 0.0 else None


def fill_periodic_missing(values: list[Vec3 | float | None]) -> list[Vec3 | float]:
    count = len(values)
    valid = [i for i, value in enumerate(values) if value is not None]
    if not valid:
        raise ValueError("Cannot interpolate a field with no valid values")
    output: list[Vec3 | float | None] = list(values)
    for index, value in enumerate(values):
        if value is not None:
            continue
        before = max((candidate for candidate in valid if candidate < index), default=valid[-1] - count)
        after = min((candidate for candidate in valid if candidate > index), default=valid[0] + count)
        a = values[before % count]
        b = values[after % count]
        assert a is not None and b is not None
        t = (index - before) / (after - before)
        if isinstance(a, tuple):
            assert isinstance(b, tuple)
            output[index] = tuple(a[axis] + (b[axis] - a[axis]) * t for axis in range(3))
        else:
            assert isinstance(b, float)
            output[index] = a + (b - a) * t
    return [value for value in output if value is not None]


def dominant_texture(
    vertices: list[Vec3], faces: Iterable[Face], material_to_texture: dict[str, str], fallback: str
) -> str:
    weights: Counter[str] = Counter()
    for face in faces:
        texture = material_to_texture.get(face.material)
        if not texture:
            continue
        _, area = polygon_normal_area(vertices, face.vertices)
        weights[texture] += max(1, int(round(area * 100.0)))
    return weights.most_common(1)[0][0] if weights else fallback


def fit_y_by_lateral(vertices: list[Vec3], indices: list[int], center: Vec3, lateral: tuple[float, float]) -> tuple[float, float]:
    samples = []
    for index in indices:
        point = vertices[index]
        projection = (point[0] - center[0]) * lateral[0] + (point[2] - center[2]) * lateral[1]
        samples.append((projection, point[1]))
    if not samples:
        return (center[1], 0.0)
    mean_x = sum(x for x, _ in samples) / len(samples)
    mean_y = sum(y for _, y in samples) / len(samples)
    denominator = sum((x - mean_x) ** 2 for x, _ in samples)
    slope = sum((x - mean_x) * (y - mean_y) for x, y in samples) / denominator if denominator > 1.0e-9 else 0.0
    return (mean_y - slope * mean_x, slope)


def smooth_periodic(values: list[float], passes: int) -> list[float]:
    output = list(values)
    for _ in range(passes):
        output = [
            (output[(i - 1) % len(output)] + 2.0 * output[i] + output[(i + 1) % len(output)]) / 4.0
            for i in range(len(output))
        ]
    return output


def build_source_stations(
    vertices: list[Vec3],
    faces: list[Face],
    objects: list[str],
    material_to_texture: dict[str, str],
    min_shoulder: float,
    max_shoulder: float,
) -> tuple[list[Station], dict[str, object]]:
    by_object: dict[str, list[Face]] = defaultdict(list)
    for face in faces:
        by_object[face.object_name].append(face)

    road_by_object: list[list[Face]] = []
    raw_centers: list[Vec3 | None] = []
    for name in objects:
        road_faces = [face for face in by_object[name] if len(face.vertices) == 4 and is_main_road(face.material)]
        road_by_object.append(road_faces)
        raw_centers.append(weighted_face_center(vertices, road_faces))

    missing_road = [objects[i] for i, value in enumerate(raw_centers) if value is None]
    centers = [value for value in fill_periodic_missing(raw_centers)]
    assert all(isinstance(value, tuple) for value in centers)
    centers = list(centers)  # type: ignore[assignment]

    all_road_faces = [face for group in road_by_object for face in group]
    all_ground_faces = [
        face
        for face in faces
        if len(face.vertices) == 4
        and is_ground(face.material)
        and abs(polygon_normal_area(vertices, face.vertices)[0][1]) >= 0.55
    ]
    fallback_road = dominant_texture(vertices, all_road_faces, material_to_texture, "")
    fallback_ground = dominant_texture(vertices, all_ground_faces, material_to_texture, fallback_road)
    if not fallback_road:
        raise ValueError("No textured main-road faces were found")

    half_left: list[float | None] = []
    half_right: list[float | None] = []
    outer_left: list[float | None] = []
    outer_right: list[float | None] = []
    road_y_left: list[float | None] = []
    road_y_right: list[float | None] = []
    outer_y_left: list[float | None] = []
    outer_y_right: list[float | None] = []
    adjusted_centers: list[Vec3] = []
    road_textures: list[str] = []
    left_textures: list[str] = []
    right_textures: list[str] = []

    for index, name in enumerate(objects):
        center = centers[index]
        previous = centers[(index - 1) % len(objects)]
        following = centers[(index + 1) % len(objects)]
        tangent = normalize_xz(following[0] - previous[0], following[2] - previous[2])
        lateral = (-tangent[1], tangent[0])
        road_faces = road_by_object[index]
        road_indices = sorted({vertex for face in road_faces for vertex in face.vertices})

        if road_indices:
            projections = [
                (vertices[vertex][0] - center[0]) * lateral[0]
                + (vertices[vertex][2] - center[2]) * lateral[1]
                for vertex in road_indices
            ]
            low, high = min(projections), max(projections)
            lateral_shift = 0.5 * (low + high)
            center = (center[0] + lateral[0] * lateral_shift, center[1], center[2] + lateral[1] * lateral_shift)
            road_half = min(60.0, max(4.0, 0.5 * (high - low)))
            intercept_y, slope_y = fit_y_by_lateral(vertices, road_indices, center, lateral)
            y_low = intercept_y - slope_y * road_half
            y_high = intercept_y + slope_y * road_half
            y_values = [vertices[vertex][1] for vertex in road_indices]
            y_low = max(min(y_values), min(max(y_values), y_low))
            y_high = max(min(y_values), min(max(y_values), y_high))
            half_left.append(road_half)
            half_right.append(road_half)
            road_y_left.append(y_high)
            road_y_right.append(y_low)
        else:
            half_left.append(None)
            half_right.append(None)
            road_y_left.append(None)
            road_y_right.append(None)
        adjusted_centers.append(center)

        candidates = [
            face
            for face in by_object[name]
            if len(face.vertices) == 4
            and is_ground(face.material)
            and abs(polygon_normal_area(vertices, face.vertices)[0][1]) >= 0.55
        ]
        left_faces: list[Face] = []
        right_faces: list[Face] = []
        left_points: list[tuple[float, float]] = []
        right_points: list[tuple[float, float]] = []
        for face in candidates:
            centroid = face_centroid(vertices, face)
            side = (centroid[0] - center[0]) * lateral[0] + (centroid[2] - center[2]) * lateral[1]
            target_faces = left_faces if side >= 0.0 else right_faces
            target_points = left_points if side >= 0.0 else right_points
            target_faces.append(face)
            for vertex in face.vertices:
                point = vertices[vertex]
                projection = (point[0] - center[0]) * lateral[0] + (point[2] - center[2]) * lateral[1]
                target_points.append((projection, point[1]))

        road_left = half_left[-1]
        road_right = half_right[-1]
        road_left_y = road_y_left[-1]
        road_right_y = road_y_right[-1]
        if isinstance(road_left, float):
            left_limit = road_left + max_shoulder
            left_width = min(left_limit, max((p for p, _ in left_points), default=road_left + min_shoulder))
            left_width = max(road_left + min_shoulder, left_width)
            extreme = [y for p, y in left_points if p >= left_width - max(2.0, 0.2 * min_shoulder)]
            outer_left.append(left_width)
            outer_y_left.append(sum(extreme) / len(extreme) if extreme else road_left_y)
        else:
            outer_left.append(None)
            outer_y_left.append(None)
        if isinstance(road_right, float):
            right_limit = road_right + max_shoulder
            right_width = min(right_limit, max((-p for p, _ in right_points), default=road_right + min_shoulder))
            right_width = max(road_right + min_shoulder, right_width)
            extreme = [y for p, y in right_points if -p >= right_width - max(2.0, 0.2 * min_shoulder)]
            outer_right.append(right_width)
            outer_y_right.append(sum(extreme) / len(extreme) if extreme else road_right_y)
        else:
            outer_right.append(None)
            outer_y_right.append(None)

        road_textures.append(dominant_texture(vertices, road_faces, material_to_texture, fallback_road))
        left_textures.append(dominant_texture(vertices, left_faces, material_to_texture, fallback_ground))
        right_textures.append(dominant_texture(vertices, right_faces, material_to_texture, fallback_ground))

    numeric_fields = [
        half_left,
        half_right,
        outer_left,
        outer_right,
        road_y_left,
        road_y_right,
        outer_y_left,
        outer_y_right,
    ]
    filled = [[float(value) for value in fill_periodic_missing(field)] for field in numeric_fields]
    filled[0] = smooth_periodic(filled[0], 2)
    filled[1] = smooth_periodic(filled[1], 2)
    filled[2] = smooth_periodic(filled[2], 2)
    filled[3] = smooth_periodic(filled[3], 2)
    filled[6] = smooth_periodic(filled[6], 1)
    filled[7] = smooth_periodic(filled[7], 1)

    stations = [
        Station(
            center=adjusted_centers[i],
            road_half_left=filled[0][i],
            road_half_right=filled[1][i],
            outer_left=max(filled[2][i], filled[0][i] + min_shoulder),
            outer_right=max(filled[3][i], filled[1][i] + min_shoulder),
            road_y_left=filled[4][i],
            road_y_right=filled[5][i],
            outer_y_left=filled[6][i],
            outer_y_right=filled[7][i],
            road_texture=road_textures[i],
            left_texture=left_textures[i],
            right_texture=right_textures[i],
        )
        for i in range(len(objects))
    ]
    metadata = {
        "sourceTrackObjects": len(objects),
        "sourceObjectsWithoutMainRoadQuad": missing_road,
        "fallbackRoadTexture": fallback_road,
        "fallbackGroundTexture": fallback_ground,
    }
    return stations, metadata


def lerp(a: float, b: float, t: float) -> float:
    return a + (b - a) * t


def interpolate_station(a: Station, b: Station, t: float) -> Station:
    nearest = a if t < 0.5 else b
    return Station(
        center=tuple(lerp(a.center[axis], b.center[axis], t) for axis in range(3)),  # type: ignore[arg-type]
        road_half_left=lerp(a.road_half_left, b.road_half_left, t),
        road_half_right=lerp(a.road_half_right, b.road_half_right, t),
        outer_left=lerp(a.outer_left, b.outer_left, t),
        outer_right=lerp(a.outer_right, b.outer_right, t),
        road_y_left=lerp(a.road_y_left, b.road_y_left, t),
        road_y_right=lerp(a.road_y_right, b.road_y_right, t),
        outer_y_left=lerp(a.outer_y_left, b.outer_y_left, t),
        outer_y_right=lerp(a.outer_y_right, b.outer_y_right, t),
        road_texture=nearest.road_texture,
        left_texture=nearest.left_texture,
        right_texture=nearest.right_texture,
    )


def resample_periodic(stations: list[Station], target_count: int) -> tuple[list[Station], float]:
    lengths = [distance_xz(stations[i].center, stations[(i + 1) % len(stations)].center) for i in range(len(stations))]
    total = sum(lengths)
    if total <= 0.0:
        raise ValueError("Track centerline has zero length")
    cumulative = [0.0]
    for length in lengths:
        cumulative.append(cumulative[-1] + length)
    result: list[Station] = []
    segment = 0
    for sample_index in range(target_count):
        target = total * sample_index / target_count
        while segment + 1 < len(cumulative) and cumulative[segment + 1] < target:
            segment += 1
        local_length = lengths[segment]
        t = (target - cumulative[segment]) / local_length if local_length > 0.0 else 0.0
        result.append(interpolate_station(stations[segment], stations[(segment + 1) % len(stations)], t))
    return result, total


def parse_first_guide_loop(path: Path) -> list[Vec3]:
    vertices: list[Vec3] = []
    active_object = ""
    first_object = ""
    with path.open("r", encoding="utf-8", errors="replace") as handle:
        for raw in handle:
            parts = raw.strip().split()
            if not parts:
                continue
            if parts[0] == "o":
                active_object = " ".join(parts[1:])
                if not first_object:
                    first_object = active_object
                elif active_object != first_object and vertices:
                    break
            elif parts[0] == "v" and active_object == first_object and len(parts) >= 4:
                vertices.append((float(parts[1]), float(parts[2]), float(parts[3])))
    if len(vertices) < 16:
        raise ValueError(f"Guide path has too few points: {path}")
    return vertices


def remap_stations_to_guide(
    source_stations: list[Station], guide_path: Path, max_road_half: float, max_shoulder: float
) -> tuple[list[Station], dict[str, object]]:
    """Align PATH.obj's clean loop, then transfer source widths/materials by proximity."""
    guide = parse_first_guide_loop(guide_path)
    source_centers = [station.center for station in source_stations]
    source_mean = tuple(sum(point[axis] for point in source_centers) / len(source_centers) for axis in range(3))
    candidates: list[tuple[float, bool, int, int, Vec3, list[Vec3], float]] = []
    for swap in (False, True):
        for sign_x in (-1, 1):
            for sign_z in (-1, 1):
                base: list[Vec3] = []
                for px, py, pz in guide:
                    first, second = (py * 0.1, px * 0.1) if swap else (px * 0.1, py * 0.1)
                    base.append((sign_x * first, pz * 0.1, sign_z * second))
                base_mean = tuple(sum(point[axis] for point in base) / len(base) for axis in range(3))
                translation = tuple(source_mean[axis] - base_mean[axis] for axis in range(3))
                transformed = [add(point, translation) for point in base]
                distances = [
                    min(distance_xz(source, point) for point in transformed)
                    for source in source_centers
                ]
                candidates.append((sum(distances) / len(distances), swap, sign_x, sign_z, translation, transformed, max(distances)))
    mean_distance, swap, sign_x, sign_z, translation, transformed, max_distance = min(candidates, key=lambda item: item[0])
    elevation_pairs: list[tuple[float, float]] = []
    for raw_point, planar_point in zip(guide, transformed):
        nearest = min(source_stations, key=lambda station: distance_xz(station.center, planar_point))
        elevation_pairs.append((raw_point[2], nearest.center[1]))
    guide_elevation_mean = sum(pair[0] for pair in elevation_pairs) / len(elevation_pairs)
    source_elevation_mean = sum(pair[1] for pair in elevation_pairs) / len(elevation_pairs)
    elevation_denominator = sum((pair[0] - guide_elevation_mean) ** 2 for pair in elevation_pairs)
    vertical_scale = (
        sum((pair[0] - guide_elevation_mean) * (pair[1] - source_elevation_mean) for pair in elevation_pairs)
        / elevation_denominator
        if elevation_denominator > 1.0e-9
        else -0.1
    )
    vertical_translation = source_elevation_mean - vertical_scale * guide_elevation_mean
    vertical_errors = [vertical_scale * raw + vertical_translation - target for raw, target in elevation_pairs]
    transformed = [
        (point[0], vertical_scale * raw[2] + vertical_translation, point[2])
        for raw, point in zip(guide, transformed)
    ]
    translation = (translation[0], vertical_translation, translation[2])

    remapped: list[Station] = []
    for center in transformed:
        source = min(source_stations, key=lambda station: distance_xz(station.center, center))
        road_left = min(max_road_half, source.road_half_left)
        road_right = min(max_road_half, source.road_half_right)
        left_shoulder = min(max_shoulder, max(8.0, source.outer_left - source.road_half_left))
        right_shoulder = min(max_shoulder, max(8.0, source.outer_right - source.road_half_right))
        remapped.append(
            Station(
                center=center,
                road_half_left=road_left,
                road_half_right=road_right,
                outer_left=road_left + left_shoulder,
                outer_right=road_right + right_shoulder,
                road_y_left=center[1] + (source.road_y_left - source.center[1]),
                road_y_right=center[1] + (source.road_y_right - source.center[1]),
                outer_y_left=center[1] + max(-15.0, min(15.0, source.outer_y_left - source.center[1])),
                outer_y_right=center[1] + max(-15.0, min(15.0, source.outer_y_right - source.center[1])),
                road_texture=source.road_texture,
                left_texture=source.left_texture,
                right_texture=source.right_texture,
            )
        )
    for field in ("road_half_left", "road_half_right", "outer_left", "outer_right"):
        smoothed = smooth_periodic([float(getattr(station, field)) for station in remapped], 2)
        for station, value in zip(remapped, smoothed):
            setattr(station, field, value)
    for station in remapped:
        station.outer_left = max(station.outer_left, station.road_half_left + 8.0)
        station.outer_right = max(station.outer_right, station.road_half_right + 8.0)
    metadata = {
        "guidePath": str(guide_path),
        "guidePoints": len(guide),
        "scale": 0.1,
        "verticalScale": vertical_scale,
        "verticalFitRms": math.sqrt(sum(error * error for error in vertical_errors) / len(vertical_errors)),
        "planarAxisSwap": swap,
        "planarSignX": sign_x,
        "planarSignZ": sign_z,
        "translation": translation,
        "meanNearestSourceDistanceXZ": mean_distance,
        "maxNearestSourceDistanceXZ": max_distance,
        "maxRoadHalfWidth": max_road_half,
    }
    return remapped, metadata


def make_material_names(textures: Iterable[str]) -> dict[str, str]:
    result: dict[str, str] = {}
    used: set[str] = set()
    for texture in sorted(set(textures), key=str.lower):
        stem = re.sub(r"[^A-Za-z0-9_]+", "_", Path(texture).stem).upper().strip("_") or "TEXTURE"
        candidate = f"WORLD_{stem}"
        if candidate in used:
            digest = hashlib.sha1(texture.encode("utf-8")).hexdigest()[:6].upper()
            candidate = f"{candidate}_{digest}"
        used.add(candidate)
        result[texture] = candidate
    return result


def build_ring_vertices(stations: list[Station]) -> list[Vec3]:
    count = len(stations)
    mesh_vertices: list[Vec3] = []
    for i, station in enumerate(stations):
        previous = stations[(i - 1) % count].center
        following = stations[(i + 1) % count].center
        tangent = normalize_xz(following[0] - previous[0], following[2] - previous[2])
        lateral = (-tangent[1], tangent[0])
        cx, _, cz = station.center
        mesh_vertices.extend(
            [
                (cx + lateral[0] * station.outer_left, station.outer_y_left, cz + lateral[1] * station.outer_left),
                (cx + lateral[0] * station.road_half_left, station.road_y_left, cz + lateral[1] * station.road_half_left),
                (cx - lateral[0] * station.road_half_right, station.road_y_right, cz - lateral[1] * station.road_half_right),
                (cx - lateral[0] * station.outer_right, station.outer_y_right, cz - lateral[1] * station.outer_right),
            ]
        )
    return mesh_vertices


def stabilize_outer_bands(stations: list[Station]) -> int:
    """Shrink only locally inverted shoulders around tight inside curves."""
    adjustment_count = 0
    for _ in range(24):
        vertices = build_ring_vertices(stations)
        inverted: list[tuple[int, str]] = []
        for i in range(len(stations)):
            j = (i + 1) % len(stations)
            a, b = i * 4, j * 4
            left_normal, _ = polygon_normal_area(vertices, (a + 0, b + 0, b + 1, a + 1))
            right_normal, _ = polygon_normal_area(vertices, (a + 2, b + 2, b + 3, a + 3))
            if left_normal[1] <= 0.01:
                inverted.append((i, "left"))
            if right_normal[1] <= 0.01:
                inverted.append((i, "right"))
        if not inverted:
            return adjustment_count
        for i, side in inverted:
            for index in (i, (i + 1) % len(stations)):
                station = stations[index]
                if side == "left":
                    station.outer_left = station.road_half_left + (station.outer_left - station.road_half_left) * 0.65
                    station.outer_y_left = station.road_y_left + (station.outer_y_left - station.road_y_left) * 0.65
                else:
                    station.outer_right = station.road_half_right + (station.outer_right - station.road_half_right) * 0.65
                    station.outer_y_right = station.road_y_right + (station.outer_y_right - station.road_y_right) * 0.65
                adjustment_count += 1
    return adjustment_count


def build_mesh(stations: list[Station]) -> tuple[list[Vec3], list[tuple[int, int, int, int, str]], dict[str, str]]:
    count = len(stations)
    mesh_vertices = build_ring_vertices(stations)

    textures = [texture for station in stations for texture in (station.left_texture, station.road_texture, station.right_texture)]
    material_names = make_material_names(textures)
    mesh_faces: list[tuple[int, int, int, int, str]] = []
    for i, station in enumerate(stations):
        j = (i + 1) % count
        a, b = i * 4, j * 4
        mesh_faces.append((a + 0, b + 0, b + 1, a + 1, material_names[station.left_texture]))
        mesh_faces.append((a + 1, b + 1, b + 2, a + 2, material_names[station.road_texture]))
        mesh_faces.append((a + 2, b + 2, b + 3, a + 3, material_names[station.right_texture]))
    # A very tight inside curve can reverse an almost-zero-width shoulder even
    # after local shrinking. Keep its topology and correct only face winding.
    for index, (a, b, c, d, material) in enumerate(mesh_faces):
        normal, _ = polygon_normal_area(mesh_vertices, (a, b, c, d))
        if normal[1] <= 0.0:
            mesh_faces[index] = (d, c, b, a, material)
    return mesh_vertices, mesh_faces, material_names


def write_obj(path: Path, mtl_name: str, vertices: list[Vec3], faces: list[tuple[int, int, int, int, str]]) -> None:
    with path.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write("# Interlagos always-resident world LOD\n")
        handle.write("# Quad-only 200-ring strip: left ground, road, right ground\n")
        handle.write(f"mtllib {mtl_name}\n")
        handle.write("o INTERLAGOS_WORLD_LOD600\n")
        for x, y, z in vertices:
            handle.write(f"v {x:.6f} {y:.6f} {z:.6f}\n")
        for _ in faces:
            handle.write("vt 0.0 0.0\nvt 0.0 1.0\nvt 1.0 1.0\nvt 1.0 0.0\n")
        current_material = ""
        for face_index, (a, b, c, d, material) in enumerate(faces):
            if material != current_material:
                handle.write(f"usemtl {material}\n")
                current_material = material
            uv = face_index * 4 + 1
            handle.write(f"f {a + 1}/{uv} {b + 1}/{uv + 1} {c + 1}/{uv + 2} {d + 1}/{uv + 3}\n")


def write_mtl(path: Path, material_names: dict[str, str]) -> None:
    by_name = sorted(((name, texture) for texture, name in material_names.items()), key=lambda pair: pair[0])
    with path.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write("# Materials consolidated by original texture path\n\n")
        for name, texture in by_name:
            handle.write(f"newmtl {name}\n")
            handle.write("Ns 0.000000\nKa 1.000000 1.000000 1.000000\n")
            handle.write("Kd 1.000000 1.000000 1.000000\nKs 0.000000 0.000000 0.000000\n")
            handle.write("d 1.000000\nillum 1\n")
            handle.write(f"map_Kd {texture}\n\n")


def mesh_metrics(vertices: list[Vec3], faces: list[tuple[int, int, int, int, str]]) -> dict[str, object]:
    edges: Counter[tuple[int, int]] = Counter()
    neighbors: dict[int, set[int]] = defaultdict(set)
    normal_y: list[float] = []
    for a, b, c, d, _ in faces:
        indices = (a, b, c, d)
        normal, _ = polygon_normal_area(vertices, indices)
        normal_y.append(normal[1])
        for p, q in zip(indices, indices[1:] + indices[:1]):
            edge = tuple(sorted((p, q)))
            edges[edge] += 1
            neighbors[p].add(q)
            neighbors[q].add(p)
    degrees = [len(neighbors[i]) for i in range(len(vertices))]
    return {
        "vertices": len(vertices),
        "faces": len(faces),
        "quads": len(faces),
        "edges": len(edges),
        "boundaryEdges": sum(value == 1 for value in edges.values()),
        "manifoldEdges": sum(value == 2 for value in edges.values()),
        "nonManifoldEdges": sum(value > 2 for value in edges.values()),
        "maxVertexEdgeDegree": max(degrees, default=0),
        "verticesOverDegree4": sum(value > 4 for value in degrees),
        "upwardFaces": sum(value > 0.0 for value in normal_y),
        "downwardFaces": sum(value <= 0.0 for value in normal_y),
        "bounds": {
            "min": [min(vertex[axis] for vertex in vertices) for axis in range(3)],
            "max": [max(vertex[axis] for vertex in vertices) for axis in range(3)],
        },
    }


def source_metrics(vertices: list[Vec3], faces: list[Face], material_to_texture: dict[str, str]) -> dict[str, object]:
    size_counts = Counter(len(face.vertices) for face in faces)
    neighbors: dict[int, set[int]] = defaultdict(set)
    for face in faces:
        for a, b in zip(face.vertices, face.vertices[1:] + face.vertices[:1]):
            neighbors[a].add(b)
            neighbors[b].add(a)
    textures = {material_to_texture[face.material] for face in faces if face.material in material_to_texture}
    return {
        "vertices": len(vertices),
        "faces": len(faces),
        "faceSizeCounts": dict(sorted(size_counts.items())),
        "materialsUsed": len({face.material for face in faces}),
        "texturePathsUsed": len(textures),
        "maxVertexEdgeDegree": max((len(value) for value in neighbors.values()), default=0),
        "verticesOverDegree4": sum(len(value) > 4 for value in neighbors.values()),
    }


def write_preview(path: Path, source_stations: list[Station], lod_vertices: list[Vec3]) -> bool:
    width, height, margin = 1100, 1400, 45
    source_points = [(station.center[0], station.center[2]) for station in source_stations]
    all_points = source_points + [(vertex[0], vertex[2]) for vertex in lod_vertices]
    min_x = min(point[0] for point in all_points)
    max_x = max(point[0] for point in all_points)
    min_z = min(point[1] for point in all_points)
    max_z = max(point[1] for point in all_points)
    scale = min((width - 2 * margin) / (max_x - min_x), (height - 2 * margin) / (max_z - min_z))
    x_pad = (width - (max_x - min_x) * scale) * 0.5
    z_pad = (height - (max_z - min_z) * scale) * 0.5

    def project(point: tuple[float, float]) -> tuple[int, int]:
        x = int(round(x_pad + (point[0] - min_x) * scale))
        y = int(round(height - (z_pad + (point[1] - min_z) * scale)))
        return (x, y)

    pixels = bytearray([248, 249, 250] * width * height)

    def put_pixel(x: int, y: int, color: tuple[int, int, int], thickness: int) -> None:
        radius = max(0, thickness // 2)
        for py in range(y - radius, y + radius + 1):
            if py < 0 or py >= height:
                continue
            for px in range(x - radius, x + radius + 1):
                if px < 0 or px >= width:
                    continue
                offset = (py * width + px) * 3
                pixels[offset : offset + 3] = bytes(color)

    def draw_line(a: tuple[int, int], b: tuple[int, int], color: tuple[int, int, int], thickness: int) -> None:
        x0, y0 = a
        x1, y1 = b
        dx, sx = abs(x1 - x0), 1 if x0 < x1 else -1
        dy, sy = -abs(y1 - y0), 1 if y0 < y1 else -1
        error = dx + dy
        while True:
            put_pixel(x0, y0, color, thickness)
            if x0 == x1 and y0 == y1:
                break
            twice = 2 * error
            if twice >= dy:
                error += dy
                x0 += sx
            if twice <= dx:
                error += dx
                y0 += sy

    def draw_loop(points: list[tuple[float, float]], color: tuple[int, int, int], thickness: int) -> None:
        projected = [project(point) for point in points]
        for a, b in zip(projected, projected[1:] + projected[:1]):
            draw_line(a, b, color, thickness)

    draw_loop(source_points, (154, 160, 166), 1)
    ring_count = len(lod_vertices) // 4
    for offset, color, thickness in (
        (0, (82, 132, 56), 2),
        (3, (82, 132, 56), 2),
        (1, (32, 33, 36), 3),
        (2, (32, 33, 36), 3),
    ):
        draw_loop([(lod_vertices[i * 4 + offset][0], lod_vertices[i * 4 + offset][2]) for i in range(ring_count)], color, thickness)

    def png_chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + bytes(pixels[row * width * 3 : (row + 1) * width * 3]) for row in range(height))
    png = b"\x89PNG\r\n\x1a\n"
    png += png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    png += png_chunk(b"IDAT", zlib.compress(raw, 9))
    png += png_chunk(b"IEND", b"")
    path.write_bytes(png)
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source_obj", type=Path)
    parser.add_argument("--source-mtl", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--guide-path", type=Path, help="Optional clean centerline OBJ; defaults to PATH.obj beside source")
    parser.add_argument("--rings", type=int, default=200, help="Ring count; output faces are rings * 3")
    parser.add_argument("--min-shoulder", type=float, default=8.0)
    parser.add_argument("--max-shoulder", type=float, default=24.0)
    parser.add_argument("--max-road-half", type=float, default=24.0)
    parser.add_argument("--no-preview", action="store_true")
    args = parser.parse_args()

    source_obj = args.source_obj.resolve()
    source_mtl = (args.source_mtl or source_obj.with_suffix(".mtl")).resolve()
    output_obj = (args.output or source_obj.with_name("INTERLAGOS_mundo_LOD600.obj")).resolve()
    output_mtl = output_obj.with_suffix(".mtl")
    report_path = output_obj.with_name(output_obj.stem + "_report.json")
    preview_path = output_obj.with_name(output_obj.stem + "_preview.png")
    if args.rings < 16:
        parser.error("--rings must be at least 16")
    if not source_obj.is_file() or not source_mtl.is_file():
        parser.error("source OBJ/MTL not found")
    if output_obj == source_obj or output_mtl == source_mtl:
        parser.error("output must not overwrite the source OBJ/MTL")

    vertices, faces, object_order = parse_obj(source_obj)
    original_material_to_texture = parse_mtl(source_mtl)
    material_to_texture = prefer_existing_8x8_textures(original_material_to_texture, source_obj.parent)
    texture_alias_remaps = sum(
        material_to_texture.get(material) != texture for material, texture in original_material_to_texture.items()
    )
    objects_with_faces = {face.object_name for face in faces}
    track_objects = ordered_track_objects(name for name in object_order if name in objects_with_faces)
    source_stations, station_metadata = build_source_stations(
        vertices,
        faces,
        track_objects,
        material_to_texture,
        args.min_shoulder,
        args.max_shoulder,
    )
    guide_path = args.guide_path.resolve() if args.guide_path else source_obj.with_name("PATH.obj")
    if guide_path.is_file():
        source_stations, guide_metadata = remap_stations_to_guide(
            source_stations, guide_path, args.max_road_half, args.max_shoulder
        )
        station_metadata["centerlineGuide"] = guide_metadata
    else:
        station_metadata["centerlineGuide"] = None
    lod_stations, centerline_length = resample_periodic(source_stations, args.rings)
    shoulder_adjustments = stabilize_outer_bands(lod_stations)
    lod_vertices, lod_faces, material_names = build_mesh(lod_stations)
    output_metrics = mesh_metrics(lod_vertices, lod_faces)

    expected_faces = args.rings * 3
    errors = []
    if output_metrics["faces"] != expected_faces:
        errors.append(f"expected {expected_faces} faces")
    if output_metrics["maxVertexEdgeDegree"] > 4:
        errors.append("vertex edge degree exceeds four")
    if output_metrics["nonManifoldEdges"] != 0:
        errors.append("mesh contains non-manifold edges")
    if output_metrics["downwardFaces"] != 0:
        errors.append("mesh contains downward-facing quads")
    if errors:
        raise RuntimeError("; ".join(errors))

    output_obj.parent.mkdir(parents=True, exist_ok=True)
    write_obj(output_obj, output_mtl.name, lod_vertices, lod_faces)
    write_mtl(output_mtl, material_names)
    preview_written = False if args.no_preview else write_preview(preview_path, source_stations, lod_vertices)
    report = {
        "generator": "tools/build_interlagos_world_lod.py",
        "sourceObj": str(source_obj),
        "sourceMtl": str(source_mtl),
        "sourceSha256": hashlib.sha256(source_obj.read_bytes()).hexdigest(),
        "outputObj": str(output_obj),
        "outputMtl": str(output_mtl),
        "preview": str(preview_path) if preview_written else None,
        "rings": args.rings,
        "bandsPerRing": 3,
        "centerlineLengthXZ": centerline_length,
        "localShoulderShrinkAdjustments": shoulder_adjustments,
        "source": source_metrics(vertices, faces, material_to_texture),
        "output": output_metrics,
        "materials": {
            "count": len(material_names),
            "sourceMaterialTextureAliasesResolvedTo8x8": texture_alias_remaps,
            "textureToMaterial": material_names,
        },
        "stationAnalysis": station_metadata,
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
