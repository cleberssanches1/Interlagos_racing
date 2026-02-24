#pragma once

#include <cstddef>
#include <cstdint>
#include <string.h>
#include <vector>

#define DOXYGEN 1
#include <srl.hpp>
#undef DOXYGEN

#include "segment_component_format.hpp"

namespace SegmentComponent
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

            std::vector<uint8_t> tmp(sz);
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

    template <typename T>
    static bool ReadPodAt(const std::vector<uint8_t>& bytes, size_t offset, T& out)
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
        if (!ReadPodAt(blob.bytes, off, out.file)) return false;
        off += sizeof(FileHeader);

        if (out.file.magic != kGeoMagic) return false;
        if (out.file.version != 1) return false;
        if (out.file.payloadBytes + sizeof(FileHeader) > blob.bytes.size()) return false;

        if (!ReadPodAt(blob.bytes, off, out.header)) return false;
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
        if (!ReadPodAt(blob.bytes, off, out.file)) return false;
        off += sizeof(FileHeader);

        if (out.file.magic != kMatMagic) return false;
        if (out.file.version != 1) return false;
        if (out.file.payloadBytes + sizeof(FileHeader) > blob.bytes.size()) return false;

        if (!ReadPodAt(blob.bytes, off, out.header)) return false;
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
