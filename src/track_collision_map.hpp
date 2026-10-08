#pragma once

#include <cstddef>
#include <cstdint>

namespace TrackCollisionMap
{
static constexpr uint32_t kMagic = 0x314C4354u; // "TCL1" little-endian
static constexpr uint16_t kVersion = 1u;
static constexpr uint16_t kHeaderSize = 34u;
static constexpr uint16_t kDirectorySize = 56u;
static constexpr uint16_t kVertexSize = 12u;
static constexpr uint16_t kGroundSize = 16u;
static constexpr uint16_t kWallSize = 36u;
static constexpr uint16_t kCellSize = 8u;
static constexpr uint16_t kGridDim = 4u;

struct SegmentView
{
    uint16_t segmentId = 0u;
    uint16_t vertexCount = 0u;
    uint16_t groundCount = 0u;
    uint16_t wallCount = 0u;
    uint16_t gridDim = 0u;
    int32_t minXRaw = 0;
    int32_t maxXRaw = 0;
    int32_t minZRaw = 0;
    int32_t maxZRaw = 0;
    uint32_t verticesOffset = 0u;
    uint32_t groundOffset = 0u;
    uint32_t wallOffset = 0u;
    uint32_t groundCellsOffset = 0u;
    uint32_t wallCellsOffset = 0u;
};

struct Vertex
{
    int32_t xRaw = 0;
    int32_t yRaw = 0;
    int32_t zRaw = 0;
};

struct GroundFace
{
    uint16_t vertices[4]{};
    uint16_t familyId = 0u;
    uint8_t surfaceType = 0u;
    uint8_t vertexCount = 0u;
    uint16_t flags = 0u;
    uint16_t sourceFaceIndex = 0u;
};

struct Wall
{
    int32_t axRaw = 0;
    int32_t azRaw = 0;
    int32_t bxRaw = 0;
    int32_t bzRaw = 0;
    int32_t minYRaw = 0;
    int32_t maxYRaw = 0;
    int32_t nxRaw = 0;
    int32_t nzRaw = 0;
    uint16_t familyId = 0u;
    uint16_t sourceFaceIndex = 0u;
};

struct CellView
{
    uint32_t indicesOffset = 0u;
    uint16_t count = 0u;
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
        if (ReadLe32(data_) != kMagic || ReadLe16(data_ + 4u) != kVersion ||
            ReadLe16(data_ + 6u) != kHeaderSize || ReadLe16(data_ + 8u) != kDirectorySize ||
            ReadLe16(data_ + 10u) != kVertexSize || ReadLe16(data_ + 12u) != kGroundSize ||
            ReadLe16(data_ + 14u) != kWallSize || ReadLe16(data_ + 16u) != kCellSize ||
            ReadLe16(data_ + 18u) != kGridDim)
        {
            return false;
        }
        segmentCount_ = ReadLe16(data_ + 20u);
        totalGroundCount_ = ReadLe32(data_ + 22u);
        totalWallCount_ = ReadLe32(data_ + 26u);
        directoryOffset_ = ReadLe32(data_ + 30u);
        if (segmentCount_ == 0u || directoryOffset_ < kHeaderSize) return false;
        const size_t end = static_cast<size_t>(directoryOffset_) +
                           static_cast<size_t>(segmentCount_) * kDirectorySize;
        if (end > size_) return false;
        valid_ = true;
        return true;
    }

