#pragma once

#include <cstddef>
#include <cstdint>

#include "segment_draw_ready_format.hpp"

namespace BatchDrawReady
{
// Stable on-disk format for one draw-ready batch blob.
static constexpr uint32_t kMagicBdr1 = 0x31524442; // "BDR1"
static constexpr uint16_t kVersion1 = 1;

// Header for one BDR1 batch file.
struct HeaderV1
{
    uint32_t magic = kMagicBdr1;
    uint16_t version = kVersion1;
    uint16_t headerSize = sizeof(HeaderV1);

    uint16_t batchId = 0;
    uint16_t flags = 0;

    uint16_t logicalSegmentCount = 0;
    uint16_t reservedA = 0;

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

    uint32_t segmentIdsOffset = 0;
    uint32_t verticesOffset = 0;
    uint32_t facesOffset = 0;
    uint32_t attrsOffset = 0;
    uint32_t familyIdsOffset = 0;
    uint32_t faceRankOffsetsOffset = 0;

    uint32_t reserved0 = 0;
    uint32_t reserved1 = 0;
};

inline bool IsRangeValid(size_t blobSize, uint32_t offset, uint32_t bytes)
{
    if (offset > blobSize) return false;
    const uint64_t end = static_cast<uint64_t>(offset) + static_cast<uint64_t>(bytes);
    return end <= static_cast<uint64_t>(blobSize);
}

inline uint32_t Align4(uint32_t value)
{
    return static_cast<uint32_t>((value + 3u) & ~3u);
}

inline bool IsHeaderSane(const HeaderV1& header, size_t blobSize)
{
    if (header.magic != kMagicBdr1) return false;
    if (header.version != kVersion1) return false;
    if (header.headerSize < sizeof(HeaderV1)) return false;
    if (header.logicalSegmentCount == 0) return false;
    if (header.vertexCount == 0 || header.faceCount == 0) return false;

    const uint32_t segmentIdsBytes =
        static_cast<uint32_t>(header.logicalSegmentCount * sizeof(uint16_t));
    const uint32_t verticesBytes =
        static_cast<uint32_t>(header.vertexCount * sizeof(SegmentDrawReady::Vertex));
    const uint32_t facesBytes =
        static_cast<uint32_t>(header.faceCount * sizeof(SegmentDrawReady::Face));
    const uint32_t attrsBytes =
        static_cast<uint32_t>(header.faceCount * sizeof(SegmentDrawReady::AttrBase));
    const uint32_t familyBytes =
        static_cast<uint32_t>(header.faceCount * sizeof(uint16_t));
    const uint32_t rankBytes =
        static_cast<uint32_t>(header.faceCount * sizeof(uint8_t));

    if (!IsRangeValid(blobSize, header.segmentIdsOffset, segmentIdsBytes)) return false;
    if (!IsRangeValid(blobSize, header.verticesOffset, verticesBytes)) return false;
    if (!IsRangeValid(blobSize, header.facesOffset, facesBytes)) return false;
    if (!IsRangeValid(blobSize, header.attrsOffset, attrsBytes)) return false;
    if (!IsRangeValid(blobSize, header.familyIdsOffset, familyBytes)) return false;
    if (!IsRangeValid(blobSize, header.faceRankOffsetsOffset, rankBytes)) return false;

    return true;
}
} // namespace BatchDrawReady
