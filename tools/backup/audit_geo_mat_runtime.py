from __future__ import annotations

import argparse
import json
import struct
from dataclasses import dataclass
from html import escape
from pathlib import Path
from typing import Dict, List, Optional


GEO_MAGIC = 0x314F4547  # GEO1
MAT_MAGIC = 0x3154414D  # MAT1
FACE_KIND_TRI = 3
FACE_KIND_QUAD = 4


@dataclass
class GeoInfo:
    path: Path
    segment_id: int
    vertex_count: int
    face_count: int
    tri_count: int
    quad_count: int
    other_face_kinds: int


@dataclass
class MatInfo:
    path: Path
    segment_id: int
    lod: str
    face_count: int
    zero_bindings: int
    nonzero_bindings: int
    unique_materials: int


@dataclass
class SegmentAudit:
    segment_id: int
    geo: Optional[GeoInfo]
    mats: List[MatInfo]
    map_face_count: Optional[int]
    face_count_match_map: Optional[bool]
    has_any_zero_binding: bool
    all_mats_match_geo: bool


def read_u16(data: bytes, off: int) -> int:
    return struct.unpack_from("<H", data, off)[0]


def read_u32(data: bytes, off: int) -> int:
    return struct.unpack_from("<I", data, off)[0]


def parse_geo(path: Path) -> GeoInfo:
    data = path.read_bytes()
    if len(data) < 24:
        raise ValueError(f"GEO truncado: {path}")
    magic = read_u32(data, 0)
    version = read_u16(data, 4)
    segment_id = read_u32(data, 8)
    payload_bytes = read_u32(data, 12)
    if magic != GEO_MAGIC or version != 1:
        raise ValueError(f"GEO invalido: {path}")
    if payload_bytes + 16 > len(data):
        raise ValueError(f"GEO payload inconsistente: {path}")

    vertex_count = read_u32(data, 16)
    face_count = read_u32(data, 20)
    face_off = 24 + vertex_count * 12
    face_size = 28
    if face_off + face_count * face_size > len(data):
        raise ValueError(f"GEO faces truncadas: {path}")

    tri_count = 0
    quad_count = 0
    other_face_kinds = 0
    for i in range(face_count):
        off = face_off + i * face_size
        kind = data[off + 24]
        if kind == FACE_KIND_TRI:
            tri_count += 1
        elif kind == FACE_KIND_QUAD:
            quad_count += 1
        else:
            other_face_kinds += 1

    return GeoInfo(
        path=path,
        segment_id=segment_id,
        vertex_count=vertex_count,
        face_count=face_count,
        tri_count=tri_count,
        quad_count=quad_count,
        other_face_kinds=other_face_kinds,
    )


def parse_mat(path: Path) -> MatInfo:
    data = path.read_bytes()
    if len(data) < 20:
        raise ValueError(f"MAT truncado: {path}")
    magic = read_u32(data, 0)
    version = read_u16(data, 4)
    segment_id = read_u32(data, 8)
    payload_bytes = read_u32(data, 12)
    if magic != MAT_MAGIC or version != 1:
        raise ValueError(f"MAT invalido: {path}")
    if payload_bytes + 16 > len(data):
        raise ValueError(f"MAT payload inconsistente: {path}")

    face_count = read_u32(data, 16)
    bind_off = 20
    if bind_off + face_count * 4 > len(data):
        raise ValueError(f"MAT bindings truncados: {path}")

    bindings: List[int] = []
    for i in range(face_count):
        bindings.append(read_u32(data, bind_off + i * 4))

    zero_bindings = sum(1 for x in bindings if x == 0)
    nonzero = [x for x in bindings if x != 0]
    unique_materials = len(set(nonzero))

    lod = "?"
    stem = path.stem.upper()
    if "M" in stem:
        lod = stem.split("M", 1)[1]

    return MatInfo(
        path=path,
        segment_id=segment_id,
        lod=lod,
        face_count=face_count,
        zero_bindings=zero_bindings,
        nonzero_bindings=len(nonzero),
        unique_materials=unique_materials,
    )


def read_segments_map_face_counts(path: Optional[Path]) -> Dict[int, int]:
    if path is None or not path.exists():
        return {}
    root = json.loads(path.read_text(encoding="utf-8-sig"))
    out: Dict[int, int] = {}
    for seg in root.get("segments", []):
        sid = int(seg.get("id", -1))
        if sid < 0:
            continue
        if "faces" in seg and seg["faces"]:
            out[sid] = len(seg["faces"])
        else:
            out[sid] = len(seg.get("faceTextureFamily", []))
    return out


