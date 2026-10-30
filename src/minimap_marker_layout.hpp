#pragma once

#include <cstdint>

// Pure layout rules for the 128x128 VDP2 minimap. A shifted 16x16 marker can
// touch three cells in each axis; the last cell must remain inside the art.
namespace MinimapMarkerLayout
{
struct Pose
{
    uint8_t pixelX = 0u;
    uint8_t pixelY = 0u;
    uint8_t direction = 0u;
    bool visible = false;
};

inline uint8_t CellX(const Pose& pose)
{
    const uint8_t cell = static_cast<uint8_t>(pose.pixelX >> 3u);
    return cell > 13u ? 13u : cell;
}

inline uint8_t CellY(const Pose& pose)
{
    const uint8_t cell = static_cast<uint8_t>(pose.pixelY >> 3u);
    return cell > 13u ? 13u : cell;
}

inline bool CoversTile(const Pose& pose, uint8_t tileX, uint8_t tileY)
{
    if (!pose.visible) return false;
    const uint8_t x = CellX(pose);
    const uint8_t y = CellY(pose);
    return tileX >= x && tileX < x + 3u &&
           tileY >= y && tileY < y + 3u;
}

inline uint8_t OwnerForTile(const Pose* poses, uint8_t count,
                            uint8_t tileX, uint8_t tileY)
{
    for (uint8_t i = 0u; i < count; ++i)
        if (CoversTile(poses[i], tileX, tileY)) return i;
    return 0xffu;
}
} // namespace MinimapMarkerLayout
