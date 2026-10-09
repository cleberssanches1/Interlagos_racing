#!/usr/bin/env python3
"""
Quick static checker for left/right asymmetry in low-speed turning logic.
"""

from __future__ import annotations

import pathlib
import re
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
TARGET = ROOT / "src" / "car_dynamics_model.hpp"


def main() -> int:
    if not TARGET.exists():
        print(f"missing: {TARGET}")
        return 2

    text = TARGET.read_text(encoding="utf-8", errors="ignore")

    # Heuristics: flag side-specific launch hacks that are not mirrored.
    suspicious = [
        r"launchForwardSteerLeft",
        r"Left launch: invert rear kick direction",
        r"steering\s*<\s*0[^;\n]*\?\s*Fxp::BuildRaw\(-",
    ]

    found = []
    for rx in suspicious:
        if re.search(rx, text, re.IGNORECASE):
            found.append(rx)

    if found:
        print("ASYMMETRY_RISK=1")
        for item in found:
            print(f"hit: {item}")
        return 1

    print("ASYMMETRY_RISK=0")
    return 0


if __name__ == "__main__":
    sys.exit(main())

