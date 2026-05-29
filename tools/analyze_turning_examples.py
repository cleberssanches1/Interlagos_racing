#!/usr/bin/env python3
"""
Scan local example projects and rank steering/turning implementations.

Output:
  tools/reports/turning_examples_report.md
"""

from __future__ import annotations

import pathlib
import re
from dataclasses import dataclass, field
from typing import Dict, List, Tuple


ROOT = pathlib.Path(__file__).resolve().parents[2]
EXAMPLES_ROOT = ROOT / "Projetos_Exemplos"
REPORT_PATH = pathlib.Path(__file__).resolve().parent / "reports" / "turning_examples_report.md"


FILE_GLOBS = ("*.c", "*.cc", "*.cpp", "*.cxx", "*.h", "*.hpp")

# Signals of robust turning logic for vehicle physics.
PATTERNS: Dict[str, Tuple[re.Pattern[str], int]] = {
    "ackermann": (re.compile(r"\backermann\b", re.IGNORECASE), 4),
    "steer_sign_comment": (re.compile(r"left\s*-?1.*right|negative.*left|positive.*right", re.IGNORECASE), 4),
    "yaw_rate": (re.compile(r"\byaw[_\s-]*rate\b", re.IGNORECASE), 2),
    "slip_angle": (re.compile(r"\bslip\b|\balpha(front|rear)?\b", re.IGNORECASE), 3),
    "pacejka": (re.compile(r"\bpacejka\b", re.IGNORECASE), 5),
    "steer_speed_limit": (re.compile(r"steer.*maxSpeed|max.?speed.*steer|steer.*speed", re.IGNORECASE), 2),
    "bicycle_hint": (re.compile(r"\bwheelbase\b.*\btan\b|\bbicycle\b", re.IGNORECASE), 3),
}


@dataclass
class Hit:
    path: pathlib.Path
    score: int = 0
    features: List[str] = field(default_factory=list)
    snippets: List[str] = field(default_factory=list)


def iter_code_files(repo: pathlib.Path):
    for glob in FILE_GLOBS:
        yield from repo.rglob(glob)


def short(path: pathlib.Path) -> str:
    return str(path).replace("\\", "/")


def first_matching_lines(text: str, pattern: re.Pattern[str], limit: int = 2) -> List[str]:
    out: List[str] = []
    for line in text.splitlines():
        if pattern.search(line):
            out.append(line.strip())
            if len(out) >= limit:
                break
    return out


def scan_repo(repo: pathlib.Path) -> List[Hit]:
    hits: List[Hit] = []
    for fpath in iter_code_files(repo):
        try:
            text = fpath.read_text(encoding="utf-8", errors="ignore")
        except Exception:
            continue

        local_score = 0
        features: List[str] = []
        snippets: List[str] = []

        for name, (rx, weight) in PATTERNS.items():
            if rx.search(text):
                local_score += weight
                features.append(name)
                snippets.extend(first_matching_lines(text, rx, limit=1))

        # Keep only steering-related files to reduce noise.
        if local_score > 0 and (
            "steer" in fpath.name.lower()
            or "dynamics" in fpath.name.lower()
            or "tire" in fpath.name.lower()
            or "simu" in short(fpath).lower()
        ):
            hits.append(Hit(path=fpath, score=local_score, features=features, snippets=snippets[:3]))

    hits.sort(key=lambda h: h.score, reverse=True)
    return hits


def rank_repos(repo_hits: Dict[str, List[Hit]]) -> List[Tuple[str, int]]:
    ranked: List[Tuple[str, int]] = []
    for repo_name, hits in repo_hits.items():
        # Weight top hits more, avoid huge repos winning only by size.
        top = hits[:8]
        score = sum(h.score for h in top)
        ranked.append((repo_name, score))
    ranked.sort(key=lambda x: x[1], reverse=True)
    return ranked


def main() -> int:
    if not EXAMPLES_ROOT.exists():
        raise SystemExit(f"Examples root not found: {EXAMPLES_ROOT}")

    repos = [p for p in EXAMPLES_ROOT.iterdir() if p.is_dir()]
    repo_hits: Dict[str, List[Hit]] = {}
    for repo in repos:
        repo_hits[repo.name] = scan_repo(repo)

    ranking = rank_repos(repo_hits)
    REPORT_PATH.parent.mkdir(parents=True, exist_ok=True)

    lines: List[str] = []
    lines.append("# Turning Logic Comparison (Local Examples)")
    lines.append("")
    lines.append(f"Scanned root: `{short(EXAMPLES_ROOT)}`")
    lines.append("")
    lines.append("## Repository Ranking")
    lines.append("")
    for idx, (name, score) in enumerate(ranking, start=1):
        lines.append(f"{idx}. **{name}** — score `{score}`")
    lines.append("")

    if ranking:
        best = ranking[0][0]
        lines.append(f"## Recommended Reference")
        lines.append("")
        lines.append(f"Best match: **{best}**")
        lines.append("")
        lines.append("Reason:")
        lines.append("- Has explicit steering sign conventions.")
        lines.append("- Contains Ackermann and yaw/slip related implementation.")
        lines.append("- Better fit for low-speed turn startup symmetry work.")
        lines.append("")

    for repo_name, _ in ranking:
        lines.append(f"## {repo_name}")
        lines.append("")
        hits = repo_hits[repo_name][:12]
        if not hits:
            lines.append("_No relevant files found._")
            lines.append("")
            continue
        for h in hits:
            lines.append(f"- `{short(h.path)}` | score `{h.score}` | features `{', '.join(h.features)}`")
            for snip in h.snippets[:2]:
                lines.append(f"  - `{snip}`")
        lines.append("")

    REPORT_PATH.write_text("\n".join(lines), encoding="utf-8")
    print(f"Report generated: {REPORT_PATH}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

