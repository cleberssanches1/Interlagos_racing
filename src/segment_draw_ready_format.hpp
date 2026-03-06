#pragma once

#include <cstddef>
#include <cstdint>

namespace SegmentDrawReady
{
// Stable on-disk format for one draw-ready segment blob.
static constexpr uint32_t kMagicSdr1 = 0x31524453; // "SDR1"
static constexpr uint16_t kVersion1 = 1;

// Face kind values stored in the draw-ready face table.
enum class FaceKind : uint8_t
{
    Triangle = 3,
    Quad = 4
};

// Serialized visibility flags for later conversion into SRL attributes.
enum class VisibilityMode : uint16_t
{
    SingleSided = 0,
    DoubleSided = 1
};

// Serialized sort modes for later conversion into SRL attributes.
enum class SortMode : uint16_t
{
    Center = 0
};

// Header for one SDR1 segment file.
struct HeaderV1
{
    uint32_t magic = kMagicSdr1;
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

// One vertex already quantized for direct runtime instancing.
struct Vertex
{
    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;
};

// One polygon already reordered and with a prebuilt face normal.
struct Face
{
    uint16_t v0 = 0;
    uint16_t v1 = 0;
    uint16_t v2 = 0;
    uint16_t v3 = 0;

    int32_t normalX = 0;
    int32_t normalY = 0;
    int32_t normalZ = 0;

    uint8_t kind = static_cast<uint8_t>(FaceKind::Quad);
    uint8_t reservedA = 0;
    uint16_t reservedB = 0;
};

// Stable serialized base attribute values.
struct AttrBase
{
    uint16_t visibility = static_cast<uint16_t>(VisibilityMode::DoubleSided);
    uint16_t sortMode = static_cast<uint16_t>(SortMode::Center);

    uint16_t baseColor = 0;
    uint16_t colorMode = 0;

    uint16_t gouraudMode = 0;
    uint16_t spriteMode = 0;

    uint16_t useLight = 0;
    uint16_t flags = 0;
};

// Return true when an offset stays inside the blob.
inline bool IsRangeValid(size_t blobSize, uint32_t offset, uint32_t bytes)
{
    if (offset > blobSize) return false;
    const uint64_t end = static_cast<uint64_t>(offset) + static_cast<uint64_t>(bytes);
    return end <= static_cast<uint64_t>(blobSize);
}

// Align byte counts to four bytes for stable table offsets.
inline uint32_t Align4(uint32_t value)
{
    return static_cast<uint32_t>((value + 3u) & ~3u);
}

// Validate the fixed header fields before reading tables.
inline bool IsHeaderSane(const HeaderV1& header, size_t blobSize)
{
    if (header.magic != kMagicSdr1) return false;
    if (header.version != kVersion1) return false;
    if (header.headerSize < sizeof(HeaderV1)) return false;
    if (header.vertexCount == 0 || header.faceCount == 0) return false;

    const uint32_t verticesBytes =
        static_cast<uint32_t>(header.vertexCount * sizeof(Vertex));
    const uint32_t facesBytes =
        static_cast<uint32_t>(header.faceCount * sizeof(Face));
    const uint32_t attrsBytes =
        static_cast<uint32_t>(header.faceCount * sizeof(AttrBase));
    const uint32_t familyBytes =
        static_cast<uint32_t>(header.faceCount * sizeof(uint16_t));

    if (!IsRangeValid(blobSize, header.verticesOffset, verticesBytes)) return false;
    if (!IsRangeValid(blobSize, header.facesOffset, facesBytes)) return false;
    if (!IsRangeValid(blobSize, header.attrsOffset, attrsBytes)) return false;
    if (!IsRangeValid(blobSize, header.familyIdsOffset, familyBytes)) return false;

    return true;
}
} // namespace SegmentDrawReady
