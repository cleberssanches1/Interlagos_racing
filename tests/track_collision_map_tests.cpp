#include "track_collision_map.hpp"

#include <cassert>
#include <cstdint>
#include <vector>

namespace
{
void Put16(std::vector<uint8_t>& blob, size_t offset, uint16_t value)
{
    blob[offset] = static_cast<uint8_t>(value & 0xFFu);
    blob[offset + 1u] = static_cast<uint8_t>((value >> 8u) & 0xFFu);
}

void Put32(std::vector<uint8_t>& blob, size_t offset, uint32_t value)
{
    for (size_t i = 0u; i < 4u; ++i)
    {
        blob[offset + i] = static_cast<uint8_t>((value >> (i * 8u)) & 0xFFu);
    }
}

void PutI32(std::vector<uint8_t>& blob, size_t offset, int32_t value)
{
    Put32(blob, offset, static_cast<uint32_t>(value));
}
}

int main()
{
    constexpr size_t directoryOffset = TrackCollisionMap::kHeaderSize;
    constexpr size_t vertexOffset = directoryOffset + TrackCollisionMap::kDirectorySize;
    constexpr size_t groundOffset = vertexOffset + 3u * TrackCollisionMap::kVertexSize;
    constexpr size_t wallOffset = groundOffset + TrackCollisionMap::kGroundSize;
    constexpr size_t groundCellsOffset = wallOffset + TrackCollisionMap::kWallSize;
    constexpr size_t groundIndicesOffset = groundCellsOffset + 16u * TrackCollisionMap::kCellSize;
    constexpr size_t wallCellsOffset = groundIndicesOffset + 2u;
    constexpr size_t wallIndicesOffset = wallCellsOffset + 16u * TrackCollisionMap::kCellSize;
    std::vector<uint8_t> blob(wallIndicesOffset + 2u, 0u);

    Put32(blob, 0u, TrackCollisionMap::kMagic);
    Put16(blob, 4u, TrackCollisionMap::kVersion);
    Put16(blob, 6u, TrackCollisionMap::kHeaderSize);
    Put16(blob, 8u, TrackCollisionMap::kDirectorySize);
    Put16(blob, 10u, TrackCollisionMap::kVertexSize);
    Put16(blob, 12u, TrackCollisionMap::kGroundSize);
    Put16(blob, 14u, TrackCollisionMap::kWallSize);
    Put16(blob, 16u, TrackCollisionMap::kCellSize);
    Put16(blob, 18u, TrackCollisionMap::kGridDim);
    Put16(blob, 20u, 1u);
    Put32(blob, 22u, 1u);
    Put32(blob, 26u, 1u);
    Put32(blob, 30u, static_cast<uint32_t>(directoryOffset));

    Put16(blob, directoryOffset + 0u, 7u);
    Put16(blob, directoryOffset + 2u, 3u);
    Put16(blob, directoryOffset + 4u, 1u);
    Put16(blob, directoryOffset + 6u, 1u);
    Put16(blob, directoryOffset + 8u, TrackCollisionMap::kGridDim);
    PutI32(blob, directoryOffset + 12u, 0);
    PutI32(blob, directoryOffset + 16u, 65536);
    PutI32(blob, directoryOffset + 20u, 0);
    PutI32(blob, directoryOffset + 24u, 65536);
    Put32(blob, directoryOffset + 28u, static_cast<uint32_t>(vertexOffset));
    Put32(blob, directoryOffset + 32u, static_cast<uint32_t>(groundOffset));
    Put32(blob, directoryOffset + 36u, static_cast<uint32_t>(wallOffset));
    Put32(blob, directoryOffset + 40u, static_cast<uint32_t>(groundCellsOffset));
    Put32(blob, directoryOffset + 44u, static_cast<uint32_t>(groundIndicesOffset));
    Put32(blob, directoryOffset + 48u, static_cast<uint32_t>(wallCellsOffset));
    Put32(blob, directoryOffset + 52u, static_cast<uint32_t>(wallIndicesOffset));

    PutI32(blob, vertexOffset + 0u, 0);
    PutI32(blob, vertexOffset + 4u, 1000);
    PutI32(blob, vertexOffset + 8u, 0);
    PutI32(blob, vertexOffset + 12u, 65536);
    PutI32(blob, vertexOffset + 16u, 1000);
    PutI32(blob, vertexOffset + 20u, 0);
    PutI32(blob, vertexOffset + 24u, 0);
    PutI32(blob, vertexOffset + 28u, 1000);
    PutI32(blob, vertexOffset + 32u, 65536);

    Put16(blob, groundOffset + 0u, 0u);
    Put16(blob, groundOffset + 2u, 1u);
    Put16(blob, groundOffset + 4u, 2u);
    Put16(blob, groundOffset + 6u, 2u);
    Put16(blob, groundOffset + 8u, 42u);
    blob[groundOffset + 10u] = 1u;
    blob[groundOffset + 11u] = 3u;
    Put16(blob, groundOffset + 12u, 3u);
    Put16(blob, groundOffset + 14u, 9u);

    PutI32(blob, wallOffset + 0u, 0);
    PutI32(blob, wallOffset + 4u, 0);
    PutI32(blob, wallOffset + 8u, 65536);
    PutI32(blob, wallOffset + 12u, 0);
    PutI32(blob, wallOffset + 16u, -65536);
    PutI32(blob, wallOffset + 20u, 65536);
    PutI32(blob, wallOffset + 24u, 0);
    PutI32(blob, wallOffset + 28u, 65536);
    Put16(blob, wallOffset + 32u, 77u);
    Put16(blob, wallOffset + 34u, 10u);

    Put32(blob, groundCellsOffset, static_cast<uint32_t>(groundIndicesOffset));
    Put16(blob, groundCellsOffset + 4u, 1u);
    Put16(blob, groundIndicesOffset, 0u);
    Put32(blob, wallCellsOffset, static_cast<uint32_t>(wallIndicesOffset));
    Put16(blob, wallCellsOffset + 4u, 1u);
    Put16(blob, wallIndicesOffset, 0u);

    TrackCollisionMap::View view(blob.data(), blob.size());
    assert(view.Valid());
    TrackCollisionMap::SegmentView segment{};
    assert(view.FindSegment(7u, segment));
    assert(segment.vertexCount == 3u && segment.groundCount == 1u && segment.wallCount == 1u);
    TrackCollisionMap::GroundFace ground{};
    assert(view.ReadGround(segment, 0u, ground));
    assert(ground.familyId == 42u && ground.vertexCount == 3u && ground.sourceFaceIndex == 9u);
    TrackCollisionMap::Vertex vertex{};
    assert(view.ReadVertex(segment, ground.vertices[1], vertex));
    assert(vertex.xRaw == 65536 && vertex.yRaw == 1000);
    TrackCollisionMap::Wall wall{};
    assert(view.ReadWall(segment, 0u, wall));
    assert(wall.familyId == 77u && wall.nzRaw == 65536);
    TrackCollisionMap::CellView cell{};
    assert(view.FindGroundCell(segment, 0, 0, cell));
    uint16_t index = 99u;
    assert(view.ReadCellIndex(cell, 0u, index) && index == 0u);
    assert(!view.FindGroundCell(segment, -1, 0, cell));
    return 0;
}
