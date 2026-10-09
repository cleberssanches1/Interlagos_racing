#!/usr/bin/env python3
"""
Extracts ground/surface/face-collision related logic snippets from a codebase.

Usage:
  python tools/extract_collision_logic.py --root "C:/.../mariokart64-master"
"""

from __future__ import annotations

import argparse
import json
import re
from collections import Counter
from pathlib import Path
from typing import Iterable

TEXT_EXTS = {".c", ".h", ".s", ".asm", ".txt", ".md"}

HIGH_SIGNAL_KEYWORDS = [
    "collision", "collide", "ground", "floor", "surface", "wall",
    "face", "triangle", "height", "slope", "plane",
    "ray", "friction", "gravity",
]

LOW_SIGNAL_KEYWORDS = [
    "segment", "road", "kart", "track",
]

KEYWORD_WEIGHTS = {
    **{k: 4 for k in HIGH_SIGNAL_KEYWORDS},
    **{k: 1 for k in LOW_SIGNAL_KEYWORDS},
}

KEYWORDS = HIGH_SIGNAL_KEYWORDS + LOW_SIGNAL_KEYWORDS
KEYWORD_RE = re.compile(r"(" + "|".join(re.escape(k) for k in KEYWORDS) + r")", re.IGNORECASE)
HIGH_SIGNAL_SET = {k.lower() for k in HIGH_SIGNAL_KEYWORDS}

C_FUNC_RE = re.compile(
    r"^\s*(?:[\w\*\s]+)\s+([A-Za-z_]\w*)\s*\([^;]*\)\s*\{\s*$"
)
ASM_LABEL_RE = re.compile(r"^\s*([A-Za-z_.$][\w.$]*):\s*(?:;.*)?$")


def read_text(path: Path) -> str:
    raw = path.read_bytes()
    for enc in ("utf-8", "utf-8-sig", "cp1252", "latin1"):
        try:
            return raw.decode(enc)
        except UnicodeDecodeError:
            continue
    return raw.decode("latin1", errors="replace")


def iter_files(root: Path) -> Iterable[Path]:
    for p in root.rglob("*"):
        if not p.is_file():
            continue
        if p.suffix.lower() not in TEXT_EXTS:
            continue
        yield p


def find_owner_block(lines: list[str], line_idx: int, suffix: str) -> str:
    start = max(0, line_idx - 140)
    for i in range(line_idx, start - 1, -1):
        line = lines[i]
        if suffix in {".c", ".h"}:
            m = C_FUNC_RE.match(line)
            if m:
                return f"function {m.group(1)}"
        if suffix in {".s", ".asm"}:
            m = ASM_LABEL_RE.match(line)
            if m:
                return f"label {m.group(1)}"
    return "(no function/label found)"


def snippet(lines: list[str], line_idx: int, radius: int = 2) -> str:
    a = max(0, line_idx - radius)
    b = min(len(lines), line_idx + radius + 1)
    out = []
    for i in range(a, b):
        mark = ">" if i == line_idx else " "
        out.append(f"{mark}{i+1:6d}: {lines[i]}".rstrip())
    return "\n".join(out)


def analyze(root: Path, max_hits_per_file: int) -> dict:
    files = list(iter_files(root))
    ext_count = Counter(p.suffix.lower() for p in files)

    results = []
    total_hits = 0

    for path in files:
        text = read_text(path)
        lines = text.splitlines()
        hits = []
        weighted_hit_score = 0
        high_signal_hits = 0
        for idx, line in enumerate(lines):
            kw = [m.lower() for m in KEYWORD_RE.findall(line)]
            if not kw:
                continue
            if not any(k in HIGH_SIGNAL_SET for k in kw):
                # Avoid noise-only matches such as lines containing only "track"/"road".
                continue
            owner = find_owner_block(lines, idx, path.suffix.lower())
            unique_kw = sorted(set(kw))
            weighted_hit_score += sum(KEYWORD_WEIGHTS.get(k, 1) for k in unique_kw)
            high_signal_hits += sum(1 for k in unique_kw if k in HIGH_SIGNAL_SET)
            hits.append(
                {
                    "line": idx + 1,
                    "owner": owner,
                    "keywords": unique_kw,
                    "snippet": snippet(lines, idx),
                }
            )

        if not hits:
            continue

        total_hits += len(hits)
        ext_bonus = 25 if path.suffix.lower() in {".c", ".h", ".s", ".asm"} else 0
        score = weighted_hit_score + high_signal_hits + ext_bonus
        results.append(
            {
                "file": str(path),
                "ext": path.suffix.lower(),
                "score": score,
                "hit_count": len(hits),
                "weighted_hit_score": weighted_hit_score,
                "high_signal_hits": high_signal_hits,
                "hits": hits[:max_hits_per_file],
            }
        )

    results.sort(key=lambda x: (x["score"], x["hit_count"]), reverse=True)

    return {
        "root": str(root),
        "total_files_scanned": len(files),
        "extensions": dict(ext_count),
        "total_keyword_hits": total_hits,
        "files_with_hits": len(results),
        "results": results,
    }


