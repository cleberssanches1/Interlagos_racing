#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "segment_runtime_draw_format.hpp"
#include "track_zone_alloc.hpp"

namespace SegmentRuntimeDraw
{
struct Blob
{
    TrackLowWorkVectorBase<uint8_t> bytes{};
    bool loaded = false;
    size_t size = 0;
};

struct MappedBlob
{
    const uint8_t* data = nullptr;
    size_t size = 0;
    bool loaded = false;
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
        const uint8_t* data = nullptr;
        size_t blobSize = 0;
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

    static bool ReadHeaderLeAt(const uint8_t* data, size_t size, size_t offset, HeaderV1& out)
    {
        if (!data) return false;
        if (offset + sizeof(HeaderV1) > size) return false;
        const uint8_t* p = data + offset;

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
    static bool ReadHeaderLeAt(const ByteVec& bytes, size_t offset, HeaderV1& out)
    {
        return ReadHeaderLeAt(bytes.data(), bytes.size(), offset, out);
    }

    static bool ReadVertexLeAt(const uint8_t* data, size_t size, size_t offset, Vertex& out)
    {
        if (!data) return false;
        if (offset + sizeof(Vertex) > size) return false;
        const uint8_t* p = data + offset;
        out.x = ReadLeI32(p + 0);
        out.y = ReadLeI32(p + 4);
        out.z = ReadLeI32(p + 8);
        return true;
    }

    template <typename ByteVec>
    static bool ReadVertexLeAt(const ByteVec& bytes, size_t offset, Vertex& out)
    {
        return ReadVertexLeAt(bytes.data(), bytes.size(), offset, out);
    }

    static bool ReadFaceLeAt(const uint8_t* data, size_t size, size_t offset, Face& out)
    {
        if (!data) return false;
        if (offset + sizeof(Face) > size) return false;
        const uint8_t* p = data + offset;
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
    static bool ReadFaceLeAt(const ByteVec& bytes, size_t offset, Face& out)
    {
        return ReadFaceLeAt(bytes.data(), bytes.size(), offset, out);
    }

    static bool ReadAttrLeAt(const uint8_t* data, size_t size, size_t offset, Attr& out)
    {
        if (!data) return false;
        if (offset + sizeof(Attr) > size) return false;
        const uint8_t* p = data + offset;
        out.visibility = p[0];
        out.sort = p[1];
        out.texture = ReadLe16(p + 2);
        out.display = ReadLe16(p + 4);
        out.colorMode = ReadLe16(p + 6);
        out.gouraud = ReadLe16(p + 8);
        out.direction = ReadLe16(p + 10);
        return true;
    }

    template <typename ByteVec>
    static bool ReadAttrLeAt(const ByteVec& bytes, size_t offset, Attr& out)
    {
        return ReadAttrLeAt(bytes.data(), bytes.size(), offset, out);
    }

    static bool ReadFamilyIdLeAt(const uint8_t* data, size_t size, size_t offset, uint16_t& out)
    {
        if (!data) return false;
        if (offset + sizeof(uint16_t) > size) return false;
        out = ReadLe16(data + offset);
        return true;
    }

    template <typename ByteVec>
    static bool ReadFamilyIdLeAt(const ByteVec& bytes, size_t offset, uint16_t& out)
    {
        return ReadFamilyIdLeAt(bytes.data(), bytes.size(), offset, out);
    }

    static bool Parse(const uint8_t* data, size_t size, View& out)
    {
        out = {};
        if (!data || size == 0) return false;
        if (!ReadHeaderLeAt(data, size, 0, out.header)) return false;
        if (!IsHeaderSane(out.header, size)) return false;

        out.verticesOffset = static_cast<size_t>(out.header.verticesOffset);
        out.facesOffset = static_cast<size_t>(out.header.facesOffset);
        out.attrsOffset = static_cast<size_t>(out.header.attrsOffset);
        out.familyIdsOffset = static_cast<size_t>(out.header.familyIdsOffset);
        out.data = data;
        out.blobSize = size;
        out.valid = true;
        return true;
    }

    static bool Parse(const Blob& blob, View& out)
    {
        if (!blob.loaded || blob.bytes.empty()) return false;
        return Parse(blob.bytes.data(), blob.bytes.size(), out);
    }

    static bool Parse(const MappedBlob& blob, View& out)
    {
        if (!blob.loaded || !blob.data || blob.size == 0) return false;
        return Parse(blob.data, blob.size, out);
    }
};
} // namespace SegmentRuntimeDraw
