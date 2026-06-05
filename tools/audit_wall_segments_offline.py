from __future__ import annotations

import argparse
import json
import math
import struct
from dataclasses import dataclass
from html import escape
from pathlib import Path
from typing import List, Optional, Tuple


@dataclass
class Vertex:
    x: float
    y: float
    z: float


@dataclass
class Face:
    verts: List[int]
    material: str
    family_id: int = 0
    surface_type: int = 0


@dataclass
class WallSegment:
    ax: float
    az: float
    bx: float
    bz: float
    min_x: float
    max_x: float
    min_z: float
    max_z: float
    min_y: float
    max_y: float
    nx: float
    nz: float
    face_index: int
    family_id: int
    surface_type: int
    length_xz: float


@dataclass
class AuditStats:
    total_faces: int = 0
    total_vertices: int = 0
    family_count: int = 0
    driveable_faces: int = 0
    non_driveable_faces: int = 0
    vertical_candidates: int = 0
    wall_segments: int = 0
    duplicate_segments: int = 0
    degenerate_faces: int = 0
    degenerate_segments: int = 0
    zero_normals: int = 0
    family_mismatch: bool = False


def read_segments_map(path: Path, segment_id: int) -> Tuple[List[int], int]:
    root = json.loads(path.read_text(encoding="utf-8-sig"))
    seg = None
    for s in root.get("segments", []):
        if int(s.get("id", -1)) == segment_id:
            seg = s
            break
    if seg is None:
        raise ValueError(f"segmento {segment_id} nao encontrado em {path}")

    if seg.get("faces"):
        faces = sorted(seg["faces"], key=lambda x: int(x.get("index", 0)))
        family_ids = [max(0, int(f.get("familyId", 0))) for f in faces]
    else:
        family_ids = [max(0, int(v)) for v in seg.get("faceTextureFamily", [])]
    return family_ids, len(root.get("textureFamilies", []))


def read_sfmap(path: Path) -> dict[int, int]:
    data = path.read_bytes()
    if len(data) < 12:
        raise ValueError(f"SFMAP curto: {path}")
    magic, version, _reserved, entry_count = struct.unpack_from("<IHHI", data, 0)
    if magic != 0x314D4653 or version != 1:
        raise ValueError(f"SFMAP invalido: {path}")
    need = 12 + entry_count * 4
    if need > len(data):
        raise ValueError(f"SFMAP truncado: {path}")
    mapping: dict[int, int] = {}
    off = 12
    for _ in range(entry_count):
        family_id, surface_type, _pad = struct.unpack_from("<HBB", data, off)
        mapping[int(family_id)] = int(surface_type)
        off += 4
    return mapping


def is_driveable_surface(surface_type: int) -> bool:
    return surface_type in (1, 2, 3)


def parse_obj(path: Path) -> Tuple[List[Vertex], List[Face]]:
    verts: List[Vertex] = []
    faces: List[Face] = []
    current_material = ""
    for raw in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("usemtl "):
            current_material = line[7:].strip()
            continue
        if line.startswith("v "):
            parts = line.split()
            if len(parts) >= 4:
                verts.append(Vertex(float(parts[1]), float(parts[2]), float(parts[3])))
            continue
        if line.startswith("f "):
            parts = line.split()[1:]
            idxs: List[int] = []
            for p in parts:
                tok = p.split("/")[0]
                if not tok:
                    continue
                vi = int(tok)
                if vi < 0:
                    vi = len(verts) + vi
                else:
                    vi -= 1
                idxs.append(vi)
            if len(idxs) >= 3:
                faces.append(Face(verts=idxs, material=current_material))
    return verts, faces


def face_normal(a: Vertex, b: Vertex, c: Vertex) -> Tuple[float, float, float]:
    abx = b.x - a.x
    aby = b.y - a.y
    abz = b.z - a.z
    acx = c.x - a.x
    acy = c.y - a.y
    acz = c.z - a.z
    nx = aby * acz - abz * acy
    ny = abz * acx - abx * acz
    nz = abx * acy - aby * acx
    return nx, ny, nz


def endpoint_key(ax: float, az: float, bx: float, bz: float) -> Tuple[Tuple[int, int], Tuple[int, int]]:
    a = (round(ax * 1000), round(az * 1000))
    b = (round(bx * 1000), round(bz * 1000))
    return (a, b) if a <= b else (b, a)


