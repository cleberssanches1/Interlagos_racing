#pragma once

#include <cstddef>
#include <cstdint>

namespace TrackRuntimePack
{
static constexpr uint32_t kMagicTrk1 = 0x314B5254; // "TRK1"
static constexpr uint16_t kVersion1 = 1;

struct HeaderV1
{
    uint32_t magic = kMagicTrk1;
    uint16_t version = kVersion1;
    uint16_t headerSize = sizeof(HeaderV1);

    uint16_t segmentCount = 0;
    uint16_t maxSegmentId = 0;

    uint32_t directoryOffset = 0;
    uint32_t dataOffset = 0;

    uint32_t maxBlobSize = 0;
    uint32_t maxVertexCount = 0;
    uint32_t maxFaceCount = 0;
    uint32_t maxFamilyCount = 0;

    uint32_t reserved0 = 0;
    uint32_t reserved1 = 0;
};

struct DirectoryEntryV1
{
    uint32_t offset = 0;
    uint32_t size = 0;
    uint32_t vertexCount = 0;
    uint32_t faceCount = 0;
    uint16_t familyCount = 0;
    uint16_t flags = 0;
    uint32_t reserved = 0;
};

inline bool IsRangeValid(size_t blobSize, uint32_t offset, uint32_t bytes)
{
    if (offset > blobSize) return false;
    const uint64_t end = static_cast<uint64_t>(offset) + static_cast<uint64_t>(bytes);
    return end <= static_cast<uint64_t>(blobSize);
}

inline bool IsHeaderSane(const HeaderV1& header, size_t blobSize)
{
    if (header.magic != kMagicTrk1) return false;
    if (header.version != kVersion1) return false;
    if (header.headerSize < sizeof(HeaderV1)) return false;
    if (header.maxSegmentId == 0) return false;

    const uint32_t directoryBytes =
        static_cast<uint32_t>(static_cast<uint32_t>(header.maxSegmentId) *
                              static_cast<uint32_t>(sizeof(DirectoryEntryV1)));

    if (!IsRangeValid(blobSize, header.directoryOffset, directoryBytes)) return false;
    if (header.dataOffset > blobSize) return false;
    if (header.directoryOffset < header.headerSize) return false;
    if (header.dataOffset < header.directoryOffset + directoryBytes) return false;
    return true;
}
} // namespace TrackRuntimePack
