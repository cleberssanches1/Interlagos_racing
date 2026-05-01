import argparse
import json
import re
import struct
from collections import defaultdict
from pathlib import Path


RDR1_MAGIC = 0x31524452  # "RDR1"
RDR1_VERSION = 1
DVM1_MAGIC = 0x31564D44  # "DVM1"
DVM1_VERSION = 1

RDR1_HEADER = struct.Struct("<IHHHHIIiiiiiiiiiIIIIII")
RDR1_VERTEX = struct.Struct("<iii")
RDR1_FACE = struct.Struct("<HHHHiiiBBH")

DVM1_HEADER = struct.Struct("<IHHHH" "IIIIIII" "iiii" "II")
DVM1_SEGMENT_ENTRY = struct.Struct("<IHH")
DVM1_TRIANGLE_ENTRY = struct.Struct("<HHHHiiiiiiiqqqiiiii")

DEFAULT_DRIVABLE_STEMS = [
    "F05564",
    "F04764",
    "F01064",
    "F01864",
    "F02564",
    "F04364",
    "F04664",
    "F05464",
    "F00164",
    "F00264",
    "F00364",
    "F00464",
    "F00564",
    "F06164",
    "F06264",
    "F06364",
]


def align4(value: int) -> int:
    return (value + 3) & ~3


def normalize_stem(value: str) -> str:
    if not value:
        return ""
    return "".join(ch for ch in value.lower() if ch.isalnum())


def stem_from_texture_token(value: str) -> str:
    if not value:
        return ""
    token = Path(value.split(";", 1)[0]).name
    stem = Path(token).stem
    norm = normalize_stem(stem)
    if not norm:
        return ""
    match = re.search(r"(f\d{3})(8|16|32|64)$", norm)
    if match:
        return f"{match.group(1)}{match.group(2)}"
    return norm


def int32_clamp(value: int) -> int:
    if value > 0x7FFFFFFF:
        return 0x7FFFFFFF
    if value < -0x80000000:
        return -0x80000000
    return int(value)