def reconstruct_wall_segments(
    verts: List[Vertex],
    faces: List[Face],
    family_ids: Optional[List[int]],
    surface_types: Optional[dict[int, int]],
    driveable_filter: bool,
) -> Tuple[AuditStats, List[WallSegment], List[dict]]:
    stats = AuditStats(total_faces=len(faces), total_vertices=len(verts))
    if family_ids is not None and len(family_ids) != len(faces):
        stats.family_mismatch = True

    segments: List[WallSegment] = []
    suspicious: List[dict] = []
    dedup = set()

    for fi, face in enumerate(faces):
        if family_ids is not None and fi < len(family_ids):
            face.family_id = int(family_ids[fi])
        if surface_types is not None and face.family_id in surface_types:
            face.surface_type = int(surface_types[face.family_id])

        if face.surface_type and is_driveable_surface(face.surface_type):
            stats.driveable_faces += 1
            if driveable_filter:
                continue
        else:
            stats.non_driveable_faces += 1

        pts = [verts[i] for i in face.verts if 0 <= i < len(verts)]
        if len(pts) < 3:
            stats.degenerate_faces += 1
            suspicious.append({"face": fi, "reason": "face com menos de 3 vertices"})
            continue

        nx, ny, nz = face_normal(pts[0], pts[1], pts[2])
        if nx == 0.0 and ny == 0.0 and nz == 0.0:
            stats.zero_normals += 1
            stats.degenerate_faces += 1
            suspicious.append({"face": fi, "reason": "normal zero"})
            continue

        planar_abs = max(abs(nx), abs(nz))
        if planar_abs <= 0.0:
            continue
        if (planar_abs * 2.0) < abs(ny):
            continue
        stats.vertical_candidates += 1

        best_a = 0
        best_b = 1
        best_len_sq = -1.0
        for a in range(len(pts)):
            for b in range(a + 1, len(pts)):
                dx = pts[b].x - pts[a].x
                dz = pts[b].z - pts[a].z
                lsq = dx * dx + dz * dz
                if lsq > best_len_sq:
                    best_len_sq = lsq
                    best_a = a
                    best_b = b
        if best_len_sq <= 0.0:
            stats.degenerate_segments += 1
            suspicious.append({"face": fi, "reason": "sem aresta XZ valida"})
            continue

        pa = pts[best_a]
        pb = pts[best_b]
        key = endpoint_key(pa.x, pa.z, pb.x, pb.z)
        if key in dedup:
            stats.duplicate_segments += 1
            suspicious.append({"face": fi, "reason": "segmento duplicado", "family": face.family_id})
            continue
        dedup.add(key)

        max_axis = max(abs(nx), abs(nz))
        nnx = nx / max_axis
        nnz = nz / max_axis
        min_x = min(p.x for p in pts)
        max_x = max(p.x for p in pts)
        min_y = min(p.y for p in pts)
        max_y = max(p.y for p in pts)
        min_z = min(p.z for p in pts)
        max_z = max(p.z for p in pts)
        length_xz = math.sqrt(best_len_sq)

        if length_xz < 0.25:
            suspicious.append({
                "face": fi,
                "reason": "segmento muito curto",
                "family": face.family_id,
                "length_xz": round(length_xz, 4),
            })

        segments.append(WallSegment(
            ax=pa.x, az=pa.z, bx=pb.x, bz=pb.z,
            min_x=min_x, max_x=max_x, min_z=min_z, max_z=max_z,
            min_y=min_y, max_y=max_y,
            nx=nnx, nz=nnz,
            face_index=fi,
            family_id=face.family_id,
            surface_type=face.surface_type,
            length_xz=length_xz,
        ))

    stats.wall_segments = len(segments)
    stats.family_count = len({f.family_id for f in faces if f.family_id > 0})
    suspicious.sort(key=lambda x: (x.get("length_xz", 999999.0), x.get("face", 999999)))
    return stats, segments, suspicious


