#!/usr/bin/env python3
"""Build the compact, offline face-to-surface spatial index used by Saturn.

The index never duplicates XYZ vertices from GEO.BIN.  GEO defines geometry and
face order; the matching MAT defines familyId in that exact order; JSON only
maps familyId to surface type.  The index keeps a conservative XZ AABB
quantized to 1/16 world unit.  Only driveable, floor-like faces are stored, so
scenery and wall polygons cost no runtime search time or Cart RAM.
"""
from __future__ import annotations

import argparse
import json
import math
import re
import struct
from dataclasses import dataclass
from pathlib import Path


MAGIC = b"FSM1"
VERSION = 1
HEADER = struct.Struct("<4sHHHHII")
DIRECTORY = struct.Struct("<HHHBBiiII")
RECORD = struct.Struct("<HhhhhBB")
QUANT_SHIFT = 12  # 16.16 raw -> 1/16 world unit
DRIVEABLE_TYPES = frozenset((1, 2, 3))
GEO_MAGIC = 0x314F4547
MAT_MAGIC = 0x3154414D
GEO_FACE_SIZE = 28


def ceil_div(value: int, divisor: int) -> int:
    return -((-value) // divisor)


@dataclass(frozen=True)
class SourceFace:
    index: int
    vertices: tuple[tuple[int, int, int], ...]
    floor_like: bool


@dataclass(frozen=True)
class FaceRecord:
    face_index: int
    min_x: int
    max_x: int
    min_z: int
    max_z: int
    surface_type: int
    flags: int


@dataclass
class SegmentIndex:
    segment_id: int
    face_count: int
    json_face_count: int
    origin_x_raw: int
    origin_z_raw: int
    records: list[FaceRecord]
    source_faces: dict[int, SourceFace]


def read_geo_faces(path: Path, expected_segment_id: int) -> list[SourceFace]:
    data = path.read_bytes()
    if len(data) < 24:
        raise ValueError(f"GEO muito pequeno: {path}")
    magic, version, _, segment_id, payload_bytes = struct.unpack_from("<IHHII", data, 0)
    if magic != GEO_MAGIC or version != 1 or segment_id != expected_segment_id:
        raise ValueError(f"cabecalho GEO invalido: {path}")
    if payload_bytes + 16 > len(data):
        raise ValueError(f"payload GEO truncado: {path}")
    vertex_count, face_count = struct.unpack_from("<II", data, 16)
    vertex_offset = 24
    face_offset = vertex_offset + vertex_count * 12
    if face_offset + face_count * GEO_FACE_SIZE > len(data):
        raise ValueError(f"faces GEO truncadas: {path}")
    vertices = [struct.unpack_from("<iii", data, vertex_offset + index * 12) for index in range(vertex_count)]
    faces: list[SourceFace] = []
    for face_index in range(face_count):
        base = face_offset + face_index * GEO_FACE_SIZE
        indices = struct.unpack_from("<HHHH", data, base)
        if max(indices, default=0) >= vertex_count:
            raise ValueError(f"indice de vertice invalido: {path}, face={face_index}")
        if indices[2] == indices[3]:
            indices = indices[:3]
        face_vertices = tuple(vertices[index] for index in indices)
        ax, ay, az = face_vertices[0]
        bx, by, bz = face_vertices[1]
        cx, cy, cz = face_vertices[2]
        ux, uy, uz = bx - ax, by - ay, bz - az
        vx, vy, vz = cx - ax, cy - ay, cz - az
        nx = uy * vz - uz * vy
        ny = uz * vx - ux * vz
        nz = ux * vy - uy * vx
        length = math.sqrt(nx * nx + ny * ny + nz * nz)
        floor_like = length > 0.0 and abs(ny) / length >= 0.5
        faces.append(SourceFace(face_index, face_vertices, floor_like))
    return faces


def read_mat_family_ids(path: Path, expected_segment_id: int) -> list[int]:
    data = path.read_bytes()
    if len(data) < 20:
        raise ValueError(f"MAT muito pequeno: {path}")
    magic, version, _, segment_id, payload_bytes = struct.unpack_from("<IHHII", data, 0)
    if magic != MAT_MAGIC or version != 1 or segment_id != expected_segment_id:
        raise ValueError(f"cabecalho MAT invalido: {path}")
    if payload_bytes + 16 > len(data):
        raise ValueError(f"payload MAT truncado: {path}")
    face_count = struct.unpack_from("<I", data, 16)[0]
    bindings_offset = 20
    if bindings_offset + face_count * 4 > len(data):
        raise ValueError(f"bindings MAT truncados: {path}")
    return [
        struct.unpack_from("<I", data, bindings_offset + index * 4)[0]
        for index in range(face_count)
    ]


def make_segment_index(
    segment: dict[str, object],
    geometry_dir: Path,
    surface_type_by_family_id: dict[int, int],
) -> SegmentIndex:
    segment_id = int(segment["id"])
    geo_path = geometry_dir / f"S{segment_id:03d}.GEO"
    if not geo_path.exists():
        geo_path = geometry_dir / f"SEG_{segment_id:03d}.GEO"
    mat_path = geometry_dir / f"S{segment_id:03d}M64.MAT"
    if not mat_path.exists():
        mat_path = geometry_dir / f"S{segment_id:03d}M32.MAT"
    faces = read_geo_faces(geo_path, segment_id)
    family_ids = read_mat_family_ids(mat_path, segment_id)
    if len(family_ids) != len(faces):
        raise ValueError(
            f"SEG {segment_id:03d}: faces GEO={len(faces)}, MAT={len(family_ids)}"
        )
    surface_types = [surface_type_by_family_id.get(family_id, 0) for family_id in family_ids]
    json_face_count = (
        int(segment.get("faceCount", len(segment.get("faceSurfaceType", []))))
        if segment.get("presentInJson", True)
        else -1
    )

    selected = [
        face
        for face, surface_type in zip(faces, surface_types)
        if surface_type in DRIVEABLE_TYPES and face.floor_like
    ]
    all_x = [vertex[0] for face in selected for vertex in face.vertices]
    all_z = [vertex[2] for face in selected for vertex in face.vertices]
    # Centering the origin gives a signed int16 twice the useful span of an
    # origin anchored at the minimum.  Some segments include distant grass
    # polygons even though their asphalt is compact.
    origin_x = (min(all_x) + max(all_x)) // 2 if all_x else 0
    origin_z = (min(all_z) + max(all_z)) // 2 if all_z else 0
    quantum = 1 << QUANT_SHIFT
    records: list[FaceRecord] = []

    for face in selected:
        surface_type = surface_types[face.index]
        xs = [vertex[0] for vertex in face.vertices]
        zs = [vertex[2] for vertex in face.vertices]
        min_x = (min(xs) - origin_x) // quantum
        max_x = ceil_div(max(xs) - origin_x, quantum)
        min_z = (min(zs) - origin_z) // quantum
        max_z = ceil_div(max(zs) - origin_z, quantum)
        quantized = (min_x, max_x, min_z, max_z)
        if any(value < -32768 or value > 32767 for value in quantized):
            raise ValueError(
                f"SEG {segment_id:03d} face {face.index}: AABB excede int16: {quantized}"
            )
        flags = 0x01  # driveable
        if surface_type == 1:
            flags |= 0x02  # asphalt-like
        else:
            flags |= 0x04  # offroad-like
        if len(face.vertices) == 3:
            flags |= 0x08
        records.append(
            FaceRecord(face.index, min_x, max_x, min_z, max_z, surface_type, flags)
        )

    return SegmentIndex(
        segment_id=segment_id,
        face_count=len(faces),
        json_face_count=json_face_count,
        origin_x_raw=origin_x,
        origin_z_raw=origin_z,
        records=records,
        source_faces={face.index: face for face in selected},
    )


def build_binary(segments: list[SegmentIndex]) -> bytes:
    directory_offset = HEADER.size
    records_offset = directory_offset + len(segments) * DIRECTORY.size
    total_records = sum(len(segment.records) for segment in segments)
    output = bytearray(
        HEADER.pack(
            MAGIC,
            VERSION,
            HEADER.size,
            len(segments),
            RECORD.size,
            total_records,
            directory_offset,
        )
    )
    cursor = records_offset
    for segment in segments:
        output.extend(
            DIRECTORY.pack(
                segment.segment_id,
                segment.face_count,
                len(segment.records),
                QUANT_SHIFT,
                0x01,
                segment.origin_x_raw,
                segment.origin_z_raw,
                cursor,
                0,
            )
        )
        cursor += len(segment.records) * RECORD.size
    for segment in segments:
        for record in segment.records:
            output.extend(
                RECORD.pack(
                    record.face_index,
                    record.min_x,
                    record.max_x,
                    record.min_z,
                    record.max_z,
                    record.surface_type,
                    record.flags,
                )
            )
    return bytes(output)


def verify_binary(blob: bytes, segments: list[SegmentIndex]) -> None:
    if len(blob) < HEADER.size:
        raise ValueError("FSMAP truncado")
    magic, version, header_size, segment_count, record_size, total_records, directory_offset = HEADER.unpack_from(blob)
    if (magic, version, header_size, record_size) != (MAGIC, VERSION, HEADER.size, RECORD.size):
        raise ValueError("cabecalho FSMAP invalido")
    if segment_count != len(segments) or total_records != sum(len(item.records) for item in segments):
        raise ValueError("contagens FSMAP invalidas")
    previous_id = -1
    for index, expected in enumerate(segments):
        entry_offset = directory_offset + index * DIRECTORY.size
        if entry_offset + DIRECTORY.size > len(blob):
            raise ValueError("diretorio FSMAP truncado")
        values = DIRECTORY.unpack_from(blob, entry_offset)
        segment_id, face_count, record_count, shift, _, origin_x, origin_z, offset, _ = values
        if segment_id <= previous_id or segment_id != expected.segment_id:
            raise ValueError("diretorio FSMAP fora de ordem")
        previous_id = segment_id
        if (face_count, record_count, shift, origin_x, origin_z) != (
            expected.face_count,
            len(expected.records),
            QUANT_SHIFT,
            expected.origin_x_raw,
            expected.origin_z_raw,
        ):
            raise ValueError(f"diretorio divergente no segmento {segment_id}")
        if offset + record_count * RECORD.size > len(blob):
            raise ValueError(f"registros truncados no segmento {segment_id}")
        quantum = 1 << shift
        for record_index in range(record_count):
            record = RECORD.unpack_from(blob, offset + record_index * RECORD.size)
            face_index, min_x, max_x, min_z, max_z, surface_type, flags = record
            source = expected.source_faces.get(face_index)
            if source is None or surface_type not in DRIVEABLE_TYPES or not (flags & 0x01):
                raise ValueError(f"registro invalido: segmento {segment_id}, face {face_index}")
            raw_min_x = origin_x + min_x * quantum
            raw_max_x = origin_x + max_x * quantum
            raw_min_z = origin_z + min_z * quantum
            raw_max_z = origin_z + max_z * quantum
            for x, _, z in source.vertices:
                if not (raw_min_x <= x <= raw_max_x and raw_min_z <= z <= raw_max_z):
                    raise ValueError(f"AABB nao conservadora: segmento {segment_id}, face {face_index}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--geometry-dir", type=Path, required=True)
    parser.add_argument("--segments-map", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()

    root = json.loads(args.segments_map.read_text(encoding="utf-8-sig"))
    rows_by_id = {int(item["id"]): item for item in root.get("segments", [])}
    geo_ids: list[int] = []
    for geo_path in args.geometry_dir.glob("S???.GEO"):
        match = re.fullmatch(r"S(\d{3})\.GEO", geo_path.name, re.IGNORECASE)
        if match:
            geo_ids.append(int(match.group(1)))
    geo_ids = sorted(set(geo_ids))
    if not geo_ids:
        raise ValueError(f"nenhum S###.GEO encontrado em {args.geometry_dir}")
    rows = [
        rows_by_id.get(segment_id, {"id": segment_id, "presentInJson": False})
        for segment_id in geo_ids
    ]
    surface_type_by_family_id = {
        int(family["id"]): int(family.get("surfaceTypeId", 0))
        for family in root.get("textureFamilies", [])
        if int(family.get("id", 0)) > 0
    }
    segments = [
        make_segment_index(row, args.geometry_dir, surface_type_by_family_id)
        for row in rows
    ]
    blob = build_binary(segments)
    verify_binary(blob, segments)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(blob)

    total_faces = sum(segment.face_count for segment in segments)
    total_records = sum(len(segment.records) for segment in segments)
    report = {
        "format": "FSM1",
        "version": VERSION,
        "quantizationWorldUnits": 1.0 / 16.0,
        "segmentCount": len(segments),
        "sourceFaceCount": total_faces,
        "indexedDriveableFloorFaceCount": total_records,
        "excludedFaceCount": total_faces - total_records,
        "binaryBytes": len(blob),
        "bytesPerIndexedFace": RECORD.size,
        "runtimeStorage": "CartRam",
        "geometrySource": "S###.GEO",
        "faceFamilySource": "S###M64.MAT (fallback S###M32.MAT)",
        "surfaceTypeSource": "segments_map.textureFamilies[].surfaceTypeId",
        "jsonFaceCountMismatchSegments": [
            segment.segment_id
            for segment in segments
            if segment.json_face_count != segment.face_count
        ],
        "missingJsonSegments": [
            segment.segment_id
            for segment in segments
            if segment.json_face_count < 0
        ],
        "segments": [
            {
                "id": segment.segment_id,
                "sourceFaces": segment.face_count,
                "jsonFaces": segment.json_face_count,
                "indexedFaces": len(segment.records),
            }
            for segment in segments
        ],
    }
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(
        f"FSMAP: segments={len(segments)} source_faces={total_faces} "
        f"indexed={total_records} bytes={len(blob)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
