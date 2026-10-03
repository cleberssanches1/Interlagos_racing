#!/usr/bin/env python3
"""Validate LIT1 files and their correspondence with SDR1 files."""

from __future__ import annotations

import argparse
import json
import re
import struct
from pathlib import Path


SDR_MAGIC = 0x31524453
LIT_MAGIC = 0x3154494C


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Valida iluminacao baked dos segmentos")
    parser.add_argument("--data-dir", required=True, type=Path)
    parser.add_argument("--lighting-dir", type=Path)
    parser.add_argument("--report", type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    lighting_dir = args.lighting_dir or args.data_dir
    pattern = re.compile(r"^S(\d{3})(L?)\.SDR$", re.IGNORECASE)
    checked = 0
    faces = 0
    errors: list[str] = []
    for sdr_path in sorted(args.data_dir.iterdir(), key=lambda path: path.name.lower()):
        match = pattern.match(sdr_path.name)
        if not match:
            continue
        lit_path = lighting_dir / sdr_path.with_suffix(".LIT").name
        if not lit_path.is_file():
            errors.append(f"LIT ausente: {lit_path.name}")
            continue
        sdr = sdr_path.read_bytes()
        lit = lit_path.read_bytes()
        if len(sdr) < 80 or struct.unpack_from("<I", sdr, 0)[0] != SDR_MAGIC:
            errors.append(f"SDR invalido: {sdr_path.name}")
            continue
        if len(lit) < 32 or struct.unpack_from("<I", lit, 0)[0] != LIT_MAGIC:
            errors.append(f"LIT invalido: {lit_path.name}")
            continue
        segment_id = struct.unpack_from("<H", sdr, 8)[0]
        face_count = struct.unpack_from("<I", sdr, 16)[0]
        lit_segment_id = struct.unpack_from("<H", lit, 8)[0]
        lit_face_count = struct.unpack_from("<I", lit, 12)[0]
        entries_offset = struct.unpack_from("<I", lit, 16)[0]
        entry_size = struct.unpack_from("<H", lit, 20)[0]
        if segment_id != lit_segment_id:
            errors.append(f"segmentId divergente: {sdr_path.name}/{lit_path.name}")
        if face_count != lit_face_count:
            errors.append(f"faceCount divergente: {sdr_path.name}/{lit_path.name}")
        if entry_size != 4 or entries_offset + face_count * entry_size > len(lit):
            errors.append(f"payload LIT invalido: {lit_path.name}")
            continue
        payload = lit[entries_offset : entries_offset + face_count * entry_size]
        if any(level > 31 for level in payload):
            errors.append(f"nivel fora de 0..31: {lit_path.name}")
        checked += 1
        faces += face_count

    if checked == 0:
        errors.append("nenhum par SDR/LIT encontrado")
    if not (lighting_dir / "TLIT.BIN").is_file():
        errors.append("TLIT.BIN ausente")
    if args.report and not args.report.is_file():
        errors.append(f"relatorio ausente: {args.report}")
    if args.report and args.report.is_file():
        report = json.loads(args.report.read_text(encoding="utf-8-sig"))
        if int(report.get("segments", -1)) != checked:
            errors.append(f"relatorio possui segments={report.get('segments')}; pares validados={checked}")
        continuity = report.get("continuity")
        if not isinstance(continuity, dict):
            errors.append("relatorio nao possui metricas de continuidade de iluminacao")
        else:
            allowed_delta = int(continuity.get("maximumAllowedGroundLevelDelta", 1))
            ground = continuity.get("ground")
            if not isinstance(ground, dict):
                errors.append("relatorio nao possui metricas de continuidade do solo")
            else:
                maximum_delta = int(ground.get("maximumLevelDelta", 99))
                mismatches = int(ground.get("levelMismatchCount", -1))
                if maximum_delta > allowed_delta or mismatches != 0:
                    errors.append(
                        "descontinuidade de luz no solo: "
                        f"maxDelta={maximum_delta} mismatchCount={mismatches} permitido={allowed_delta}"
                    )

    if errors:
        for error in errors:
            print(f"ERRO: {error}")
        return 1
    print(f"track lighting validation ok: segments={checked} faces={faces}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
