#!/usr/bin/env python3
"""
Daytona Saturn course reverse-engineering helper.

Focused on COURSE*.MDL and CS*_BLK.BIN files extracted from DAYTONA USA CE.

Usage examples:
  python tools/daytona_course_tools.py summary --daytona-dir "C:\\saturn\\Saturn_game\\DAYTONA_USA_CE\\DAYTONA_EXTRACT_TEST\\DAYTONA" --course 1
  python tools/daytona_course_tools.py dump-mdl --mdl "...\\COURSE1.MDL" --json-out COURSE1_MDL.json --csv-out COURSE1_vertices.csv
  python tools/daytona_course_tools.py patch-mdl-vertex --mdl "...\\COURSE1.MDL" --index 0 --x -11200 --y 32 --z -6400 --output "...\\COURSE1_EDIT.MDL"
  python tools/daytona_course_tools.py dump-blk --blk "...\\CS1_BLK.BIN" --json-out CS1_BLK.json
"""

from __future__ import annotations

import argparse
import csv
import json
import struct
import sys
from pathlib import Path
from typing import Dict, List, Tuple


def _u16_to_s16(v: int) -> int:
    return v - 65536 if v >= 32768 else v


def _s16_to_u16(v: int) -> int:
    if v < -32768 or v > 32767:
        raise ValueError(f"value {v} outside int16 range")
    return v & 0xFFFF


def parse_mdl(path: Path) -> Dict:
    data = path.read_bytes()
    if len(data) < 20:
        raise ValueError(f"{path} too small to be a valid MDL")

    count_a, count_b = struct.unpack_from(">HH", data, 0)
    table0_off, section1_off, tail_off = struct.unpack_from(">III", data, 4)

    if table0_off + count_a * 6 > len(data):
        raise ValueError("table0 header points outside file")

    vertices = []
    for i in range(count_a):
        x_u, y_u, z_u = struct.unpack_from(">HHH", data, table0_off + i * 6)
        vertices.append(
            {
                "index": i,
                "x": _u16_to_s16(x_u),
                "y": _u16_to_s16(y_u),
                "z": _u16_to_s16(z_u),
                "x_u16": x_u,
                "y_u16": y_u,
                "z_u16": z_u,
            }
        )

    x_values = [v["x"] for v in vertices] if vertices else [0]
    y_values = [v["y"] for v in vertices] if vertices else [0]
    z_values = [v["z"] for v in vertices] if vertices else [0]

    section1_len = max(0, tail_off - section1_off) if tail_off <= len(data) else 0
    tail_len = max(0, len(data) - tail_off) if tail_off <= len(data) else 0
    tail_pairs = tail_len // 8
    tail_remainder = tail_len % 8

    section1_preview: List[Tuple[int, int, int, int, int, int]] = []
    preview_count = min(16, section1_len // 12)
    for i in range(preview_count):
        section1_preview.append(struct.unpack_from(">6H", data, section1_off + i * 12))

    return {
        "path": str(path),
        "size": len(data),
        "header": {
            "count_a": count_a,
            "count_b": count_b,
            "table0_off": table0_off,
            "section1_off": section1_off,
            "tail_off": tail_off,
            "section1_len": section1_len,
            "tail_len": tail_len,
            "tail_pairs_8_bytes": tail_pairs,
            "tail_remainder_bytes": tail_remainder,
        },
        "table0_stats": {
            "min_x": min(x_values),
            "max_x": max(x_values),
            "min_y": min(y_values),
            "max_y": max(y_values),
            "min_z": min(z_values),
            "max_z": max(z_values),
        },
        "section1_preview_u16x6": section1_preview,
        "vertices": vertices,
    }


def write_vertices_csv(vertices: List[Dict], out_path: Path) -> None:
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["index", "x", "y", "z", "x_u16", "y_u16", "z_u16"])
        for v in vertices:
            w.writerow([v["index"], v["x"], v["y"], v["z"], v["x_u16"], v["y_u16"], v["z_u16"]])


def patch_mdl_vertex(path: Path, index: int, x: int | None, y: int | None, z: int | None, output: Path | None) -> Path:
    data = bytearray(path.read_bytes())
    if len(data) < 20:
        raise ValueError(f"{path} too small to be a valid MDL")

    count_a = struct.unpack_from(">H", data, 0)[0]
    table0_off = struct.unpack_from(">I", data, 4)[0]

    if index < 0 or index >= count_a:
        raise ValueError(f"vertex index out of range: {index} (0..{count_a - 1})")

    base = table0_off + index * 6
    x_u, y_u, z_u = struct.unpack_from(">HHH", data, base)
    current = [_u16_to_s16(x_u), _u16_to_s16(y_u), _u16_to_s16(z_u)]

    updated = [current[0], current[1], current[2]]
    if x is not None:
        updated[0] = x
    if y is not None:
        updated[1] = y
    if z is not None:
        updated[2] = z

    struct.pack_into(">HHH", data, base, _s16_to_u16(updated[0]), _s16_to_u16(updated[1]), _s16_to_u16(updated[2]))

    target = output if output is not None else path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(data)
    return target