    bool Valid() const { return valid_; }
    uint16_t SegmentCount() const { return segmentCount_; }
    uint32_t TotalGroundCount() const { return totalGroundCount_; }
    uint32_t TotalWallCount() const { return totalWallCount_; }

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
        return low < segmentCount_ && ReadSegmentAt(low, out) && out.segmentId == segmentId;
    }

    bool ReadVertex(const SegmentView& segment, uint16_t index, Vertex& out) const
    {
        out = {};
        if (!valid_ || index >= segment.vertexCount) return false;
        const size_t offset = static_cast<size_t>(segment.verticesOffset) +
                              static_cast<size_t>(index) * kVertexSize;
        if (offset + kVertexSize > size_) return false;
        out.xRaw = ReadLeI32(data_ + offset);
        out.yRaw = ReadLeI32(data_ + offset + 4u);
        out.zRaw = ReadLeI32(data_ + offset + 8u);
        return true;
    }

    bool ReadGround(const SegmentView& segment, uint16_t index, GroundFace& out) const
    {
        out = {};
        if (!valid_ || index >= segment.groundCount) return false;
        const size_t offset = static_cast<size_t>(segment.groundOffset) +
                              static_cast<size_t>(index) * kGroundSize;
        if (offset + kGroundSize > size_) return false;
        for (size_t i = 0u; i < 4u; ++i) out.vertices[i] = ReadLe16(data_ + offset + i * 2u);
        out.familyId = ReadLe16(data_ + offset + 8u);
        out.surfaceType = data_[offset + 10u];
        out.vertexCount = data_[offset + 11u];
        out.flags = ReadLe16(data_ + offset + 12u);
        out.sourceFaceIndex = ReadLe16(data_ + offset + 14u);
        return out.vertexCount >= 3u && out.vertexCount <= 4u;
    }

    bool ReadWall(const SegmentView& segment, uint16_t index, Wall& out) const
    {
        out = {};
        if (!valid_ || index >= segment.wallCount) return false;
        const size_t offset = static_cast<size_t>(segment.wallOffset) +
                              static_cast<size_t>(index) * kWallSize;
        if (offset + kWallSize > size_) return false;
        out.axRaw = ReadLeI32(data_ + offset);
        out.azRaw = ReadLeI32(data_ + offset + 4u);
        out.bxRaw = ReadLeI32(data_ + offset + 8u);
        out.bzRaw = ReadLeI32(data_ + offset + 12u);
        out.minYRaw = ReadLeI32(data_ + offset + 16u);
        out.maxYRaw = ReadLeI32(data_ + offset + 20u);
        out.nxRaw = ReadLeI32(data_ + offset + 24u);
        out.nzRaw = ReadLeI32(data_ + offset + 28u);
        out.familyId = ReadLe16(data_ + offset + 32u);
        out.sourceFaceIndex = ReadLe16(data_ + offset + 34u);
        return true;
    }

    bool FindGroundCell(const SegmentView& segment, int64_t xRaw, int64_t zRaw, CellView& out) const
    {
        return FindCell(segment, segment.groundCellsOffset, xRaw, zRaw, out);
    }

    bool FindWallCell(const SegmentView& segment, int64_t xRaw, int64_t zRaw, CellView& out) const
    {
        return FindCell(segment, segment.wallCellsOffset, xRaw, zRaw, out);
    }

    // Clamp to the segment AABB then resolve the 4x4 cell — used when the sample
    // sits just outside the bounds or when padding by collision radius.
    bool FindWallCellClamped(const SegmentView& segment, int64_t xRaw, int64_t zRaw, CellView& out) const
    {
        if (!valid_ || segment.gridDim != kGridDim) return false;
        if (xRaw < segment.minXRaw) xRaw = segment.minXRaw;
        if (xRaw > segment.maxXRaw) xRaw = segment.maxXRaw;
        if (zRaw < segment.minZRaw) zRaw = segment.minZRaw;
        if (zRaw > segment.maxZRaw) zRaw = segment.maxZRaw;
        return FindCell(segment, segment.wallCellsOffset, xRaw, zRaw, out);
    }

    bool ResolveWallCellCoords(const SegmentView& segment,
                               int64_t xRaw,
                               int64_t zRaw,
                               size_t& outCellX,
                               size_t& outCellZ) const
    {
        outCellX = 0u;
        outCellZ = 0u;
        if (!valid_ || segment.gridDim != kGridDim) return false;
        if (xRaw < segment.minXRaw) xRaw = segment.minXRaw;
        if (xRaw > segment.maxXRaw) xRaw = segment.maxXRaw;
        if (zRaw < segment.minZRaw) zRaw = segment.minZRaw;
        if (zRaw > segment.maxZRaw) zRaw = segment.maxZRaw;
        const int64_t spanX = static_cast<int64_t>(segment.maxXRaw) - segment.minXRaw + 1;
        const int64_t spanZ = static_cast<int64_t>(segment.maxZRaw) - segment.minZRaw + 1;
        if (spanX <= 0 || spanZ <= 0) return false;
        outCellX = static_cast<size_t>(((xRaw - segment.minXRaw) * kGridDim) / spanX);
        outCellZ = static_cast<size_t>(((zRaw - segment.minZRaw) * kGridDim) / spanZ);
        if (outCellX >= kGridDim) outCellX = kGridDim - 1u;
        if (outCellZ >= kGridDim) outCellZ = kGridDim - 1u;
        return true;
    }

    bool FindWallCellAt(const SegmentView& segment,
                        size_t cellX,
                        size_t cellZ,
                        CellView& out) const
    {
        out = {};
        if (!valid_ || segment.gridDim != kGridDim ||
            cellX >= kGridDim || cellZ >= kGridDim)
        {
            return false;
        }
        const size_t offset = static_cast<size_t>(segment.wallCellsOffset) +
                              (cellZ * kGridDim + cellX) * kCellSize;
        if (offset + kCellSize > size_) return false;
        out.indicesOffset = ReadLe32(data_ + offset);
        out.count = ReadLe16(data_ + offset + 4u);
        return static_cast<size_t>(out.indicesOffset) + static_cast<size_t>(out.count) * 2u <= size_;
    }

    bool ReadCellIndex(const CellView& cell, uint16_t index, uint16_t& out) const
    {
        out = 0u;
        if (!valid_ || index >= cell.count) return false;
        const size_t offset = static_cast<size_t>(cell.indicesOffset) + static_cast<size_t>(index) * 2u;
        if (offset + 2u > size_) return false;
        out = ReadLe16(data_ + offset);
        return true;
    }

