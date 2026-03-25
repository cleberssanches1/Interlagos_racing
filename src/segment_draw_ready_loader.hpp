#pragma once

#include <cstddef>
#include <cstdint>
#include <string.h>
#include <vector>

#include "segment_draw_ready_format.hpp"
#include "track_zone_alloc.hpp"

namespace SegmentDrawReady
{
struct Blob
{
    TrackLowWorkVectorBase<uint8_t> bytes{};
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
        size_t verticesOffset = 0;
        size_t facesOffset = 0;
        size_t attrsOffset = 0;
        size_t familyIdsOffset = 0;
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

    template <typename ByteVec>
    static bool ReadHeaderLeAt(const ByteVec& bytes, size_t offset, HeaderV1& out)
    {
        if (offset + sizeof(HeaderV1) > bytes.size()) return false;
        const uint8_t* p = bytes.data() + offset;

        out.magic = ReadLe32(p + 0);
        out.version = ReadLe16(p + 4);
        out.headerSize = ReadLe16(p + 6);
        out.segmentId = ReadLe16(p + 8);
        out.flags = ReadLe16(p + 10);
        out.vertexCount = ReadLe32(p + 12);
        out.faceCount = ReadLe32(p + 16);
        out.centerX = ReadLeI32(p + 20);
        out.centerY = ReadLeI32(p + 24);
        out.centerZ = ReadLeI32(p + 28);
        out.boundsMinX = ReadLeI32(p + 32);
        out.boundsMinY = ReadLeI32(p + 36);
        out.boundsMinZ = ReadLeI32(p + 40);
        out.boundsMaxX = ReadLeI32(p + 44);
        out.boundsMaxY = ReadLeI32(p + 48);
        out.boundsMaxZ = ReadLeI32(p + 52);
        out.verticesOffset = ReadLe32(p + 56);
        out.facesOffset = ReadLe32(p + 60);
        out.attrsOffset = ReadLe32(p + 64);
        out.familyIdsOffset = ReadLe32(p + 68);
        out.reserved0 = ReadLe32(p + 72);
        out.reserved1 = ReadLe32(p + 76);
        return true;
    }

    template <typename ByteVec>
    static bool ReadVertexLeAt(const ByteVec& bytes, size_t offset, Vertex& out)
    {
        if (offset + sizeof(Vertex) > bytes.size()) return false;
        const uint8_t* p = bytes.data() + offset;
        out.x = ReadLeI32(p + 0);
        out.y = ReadLeI32(p + 4);
        out.z = ReadLeI32(p + 8);
        return true;
    }

    template <typename ByteVec>
    static bool ReadFaceLeAt(const ByteVec& bytes, size_t offset, Face& out)
    {
        if (offset + sizeof(Face) > bytes.size()) return false;
        const uint8_t* p = bytes.data() + offset;
        out.v0 = ReadLe16(p + 0);
        out.v1 = ReadLe16(p + 2);
        out.v2 = ReadLe16(p + 4);
        out.v3 = ReadLe16(p + 6);
        out.normalX = ReadLeI32(p + 8);
        out.normalY = ReadLeI32(p + 12);
        out.normalZ = ReadLeI32(p + 16);
        out.kind = p[20];
        out.reservedA = p[21];
        out.reservedB = ReadLe16(p + 22);
        return true;
    }

    template <typename ByteVec>
    static bool ReadAttrBaseLeAt(const ByteVec& bytes, size_t offset, AttrBase& out)
    {
        if (offset + sizeof(AttrBase) > bytes.size()) return false;
        const uint8_t* p = bytes.data() + offset;
        out.visibility = ReadLe16(p + 0);
        out.sortMode = ReadLe16(p + 2);
        out.baseColor = ReadLe16(p + 4);
        out.colorMode = ReadLe16(p + 6);
        out.gouraudMode = ReadLe16(p + 8);
        out.spriteMode = ReadLe16(p + 10);
        out.useLight = ReadLe16(p + 12);
        out.flags = ReadLe16(p + 14);
        return true;
    }

    template <typename ByteVec>
    static bool ReadFamilyIdLeAt(const ByteVec& bytes, size_t offset, uint16_t& out)
    {
        if (offset + sizeof(uint16_t) > bytes.size()) return false;
        out = ReadLe16(bytes.data() + offset);
        return true;
    }

    template <typename T, typename ByteVec>
    static bool ReadPodAt(const ByteVec& bytes, size_t offset, T& out)
    {
        if (offset + sizeof(T) > bytes.size()) return false;
        ::memcpy(&out, bytes.data() + offset, sizeof(T));
        return true;
    }

    static bool Parse(const Blob& blob, View& out)
    {
        out = {};
        if (!blob.loaded || blob.bytes.empty()) return false;
        if (!ReadHeaderLeAt(blob.bytes, 0, out.header)) return false;
        if (!IsHeaderSane(out.header, blob.bytes.size())) return false;

        out.verticesOffset = static_cast<size_t>(out.header.verticesOffset);
        out.facesOffset = static_cast<size_t>(out.header.facesOffset);
        out.attrsOffset = static_cast<size_t>(out.header.attrsOffset);
        out.familyIdsOffset = static_cast<size_t>(out.header.familyIdsOffset);
        out.valid = true;
        return true;
    }
};
} // namespace SegmentDrawReady
