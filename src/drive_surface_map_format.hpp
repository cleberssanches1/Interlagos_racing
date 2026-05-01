#pragma once

#include <cstddef>
#include <cstdint>

namespace DriveSurfaceMap
{
static constexpr uint32_t kMagicDvm1 = 0x31564D44; // "DVM1"
static constexpr uint16_t kVersion1 = 1;

struct HeaderV1
{
    uint32_t magic = kMagicDvm1;
    uint16_t version = kVersion1;
    uint16_t headerSize = sizeof(HeaderV1);
    uint16_t segmentCountWithTriangles = 0;
    uint16_t maxSegmentId = 0;

    uint32_t familyCount = 0;
    uint32_t triangleCount = 0;

    uint32_t segmentTableOffset = 0;
    uint32_t familyIdsOffset = 0;
    uint32_t trianglesOffset = 0;
    uint32_t segmentEntrySize = 0;
    uint32_t triangleEntrySize = 0;

    int32_t worldMinX = 0;
    int32_t worldMaxX = 0;
    int32_t worldMinZ = 0;
    int32_t worldMaxZ = 0;

    uint32_t reserved0 = 0;
    uint32_t reserved1 = 0;
};

struct SegmentEntryV1
{
    uint32_t firstTriangleIndex = 0;
    uint16_t triangleCount = 0;
    uint16_t reserved = 0;
};

struct TriangleEntryV1
{
    uint16_t segmentId = 0;
    uint16_t faceIndex = 0;
    uint16_t familyId = 0;
    uint16_t reserved = 0;

    int32_t minX = 0;
    int32_t maxX = 0;
    int32_t minZ = 0;
    int32_t maxZ = 0;

    int32_t ax = 0;
    int32_t ay = 0;
    int32_t az = 0;

    int64_t nx = 0;
    int64_t ny = 0;
    int64_t nz = 0;

    int32_t normalX = 0;
    int32_t normalY = 0;
    int32_t normalZ = 0;

    int32_t aux0 = 0;
    int32_t aux1 = 0;
};

inline bool IsRangeValid(size_t blobSize, uint32_t offset, uint32_t bytes)
{
    if (offset > blobSize) return false;
    const uint64_t end = static_cast<uint64_t>(offset) + static_cast<uint64_t>(bytes);
    return end <= static_cast<uint64_t>(blobSize);
}
} // namespace DriveSurfaceMap

