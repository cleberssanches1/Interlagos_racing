#!/usr/bin/env python3
"""Generate deterministic per-face/corner lighting for SDR track segments.

The output is intentionally independent from the runtime Gouraud address.  Each
face receives four quantized levels (0..31).  RDR generation embeds the LIT1
payload and the Saturn runtime maps it to its reserved VDP1 Gouraud region.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


SDR_MAGIC = 0x31524453  # SDR1
SDR_VERSION = 1
LIT_MAGIC = 0x3154494C  # LIT1
LIT_VERSION = 1
LIT_HEADER_SIZE = 32
TLIT_MAGIC = 0x31544C54  # TLT1
TLIT_VERSION = 1
TLIT_HEADER_SIZE = 16
FIXED_ONE = 65536.0
FACE_SIZE = 24
VERTEX_SIZE = 12
LIGHT_ENTRY_SIZE = 4
GROUND_FLAG = 1 << 0

SURFACE_NAMES = {
    0: "unknown",
    1: "asphalt",
    2: "escape_area",
    3: "grass",
}


@dataclass(frozen=True)
class Vertex:
    x: int
    y: int
    z: int


@dataclass(frozen=True)
class Face:
    indices: tuple[int, int, int, int]
    normal: tuple[int, int, int]
    kind: int
    surface_type: int
    surface_flags: int


@dataclass
class Segment:
    path: Path
    segment_id: int
    tag: str
    vertices: list[Vertex]
    faces: list[Face]


def read_u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def read_u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def read_i32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<i", data, offset)[0]


def checked_range(size: int, offset: int, count: int, label: str, path: Path) -> None:
    if offset < 0 or count < 0 or offset + count > size:
        raise ValueError(f"{path}: faixa {label} invalida off={offset} bytes={count} size={size}")


def load_sdr(path: Path, tag: str) -> Segment:
    data = path.read_bytes()
    if len(data) < 80:
        raise ValueError(f"SDR pequeno demais: {path}")
    if read_u32(data, 0) != SDR_MAGIC or read_u16(data, 4) != SDR_VERSION:
        raise ValueError(f"SDR invalido: {path}")

    segment_id = read_u16(data, 8)
    vertex_count = read_u32(data, 12)
    face_count = read_u32(data, 16)
    vertices_offset = read_u32(data, 56)
    faces_offset = read_u32(data, 60)
    checked_range(len(data), vertices_offset, vertex_count * VERTEX_SIZE, "vertices", path)
    checked_range(len(data), faces_offset, face_count * FACE_SIZE, "faces", path)

    vertices = [
        Vertex(*struct.unpack_from("<iii", data, vertices_offset + index * VERTEX_SIZE))
        for index in range(vertex_count)
    ]
    faces: list[Face] = []
    for face_index in range(face_count):
        offset = faces_offset + face_index * FACE_SIZE
        indices = struct.unpack_from("<HHHH", data, offset)
        normal = struct.unpack_from("<iii", data, offset + 8)
        kind = data[offset + 20]
        surface_type = data[offset + 21]
        surface_flags = read_u16(data, offset + 22)
        used_indices = indices[:3] if kind == 3 else indices
        if any(index >= vertex_count for index in used_indices):
            raise ValueError(f"{path}: face {face_index} referencia vertice fora do SDR")
        faces.append(Face(indices, normal, kind, surface_type, surface_flags))
    return Segment(path, segment_id, tag, vertices, faces)


def normalized(values: Iterable[float], label: str) -> tuple[float, ...]:
    result = tuple(float(value) for value in values)
    magnitude = math.sqrt(sum(value * value for value in result))
    if magnitude <= 1e-9:
        raise ValueError(f"vetor zero em {label}")
    return tuple(value / magnitude for value in result)


def smoothstep(edge0: float, edge1: float, value: float) -> float:
    if edge1 <= edge0:
        return 1.0 if value >= edge1 else 0.0
    t = max(0.0, min(1.0, (value - edge0) / (edge1 - edge0)))
    return t * t * (3.0 - 2.0 * t)


def zone_multiplier(zone: dict, x: float, z: float) -> float:
    shape = str(zone.get("shape", "circle")).strip().lower()
    multiplier = float(zone.get("multiplier", 1.0))
    feather = max(0.0, float(zone.get("feather", 0.0)))

    if shape == "circle":
        center = zone.get("centerXZ", [0.0, 0.0])
        radius = max(0.0, float(zone.get("radius", 0.0)))
        if len(center) != 2 or radius <= 0.0:
            return 1.0
        distance = math.hypot(x - float(center[0]), z - float(center[1]))
        inner = max(0.0, radius - feather)
        weight = 1.0 - smoothstep(inner, radius, distance)
        return 1.0 + (multiplier - 1.0) * weight

    if shape == "box":
        minimum = zone.get("minXZ", [0.0, 0.0])
        maximum = zone.get("maxXZ", [0.0, 0.0])
        if len(minimum) != 2 or len(maximum) != 2:
            return 1.0
        min_x, min_z = float(minimum[0]), float(minimum[1])
        max_x, max_z = float(maximum[0]), float(maximum[1])
        if x < min_x - feather or x > max_x + feather or z < min_z - feather or z > max_z + feather:
            return 1.0
        if min_x <= x <= max_x and min_z <= z <= max_z:
            weight = 1.0
        else:
            dx = max(min_x - x, 0.0, x - max_x)
            dz = max(min_z - z, 0.0, z - max_z)
            distance = math.hypot(dx, dz)
            weight = 1.0 - smoothstep(0.0, max(feather, 1e-6), distance)
        return 1.0 + (multiplier - 1.0) * weight

    raise ValueError(f"shape de zona desconhecido: {shape}")


class LightingProfile:
    def __init__(self, raw: dict) -> None:
        if int(raw.get("version", 0)) != 1:
            raise ValueError("track_lighting_profile.json deve ter version=1")
        if not bool(raw.get("enabled", True)):
            raise ValueError("perfil de iluminacao esta desabilitado")
        self.sun = normalized(raw.get("sunDirection", [0.35, -0.15, 0.35]), "sunDirection")
        self.ambient = float(raw.get("ambient", 0.48))
        self.diffuse = float(raw.get("diffuse", 0.42))
        self.gamma = max(0.05, float(raw.get("gamma", 1.0)))
        self.minimum = max(0, min(31, int(raw.get("minimumLevel", 5))))
        self.maximum = max(0, min(31, int(raw.get("maximumLevel", 27))))
        if self.maximum < self.minimum:
            raise ValueError("maximumLevel deve ser >= minimumLevel")
        gains = raw.get("surfaceGain", {})
        self.surface_gain = {
            surface_id: float(gains.get(name, gains.get("unknown", 1.0)))
            for surface_id, name in SURFACE_NAMES.items()
        }
        self.bands: list[tuple[str, tuple[float, float], float, float, float]] = []
        for raw_band in raw.get("worldBands", []):
            name = str(raw_band.get("name", "band"))
            direction = normalized(raw_band.get("directionXZ", [1.0, 0.0]), f"worldBands.{name}")
            wavelength = float(raw_band.get("wavelength", 0.0))
            amplitude = float(raw_band.get("amplitude", 0.0))
            phase = math.radians(float(raw_band.get("phaseDegrees", 0.0)))
            if wavelength <= 0.0:
                raise ValueError(f"world band '{name}' possui wavelength invalido")
            if abs(amplitude) > 0.75:
                raise ValueError(f"world band '{name}' possui amplitude excessiva")
            self.bands.append((name, (direction[0], direction[1]), wavelength, amplitude, phase))
        self.zones = list(raw.get("zones", []))

    def level(
        self,
        vertex: Vertex,
        face: Face,
        normal_override: tuple[float, float, float] | None = None,
    ) -> int:
        # Ground faces can share a vertex while retaining slightly different
        # triangulation normals.  Callers supply their common averaged normal
        # in that case so the quantized Gouraud level cannot create a seam.
        if normal_override is None:
            nx, ny, nz = (component / FIXED_ONE for component in face.normal)
        else:
            nx, ny, nz = normal_override
        normal_magnitude = math.sqrt(nx * nx + ny * ny + nz * nz)
        if normal_magnitude <= 1e-9:
            ndotl = 0.0
        else:
            nx, ny, nz = nx / normal_magnitude, ny / normal_magnitude, nz / normal_magnitude
            ndotl = max(0.0, nx * self.sun[0] + ny * self.sun[1] + nz * self.sun[2])

        x, z = vertex.x / FIXED_ONE, vertex.z / FIXED_ONE
        variation = 1.0
        for _name, direction, wavelength, amplitude, phase in self.bands:
            coordinate = x * direction[0] + z * direction[1]
            variation *= 1.0 + amplitude * math.sin((2.0 * math.pi * coordinate / wavelength) + phase)
        for zone in self.zones:
            surfaces = zone.get("surfaceTypes")
            if surfaces:
                accepted = {str(value).strip().lower() for value in surfaces}
                surface_name = SURFACE_NAMES.get(face.surface_type, "unknown")
                if surface_name not in accepted and str(face.surface_type) not in accepted:
                    continue
            variation *= zone_multiplier(zone, x, z)

        surface_gain = self.surface_gain.get(face.surface_type, self.surface_gain[0])
        value = max(0.0, min(1.0, (self.ambient + self.diffuse * ndotl) * surface_gain * variation))
        value = math.pow(value, 1.0 / self.gamma)
        level = round(self.minimum + (self.maximum - self.minimum) * value)
        return max(0, min(31, int(level)))


def lit_name(segment: Segment) -> str:
    return f"S{segment.segment_id:03d}{segment.tag}.LIT"


def write_lit(path: Path, segment: Segment, entries: list[tuple[int, int, int, int]], profile_hash: int) -> None:
    if len(entries) != len(segment.faces):
        raise ValueError(f"{path}: quantidade de luz difere das faces")
    header = struct.pack(
        "<IHHHHIIHHII",
        LIT_MAGIC,
        LIT_VERSION,
        LIT_HEADER_SIZE,
        segment.segment_id,
        1 if segment.tag == "L" else 0,
        len(entries),
        LIT_HEADER_SIZE,
        LIGHT_ENTRY_SIZE,
        32,
        profile_hash,
        0,
    )
    payload = bytes(component for entry in entries for component in entry)
    path.write_bytes(header + payload)


def rgb555(level: int) -> int:
    level = max(0, min(31, level))
    return 0x8000 | (level << 10) | (level << 5) | level


def write_tlit(path: Path, profile_hash: int) -> None:
    header = struct.pack("<IHHHHI", TLIT_MAGIC, TLIT_VERSION, TLIT_HEADER_SIZE, 32, 0, profile_hash)
    ramp = struct.pack("<32H", *(rgb555(level) for level in range(32)))
    path.write_bytes(header + ramp)


def collect_segments(data_dir: Path) -> list[Segment]:
    pattern = re.compile(r"^S(\d{3})(L?)\.SDR$", re.IGNORECASE)
    found: list[tuple[int, str, Path]] = []
    for path in data_dir.iterdir():
        match = pattern.match(path.name)
        if match:
            found.append((int(match.group(1)), match.group(2).upper(), path))
    found.sort(key=lambda item: (item[1], item[0]))
    if not found:
        raise ValueError(f"nenhum S###.SDR/S###L.SDR encontrado em {data_dir}")
    return [load_sdr(path, tag) for _segment_id, tag, path in found]


def face_corner_count(face: Face) -> int:
    return 3 if face.kind == 3 else 4


def ground_vertex_key(vertex: Vertex, surface_type: int) -> tuple[int, int, int, int]:
    """Return the 1/16-unit weld key used by track exports and validation."""
    return (
        round(vertex.x / 4096),
        round(vertex.y / 4096),
        round(vertex.z / 4096),
        surface_type,
    )


def build_ground_vertex_levels(segments: list[Segment], profile: LightingProfile) -> dict[tuple[int, int, int, int], int]:
    """Build one stable level for each welded ground vertex across all LODs.

    A road mesh can be split into several faces (and duplicated in LOD1), each
    with a marginally different normal.  Averaging every incident ground normal
    before quantization preserves the existing sun profile while making the
    value canonical at the seam.
    """
    accumulators: dict[tuple[int, int, int, int], list] = {}
    for segment in segments:
        for face in segment.faces:
            if not (face.surface_flags & GROUND_FLAG):
                continue
            normal = tuple(component / FIXED_ONE for component in face.normal)
            for corner_index in range(face_corner_count(face)):
                vertex = segment.vertices[face.indices[corner_index]]
                key = ground_vertex_key(vertex, face.surface_type)
                accumulator = accumulators.get(key)
                if accumulator is None:
                    # sumX, sumY, sumZ, samples, representative face
                    accumulator = [0.0, 0.0, 0.0, 0, face]
                    accumulators[key] = accumulator
                accumulator[0] += normal[0]
                accumulator[1] += normal[1]
                accumulator[2] += normal[2]
                accumulator[3] += 1

    levels: dict[tuple[int, int, int, int], int] = {}
    for key, accumulator in accumulators.items():
        sample_count = accumulator[3]
        canonical_vertex = Vertex(key[0] * 4096, key[1] * 4096, key[2] * 4096)
        average_normal = (
            accumulator[0] / sample_count,
            accumulator[1] / sample_count,
            accumulator[2] / sample_count,
        )
        levels[key] = profile.level(canonical_vertex, accumulator[4], average_normal)
    return levels


def continuity_metrics(groups: dict[tuple, set[int]]) -> dict[str, int]:
    deltas = [max(levels) - min(levels) for levels in groups.values() if levels]
    return {
        "sharedVertexSamples": len(groups),
        "levelMismatchCount": sum(1 for delta in deltas if delta > 1),
        "maximumLevelDelta": max(deltas, default=0),
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Gera iluminacao Gouraud offline para segmentos SDR")
    parser.add_argument("--data-dir", required=True, type=Path)
    parser.add_argument("--out-dir", required=True, type=Path)
    parser.add_argument("--profile", required=True, type=Path)
    parser.add_argument("--report", required=True, type=Path)
    parser.add_argument("--clean", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if not args.data_dir.is_dir():
        raise ValueError(f"data-dir inexistente: {args.data_dir}")
    if not args.profile.is_file():
        raise ValueError(f"perfil inexistente: {args.profile}")
    args.out_dir.mkdir(parents=True, exist_ok=True)
    args.report.parent.mkdir(parents=True, exist_ok=True)

    profile_bytes = args.profile.read_bytes()
    profile_raw = json.loads(profile_bytes.decode("utf-8-sig"))
    profile = LightingProfile(profile_raw)
    profile_digest = hashlib.sha256(profile_bytes).hexdigest()
    profile_hash = int(profile_digest[:8], 16)

    if args.clean:
        for old_path in args.out_dir.glob("S*.LIT"):
            if re.match(r"^S\d{3}L?\.LIT$", old_path.name, re.IGNORECASE):
                old_path.unlink()

    segments = collect_segments(args.data_dir)
    ground_vertex_levels = build_ground_vertex_levels(segments, profile)
    histogram = [0] * 32
    tag_stats: dict[str, dict[str, int]] = {}
    shared_levels: dict[tuple[int, int, int, int, int], set[int]] = {}
    ground_shared_levels: dict[tuple[int, int, int, int], set[int]] = {}
    ground_levels_by_tag: dict[str, dict[tuple[int, int, int, int], set[int]]] = {
        "high": {},
        "low": {},
    }
    ground_cross_lod_levels: dict[tuple[int, int, int, int], dict[str, set[int]]] = {}
    written: list[dict] = []

    for segment in segments:
        entries: list[tuple[int, int, int, int]] = []
        for face in segment.faces:
            count = face_corner_count(face)
            levels = []
            for index in range(count):
                vertex = segment.vertices[face.indices[index]]
                if face.surface_flags & GROUND_FLAG:
                    levels.append(ground_vertex_levels[ground_vertex_key(vertex, face.surface_type)])
                else:
                    levels.append(profile.level(vertex, face))
            if count == 3:
                levels.append(levels[2])
            entry = (levels[0], levels[1], levels[2], levels[3])
            entries.append(entry)
            for corner_index, level in enumerate(entry):
                histogram[level] += 1
                vertex = segment.vertices[face.indices[corner_index if corner_index < count else count - 1]]
                key = (
                    round(vertex.x / 4096),
                    round(vertex.y / 4096),
                    round(vertex.z / 4096),
                    face.surface_type,
                    1 if face.surface_flags & GROUND_FLAG else 0,
                )
                shared_levels.setdefault(key, set()).add(level)
                if face.surface_flags & GROUND_FLAG:
                    ground_key = ground_vertex_key(vertex, face.surface_type)
                    ground_shared_levels.setdefault(ground_key, set()).add(level)
                    tag_key = "low" if segment.tag == "L" else "high"
                    ground_levels_by_tag[tag_key].setdefault(ground_key, set()).add(level)
                    ground_cross_lod_levels.setdefault(ground_key, {}).setdefault(tag_key, set()).add(level)

        output_path = args.out_dir / lit_name(segment)
        write_lit(output_path, segment, entries, profile_hash)
        tag_key = "low" if segment.tag == "L" else "high"
        stats = tag_stats.setdefault(tag_key, {"segments": 0, "faces": 0, "bytes": 0})
        stats["segments"] += 1
        stats["faces"] += len(entries)
        stats["bytes"] += output_path.stat().st_size
        written.append({
            "segmentId": segment.segment_id,
            "tag": segment.tag,
            "faces": len(entries),
            "file": output_path.name,
        })

    tlit_path = args.out_dir / "TLIT.BIN"
    write_tlit(tlit_path, profile_hash)
    all_continuity = continuity_metrics(shared_levels)
    ground_continuity = continuity_metrics(ground_shared_levels)
    high_ground_continuity = continuity_metrics(ground_levels_by_tag["high"])
    low_ground_continuity = continuity_metrics(ground_levels_by_tag["low"])
    cross_lod_groups = {
        key: levels_by_tag["high"] | levels_by_tag["low"]
        for key, levels_by_tag in ground_cross_lod_levels.items()
        if "high" in levels_by_tag and "low" in levels_by_tag
    }
    cross_lod_continuity = continuity_metrics(cross_lod_groups)
    report = {
        "version": 2,
        "profile": str(args.profile),
        "profileSha256": profile_digest,
        "segments": len(segments),
        "tags": tag_stats,
        "histogram": histogram,
        "minimumGeneratedLevel": next((index for index, count in enumerate(histogram) if count), 0),
        "maximumGeneratedLevel": next((index for index in range(31, -1, -1) if histogram[index]), 0),
        # Keep the original fields for tooling that reads the report directly.
        "sharedVertexSamples": all_continuity["sharedVertexSamples"],
        "sharedVertexLevelMismatchCount": all_continuity["levelMismatchCount"],
        "maximumSharedVertexLevelDelta": all_continuity["maximumLevelDelta"],
        "continuity": {
            "maximumAllowedGroundLevelDelta": 1,
            "all": all_continuity,
            "ground": ground_continuity,
            "highGround": high_ground_continuity,
            "lowGround": low_ground_continuity,
            "crossLodGround": cross_lod_continuity,
        },
        "tlit": tlit_path.name,
        "files": written,
    }
    args.report.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(
        "track lighting ok: "
        f"segments={len(segments)} high={tag_stats.get('high', {}).get('segments', 0)} "
        f"low={tag_stats.get('low', {}).get('segments', 0)} "
        f"levels={report['minimumGeneratedLevel']}..{report['maximumGeneratedLevel']} "
        f"maxGroundSeamDelta={ground_continuity['maximumLevelDelta']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