def render_html(out_path: Path, package_dir: Path, rows: List[SegmentAudit]) -> None:
    html: List[str] = [
        "<!DOCTYPE html>",
        '<html lang="pt-BR">',
        "<head>",
        '  <meta charset="UTF-8" />',
        "  <title>Auditoria GEO/MAT Runtime</title>",
        "  <style>",
        "body { font-family: Segoe UI, Arial, sans-serif; margin: 24px; line-height: 1.45; color: #111; }",
        "table { border-collapse: collapse; width: 100%; margin: 16px 0; }",
        "th, td { border: 1px solid #cfd6dd; padding: 8px; text-align: left; vertical-align: top; }",
        "th { background: #eef3f8; }",
        ".warn { background: #fff4db; }",
        ".fail { background: #ffe3e3; }",
        "  </style>",
        "</head>",
        "<body>",
        "<h1>Auditoria GEO/MAT Runtime</h1>",
        f"<p><strong>Pacote:</strong> {escape(str(package_dir))}</p>",
        "<table>",
        "<tr><th>Seg</th><th>GEO faces</th><th>GEO tris</th><th>GEO quads</th><th>Map faces</th><th>MATs</th><th>Zero bindings</th><th>Match GEO/MAT</th><th>Match Map</th></tr>",
    ]
    for row in rows:
        geo_faces = row.geo.face_count if row.geo else 0
        tri_count = row.geo.tri_count if row.geo else 0
        quad_count = row.geo.quad_count if row.geo else 0
        mat_count = len(row.mats)
        zero_total = sum(m.zero_bindings for m in row.mats)
        cls = ""
        if row.geo is None or not row.all_mats_match_geo:
            cls = ' class="fail"'
        elif row.has_any_zero_binding or row.face_count_match_map is False:
            cls = ' class="warn"'
        html.append(
            f"<tr{cls}><td>{row.segment_id}</td><td>{geo_faces}</td><td>{tri_count}</td><td>{quad_count}</td>"
            f"<td>{'' if row.map_face_count is None else row.map_face_count}</td><td>{mat_count}</td><td>{zero_total}</td>"
            f"<td>{row.all_mats_match_geo}</td><td>{'' if row.face_count_match_map is None else row.face_count_match_map}</td></tr>"
        )
    html.extend(["</table>", "</body></html>"])
    out_path.write_text("\n".join(html), encoding="utf-8")


def collect_audit(package_dir: Path, segment_id: Optional[int], segments_map_path: Optional[Path]) -> List[SegmentAudit]:
    face_counts = read_segments_map_face_counts(segments_map_path)
    geos = sorted(package_dir.glob("S???.GEO"))
    if segment_id is not None:
        geos = [p for p in geos if p.stem == f"S{segment_id:03d}"]

    rows: List[SegmentAudit] = []
    for geo_path in geos:
        geo = parse_geo(geo_path)
        mats = sorted(package_dir.glob(f"S{geo.segment_id:03d}M*.MAT"))
        mat_infos = [parse_mat(p) for p in mats]
        all_match = all(m.face_count == geo.face_count for m in mat_infos)
        has_zero = any(m.zero_bindings > 0 for m in mat_infos)
        map_face_count = face_counts.get(geo.segment_id)
        face_count_match_map = None if map_face_count is None else (map_face_count == geo.face_count)
        rows.append(
            SegmentAudit(
                segment_id=geo.segment_id,
                geo=geo,
                mats=mat_infos,
                map_face_count=map_face_count,
                face_count_match_map=face_count_match_map,
                has_any_zero_binding=has_zero,
                all_mats_match_geo=all_match,
            )
        )
    return rows


def main() -> int:
    ap = argparse.ArgumentParser(description="Audita GEO/MAT finais consumidos pelo runtime.")
    ap.add_argument("--package-dir", required=True, help="Diretório do pacote com Sxxx.GEO e SxxxM*.MAT")
    ap.add_argument("--segment-id", type=int, default=None, help="Restringe a um segmento")
    ap.add_argument("--segments-map", default=None, help="segments_map.json final para comparação opcional")
    ap.add_argument("--report-html", default=None, help="Relatório HTML")
    args = ap.parse_args()

    package_dir = Path(args.package_dir)
    if not package_dir.exists():
        raise SystemExit(f"Pacote nao encontrado: {package_dir}")
    segments_map_path = Path(args.segments_map) if args.segments_map else None

    rows = collect_audit(package_dir, args.segment_id, segments_map_path)
    if not rows:
        raise SystemExit("Nenhum GEO encontrado para auditoria.")

    total_zero = sum(sum(m.zero_bindings for m in row.mats) for row in rows)
    mismatch_geo_mat = sum(1 for row in rows if not row.all_mats_match_geo)
    mismatch_map = sum(1 for row in rows if row.face_count_match_map is False)

    print(f"segments={len(rows)}")
    print(f"geo_mat_mismatch={mismatch_geo_mat}")
    print(f"map_face_mismatch={mismatch_map}")
    print(f"zero_bindings_total={total_zero}")
    print("segments_top:")
    for row in rows[:10]:
        print(
            json.dumps(
                {
                    "segment": row.segment_id,
                    "geo_faces": row.geo.face_count if row.geo else 0,
                    "map_faces": row.map_face_count,
                    "mat_count": len(row.mats),
                    "zero_bindings_total": sum(m.zero_bindings for m in row.mats),
                    "geo_mat_match": row.all_mats_match_geo,
                    "map_match": row.face_count_match_map,
                },
                ensure_ascii=False,
            )
        )

    if args.report_html:
        render_html(Path(args.report_html), package_dir, rows)
        print(f"html_report={args.report_html}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
