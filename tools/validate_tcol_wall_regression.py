#!/usr/bin/env python3
"""Check the Interlagos wall bake against known missing-barrier regressions.

Example: python tools/validate_tcol_wall_regression.py --tcol cd/data/TCOL.BIN \
    --report cd/data/track_collision_report.json --segments-map PACKAGE/segments_map.json
"""
from __future__ import annotations

import argparse
import json
import math
import struct
from pathlib import Path

from build_track_collision import DIRECTORY, MAGIC, WALL, WallRecord, wall_distance_to_point_xz


def walls_for_segment(data: bytes, segment_id: int) -> list[WallRecord]:
    if data[:4] != MAGIC:
        raise ValueError("TCOL.BIN tem magic invalido")
    segment_count = struct.unpack_from("<H", data, 20)[0]
    if not 1 <= segment_id <= segment_count:
        raise ValueError(f"SEG {segment_id} fora de TCOL.BIN ({segment_count})")
    directory_offset = struct.unpack_from("<I", data, 30)[0]
    entry = DIRECTORY.unpack_from(data, directory_offset + (segment_id - 1) * DIRECTORY.size)
    if entry[0] != segment_id:
        raise ValueError(f"diretorio TCOL desalinhado em SEG {segment_id}")
    wall_count, wall_offset = entry[3], entry[12]
    return [WallRecord(*WALL.unpack_from(data, wall_offset + i * WALL.size))
            for i in range(wall_count)]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tcol", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--segments-map", type=Path, required=True)
    args = parser.parse_args()

    data = args.tcol.read_bytes()
    report = json.loads(args.report.read_text(encoding="utf-8"))
    catalog = json.loads(args.segments_map.read_text(encoding="utf-8-sig"))
    family = {str(item.get("sourceStem", "")).lower(): int(item["id"])
              for item in catalog["textureFamilies"]}
    f03664 = family["f03664"]

    assert report["binaryBytes"] == len(data), "relatorio e TCOL divergentes"
    assert report["wallFaceCount"] == sum(
        len(walls_for_segment(data, sid)) for sid in range(1, report["segmentCount"] + 1)
    ), "contagem de paredes divergente"
    assert report["protectedLongitudinalWalls"] > 0, "nenhuma barreira longitudinal protegida"
    assert report["joinAudit"]["seamCorridorHits"] == 0, "tampao em juncao da pista"
    assert report["joinAudit"]["joinGroundMisses"] == 0, "falha de solo em juncao"

    # The visible pit-side F03664 barrier is continuous across these three
    # segments. It was absent from TCOL despite existing in LOD0 and LOD1.
    targets = ((2, 5358.0, 1447.0), (3, 5361.0, 1183.0), (4, 5364.0, 919.0))
    for sid, x, z in targets:
        nearest = min(
            (wall_distance_to_point_xz(wall, x * 65536, z * 65536) / 65536
             for wall in walls_for_segment(data, sid) if wall.family_id == f03664),
            default=math.inf,
        )
        assert nearest < 5.0, f"SEG {sid}: F03664 ausente ({nearest:.1f}u)"
        print(f"SEG {sid:03d} F03664: distancia {nearest:.2f}u")

    print(f"OK: {report['wallFaceCount']} paredes, "
          f"{report['protectedLongitudinalWalls']} longitudinais protegidas, "
          "sem tampoes de juncao")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
