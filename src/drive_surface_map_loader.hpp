#pragma once

#include <cstddef>
#include <cstdint>

#include "drive_surface_map_format.hpp"

namespace DriveSurfaceMap
{
class Loader
{
public:
    struct View
    {
        bool valid = false;
        HeaderV1 header{};
        const uint8_t* data = nullptr;
        size_t blobSize = 0;
        size_t segmentTableOffset = 0;
        size_t familyIdsOffset = 0;
        size_t trianglesOffset = 0;
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

    static int64_t ReadLeI64(const uint8_t* p)
    {
        const uint64_t lo = static_cast<uint64_t>(ReadLe32(p + 0));
        const uint64_t hi = static_cast<uint64_t>(ReadLe32(p + 4));
        return static_cast<int64_t>((hi << 32) | lo);
    }

    static bool ReadHeaderLeAt(const uint8_t* data, size_t size, size_t offset, HeaderV1& out)
    {
        if (!data) return false;
        if (offset + 64 > size) return false;
        const uint8_t* p = data + offset;
        out.magic = ReadLe32(p + 0);
        out.version = ReadLe16(p + 4);
        out.headerSize = ReadLe16(p + 6);
        out.segmentCountWithTriangles = ReadLe16(p + 8);
        out.maxSegmentId = ReadLe16(p + 10);
        out.familyCount = ReadLe32(p + 12);
        out.triangleCount = ReadLe32(p + 16);
        out.segmentTableOffset = ReadLe32(p + 20);
        out.familyIdsOffset = ReadLe32(p + 24);
        out.trianglesOffset = ReadLe32(p + 28);
        out.segmentEntrySize = ReadLe32(p + 32);
        out.triangleEntrySize = ReadLe32(p + 36);
        out.worldMinX = ReadLeI32(p + 40);
        out.worldMaxX = ReadLeI32(p + 44);
        out.worldMinZ = ReadLeI32(p + 48);
        out.worldMaxZ = ReadLeI32(p + 52);
        out.reserved0 = ReadLe32(p + 56);
        out.reserved1 = ReadLe32(p + 60);
        return true;
    }

    template <typename ByteVec>
    static bool ReadHeaderLeAt(const ByteVec& bytes, size_t offset, HeaderV1& out)
    {
        return ReadHeaderLeAt(bytes.data(), bytes.size(), offset, out);
    }

    static bool IsHeaderSane(const HeaderV1& header, size_t blobSize)
    {
        if (header.magic != kMagicDvm1) return false;
        if (header.version != kVersion1) return false;
        if (header.headerSize < 64) return false;
        if (header.maxSegmentId == 0) return false;
        if (header.segmentEntrySize != 8) return false;
        if (header.triangleEntrySize != 80) return false;

        const uint32_t segmentTableBytes =
            static_cast<uint32_t>(static_cast<uint32_t>(header.maxSegmentId) * header.segmentEntrySize);
        const uint32_t familyIdsBytes = static_cast<uint32_t>(header.familyCount * sizeof(uint16_t));
        const uint32_t trianglesBytes = static_cast<uint32_t>(header.triangleCount * header.triangleEntrySize);

        if (!IsRangeValid(blobSize, header.segmentTableOffset, segmentTableBytes)) return false;
        if (!IsRangeValid(blobSize, header.familyIdsOffset, familyIdsBytes)) return false;
        if (!IsRangeValid(blobSize, header.trianglesOffset, trianglesBytes)) return false;
        return true;
    }

    static bool Parse(const uint8_t* data, size_t size, View& out)
    {
        out = {};
        if (!data || size == 0) return false;
        if (!ReadHeaderLeAt(data, size, 0, out.header)) return false;
        if (!IsHeaderSane(out.header, size)) return false;

        out.data = data;
        out.blobSize = size;
        out.segmentTableOffset = static_cast<size_t>(out.header.segmentTableOffset);
        out.familyIdsOffset = static_cast<size_t>(out.header.familyIdsOffset);
        out.trianglesOffset = static_cast<size_t>(out.header.trianglesOffset);
        out.valid = true;
        return true;
    }

    template <typename ByteVec>
    static bool Parse(const ByteVec& bytes, View& out)
    {
        if (bytes.empty()) return false;
        return Parse(bytes.data(), bytes.size(), out);
    }

    static bool ReadSegmentEntryById(const View& view, int32_t segmentId, SegmentEntryV1& out)
    {
        out = {};
        if (!view.valid || !view.data) return false;
        if (segmentId <= 0) return false;
        if (segmentId > static_cast<int32_t>(view.header.maxSegmentId)) return false;

        const size_t off = view.segmentTableOffset +
            static_cast<size_t>(segmentId - 1) * static_cast<size_t>(view.header.segmentEntrySize);
        if (off + 8 > view.blobSize) return false;
        const uint8_t* p = view.data + off;
        out.firstTriangleIndex = ReadLe32(p + 0);
        out.triangleCount = ReadLe16(p + 4);
        out.reserved = ReadLe16(p + 6);
        return true;
    }

    static bool ReadTriangleByIndex(const View& view, uint32_t triangleIndex, TriangleEntryV1& out)
    {
        out = {};
        if (!view.valid || !view.data) return false;
        if (triangleIndex >= view.header.triangleCount) return false;
        const size_t off = view.trianglesOffset +
            static_cast<size_t>(triangleIndex) * static_cast<size_t>(view.header.triangleEntrySize);
        if (off + 80 > view.blobSize) return false;
        const uint8_t* p = view.data + off;

        out.segmentId = ReadLe16(p + 0);
        out.faceIndex = ReadLe16(p + 2);
        out.familyId = ReadLe16(p + 4);
        out.reserved = ReadLe16(p + 6);
        out.minX = ReadLeI32(p + 8);
        out.maxX = ReadLeI32(p + 12);
        out.minZ = ReadLeI32(p + 16);
        out.maxZ = ReadLeI32(p + 20);
        out.ax = ReadLeI32(p + 24);
        out.ay = ReadLeI32(p + 28);
        out.az = ReadLeI32(p + 32);
        out.nx = ReadLeI64(p + 36);
        out.ny = ReadLeI64(p + 44);
        out.nz = ReadLeI64(p + 52);
        out.normalX = ReadLeI32(p + 60);
        out.normalY = ReadLeI32(p + 64);
        out.normalZ = ReadLeI32(p + 68);
        out.aux0 = ReadLeI32(p + 72);
        out.aux1 = ReadLeI32(p + 76);
        return true;
    }
};
} // namespace DriveSurfaceMap

