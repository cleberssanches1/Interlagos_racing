#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


@dataclass(frozen=True)
class SymbolRequest:
    rel_path: str
    kind: str
    symbol: str
    note: str


REQUESTS: tuple[SymbolRequest, ...] = (
    SymbolRequest(
        "src_rebuild/Game/dr2types.h",
        "struct",
        "HANDLING_DATA",
        "Core drivetrain state: wheel speed, revs, gear, changingGear, autoBrake.",
    ),
    SymbolRequest(
        "src_rebuild/Game/dr2types.h",
        "struct",
        "PLAYER",
        "Per-player audio state: car_sound_timer, revsvol, idlevol, skidding, wheelnoise.",
    ),
    SymbolRequest(
        "src_rebuild/Game/C/gamesnd.c",
        "struct",
        "GEAR_DESC",
        "Gear band thresholds and rev ratios used by the audio model.",
    ),
    SymbolRequest(
        "src_rebuild/Game/C/gamesnd.c",
        "array",
        "geard",
        "Forward speed bands for civilian/player cars.",
    ),
    SymbolRequest(
        "src_rebuild/Game/C/gamesnd.c",
        "function",
        "GetEngineRevs",
        "Maps wheel speed, thrust and gear into engine revs.",
    ),
    SymbolRequest(
        "src_rebuild/Game/C/gamesnd.c",
        "function",
        "ControlCarRevs",
        "Smooths rev changes, marks changingGear, and crossfades idle/rev volumes.",
    ),
    SymbolRequest(
        "src_rebuild/Game/C/gamesnd.c",
        "context",
        "car engine sounds",
        "Updates two engine channels every frame: rev bed and idle bed.",
    ),
    SymbolRequest(
        "src_rebuild/Game/C/wheelforces.c",
        "function",
        "GetFrictionScalesDriver1",
        "How thrust, handbrake and wheelspin alter traction and wheel lock.",
    ),
    SymbolRequest(
        "src_rebuild/Game/C/wheelforces.c",
        "function",
        "AddWheelForcesDriver1",
        "Wheel contact, surface sampling and slip metrics used by tyre audio.",
    ),
    SymbolRequest(
        "src_rebuild/Game/C/handling.c",
        "context",
        "desired_skid",
        "Skid sound selection, restart and continuous update.",
    ),
    SymbolRequest(
        "src_rebuild/Game/C/handling.c",
        "context",
        "desired_wheel",
        "Wheel noise selection by surface and speed.",
    ),
)


def read_lines(path: Path) -> list[str]:
    return path.read_text(encoding="utf-8", errors="replace").splitlines()


def find_line(lines: list[str], pattern: str) -> int:
    regex = re.compile(pattern)
    for idx, line in enumerate(lines):
        if regex.search(line):
            return idx
    return -1


def extract_brace_block(lines: list[str], start_line: int) -> tuple[int, int]:
    brace_depth = 0
    seen_open = False
    start = start_line
    for idx in range(start_line, len(lines)):
        line = lines[idx]
        for char in line:
            if char == "{":
                brace_depth += 1
                seen_open = True
            elif char == "}":
                brace_depth -= 1
                if seen_open and brace_depth == 0:
                    return start, idx
    raise ValueError(f"Could not close brace block starting at line {start_line + 1}")


def extract_array_block(lines: list[str], start_line: int) -> tuple[int, int]:
    start = start_line
    semicolon_seen = False
    brace_depth = 0
    for idx in range(start_line, len(lines)):
        line = lines[idx]
        brace_depth += line.count("{")
        brace_depth -= line.count("}")
        if ";" in line and brace_depth <= 0:
            semicolon_seen = True
            return start, idx
    if not semicolon_seen:
        raise ValueError(f"Could not close array block starting at line {start_line + 1}")
    return start, start_line


def extract_context(lines: list[str], start_line: int, radius: int = 28) -> tuple[int, int]:
    start = max(0, start_line - 6)
    end = min(len(lines) - 1, start_line + radius)
    return start, end


