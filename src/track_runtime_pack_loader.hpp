#pragma once

#include <cstddef>
#include <cstdint>

#include "track_runtime_pack_format.hpp"

namespace TrackRuntimePack
{
class Loader
{
public:
    struct View
    {
        bool valid = false;
        HeaderV1 header{};
        const uint8_t* data = nullptr;
        size_t size = 0;
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

    static bool ReadHeaderLeAt(const uint8_t* data, size_t size, size_t offset, HeaderV1& out)
    {
        if (!data) return false;
        if (offset + sizeof(HeaderV1) > size) return false;
        const uint8_t* p = data + offset;
        out.magic = ReadLe32(p + 0);
        out.version = ReadLe16(p + 4);
        out.headerSize = ReadLe16(p + 6);
        out.segmentCount = ReadLe16(p + 8);
        out.maxSegmentId = ReadLe16(p + 10);
        out.directoryOffset = ReadLe32(p + 12);
        out.dataOffset = ReadLe32(p + 16);
        out.maxBlobSize = ReadLe32(p + 20);
        out.maxVertexCount = ReadLe32(p + 24);
        out.maxFaceCount = ReadLe32(p + 28);
        out.maxFamilyCount = ReadLe32(p + 32);
        out.reserved0 = ReadLe32(p + 36);
        out.reserved1 = ReadLe32(p + 40);
        return true;
    }

    static bool ReadEntryLeAt(const View& view, int segmentId, DirectoryEntryV1& out)
    {
        if (!view.valid || !view.data) return false;
        if (segmentId <= 0) return false;
        if (segmentId > static_cast<int>(view.header.maxSegmentId)) return false;

        const size_t offset =
            static_cast<size_t>(view.header.directoryOffset) +
            static_cast<size_t>(segmentId - 1) * sizeof(DirectoryEntryV1);
        if (offset + sizeof(DirectoryEntryV1) > view.size) return false;

        const uint8_t* p = view.data + offset;
        out.offset = ReadLe32(p + 0);
        out.size = ReadLe32(p + 4);
        out.vertexCount = ReadLe32(p + 8);
        out.faceCount = ReadLe32(p + 12);
        out.familyCount = ReadLe16(p + 16);
        out.flags = ReadLe16(p + 18);
        out.reserved = ReadLe32(p + 20);
        return true;
    }

    static bool ResolveEntryRange(const View& view,
                                  int segmentId,
                                  const uint8_t*& outData,
                                  size_t& outSize,
                                  DirectoryEntryV1* outEntry = nullptr)
    {
        outData = nullptr;
        outSize = 0;

        DirectoryEntryV1 entry{};
        if (!ReadEntryLeAt(view, segmentId, entry)) return false;
        if (entry.size == 0 || entry.offset == 0) return false;
        if (!IsRangeValid(view.size, entry.offset, entry.size)) return false;
        if (entry.offset < view.header.dataOffset) return false;

        outData = view.data + entry.offset;
        outSize = static_cast<size_t>(entry.size);
        if (outEntry) *outEntry = entry;
        return true;
    }

    static bool Parse(const void* data, size_t size, View& out)
    {
        out = {};
        if (!data || size < sizeof(HeaderV1)) return false;

        out.data = static_cast<const uint8_t*>(data);
        out.size = size;
        if (!ReadHeaderLeAt(out.data, out.size, 0, out.header)) return false;
        if (!IsHeaderSane(out.header, out.size)) return false;

        out.valid = true;
        return true;
    }
};
} // namespace TrackRuntimePack
