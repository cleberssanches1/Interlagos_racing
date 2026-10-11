"""Small geometry guards for the TCOL racing-line filter."""
from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from build_track_collision import WallRecord, wall_is_protected_longitudinal  # noqa: E402


class ProtectedLongitudinalWallTests(unittest.TestCase):
    def test_only_long_same_side_physical_walls_are_protected(self) -> None:
        unit = 1 << 16
        family_id = 32
        params = (0, 5 * unit, 0, unit, family_id, 1)
        long_wall = WallRecord(16 * unit, 0, 16 * unit, 128 * unit, *params)
        transverse = WallRecord(-16 * unit, 0, 16 * unit, 0, *params)
        short_wall = WallRecord(16 * unit, 0, 16 * unit, 16 * unit, *params)
        slanted = WallRecord(16 * unit, 0, 32 * unit, 128 * unit, *params)
        center = (0.0, 64.0 * unit)
        travel = (0.0, 128.0 * unit)

        self.assertTrue(wall_is_protected_longitudinal(long_wall, center, travel, {family_id}))
        for wall in (transverse, short_wall, slanted):
            self.assertFalse(wall_is_protected_longitudinal(wall, center, travel, {family_id}))
        self.assertFalse(wall_is_protected_longitudinal(long_wall, center, travel, set()))


if __name__ == "__main__":
    unittest.main()
