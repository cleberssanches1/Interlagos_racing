#pragma once

#include <cstddef>
#include <cstdint>
#include <string.h>
#include <vector>

#define DOXYGEN 1
#include <srl.hpp>
#undef DOXYGEN

#include "segment_component_format.hpp"
#include "track_zone_alloc.hpp"

namespace SegmentComponent
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

    static int16_t ReadLeI16(const uint8_t* p)
    {
        return static_cast<int16_t>(ReadLe16(p));
    }

    static int32_t ReadLeI32(const uint8_t* p)
    {
        return static_cast<int32_t>(ReadLe32(p));
    }

    template <typename ByteVec>
    static bool ReadFileHeaderLeAt(const ByteVec& bytes, size_t offset, FileHeader& out)
    {
        if (offset + 16 > bytes.size()) return false;
        const uint8_t* p = bytes.data() + offset;
        out.magic = ReadLe32(p + 0);
        out.version = ReadLe16(p + 4);
        out.reserved = ReadLe16(p + 6);
        out.segmentId = ReadLe32(p + 8);
        out.payloadBytes = ReadLe32(p + 12);
        return true;
    }

    template <typename ByteVec>
    static bool ReadGeoHeaderLeAt(const ByteVec& bytes, size_t offset, GeoHeader& out)
    {
        if (offset + 8 > bytes.size()) return false;
        const uint8_t* p = bytes.data() + offset;
        out.vertexCount = ReadLe32(p + 0);
        out.faceCount = ReadLe32(p + 4);
        return true;
    }

    template <typename ByteVec>
    static bool ReadMatHeaderLeAt(const ByteVec& bytes, size_t offset, MatHeader& out)
    {
        if (offset + 4 > bytes.size()) return false;
        const uint8_t* p = bytes.data() + offset;
        out.faceCount = ReadLe32(p + 0);
        return true;
    }

    template <typename ByteVec>
    static bool ReadGeoVertexLeAt(const ByteVec& bytes, size_t offset, GeoVertex& out)
    {
        if (offset + 12 > bytes.size()) return false;
        const uint8_t* p = bytes.data() + offset;
        out.x = ReadLeI32(p + 0);
        out.y = ReadLeI32(p + 4);
        out.z = ReadLeI32(p + 8);
        return true;
    }

    template <typename ByteVec>
    static bool ReadGeoFaceLeAt(const ByteVec& bytes, size_t offset, GeoFace& out)
    {
        if (offset + sizeof(GeoFace) > bytes.size()) return false;
        const uint8_t* p = bytes.data() + offset;
        for (size_t i = 0; i < 4; ++i) out.vertex[i] = ReadLe16(p + (i * 2));
        for (size_t i = 0; i < 4; ++i) out.u[i] = ReadLeI16(p + 8 + (i * 2));
        for (size_t i = 0; i < 4; ++i) out.v[i] = ReadLeI16(p + 16 + (i * 2));
        out.kind = p[24];
        out.reservedA = p[25];
        out.reservedB = ReadLe16(p + 26);
        return true;
    }

    template <typename ByteVec>
    static bool ReadMatFaceBindingLeAt(const ByteVec& bytes, size_t offset, MatFaceBinding& out)
    {
        if (offset + 4 > bytes.size()) return false;
        const uint8_t* p = bytes.data() + offset;
        out.materialId = ReadLe32(p + 0);
        return true;
    }

    static bool LoadFirstExistingFromCd(const char* const* candidates, size_t count, Blob& out)
    {
        out = {};
        for (size_t i = 0; i < count; ++i)
        {
            SRL::Cd::File f(candidates[i]);
            if (!f.Exists() || f.Size.Bytes <= 0) continue;
            if (!f.Open()) continue;

            const size_t sz = static_cast<size_t>(f.Size.Bytes);
            if (sz == 0) continue;

            TrackLowWorkVectorBase<uint8_t> tmp(sz);
            const int32_t read = f.Read(static_cast<int32_t>(sz), tmp.data());
            if (read <= 0) continue;
            if (static_cast<size_t>(read) > sz) continue;

            tmp.resize(static_cast<size_t>(read));
            out.bytes = std::move(tmp);
            out.loaded = true;
            out.size = out.bytes.size();
            return true;
        }
        return false;
    }

    template <typename T, typename ByteVec>
    static bool ReadPodAt(const ByteVec& bytes, size_t offset, T& out)
    {
        if (offset + sizeof(T) > bytes.size()) return false;
        ::memcpy(&out, bytes.data() + offset, sizeof(T));
        return true;
    }

    struct GeoView
    {
        bool valid = false;
        FileHeader file{};
        GeoHeader header{};
        size_t vertexOffset = 0;
        size_t faceOffset = 0;
    };

    struct MatView
    {
        bool valid = false;
        FileHeader file{};
        MatHeader header{};
        size_t bindingOffset = 0;
    };

    static bool ParseGeo(const Blob& blob, GeoView& out)
    {
        out = {};
        if (!blob.loaded || blob.bytes.empty()) return false;

        size_t off = 0;
        if (!ReadFileHeaderLeAt(blob.bytes, off, out.file)) return false;
        off += sizeof(FileHeader);

        if (out.file.magic != kGeoMagic) return false;
        if (out.file.version != 1) return false;
        if (out.file.payloadBytes + sizeof(FileHeader) > blob.bytes.size()) return false;

        if (!ReadGeoHeaderLeAt(blob.bytes, off, out.header)) return false;
        off += sizeof(GeoHeader);

        const size_t vertexBytes = static_cast<size_t>(out.header.vertexCount) * sizeof(GeoVertex);
        const size_t faceBytes = static_cast<size_t>(out.header.faceCount) * sizeof(GeoFace);
        if (off + vertexBytes + faceBytes > blob.bytes.size()) return false;

        out.vertexOffset = off;
        out.faceOffset = off + vertexBytes;
        out.valid = true;
        return true;
    }

    static bool ParseMat(const Blob& blob, MatView& out)
    {
        out = {};
        if (!blob.loaded || blob.bytes.empty()) return false;

        size_t off = 0;
        if (!ReadFileHeaderLeAt(blob.bytes, off, out.file)) return false;
        off += sizeof(FileHeader);

        if (out.file.magic != kMatMagic) return false;
        if (out.file.version != 1) return false;
        if (out.file.payloadBytes + sizeof(FileHeader) > blob.bytes.size()) return false;

        if (!ReadMatHeaderLeAt(blob.bytes, off, out.header)) return false;
        off += sizeof(MatHeader);

        const size_t bindBytes = static_cast<size_t>(out.header.faceCount) * sizeof(MatFaceBinding);
        if (off + bindBytes > blob.bytes.size()) return false;

        out.bindingOffset = off;
        out.valid = true;
        return true;
    }

    static bool ValidateGeoMatPair(const GeoView& geo, const MatView& mat)
    {
        if (!geo.valid || !mat.valid) return false;
        if (geo.file.segmentId != mat.file.segmentId) return false;
        if (geo.header.faceCount != mat.header.faceCount) return false;
        return true;
    }
};
} // namespace SegmentComponent