private:
    static uint16_t ReadLe16(const uint8_t* p)
    {
        return static_cast<uint16_t>(p[0]) |
               static_cast<uint16_t>(static_cast<uint16_t>(p[1]) << 8u);
    }

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
        out.segmentId = ReadLe16(p);
        out.vertexCount = ReadLe16(p + 2u);
        out.groundCount = ReadLe16(p + 4u);
        out.wallCount = ReadLe16(p + 6u);
        out.gridDim = ReadLe16(p + 8u);
        out.minXRaw = ReadLeI32(p + 12u);
        out.maxXRaw = ReadLeI32(p + 16u);
        out.minZRaw = ReadLeI32(p + 20u);
        out.maxZRaw = ReadLeI32(p + 24u);
        out.verticesOffset = ReadLe32(p + 28u);
        out.groundOffset = ReadLe32(p + 32u);
        out.wallOffset = ReadLe32(p + 36u);
        out.groundCellsOffset = ReadLe32(p + 40u);
        out.wallCellsOffset = ReadLe32(p + 48u);
        return out.gridDim == kGridDim &&
               static_cast<size_t>(out.verticesOffset) + static_cast<size_t>(out.vertexCount) * kVertexSize <= size_ &&
               static_cast<size_t>(out.groundOffset) + static_cast<size_t>(out.groundCount) * kGroundSize <= size_ &&
               static_cast<size_t>(out.wallOffset) + static_cast<size_t>(out.wallCount) * kWallSize <= size_ &&
               static_cast<size_t>(out.groundCellsOffset) + kGridDim * kGridDim * kCellSize <= size_ &&
               static_cast<size_t>(out.wallCellsOffset) + kGridDim * kGridDim * kCellSize <= size_;
    }

    bool FindCell(const SegmentView& segment, uint32_t tableOffset,
                  int64_t xRaw, int64_t zRaw, CellView& out) const
    {
        out = {};
        if (!valid_ || segment.gridDim != kGridDim ||
            xRaw < segment.minXRaw || xRaw > segment.maxXRaw ||
            zRaw < segment.minZRaw || zRaw > segment.maxZRaw)
        {
            return false;
        }
        const int64_t spanX = static_cast<int64_t>(segment.maxXRaw) - segment.minXRaw + 1;
        const int64_t spanZ = static_cast<int64_t>(segment.maxZRaw) - segment.minZRaw + 1;
        size_t cellX = static_cast<size_t>(((xRaw - segment.minXRaw) * kGridDim) / spanX);
        size_t cellZ = static_cast<size_t>(((zRaw - segment.minZRaw) * kGridDim) / spanZ);
        if (cellX >= kGridDim) cellX = kGridDim - 1u;
        if (cellZ >= kGridDim) cellZ = kGridDim - 1u;
        const size_t offset = static_cast<size_t>(tableOffset) +
                              (cellZ * kGridDim + cellX) * kCellSize;
        if (offset + kCellSize > size_) return false;
        out.indicesOffset = ReadLe32(data_ + offset);
        out.count = ReadLe16(data_ + offset + 4u);
        return static_cast<size_t>(out.indicesOffset) + static_cast<size_t>(out.count) * 2u <= size_;
    }

    const uint8_t* data_ = nullptr;
    size_t size_ = 0u;
    uint32_t directoryOffset_ = 0u;
    uint32_t totalGroundCount_ = 0u;
    uint32_t totalWallCount_ = 0u;
    uint16_t segmentCount_ = 0u;
    bool valid_ = false;
};
} // namespace TrackCollisionMap
