#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "batch_draw_ready_format.hpp"

namespace BatchDrawReady
{
struct Blob
{
    std::vector<uint8_t> bytes{};
    bool loaded = false;
    size_t size = 0;
};

class Loader
{
public:
    struct View
    {
        bool valid = false;
        HeaderV1 header{};
        size_t segmentIdsOffset = 0;
        size_t verticesOffset = 0;
        size_t facesOffset = 0;
        size_t attrsOffset = 0;
        size_t familyIdsOffset = 0;
        size_t faceRankOffsetsOffset = 0;
    };

    static uint16_t ReadLe16(const uint8_t* p)
    {
        return static_cast<uint16_t>(static_cast<uint16_t>(p[0]) |
                                     (static_cast<uint16_t>(p[1]) << 8));
    }

    static uint32_t ReadLe32(const uint8_t* p)
    {
        return static_cast<uint32_t>(static_cast<uint32_t>(p[0]) |
                                     (static_cast<uint32_t>(p[1]) << 8) |
                                     (static_cast<uint32_t>(p[2]) << 16) |
                                     (static_cast<uint32_t>(p[3]) << 24));
    }

    static int32_t ReadLeI32(const uint8_t* p)
    {
        return static_cast<int32_t>(ReadLe32(p));
    }

    static bool ReadHeaderLeAt(const std::vector<uint8_t>& bytes, size_t offset, HeaderV1& out)
    {
        if (offset + sizeof(HeaderV1) > bytes.size()) return false;
        const uint8_t* p = bytes.data() + offset;

        out.magic = ReadLe32(p + 0);
        out.version = ReadLe16(p + 4);
        out.headerSize = ReadLe16(p + 6);
        out.batchId = ReadLe16(p + 8);
        out.flags = ReadLe16(p + 10);
        out.logicalSegmentCount = ReadLe16(p + 12);
        out.reservedA = ReadLe16(p + 14);
        out.vertexCount = ReadLe32(p + 16);
        out.faceCount = ReadLe32(p + 20);
        out.centerX = ReadLeI32(p + 24);
        out.centerY = ReadLeI32(p + 28);
        out.centerZ = ReadLeI32(p + 32);
        out.boundsMinX = ReadLeI32(p + 36);
        out.boundsMinY = ReadLeI32(p + 40);
        out.boundsMinZ = ReadLeI32(p + 44);
        out.boundsMaxX = ReadLeI32(p + 48);
        out.boundsMaxY = ReadLeI32(p + 52);
        out.boundsMaxZ = ReadLeI32(p + 56);
        out.segmentIdsOffset = ReadLe32(p + 60);
        out.verticesOffset = ReadLe32(p + 64);
        out.facesOffset = ReadLe32(p + 68);
        out.attrsOffset = ReadLe32(p + 72);
        out.familyIdsOffset = ReadLe32(p + 76);
        out.faceRankOffsetsOffset = ReadLe32(p + 80);
        out.reserved0 = ReadLe32(p + 84);
        out.reserved1 = ReadLe32(p + 88);
        return true;
    }

    static bool Parse(const Blob& blob, View& out)
    {
        out = {};
        if (!blob.loaded || blob.bytes.empty()) return false;
        if (!ReadHeaderLeAt(blob.bytes, 0, out.header)) return false;
        if (!IsHeaderSane(out.header, blob.bytes.size())) return false;

        out.segmentIdsOffset = static_cast<size_t>(out.header.segmentIdsOffset);
        out.verticesOffset = static_cast<size_t>(out.header.verticesOffset);
        out.facesOffset = static_cast<size_t>(out.header.facesOffset);
        out.attrsOffset = static_cast<size_t>(out.header.attrsOffset);
        out.familyIdsOffset = static_cast<size_t>(out.header.familyIdsOffset);
        out.faceRankOffsetsOffset = static_cast<size_t>(out.header.faceRankOffsetsOffset);
        out.valid = true;
        return true;
    }
};
} // namespace BatchDrawReady