def extract_request(root: Path, request: SymbolRequest) -> dict:
    path = root / request.rel_path
    lines = read_lines(path)

    if request.kind == "function":
        line_idx = find_line(lines, rf"\b{re.escape(request.symbol)}\s*\(")
        if line_idx < 0:
            raise ValueError(f"Function {request.symbol} not found in {request.rel_path}")
        start, end = extract_brace_block(lines, line_idx)
    elif request.kind == "struct":
        patterns = (
            rf"typedef\s+struct\s+_{re.escape(request.symbol)}\b",
            rf"struct\s+{re.escape(request.symbol)}\b",
            rf"typedef\s+struct\s+{re.escape(request.symbol)}\b",
            rf"_ {re.escape(request.symbol)}",
        )
        line_idx = -1
        for pattern in patterns:
            line_idx = find_line(lines, pattern)
            if line_idx >= 0:
                break
        if line_idx < 0:
            raise ValueError(f"Struct {request.symbol} not found in {request.rel_path}")
        start, end = extract_brace_block(lines, line_idx)
        if end + 1 < len(lines) and lines[end + 1].strip().startswith(request.symbol):
            end += 1
        elif end + 1 < len(lines) and lines[end + 1].strip().startswith("} "):
            end += 1
    elif request.kind == "array":
        line_idx = find_line(lines, rf"\b{re.escape(request.symbol)}\b\s*\[")
        if line_idx < 0:
            raise ValueError(f"Array {request.symbol} not found in {request.rel_path}")
        start, end = extract_array_block(lines, line_idx)
    elif request.kind == "context":
        line_idx = find_line(lines, re.escape(request.symbol))
        if line_idx < 0:
            raise ValueError(f"Context anchor {request.symbol} not found in {request.rel_path}")
        start, end = extract_context(lines, line_idx)
    else:
        raise ValueError(f"Unsupported kind {request.kind}")

    snippet = "\n".join(lines[start : end + 1]).rstrip()
    return {
        "path": request.rel_path.replace("\\", "/"),
        "kind": request.kind,
        "symbol": request.symbol,
        "note": request.note,
        "start_line": start + 1,
        "end_line": end + 1,
        "snippet": snippet,
    }


def build_markdown(repo_root: Path, entries: Iterable[dict]) -> str:
    lines: list[str] = []
    lines.append("# REDRIVER2 vehicle audio/physics extraction")
    lines.append("")
    lines.append(f"Source root: `{repo_root}`")
    lines.append("")
    lines.append("This report extracts the code most relevant to car acceleration, gear/revs, movement coupling and tyre/engine audio.")
    lines.append("")

    for entry in entries:
        lines.append(f"## {entry['symbol']} ({entry['kind']})")
        lines.append("")
        lines.append(f"- File: `{entry['path']}`")
        lines.append(f"- Lines: `{entry['start_line']}-{entry['end_line']}`")
        lines.append(f"- Why it matters: {entry['note']}")
        lines.append("")
        lines.append("```c")
        lines.append(entry["snippet"])
        lines.append("```")
        lines.append("")

    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description="Extract REDRIVER2 vehicle movement/audio logic into a local report.")
    parser.add_argument(
        "--repo-root",
        default=r"C:\saturn\SaturnRingLib-main\Projects\Projetos_Exemplos\REDRIVER2-master",
        help="Path to the REDRIVER2 repository root.",
    )
    parser.add_argument(
        "--output-dir",
        default=str(Path(__file__).resolve().parent / "reports" / "redriver2_vehicle_audio"),
        help="Directory where markdown/json outputs will be written.",
    )
    args = parser.parse_args()

    repo_root = Path(args.repo_root)
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    entries = [extract_request(repo_root, request) for request in REQUESTS]

    markdown_path = output_dir / "redriver2_vehicle_audio_report.md"
    json_path = output_dir / "redriver2_vehicle_audio_report.json"

    markdown_path.write_text(build_markdown(repo_root, entries), encoding="utf-8")
    json_path.write_text(json.dumps(entries, indent=2, ensure_ascii=True), encoding="utf-8")

    print(f"Wrote {markdown_path}")
    print(f"Wrote {json_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
