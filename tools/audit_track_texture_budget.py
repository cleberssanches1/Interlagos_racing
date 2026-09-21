#!/usr/bin/env python3
"""Fase 0 metrics: orphans, TBK sizes, unique families in the visible window."""

from __future__ import annotations

import argparse
import json
import struct
from collections import Counter
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional, Set, Tuple


def load_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def read_tga_size(path: Path) -> Optional[Tuple[int, int, int, int, int]]:
    if not path.is_file():
        return None
    data = path.read_bytes()
    if len(data) < 18:
        return None
    return (
        int(data[1]),
        int(data[2]),
        int(data[16]),
        int(struct.unpack_from("<H", data, 12)[0]),
        int(struct.unpack_from("<H", data, 14)[0]),
    )


def main() -> int:
    ap = argparse.ArgumentParser(description="Audit track texture budget / orphans")
    ap.add_argument("--segments-map", required=True)
    ap.add_argument("--package-dir", required=True)
    ap.add_argument("--window", type=int, default=16, help="Visible segment window size")
    ap.add_argument("--fail-on-orphan", action="store_true")
    ap.add_argument("--fail-on-bad-size", action="store_true")
    ap.add_argument(
        "--expected-sizes",
        default="TBKLOD0:64,TBKLOD1:64,TBKLOD2:32",
        help="bankStem:nominalSize pairs",
    )
    args = ap.parse_args()

    package = Path(args.package_dir)
    sm = load_json(Path(args.segments_map))
    families = {int(f["id"]): f for f in (sm.get("textureFamilies") or []) if f and "id" in f}
    segments = sorted(
        [s for s in (sm.get("segments") or []) if s and "id" in s],
        key=lambda s: int(s["id"]),
    )

    used_all: Set[int] = set()
    used_window: Set[int] = set()
    for idx, seg in enumerate(segments):
        arr = seg.get("faceTextureFamily") or []
        for fid in arr:
            if fid is None:
                continue
            fid_i = int(fid)
            if fid_i <= 0:
                continue  # 0 = sem textura / unset
            used_all.add(fid_i)
            if idx < args.window:
                used_window.add(fid_i)

    orphans_used = sorted(fid for fid in used_all if fid not in families)
    orphans_window = sorted(fid for fid in used_window if fid not in families)

    print("=== segments_map ===")
    print(f"families={len(families)} segments={len(segments)}")
    print(f"unique families used (all)={len(used_all)}")
    print(f"unique families used (first {args.window} segs)={len(used_window)}")
    print(f"orphans used (missing family node)={len(orphans_used)} {orphans_used[:20]}")
    print(f"orphans in window={len(orphans_window)} {orphans_window[:20]}")

    expected: Dict[str, int] = {}
    for part in str(args.expected_sizes).split(","):
        part = part.strip()
        if not part:
            continue
        stem, size_s = part.split(":")
        expected[stem.strip()] = int(size_s)

    print("=== TBKLOD indexes ===")
    bad_sizes: List[str] = []
    missing_in_bank: Dict[str, List[int]] = {}
    for stem, nominal in expected.items():
        idx_path = package / f"{stem}.json"
        if not idx_path.is_file():
            print(f"{stem}: INDEX MISSING")
            continue
        idx = load_json(idx_path)
        entries = idx.get("entries") or []
        bank_ids = {int(e["familyId"]) for e in entries if e and "familyId" in e}
        size_hist = Counter((int(e.get("tgaWidth", 0)), int(e.get("tgaHeight", 0))) for e in entries)
        fmt_hist = Counter(
            (int(e.get("tgaImageType", -1)), int(e.get("tgaPixelDepth", -1)), int(e.get("tgaColorMapType", -1)))
            for e in entries
        )
        print(f"{stem}: count={len(entries)} nominal={nominal}")
        print(f"  sizes={size_hist.most_common(8)}")
        print(f"  formats(imageType,depth,cmap)={fmt_hist.most_common(5)}")
        for (w, h), n in size_hist.items():
            if w != nominal or h != nominal:
                bad_sizes.append(f"{stem}:{w}x{h}x{n}")
        missing = sorted(fid for fid in used_window if fid not in bank_ids and fid in families)
        missing_in_bank[stem] = missing
        print(f"  used-in-window missing from bank={len(missing)} {missing[:20]}")

    print("=== texture_sources_manifest (if present) ===")
    man_path = package / "texture_sources_manifest.json"
    if man_path.is_file():
        man = load_json(man_path)
        entries = man.get("entries") or []
        tr = Counter(str(e.get("transform")) for e in entries)
        wh = Counter((int(e.get("width", 0)), int(e.get("height", 0)), str(e.get("sourceGroup"))) for e in entries)
        print(f"entries={len(entries)}")
        print(f"transforms={tr.most_common(10)}")
        print(f"wh/group top={wh.most_common(12)}")
    else:
        print("absent")

    failed = False
    if args.fail_on_orphan and (orphans_used or any(missing_in_bank.values())):
        failed = True
        print("FAIL: orphans detected")
    if args.fail_on_bad_size and bad_sizes:
        failed = True
        print("FAIL: bad sizes", bad_sizes[:20])

    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
