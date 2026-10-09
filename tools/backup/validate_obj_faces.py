#!/usr/bin/env python3
"""
Validate OBJ faces for ModelConverter NyaExport.

Main checks:
- Face polygon size (default: only triangles/quads are allowed)
- Face token syntax
- Vertex/UV/normal index ranges (supports positive and negative indices)

Usage:
    python tools/validate_obj_faces.py "C:\\Models\\png\\F1_model_3.obj"
    python tools/validate_obj_faces.py "file.obj" --allow 3 4
"""

from __future__ import annotations

import argparse
import re
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional, Tuple


FACE_TOKEN_RE = re.compile(r"^[+-]?\d+(?:/[+-]?\d*(?:/[+-]?\d+)?)?$")


@dataclass
class FaceIssue:
    line: int
    kind: str
    detail: str
    source: str


def _resolve_index(raw: int, count_so_far: int) -> Optional[int]:
    """
    Resolve OBJ index to 1-based absolute index using count_so_far.
    Returns None when index 0/invalid.
    """
    if raw == 0:
        return None
    if raw > 0:
        return raw
    # negative index is relative to current element list size
    resolved = count_so_far + raw + 1
    if resolved <= 0:
        return None
    return resolved


def _parse_face_token(token: str) -> Tuple[Optional[int], Optional[int], Optional[int]]:
    # Formats:
    # v
    # v/vt
    # v//vn
    # v/vt/vn
    parts = token.split("/")
    v = int(parts[0]) if parts[0] else None
    vt = None
    vn = None
    if len(parts) >= 2 and parts[1] != "":
        vt = int(parts[1])
    if len(parts) >= 3 and parts[2] != "":
        vn = int(parts[2])
    return v, vt, vn


def validate_obj(path: Path, allowed_face_sizes: set[int]) -> Tuple[Counter, List[FaceIssue], Counter]:
    issues: List[FaceIssue] = []
    face_size_counter: Counter = Counter()
    primitive_counter: Counter = Counter()

    v_count = 0
    vt_count = 0
    vn_count = 0

    with path.open("r", encoding="utf-8", errors="replace") as f:
        for line_no, line in enumerate(f, 1):
            src = line.rstrip("\n")
            s = src.strip()
            if not s or s.startswith("#"):
                continue

            parts = s.split()
            head = parts[0]
            primitive_counter[head] += 1

            if head == "v":
                v_count += 1
                continue
            if head == "vt":
                vt_count += 1
                continue
            if head == "vn":
                vn_count += 1
                continue
            if head != "f":
                continue

            tokens = parts[1:]
            n = len(tokens)
            face_size_counter[n] += 1
            if n not in allowed_face_sizes:
                issues.append(FaceIssue(line_no, "face_size", f"face has {n} vertices", src))

            for token in tokens:
                if not FACE_TOKEN_RE.fullmatch(token):
                    issues.append(FaceIssue(line_no, "token_syntax", f"invalid token '{token}'", src))
                    continue

                v, vt, vn = _parse_face_token(token)
                if v is None:
                    issues.append(FaceIssue(line_no, "vertex_index", f"missing vertex index in token '{token}'", src))
                    continue

                rv = _resolve_index(v, v_count)
                if rv is None or rv > v_count:
                    issues.append(FaceIssue(line_no, "vertex_index", f"vertex index out of range in token '{token}'", src))

                if vt is not None:
                    rvt = _resolve_index(vt, vt_count)
                    if rvt is None or rvt > vt_count:
                        issues.append(FaceIssue(line_no, "uv_index", f"uv index out of range in token '{token}'", src))

                if vn is not None:
                    rvn = _resolve_index(vn, vn_count)
                    if rvn is None or rvn > vn_count:
                        issues.append(FaceIssue(line_no, "normal_index", f"normal index out of range in token '{token}'", src))

    return face_size_counter, issues, primitive_counter


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate OBJ faces for NyaExport.")
    parser.add_argument("obj", type=Path, help="Path to OBJ file")
    parser.add_argument(
        "--allow",
        type=int,
        nargs="+",
        default=[3, 4],
        help="Allowed face vertex counts (default: 3 4)",
    )
    parser.add_argument(
        "--max-lines",
        type=int,
        default=200,
        help="Maximum issue lines printed (default: 200)",
    )
    args = parser.parse_args()

    obj_path: Path = args.obj
    if not obj_path.exists():
        print(f"[ERROR] File not found: {obj_path}")
        return 2

    allowed = set(args.allow)
    face_hist, issues, prim_hist = validate_obj(obj_path, allowed)

    print(f"OBJ: {obj_path}")
    print("Primitives:", dict(sorted(prim_hist.items())))
    print("Face vertex histogram:", dict(sorted(face_hist.items())))
    print("Allowed face sizes:", sorted(allowed))
    print("Issues:", len(issues))

    for i, issue in enumerate(issues[: args.max_lines], 1):
        print(f"{i:04d}. line {issue.line} [{issue.kind}] {issue.detail}")
        print(f"      {issue.source}")

    if len(issues) > args.max_lines:
        print(f"... {len(issues) - args.max_lines} more issue(s) omitted")

    return 1 if issues else 0


if __name__ == "__main__":
    raise SystemExit(main())

