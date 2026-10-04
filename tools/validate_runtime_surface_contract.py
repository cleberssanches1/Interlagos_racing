#!/usr/bin/env python3
"""Validate the surface contract shared by manifest, SMAP, SFMAP and RDR."""

from __future__ import annotations

import argparse
import json
import re
import struct
from pathlib import Path


SFM_MAGIC = 0x314D4653
RDR_MAGIC = 0x31524452
SURFACE_IDS = {"asphalt": 1, "escape_area": 2, "grass": 3}


def canonical_stem(value: str) -> str:
    stem = Path(str(value)).stem.lower()
    return re.sub(r"[^a-z0-9]", "", stem)


def read_sfmap(path: Path) -> dict[int, int]:
    data = path.read_bytes()
    if len(data) < 12:
        raise ValueError(f"SFMAP curto: {path}")
    magic, version, _reserved, count = struct.unpack_from("<IHHI", data, 0)
    if magic != SFM_MAGIC or version != 1 or 12 + count * 4 > len(data):
        raise ValueError(f"SFMAP invalido: {path}")
    return {
        family_id: surface_type
        for family_id, surface_type, _mask in (
            struct.unpack_from("<HBB", data, 12 + index * 4)
            for index in range(count)
        )
    }


def read_rdr_faces(path: Path) -> tuple[int, list[tuple[int, int]]]:
    data = path.read_bytes()
    if len(data) < 80:
        raise ValueError(f"RDR curto: {path}")
    magic, version, header_size = struct.unpack_from("<IHH", data, 0)
    segment_id = struct.unpack_from("<H", data, 8)[0]
    face_count = struct.unpack_from("<I", data, 16)[0]
    faces_offset, family_offset = struct.unpack_from("<I4xI", data, 60)
    if magic != RDR_MAGIC or version != 1 or header_size < 80:
        raise ValueError(f"RDR invalido: {path}")
    if faces_offset + face_count * 24 > len(data) or family_offset + face_count * 2 > len(data):
        raise ValueError(f"RDR truncado: {path}")
    faces = []
    for index in range(face_count):
        stored_surface = data[faces_offset + index * 24 + 21]
        family_id = struct.unpack_from("<H", data, family_offset + index * 2)[0]
        faces.append((family_id, stored_surface))
    return segment_id, faces


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--segments-map", type=Path, required=True)
    parser.add_argument("--surface-manifest", type=Path, required=True)
    parser.add_argument("--sfmap", type=Path, required=True)
    parser.add_argument("--rdr-dir", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    segments_map = json.loads(args.segments_map.read_text(encoding="utf-8-sig"))
    manifest = json.loads(args.surface_manifest.read_text(encoding="utf-8-sig"))
    sfmap = read_sfmap(args.sfmap)

    expected_by_stem: dict[str, int] = {}
    for group in manifest.get("surfaceTextures", []):
        name = str(group.get("surfaceType", "")).strip().lower()
        if name not in SURFACE_IDS:
            raise ValueError(f"surfaceType desconhecido no manifesto: {name}")
        surface_type = SURFACE_IDS[name]
        for source_stem in group.get("sourceStems", []):
            stem = canonical_stem(source_stem)
            previous = expected_by_stem.get(stem)
            if previous is not None and previous != surface_type:
                raise ValueError(f"stem ambiguo no manifesto: {source_stem}")
            expected_by_stem[stem] = surface_type

    family_count = 0
    manifest_family_count = 0
    for family in segments_map.get("textureFamilies", []):
        family_id = int(family.get("id", 0))
        if family_id <= 0:
            continue
        family_count += 1
        json_surface = int(family.get("surfaceTypeId", 0))
        binary_surface = sfmap.get(family_id)
        if binary_surface is None:
            raise ValueError(f"familia {family_id} ausente no SFMAP")
        if binary_surface != json_surface:
            raise ValueError(
                f"familia {family_id}: SMAP={json_surface} diverge de SFMAP={binary_surface}"
            )
        stem = canonical_stem(family.get("sourceStem", ""))
        if stem in expected_by_stem:
            manifest_family_count += 1
            expected = expected_by_stem[stem]
            if json_surface != expected:
                raise ValueError(
                    f"familia {family_id} stem={family.get('sourceStem')}: "
                    f"surface={json_surface}, esperado pelo manifesto={expected}"
                )

    segment_ids = sorted(int(row["id"]) for row in segments_map.get("segments", []))
    if not segment_ids:
        raise ValueError("segments_map sem segmentos")

    rdr_face_count = 0
    for tag in ("", "L"):
        for expected_segment in segment_ids:
            path = args.rdr_dir / f"S{expected_segment:03d}{tag}.RDR"
            if not path.is_file():
                raise ValueError(f"RDR ausente: {path}")
            segment_id, faces = read_rdr_faces(path)
            if segment_id != expected_segment:
                raise ValueError(f"RDR {path.name}: segmento interno={segment_id}")
            for face_index, (family_id, stored_surface) in enumerate(faces):
                mapped_surface = sfmap.get(family_id, 0)
                if stored_surface != mapped_surface:
                    raise ValueError(
                        f"{path.name} face={face_index} family={family_id}: "
                        f"RDR={stored_surface} diverge de SFMAP={mapped_surface}"
                    )
            rdr_face_count += len(faces)

    print(
        "runtime surface contract ok: "
        f"families={family_count} manifestFamilies={manifest_family_count} "
        f"segments={len(segment_ids)} rdrFaces={rdr_face_count}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
