#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace PathNya
{
static constexpr size_t kMaxSupportedLines = 3u;

struct PointRaw
{
    int32_t xRaw = 0;
    int32_t yRaw = 0;
    int32_t zRaw = 0;
};

using PathLine = std::vector<PointRaw>;

struct ParseResult
{
    uint32_t version = 0u;
    uint32_t lineCount = 0u;
    bool bigEndian = true;
    std::array<PathLine, kMaxSupportedLines> lines{};
};

inline uint32_t ReadU32(const uint8_t* bytes, bool bigEndian)
{
    if (!bytes) return 0u;
    if (bigEndian)
    {
        return (static_cast<uint32_t>(bytes[0]) << 24) |
               (static_cast<uint32_t>(bytes[1]) << 16) |
               (static_cast<uint32_t>(bytes[2]) << 8) |
               static_cast<uint32_t>(bytes[3]);
    }

    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) |
           (static_cast<uint32_t>(bytes[3]) << 24);
}

inline int32_t ReadS32(const uint8_t* bytes, bool bigEndian)
{
    return static_cast<int32_t>(ReadU32(bytes, bigEndian));
}

inline void Clear(ParseResult& ioResult)
{
    ioResult.version = 0u;
    ioResult.lineCount = 0u;
    ioResult.bigEndian = true;
    for (size_t i = 0; i < ioResult.lines.size(); ++i)
    {
        ioResult.lines[i].clear();
    }
}

inline bool DetectEndian(const uint8_t* bytes,
                         size_t size,
                         bool& outBigEndian,
                         uint32_t& outVersion,
                         uint32_t& outLineCount)
{
    if (!bytes || size < 8u) return false;

    const uint32_t beVersion = ReadU32(bytes, true);
    const uint32_t beLineCount = ReadU32(bytes + 4u, true);
    if (beVersion == 1u && beLineCount > 0u && beLineCount <= 16u)
    {
        outBigEndian = true;
        outVersion = beVersion;
        outLineCount = beLineCount;
        return true;
    }

    const uint32_t leVersion = ReadU32(bytes, false);
    const uint32_t leLineCount = ReadU32(bytes + 4u, false);
    if (leVersion == 1u && leLineCount > 0u && leLineCount <= 16u)
    {
        outBigEndian = false;
        outVersion = leVersion;
        outLineCount = leLineCount;
        return true;
    }

    return false;
}

inline bool TryParseWithDescriptorBase(const uint8_t* bytes,
                                       size_t size,
                                       bool bigEndian,
                                       uint32_t lineCount,
                                       size_t descriptorBase,
                                       std::array<PathLine, kMaxSupportedLines>& outLines,
                                       size_t& outTotalPoints)
{
    outTotalPoints = 0u;
    for (size_t i = 0; i < outLines.size(); ++i) outLines[i].clear();

    if (!bytes) return false;
    if (lineCount == 0u) return false;
    if (size < descriptorBase) return false;
    if (size - descriptorBase < (static_cast<size_t>(lineCount) * 8u)) return false;

    for (uint32_t lineIndex = 0; lineIndex < lineCount; ++lineIndex)
    {
        const size_t descOffset = descriptorBase + static_cast<size_t>(lineIndex) * 8u;
        const uint32_t pointCount = ReadU32(bytes + descOffset, bigEndian);
        const uint32_t pointsOffset = ReadU32(bytes + descOffset + 4u, bigEndian);

        if (lineIndex >= outLines.size())
        {
            if (pointCount == 0u) continue;
            if (pointsOffset == 0u) return false;
            if (pointsOffset > size) return false;
            const size_t bytesAvailable = size - static_cast<size_t>(pointsOffset);
            if (bytesAvailable < static_cast<size_t>(pointCount) * 12u) return false;
            continue;
        }

        PathLine& line = outLines[static_cast<size_t>(lineIndex)];
        if (pointCount == 0u)
        {
            line.clear();
            continue;
        }

        if (pointsOffset == 0u) return false;
        if (pointsOffset > size) return false;
        const size_t bytesAvailable = size - static_cast<size_t>(pointsOffset);
        if (bytesAvailable < static_cast<size_t>(pointCount) * 12u) return false;

        line.reserve(pointCount);
        for (uint32_t pointIndex = 0; pointIndex < pointCount; ++pointIndex)
        {
            const size_t pointOffset =
                static_cast<size_t>(pointsOffset) + static_cast<size_t>(pointIndex) * 12u;
            PointRaw point{};
            point.xRaw = ReadS32(bytes + pointOffset, bigEndian);
            point.yRaw = ReadS32(bytes + pointOffset + 4u, bigEndian);
            point.zRaw = ReadS32(bytes + pointOffset + 8u, bigEndian);
            line.push_back(point);
            ++outTotalPoints;
        }
    }

    return true;
}

inline bool Parse(const uint8_t* bytes, size_t size, ParseResult& outResult)
{
    Clear(outResult);
    if (!bytes || size < 8u) return false;

    bool bigEndian = true;
    uint32_t version = 0u;
    uint32_t lineCount = 0u;
    if (!DetectEndian(bytes, size, bigEndian, version, lineCount)) return false;

    std::array<PathLine, kMaxSupportedLines> linesBase8{};
    std::array<PathLine, kMaxSupportedLines> linesBase12{};
    size_t totalPointsBase8 = 0u;
    size_t totalPointsBase12 = 0u;
    const bool parsedBase8 = TryParseWithDescriptorBase(
        bytes,
        size,
        bigEndian,
        lineCount,
        8u,
        linesBase8,
        totalPointsBase8);
    const bool parsedBase12 = TryParseWithDescriptorBase(
        bytes,
        size,
        bigEndian,
        lineCount,
        12u,
        linesBase12,
        totalPointsBase12);

    if (!parsedBase8 && !parsedBase12) return false;

    outResult.version = version;
    outResult.lineCount = lineCount;
    outResult.bigEndian = bigEndian;
    outResult.lines = (parsedBase12 && totalPointsBase12 > totalPointsBase8)
        ? linesBase12
        : linesBase8;
    return true;
}

inline bool HasUsableLine(const ParseResult& result,
                          size_t lineIndex,
                          size_t minimumPoints = 2u)
{
    if (lineIndex >= result.lines.size()) return false;
    return result.lines[lineIndex].size() >= minimumPoints;
}
} // namespace PathNya