def load_json(path: Path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def resolve_family_stems(segments_map: dict) -> tuple[dict[int, set[str]], dict[str, set[int]]]:
    by_family: dict[int, set[str]] = {}
    by_stem: dict[str, set[int]] = defaultdict(set)

    for family in segments_map.get("textureFamilies", []):
        if not isinstance(family, dict):
            continue
        family_id = int(family.get("id", 0) or 0)
        if family_id <= 0:
            continue

        stems: set[str] = set()

        for key in ("sourceStem", "name", "sourceFamilyName"):
            value = family.get(key)
            if isinstance(value, str):
                norm = normalize_stem(value)
                if norm:
                    stems.add(norm)
                token_stem = stem_from_texture_token(value)
                if token_stem:
                    stems.add(token_stem)

        aliases = family.get("aliases")
        if isinstance(aliases, list):
            for alias in aliases:
                if not isinstance(alias, str):
                    continue
                norm = normalize_stem(alias)
                if norm:
                    stems.add(norm)
                token_stem = stem_from_texture_token(alias)
                if token_stem:
                    stems.add(token_stem)

        for key in ("variants", "imageFiles"):
            value = family.get(key)
            if not isinstance(value, dict):
                continue
            for token in value.values():
                if not isinstance(token, str):
                    continue
                token_stem = stem_from_texture_token(token)
                if token_stem:
                    stems.add(token_stem)

        stems = {s for s in stems if s}
        if not stems:
            continue

        by_family[family_id] = stems
        for stem in stems:
            by_stem[stem].add(family_id)

    return by_family, by_stem


def load_drivable_config(config_path: Path | None) -> dict:
    config = {
        "version": 1,
        "strict": True,
        "drivable_surface_stems": list(DEFAULT_DRIVABLE_STEMS),
        "drivable_family_ids": [],
    }
    if config_path is None:
        return config
    data = load_json(config_path)
    if not isinstance(data, dict):
        raise ValueError(f"Config invalido: {config_path}")
    config.update(data)
    return config


def resolve_drivable_family_ids(config: dict, by_stem: dict[str, set[int]]) -> tuple[list[int], list[str], list[str]]:
    requested_stems: list[str] = []
    unresolved: list[str] = []
    resolved: set[int] = set()

    for raw_stem in config.get("drivable_surface_stems", []):
        if not isinstance(raw_stem, str):
            continue
        stem = normalize_stem(raw_stem)
        if not stem:
            continue
        requested_stems.append(stem)
        ids = by_stem.get(stem, set())
        if not ids:
            unresolved.append(raw_stem)
            continue
        resolved.update(ids)

    for raw_id in config.get("drivable_family_ids", []):
        try:
            family_id = int(raw_id)
        except (TypeError, ValueError):
            continue
        if family_id > 0:
            resolved.add(family_id)

    return sorted(resolved), requested_stems, unresolved


def parse_rdr1(path: Path) -> dict:
    blob = path.read_bytes()
    if len(blob) < RDR1_HEADER.size:
        raise ValueError(f"RDR pequeno demais: {path}")

    header = RDR1_HEADER.unpack_from(blob, 0)
    (
        magic,
        version,
        header_size,
        segment_id,
        _flags,
        vertex_count,
        face_count,
        _center_x,
        _center_y,
        _center_z,
        _min_x,
        _min_y,
        _min_z,
        _max_x,
        _max_y,
        _max_z,
        vertices_offset,
        faces_offset,
        _attrs_offset,
        family_ids_offset,
        _reserved0,
        _reserved1,
    ) = header

    if magic != RDR1_MAGIC:
        raise ValueError(f"Magic RDR invalido em {path}: 0x{magic:08X}")
    if version != RDR1_VERSION:
        raise ValueError(f"Versao RDR invalida em {path}: {version}")
    if header_size < RDR1_HEADER.size:
        raise ValueError(f"Header RDR invalido em {path}: {header_size}")

    vertices_bytes = vertex_count * RDR1_VERTEX.size
    faces_bytes = face_count * RDR1_FACE.size
    family_bytes = face_count * 2

    if vertices_offset + vertices_bytes > len(blob):
        raise ValueError(f"Tabela de vertices invalida em {path}")
    if faces_offset + faces_bytes > len(blob):
        raise ValueError(f"Tabela de faces invalida em {path}")
    if family_ids_offset + family_bytes > len(blob):
        raise ValueError(f"Tabela de familyId invalida em {path}")

    vertices: list[tuple[int, int, int]] = []
    for index in range(vertex_count):
        off = vertices_offset + index * RDR1_VERTEX.size
        vertices.append(RDR1_VERTEX.unpack_from(blob, off))

    faces: list[tuple[int, int, int, int, int, int, int]] = []
    for index in range(face_count):
        off = faces_offset + index * RDR1_FACE.size
        v0, v1, v2, v3, nx, ny, nz, _kind, _ra, _rb = RDR1_FACE.unpack_from(blob, off)
        faces.append((v0, v1, v2, v3, nx, ny, nz))

    family_ids: list[int] = []
    for index in range(face_count):
        off = family_ids_offset + index * 2
        (family_id,) = struct.unpack_from("<H", blob, off)
        family_ids.append(family_id)

    return {
        "path": str(path),
        "segment_id": int(segment_id),
        "vertex_count": int(vertex_count),
        "face_count": int(face_count),
        "vertices": vertices,
        "faces": faces,
        "family_ids": family_ids,
    }


def build_triangle_entry(
    segment_id: int,
    face_index: int,
    family_id: int,
    a: tuple[int, int, int],
    b: tuple[int, int, int],
    c: tuple[int, int, int],
) -> tuple[int, ...] | None:
    ax, ay, az = a
    bx, by, bz = b
    cx, cy, cz = c

    ux = bx - ax
    uy = by - ay
    uz = bz - az

    vx = cx - ax
    vy = cy - ay
    vz = cz - az

    nx = (uy * vz) - (uz * vy)
    ny = (uz * vx) - (ux * vz)
    nz = (ux * vy) - (uy * vx)
    if ny == 0:
        return None

    min_x = min(ax, bx, cx)
    max_x = max(ax, bx, cx)
    min_z = min(az, bz, cz)
    max_z = max(az, bz, cz)

    max_comp = max(abs(nx), abs(ny), abs(nz), 1)
    normal_x = int32_clamp((nx << 16) // max_comp)
    normal_y = int32_clamp((ny << 16) // max_comp)
    normal_z = int32_clamp((nz << 16) // max_comp)

    return (
        int(segment_id),
        int(face_index),
        int(family_id),
        0,
        int32_clamp(min_x),
        int32_clamp(max_x),
        int32_clamp(min_z),
        int32_clamp(max_z),
        int32_clamp(ax),
        int32_clamp(ay),
        int32_clamp(az),
        int(nx),
        int(ny),
        int(nz),
        normal_x,
        normal_y,
        normal_z,
        0,
        0,
    )


def build_drive_map(
    rdr_entries: list[dict],
    drivable_family_ids: set[int],
    min_normal_y_raw: int,
) -> tuple[list[tuple[int, ...]], dict[int, list[tuple[int, ...]]]]:
    triangles_by_segment: dict[int, list[tuple[int, ...]]] = defaultdict(list)
    all_triangles: list[tuple[int, ...]] = []

    for entry in rdr_entries:
        segment_id = entry["segment_id"]
        vertices = entry["vertices"]
        faces = entry["faces"]
        family_ids = entry["family_ids"]

        for face_index, face in enumerate(faces):
            family_id = int(family_ids[face_index])
            if family_id not in drivable_family_ids:
                continue

            v0, v1, v2, v3, _nx, face_ny, _nz = face
            if abs(face_ny) < min_normal_y_raw:
                continue

            if v0 >= len(vertices) or v1 >= len(vertices) or v2 >= len(vertices) or v3 >= len(vertices):
                continue

            a = vertices[v0]
            b = vertices[v1]
            c = vertices[v2]
            d = vertices[v3]

            tri0 = build_triangle_entry(segment_id, face_index, family_id, a, b, c)
            if tri0 is not None:
                triangles_by_segment[segment_id].append(tri0)
                all_triangles.append(tri0)

            tri1 = build_triangle_entry(segment_id, face_index, family_id, a, c, d)
            if tri1 is not None:
                triangles_by_segment[segment_id].append(tri1)
                all_triangles.append(tri1)

    return all_triangles, triangles_by_segment


def write_drive_map_bin(
    out_path: Path,
    drivable_family_ids: list[int],
    all_triangles: list[tuple[int, ...]],
    triangles_by_segment: dict[int, list[tuple[int, ...]]],
    max_segment_id: int,
) -> dict:
    segment_count_with_triangles = sum(1 for tris in triangles_by_segment.values() if tris)
    family_ids = sorted(set(drivable_family_ids))

    segment_table_offset = DVM1_HEADER.size
    segment_table_bytes = max_segment_id * DVM1_SEGMENT_ENTRY.size
    family_ids_offset = align4(segment_table_offset + segment_table_bytes)
    family_ids_bytes = len(family_ids) * 2
    triangles_offset = align4(family_ids_offset + family_ids_bytes)
    triangles_bytes = len(all_triangles) * DVM1_TRIANGLE_ENTRY.size
    total_size = triangles_offset + triangles_bytes

    world_min_x = 0
    world_max_x = 0
    world_min_z = 0
    world_max_z = 0
    if all_triangles:
        world_min_x = min(t[4] for t in all_triangles)
        world_max_x = max(t[5] for t in all_triangles)
        world_min_z = min(t[6] for t in all_triangles)
        world_max_z = max(t[7] for t in all_triangles)

    payload = bytearray(total_size)
    DVM1_HEADER.pack_into(
        payload,
        0,
        DVM1_MAGIC,
        DVM1_VERSION,
        DVM1_HEADER.size,
        segment_count_with_triangles,
        max_segment_id,
        len(family_ids),
        len(all_triangles),
        segment_table_offset,
        family_ids_offset,
        triangles_offset,
        DVM1_SEGMENT_ENTRY.size,
        DVM1_TRIANGLE_ENTRY.size,
        world_min_x,
        world_max_x,
        world_min_z,
        world_max_z,
        0,
        0,
    )

    segment_first: list[int] = [0] * (max_segment_id + 1)
    segment_count: list[int] = [0] * (max_segment_id + 1)

    running_index = 0
    ordered_triangles: list[tuple[int, ...]] = []
    for segment_id in range(1, max_segment_id + 1):
        tris = triangles_by_segment.get(segment_id, [])
        if tris:
            segment_first[segment_id] = running_index
            segment_count[segment_id] = len(tris)
            ordered_triangles.extend(tris)
            running_index += len(tris)

    if len(ordered_triangles) != len(all_triangles):
        raise ValueError("Inconsistencia interna: contagem de triangulos divergiu")

    for segment_id in range(1, max_segment_id + 1):
        off = segment_table_offset + (segment_id - 1) * DVM1_SEGMENT_ENTRY.size
        DVM1_SEGMENT_ENTRY.pack_into(
            payload,
            off,
            segment_first[segment_id],
            segment_count[segment_id],
            0,
        )

    off = family_ids_offset
    for family_id in family_ids:
        struct.pack_into("<H", payload, off, family_id)
        off += 2

    off = triangles_offset
    for tri in ordered_triangles:
        DVM1_TRIANGLE_ENTRY.pack_into(payload, off, *tri)
        off += DVM1_TRIANGLE_ENTRY.size

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(payload)

    return {
        "segment_count_with_triangles": segment_count_with_triangles,
        "triangle_count": len(ordered_triangles),
        "world_min_x_raw": world_min_x,
        "world_max_x_raw": world_max_x,
        "world_min_z_raw": world_min_z,
        "world_max_z_raw": world_max_z,
        "binary_size": len(payload),
        "triangle_entry_size": DVM1_TRIANGLE_ENTRY.size,
        "segment_entry_size": DVM1_SEGMENT_ENTRY.size,
        "max_segment_id": max_segment_id,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Gera mapa rapido de superficie dirigivel (DRVMAP.BIN).")
    parser.add_argument("--segments-map", required=True, help="Caminho do segments_map.json")
    parser.add_argument("--rdr-dir", required=True, help="Diretorio contendo S???.RDR")
    parser.add_argument("--out-bin", required=True, help="Saida binaria DRVMAP.BIN")
    parser.add_argument("--out-debug-json", default="", help="Saida JSON de diagnostico (opcional)")
    parser.add_argument("--config", default="", help="Config JSON de familias dirigiveis")
    parser.add_argument("--min-normal-y-raw", type=int, default=(1 << 10), help="Filtro minimo de |normalY| em raw")
    args = parser.parse_args()

    segments_map_path = Path(args.segments_map)
    rdr_dir = Path(args.rdr_dir)
    out_bin_path = Path(args.out_bin)
    out_debug_path = Path(args.out_debug_json) if args.out_debug_json else None
    config_path = Path(args.config) if args.config else None

    if not segments_map_path.exists():
        raise FileNotFoundError(f"segments_map nao encontrado: {segments_map_path}")
    if not rdr_dir.exists():
        raise FileNotFoundError(f"Diretorio RDR nao encontrado: {rdr_dir}")
    if config_path and not config_path.exists():
        raise FileNotFoundError(f"Config nao encontrada: {config_path}")

    segments_map = load_json(segments_map_path)
    by_family_stems, by_stem = resolve_family_stems(segments_map)
    config = load_drivable_config(config_path)
    strict = bool(config.get("strict", True))

    drivable_family_ids, requested_stems, unresolved_stems = resolve_drivable_family_ids(config, by_stem)
    if unresolved_stems and strict:
        raise ValueError(
            "Familias nao resolvidas na configuracao de pista: " + ", ".join(unresolved_stems)
        )
    if not drivable_family_ids:
        raise ValueError("Nenhuma familia dirigivel resolvida para montar o DRVMAP")

    rdr_paths = sorted(rdr_dir.glob("S???.RDR"))
    if not rdr_paths:
        raise ValueError(f"Nenhum S???.RDR encontrado em {rdr_dir}")

    rdr_entries = [parse_rdr1(path) for path in rdr_paths]
    max_segment_id = max(entry["segment_id"] for entry in rdr_entries)

    all_triangles, triangles_by_segment = build_drive_map(
        rdr_entries=rdr_entries,
        drivable_family_ids=set(drivable_family_ids),
        min_normal_y_raw=int(args.min_normal_y_raw),
    )

    summary = write_drive_map_bin(
        out_path=out_bin_path,
        drivable_family_ids=drivable_family_ids,
        all_triangles=all_triangles,
        triangles_by_segment=triangles_by_segment,
        max_segment_id=max_segment_id,
    )

    if out_debug_path:
        per_segment = []
        for segment_id in range(1, max_segment_id + 1):
            count = len(triangles_by_segment.get(segment_id, []))
            if count <= 0:
                continue
            per_segment.append({"segmentId": segment_id, "triangleCount": count})

        debug_payload = {
            "version": 1,
            "segments_map": str(segments_map_path),
            "rdr_dir": str(rdr_dir),
            "out_bin": str(out_bin_path),
            "out_bin_size_bytes": summary["binary_size"],
            "drivable_surface_stems_requested": requested_stems,
            "unresolved_stems": unresolved_stems,
            "drivable_family_ids": drivable_family_ids,
            "family_stems_resolved": {
                str(fid): sorted(by_family_stems.get(fid, [])) for fid in drivable_family_ids
            },
            "min_normal_y_raw": int(args.min_normal_y_raw),
            "segment_count_scanned": len(rdr_entries),
            "segment_count_with_triangles": summary["segment_count_with_triangles"],
            "max_segment_id": summary["max_segment_id"],
            "triangle_count": summary["triangle_count"],
            "world_bounds_raw": {
                "minX": summary["world_min_x_raw"],
                "maxX": summary["world_max_x_raw"],
                "minZ": summary["world_min_z_raw"],
                "maxZ": summary["world_max_z_raw"],
            },
            "entries": {
                "segment_entry_size": summary["segment_entry_size"],
                "triangle_entry_size": summary["triangle_entry_size"],
            },
            "segments": per_segment,
        }
        out_debug_path.parent.mkdir(parents=True, exist_ok=True)
        out_debug_path.write_text(json.dumps(debug_payload, indent=2), encoding="utf-8")

    print(
        "DRVMAP ok: "
        f"bin={out_bin_path} "
        f"segs={summary['segment_count_with_triangles']} "
        f"tris={summary['triangle_count']} "
        f"families={len(drivable_family_ids)} "
        f"bytes={summary['binary_size']}"
    )
    if unresolved_stems:
        print("DRVMAP aviso: stems nao resolvidos (strict=false): " + ", ".join(unresolved_stems))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