def parse_blk(path: Path, preview_words: int = 12) -> Dict:
    data = path.read_bytes()
    if len(data) < 16:
        raise ValueError(f"{path} too small to be a valid BLK")

    o0, o1, o2, o3 = struct.unpack_from(">4I", data, 0)
    if not (0 <= o0 <= o1 <= o2 <= o3 <= len(data)):
        raise ValueError("BLK section offsets are not monotonic or exceed file size")

    sec1 = data[o0:o1]
    sec2 = data[o1:o2]
    sec3 = data[o2:o3]
    sec4 = data[o3:]

    sec2_u16 = []
    if len(sec2) % 2 == 0:
        sec2_u16 = list(struct.unpack(">" + "H" * (len(sec2) // 2), sec2))

    sec2_nonzero = [v for v in sec2_u16 if v != 0]
    sec2_nonzero_unique_sorted = sorted(set(sec2_nonzero))

    sec1_ref_preview = []
    if sec2_nonzero_unique_sorted:
        for ref in sec2_nonzero_unique_sorted[:32]:
            if ref + preview_words * 2 <= len(sec1):
                words = struct.unpack_from(">" + "h" * preview_words, sec1, ref)
                sec1_ref_preview.append({"ref": ref, "words": list(words)})

    sec4_u32 = []
    if len(sec4) % 4 == 0:
        sec4_u32 = list(struct.unpack(">" + "I" * (len(sec4) // 4), sec4))

    sec3_ref_preview = []
    sec4_nonzero_refs = [v for v in sec4_u32 if v != 0]
    for abs_ref in sec4_nonzero_refs[:32]:
        rel = abs_ref - o2
        if 0 <= rel and rel + preview_words * 2 <= len(sec3):
            words = struct.unpack_from(">" + "h" * preview_words, sec3, rel)
            sec3_ref_preview.append({"abs_ref": abs_ref, "rel_ref": rel, "words": list(words)})

    # IDs visible in sec3 referenced pool (useful to identify segment index universe)
    sec3_ids = []
    if sec4_nonzero_refs:
        pool_start_abs = min(sec4_nonzero_refs)
        pool_start_rel = pool_start_abs - o2
        if 0 <= pool_start_rel < len(sec3):
            pool = sec3[pool_start_rel:]
            if len(pool) % 2 == 0:
                vals = struct.unpack(">" + "h" * (len(pool) // 2), pool)
                sec3_ids = sorted({v for v in vals if v >= 0})

    return {
        "path": str(path),
        "size": len(data),
        "offsets": {
            "section1_off": o0,
            "section2_off": o1,
            "section3_off": o2,
            "section4_off": o3,
        },
        "section_lengths": {
            "section1_len": len(sec1),
            "section2_len": len(sec2),
            "section3_len": len(sec3),
            "section4_len": len(sec4),
        },
        "section2_grid_u16_count": len(sec2_u16),
        "section2_nonzero_count": len(sec2_nonzero),
        "section2_nonzero_unique_count": len(sec2_nonzero_unique_sorted),
        "section2_nonzero_min": min(sec2_nonzero_unique_sorted) if sec2_nonzero_unique_sorted else None,
        "section2_nonzero_max": max(sec2_nonzero_unique_sorted) if sec2_nonzero_unique_sorted else None,
        "section2_ref_preview": sec1_ref_preview,
        "section4_u32_count": len(sec4_u32),
        "section4_nonzero_refs_count": len(sec4_nonzero_refs),
        "section3_ref_preview": sec3_ref_preview,
        "section3_id_universe": {
            "count": len(sec3_ids),
            "min": min(sec3_ids) if sec3_ids else None,
            "max": max(sec3_ids) if sec3_ids else None,
            "first_32": sec3_ids[:32],
            "last_32": sec3_ids[-32:] if sec3_ids else [],
        },
    }


def print_summary(daytona_dir: Path, course: int) -> None:
    mdl = daytona_dir / f"COURSE{course}.MDL"
    tex = daytona_dir / f"COURSE{course}.TEX"
    blk = daytona_dir / f"CS{course}_BLK.BIN"
    col = daytona_dir / f"CS{course}_COL.BIN"

    print(f"DAYTONA dir : {daytona_dir}")
    print(f"Course      : {course}")

    for p in [mdl, tex, blk, col]:
        print(f"- {p.name:12s} exists={p.exists()} size={(p.stat().st_size if p.exists() else 0)}")

    if mdl.exists():
        m = parse_mdl(mdl)
        h = m["header"]
        s = m["table0_stats"]
        print("\n[MDL]")
        print(
            f"count_a={h['count_a']} count_b={h['count_b']} "
            f"table0_off=0x{h['table0_off']:X} section1_off=0x{h['section1_off']:X} tail_off=0x{h['tail_off']:X}"
        )
        print(
            f"table0 x[{s['min_x']}..{s['max_x']}] y[{s['min_y']}..{s['max_y']}] z[{s['min_z']}..{s['max_z']}]"
        )
        print(
            f"section1_len={h['section1_len']} tail_len={h['tail_len']} tail_pairs={h['tail_pairs_8_bytes']}"
        )

    if blk.exists():
        b = parse_blk(blk)
        print("\n[BLK]")
        print(
            f"sec1={b['section_lengths']['section1_len']} "
            f"sec2={b['section_lengths']['section2_len']} "
            f"sec3={b['section_lengths']['section3_len']} "
            f"sec4={b['section_lengths']['section4_len']}"
        )
        print(
            f"sec2_u16={b['section2_grid_u16_count']} nonzero={b['section2_nonzero_count']} "
            f"unique_nonzero={b['section2_nonzero_unique_count']}"
        )
        u = b["section3_id_universe"]
        print(f"sec3 ids: count={u['count']} range=[{u['min']}..{u['max']}]")


def _json_dump(path: Path, obj: Dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(obj, indent=2, ensure_ascii=False), encoding="utf-8")


def main(argv: List[str]) -> int:
    parser = argparse.ArgumentParser(description="Daytona COURSE/BLK reverse engineering helper")
    sub = parser.add_subparsers(dest="cmd", required=True)

    p_summary = sub.add_parser("summary", help="quick summary for COURSE<n>")
    p_summary.add_argument("--daytona-dir", required=True, type=Path)
    p_summary.add_argument("--course", required=True, type=int)

    p_dump_mdl = sub.add_parser("dump-mdl", help="dump COURSE*.MDL to JSON/CSV")
    p_dump_mdl.add_argument("--mdl", required=True, type=Path)
    p_dump_mdl.add_argument("--json-out", type=Path)
    p_dump_mdl.add_argument("--csv-out", type=Path)

    p_patch = sub.add_parser("patch-mdl-vertex", help="patch one vertex in table0")
    p_patch.add_argument("--mdl", required=True, type=Path)
    p_patch.add_argument("--index", required=True, type=int)
    p_patch.add_argument("--x", type=int)
    p_patch.add_argument("--y", type=int)
    p_patch.add_argument("--z", type=int)
    p_patch.add_argument("--output", type=Path)

    p_dump_blk = sub.add_parser("dump-blk", help="dump CS*_BLK.BIN structure")
    p_dump_blk.add_argument("--blk", required=True, type=Path)
    p_dump_blk.add_argument("--json-out", type=Path)
    p_dump_blk.add_argument("--preview-words", type=int, default=12)

    args = parser.parse_args(argv)

    if args.cmd == "summary":
        print_summary(args.daytona_dir, args.course)
        return 0

    if args.cmd == "dump-mdl":
        mdl_info = parse_mdl(args.mdl)
        if args.json_out:
            _json_dump(args.json_out, mdl_info)
            print(f"JSON written: {args.json_out}")
        if args.csv_out:
            write_vertices_csv(mdl_info["vertices"], args.csv_out)
            print(f"CSV written: {args.csv_out}")
        if not args.json_out and not args.csv_out:
            print(json.dumps(mdl_info, indent=2, ensure_ascii=False))
        return 0

    if args.cmd == "patch-mdl-vertex":
        if args.x is None and args.y is None and args.z is None:
            raise ValueError("specify at least one of --x/--y/--z")
        target = patch_mdl_vertex(args.mdl, args.index, args.x, args.y, args.z, args.output)
        print(f"Patched file: {target}")
        return 0

    if args.cmd == "dump-blk":
        blk_info = parse_blk(args.blk, preview_words=args.preview_words)
        if args.json_out:
            _json_dump(args.json_out, blk_info)
            print(f"JSON written: {args.json_out}")
        else:
            print(json.dumps(blk_info, indent=2, ensure_ascii=False))
        return 0

    return 1


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise
