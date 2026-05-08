#pragma once

#include <cstddef>
#include <cstdint>

namespace SegmentRuntimeDraw
{
// Stable on-disk format for one runtime-oriented segment blob.
static constexpr uint32_t kMagicRdr1 = 0x31524452; // "RDR1"
static constexpr uint16_t kVersion1 = 1;

struct HeaderV1
{
    uint32_t magic = kMagicRdr1;
    uint16_t version = kVersion1;
    uint16_t headerSize = sizeof(HeaderV1);

    uint16_t segmentId = 0;
    uint16_t flags = 0;

    uint32_t vertexCount = 0;
    uint32_t faceCount = 0;

    int32_t centerX = 0;
    int32_t centerY = 0;
    int32_t centerZ = 0;

    int32_t boundsMinX = 0;
    int32_t boundsMinY = 0;
    int32_t boundsMinZ = 0;

    int32_t boundsMaxX = 0;
    int32_t boundsMaxY = 0;
    int32_t boundsMaxZ = 0;

    uint32_t verticesOffset = 0;
    uint32_t facesOffset = 0;
    uint32_t attrsOffset = 0;
    uint32_t familyIdsOffset = 0;

    uint32_t reserved0 = 0;
    uint32_t reserved1 = 0;
};

struct Vertex
{
    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;
};

struct Face
{
    uint16_t v0 = 0;
    uint16_t v1 = 0;
    uint16_t v2 = 0;
    uint16_t v3 = 0;

    int32_t normalX = 0;
    int32_t normalY = 0;
    int32_t normalZ = 0;

    uint8_t kind = 4;
    // Carries SegmentDrawReady::SurfaceType.
    uint8_t reservedA = 0;
    // Carries SegmentDrawReady::FaceSurfaceFlags.
    uint16_t reservedB = 0;
};

// Stores the fully expanded runtime attribute fields expected by TrackRenderer.
struct Attr
{
    uint8_t visibility = 0;
    uint8_t sort = 0;
    uint16_t texture = 0;
    uint16_t display = 0;
    uint16_t colorMode = 0;
    uint16_t gouraud = 0;
    uint16_t direction = 0;
};

inline bool IsRangeValid(size_t blobSize, uint32_t offset, uint32_t bytes)
{
    if (offset > blobSize) return false;
    const uint64_t end = static_cast<uint64_t>(offset) + static_cast<uint64_t>(bytes);
    return end <= static_cast<uint64_t>(blobSize);
}

inline bool IsHeaderSane(const HeaderV1& header, size_t blobSize)
{
    if (header.magic != kMagicRdr1) return false;
    if (header.version != kVersion1) return false;
    if (header.headerSize < sizeof(HeaderV1)) return false;
    if (header.vertexCount == 0 || header.faceCount == 0) return false;

    const uint32_t verticesBytes =
        static_cast<uint32_t>(header.vertexCount * sizeof(Vertex));
    const uint32_t facesBytes =
        static_cast<uint32_t>(header.faceCount * sizeof(Face));
    const uint32_t attrsBytes =
        static_cast<uint32_t>(header.faceCount * sizeof(Attr));
    const uint32_t familyBytes =
        static_cast<uint32_t>(header.faceCount * sizeof(uint16_t));

    if (!IsRangeValid(blobSize, header.verticesOffset, verticesBytes)) return false;
    if (!IsRangeValid(blobSize, header.facesOffset, facesBytes)) return false;
    if (!IsRangeValid(blobSize, header.attrsOffset, attrsBytes)) return false;
    if (!IsRangeValid(blobSize, header.familyIdsOffset, familyBytes)) return false;

    return true;
}
} // namespace SegmentRuntimeDraw
