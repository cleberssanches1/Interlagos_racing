#pragma once

#include <cstddef>
#include <cstdint>

namespace FaceSurfaceMap
{
static constexpr uint32_t kMagic = 0x314D5346u; // "FSM1" little-endian
static constexpr uint16_t kVersion = 1u;
static constexpr uint16_t kHeaderSize = 20u;
static constexpr uint16_t kDirectorySize = 24u;
static constexpr uint16_t kRecordSize = 12u;

struct SegmentView
{
    uint16_t segmentId = 0u;
    uint16_t faceCount = 0u;
    uint16_t recordCount = 0u;
    uint8_t quantShift = 0u;
    uint8_t flags = 0u;
    int32_t originXRaw = 0;
    int32_t originZRaw = 0;
    uint32_t recordsOffset = 0u;
};

struct FaceRecord
{
    uint16_t faceIndex = 0u;
    int16_t minX = 0;
    int16_t maxX = 0;
    int16_t minZ = 0;
    int16_t maxZ = 0;
    uint8_t surfaceType = 0u;
    uint8_t flags = 0u;
};

class View
{
public:
    View() = default;
    View(const void* data, size_t size) { Reset(data, size); }

    bool Reset(const void* data, size_t size)
    {
        data_ = static_cast<const uint8_t*>(data);
        size_ = size;
        valid_ = false;
        segmentCount_ = 0u;
        directoryOffset_ = 0u;
        if (!data_ || size_ < kHeaderSize) return false;
        const uint32_t magic = ReadLe32(data_ + 0u);
        const uint16_t version = ReadLe16(data_ + 4u);
        const uint16_t headerSize = ReadLe16(data_ + 6u);
        segmentCount_ = ReadLe16(data_ + 8u);
        const uint16_t recordSize = ReadLe16(data_ + 10u);
        directoryOffset_ = ReadLe32(data_ + 16u);
        if (magic != kMagic || version != kVersion || headerSize != kHeaderSize ||
            recordSize != kRecordSize || segmentCount_ == 0u ||
            directoryOffset_ < kHeaderSize)
        {
            return false;
        }
        const size_t directoryEnd = static_cast<size_t>(directoryOffset_) +
                                    static_cast<size_t>(segmentCount_) * kDirectorySize;
        if (directoryEnd > size_) return false;
        valid_ = true;
        return true;
    }

    bool Valid() const { return valid_; }

    bool FindSegment(uint16_t segmentId, SegmentView& out) const
    {
        out = {};
        if (!valid_ || segmentId == 0u) return false;
        size_t low = 0u;
        size_t high = segmentCount_;
        while (low < high)
        {
            const size_t middle = low + ((high - low) / 2u);
            SegmentView candidate{};
            if (!ReadSegmentAt(middle, candidate)) return false;
            if (candidate.segmentId < segmentId) low = middle + 1u;
            else high = middle;
        }
        if (low >= segmentCount_ || !ReadSegmentAt(low, out) || out.segmentId != segmentId)
        {
            out = {};
            return false;
        }
        return true;
    }

    bool ReadRecord(const SegmentView& segment, uint16_t index, FaceRecord& out) const
    {
        out = {};
        if (!valid_ || index >= segment.recordCount) return false;
        const size_t offset = static_cast<size_t>(segment.recordsOffset) +
                              static_cast<size_t>(index) * kRecordSize;
        if (offset + kRecordSize > size_) return false;
        const uint8_t* p = data_ + offset;
        out.faceIndex = ReadLe16(p + 0u);
        out.minX = ReadLeI16(p + 2u);
        out.maxX = ReadLeI16(p + 4u);
        out.minZ = ReadLeI16(p + 6u);
        out.maxZ = ReadLeI16(p + 8u);
        out.surfaceType = p[10u];
        out.flags = p[11u];
        return true;
    }

    static bool ContainsXZ(const SegmentView& segment,
                           const FaceRecord& record,
                           int64_t modelXRaw,
                           int64_t modelZRaw)
    {
        if (segment.quantShift > 30u) return false;
        const int64_t quantum = static_cast<int64_t>(1u) << segment.quantShift;
        const int64_t minX = static_cast<int64_t>(segment.originXRaw) +
                             static_cast<int64_t>(record.minX) * quantum;
        const int64_t maxX = static_cast<int64_t>(segment.originXRaw) +
                             static_cast<int64_t>(record.maxX) * quantum;
        const int64_t minZ = static_cast<int64_t>(segment.originZRaw) +
                             static_cast<int64_t>(record.minZ) * quantum;
        const int64_t maxZ = static_cast<int64_t>(segment.originZRaw) +
                             static_cast<int64_t>(record.maxZ) * quantum;
        return modelXRaw >= minX && modelXRaw <= maxX &&
               modelZRaw >= minZ && modelZRaw <= maxZ;
    }

private:
    static uint16_t ReadLe16(const uint8_t* p)
    {
        return static_cast<uint16_t>(p[0]) |
               static_cast<uint16_t>(static_cast<uint16_t>(p[1]) << 8u);
    }

    static int16_t ReadLeI16(const uint8_t* p) { return static_cast<int16_t>(ReadLe16(p)); }

    static uint32_t ReadLe32(const uint8_t* p)
    {
        return static_cast<uint32_t>(p[0]) |
               (static_cast<uint32_t>(p[1]) << 8u) |
               (static_cast<uint32_t>(p[2]) << 16u) |
               (static_cast<uint32_t>(p[3]) << 24u);
    }

    static int32_t ReadLeI32(const uint8_t* p) { return static_cast<int32_t>(ReadLe32(p)); }

    bool ReadSegmentAt(size_t index, SegmentView& out) const
    {
        if (!valid_ || index >= segmentCount_) return false;
        const size_t offset = static_cast<size_t>(directoryOffset_) + index * kDirectorySize;
        if (offset + kDirectorySize > size_) return false;
        const uint8_t* p = data_ + offset;
        out.segmentId = ReadLe16(p + 0u);
        out.faceCount = ReadLe16(p + 2u);
        out.recordCount = ReadLe16(p + 4u);
        out.quantShift = p[6u];
        out.flags = p[7u];
        out.originXRaw = ReadLeI32(p + 8u);
        out.originZRaw = ReadLeI32(p + 12u);
        out.recordsOffset = ReadLe32(p + 16u);
        const size_t recordsEnd = static_cast<size_t>(out.recordsOffset) +
                                  static_cast<size_t>(out.recordCount) * kRecordSize;
        return out.quantShift <= 30u && recordsEnd <= size_;
    }

    const uint8_t* data_ = nullptr;
    size_t size_ = 0u;
    uint32_t directoryOffset_ = 0u;
    uint16_t segmentCount_ = 0u;
    bool valid_ = false;
};
} // namespace FaceSurfaceMap