def write_report(data: dict, md_path: Path, json_path: Path, top: int) -> None:
    md_lines = []
    md_lines.append("# Collision/Ground Logic Extraction Report")
    md_lines.append("")
    md_lines.append(f"- Root: `{data['root']}`")
    md_lines.append(f"- Files scanned: **{data['total_files_scanned']}**")
    md_lines.append(f"- Files with hits: **{data['files_with_hits']}**")
    md_lines.append(f"- Total keyword hits: **{data['total_keyword_hits']}**")
    md_lines.append("")
    md_lines.append("## File types")
    for ext, count in sorted(data["extensions"].items(), key=lambda x: x[1], reverse=True):
        md_lines.append(f"- `{ext or '(no ext)'}`: {count}")

    md_lines.append("")
    md_lines.append("## Keywords Used")
    md_lines.append("- High-signal: " + ", ".join(HIGH_SIGNAL_KEYWORDS))
    md_lines.append("- Low-signal: " + ", ".join(LOW_SIGNAL_KEYWORDS))
    code_exts = {".c", ".h", ".s", ".asm"}
    code_entries = [e for e in data["results"] if e["ext"] in code_exts]
    note_entries = [e for e in data["results"] if e["ext"] not in code_exts]

    md_lines.append("")
    md_lines.append(f"## Top {top} relevant code files")

    for entry in code_entries[:top]:
        md_lines.append(f"### {entry['file']}")
        md_lines.append(
            f"- Score: {entry['score']} | Weighted: {entry['weighted_hit_score']} | "
            f"High-signal hits: {entry['high_signal_hits']} | Hits: {entry['hit_count']} | Type: `{entry['ext']}`"
        )
        for h in entry["hits"]:
            md_lines.append(f"- Line {h['line']} | {h['owner']} | keywords: {', '.join(h['keywords'])}")
            md_lines.append("```text")
            md_lines.append(h["snippet"])
            md_lines.append("```")
        md_lines.append("")

    md_lines.append(f"## Top {top} relevant notes/docs files")

    for entry in note_entries[:top]:
        md_lines.append(f"### {entry['file']}")
        md_lines.append(
            f"- Score: {entry['score']} | Weighted: {entry['weighted_hit_score']} | "
            f"High-signal hits: {entry['high_signal_hits']} | Hits: {entry['hit_count']} | Type: `{entry['ext']}`"
        )
        for h in entry["hits"]:
            md_lines.append(f"- Line {h['line']} | {h['owner']} | keywords: {', '.join(h['keywords'])}")
            md_lines.append("```text")
            md_lines.append(h["snippet"])
            md_lines.append("```")
        md_lines.append("")

    md_path.write_text("\n".join(md_lines), encoding="utf-8")
    json_path.write_text(json.dumps(data, indent=2, ensure_ascii=False), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="Extracts ground/collision/face logic from mixed C/ASM/text repositories")
    parser.add_argument("--root", required=True, help="Repository/folder to scan")
    parser.add_argument("--out-md", default="mk64_collision_extract.md", help="Output markdown report path")
    parser.add_argument("--out-json", default="mk64_collision_extract.json", help="Output JSON report path")
    parser.add_argument("--top", type=int, default=20, help="Top files to include in markdown")
    parser.add_argument("--max-hits-per-file", type=int, default=8, help="Max snippets per file in output")
    args = parser.parse_args()

    root = Path(args.root)
    if not root.exists() or not root.is_dir():
        raise SystemExit(f"Invalid --root: {root}")

    data = analyze(root=root, max_hits_per_file=args.max_hits_per_file)
    out_md = Path(args.out_md)
    out_json = Path(args.out_json)
    write_report(data, out_md, out_json, top=max(1, args.top))

    print(f"[ok] scanned={data['total_files_scanned']} files, hits={data['total_keyword_hits']}, files_with_hits={data['files_with_hits']}")
    print(f"[ok] markdown={out_md.resolve()}")
    print(f"[ok] json={out_json.resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