def render_html(
    out_path: Path,
    obj_path: Path,
    segment_id: Optional[int],
    stats: AuditStats,
    segments: List[WallSegment],
    suspicious: List[dict],
) -> None:
    shortest = sorted(segments, key=lambda s: s.length_xz)[:20]
    html = [
        "<!DOCTYPE html>",
        '<html lang="pt-BR">',
        "<head>",
        '  <meta charset="UTF-8" />',
        "  <title>Auditoria Offline de WallSegment2D</title>",
        "  <style>",
        "body { font-family: Segoe UI, Arial, sans-serif; margin: 24px; line-height: 1.45; color: #111; }",
        "table { border-collapse: collapse; width: 100%; margin: 16px 0; }",
        "th, td { border: 1px solid #cfd6dd; padding: 8px; text-align: left; vertical-align: top; }",
        "th { background: #eef3f8; }",
        "code, pre { font-family: Consolas, monospace; background: #f4f6f8; }",
        "  </style>",
        "</head>",
        "<body>",
        "<h1>Auditoria Offline de WallSegment2D</h1>",
        f"<p><strong>OBJ:</strong> {escape(str(obj_path))}</p>",
        f"<p><strong>Segmento:</strong> {segment_id if segment_id is not None else 'n/a'}</p>",
        "<h2>Resumo</h2>",
        "<table>",
        "<tr><th>Métrica</th><th>Valor</th></tr>",
    ]
    metrics = [
        ("Vertices", stats.total_vertices),
        ("Faces", stats.total_faces),
        ("Famílias detectadas", stats.family_count),
        ("Faces dirigíveis", stats.driveable_faces),
        ("Faces não dirigíveis", stats.non_driveable_faces),
        ("Faces verticais candidatas", stats.vertical_candidates),
        ("Wall segments gerados", stats.wall_segments),
        ("Duplicados removidos", stats.duplicate_segments),
        ("Faces degeneradas", stats.degenerate_faces),
        ("Segmentos degenerados", stats.degenerate_segments),
        ("Normals zero", stats.zero_normals),
        ("Mismatch face/family count", stats.family_mismatch),
    ]
    for k, v in metrics:
        html.append(f"<tr><td>{escape(str(k))}</td><td>{escape(str(v))}</td></tr>")
    html.append("</table>")

    html.append("<h2>20 menores segmentos</h2>")
    html.append("<table><tr><th>Face</th><th>Família</th><th>SurfaceType</th><th>Comprimento XZ</th><th>A</th><th>B</th></tr>")
    for s in shortest:
        html.append(
            f"<tr><td>{s.face_index}</td><td>{s.family_id}</td><td>{s.surface_type}</td>"
            f"<td>{s.length_xz:.4f}</td><td>({s.ax:.3f},{s.az:.3f})</td><td>({s.bx:.3f},{s.bz:.3f})</td></tr>"
        )
    html.append("</table>")

    html.append("<h2>Ocorrências suspeitas</h2>")
    html.append("<table><tr><th>Face</th><th>Família</th><th>Motivo</th><th>Comprimento</th></tr>")
    for item in suspicious[:100]:
        html.append(
            f"<tr><td>{item.get('face','')}</td><td>{item.get('family','')}</td>"
            f"<td>{escape(str(item.get('reason','')))}</td><td>{item.get('length_xz','')}</td></tr>"
        )
    html.append("</table>")
    html.append("</body></html>")
    out_path.write_text("\n".join(html), encoding="utf-8")


def main() -> int:
    ap = argparse.ArgumentParser(description="Audita offline a reconstrução de WallSegment2D a partir do OBJ-fonte do segmento.")
    ap.add_argument("--obj", required=True, help="Caminho do OBJ do segmento (ex.: seg_001.obj)")
    ap.add_argument("--segment-id", type=int, default=None, help="ID do segmento para casar com segments_map.json")
    ap.add_argument("--segments-map", default=None, help="segments_map.json com faceTextureFamily/faces[].familyId")
    ap.add_argument("--sfmap", default=None, help="SFMAP.BIN para mapear familyId -> surfaceType")
    ap.add_argument("--keep-driveable", action="store_true", help="Não filtra faces dirigíveis")
    ap.add_argument("--report-html", default=None, help="Saída HTML")
    args = ap.parse_args()

    obj_path = Path(args.obj)
    if not obj_path.exists():
        raise SystemExit(f"OBJ nao encontrado: {obj_path}")

    verts, faces = parse_obj(obj_path)
    family_ids = None
    if args.segments_map:
        if args.segment_id is None:
            raise SystemExit("--segment-id é obrigatório quando --segments-map é usado")
        family_ids, _ = read_segments_map(Path(args.segments_map), args.segment_id)

    surface_types = None
    if args.sfmap:
        surface_types = read_sfmap(Path(args.sfmap))

    stats, segments, suspicious = reconstruct_wall_segments(
        verts, faces, family_ids, surface_types, driveable_filter=not args.keep_driveable
    )

    print(f"OBJ: {obj_path}")
    print(f"vertices={stats.total_vertices} faces={stats.total_faces}")
    print(f"vertical_candidates={stats.vertical_candidates} wall_segments={stats.wall_segments}")
    print(f"duplicates={stats.duplicate_segments} deg_faces={stats.degenerate_faces} deg_segments={stats.degenerate_segments} zero_normals={stats.zero_normals}")
    print(f"driveable_faces={stats.driveable_faces} non_driveable_faces={stats.non_driveable_faces}")
    print(f"family_mismatch={stats.family_mismatch}")

    if suspicious:
        print("suspicious_top10:")
        for item in suspicious[:10]:
            print(json.dumps(item, ensure_ascii=False))

    if args.report_html:
        render_html(Path(args.report_html), obj_path, args.segment_id, stats, segments, suspicious)
        print(f"html_report={args.report_html}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
